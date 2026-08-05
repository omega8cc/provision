<?php

/**
 * Make sure we have a valid site being restored, and that the file being restored from exists
 */
function drush_provision_drupal_provision_restore_validate($restore_file = null) {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (drush_drupal_major_version() >= 8) {
    // Lock Site-Local Drush if Unlocked
    provision_check_codebase_status();
    if (!d()->isConsolePatched || !d()->isCorePatched) {
      drush_log(dt('RESTORE/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
      provision_lock_drush_local_vnd();
    }
  }
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);

  $exists = provision_file()->exists($restore_file)
    ->succeed('Restoring site from @path')
    ->fail('Could not find backup file @path', 'PROVISION_BACKUP_NOT_FOUND')
    ->status();
  if ($exists) {
    drush_set_option('restore_file', $restore_file);
  }
}

/**
 * Make a backup before making any changes, and add extract the file we are restoring from
 */
function drush_provision_drupal_pre_provision_restore($restore_file) {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  drush_log(dt("Putting site under maintenance"));
  d()->site_enabled = false;
  _provision_drupal_create_settings_file();
  drush_log(dt('Invoking drush provision-backup in drush_provision_drupal_pre_provision_restore'));
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  $internal_backup_flag_blank = "Internal-DB-Backup \n";
  $local_description = 'Adding Internal-DB-Backup Flag-File drush_provision_drupal_pre_provision_restore';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, $internal_backup_flag_blank)
      ->succeed('Generated blank ' . $local_description)
      ->fail('Could not generate ' . $local_description);
  }
  drush_invoke('provision-backup');
}

function drush_provision_drupal_pre_provision_restore_rollback($restore_file) {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  $success =  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused migration site package')
    ->fail('Could not remove unused migration site package');
  // Belt for the flag cleanup above: rollback of the completed pre phase runs
  // when a later phase fails before the command-level cleanup is reached.
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (is_file($internal_backup_flag)) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in restore rollback')
      ->fail('Could not remove Internal-DB-Backup Flag-File in restore rollback');
  }
}

/**
 * Switch the restore directories around now that we have the new db installed
 */
function drush_provision_drupal_provision_restore($restore_file) {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  // Build an options array to pass to the deploy task. This allows injection
  // of data into the deploy task. See: https://www.drupal.org/node/2471805
  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
    // The deploy import must use the archive's own database.sql: on MyQuick
    // boxes the tmp_expim dir holds the PRE-restore safety dump of the
    // CURRENT database (left in place by the internal-backup flag), and the
    // fast-import path would silently re-import it - restoring nothing.
    'is_restore' => TRUE,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'restore');

  provision_backend_invoke(d()->name, 'provision-deploy', array(drush_get_option('restore_file')), $deploy_options);
  if (!drush_get_error()) {
    provision_backend_invoke(d()->name, 'provision-verify');

    // Native files symlinking after restore: a files-carrying archive is
    // extracted as plain files/private dirs, so the deploy swap replaces the
    // in-site symlinks with real dirs on the platform partition and orphans
    // the per-account store. The helper re-converts (as clone and migrate
    // do, but without breaking a deliberate share), and protects against
    // files-less archives (DB-only backup mode) by re-pointing at the
    // existing store instead of archiving it away. Warn-not-fail.
    _provision_drupal_native_symlink_after_restore(d()->uri);

    drush_set_option('installed', false);
  }

  // Always clear the flag, success or failure: a leftover one makes every
  // later backup on the account skip the MyQuick tmp_expim rotation, silently
  // ending dated DB-dump retention.
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  if (is_file($internal_backup_flag)) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_drupal_provision_restore')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_drupal_provision_restore');
  }
}
