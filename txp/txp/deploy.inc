<?php
/**
 * @file
 * Post-deploy SQL rewrite + security scrub for a deployed TXP site.
 *
 * Runs after clone/migrate/rename/restore extraction + DB import, against the
 * TARGET site's DB with the target creds. The caller names the class in the
 * deploy options ('deploy_class'); the classes are deliberately treated
 * differently (docs/analysis/txp-clone-surface.md §2):
 *
 *  - NEW OWNER ('clone'): the copy must not inherit the source's secrets, its
 *    sessions, or its people. blog_uid is REGENERATED (it is both the feed
 *    GUID seed and the CSRF salt), blog_mail_uid reset, the whole mail block
 *    returned to Textpattern's shipped defaults (a credential must never
 *    travel to another site/owner), txp_users.nonce rotated (a live session
 *    cookie from the source authenticates on the copy otherwise, whenever the
 *    two share a cookie domain), and every stored contact address re-pointed
 *    at the copy's own admin address.
 *  - IDENTITY-PRESERVING, same site ('restore', same URI): blog_uid is KEPT --
 *    regenerating it would churn every feed GUID for no security gain, since
 *    there is no second site to share the salt with -- and nothing about the
 *    site's contact data is touched: it is the same site, coming back.
 *  - IDENTITY-PRESERVING, new name ('rename' and 'upgrade'): the publication,
 *    its owner and its people are unchanged; only the name or the codebase
 *    under it moves. Both get the session rotation (a cookie minted against
 *    the old name must not authenticate) and the truncations, and NEITHER gets
 *    blog_uid regeneration (feed GUIDs must survive a rename and an upgrade)
 *    nor the contact rewrite (there is no new owner to re-point mail at).
 *    They are named here rather than left to fall through the default because
 *    silence about a class is how one of them later acquires the wrong half of
 *    a copy's treatment.
 *  - Any other class is treated as a copy: that is the safe way round, since a
 *    copy treated as a restore keeps the source's secrets.
 *
 * EVERY class: re-point every stored path/URL pref (the shared
 * _provision_txp_repoint_uri(), which the rename command calls too -- two
 * writers of the same six rows drifting apart is how one of them silently
 * stops moving a row), normalise production_status to 'live' (a site
 * restored/cloned from a debug-mode source would otherwise serve trace + SQL
 * detail publicly), and truncate txp_token / txp_log / txp_discuss_nonce
 * (stale on a restore, live-and-dangerous on a copy).
 *
 * Ordering: the security half runs FIRST. A connection loss mid-sequence then
 * leaves a copy that is scrubbed but not yet re-pointed (inconvenient), never
 * one that is re-pointed and still carrying the source's secrets (dangerous).
 * TRUNCATE is DDL and self-commits, so a wrapping transaction would be theatre.
 */

$uri = d()->uri;
$site_path = d()->site_path;

// The deploy class decides what may be mutated, so it is named outright by the
// calling hook rather than inferred here. The fallback keeps every older or
// contributed caller working: is_restore first (what the restore hook used to
// send alone), then the old_uri comparison as the belt for a caller that sends
// neither. Anything that is not a restore and is not explicitly some other
// class is a copy, which is the safe way round: a copy treated as a restore
// keeps the source's secrets.
$old_uri = drush_get_option('old_uri', '');
$deploy_class = drush_get_option('deploy_class', '');
if (!is_string($deploy_class) || $deploy_class === '') {
  $deploy_class = (drush_get_option('is_restore', FALSE) || ($old_uri !== '' && $old_uri === $uri)) ? 'restore' : 'clone';
}
$is_restore = ($deploy_class === 'restore');
$is_clone = ($deploy_class === 'clone');
// The two classes that move a site without changing who owns it.
$is_same_identity = ($deploy_class === 'rename' || $deploy_class === 'upgrade');

// The copy's OWN contact address, resolved exactly the way the install engine
// resolves it (txp/install.inc): the address the front end sent when it is a
// valid one, else admin@<uri>. A clone both writes it onto the copy's user
// rows and uses it afterwards to tell an inherited address from its own.
$client_email = drush_get_option('client_email', '');
if (!is_string($client_email) || !filter_var($client_email, FILTER_VALIDATE_EMAIL)) {
  $client_email = 'admin@' . $uri;
}

