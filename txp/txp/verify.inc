<?php
/**
 * @file
 * DB-side verify checks for a Textpattern site (bootstrap-free by design).
 *
 * Verify is the operator's heal action, so a dead DB never fails the task:
 * the checks DEGRADE (version 'unknown', a warning) while the tree/config
 * repair in the calling hook has already run. With the DB up: assert the
 * schema anchor table, read the installed version, and RE-ASSERT the
 * backup-scope prefs (file_base_path/tempdir/path_to_site are operator-editable
 * in the TXP admin -- install-time seeding guarantees nothing; drifted values
 * put runtime writes outside sites/<uri>/ and silently outside every backup).
 */

$uri = d()->uri;
$site_path = d()->site_path;

$db_user = urldecode(drush_get_option('db_user', '', 'site'));
$db_passwd = urldecode(drush_get_option('db_passwd', '', 'site'));
$db_host = drush_get_option('db_host', 'localhost', 'site');
$db_port = drush_get_option('db_port', '', 'site');
$db_name = drush_get_option('db_name', '', 'site');

drush_set_option('txp_site_version', 'unknown', 'site');

if ($db_name === '' || $db_user === '') {
  drush_log(dt('No DB credentials in the context; TXP DB checks skipped (codebase-only verify).'), 'warning');
  return;
}

mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);
try {
  $mysqli = mysqli_init();
  $mysqli->real_connect($db_host, $db_user, $db_passwd, $db_name, $db_port ? (int) $db_port : 3306);
  $mysqli->set_charset('utf8mb4');
}
catch (Exception $e) {
  drush_log(dt('Site DB unreachable (!msg); TXP DB checks skipped (codebase-only verify).',
    array('!msg' => $e->getMessage())), 'warning');
  return;
}

try {
  $result = $mysqli->query("SHOW TABLES LIKE 'textpattern'");
  if ($result === FALSE || $result->num_rows !== 1) {
    drush_log(dt('The textpattern anchor table is missing -- the DB exists but carries no TXP schema.'), 'warning');
    $mysqli->close();
    return;
  }

  $version = 'unknown';
  $result = $mysqli->query("SELECT val FROM txp_prefs WHERE name = 'version'");
  if ($result !== FALSE && ($row = $result->fetch_row())) {
    $version = $row[0];
  }
  drush_set_option('txp_site_version', $version, 'site');
  drush_log(dt('Textpattern site schema present, version !v.', array('!v' => $version)), 'ok');

  // Backup-scope law re-assertion.
  $canonical = array(
    'path_to_site' => $site_path . '/public',
    'file_base_path' => $site_path . '/public/files',
    'tempdir' => $site_path . '/tmp',
  );
  $stmt = $mysqli->prepare("SELECT val FROM txp_prefs WHERE name = ?");
  $upd = $mysqli->prepare("UPDATE txp_prefs SET val = ? WHERE name = ?");
  foreach ($canonical as $name => $want) {
    $stmt->bind_param('s', $name);
    $stmt->execute();
    $stmt->bind_result($have);
    $found = $stmt->fetch();
    $stmt->free_result();
    if ($found && $have !== $want) {
      $upd->bind_param('ss', $want, $name);
      $upd->execute();
      drush_log(dt('Healed pref !name: "!have" -> "!want" (backup-scope law).',
        array('!name' => $name, '!have' => $have, '!want' => $want)), 'warning');
    }
  }
  $stmt->close();
  $upd->close();
  $mysqli->close();
}
catch (Exception $e) {
  drush_log(dt('TXP DB checks aborted: !msg', array('!msg' => $e->getMessage())), 'warning');
}
