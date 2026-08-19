<?php

/**
 * @file
 * Deploy command implementation (Backdrop layer).
 *
 * Fork of platform/deploy.provision.inc collapsed to the single Backdrop path:
 * the whole D8/D10/D11 site-local Drush lock/unlock/patch-revert machinery is
 * dropped (Backdrop ships no site-local vendor/drush), and updatedb runs via the
 * backdrop-drush-extension's backdrop-updatedb verb. The CMS-agnostic
 * extract/swap/backup scaffolding is kept byte-similar.
 *
 * Needed for verify/clone parity in Phase 1; heavy Phase-2 (D7->Backdrop
 * migrate) use.
 */

/**
 * Implementation of drush_hook_COMMAND_validate().
 */
function drush_provision_backdrop_provision_deploy_validate($backup_file = NULL) {
  if (!provision_platform_is_backdrop(d())) {
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
 * Make a backup before making any changes, and extract the file we are restoring from.
 *
 * Implements drush_hook_pre_COMMAND().
 */
function drush_provision_backdrop_pre_provision_deploy($backup_file) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
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
    // Backdrop's file-based JSON config lives here and is web-group-writable at
    // runtime; restore it group-writable so the web process can keep writing it.
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

    // Write sites.php BEFORE the first site bootstrap: Backdrop caches the
    // first conf_path() for the whole process, and on a fresh target platform
    // an empty sites.php resolves it to '.' -- the post-hook FULL escalation
    // then loads the platform-root stub settings.php and dies (RCA
    // 2026-07-19). The Backdrop store's primary-URI self-map guarantees a
    // truthy $sites even for alias-less sites.
    _provision_backdrop_maintain_aliases();

    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
    _provision_backdrop_create_settings_file();
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

    if (drush_get_option('backdrop_d7_upgrade', FALSE)) {
      // D7 -> Backdrop upgrade deploy: the imported dump still carries Drupal 7
      // schema versions (system 7xxx and friends) while the Backdrop platform
      // reports renumbered 1xxx schemas, so the namesake comparison below would
      // abort every upgrade with PROVISION_SCHEMA_UPGRADE_FAILURE. The only
      // gate that matters here (REQUIRED_D7_SCHEMA_VERSION) is enforced by
      // provision-backdrop-upgrade's validate and re-checked by the standalone
      // updater before converting.
      drush_log(dt('BACKDROP/UPGRADE Skipping the module schema version check on a D7 -> Backdrop upgrade deploy.'), 'info');
    }
    elseif (isset($site_packages['modules']) && is_array($site_packages['modules'])) {
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
function drush_provision_backdrop_pre_provision_deploy_rollback() {
  if (!provision_platform_is_backdrop(d())) {
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

       _provision_backdrop_create_settings_file();
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
function drush_provision_backdrop_provision_deploy() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  // Purge any cache namespace left in the shared Valkey/Redis store by a
  // previously deleted site whose DB coordinates the deployed site now
  // reuses, BEFORE the post hook's updatedb runs the first site code. Clone,
  // rename and restore land Backdrop sites here without passing through
  // provision-install, and Aegir mints db names deterministically from the
  // URI, so a target named after a deleted site inherits its namespace. The
  // pre hooks have already created the target db and written its settings,
  // so the credentials in context are the ones the site will cache under.
  // For a same-URI deploy over an existing site this only flushes that
  // site's own cache mid-maintenance. Non-fatal by design.
  _provision_valkey_purge_backdrop_site(d()->uri);
  _provision_backdrop_maintain_aliases();
  _provision_backdrop_create_directories();
}

/**
 * Implements drush_hook_post_COMMAND().
 */
function drush_provision_backdrop_post_provision_deploy() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }

  $aegir_root = d('@server_master')->aegir_root;
  $dis_auto_up_db_global = $aegir_root . '/static/control/DisAutoUpDb.info';
  $dis_auto_up_db_site = $aegir_root . '/static/control/' . d()->uri . '_DisAutoUpDb.info';
  if (provision_is_hostmaster_site()) {
    // Hostmaster is never a Backdrop site; kept for structural parity.
    drush_log(dt('AEGIR/DRUSH Invoking updatedb in drush_provision_backdrop_post_provision_deploy'), 'info');
    provision_backend_invoke(d()->name, 'updatedb -y');
  }
  else {
    if (drush_get_option('backdrop_d7_upgrade', FALSE)) {
      // D7 -> Backdrop upgrade deploy: the copy database still holds Drupal 7
      // schema at this point. backdrop-updatedb declares a dispatch-time FULL
      // bootstrap, which cannot succeed on an unconverted D7 database — the
      // conversion is performed instead by the standalone updater that
      // provision-backdrop-upgrade shells out to after this deploy.
      drush_log(dt('BACKDROP/UPGRADE Skipping backdrop-updatedb on a D7 -> Backdrop upgrade deploy; the standalone updater performs the conversion.'), 'info');
    }
    elseif (is_file($dis_auto_up_db_global)) {
      drush_log(dt('DRUSH/CTRL/UPDATEDB skipped due to the presence of a control file @path', array('@path' => $dis_auto_up_db_global)), 'info');
    }
    elseif (is_file($dis_auto_up_db_site)) {
      drush_log(dt('DRUSH/CTRL/UPDATEDB skipped due to the presence of a control file @path', array('@path' => $dis_auto_up_db_site)), 'info');
    }
    else {
      // Tell How-To Skip the DB update.
      drush_log(dt('AEGIR/DRUSH/CTRL To skip updatedb please create an empty control file @path', array('@path' => $dis_auto_up_db_global)), 'info');
      // Apply pending schema updates with the backdrop-drush-extension verb,
      // driven by Aegir's own Drush 8 (BackdropBoot bootstraps the site).
      drush_log(dt('BACKDROP/DRUSH Invoking backdrop-updatedb in drush_provision_backdrop_post_provision_deploy'), 'info');
      provision_backend_invoke(d()->name, 'backdrop-updatedb');
    }
  }

  // Belt-and-braces for the '.'-poison class (RCA 2026-07-19): sites.php is
  // certainly truthy by now (the COMMAND hook rewrote it), so recompute the
  // process-cached conf_path before the FULL escalation reads settings
  // through it. Backdrop core's global conf_path() honours $reset; the stock
  // BackdropBoot wrapper drops its arguments (patched at stage time).
  if (function_exists('conf_path')) {
    conf_path(TRUE, TRUE);
  }

  // Finish the rebuild in-process under a FULL bootstrap: regenerate the site
  // 'packages' map, run the URL-rename deploy engine (system.core:site_name on a
  // clone-to-new-URL) and rebuild node access.
  if (drush_get_option('backdrop_d7_upgrade', FALSE)) {
    // D7 -> Backdrop upgrade deploy: bootstrapping the still-unconverted copy
    // database here would fail; the standalone updater performs the conversion,
    // cache rebuild and node access rebuild in its own php process, and the
    // final provision-verify regenerates the packages map.
    drush_log(dt('BACKDROP/UPGRADE Skipping the in-process post-deploy rebuild on a D7 -> Backdrop upgrade deploy.'), 'info');
  }
  elseif (drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL)) {
    drush_include_engine('backdrop', 'deploy');
    drush_set_option('packages', _scrub_object(provision_backdrop_system_map()), 'site');
    _provision_backdrop_rebuild_caches();

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
    drush_log("could not bootstrap Backdrop after updatedb");
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
