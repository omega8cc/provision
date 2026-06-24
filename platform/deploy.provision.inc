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
 *   6. Call the drush 'updatedb -y' command to update the database if neccesary.
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
    if (drush_drupal_major_version() >= 8) {
      // Lock Site-Local Drush if Unlocked
      provision_check_codebase_status();
      if (!d()->isConsolePatched || !d()->isCorePatched) {
        drush_log(dt('DEPLOY1/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
        provision_lock_drush_local_vnd();
      }
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
    // Always Call The Drush updatedb Command With Aegir Own Drush 8 In The Hostmaster Site
    drush_log(dt('AEGIR/DRUSH Invoking updatedb in drush_provision_drupal_post_provision_deploy'), 'info');
    provision_backend_invoke(d()->name, 'updatedb -y');
  }
  else {
    if (is_file($dis_auto_up_db_global)) {
      drush_log(dt('DRUSH/CTRL/UPDATEDB skipped due to the presence of a control file @path', array('@path' => $dis_auto_up_db_global)), 'info');
    }
    elseif (is_file($dis_auto_up_db_site)) {
      drush_log(dt('DRUSH/CTRL/UPDATEDB skipped due to the presence of a control file @path', array('@path' => $dis_auto_up_db_site)), 'info');
    }
    else {
      if (drush_drupal_major_version() >= 10) {

        // Unlock Site-Local Drush In The Old Platform Too
        // Otherwise Site-Local Drush In The New Platform Would Complain
        if (!drush_get_option('deploy_replace_site', FALSE)) {
          if (drush_get_option('old_platform', FALSE)) {
            provision_backend_invoke($old_platform, 'provision-unlock');
            sleep(5);
          }
        }

        // Update Drush 10+ Aliases
        drush_log(dt('DRUSH/TEN/ALIASES/UPDATE drush10 site:alias-convert ~/.drush/sites --yes in @aegir_root', array('@aegir_root' => $aegir_root)), 'info');
        $result = drush_shell_cd_and_exec($aegir_root, 'drush10 site:alias-convert ~/.drush/sites --yes');
        $output = implode('', drush_shell_exec_output());
        drush_log("DRUSH/TEN/ALIASES/LOG " . $output);

        // Unlock Site-Local Drush In The New Platform
        provision_check_codebase_status();
        if (d()->isConsolePatched || d()->isCorePatched) {
          drush_log(dt('DEPLOY2/LOCAL/DRUSH/UNLOCK with provision_unlock_drush_local_vnd'), 'info');
          provision_unlock_drush_local_vnd();
        }

        // Define Drush Command
        $drushCommand = ['updb'];

        // Find The App Root Directory
        $appRoot = findAppRoot(d()->root);

        // Define Local Drush Path
        $drushPath = 'vendor/drush/drush/drush.php';

        // Check If The Local Drush Exists And Is Executable
        $drushAbsolutePath = $appRoot . '/' . $drushPath;
        drush_log(dt('DEPLOY3/LOCAL/DRUSH/CHECK Path @path', array('@path' => $drushAbsolutePath)), 'info');

        if (!is_executable($drushAbsolutePath)) {
          // Log The Useful Info
          drush_log(dt('DEPLOY4/LOCAL/DRUSH/CHECK 404 Not Found or is not executable at @drush_path', array('@drush_path' => $drushAbsolutePath)), 'info');
          // Lock Site-Local Drush if Unlocked
          provision_check_codebase_status();
          if (!d()->isConsolePatched || !d()->isCorePatched) {
            drush_log(dt('DEPLOY5/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
            provision_lock_drush_local_vnd();
          }
          // Log The Useful Info
          drush_log(dt('AEGIR/DRUSH Invoking updatedb with Drush 8 in drush_provision_drupal_post_provision_deploy'), 'info');
          // Tell How-To Skip Drush updatedb
          drush_log(dt('AEGIR/DRUSH/CTRL To skip updatedb please create an empty control file @path', array('@path' => $dis_auto_up_db_global)), 'info');
          // Call The Drush updatedb Command With Aegir Own Drush 8
          provision_backend_invoke(d()->name, 'updatedb -y');
          // Break the Update-module fetch-task deadlock introduced by the DB
          // import (clone/migrate/restore). On this degraded D10+ path the
          // site-local Drush is not executable, so the helper logs and skips
          // (no Aegir Drush 8 fallback on D10+); the manual SQL workaround
          // still applies. See
          // https://www.drupal.org/project/drupal/issues/2920285
          _provision_drupal_clear_update_fetch_tasks();
        }
        else {
          // Log The Useful Info
          drush_log(dt('DEPLOY6/LOCAL/DRUSH/CMD vendor/drush/drush/drush.php --version in @appRoot', array('@appRoot' => $appRoot)), 'info');
          // Check And Display Site-Local Drush Version
          $result = drush_shell_cd_and_exec($appRoot, 'vendor/drush/drush/drush.php --version');
          $output = implode('', drush_shell_exec_output());
          drush_log("DEPLOY7/LOCAL/DRUSH/VERSION " . $output);
          // Tell How-To Skip Drush updatedb
          drush_log(dt('DEPLOY8/LOCAL/DRUSH/CTRL To skip updatedb please create an empty control file @path', array('@path' => $dis_auto_up_db_global)), 'info');
          // Figure Out The Drush 10+ Compatible Site Alias
          $dnameten = preg_replace('/\.(?![^.]*$)/', '-', d()->name);
          // Call The Drush updatedb Command with Site-Local Drush
          drush_log(dt('DEPLOY9/LOCAL/DRUSH/CMD vendor/drush/drush/drush.php @dnameten updatedb in @appRoot', array('@dnameten' => $dnameten, '@appRoot' => $appRoot)), 'info');
          // SECURITY: $dnameten derives from d()->name (Drush alias name),
          // which is not validated against shell-unsafe characters at the
          // Hostmaster UI layer. Escape before shell interpolation.
          // (string) cast guards against PHP 8.1+ null-to-escapeshellarg
          // deprecation if preg_replace() ever returns null upstream.
          $result = drush_shell_cd_and_exec($appRoot, 'vendor/drush/drush/drush.php ' . escapeshellarg((string) $dnameten) . ' updatedb -y');
          $output = drush_shell_exec_output();
          foreach ($output as $index => $line) {
            if (!$success) { // Log the entire error as a notice.
              $line_status = 'notice';
            }
            else { // Only log the last line as success.
              $line_status = $index+1 == count($output) ? 'success' : 'notice';
            }
            drush_log($line, $line_status);
          }
          // Break the Update-module fetch-task deadlock introduced by the DB
          // import (clone/migrate/restore), while the site-local Drush is still
          // unlocked. provision_lock_drush_local_vnd() below chmods vendor/drush
          // to 0400, after which the helper's is_executable() gate would fail.
          // See _provision_drupal_clear_update_fetch_tasks() and
          // https://www.drupal.org/project/drupal/issues/2920285
          _provision_drupal_clear_update_fetch_tasks();
          // Rebuild the D8+ service container and flush all caches via the
          // site-local Drush, the analog of the D<8 registry-rebuild above.
          // Runs here, in the same unlocked window as updatedb and before the
          // re-lock below, so it works on D10+ where Aegir Drush 8 cannot
          // bootstrap the site (the FULL-bootstrap rebuild in the deploy tail
          // no-ops there). Non-fatal.
          _provision_drupal_rebuild_d8plus();
          // Lock Site-Local Drush if Unlocked
          provision_check_codebase_status();
          if (!d()->isConsolePatched || !d()->isCorePatched) {
            drush_log(dt('DEPLOY10/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
            provision_lock_drush_local_vnd();
          }
          // Lock Site-Local Drush In The Old Platform Again Too
          if (!drush_get_option('deploy_replace_site', FALSE)) {
            if (drush_get_option('old_platform', FALSE)) {
              provision_backend_invoke($old_platform, 'provision-verify');
              sleep(5);
            }
          }
        }
      }
      else {
        if (drush_drupal_major_version() >= 8) {
          // Lock Site-Local Drush if Unlocked
          provision_check_codebase_status();
          if (!d()->isConsolePatched || !d()->isCorePatched) {
            drush_log(dt('DEPLOY11/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
            provision_lock_drush_local_vnd();
          }
        }
        // Tell How-To Skip Drush updatedb
        drush_log(dt('AEGIR/DRUSH/CTRL To skip updatedb please create an empty control file @path', array('@path' => $dis_auto_up_db_global)), 'info');
        // Call The Drush updatedb Command With Aegir Own Drush 8
        drush_log(dt('AEGIR/DRUSH Invoking updatedb with Drush 8 in drush_provision_drupal_post_provision_deploy'), 'info');
        provision_backend_invoke(d()->name, 'updatedb -y');
        // Break the Update-module fetch-task deadlock introduced by the DB
        // import (clone/migrate/restore). D8/D9 runs the clear with Aegir's own
        // Drush 8, same as the updatedb above. See
        // https://www.drupal.org/project/drupal/issues/2920285
        _provision_drupal_clear_update_fetch_tasks();
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
  // D6-D9 finish the rebuild in-process under a FULL bootstrap that Aegir's own
  // Drush 8 can still drive. D10+ are already rebuilt above via the site-local
  // 'cr' in _provision_drupal_rebuild_d8plus() (Aegir Drush 8 cannot bootstrap
  // them, so this block only ever logged "could not bootstrap" for D10/D11);
  // they take the CONFIGURATION-only branch below, like D12+ already did.
  if (drush_drupal_major_version() < 10) {
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
    drush_log("Skipping FULL bootstrap for drupal 10+ in drush_provision_drupal_post_provision_deploy");
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

/**
 * Clear the Update module's stale fetch-task tracker after a DB import.
 *
 * Drupal 8+ tracks "a release-history fetch is already queued for project X"
 * in the non-expirable {key_value} collection 'update_fetch_task'. The row is
 * only ever cleared by UpdateProcessor::processFetchTask(), which runs solely
 * when the matching 'update_fetch_tasks' queue item is processed. A clone or
 * migrate restores the durable {key_value} rows but does not preserve the
 * transient queue in lockstep, leaving tracker rows with no queue item.
 * Because UpdateProcessor::createFetchTask() refuses to enqueue when a row
 * exists, the site is then permanently stuck on "No available releases found",
 * with no recovery via cron or the manual check (refreshUpdateData() deletes
 * the available-releases data but never touches this collection).
 *
 * Upstream (still open): https://www.drupal.org/project/drupal/issues/2920285
 *
 * Drupal 7 kept the same tracker in the {cache_update} cache bin, which our
 * deploy already wipes, so this only affects D8+ sites.
 *
 * This is the Drupal-native equivalent of the documented workaround
 * (DELETE FROM key_value WHERE collection = 'update_fetch_task') and is safe
 * to run on every deploy: the next update-status check simply re-fetches.
 *
 * Callers must invoke this from each updatedb branch, right after updatedb and
 * BEFORE the site-local Drush is re-locked: provision_lock_drush_local_vnd()
 * chmods vendor/drush to 0400, after which the is_executable() gate below would
 * fail and the D10+ clear would silently skip. Failure is non-fatal by design
 * (logging only, never drush_set_error) — a cosmetic update-status reset must
 * never fail a deploy.
 */
function _provision_drupal_clear_update_fetch_tasks() {
  // D7 and hostmaster have no {key_value} table; nothing to clear. A
  // non-hostmaster D7 site reaches the callers below, so this guard is required.
  if (provision_is_hostmaster_site() || drush_drupal_major_version() < 8) {
    return;
  }

  // Drupal-native equivalent of the SQL workaround. Double-quoted so "\\Drupal"
  // collapses to the literal "\Drupal"; the single quotes around the collection
  // name are safe inside a double-quoted PHP string.
  $php = "\\Drupal::keyValue('update_fetch_task')->deleteAll();";

  if (drush_drupal_major_version() >= 10) {
    // D10+: site-local Drush only. Never use Aegir Drush 8 / @alias on D10+.
    // Aliases were already converted in the updatedb branch (site:alias-convert)
    // before this helper is called, so the site-local alias resolves.
    $appRoot = findAppRoot(d()->root);
    $drushAbsolutePath = $appRoot . '/vendor/drush/drush/drush.php';
    if (is_executable($drushAbsolutePath)) {
      $dnameten = preg_replace('/\.(?![^.]*$)/', '-', d()->name);
      // SECURITY: $dnameten derives from d()->name and is not validated against
      // shell-unsafe characters at the Hostmaster UI layer; escape it. $php is
      // a fixed literal but is escaped for symmetry. No extra args are passed to
      // drush_shell_cd_and_exec(), so the command is NOT run through sprintf()
      // (no '%' hazard). (string) cast guards the PHP 8.1+ null-to-escapeshellarg
      // deprecation if preg_replace() ever returns null.
      drush_log(dt('UPDFETCH/LOCAL/DRUSH/CMD vendor/drush/drush/drush.php @dnameten ev (clear update_fetch_task) in @appRoot', array('@dnameten' => $dnameten, '@appRoot' => $appRoot)), 'info');
      drush_shell_cd_and_exec($appRoot, 'vendor/drush/drush/drush.php ' . escapeshellarg((string) $dnameten) . ' ev ' . escapeshellarg($php));
      foreach (drush_shell_exec_output() as $line) {
        drush_log($line, 'notice');
      }
    }
    else {
      // Degraded codebase (no site-local Drush). Do not fall back to @alias /
      // Aegir Drush 8 on D10+, per platform policy; log and skip. The documented
      // manual SQL workaround still applies.
      drush_log(dt('UPDFETCH/LOCAL/DRUSH/SKIP site-local Drush not executable at @path; update_fetch_task left untouched', array('@path' => $drushAbsolutePath)), 'warning');
    }
  }
  else {
    // D8/D9: Aegir Drush 8 is acceptable. provision_backend_invoke passes the
    // eval string as a command argument via the Drush backend, so no manual
    // shell escaping is required here. That invoke runs with integrate => TRUE,
    // which replays a child error into the parent's drush_get_error(); the
    // deploy's restore-dir/old-DB cleanup is gated on !drush_get_error(), so a
    // failed cosmetic clear could otherwise fail the deploy and leak the old
    // database. Snapshot and restore the error code so this can never happen.
    drush_log(dt('UPDFETCH/AEGIR/DRUSH Invoking ev (clear update_fetch_task) with Drush 8'), 'info');
    $pre_error = drush_get_context('DRUSH_ERROR_CODE', DRUSH_SUCCESS);
    provision_backend_invoke(d()->name, 'ev', array($php));
    drush_set_context('DRUSH_ERROR_CODE', $pre_error);
  }
}
