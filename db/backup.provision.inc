<?php

function drush_db_pre_provision_backup() {
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
  // The files-only mode promises "without any DB": skip the dump entirely
  // (classic database.sql and the MyQuick tmp_expim path alike). The mode is
  // latched by the backup validate before this pre hook runs. Never skip for
  // an INTERNAL backup (the flag is set by clone/migrate/restore before
  // invoking provision-backup): their deploy needs the dump regardless of a
  // leaked mode from a concurrent panel task.
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (defined('SELECTED_BACKUP_MODE') && SELECTED_BACKUP_MODE === 'site_files_without_db_backup'
      && !is_file($internal_backup_flag)) {
    drush_log(dt('Skipping the database dump for the files-only backup mode.'), 'info');
    return;
  }
  d()->service('db')->generate_dump();
}

function drush_db_pre_provision_backup_rollback() {
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
  provision_file()->unlink(d()->site_path . '/database.sql')
    ->succeed('Deleted mysql dump from sites directory')
    ->fail('Could not delete mysql dump from sites directory');
}

function drush_db_post_provision_backup() {
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
  drush_db_pre_provision_backup_rollback();
}

