<?php
/**
 * @file
 * Migrate command implementation
 */

/**
 * Make sure we have a valid site being migrated, and that the file being migrated from exists
 *
 * Implementation of drush_hook_COMMAND_validate().
 */
function drush_provision_drupal_provision_migrate_validate($platform = NULL) {
  // Lock Site-Local Drush if Unlocked
  $drush_lock_ctrl = d()->root . '/local_drush_locked.pid';
  $drush_unlock_ctrl = d()->root . '/local_drush_unlocked.pid';
  if (provision_file()->exists($drush_unlock_ctrl)->status() || !provision_file()->exists($drush_lock_ctrl)->status()) {
    drush_log(dt('LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
    provision_lock_drush_local_vnd();
  }
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
}

/**
 * Make a backup before making any changes, and add extract the file we are restoring from
 *
 * Implementation of drush_hook_pre_COMMAND().
 */
function drush_provision_drupal_pre_provision_migrate($platform, $new_name = NULL) {
  // Set offline mode to true and re-generate the settings.php file.
  // This will write a $conf['site_offline'] = 1; to the settings.php file.
  drush_log(dt("Putting site under maintenance"));
  d()->site_enabled = FALSE;
  _provision_drupal_create_settings_file();
  drush_log(dt('Invoking drush provision-backup in drush_provision_drupal_pre_provision_migrate'));
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  $internal_backup_flag_blank = "Internal-DB-Backup \n";
  $local_description = 'Adding Internal-DB-Backup Flag-File drush_provision_drupal_pre_provision_migrate';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, $internal_backup_flag_blank)
      ->succeed('Generated blank ' . $local_description)
      ->fail('Could not generate ' . $local_description);
  }
  drush_invoke('provision-backup');
  provision_drupal_push_site();

  drush_set_option('old_platform', d()->platform->name);

  // If we are renaming the site, pick this up early.
  if (!is_null($new_name) && ($new_name != d()->name)) {
    drush_set_option('target_name', $new_name);
    drush_set_option('source_name', d()->uri);
  }
  else {
    drush_set_option('target_name', d()->name);

    // Detect if the platform stays the same.
    if (d()->platform->name === $platform) {
      drush_log('Replacing the existing site on the same platform');
      // deploying over existing site.
      drush_set_option('deploy_replace_site', TRUE);
    }
  }
}

/**
 * Remove the extracted site directory
 * Restore the vhost conf per the original platform
 *
 * Implementation of drush_hook_pre_COMMAND_rollback().
 */
function drush_provision_drupal_pre_provision_migrate_rollback($platform, $new_name = NULL) {
  // Set site_offline to false and regenerate the config.
  drush_log(dt("Bringing site out of maintenance"));
  d()->site_enabled = true;
  _provision_drupal_create_settings_file();
  provision_drupal_push_site();

  $success =  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused migration site package')
    ->fail('Could not remove unused migration site package');
  d()->service('http')->create_config('site');
  d()->service('http')->parse_configs();
}

/**
 * Switch the migrate directories around now that we have the new db installed
 *
 * Implementation of drush_hook_COMMAND().
 */
