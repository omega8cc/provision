<?php

/**
 * Restore command implementation (Backdrop layer).
 *
 * Fork of platform/restore.provision.inc: CMS-agnostic (backup, then deploy from
 * the restore file). The D8+ site-local Drush lock in validate is dropped and
 * the settings writer is the Backdrop one.
 */

/**
 * Make sure we have a valid site being restored, and that the file being restored from exists.
 */
function drush_provision_backdrop_provision_restore_validate($restore_file = null) {
  if (!provision_platform_is_backdrop(d())) {
    return;
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
 * Make a backup before making any changes, and extract the file we are restoring from.
 */
function drush_provision_backdrop_pre_provision_restore($restore_file) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  drush_log(dt("Putting site under maintenance"));
  d()->site_enabled = false;
  // Reload the persisted db creds BEFORE the maintenance re-render: at this
  // point the 'site' option context carries no db_* values (same class the
  // helper was built for), so the write below would otherwise blank the live
  // settings.php credentials -- breaking serving for the restore window on
  // uncloaked boxes, and stranding a credential-less sites/<uri>.restore
  // artifact when the deploy aborts mid-swap (D-035: the purge task can then
  // never confirm the orphan copy database).
  _provision_backdrop_ensure_db_creds_loaded();
  _provision_backdrop_create_settings_file();
  drush_log(dt('Invoking drush provision-backup in drush_provision_backdrop_pre_provision_restore'));
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  $internal_backup_flag_blank = "Internal-DB-Backup \n";
  $local_description = 'Adding Internal-DB-Backup Flag-File drush_provision_backdrop_pre_provision_restore';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, $internal_backup_flag_blank)
      ->succeed('Generated blank ' . $local_description)
      ->fail('Could not generate ' . $local_description);
  }
  drush_invoke('provision-backup');
}

function drush_provision_backdrop_pre_provision_restore_rollback($restore_file) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  $success =  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused migration site package')
    ->fail('Could not remove unused migration site package');
}

/**
 * Switch the restore directories around now that we have the new db installed.
 */
function drush_provision_backdrop_provision_restore($restore_file) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  // Build an options array to pass to the deploy task.
  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'restore');

  provision_backend_invoke(d()->name, 'provision-deploy', array(drush_get_option('restore_file')), $deploy_options);
  if (!drush_get_error()) {
    provision_backend_invoke(d()->name, 'provision-verify');
    drush_set_option('installed', false);
    $backup_path = d('@server_master')->backup_path;
    $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_backdrop_provision_restore')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_backdrop_provision_restore');
  }
}
