<?php

/**
 * Provision backup command
 *
 * Back up an existing site
 */

const PROVISION_BACKUP_EXCLUDED_DIRECTORIES = array(
  './files/css',
  './files/js',
  './private/temp',
);

/**
 * Make sure the site is installed and enabled, and that we have a valid target to back up to.
 */
function drush_provision_drupal_provision_backup_validate($backup_file = NULL) {
  if (!@drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_CONFIGURATION)) {
    if (drush_get_option('force', false)) {
      drush_log("clearing error");
      drush_set_context('DRUSH_ERROR_CODE', DRUSH_SUCCESS);
    }
  }
  if (!drush_get_option('installed') && !drush_get_option('force', false)) {
     drush_set_error('PROVISION_DRUPAL_SITE_NOT_FOUND');
   }

  // This is the actual drupal provisioning requirements.
  if (!is_dir(d()->platform->server->backup_path)) {
    drush_set_error('PROVISION_BACKUP_PATH_NOT_FOUND');
  }

  if ($backup_file) {
    if (provision_file()->exists($backup_file)->status()) {
      drush_set_error('PROVISION_BACKUP_ALREADY_EXISTS', dt('Back up file @path already exists.', array('@path' => $backup_file)));
    }
    else {
      drush_log(dt('Backing site up to @path.', array('@path' => $backup_file)), 'info');
      drush_set_option('backup_file', $backup_file);
    }
  }

  if (!$backup_file) {
    $suggested = drush_provision_drupal_provision_backup_suggest_filename();
    drush_set_option('backup_file', $suggested);
  }

}

/**
 * Util function to suggest a filename for a new site backup.
 */
function drush_provision_drupal_provision_backup_suggest_filename() {
  $suffix = drush_get_option('provision_backup_suffix', '.tar');
  $suggested = d()->platform->server->backup_path . '/' . d()->uri . '-' . date("Ymd.His", time()) . $suffix;

  // Use format of mysite.com-2008-01-02, if already existing, add number.
  $count = 0;
  while (is_file($suggested)) {
    $count++;
    $suggested = d()->platform->server->backup_path . '/' . d()->uri . '-' .  date('Ymd.His', time()) . '_' . $count . $suffix;
  }
  return $suggested;
}

/**
 * Implentation of hook_provision_backup()
 */
function drush_provision_drupal_provision_backup() {
  $backup_file = drush_get_option('backup_file');
  // Adds the site directory into the backup file
  drush_log(dt("Adding sites directory to !backup_file", array('!backup_file' => $backup_file)), 'backup');


  // synch all filesystem changes back from the remote server.
  provision_drupal_fetch_site();

  // Check if we are currently cloaking credentials
  $cloaked = d()->service('http')->cloaked_db_creds();
  $cloaked = drush_get_option('provision_db_cloaking', $cloaked);

  if ($cloaked) {
    drush_set_option('cloaking_off_temp', TRUE);
    // Disable the cloaking of credentials temporarily
    drush_log(dt("Temporarily uncloaking database credentials for backup"));
    drush_set_option('provision_db_cloaking', FALSE);

    // Write the uncloaked credentials to the settings.php
    _provision_drupal_create_settings_file();
    provision_drupal_push_site();

  }

  $olddir = getcwd();

  if (!chdir(d()->site_path)) {
    return drush_set_error('PROVISION_BACKUP_PATH_NOT_FOUND', dt('cannot change directory to %dir', array('%dir' => d()->site_path)));
  }

  $exclude_tag = "touch" . drush_provision_drupal_provision_backup_get_exclusions();
  drush_shell_exec($exclude_tag);

  $command_base = "tar cpf";
  $command_arguments = " %s --exclude-tag=exclude.tag";
  if (substr($backup_file, -2) == 'gz') {
    $command = $command_base . 'z' . $command_arguments;
  }
  elseif (substr($backup_file, -3) == 'bz2') {
    $command = $command_base . 'j' . $command_arguments;
  } else {
    $command = $command_base . $command_arguments;
  }
  $command .= ' .';
  $result = drush_shell_exec($command,  $backup_file);

  // Get the size of the backup
  $size = filesize($backup_file);
  drush_set_option('backup_file_size', $size);

  chdir($olddir);

  if (drush_get_option('cloaking_off_temp', FALSE)) {
    drush_log(dt("Re-cloaking database credentials after backup"));
    drush_set_option('provision_db_cloaking', TRUE);
    _provision_drupal_create_settings_file();
    provision_drupal_push_site();

  }

  if (!$result && !drush_get_option('force', false)) {
    drush_set_error('PROVISION_BACKUP_FAILED', dt("Could not back up sites directory for drupal"));
  }
}