function drush_provision_drupal_provision_migrate($platform, $new_name = NULL) {

  $target = drush_get_option('target_name');
  $options = array();
  $options = d()->options;
  $options['uri'] = d()->uri;

  $aegir_root = d('@server_master')->aegir_root;
  $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

  if (!is_file($enable_fast_track)) {
    // If the site is migrated between platforms and not just renamed,
    // we should update the info collected about source and target platform first.
    // Note that we have to exclude Hostmaster platform from this extra verify.
    if (!provision_is_hostmaster_site() && drush_get_option('provision_verify_platforms_before_migrate', TRUE)) {
      if (!is_null(d($platform)->name) && (d($platform)->name != d()->platform->name)) {
        provision_backend_invoke('@hostmaster', 'hosting-task', array(d()->platform->name, 'verify'), array('force' => TRUE));
        sleep(5); // A small trick to avoid high load and race conditions.
        provision_backend_invoke('@hostmaster', 'hosting-task', array(d($platform)->name, 'verify'), array('force' => TRUE));
        sleep(5); // A small trick to avoid high load and race conditions.
      }
    }
  }

  if (!is_null($new_name) && ($new_name != d()->name)) {
    // XXX: right now we just drop SSL configuration when migrating, because
    // we do not check if the site name change is compatible with the
    // certificate. This can be removed when we a) check for wildcard
    // certs and b) allow the user to change the cert from the UI.
    $options['ssl_enabled'] = 0;
    unset($options['ssl_key']);

    $options['uri'] = ltrim($new_name, '@');
    $hash_name = drush_get_option('#name') ? '#name' : 'name';
    $options[$hash_name] = $new_name;
    $new_aliases = array();
    $old_aliases = (is_array($options['aliases'])) ? $options['aliases'] : FALSE;
    $pre_old_uri = ltrim(d()->uri, '@');
    $pre_new_uri = ltrim($new_name, '@');
    $raw_old_uri = preg_replace('/^www\./', '', $pre_old_uri);
    $raw_new_uri = preg_replace('/^www\./', '', $pre_new_uri);
    $www_old_uri = "www." . $raw_old_uri;
    $www_new_uri = "www." . $raw_new_uri;
    if (is_array($old_aliases) && !empty($old_aliases)) {
      foreach ($old_aliases as $alias) {
        drush_log(dt("LOG/RENAME old_aliases is @this", array('@this' => $alias)), 'info');
        if (!preg_match("/^www\./", $pre_old_uri) && preg_match("/^www\./", $pre_new_uri)) {
          $new_aliases[] = str_replace($www_old_uri, $raw_new_uri, $alias);
        }
        elseif (preg_match("/^www\./", $pre_old_uri) && !preg_match("/^www\./", $pre_new_uri)) {
          $new_aliases[] = str_replace($raw_old_uri, $www_new_uri, $alias);
        }
        elseif (!preg_match("/^www\./", $pre_old_uri) && !preg_match("/^www\./", $pre_new_uri)) {
          $new_aliases[] = str_replace($www_old_uri, $www_new_uri, $alias);
        }
        elseif (preg_match("/^www\./", $pre_old_uri) && preg_match("/^www\./", $pre_new_uri)) {
          $new_aliases[] = str_replace($raw_old_uri, $raw_new_uri, $alias);
        }
      }
      $unique_aliases = array_unique($new_aliases); // Make sure there are no duplicates.
      $options['aliases'] = array(); // Reset original aliases array.
      $options['aliases'] = $unique_aliases; // Use rewritten aliases array.
      if (!is_null(d($platform)->name) && (d($platform)->name === d()->platform->name)) {
        drush_set_option('rename_only', TRUE);
        foreach ($new_aliases as $alias) {
          drush_log(dt("LOG/RENAME new_aliases is @this", array('@this' => $alias)), 'info');
        }
        foreach ($unique_aliases as $alias) {
          drush_log(dt("LOG/RENAME unique_aliases is @this", array('@this' => $alias)), 'info');
        }
      }
      drush_set_option('aliases', $unique_aliases);
    }
    else {
      $detected_aliases = provision_drupal_find_aliases();
      foreach ($detected_aliases as $alias) {
        drush_log(dt("LOG/RENAME detected_aliases is @this", array('@this' => $alias)), 'info');
      }
      drush_set_option('aliases', $detected_aliases);
    }
    // Warning: do not try to re-verify the original site here
    // even with backend-only verify, because it would create
    // (and never delete) duplicate vhost with old domain
    // and all aliases included - see issue #1067702.
  }
  else {

    $aegir_root = d('@server_master')->aegir_root;
    $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

    if (!is_file($enable_fast_track)) {

      // We have to exclude Hostmaster site from any extra verify steps.
      if (!provision_is_hostmaster_site()) {
        // We should update also the info collected about the site before running migrate task.
        // Doing this is safe only when the site is migrated with the same name - see issue #1067702.
        $local_uri_verify = '@' . d()->uri;
        // XXX: right now we just pause SSL configuration when migrating,
        // even if the site name doesn't change, because of possible
        // edge case situations where the task had errors preventing it
        // from managing vhost and respective SSL keys updates properly.
        if ($options['ssl_enabled'] && $options['ssl_key']) {
          $encryption_was_enabled = "YES";
          $temp_options_ssl_enabled = $options['ssl_enabled'];
          $temp_options_ssl_key = $options['ssl_key'];
          $temp_uri_verify = $local_uri_verify;
          drush_log(dt("LOG/TO DISABLE ssl_enabled WAS @this", array('@this' => $options['ssl_enabled'])), 'info');
          drush_log(dt("LOG/TO DISABLE ssl_key WAS @this", array('@this' => $options['ssl_key'])), 'info');
          drush_log(dt("LOG/TO DISABLE temp_uri_verify WAS @this", array('@this' => $temp_uri_verify)), 'info');
          drush_set_option('encryption_was_enabled', TRUE);
          drush_set_option('temp_options_ssl_enabled', $options['ssl_enabled']);
          drush_set_option('temp_options_ssl_key', $options['ssl_key']);
          drush_set_option('temp_uri_verify', $temp_uri_verify);
          $options['ssl_enabled'] = 0;
          unset($options['ssl_key']);
          drush_log(dt("LOG/DISABLED ssl_enabled WAS @this", array('@this' => $options['ssl_enabled'])), 'info');
          drush_log(dt("LOG/DISABLED ssl_key WAS @this", array('@this' => $options['ssl_key'])), 'info');
          drush_log(dt("LOG/DISABLED temp_uri_verify WAS @this", array('@this' => $temp_uri_verify)), 'info');
          drush_invoke_process('@none', 'provision-save', array($temp_uri_verify), $options);
          sleep(5); // A small trick to avoid high load and race conditions.
        }
        provision_backend_invoke('@hostmaster', 'hosting-task', array($local_uri_verify, 'verify'), array('force' => TRUE));
        sleep(5); // A small trick to avoid high load and race conditions.
      }
    }
  }
  $options['platform'] = $platform;
  $options['root'] = d($platform)->root;

  if ($profile = drush_get_option('profile', FALSE)) {
    $options['profile'] = $profile;
  }

  if ($db_server = drush_get_option('new_db_server', FALSE)) {
    $options['db_server'] = $db_server;
  }

  drush_set_option('new_site_path', "{$options['root']}/sites/{$options['uri']}");

  drush_invoke_process('@none', 'provision-save', array($target), $options);

  // Build an options array to pass to the deploy task. This allows injection
  // of data into the deploy task. See: https://www.drupal.org/node/2471805
  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'migrate');

  provision_backend_invoke($target, 'provision-deploy', array(drush_get_option('backup_file')), $deploy_options);
  if (!drush_get_error()) {
    drush_log(dt("Bringing site out of maintenance"));
    d()->site_enabled = TRUE;
    drush_set_option('provision_save_config', FALSE);
    provision_backend_invoke($target, 'provision-verify');

    $aegir_root = d('@server_master')->aegir_root;
    $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

    if (!is_file($enable_fast_track)) {
      sleep(5); // A small trick to avoid high load and race conditions.
      // Re-enable encryption again if needed and only when platform has changed
      $encryption_was_enabled = drush_get_option('encryption_was_enabled');
      if ($encryption_was_enabled) {
        drush_log(dt("LOG/ENABLE encryption_was_enabled WAS @this", array('@this' => $encryption_was_enabled)), 'info');
        $temp_options_ssl_enabled = drush_get_option('encryption_was_enabled');
        $temp_options_ssl_key = drush_get_option('encryption_was_enabled');
        $temp_uri_verify = drush_get_option('temp_uri_verify');
        if ($temp_options_ssl_enabled && $temp_options_ssl_key) {
          drush_log(dt("LOG/ENABLE temp_options_ssl_enabled WAS @this", array('@this' => $temp_options_ssl_enabled)), 'info');
          drush_log(dt("LOG/ENABLE temp_options_ssl_key WAS @this", array('@this' => $temp_options_ssl_key)), 'info');
          drush_log(dt("LOG/ENABLE temp_uri_verify WAS @this", array('@this' => $temp_uri_verify)), 'info');
          $options['ssl_enabled'] = $temp_options_ssl_enabled;
          $options['ssl_key'] = $temp_options_ssl_key;
          drush_invoke_process('@none', 'provision-save', array($temp_uri_verify), $options);
          sleep(5); // A small trick to avoid high load and race conditions.
          provision_backend_invoke('@hostmaster', 'hosting-task', array($temp_uri_verify, 'verify'), array('force' => TRUE));
          sleep(5); // A small trick to avoid high load and race conditions.
        }
      }
    }
  }
}

