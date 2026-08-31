<?php
/**
 * @file
 * Provision hooks for the provision backup-delete command (Grav layer).
 *
 * Fork of backdrop/delete.backup.provision.inc. CMS-agnostic: unlink backup
 * files + clean up client backup symlinks.
 *
 * @param string $backup
 *   A comma-separated list of backup filenames.
 */
function drush_provision_grav_provision_backup_delete($backup) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  $backups = explode(',', $backup);
  foreach ($backups as $filename) {
    provision_file()->unlink($filename)
      ->succeed('Removed backup file @path')
      ->fail('Could not remove backup file @path');
  }
}

/**
 * Implements drush_hook_post_COMMAND().
 *
 * If we have a client name, cleanup the backup symlinks created for that client.
 *
 * @param string $backup
 *   A comma-separated list of backup filenames.
 */
function drush_provision_grav_post_provision_backup_delete($backup) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->client_name) {
    $backup_dir = d()->server->clients_path . '/' . d()->client_name . '/backups';
    $backups = explode(',', $backup);
    foreach ($backups as $filename) {
      provision_file()->unlink($backup_dir . '/' . basename($filename))
        ->succeed('Deleted backup symlink @path');
    }
  }
}
