<?php

/**
 * @file
 * Deploy command implementation
 *
 * This command when called will
 *   1. Extract the backup that is being deployed to the target folder in the sites directory.
 *   2. Import the drushrc.php details.
 *   3. Do some diagnostics to make sure all the needed packages are available.
 *   4. Create a new database, belonging to the site's user, and import the database dump.
 *   5. Regenerate configuration files to show new db settings.
 *   6. Call the drush 'updatedb' command to update the database if neccesary.
 *   7. Update the file paths to the new sites directory.
 *   8. Rebuild the site's package manifest.
 *   9. Save the new drushrc.php with the newly generated settings.
 */

/**
 * Make sure we have a valid site being deployd, and that the file being deployd from exists.
 *
 * Implementation of drush_hook_COMMAND_validate().
 */
function drush_provision_drupal_provision_deploy_validate($backup_file = NULL) {
  $exists = provision_file()->exists($backup_file)
    ->succeed('Deploying site from @path')
    ->fail('Could not find backup file @path', 'PROVISION_BACKUP_NOT_FOUND')
    ->status();
  if ($exists) {
    drush_set_option('backup_file', $backup_file);
  }

  $exists = provision_file()->exists(d()->site_path)
    ->succeed('Replacing the existing site at @path')
    ->status();

  if ($exists) {
    drush_set_option('extract_path', d()->site_path . '.restore');
    drush_set_option('old_db_name', drush_get_option('db_name', ''));
  }
  else {
    drush_set_option('extract_path', d()->site_path);
  }

  drush_set_option('deploy_replace_site', $exists);
}

/**
 * Make a backup before making any changes, and add extract the file we are restoring from.
 *
 * Implements drush_hook_pre_COMMAND().
 */
