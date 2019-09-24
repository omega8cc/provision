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
  $myloader_path = '/usr/local/bin/myloader';
  $aegir_root = d('@server_master')->aegir_root;
  $backup_path = d('@server_master')->backup_path;
  $oct_db_dirx = $backup_path . '/tmp_expim';
  $oct_db_test = $oct_db_dirx . '/metadata';
  $post_import_flag = $backup_path . '/.post_import_flag.pid';
  $enable_myquick = $aegir_root . '/static/control/MyQuick.info';
  if (is_file($enable_myquick) && is_executable($myloader_path)) {
    if (is_file($oct_db_test)) {
      _provision_recursive_delete($oct_db_dirx);
      drush_log(dt("DEBUG MyQuick tmp_expim dir removed in drush_db_post_provision_deploy @var", array('@var' => $oct_db_dirx)), 'info');
      provision_file()->unlink($post_import_flag)
        ->succeed('Removed Post-DB-Import Flag-File')
        ->fail('Could not remove Post-DB-Import Flag-File');
    }
  }
  else {
    provision_file()->unlink(d()->site_path . '/database.sql')
      ->succeed('Removed dump file @path after restoring from it')
      ->fail('Could not remove dump file @path', 'DRUSH_PERM_ERROR');
  }
}