$db_user = urldecode(drush_get_option('db_user', '', 'site'));
$db_passwd = urldecode(drush_get_option('db_passwd', '', 'site'));
$db_host = drush_get_option('db_host', 'localhost', 'site');
$db_port = drush_get_option('db_port', '', 'site');
$db_name = drush_get_option('db_name', '', 'site');

if ($db_name === '' || $db_user === '') {
  return drush_set_error('PROVISION_TXP_DEPLOY_FAILED',
    dt('No site database credentials in the deploy context; cannot rewrite the deployed site.'));
}

mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);
try {
  $mysqli = mysqli_init();
  $mysqli->real_connect($db_host, $db_user, $db_passwd, $db_name, $db_port ? (int) $db_port : 3306);
  $mysqli->set_charset('utf8mb4');

  $result = $mysqli->query("SHOW TABLES LIKE 'textpattern'");
  if ($result === FALSE || $result->num_rows !== 1) {
    $mysqli->close();
    return drush_set_error('PROVISION_TXP_DEPLOY_FAILED',
      dt('The deployed database carries no TXP schema (anchor table missing).'));
  }

  // ---- security half (first) ----
  _provision_txp_truncate_sessions($mysqli);

  $upd = $mysqli->prepare("UPDATE txp_prefs SET val = ? WHERE name = ?");
  $scrub = array();
  if ($is_clone) {
    // blog_uid: 32 hex like the installer mints. Feed GUID seed AND CSRF salt.
    $scrub['blog_uid'] = md5(uniqid(mt_rand(), TRUE));
    $scrub['blog_mail_uid'] = md5(uniqid(mt_rand(), TRUE)) . 'blog@example.com';
    // The whole mail block goes back to Textpattern's shipped defaults, not
    // just the password. Clearing smtp_pass alone left smtp_host and smtp_user
    // set, so the copy kept trying an AUTHENTICATED SMTP session with an empty
    // password and every mail it sent -- comment notifications, password
    // resets -- failed. With the defaults restored, txpMail falls back to the
    // platform mailer and sends as no-reply@<the copy's own siteurl>.
    $scrub['smtp_pass'] = '';
    $scrub['publisher_email'] = '';
    $scrub['smtp_from'] = '';
    $scrub['smtp_host'] = '';
    $scrub['smtp_port'] = '';
    $scrub['smtp_user'] = '';
    $scrub['smtp_sectype'] = '';
    $scrub['enhanced_email'] = '0';
  }
  foreach ($scrub as $name => $val) {
    $upd->bind_param('ss', $val, $name);
    $upd->execute();
  }

  if (!$is_restore) {
    // Invalidates login cookies that travelled with the database.
    $rotated = _provision_txp_rotate_nonces($mysqli);
    drush_log(dt('Rotated the session nonce of !n user row(s) on the copy.', array('!n' => $rotated)), 'ok');
  }

  if ($is_clone && _provision_txp_table_exists($mysqli, 'txp_users')) {
    // Textpattern core mails txp_users.email on every public comment and on
    // every password reset, so a copy that keeps the source's addresses mails
    // the source's people from a site they do not know they own. Re-point them
    // at the copy's OWN admin address (resolved above, exactly as the install
    // engine resolves it), so a copy behaves like a freshly installed site.
    $mail_stmt = $mysqli->prepare("UPDATE txp_users SET email = ? WHERE email <> ''");
    $mail_stmt->bind_param('s', $client_email);
    $mail_stmt->execute();
    $repointed = (int) $mail_stmt->affected_rows;
    $mail_stmt->close();
    // The COUNT only: a task log is read by whoever can see the task, and an
    // address is a client identifier.
    drush_log(dt('Pointed !n user email address(es) on the copy at its own admin address; publisher and SMTP settings reset to defaults.',
      array('!n' => $repointed)), 'ok');
  }

  $upd->close();

  // ---- re-point half ----
  // The six uri-bound and path-bound rows, through the helper the rename
  // command calls too: one writer of that set, so the two flows cannot drift.
  $repointed_prefs = _provision_txp_repoint_uri($mysqli, $uri, $site_path);
  $missing = $repointed_prefs['missing'];
  if (!empty($repointed_prefs['created'])) {
    drush_log(dt('These prefs rows were absent and have been recreated with core metadata and the deployed values: !list.',
      array('!list' => implode(', ', $repointed_prefs['created']))), 'ok');
  }

  // Which contact/mail metadata did the copy actually inherit? Probed here,
  // while the connection is open, so the advisory below can name what is
  // really set instead of firing on every single copy. Only a 'clone' is
  // probed: it is the one class that rewrites contact data, so it is the only
  // class where anything still set is a leftover. On a rename or an upgrade
  // the same addresses belong to the same owner and finding them is the
  // expected, healthy outcome -- an advisory row there would be noise.
  $inherited = array();
  if ($is_clone) {
    $probe = array('publisher_email', 'smtp_from', 'smtp_host', 'smtp_user');
    foreach ($probe as $pref_name) {
      $rs = $mysqli->query("SELECT val FROM txp_prefs WHERE name = '" . $mysqli->real_escape_string($pref_name) . "'");
      if ($rs !== FALSE && ($row = $rs->fetch_row()) && trim((string) $row[0]) !== '') {
        $inherited[] = $pref_name;
      }
    }
    if (_provision_txp_table_exists($mysqli, 'txp_users')) {
      // Anything that is neither empty nor the copy's own address is, by
      // definition, an address the copy did not get from this deploy.
      $rs = $mysqli->query("SELECT COUNT(*) FROM txp_users WHERE email <> '' AND email <> '" . $mysqli->real_escape_string($client_email) . "'");
      if ($rs !== FALSE && ($row = $rs->fetch_row()) && (int) $row[0] > 0) {
        $inherited[] = dt('!n user email address(es)', array('!n' => (int) $row[0]));
      }
    }
  }

  if (!empty($missing)) {
    drush_log(dt('These prefs rows could be neither updated nor created: !list. TXP will recreate them from defaults on the next admin login (siteurl defaults to the ADMIN host) -- fix them in the Preferences panel.',
      array('!list' => implode(', ', $missing))), 'warning');
  }

  $mysqli->close();
}
catch (Exception $e) {
  return drush_set_error('PROVISION_TXP_DEPLOY_FAILED',
    dt('Deploy rewrite/scrub failed for !uri: !msg', array('!uri' => $uri, '!msg' => $e->getMessage())));
}

