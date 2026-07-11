<?php

// Fetch the Backdrop cron key and expose it as the 'cron_key' drush option so
// the front end can build the web-request cron endpoint core/cron.php?cron_key=.
//
// Backdrop keeps the cron key in the {state} table (serialized), NOT in the
// {variable} table the D7 engine reads and NOT in settings.php. Read it straight
// from the site DB through the provision DB service -- its own PDO handle,
// connected as the master admin with no default schema selected -- so this never
// depends on an in-process Backdrop bootstrap (the provision parent process
// never gets a complete one; see backdrop/install.inc).

$cron_key = '';
$db_name = _provision_backdrop_site_db_name();
if (!empty($db_name) && preg_match('/^[A-Za-z0-9_]+$/', $db_name)) {
  // Fully-qualify {state}: no default schema is selected on this connection.
  $result = d()->service('db')->query("SELECT value FROM `" . $db_name . "`.`state` WHERE name = '%s'", 'cron_key');
  if ($result) {
    $row = $result->fetch(PDO::FETCH_NUM);
    if (!empty($row[0])) {
      $value = @unserialize($row[0]);
      if (is_string($value) && $value !== '') {
        $cron_key = $value;
      }
    }
  }
}

if (!empty($cron_key)) {
  // Into the 'site' context (not the process default) so it rides the backend
  // context back to Hostmaster and persists with the rest of the site data --
  // that is what the front end reads to build core/cron.php?cron_key=<key>.
  drush_set_option('cron_key', $cron_key, 'site');
  drush_log(dt('Resolved Backdrop cron_key from {state}.'), 'ok');
}
else {
  drush_log(dt('Could not resolve the Backdrop cron_key from {state}; the cron endpoint stays unauthenticated until the next verify.'), 'warning');
}