function drush_provision_drupal_pre_provision_deploy($backup_file) {
  $extracted = provision_file()->extract($backup_file, drush_get_option('extract_path'))
    ->succeed('Successfully extracted the contents of @path')
    ->fail('Failed to extract the contents of @path to @target', 'PROVISION_BACKUP_EXTRACTION_FAILED')
    ->status();

  if ($extracted) {
    // Make sure the files in the files directory are accessible by the web server.
    provision_file()->chgrp(drush_get_option('extract_path') . '/files', d('@server_master')->web_group, TRUE)
      ->succeed('Changed group ownership of files in <code>@path</code> to @gid')
      ->fail('Could not change group ownership of files in <code>@path</code> to @gid');
    provision_file()->chgrp(drush_get_option('extract_path') . '/private/files', d('@server_master')->web_group, TRUE)
      ->succeed('Changed group ownership of private files in <code>@path</code> to @gid')
      ->fail('Could not change group ownership of private files in <code>@path</code> to @gid');
    provision_file()->chgrp(drush_get_option('extract_path') . '/private/config', d('@server_master')->web_group, TRUE)
      ->succeed('Changed group ownership of config files in <code>@path</code> to @gid')
      ->fail('Could not change group ownership of config files in <code>@path</code> to @gid');
    provision_file()->chgrp(drush_get_option('extract_path') . '/private/temp', d('@server_master')->web_group, TRUE)
      ->succeed('Changed group ownership of temp files in <code>@path</code> to @gid')
      ->fail('Could not change group ownership of temp files in <code>@path</code> to @gid');

    if (drush_get_option('deploy_replace_site', FALSE)) {
      $old = d()->site_path . '.restore';
      $new = d()->site_path;

      $swapped = provision_file()->switch_paths($old, $new)
        ->succeed('Swapping out the @path1 and @path2 directories was successful.')
        ->fail('Swapping the @path1 and @path2 directories has failed.', 'DRUSH_PERM_ERROR')
        ->status();
      if ($swapped) {
        drush_set_option('site_dirs_swapped', TRUE);
      }
      else {
        return FALSE;
      }
    }

    // We have already created a new database. Save the info to the config files.
    provision_prepare_environment();
    provision_save_site_data();

    // Load the newly created information, including re-loading the new db creds.
    // Lock site-local Drush if unlocked
    $drush_lock_ctrl = d()->root . '/local_drush_locked.pid';
    $drush_unlock_ctrl = d()->root . '/local_drush_unlocked.pid';
    if (provision_file()->exists($drush_unlock_ctrl)->status() || !provision_file()->exists($drush_lock_ctrl)->status()) {
      drush_log(dt('LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
      provision_lock_drush_local_vnd();
    }
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
    _provision_drupal_create_settings_file();
    provision_drupal_push_site($override_slave_authority = TRUE);

    $site_packages = drush_get_option('packages', array(), 'site');
    $drupal_packages = drush_get_option('packages', array(), 'drupal');
    $merged_modules = isset($drupal_packages['base']['modules']) ? $drupal_packages['base']['modules'] : array();
    if (isset($site_packages['profiles'])) {
      $profiles = array_keys($site_packages['profiles']);
      $profile = $profiles[0];
      if (isset($drupal_packages['profiles'][$profile]['modules'])) {
        $merged_modules = array_merge($merged_modules, $drupal_packages['profiles'][$profile]['modules']);
      }
    }

    if (isset($site_packages['modules']) && is_array($site_packages['modules'])) {
      foreach ($site_packages['modules'] as $name => $module) {
        if ($module['status'] == 1) {
          if (isset($module['platform']) && $module['platform'] != '-1') {
            drush_log(dt("The !name module is installed in the site's modules directory. Skipping schema version check.", array('!name' => $name)), 'info');
          }
          elseif (!array_key_exists($name, $merged_modules)) {
            drush_log(dt("Could not find a version of the !name module", array('!name' => $name)), 'warning');
          }
          else {
            if (($merged_modules[$name]['schema_version'] > 0) && ($module['schema_version'] > $merged_modules[$name]['schema_version'])) {
              drush_set_error('PROVISION_SCHEMA_UPGRADE_FAILURE',
                dt("The version of the !name module found on this platform (!versionB) has a lower Schema version than the one the site has installed (!versionA)",
                array('!name' => $name, '!versionA' => $module['schema_version'], '!versionB' => $merged_modules[$name]['schema_version'])));
            }
            else {
              drush_log(dt("Found a valid version of the !name module with schema version !schema_version",
                array('!name' => $name, '!schema_version' => $merged_modules[$name]['schema_version'])));
            }
          }
        }
      }
    }
  }
}

/**
 * Remove the extracted site directory.
 *
 * Implementation of drush_hook_pre_COMMAND_rollback().
 */
function drush_provision_drupal_pre_provision_deploy_rollback() {
  if (drush_get_option('deploy_replace_site', FALSE)) {
    if (drush_get_option('site_dirs_swapped', FALSE)) {

      // Swap the site directories back if necessary.
      $old = d()->site_path . '.restore';
      $new = d()->site_path;

       provision_file()->switch_paths($old, $new)
         ->succeed('Swapping out the @path1 and @path2 directories was successful.')
         ->fail('Swapping the @path1 and @path2 directories has failed.', 'DRUSH_PERM_ERROR');

       _provision_drupal_create_settings_file();
       provision_save_site_data();
    }
  }

  if (provision_file()->exists(drush_get_option('extract_path'))->status()) {
    _provision_recursive_delete(drush_get_option('extract_path'));
  }
}

/**
 * Implements drush_hook_COMMAND().
 */
function drush_provision_drupal_provision_deploy() {
  _provision_drupal_maintain_aliases();
  _provision_drupal_create_directories();
}

/**
 * Custom helper function findAppRoot
 */
function findAppRoot($startDir) {
  $dir = realpath($startDir);
  while ($dir !== '/' && !is_dir($dir . '/vendor')) {
    $dir = dirname($dir);
  }
  return is_dir($dir . '/vendor') ? $dir : null;
}

/**
 * Custom helper function runDrush with respawn support
 */
function runDrush($appRoot, $command, $cacheClear = 'default') {
  // Normalize command if it is updb to updatedb
  if ($command[0] === 'updb') {
    $command[0] = 'updatedb';
  }

  // Add the --yes and --cache-clear options to bypass confirmation prompts and handle cache clearing
  array_push($command, '--yes', "--cache-clear=$cacheClear");

  // Prepare the command to run Drush
  $cmd = escapeshellcmd($drushPath) . ' ' . implode(' ', array_map('escapeshellarg', $command));

  // Change directory to app root
  chdir($appRoot);

  // Run the Drush command and handle respawning
  $descriptorspec = array(
    0 => array('pipe', 'r'),  // stdin
    1 => array('pipe', 'w'),  // stdout
    2 => array('pipe', 'w')   // stderr
  );

  $process = proc_open($cmd, $descriptorspec, $pipes, $appRoot);
  if (is_resource($process)) {
    // Capture the output and error streams
    $stdout = stream_get_contents($pipes[1]);
    $stderr = stream_get_contents($pipes[2]);

    // Close the streams
    fclose($pipes[1]);
    fclose($pipes[2]);

    // Wait for the process to complete
    $exitCode = proc_close($process);

    // Log the output and error
    logOutput($stdout, $stderr, $exitCode, $cmd);

    // Check if Drush command was successful
    if ($exitCode === 0) {
      logDrush('runDrush: command completed successfully: ' . $cmd, 'info');
    }
    else {
      logDrush('runDrush: command failed with exit code ' . $exitCode . ': ' . $cmd, 'warning');
    }
  }
  else {
    logDrush('runDrush: Failed to run command: ' . $cmd, 'warning');
  }
}

/**
 * Custom helper function logOutput
 */
function logOutput($stdout, $stderr, $exitCode, $cmd) {
  // Define the log file
  $aegir_root = d('@server_master')->aegir_root;
  $logFile = $aegir_root . '/log/drush_tmp_log.txt';

  // Prepare the log entry
  $logEntry = "Command: $cmd\n";
  $logEntry .= "Exit Code: $exitCode\n";
  $logEntry .= "Standard Output:\n$stdout\n";
  $logEntry .= "Standard Error:\n$stderr\n";
  $logEntry .= "=====================\n";

  // Write the log entry to the file
  file_put_contents($logFile, $logEntry);

  // Log the entry using Drush
  logDrush($logEntry, $exitCode === 0 ? 'info' : 'warning');
}

/**
 * Custom helper function logDrush
 */
function logDrush($message, $type = 'info') {
  // Check if Drush is available
  if (function_exists('drush_log')) {
    // Log the message using Drush
    drush_log($message, $type);
  }
  else {
    // Fallback logging mechanism
    error_log("[$type] $message");
  }
}

/**
 * Implements drush_hook_post_COMMAND().
 */
function drush_provision_drupal_post_provision_deploy() {
  // Run registry-rebuild without cache-clear, if available.
  if (function_exists('drush_registry_rebuild')) {
    if (drush_drupal_major_version() < 8 && !provision_is_hostmaster_site()) {
      provision_backend_invoke(d()->name, 'registry-rebuild --no-cache-clear');
      drush_log(dt('Registry rebuilt with --no-cache-clear in drush_provision_drupal_post_provision_deploy'));
    }
  }

  $aegir_root = d('@server_master')->aegir_root;
  $dis_auto_up_db_global = $aegir_root . '/static/control/DisAutoUpDb.info';
  $dis_auto_up_db_site = $aegir_root . '/static/control/' . d()->uri . '_DisAutoUpDb.info';
  if (provision_is_hostmaster_site()) {
    // Call the drush updatedb command.
    drush_log(dt('Invoking updatedb in drush_provision_drupal_post_provision_deploy'), 'info');
    provision_backend_invoke(d()->name, 'updatedb');
  }
  else {
    if (is_file($dis_auto_up_db_global)) {
      drush_log(dt('Database update skipped due to the presence of a control file @path', array('@path' => $dis_auto_up_db_global)), 'info');
    }
    elseif (is_file($dis_auto_up_db_site)) {
      drush_log(dt('Database update skipped due to the presence of a control file @path', array('@path' => $dis_auto_up_db_site)), 'info');
    }
    else {
      if (drush_drupal_major_version() >= 10) {
        // Call the drush updatedb command with site-local Drush

        // Unlock site-local Drush in the old platform too
        if (!drush_get_option('deploy_replace_site', FALSE)) {
          if (drush_get_option('old_platform', FALSE)) {
            provision_backend_invoke($old_platform, 'provision-unlock');
            sleep(5);
          }
        }

        // Update Drush 10 aliases
        drush_log(dt('DRUSH/TEN/ALIASES/UPDATE drush10 site:alias-convert ~/.drush/sites --yes in @aegir_root', array('@aegir_root' => $aegir_root)), 'info');
        $result = drush_shell_cd_and_exec($aegir_root, 'drush10 site:alias-convert ~/.drush/sites --yes');
        $output = implode('', drush_shell_exec_output());
        drush_log("DRUSH/TEN/ALIASES/LOG " . $output);

        // Unlock site-local Drush in the new platform
        $drush_lock_ctrl = d()->root . '/local_drush_locked.pid';
        if (provision_file()->exists($drush_lock_ctrl)->status()) {
          drush_log(dt('LOCAL/DRUSH/UNLOCK with provision_unlock_drush_local_vnd'), 'info');
          provision_unlock_drush_local_vnd();
        }

        // Define the initial path and Drush command
        $initialPath = d()->root;
        $drushCommand = ['updb'];

        // Find the app root directory
        $appRoot = findAppRoot($initialPath);

        // Define local drush path
        $drushPath = 'vendor/bin/drush';

        // Check if the local Drush exists and is executable
        $drushAbsolutePath = $appRoot . '/' . $drushPath;
        drush_log(dt('LOCAL/DRUSH/CHECK Path @path', array('@path' => $drushAbsolutePath)), 'info');

        if (!is_executable($drushAbsolutePath)) {
          // Lock site-local Drush if unlocked
          $drush_lock_ctrl = d()->root . '/local_drush_locked.pid';
          $drush_unlock_ctrl = d()->root . '/local_drush_unlocked.pid';
          if (provision_file()->exists($drush_unlock_ctrl)->status() || !provision_file()->exists($drush_lock_ctrl)->status()) {
            drush_log(dt('LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
            provision_lock_drush_local_vnd();
          }
          drush_log(dt('Drush executable not found or is not executable at @drush_path', array('@drush_path' => $drushAbsolutePath)), 'info');
          // Call the drush updatedb command with Aegir own Drush 8
          drush_log(dt('Invoking updatedb in drush_provision_drupal_post_provision_deploy'), 'info');
          drush_log(dt('To skip updatedb please create an empty control file @path', array('@path' => $dis_auto_up_db_global)), 'info');
          provision_backend_invoke(d()->name, 'updatedb');
        }
        else {
          drush_log(dt('LOCAL/DRUSH/CMD vendor/bin/drush --version in @appRoot', array('@appRoot' => $appRoot)), 'info');
          $result = drush_shell_cd_and_exec($appRoot, 'vendor/bin/drush --version');
          $output = implode('', drush_shell_exec_output());
          drush_log("LOCAL/DRUSH/VERSION " . $output);
          // Run the Drush command with the --cache-clear option set to 'default'
          // runDrush($appRoot, $drushCommand, 'default');
          $dnameten = preg_replace('/\.(?![^.]*$)/', '-', d()->name);
          drush_log(dt('LOCAL/DRUSH/CMD vendor/bin/drush @dnameten updatedb in @appRoot', array('@dnameten' => $dnameten, '@appRoot' => $appRoot)), 'info');
          $result = drush_shell_cd_and_exec($appRoot, 'vendor/bin/drush ' . $dnameten . ' updatedb --yes --cache-clear=default');
          $output = implode('', drush_shell_exec_output());
          drush_log("LOCAL/DRUSH/LOG " . $output);
          // Lock site-local Drush again
          // Lock site-local Drush if unlocked
          $drush_lock_ctrl = d()->root . '/local_drush_locked.pid';
          $drush_unlock_ctrl = d()->root . '/local_drush_unlocked.pid';
          if (provision_file()->exists($drush_unlock_ctrl)->status() || !provision_file()->exists($drush_lock_ctrl)->status()) {
            drush_log(dt('LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
            provision_lock_drush_local_vnd();
          }
          // Lock site-local Drush in the old platform again
          if (!drush_get_option('deploy_replace_site', FALSE)) {
            if (drush_get_option('old_platform', FALSE)) {
              provision_backend_invoke($old_platform, 'provision-verify');
              sleep(5);
            }
          }
        }
      }
      else {
        // Call the drush updatedb command with Aegir own Drush 8
        // Lock site-local Drush if unlocked
        $drush_lock_ctrl = d()->root . '/local_drush_locked.pid';
        $drush_unlock_ctrl = d()->root . '/local_drush_unlocked.pid';
        if (provision_file()->exists($drush_unlock_ctrl)->status() || !provision_file()->exists($drush_lock_ctrl)->status()) {
          drush_log(dt('LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
          provision_lock_drush_local_vnd();
        }
        drush_log(dt('Invoking updatedb in drush_provision_drupal_post_provision_deploy'), 'info');
        drush_log(dt('To skip updatedb please create an empty control file @path', array('@path' => $dis_auto_up_db_global)), 'info');
        provision_backend_invoke(d()->name, 'updatedb');
      }
    }
  }

  // Run registry-rebuild with cache-clear, if available.
  if (function_exists('drush_registry_rebuild')) {
    if (drush_drupal_major_version() < 8 && !provision_is_hostmaster_site()) {
      provision_backend_invoke(d()->name, 'registry-rebuild');
      drush_log(dt('Registry rebuilt without --no-cache-clear in drush_provision_drupal_post_provision_deploy'));
    }
  }
  if (drush_drupal_major_version() < 12) {
    // We should be able to fully load Drupal now.
    if (drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL)) {
      drush_include_engine('drupal', 'deploy');
      drush_set_option('packages', _scrub_object(provision_drupal_system_map()), 'site');
      _provision_drupal_rebuild_caches();

      // Rebuild the node access tables only if necessary.
      if (!function_exists("node_access_needs_rebuild") || node_access_needs_rebuild()) {
        if (function_exists("node_access_rebuild")) {
          node_access_rebuild();
          drush_log(dt('Rebuilt node access table'));
        }
        else {
          drush_log(dt('OPS, NO rebuild of node access table available'));
        }
      }
    }
    else {
      drush_log("could not bootstrap drupal after updatedb");
    }
  }
  else {
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_CONFIGURATION);
    drush_log("Skipping FULL bootstrap for drupal 12+ in drush_provision_drupal_post_provision_deploy");
  }

  // Remove the restore directory.
  if (!drush_get_error() && drush_get_option('deploy_replace_site', FALSE)) {
    _provision_recursive_delete(drush_get_option('extract_path'));
  }

  // Remove the old database.
  if (!drush_get_error() && drush_get_option('deploy_replace_site', FALSE)) {
    if ($old_database = drush_get_option('old_db_name', '')) {
      if (!d()->service('db')->drop_database($old_database)) {
        drush_log(dt('Failed to drop database @old_database', array('@old_database' => $old_database)), 'warning');
      }
      else {
        drush_log(dt('Dropped the old database (@old_database).', array('@old_database' => $old_database)), 'info');
      }
    }
  }
}