/**
 * Something went wrong
 *
 * Implementation of drush_hook_COMMAND_rollback().
 */
function drush_provision_drupal_provision_migrate_rollback($platform) {
  drush_invoke_process('@none', 'provision-save', array(d()->name), array('platform' => drush_get_option('old_platform'), 'root' => NULL, 'uri' => NULL));
}

/**
 * Delete the old site directory and recreate the settings file
 *
 * Implementation of drush_hook_post_COMMAND().
 */
function drush_provision_drupal_post_provision_migrate($platform, $new_name = NULL) {
  drush_set_option('installed', FALSE);

  if (!drush_get_option('deploy_replace_site', FALSE)) {
    // we remove the aliases even if redirection is enabled as a precaution
    // if redirection is enabled, keep silent about errors
    _provision_drupal_delete_aliases();
    _provision_recursive_delete(d()->site_path);
    d()->service('http')->sync(d()->site_path);
  }

  if (!is_null($new_name) && ($new_name != d()->name)) {
    // Use correct context for cleanup operations.
    $source_name = drush_get_option('source_name');
    drush_log(dt("LOG/RENAME source_name is @this", array('@this' => $source_name)), 'info');
    // Remove the existing drush alias.
    $config = new Provision_Config_Drushrc_Alias(d()->name);
    $config->unlink();
    // Delete the old site directory.
    _provision_recursive_delete(d($source_name)->site_path);
    d()->service('http')->sync(d($source_name)->site_path);
    // Remove old site name aliases.
    _provision_drupal_delete_aliases();
    // Remove the symlink in the clients directory.
    _provision_client_delete_symlink();
  }

  $rename_only = drush_get_option('rename_only');
  if ($rename_only) {
    // We run this extra tricky backend verify to avoid issue #1004526.
    if (!provision_is_hostmaster_site()) {
      drush_log(dt("LOG/RENAME new_uri is @this", array('@this' => $new_name)), 'info');
      $deploy_replace_site = drush_get_option('deploy_replace_site');
      drush_log(dt("LOG/RENAME deploy_replace_site is @this", array('@this' => $deploy_replace_site)), 'info');
      $final_aliases = drush_get_option('aliases');
      foreach ($final_aliases as $alias) {
        drush_log(dt("LOG/RENAME final_aliases is @this", array('@this' => $alias)), 'info');
      }
      provision_reload_config('site', drush_get_option('new_site_path', d($new_name)->site_path) . '/drushrc.php');
      d($new_name)->aliases = drush_set_option('aliases', $final_aliases);
      d($new_name)->write_alias();
      drush_set_option('aliases', drush_get_option('aliases'),  'site');
      drush_set_option('provision_save_config', false);
      provision_backend_invoke($new_name, 'provision-verify');
    }
  }
  else {
    // Load the config file of the newly migrated site and return it to hosting.
    provision_reload_config('site', drush_get_option('new_site_path', d()->site_path) . '/drushrc.php');
  }

  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  provision_file()->unlink($internal_backup_flag)
    ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_drupal_post_provision_migrate')
    ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_drupal_post_provision_migrate');
}
