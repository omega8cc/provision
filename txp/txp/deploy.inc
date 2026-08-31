<?php
/**
 * @file
 * Post-deploy SQL rewrite + security scrub for a deployed TXP site.
 *
 * Runs after clone/migrate/rename/restore extraction + DB import, against the
 * TARGET site's DB with the target creds. Two classes of deploy, deliberately
 * treated differently (docs/analysis/txp-clone-surface.md §2):
 *
 *  - IDENTITY-CHANGING (clone, rename, migrate): the copy must not inherit the
 *    source's secrets or sessions. blog_uid is REGENERATED (it is both the feed
 *    GUID seed and the CSRF salt), blog_mail_uid reset, smtp_pass cleared (a
 *    credential must never travel to another site/owner), and txp_users.nonce
 *    rotated (a live session cookie from the source authenticates on the copy
 *    otherwise, whenever the two share a cookie domain).
 *  - IDENTITY-PRESERVING (same-URI restore): blog_uid is KEPT -- regenerating
 *    it would churn every feed GUID for no security gain, since there is no
 *    second site to share the salt with.
 *
 * BOTH classes: re-point every stored path/URL pref, normalise
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

// Restore is identity-preserving; everything else is a copy. is_restore is set
// by the restore hook's deploy options; the old_uri comparison is the belt for
// any caller that omits it.
$old_uri = drush_get_option('old_uri', '');
$is_restore = drush_get_option('is_restore', FALSE) || ($old_uri !== '' && $old_uri === $uri);

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
  if (!$is_restore) {
    // blog_uid: 32 hex like the installer mints. Feed GUID seed AND CSRF salt.
    $scrub['blog_uid'] = md5(uniqid(mt_rand(), TRUE));
    $scrub['blog_mail_uid'] = md5(uniqid(mt_rand(), TRUE)) . 'blog@example.com';
    // An SMTP password must never travel to a copy.
    $scrub['smtp_pass'] = '';
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
    // login -- and the siteurl default is the ADMIN hostname. Say so loudly.
    if ($mysqli->affected_rows === 0) {
      $rs = $mysqli->query("SELECT COUNT(*) FROM txp_prefs WHERE name = '" . $mysqli->real_escape_string($name) . "'");
      $row = ($rs !== FALSE) ? $rs->fetch_row() : array(0);
      if ((int) $row[0] === 0) {
        $missing[] = $name;
      }
    }
  }
  $upd->close();
  if (!empty($missing)) {
    drush_log(dt('These prefs rows were absent and could NOT be re-pointed: !list. TXP will recreate them from defaults on the next admin login (siteurl defaults to the ADMIN host) -- fix them in the Preferences panel.',
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
  drush_log(dt('Deployed TXP copy re-pointed at !uri; blog_uid + user nonces regenerated, smtp_pass cleared, tokens/logs/nonces truncated.', array('!uri' => $uri)), 'ok');
  // Deliberately NOT mutated (they are content/identity, not credentials, and
  // silently rewriting them would break a legitimate copy): txp_users.email
  // rows, publisher_email, smtp_from/smtp_host/smtp_user. Name them so the
  // operator can decide.
  drush_log(dt('NOTE: the copy inherits the source contact metadata (user emails, publisher_email, smtp_from/host/user). Review them in the Preferences and Users panels before the copy sends mail.'), 'warning');
}
