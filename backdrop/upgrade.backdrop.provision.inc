<?php

/**
 * provision-backdrop-upgrade — D7 -> Backdrop upgrade (Backdrop layer).
 *
 * Clone-preserving shape: backup the live D7 source, save a NEW site alias at
 * a NEW uri on the Backdrop target platform, deploy the backup there (deploy
 * always imports into a freshly created copy database), convert the COPY with
 * the standalone updater (scripts/update-d7.php, a separate modern-PHP
 * process using core's own backdrop_bootstrap — never drush/BackdropBoot),
 * then verify. The source site, database and vhost are never in the write
 * path; rollback at any step = discard the copy.
 *
 * Hook naming: the command name starts with this commandfile's name, so Drush
 * COLLAPSES the prefix for the validate/command/rollback hooks
 * (drush_provision_backdrop_upgrade*) while the pre/post variations keep the
 * doubled form (drush_provision_backdrop_pre_provision_backdrop_upgrade) —
 * see includes/command.inc:405 in the Drush 8 fork.
 *
 * File name: Drush resolves command includes by the reversed-dotted command
 * name, so provision-backdrop-upgrade => upgrade.backdrop.provision.inc.
 */

/**
 * Implements drush_hook_COMMAND_validate() (collapsed name).
 *
 * Every precondition, fail-closed, before anything is created.
 */
function drush_provision_backdrop_upgrade_validate($new_name = NULL, $platform = NULL) {
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_BAD_SOURCE',
      dt('provision-backdrop-upgrade must run against a site context.'));
  }
  // Backup mode ctrl file cleanup, as the other lifecycle validates do.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }

  // The source must be Drupal 7 lineage, never Backdrop.
  if (provision_platform_is_backdrop(d())) {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_BAD_SOURCE',
      dt('The source site is already Backdrop; provision-backdrop-upgrade only upgrades Drupal 7 sites.'));
  }
  if (drush_drupal_major_version() != 7) {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_BAD_SOURCE',
      dt('The source platform is Drupal @v; only Drupal 7 upgrades to Backdrop. Upgrade Drupal 6 to Drupal 7 first.',
      array('@v' => drush_drupal_major_version())));
  }

  // Backdrop's own floor: the conversion runs under this backend PHP.
  if (version_compare(PHP_VERSION, '7.1.0', '<')) {
    return drush_set_error('PROVISION_BACKDROP_PHP_TOO_OLD',
      dt('Backdrop needs PHP 7.1+; the backend runs @v.', array('@v' => PHP_VERSION)));
  }

  // Target platform must be a known Backdrop root.
  if (!is_string($platform) || $platform === '') {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_BAD_TARGET',
      dt('No target platform alias given.'));
  }
  $target_root = d($platform)->root;
  if (empty($target_root) || !provision_platform_is_backdrop(d($platform))) {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_BAD_TARGET',
      dt('The target platform !platform is not a Backdrop platform.', array('!platform' => $platform)));
  }

  // The new uri must differ from the source and be free on the target.
  if (!is_string($new_name) || $new_name === '') {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_BAD_TARGET',
      dt('No new site alias given.'));
  }
  $new_uri = ltrim($new_name, '@');
  if ($new_uri === d()->uri) {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_BAD_TARGET',
      dt('The new uri must differ from the source uri (the source keeps serving until you retire it).'));
  }
  if (is_dir($target_root . '/sites/' . $new_uri)) {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_BAD_TARGET',
      dt('sites/@uri already exists on the target platform.', array('@uri' => $new_uri)));
  }

  // The mapped Backdrop profile must exist on the target platform.
  $profile = drush_get_option('profile', 'standard');
  if (!is_dir($target_root . '/core/profiles/' . $profile) && !is_dir($target_root . '/profiles/' . $profile)) {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_BAD_TARGET',
      dt('The @profile profile does not exist on the target platform.', array('@profile' => $profile)));
  }

  // The hard gate: source system schema >= 7069 (REQUIRED_D7_SCHEMA_VERSION).
  // Bootstrap-free: the provision DB service PDO selects no default schema, so
  // the table MUST be qualified with the source db name (cron_key.inc lesson).
  $db_name = _provision_backdrop_site_db_name();
  if (empty($db_name) || !preg_match('/^[A-Za-z0-9_]+$/', $db_name)) {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_NO_DB',
      dt('Could not resolve the source site database name.'));
  }
  $db = d()->service('db');
  $db->connect();
  $schema = 0;
  $result = $db->query("SELECT schema_version FROM `" . $db_name . "`.`system` WHERE name = '%s' AND type = '%s'", 'system', 'module');
  if ($result) {
    $row = $result->fetch(PDO::FETCH_NUM);
    if (isset($row[0])) {
      $schema = intval($row[0]);
    }
  }
  if ($schema < 7069) {
    return drush_set_error('PROVISION_BACKDROP_UPGRADE_SCHEMA_TOO_OLD',
      dt('The source system schema is @schema; Backdrop requires >= 7069. Update the Drupal 7 site to the most recent version first.',
      array('@schema' => $schema ? $schema : dt('unreadable'))));
  }
  drush_log(dt('Source system schema @schema passes the Backdrop >= 7069 gate.', array('@schema' => $schema)), 'ok');

  // Advisory module compatibility report from the data map. Never a hard gate:
  // update_fix_compatibility() disables non-backdrop extensions anyway; this
  // is the operator warning BEFORE committing.
  _provision_backdrop_upgrade_module_report($db, $db_name);
}

