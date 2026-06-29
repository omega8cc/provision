<?php

/**
 * Clone command implementation
 *
 * This command when called will
 *   1. Make a backup of the current site, before modifications are made.
 *   2. Execute 'provision deploy' to build the new site using the backup of the old site.
 *
 */

/**
 * Make sure we have a valid site being cloned, and that the file being cloned from exists
 */
function drush_provision_drupal_provision_clone_validate($new_name = null, $platform = null) {
  // Backup mode ctrl file cleanup
  unlink(AEGIR_BACKUP_MODE_CTRL);
  if (drush_drupal_major_version() >= 8) {
    // Lock Site-Local Drush if Unlocked
    provision_check_codebase_status();
    if (!d()->isConsolePatched || !d()->isCorePatched) {
      drush_log(dt('CLONE/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
      provision_lock_drush_local_vnd();
    }
  }
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
}

/**
 * Make a backup before making any changes, and add extract the file we are restoring from
 */
function drush_provision_drupal_pre_provision_clone($new_name, $platform = null) {
  drush_log(dt('Invoking drush provision-backup in drush_provision_drupal_pre_provision_clone'));
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  $internal_backup_flag_blank = "Internal-DB-Backup \n";
  $local_description = 'Adding Internal-DB-Backup Flag-File drush_provision_drupal_pre_provision_clone';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, $internal_backup_flag_blank)
      ->succeed('Generated blank ' . $local_description)
      ->fail('Could not generate ' . $local_description);
  }
  drush_invoke('provision-backup');
}

/**
 * Remove the extracted site directory
 */
function drush_provision_drupal_pre_provision_clone_rollback($new_name, $platform = null) {
  $success =  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused clone site package')
    ->fail('Could not remove unused clone site package');
}

/**
 * Switch the clone directories around now that we have the new db installed
 */
function drush_provision_drupal_provision_clone($new_name, $platform = null) {
  drush_set_option('old_platform', d()->platform->name);

  $aegir_root = d('@server_master')->aegir_root;
  $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

  if (!is_file($enable_fast_track)) {
    // If the site is cloned between platforms and not just in the same platform,
    // we should update the info collected about source and target platform first.
    if (!is_null(d($platform)->name) && (d($platform)->name != d()->platform->name)) {
      provision_backend_invoke('@hostmaster', 'hosting-task', array(d()->platform->name, 'verify'), array('force' => TRUE));
      sleep(5); // A small trick to avoid high load and race conditions.
      provision_backend_invoke('@hostmaster', 'hosting-task', array(d($platform)->name, 'verify'), array('force' => TRUE));
      sleep(5); // A small trick to avoid high load and race conditions.
    }
    // We should update also the info collected about the site before running clone.
    $local_uri_verify = '@' . d()->uri;
    provision_backend_invoke('@hostmaster', 'hosting-task', array($local_uri_verify, 'verify'), array('force' => TRUE));
    sleep(5); // A small trick to avoid high load and race conditions.
  }

  $options = d()->options;
  $options['uri'] = ltrim($new_name, '@');
  $hash_name = drush_get_option('#name') ? '#name' : 'name';
  $options[$hash_name] = $new_name;
  $options['platform'] = (isset($platform)) ? $platform : $options['platform'];
  $options['root'] = d($options['platform'])->root;
  $options['aliases'] = array();
  $options['redirection'] = 0;

  // Auto-disable Encryption on the clone. SSL keys/certs are bound to the
  // source machine-name and must not be reused by the new site name. The
  // front-end (hosting_clone) also persists this as the operator-visible state.
  $options['ssl_enabled'] = 0;
  unset($options['ssl_key']);
  drush_log(dt('[clone] Encryption disabled for new site @name before final verify', array('@name' => $new_name)), 'info');

  // Do not copy CDN aliases to the cloned site --CDN vhost
  $options['cdn'] = 0;
  unset($options['cdn']);

  if ($profile = drush_get_option('profile', FALSE)) {
    $options['profile'] = $profile;
  }

  if ($db_server = drush_get_option('new_db_server', FALSE)) {
    $options['db_server'] = $db_server;
  }

  drush_invoke_process('@none', 'provision-save', array($new_name), $options);
  # note that we reset the aliases so they don't conflict with the original site

  // Build an options array to pass to the deploy task. This allows injection
  // of data into the deploy task. See: https://www.drupal.org/node/2471805
  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'clone');

  provision_backend_invoke($new_name, 'provision-deploy', array(drush_get_option('backup_file')), $deploy_options);

  if (!drush_get_error()) {
    // Verify the newly cloned site.
    provision_backend_invoke($new_name, 'provision-verify');

    // Native files symlinking for the clone: give the new site its OWN copy of
    // the files/private dirs. A clone inherits a symlink into the SOURCE store
    // (default backups preserve symlinks), so --force-unshare breaks that
    // inherited link and moves the data into the new site's own store. The work
    // is delegated to the root autosymlink tool via its hardened sudo wrapper
    // (Provision is unprivileged and the store is root-managed). The post-deploy
    // verify above has written the vhost, so the account auto-resolves (no
    // --account). Warn-not-fail.
    _provision_drupal_native_symlink($options['uri'], TRUE);

    $aegir_root = d('@server_master')->aegir_root;
    $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

    if (!is_file($enable_fast_track)) {
      sleep(5); // A small trick to avoid high load and race conditions.
      // Verify again also original site via frontend to avoid issue #1004526
      provision_backend_invoke('@hostmaster', 'hosting-task', array($local_uri_verify, 'verify'), array('force' => TRUE));
    }

    $backup_path = d('@server_master')->backup_path;
    $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_drupal_provision_clone')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_drupal_provision_clone');
  }

  provision_reload_config('site', d()->site_path . '/drushrc.php');

}
