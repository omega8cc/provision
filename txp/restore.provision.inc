<?php
/**
 * @file
 * Provision hooks for the provision restore command (TXP layer).
 *
 * Mirrors the Backdrop restore flow: reload persisted creds BEFORE any
 * re-render (or the live config.php gets blanked for the restore window),
 * take a self-contained safety copy (forced to the restorable mode, the
 * backup contract), then deploy the archive over the site with is_restore
 * (the archive's own database.sql, never a tmp_expim fast-import) and verify.
 * TXP has no maintenance mode; the disable window is vhost-level only.
 */

function drush_provision_txp_provision_restore_validate($restore_file = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  provision_file()->exists($restore_file)
    ->succeed('Restoring site from @path')
    ->fail('Could not find backup file @path', 'PROVISION_BACKUP_NOT_FOUND')
    ->status();
  drush_set_option('restore_file', $restore_file);
  // Pin the restorable backup mode at the EARLIEST hook in this process.
  // provision_backup_mode_resolve() memoises into the SELECTED_BACKUP_MODE
  // constant on its first call and every later caller gets that value, so a
  // force applied in a pre_ hook loses to anything that resolved first --
  // which is how a "safety" copy ended up with no database in it. Setting it
  // in validate is the earliest point this layer runs.
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_with_mysqldump');
  }
  drush_set_option('selected_backup_mode', 'site_files_with_mysqldump');
  drush_set_option('backup_mode', 'site_files_with_mysqldump');

  // Clear a stale frontend backup-mode ctrl file: the safety copy below forces
  // its own mode, but a leftover would otherwise survive into the next flow.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }
}

function drush_provision_txp_pre_provision_restore($restore_file) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  d()->site_enabled = FALSE;
  // Reload the persisted db creds BEFORE the re-render below, or the write
  // blanks the live config.php credentials for the whole restore window.
  _provision_txp_ensure_db_creds_loaded();
  _provision_txp_create_settings_file();

  drush_log(dt('Invoking drush provision-backup in drush_provision_txp_pre_provision_restore'));
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, "Internal-DB-Backup \n")
      ->succeed('Generated blank Internal-DB-Backup Flag-File (txp restore)')
      ->fail('Could not generate Internal-DB-Backup Flag-File (txp restore)');
  }
  // The safety copy registers in the panel as a real backup: it must honour
  // the backup contract (self-contained, DB inside).
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_with_mysqldump');
  }
  drush_set_option('selected_backup_mode', 'site_files_with_mysqldump');
  drush_set_option('backup_mode', 'site_files_with_mysqldump');
  drush_log(dt('Restore safety copy forced to the restorable mode: site_files_with_mysqldump'), 'info');
  drush_invoke('provision-backup');
}

function drush_provision_txp_pre_provision_restore_rollback($restore_file) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused restore safety package')
    ->fail('Could not remove unused restore safety package');
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (is_file($internal_backup_flag)) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in restore rollback')
      ->fail('Could not remove Internal-DB-Backup Flag-File in restore rollback');
  }
}

function drush_provision_txp_provision_restore($restore_file) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
    // The deploy import must use the archive's own database.sql (never a
    // tmp_expim fast-import of the PRE-restore safety dump).
    'is_restore' => TRUE,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'restore');

  provision_backend_invoke(d()->name, 'provision-deploy', array(drush_get_option('restore_file')), $deploy_options);
  if (!drush_get_error()) {
    provision_backend_invoke(d()->name, 'provision-verify');
    drush_set_option('installed', FALSE);
  }
  else {
    // The pre-hook took the site into the maintenance (disabled) vhost. A
    // FAILED restore must not leave it there: the deploy has already swapped
    // the original tree back and re-rendered its config, so the only thing
    // still wrong is the vhost. Re-render it from the persisted (enabled)
    // state -- site_enabled was only ever flipped in memory, never written to
    // the alias. Without this the site stays on "Under Construction" until an
    // operator runs Enable/Verify by hand (live-proven on the rig).
    d()->site_enabled = TRUE;
    d()->service('http')->create_config('site');
    d()->service('http')->parse_configs();
    drush_log(dt('Restore failed; the original site tree, credentials and vhost have been put back.'), 'warning');
  }

  // Always clear the flag, success or failure (a leftover one silently ends
  // dated DB-dump retention on the account).
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (is_file($internal_backup_flag)) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_txp_provision_restore')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_txp_provision_restore');
  }
}
