<?php

function drush_db_provision_deploy_validate() {
  d()->service('db')->connect();
}

function drush_db_pre_provision_deploy() {
  d()->service('db')->create_site_database();
}

function drush_db_pre_provision_deploy_rollback() {
  d()->service('db')->destroy_site_database();
}

function drush_db_provision_deploy() {
  d()->service('db')->import_site_database();
}


// Rollback doesn't apply here yet. Unless we trigger a deploy of the first dump
// made. Which could go on infinitely if something is really long.
function drush_db_post_provision_deploy() {

  $enable_myquick = FALSE;
  $oct_db_test = FALSE;
  $backup_mode = drush_get_option('selected_backup_mode', FALSE);
  if (empty($backup_mode)) {
    $backup_mode = drush_get_context('BACKUP_MODE', FALSE);
  }
  drush_log(dt("DRUSH/GET/OPTION selected_backup_mode in drush_db_post_provision_deploy is: @var", array('@var' => $backup_mode)), 'info');

  if ($backup_mode != 'backup_mysqldump_only' && $backup_mode != 'site_files_with_mysqldump') {
    $aegir_root = d('@server_master')->aegir_root;
    $backup_path = d('@server_master')->backup_path;
    $oct_db_dirx = $backup_path . '/tmp_expim';
    $oct_db_test = $oct_db_dirx . '/metadata';
    $uri_db_dirz = $backup_path . '/' . d()->uri . '_sql_dump_deploy_' . date("Ymd.His", time());
    $post_import_flag = $backup_path . '/.post_import_flag.pid';
    $enable_myquick = $aegir_root . '/static/control/MyQuick.info';
  }

  if (is_file($enable_myquick) && is_file($oct_db_test)) {
    // Rename temporary sql dump dir to site/date specific backup.
    $old = $oct_db_dirx;
    $new = $uri_db_dirz;
    provision_file()->switch_paths($old, $new)
      ->succeed('Moving the Post-DB-Import @path1 to @path2 was successful.')
      ->fail('Moving the Post-DB-Import @path1 to @path2 has failed.');
    if (is_dir($new)) {
      _provision_recursive_delete($old);
      drush_log(dt("MyQuick tmp_expim dir removed in drush_db_post_provision_deploy @var", array('@var' => $old)), 'info');
    }
    provision_file()->unlink($post_import_flag)
      ->succeed('Removed Post-DB-Import Flag-File in drush_db_post_provision_deploy')
      ->fail('Could not remove Post-DB-Import Flag-File in drush_db_post_provision_deploy');
  }
  else {
    provision_file()->unlink(d()->site_path . '/database.sql')
      ->succeed('Removed dump file @path after restoring from it')
      ->fail('Could not remove dump file @path', 'DRUSH_PERM_ERROR');
  }
}