/**
 * Advisory D7-module -> Backdrop disposition report (validate helper).
 */
function _provision_backdrop_upgrade_module_report($db, $db_name) {
  $map_file = dirname(__FILE__) . '/data/d7_module_map.json';
  $map = array();
  if (is_readable($map_file)) {
    $decoded = json_decode(file_get_contents($map_file), TRUE);
    if (is_array($decoded) && isset($decoded['modules']) && is_array($decoded['modules'])) {
      $map = $decoded['modules'];
    }
  }
  if (empty($map)) {
    drush_log(dt('Module map @file missing or unreadable; skipping the compatibility report.', array('@file' => $map_file)), 'warning');
    return;
  }
  $result = $db->query("SELECT name FROM `" . $db_name . "`.`system` WHERE type = '%s' AND status = 1 ORDER BY name", 'module');
  if (!$result) {
    drush_log(dt('Could not enumerate enabled modules for the compatibility report.'), 'warning');
    return;
  }
  $counts = array('core' => 0, 'superseded' => 0, 'contrib' => 0, 'none' => 0, 'unknown' => 0);
  while ($row = $result->fetch(PDO::FETCH_NUM)) {
    $name = $row[0];
    if (isset($map[$name]['disposition'])) {
      $disposition = $map[$name]['disposition'];
      $note = isset($map[$name]['note']) ? ' — ' . $map[$name]['note'] : '';
      $successor = isset($map[$name]['backdrop_name']) ? $map[$name]['backdrop_name'] : '';
    }
    else {
      $disposition = 'unknown';
      $note = '';
      $successor = '';
    }
    if (!isset($counts[$disposition])) {
      $disposition = 'unknown';
    }
    $counts[$disposition]++;
    switch ($disposition) {
      case 'core':
        break;
      case 'superseded':
        drush_log(dt('UPGRADE/MAP @name is superseded by Backdrop core @successor.', array('@name' => $name, '@successor' => $successor)), 'ok');
        break;
      case 'contrib':
        drush_log(dt('UPGRADE/MAP @name needs the Backdrop contrib port (@successor) on the target platform, or it will be DISABLED by the conversion.@note', array('@name' => $name, '@successor' => $successor, '@note' => $note)), 'warning');
        break;
      case 'none':
        drush_log(dt('UPGRADE/MAP @name has no Backdrop equivalent and will be DISABLED by the conversion (data tables are kept).@note', array('@name' => $name, '@note' => $note)), 'warning');
        break;
      default:
        drush_log(dt('UPGRADE/MAP @name is not in the map; unless the codebase ships a backdrop = 1.x port it will be DISABLED by the conversion.', array('@name' => $name)), 'warning');
    }
  }
  drush_log(dt('UPGRADE/MAP report: @core in core, @sup superseded, @contrib contrib-port, @none removed, @unknown unmapped.',
    array('@core' => $counts['core'], '@sup' => $counts['superseded'], '@contrib' => $counts['contrib'], '@none' => $counts['none'], '@unknown' => $counts['unknown'])), 'ok');
}

