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
  // Backup mode ctrl file cleanup. The safety copy force-defines its own mode
  // and never reads the ctrl file, so without this a dead backup task's
  // leftover would survive the whole restore - adopted by the provision-deploy
  // child and left behind to mis-mode the next modeless backup. On the serial
  // queue any file present at task start is a dead leftover by definition.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
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
  // The safety copy is registered in the panel as a real backup of this
  // site, so it must honour the backup contract: self-contained, with the
  // database inside the archive (see the Drupal-layer pre-restore hook for
  // the full rationale). Clone/migrate pre-hooks are separate; their fast
  // path is untouched.
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_with_mysqldump');
  }
  drush_set_option('selected_backup_mode', 'site_files_with_mysqldump');
  drush_set_option('backup_mode', 'site_files_with_mysqldump');
  drush_log(dt('Restore safety copy forced to the restorable mode: site_files_with_mysqldump'), 'info');
  drush_invoke('provision-backup');
}

function drush_provision_backdrop_pre_provision_restore_rollback($restore_file) {
  if (!provision_platform_is_backdrop(d())) {
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
  // The deploy import must use the archive's own database.sql: on MyQuick
  // boxes the tmp_expim dir holds the PRE-restore safety dump of the CURRENT
  // database (left in place by the internal-backup flag), and the fast-import
  // path would silently re-import it - restoring nothing.
  $deploy_options['is_restore'] = TRUE;
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
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_backdrop_provision_restore')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_backdrop_provision_restore');
  }
}
