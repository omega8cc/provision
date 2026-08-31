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
          $old_platform = drush_get_option('old_platform', FALSE);
          if ($old_platform) {
            provision_backend_invoke($old_platform, 'provision-unlock');
            sleep(5);
          }
        }

        // Update Drush 10+ Aliases
        drush_log(dt('DRUSH/TEN/ALIASES/UPDATE drush10 site:alias-convert ~/.drush/sites --yes in @aegir_root', array('@aegir_root' => $aegir_root)), 'info');
        $result = drush_shell_cd_and_exec($aegir_root, 'drush10 site:alias-convert ~/.drush/sites --yes');
        $output = implode('', drush_shell_exec_output());
        drush_log("DRUSH/TEN/ALIASES/LOG " . $output);

        // Find The App Root Directory
        $appRoot = findAppRoot(d()->root);

        // Define Local Drush Path
        $drushPath = 'vendor/drush/drush/drush.php';

        // Check If The Local Drush Exists And Is Executable
        $drushAbsolutePath = $appRoot . '/' . $drushPath;
        drush_log(dt('DEPLOY3/LOCAL/DRUSH/CHECK Path @path', array('@path' => $drushAbsolutePath)), 'info');

        // Open a chmod-only executable window WITHOUT reverting the Aegir
        // core/console/validator/psr-log patches. updatedb runs a FULL bootstrap
        // and recompiles the D8+ service container; compiling it in the patch-
        // REVERTED (stock) state -- what the old provision_unlock_drush_local_vnd()
        // dance did here -- bakes the site-local modern Drush's tagged
        // 'logger.drupaltodrush' (Drush\Log\DrushLog) into the persisted
        // container, which the patched web then fatals resolving (circular
        // reference on D11, "Class Drush\Log\DrushLog not found" on D10 where
        // vendor/drush is relocked 0400). The site serves in the PATCHED state, so
        // updatedb must compile in that same state: flipping only the exec bits
        // keeps the patches applied and never poisons the container -- the analog
        // of the site-local 'cr' fix in _provision_drupal_rebuild_d8plus(). The
        // at-rest 0400 posture is restored by the chmod-only relock below, only if
        // we opened the window here.
        $was_executable = is_executable($drushAbsolutePath);
        if (!$was_executable) {
          drush_log(dt('DEPLOY2/LOCAL/DRUSH/EXEC chmod-only exec window (patches kept applied) for updatedb'), 'info');
          _provision_drupal_chmod_drush_vnd_for_cr($appRoot, TRUE);
        }

        if (!is_executable($drushAbsolutePath)) {
          // Log The Useful Info
          drush_log(dt('DEPLOY4/LOCAL/DRUSH/CHECK 404 Not Found or is not executable at @drush_path', array('@drush_path' => $drushAbsolutePath)), 'info');
          // D10+ with no executable site-local Drush. Do NOT fall back to Aegir
          // Drush 8: it cannot bootstrap a D10/D11 site, so 'updatedb' would only
          // fail noisily ("could not bootstrap") without applying anything. Skip
          // with a warning; the schema update must be applied manually (Unlock
          // Local Drush, then 'vdrush @alias updb') or by repairing the site-local
          // Drush. (D6/D7/D8/D9 keep using Aegir Drush 8 in the branch below — it
          // is the version-matched Drush for them and bootstraps them fine.)
          drush_log(dt('DEPLOY4b/LOCAL/DRUSH/SKIP updatedb skipped: site-local Drush not executable at @drush_path and Aegir Drush 8 cannot bootstrap D10+; apply the DB update manually', array('@drush_path' => $drushAbsolutePath)), 'warning');
          // Break the Update-module fetch-task deadlock introduced by the DB
          // import (clone/migrate/restore). On this degraded D10+ path the
          // site-local Drush is not executable, so the helper logs and skips
          // (no Aegir Drush 8 fallback on D10+); the manual SQL workaround
          // still applies. See
          // https://www.drupal.org/project/drupal/issues/2920285
          _provision_drupal_clear_update_fetch_tasks();
          // Relock the chmod-only window if we opened it (patches untouched). In
          // this branch drush.php was not made executable (genuinely missing), so
          // this only restores the 0400 at-rest posture.
          if (!$was_executable) {
            drush_log(dt('DEPLOY5/LOCAL/DRUSH/RELOCK chmod-only 0400 (patches untouched)'), 'info');
            _provision_drupal_chmod_drush_vnd_for_cr($appRoot, FALSE);
          }
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
          // Cloaked credentials reach the site-local Drush only through the
          // real environment; harmless when credentials are not cloaked.
          provision_prepare_environment_subprocess();
          $result = drush_shell_cd_and_exec($appRoot, 'vendor/drush/drush/drush.php ' . escapeshellarg((string) $dnameten) . ' updatedb -y');
          provision_clear_environment_subprocess();
          // drush_shell_cd_and_exec() returns TRUE on exit 0; drive the per-line
          // log status from it ($success was previously used here but never set,
          // so every line was logged as 'notice', never 'success').
          $success = $result;
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
          // executable. The chmod-only relock below returns vendor/drush to 0400,
          // after which the helper's is_executable() gate would fail.
          // See _provision_drupal_clear_update_fetch_tasks() and
          // https://www.drupal.org/project/drupal/issues/2920285
          _provision_drupal_clear_update_fetch_tasks();
          // Rebuild the D8+ service container and flush all caches via the
          // site-local Drush, the analog of the D<8 registry-rebuild above.
          // Runs here, in the same exec window as updatedb and before the
          // chmod-only relock below, so it works on D10+ where Aegir Drush 8
          // cannot bootstrap the site (the FULL-bootstrap rebuild in the deploy
          // tail no-ops there). With the window already open it reuses it (its own
          // is_executable() gate no-ops), compiling in the patched state. Non-fatal.
          _provision_drupal_rebuild_d8plus();
          // Relock the chmod-only window if we opened it, leaving the patches
          // untouched (never a patch re-apply here). Restores the at-rest 0400
          // posture, minus the patch/psr-log/ownership work already correct at
          // rest. Runs before the old-platform re-verify, matching the prior order.
          if (!$was_executable) {
            drush_log(dt('DEPLOY10/LOCAL/DRUSH/RELOCK chmod-only 0400 (patches untouched)'), 'info');
            _provision_drupal_chmod_drush_vnd_for_cr($appRoot, FALSE);
          }
          // Lock Site-Local Drush In The Old Platform Again Too
          if (!drush_get_option('deploy_replace_site', FALSE)) {
            $old_platform = drush_get_option('old_platform', FALSE);
            if ($old_platform) {
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
  // D6-D11 finish the rebuild in-process under a FULL bootstrap. Aegir's own
  // Drush 8 CAN read-only FULL-bootstrap D10/D11 here, exactly as it does at
  // install/verify VALIDATE time (the install/verify gates were restored to
  // < 12 in f9466d37 because that in-process container is what the unconditional
  // post-hook system_map() needs); this gate now mirrors them (install:323,
  // verify:22). The in-process pass regenerates the site 'packages' map so the
  // Hostmaster module/theme inventory stays current after a clone/migrate/
  // restore, runs the deploy_11 URL-rename engine (system.site name on a
  // clone-to-new-URL), and rebuilds node access. D10/D11 are ALSO rebuilt above
  // via the site-local 'cr' in _provision_drupal_rebuild_d8plus() (harmless, and
  // it pre-warms the container this bootstrap then reads). D12+ alone skip the
  // FULL bootstrap (Aegir Drush 8 cannot load them) and take the
  // CONFIGURATION-only branch below, relying on that site-local 'cr'. If the
  // bootstrap genuinely fails on D10/D11 it logs "could not bootstrap" and the
  // 'packages' map is left untouched (no regression over the prior skip).
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
      // SECURITY: $appRoot/$uri derive from the alias context; escape both.
      // $php is a fixed literal but is escaped for symmetry. No extra args are
      // passed to drush_shell_cd_and_exec(), so the command is NOT run through
      // sprintf() (no '%' hazard). Absolute --root/--uri instead of the @alias
      // form: alias resolution redispatches through a shell where the
      // parenthesised eval payload broke parsing (observed as a /bin/dash
      // syntax error in the restore deploy), so the clear silently never ran;
      // the option form executes in-process, same convention as the pm:list /
      // pm:uninstall calls.
      drush_log(dt('UPDFETCH/LOCAL/DRUSH/CMD vendor/drush/drush/drush.php ev (clear update_fetch_task) --root=@root --uri=@uri', array('@root' => d()->root, '@uri' => d()->uri)), 'info');
      // Cloaked credentials reach the site-local Drush only through the real
      // environment; harmless when credentials are not cloaked.
      provision_prepare_environment_subprocess();
      drush_shell_cd_and_exec($appRoot, 'vendor/drush/drush/drush.php ev ' . escapeshellarg($php) . ' --root=' . escapeshellarg((string) d()->root) . ' --uri=' . escapeshellarg((string) d()->uri));
      provision_clear_environment_subprocess();
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
