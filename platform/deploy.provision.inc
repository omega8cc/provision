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
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
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
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  // Purge any cache namespace left in the shared Valkey/Redis store by a
  // previously deleted site with the same URI, BEFORE the deployed site runs
  // any code: clone, rename and restore land a site here without passing
  // through provision-install, so the target would inherit a stale namespace
  // (deleted site's compiled container and routing included) exactly the way
  // a same-name reinstall did. For a same-URI deploy over an existing site
  // (migrate/restore in place) this only flushes that site's own cache
  // mid-maintenance -- the deploy tail rebuilds caches anyway. Non-fatal by
  // design: an unreachable cache store must not fail a deploy.
  _provision_valkey_purge_site(d()->uri);
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
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
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
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  _provision_drupal_maintain_aliases();
  _provision_drupal_create_directories();
}

/**
 * Implements drush_hook_post_COMMAND().
 */
function drush_provision_drupal_post_provision_deploy() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  // D6 -> D7 upgrade deploy (transient d6_d7_upgrade option): the imported
  // copy database is still Drupal 6 until the updatedb below converts it.
  // Move the copied site-local D6 module directories out of the scan path
  // first: left in place, updatedb's full bootstrap loads them against the
  // D7 platform namesakes and fatals on duplicate function definitions
  // before update_fix_compatibility() can disable anything (BOA platforms
  // ship name-shadowing o_contrib copies, so the compatibility check alone
  // cannot be trusted). Directories only; loose files like
  // default.boa_site_control.ini stay in place.
  if (drush_get_option('d6_d7_upgrade', FALSE)) {
    _provision_drupal_deploy_quarantine_d6_modules(d()->site_path);
  }
  // Run registry-rebuild without cache-clear, if available.
  if (function_exists('drush_registry_rebuild')) {
    if (drush_get_option('d6_d7_upgrade', FALSE)) {
      // A still-Drupal-6 database cannot pass the Drupal 7 bootstrap this
      // rebuild needs ({blocked_ips} exists only after updatedb's
      // update_prepare_d7_bootstrap()), and the failed subprocess error
      // would ride the integrated invoke into a full deploy rollback that
      // drops the copy database. The post-updatedb rebuild below suffices.
      // 'ok' so the skip is visible in a normal task log (the info level
      // only surfaces under --verbose).
      drush_log(dt('Pre-updatedb registry rebuild skipped for the D6 upgrade deploy (updatedb converts the copy database first).'), 'ok');
    }
    elseif (drush_drupal_major_version() < 8 && !provision_is_hostmaster_site()) {
      provision_backend_invoke(d()->name, 'registry-rebuild --no-cache-clear');
      drush_log(dt('Registry rebuilt with --no-cache-clear in drush_provision_drupal_post_provision_deploy'));
    }
  }

  $aegir_root = d('@server_master')->aegir_root;
  if (provision_is_hostmaster_site()) {
    // Always Call The Drush updatedb Command With Aegir Own Drush 8 In The Hostmaster Site
    drush_log(dt('AEGIR/DRUSH Invoking updatedb in drush_provision_drupal_post_provision_deploy'), 'info');
    provision_backend_invoke(d()->name, 'updatedb -y');
  }
  else {
    // No control file skips the database updates any more; a leftover
    // DisAutoUpDb.info is named in the log and otherwise ignored.
    provision_report_retired_updatedb_controls(d()->uri);
    if (drush_drupal_major_version() >= 10) {

      // Update Drush 10+ Aliases -- on the backend's own interpreter and ini
      // (see _provision_php_script_cmd()): the bare `drush10` launcher runs on
      // whatever ini the shell exports, and the account's Drush 8 ini is
      // refused by every modern Drush, after which no yml alias exists for a
      // new site. Never silent: a failed conversion is a warning.
      $drush10 = _provision_drush10_script();
      if ($drush10 !== '') {
        $alias_cmd = _provision_php_script_cmd($drush10, array('site:alias-convert', $aegir_root . '/.drush/sites', '--yes'));
      }
      else {
        $alias_cmd = 'drush10 site:alias-convert ~/.drush/sites --yes';
      }
      drush_log(dt('DRUSH/TEN/ALIASES/UPDATE site:alias-convert ~/.drush/sites --yes in @aegir_root', array('@aegir_root' => $aegir_root)), 'info');
      $result = drush_shell_cd_and_exec($aegir_root, $alias_cmd);
      $output = implode('', drush_shell_exec_output());
      if (!$result || preg_match('/\[(preflight|error)\]/i', $output)) {
        drush_log("DRUSH/TEN/ALIASES/FAIL " . $output, 'warning');
      }
      else {
        drush_log("DRUSH/TEN/ALIASES/LOG " . $output);
      }

      // The updatedb window itself -- a real unlock when the platform is
      // locked, the site-local Drush updatedb by --root/--uri, the fetch-task
      // clear, a real re-lock, then core's own container rebuild -- is the
      // shared _provision_drupal_site_local_updatedb(), one path for the
      // deploy (DEPLOY2 .. DEPLOY10) and the provision-update task. Its NULL
      // (DEPLOY4b: no executable site-local Drush) and FALSE (DEPLOY9/FAIL:
      // updatedb did not complete) are already a WARNING in the log, and the
      // deploy goes on to the in-process tail below: a flush there over a
      // schema left behind fails the task with the cause named right above
      // the error.
      $window = _provision_drupal_site_local_updatedb('DEPLOY');
      // Verify The Old Platform Again Too (a migrate or clone across
      // platforms), after a window that ran, as before; a skipped window
      // (DEPLOY4b) has no old-platform verify, as before. The provision-unlock
      // the old platform used to be sent first is gone: every layer's hook for
      // that command is a documented no-op ("Nothing needs to happen in the
      // backend"), so it only cost a backend process and a sleep(5) per D10+
      // deploy.
      if ($window !== NULL && !drush_get_option('deploy_replace_site', FALSE)) {
        $old_platform = drush_get_option('old_platform', FALSE);
        if ($old_platform) {
          provision_backend_invoke($old_platform, 'provision-verify');
          sleep(5);
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

  // Run registry-rebuild with cache-clear, if available.
  if (function_exists('drush_registry_rebuild')) {
    if (drush_drupal_major_version() < 8 && !provision_is_hostmaster_site()) {
      provision_backend_invoke(d()->name, 'registry-rebuild');
      drush_log(dt('Registry rebuilt without --no-cache-clear in drush_provision_drupal_post_provision_deploy'));
    }
  }
  // D6-D11 finish the rebuild in-process under a FULL bootstrap. Aegir's own
  // Drush 8 CAN read-only FULL-bootstrap D10/D11 here, exactly as it does at
  // install/verify VALIDATE time (the install/verify gates were restored to
  // < 12 in f9466d37 because that in-process container is what the unconditional
  // post-hook system_map() needs); this gate now mirrors the FULL-bootstrap gates
  // in install.provision.inc and verify.provision.inc (same '< 12' test; line
  // numbers move, the functions do not). The in-process pass regenerates the site 'packages' map so the
  // Hostmaster module/theme inventory stays current after a clone/migrate/
  // restore, runs the deploy_11 URL-rename engine (system.site name on a
  // clone-to-new-URL), and rebuilds node access. D10/D11 are ALSO rebuilt above
  // by core itself in _provision_drupal_rebuild_d8plus() (harmless, and it
  // pre-warms the container this bootstrap then reads). D12+ alone skip the
  // FULL bootstrap (Aegir Drush 8 cannot load them) and take the
  // CONFIGURATION-only branch below, relying on that core rebuild. If the
  // bootstrap genuinely fails on D10/D11 it is a WARNING naming what this
  // tail then skipped (the 'packages' map is left untouched, no regression
  // over the prior skip) -- never a plain log line, which let a deploy whose
  // tail did not run end SUCCESSFUL.
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
      // A tail that did not run is never a notice: the task would otherwise
      // end SUCCESSFUL while the packages map, the URL-rename deploy engine
      // (the system.site name on a clone to a new URL) and the node access
      // rebuild were all skipped -- and, on a replace-site deploy, the source
      // database is dropped right below.
      drush_log(dt('DEPLOY/BOOTSTRAP/FAIL could not bootstrap Drupal after updatedb: the packages map, the URL-rename deploy engine and the node access rebuild were skipped; run a Verify on the site once it bootstraps'), 'warning');
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

/**
 * Quarantine copied site-local D6 modules before the D6 -> D7 updatedb.
 *
 * Runs only on a deploy carrying the transient d6_d7_upgrade option. Every
 * DIRECTORY under sites/<uri>/modules that ships a .info file (at any depth)
 * is moved, parents first, into the sibling holding directory
 * sites/<uri>/modules-d6d7-quarantine -- kept, never deleted, and off every
 * Drupal module scan path (a holding dir under modules/ itself would be
 * re-discovered by drupal_system_listing and defeat the quarantine). Loose
 * files directly in modules/ (default.boa_site_control.ini) stay in place.
 * Failures log a warning only: a drush_set_error here would ride the
 * integrated invoke into a rollback that drops the copy database, while a
 * missed directory is backstopped fail-closed by the updatedb fatal.
 */
function _provision_drupal_deploy_quarantine_d6_modules($site_path) {
  $modules_dir = $site_path . '/modules';
  if (!is_dir($modules_dir)) {
    return;
  }

  // Collect every directory that holds a .info file.
  $to_move = array();
  $iterator = new RecursiveIteratorIterator(
    new RecursiveDirectoryIterator($modules_dir, FilesystemIterator::SKIP_DOTS),
    RecursiveIteratorIterator::SELF_FIRST
  );
  foreach ($iterator as $item) {
    if (!$item->isFile() || substr($item->getFilename(), -5) !== '.info') {
      continue;
    }
    $dir = dirname($item->getPathname());
    if ($dir === $modules_dir) {
      // A loose .info directly in modules/ has no directory of its own to
      // move; never move modules/ itself.
      continue;
    }
    $to_move[$dir] = TRUE;
  }
  if (empty($to_move)) {
    return;
  }

  $holding = $site_path . '/modules-d6d7-quarantine';
  if (!is_dir($holding) && !mkdir($holding, 0755)) {
    drush_log(dt('Could not create @dir; the D6 module quarantine was skipped and updatedb may fatal on duplicate definitions.', array('@dir' => $holding)), 'warning');
    return;
  }

  // Parents first: moving a project root (date/) takes its sub-modules along.
  $dirs = array_keys($to_move);
  usort($dirs, 'strcmp');
  foreach ($dirs as $dir) {
    if (!is_dir($dir)) {
      // Already moved with its parent project directory.
      continue;
    }
    // Only top-level children of modules/ are moved directly; nested module
    // dirs whose top-level parent is being moved ride along with it.
    $relative = substr($dir, strlen($modules_dir) + 1);
    $top = strpos($relative, '/') === FALSE ? $dir : $modules_dir . '/' . substr($relative, 0, strpos($relative, '/'));
    if (!is_dir($top)) {
      continue;
    }
    $dest = $holding . '/' . basename($top);
    $suffix = 1;
    while (file_exists($dest)) {
      $dest = $holding . '/' . basename($top) . '-' . $suffix++;
    }
    if (rename($top, $dest)) {
      drush_log(dt('DEPLOY/D6-QUARANTINE moved @dir to @dest before updatedb.', array('@dir' => $top, '@dest' => $dest)), 'ok');
    }
    else {
      drush_log(dt('DEPLOY/D6-QUARANTINE could not move @dir; updatedb may fatal on duplicate definitions.', array('@dir' => $top)), 'warning');
    }
  }
}