function drush_provision_drupal_provision_backup_get_exclusions() {
  $directories = PROVISION_BACKUP_EXCLUDED_DIRECTORIES;
  drush_command_invoke_all_ref('provision_backup_exclusions_alter', $directories);

  $exclusions = "";
  foreach ($directories as $directory) {
    $exclusions .= " '$directory'/exclude.tag";
  }
  return $exclusions;
}

function drush_provision_drupal_post_provision_backup() {
  drush_log(dt('Backed up site up to @path.', array('@path' => drush_get_option('backup_file'))), 'success');

  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  if (!is_file($internal_backup_flag)) {
    $aegir_root = d('@server_master')->aegir_root;
    $backup_path = d('@server_master')->backup_path;
    $oct_db_dirx = $backup_path . '/tmp_expim';
    $oct_db_test = $oct_db_dirx . '/metadata';
    $uri_db_dirz = $backup_path . '/' . d()->uri . '_sql_dump_backup_' . date("Ymd.His", time());
    $enable_myquick = $aegir_root . '/static/control/MyQuick.info';
    if (is_file($enable_myquick) && is_file($oct_db_test)) {
      $old = $oct_db_dirx;
      $new = $uri_db_dirz;
      provision_file()->switch_paths($old, $new)
        ->succeed('Moving the Post-DB-Backup @path1 to @path2 was successful.')
        ->fail('Moving the Post-DB-Backup @path1 to @path2 has failed.');
      if (is_dir($new)) {
        _provision_recursive_delete($old);
        drush_log(dt("DEBUG MyQuick tmp_expim dir removed in drush_provision_drupal_post_provision_backup @var", array('@var' => $old)), 'info');
      }
    }
  }

  if (d()->client_name) {
    $backup_dir = d()->server->clients_path . '/' . d()->client_name . '/backups';
    provision_file()->create_dir($backup_dir, dt('Client backup directory for @client', array('@client' => d()->client_name)), 0750);
    provision_file()->symlink(drush_get_option('backup_file'), $backup_dir . '/' . basename(drush_get_option('backup_file')))
      ->succeed('Created symlink @path to @target')
      ->fail('Could not create symlink @path to @target: @reason');
  }
}

/**
 * Remove the backup file if something went wrong
 */
function drush_provision_drupal_provision_backup_rollback() {
  $backup_file = drush_get_option('backup_file');
  if (is_readable($backup_file) && is_file($backup_file)) {
    provision_file()->unlink($backup_file)
      ->succeed('Removed stale backup file @path')
      ->fail('Failed deleting backup file @path');
    $aegir_root = d('@server_master')->aegir_root;
    $backup_path = d('@server_master')->backup_path;
    $oct_db_dirx = $backup_path . '/tmp_expim';
    $oct_db_test = $oct_db_dirx . '/metadata';
    $uri_db_dirz = $backup_path . '/' . d()->uri . '_sql_dump_rollback_' . date("Ymd.His", time());
    $enable_myquick = $aegir_root . '/static/control/MyQuick.info';
    if (is_file($enable_myquick) && is_file($oct_db_test)) {
      $old = $oct_db_dirx;
      $new = $uri_db_dirz;
      provision_file()->switch_paths($old, $new)
        ->succeed('Moving the Post-DB-Cleanup @path1 to @path2 was successful.')
        ->fail('Moving the Post-DB-Cleanup @path1 to @path2 has failed.');
      if (is_dir($new)) {
        _provision_recursive_delete($old);
        drush_log(dt("DEBUG MyQuick tmp_expim dir removed in drush_provision_drupal_provision_backup_rollback @var", array('@var' => $old)), 'info');
      }
    }
  }
}