/**
 * Implements drush_hook_pre_COMMAND() (doubled name — not collapsed).
 *
 * Take the source backup: live, never taken offline (clone semantics).
 */
function drush_provision_backdrop_pre_provision_backdrop_upgrade($new_name = NULL, $platform = NULL) {
  drush_log(dt('Invoking drush provision-backup in drush_provision_backdrop_pre_provision_backdrop_upgrade'));
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  $internal_backup_flag_blank = "Internal-DB-Backup \n";
  $local_description = 'Adding Internal-DB-Backup Flag-File drush_provision_backdrop_pre_provision_backdrop_upgrade';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, $internal_backup_flag_blank)
      ->succeed('Generated blank ' . $local_description)
      ->fail('Could not generate ' . $local_description);
  }
  drush_invoke('provision-backup');
}

/**
 * Implements drush_hook_COMMAND() (collapsed name).
 *
 * Orchestration, mirroring the VM-proven Backdrop clone command hook: save the
 * target alias, deploy the backup with the backdrop_d7_upgrade flag, unshare
 * the native files store, convert with the standalone updater, verify.
 */
function drush_provision_backdrop_upgrade($new_name = NULL, $platform = NULL) {
  drush_set_option('old_platform', d()->platform->name);

  $aegir_root = d('@server_master')->aegir_root;
  $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

  if (!is_file($enable_fast_track)) {
    // Refresh the collected info about both platforms and the source site
    // first, exactly as the clone command does.
    if (!is_null(d($platform)->name) && (d($platform)->name != d()->platform->name)) {
      provision_backend_invoke('@hostmaster', 'hosting-task', array(d()->platform->name, 'verify'), array('force' => TRUE));
      sleep(5); // A small trick to avoid high load and race conditions.
      provision_backend_invoke('@hostmaster', 'hosting-task', array(d($platform)->name, 'verify'), array('force' => TRUE));
      sleep(5); // A small trick to avoid high load and race conditions.
    }
    $local_uri_verify = '@' . d()->uri;
    provision_backend_invoke('@hostmaster', 'hosting-task', array($local_uri_verify, 'verify'), array('force' => TRUE));
    sleep(5); // A small trick to avoid high load and race conditions.
  }

  $options = d()->options;
  $options['uri'] = ltrim($new_name, '@');
  $hash_name = drush_get_option('#name') ? '#name' : 'name';
  $options[$hash_name] = $new_name;
  $options['platform'] = $platform;
  $options['root'] = d($platform)->root;
  $options['aliases'] = array();
  $options['redirection'] = 0;

  // The copy ships dark: no SSL/LE (keys are bound to the source name), no
  // CDN; cron stays disabled until the operator enables it.
  $options['ssl_enabled'] = 0;
  unset($options['ssl_key']);
  $options['cdn'] = 0;
  unset($options['cdn']);

  // Never carry the source's D7 package map onto the Backdrop alias: the
  // deploy flag skips the schema comparison, and a stale map on the saved
  // context would only mislead later consumers.
  unset($options['packages']);

  // The mapped Backdrop profile (validated to exist on the target).
  $options['profile'] = drush_get_option('profile', 'standard');

  if ($db_server = drush_get_option('new_db_server', FALSE)) {
    $options['db_server'] = $db_server;
  }

  drush_invoke_process('@none', 'provision-save', array($new_name), $options);

  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
    // Tells the Backdrop deploy fork this copy database is still Drupal 7:
    // skip the namesake schema comparison and the post-deploy
    // updatedb/in-process rebuild — the standalone updater converts instead.
    'backdrop_d7_upgrade' => 1,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'backdrop_upgrade');

  provision_backend_invoke($new_name, 'provision-deploy', array(drush_get_option('backup_file')), $deploy_options);

  if (!drush_get_error()) {
    // Give the copy its OWN native files/private store BEFORE converting, so
    // every write from here on lands in the copy's store, never the source's.
    _provision_drupal_native_symlink($options['uri'], TRUE);

    // Convert the copy: standalone updater, separate modern-PHP process.
    $script = dirname(__FILE__) . '/scripts/update-d7.php';
    if (!is_file($script)) {
      drush_set_error('PROVISION_BACKDROP_UPGRADE_NO_UPDATER',
        dt('Updater script missing at @script.', array('@script' => $script)));
    }
    else {
      $cmd = escapeshellarg(PHP_BINARY) . ' ' . escapeshellarg($script)
        . ' --root=' . escapeshellarg($options['root'])
        . ' --url=' . escapeshellarg($options['uri']) . ' 2>&1';
      drush_log(dt('BACKDROP/UPGRADE/CMD @cmd', array('@cmd' => $cmd)), 'info');
      $converted = drush_shell_cd_and_exec($options['root'], $cmd);
      $output = drush_shell_exec_output();
      if (is_array($output)) {
        foreach ($output as $line) {
          drush_log('BACKDROP/UPGRADE ' . $line, $converted ? 'info' : 'warning');
        }
      }
      if (!$converted) {
        drush_set_error('PROVISION_BACKDROP_UPGRADE_CONVERSION_FAILED',
          dt('The D7 -> Backdrop conversion failed on the copy database; the source site is untouched. Discard the copy and retry from a fresh deploy.'));
      }
    }
  }

  if (!drush_get_error()) {
    // Verify the converted copy (Phase-1 Backdrop verify: bootstrap-free,
    // reloads creds, rebuilds the package map, sets the cron_key option).
    provision_backend_invoke($new_name, 'provision-verify');

    if (!is_file($enable_fast_track)) {
      sleep(5); // A small trick to avoid high load and race conditions.
      // Re-verify the source site via the frontend, mirroring clone (#1004526).
      provision_backend_invoke('@hostmaster', 'hosting-task', array('@' . d()->uri, 'verify'), array('force' => TRUE));
    }

    $backup_path = d('@server_master')->backup_path;
    $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_backdrop_upgrade')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_backdrop_upgrade');
  }

  provision_reload_config('site', d()->site_path . '/drushrc.php');
}

/**
 * Implements drush_hook_COMMAND_rollback() (collapsed name).
 *
 * Deliberately non-destructive: the copy's artifacts (target alias, copy
 * database, target site dir) are harmless residue while the source keeps
 * serving; deleting them automatically from a failure path risks more than it
 * saves. Report exactly what may exist and how to discard it.
 */
function drush_provision_backdrop_upgrade_rollback($new_name = NULL, $platform = NULL) {
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  if (provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_backdrop_upgrade_rollback')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_backdrop_upgrade_rollback');
  }
  drush_log(dt('provision-backdrop-upgrade failed; the source site @uri is untouched and still serving. Partial copy artifacts may remain (site alias @new, its site directory and copy database on the target platform); discard them with: drush @new provision-delete (or remove the alias with provision-save --delete) before retrying.',
    array('@uri' => d()->uri, '@new' => is_string($new_name) ? $new_name : '@<new site>')), 'warning');
}
