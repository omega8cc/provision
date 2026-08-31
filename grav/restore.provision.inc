<?php

/**
 * @file
 * Restore command (Grav layer).
 *
 * Fork of platform/restore.provision.inc collapsed to the DB-less capsule: the
 * safety copy is forced to site_files_without_db_backup (the TRUTHFUL
 * grav-restorable mode -- the Drupal flow's site_files_with_mysqldump forcing
 * would be a lying label here, boa-grav R5), and the whole MyQuick
 * internal-backup-flag machinery (a DB-dump concern) is gone. The restore
 * itself is the standard backup -> provision-deploy -> provision-verify chain.
 */

/**
 * Make sure we have a valid site being restored, and that the file being restored from exists.
 */
function drush_provision_grav_provision_restore_validate($restore_file = null) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  // Backup mode ctrl file cleanup: on the serial queue any file present at
  // task start is a dead leftover by definition (same rationale as the
  // Drupal flow); the safety copy below force-defines its own mode.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }

  $exists = provision_file()->exists($restore_file)
    ->succeed('Restoring site from @path')
    ->fail('Could not find backup file @path', 'PROVISION_BACKUP_NOT_FOUND')
    ->status();
  if ($exists) {
    drush_set_option('restore_file', $restore_file);
  }
}

/**
 * Make a safety copy before making any changes.
 */
function drush_provision_grav_pre_provision_restore($restore_file) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  drush_log(dt("Putting site under maintenance"));
  d()->site_enabled = false;

  // The safety copy is registered in the panel as a real backup of this site,
  // so it must honour the backup contract for what this site IS: a DB-less
  // capsule. site_files_without_db_backup is the mode whose label tells the
  // truth (full tree, no dump) and whose tar dereferences for a
  // self-contained archive.
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_without_db_backup');
  }
  drush_set_option('selected_backup_mode', 'site_files_without_db_backup');
  drush_set_option('backup_mode', 'site_files_without_db_backup');
  drush_log(dt('Restore safety copy forced to the grav-restorable mode: site_files_without_db_backup'), 'info');
  drush_invoke('provision-backup');
}

function drush_provision_grav_pre_provision_restore_rollback($restore_file) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused restore safety copy')
    ->fail('Could not remove unused restore safety copy');
}

/**
 * Switch the restore directories around via the deploy chain.
 */
function drush_provision_grav_provision_restore($restore_file) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'restore');

  provision_backend_invoke(d()->name, 'provision-deploy', array(drush_get_option('restore_file')), $deploy_options);
  if (!drush_get_error()) {
    provision_backend_invoke(d()->name, 'provision-verify');
    drush_set_option('installed', false);
  }
}