if ($is_restore) {
  drush_log(dt('Restored TXP site re-pointed at !uri (identity preserved: blog_uid kept); tokens/logs/nonces cleared.', array('!uri' => $uri)), 'ok');
}
else {
  if ($is_clone) {
    drush_log(dt('Deployed TXP copy re-pointed at !uri; blog_uid + user nonces regenerated, contact data pointed at the copy, mail settings back to defaults, tokens/logs/nonces truncated.', array('!uri' => $uri)), 'ok');
  }
  elseif ($is_same_identity) {
    drush_log(dt('Deployed TXP site re-pointed at !uri (deploy class: !class — same publication, new name); user nonces rotated and tokens/logs/nonces truncated, while blog_uid, image ids and every contact address are kept.',
      array('!uri' => $uri, '!class' => $deploy_class)), 'ok');
  }
  else {
    drush_log(dt('Deployed TXP site re-pointed at !uri (deploy class: !class); user nonces rotated, tokens/logs/nonces truncated; identity and contact data left as they were.',
      array('!uri' => $uri, '!class' => $deploy_class)), 'ok');
  }
  // The copy's contact data used to be left alone on the argument that it is
  // content rather than a credential. It is neither: core mails those
  // addresses on every public comment and every password reset, so an
  // unattended copy mails the source's people. The block above now re-points
  // them at the copy's own admin address and returns the mail settings to
  // their shipped defaults, which is what a fresh install would carry -- so
  // this probe should come back empty and simply confirm it. It stays as the
  // defensive half: if something IS still set, say so in a colour that draws
  // the eye without claiming the deploy went wrong. A rename or an upgrade is
  // not probed at all (nothing was rewritten there), so it must not be given
  // this verdict either way.
  if ($is_clone) {
    if (!empty($inherited)) {
      drush_log(dt('NOTE: the copy still carries contact metadata from the source (!list). Review it in the Preferences and Users panels before the copy sends mail.',
        array('!list' => implode(', ', $inherited))), 'advisory');
    }
    else {
      drush_log(dt('Contact metadata check: the copy carries no publisher/SMTP address and no inherited user email address.'), 'ok');
    }
  }
}
