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
 *  - IDENTITY-PRESERVING ('restore', same URI): blog_uid is KEPT -- regenerating
 *    it would churn every feed GUID for no security gain, since there is no
 *    second site to share the salt with -- and nothing about the site's
 *    contact data is touched: it is the same site, coming back.
 *  - Any other class (a future 'rename', which changes the name but not the
 *    owner) gets the session rotation and the truncations, but neither the
 *    identity regeneration nor the contact rewrite.
 *
 * EVERY class: re-point every stored path/URL pref, normalise
 * production_status to 'live' (a site restored/cloned from a debug-mode source
 * would otherwise serve trace + SQL detail publicly), and truncate txp_token /
 * txp_log / txp_discuss_nonce (stale on a restore, live-and-dangerous on a
 * copy).
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

  // A table-existence probe that cannot fail OPEN: '_' is a single-character
  // LIKE wildcard, so a plain LIKE 'txp_token' can match more than one table
  // and a num_rows === 1 test would silently skip the TRUNCATE while the tail
  // of this engine reports success.
  $table_exists = function ($mysqli, $table) {
    $stmt = $mysqli->prepare("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ?");
    $stmt->bind_param('s', $table);
    $stmt->execute();
    $stmt->bind_result($count);
    $stmt->fetch();
    $stmt->close();
    return (int) $count === 1;
  };

  // ---- security half (first) ----
  foreach (array('txp_token', 'txp_log', 'txp_discuss_nonce') as $table) {
    if ($table_exists($mysqli, $table)) {
      $mysqli->query("TRUNCATE TABLE `" . $table . "`");
    }
  }

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

  if (!$is_restore && $table_exists($mysqli, 'txp_users')) {
    // Rotate every user's nonce: invalidates login cookies cloned along with
    // the database (txp_auth.php checks the stored nonce).
    $rotated = 0;
    $rs = $mysqli->query("SELECT user_id FROM txp_users");
    if ($rs !== FALSE) {
      $ids = array();
      while ($row = $rs->fetch_row()) {
        $ids[] = (int) $row[0];
      }
      $nonce_stmt = $mysqli->prepare("UPDATE txp_users SET nonce = ? WHERE user_id = ?");
      foreach ($ids as $id) {
        $nonce = md5(uniqid(mt_rand(), TRUE));
        $nonce_stmt->bind_param('si', $nonce, $id);
        $nonce_stmt->execute();
        $rotated++;
      }
      $nonce_stmt->close();
    }
    drush_log(dt('Rotated the session nonce of !n user row(s) on the copy.', array('!n' => $rotated)), 'ok');
  }

  if ($is_clone && $table_exists($mysqli, 'txp_users')) {
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

  // ---- re-point half ----
  $prefs = array(
    'siteurl' => $uri,
    'path_to_site' => $site_path . '/public',
    'file_base_path' => $site_path . '/public/files',
    'tempdir' => $site_path . '/tmp',
    // Source-bound and PHP is include()d from it on every public request.
    'plugin_cache_dir' => '',
    // Never inherit a debug/testing status: it publishes trace + SQL detail.
    'production_status' => 'live',
  );
  $missing = array();
  foreach ($prefs as $name => $val) {
    $upd->bind_param('ss', $val, $name);
    $upd->execute();
    // A missing pref row updates nothing and is NOT an error to mysqli; TXP's
    // checkPrefsIntegrity() would then recreate it from defaults on the next
    // admin login -- and the siteurl default is the ADMIN hostname, i.e. the
    // deployed site would start composing links to the wrong host. Collect it
    // and create it below rather than leaving it to that default.
    if ($mysqli->affected_rows === 0) {
      $rs = $mysqli->query("SELECT COUNT(*) FROM txp_prefs WHERE name = '" . $mysqli->real_escape_string($name) . "'");
      $row = ($rs !== FALSE) ? $rs->fetch_row() : array(0);
      if ((int) $row[0] === 0) {
        $missing[] = $name;
      }
    }
  }

  if (!empty($missing)) {
    // Core's own metadata for each of these rows (core.prefs): type, event,
    // html, position. collection and user_name are '' for all six, which is
    // what makes them global prefs rather than per-user overrides.
    $pref_meta = array(
      'siteurl' => array(0, 'site', 'text_input', 40),
      'path_to_site' => array(2, 'publish', 'text_input', 0),
      'file_base_path' => array(0, 'admin', 'text_input', 40),
      'tempdir' => array(0, 'admin', 'text_input', 80),
      'plugin_cache_dir' => array(0, 'admin', 'text_input', 100),
      'production_status' => array(0, 'site', 'prod_levels', 80),
    );
    $ins = $mysqli->prepare("INSERT INTO txp_prefs (name, val, type, event, collection, html, position, user_name) VALUES (?, ?, ?, ?, '', ?, ?, '')");
    $inserted = array();
    $unfixable = array();
    foreach ($missing as $name) {
      if (!isset($pref_meta[$name])) {
        $unfixable[] = $name;
        continue;
      }
      $val = $prefs[$name];
      $type = (int) $pref_meta[$name][0];
      $event = $pref_meta[$name][1];
      $html = $pref_meta[$name][2];
      $position = (int) $pref_meta[$name][3];
      try {
        $ins->bind_param('ssissi', $name, $val, $type, $event, $html, $position);
        $ins->execute();
      }
      catch (Exception $e) {
        // Swallowed on purpose: the existence test below is the verdict. A row
        // created in the meantime (TXP's integrity check racing this engine)
        // fails the INSERT on the primary key and is still exactly the state
        // this block wants.
      }
      // Re-assert the value whatever the INSERT did.
      $upd->bind_param('ss', $val, $name);
      $upd->execute();
      $rs = $mysqli->query("SELECT COUNT(*) FROM txp_prefs WHERE name = '" . $mysqli->real_escape_string($name) . "'");
      $row = ($rs !== FALSE) ? $rs->fetch_row() : array(0);
      if ((int) $row[0] > 0) {
        $inserted[] = $name;
      }
      else {
        $unfixable[] = $name;
      }
    }
    $ins->close();
    if (!empty($inserted)) {
      drush_log(dt('These prefs rows were absent and have been recreated with core metadata and the deployed values: !list.',
        array('!list' => implode(', ', $inserted))), 'ok');
    }
    $missing = $unfixable;
  }
  $upd->close();

  // Which contact/mail metadata did the copy actually inherit? Probed here,
  // while the connection is open, so the advisory below can name what is
  // really set instead of firing on every single copy.
  $inherited = array();
  if (!$is_restore) {
    $probe = array('publisher_email', 'smtp_from', 'smtp_host', 'smtp_user');
    foreach ($probe as $pref_name) {
      $rs = $mysqli->query("SELECT val FROM txp_prefs WHERE name = '" . $mysqli->real_escape_string($pref_name) . "'");
      if ($rs !== FALSE && ($row = $rs->fetch_row()) && trim((string) $row[0]) !== '') {
        $inherited[] = $pref_name;
      }
    }
    if ($table_exists($mysqli, 'txp_users')) {
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
  // the eye without claiming the deploy went wrong.
  if (!empty($inherited)) {
    drush_log(dt('NOTE: the copy still carries contact metadata from the source (!list). Review it in the Preferences and Users panels before the copy sends mail.',
      array('!list' => implode(', ', $inherited))), 'advisory');
  }
  else {
    drush_log(dt('Contact metadata check: the copy carries no publisher/SMTP address and no inherited user email address.'), 'ok');
  }
}
