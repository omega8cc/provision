<?php

/**
 * @file
 * Code related to verify tasks (Backdrop layer).
 *
 * Fork of platform/verify.provision.inc. The server branch is intentionally
 * absent: provision_platform_is_backdrop() is FALSE for a server context (the
 * aegir root is not a Backdrop codebase), so this whole hook early-returns and
 * the CMS-neutral server verify is left to the Drupal layer. The D6..D11
 * o_contrib_* symlink matrix and the D8+ site-local Drush locking are dropped
 * (those bundles carry Drupal-only module sets); instead the platform verify
 * attaches Backdrop's own shared bundle, modules/o_contrib_backdrop, from the
 * same /data/all/<NNN>/ shared space model. The verify additionally reports
 * $data['backdrop'] = the version string, the front-end's Backdrop signal.
 */

/**
 * Provision verify command.
 */
function drush_provision_backdrop_provision_verify_validate() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type === 'site') {
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
    provision_prepare_environment();

    // Reload db creds from the persisted drushrc BEFORE re-rendering settings,
    // or the connection gets blanked (see _provision_backdrop_ensure_db_creds_loaded).
    _provision_backdrop_ensure_db_creds_loaded();
    _provision_backdrop_create_settings_file();
    provision_drupal_push_site();

    drush_errors_on();
    // Deliberately NO in-process FULL bootstrap (mirrors the Drupal layer's
    // D12+ skip branch). BackdropBoot's configuration phase copies Drush's
    // $GLOBALS['databases'] into the 'databases' option, but Aegir renders
    // Backdrop's settings.php with the scalar $database (singular) form, so
    // $databases is empty and a FULL/DATABASE bootstrap here fatals with
    // "Could not find a value for the database name setting" and a socket
    // PDOException -- rolling the whole verify back even though the site is
    // fine. Every Backdrop verify step is bootstrap-free instead: the package
    // map and cron key are read straight from the site DB through the provision
    // DB service, and caches are flushed via a fresh bee subprocess.
  }
}

/**
 * Implements drush_hook_pre_COMMAND().
 */
function drush_provision_backdrop_pre_provision_verify() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type === 'platform') {

    // Add platform root auto-discovery to avoid confusing
    // Composer based codebase root with the real root.
    d()->root = provision_auto_fix_platform_root(d()->root);

    drush_log(dt("Checking for platform at path @path...", array('@path' => d()->root)), 'debug');
    drush_log(dt("Platform makefile: @makefile", array('@makefile' => d()->makefile)), 'debug');

    // Create a platform from a makefile, if it doesnt already exist and the makefile does.
    if (!provision_file()->exists(d()->root)->status() && !empty(d()->makefile)) {
      drush_log(dt("Platform path does not exist, fetching from drush makefile"), 'notice');
      $arguments = array();
      $options = array();
      // Propagate working-copy args downward.
      if (drush_get_option('working-copy') || !empty(d()->make_working_copy)) {
        $options[] = '--working-copy';
      }
      $arguments[] = d()->makefile;
      $arguments[] = d()->root;

      // Change current directory to makefile's directory until Drush fixes this bug: https://github.com/drush-ops/drush/issues/2482
      if (!empty(d()->makefile) && file_exists(dirname(d()->makefile))) {
        chdir(dirname(d()->makefile));
      }

      drush_invoke_process('@none', "make", $arguments, $options);
      if (drush_get_error()) {
        return drush_set_error("DRUSH_MAKE_FAILED",
          "Could not download platform using drush make. No platform present");
      }
    }

    // Composer Install Support
    // Step 0: if drush variable allows composer install on platforms, and check for composer executable fails, show warning.
    if (drush_get_option('provision_composer_install_platforms', FALSE) && !shell_exec('composer')) {
      drush_log(dt('The composer executable was not found. Install composer using instructions located at https://getcomposer.org/doc/00-intro.md#globally or, if you wish to disable composer install, set "provision_composer_install_platforms" to FALSE in the ~/.drush/drushrc.php file.'), 'warning');
    }
    // If drush variable allows composer install on platforms (and composer exec passed)...
    elseif (drush_get_option('provision_composer_install_platforms', FALSE)) {
      // Detect composer-based platform and run composer install if it has not been run yet.
      if (provision_file()->exists(d()->root . DIRECTORY_SEPARATOR . 'composer.json')->status()) {
        $composer_directory = d()->root;
      }
      elseif (d()->repo_path && provision_file()->exists(d()->repo_path . DIRECTORY_SEPARATOR . 'composer.json')->status()) {
        $composer_directory = d()->repo_path;
      }

      if (isset($composer_directory) && (!file_exists($composer_directory . DIRECTORY_SEPARATOR . 'vendor') || drush_get_option('provision_composer_install_platforms_verify_always', FALSE))) {
        $composer_command = drush_get_option('provision_composer_install_command', 'composer install --no-interaction --no-progress --no-dev');
        provision_process($composer_command, $composer_directory);
      }
    }

    // Re-set the ROOT PATH for Drush.
    drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);

    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_ROOT);
    // If we couldn't bootstrap, there's no sense in continuing past this point.
    if (drush_get_error()) {
      $errors = drush_get_error_log();
      foreach ($errors as $type => &$error) {
        $error = "$type: {$error[0]}";
      }
      return drush_set_error("DRUSH_INVALID_DRUPAL_ROOT",
        dt("A Backdrop codebase was not found in @path. Please check that the directory exists and Backdrop's index.php file is present. Check if the Publish Path of the platform exists, contains Backdrop and is readable, then try again. The errors were: @errors", array(
          '@path' => d()->root,
          '@errors' => implode('|', $errors)
        )));
    }

    // Fix ownership early enough.
    $success = drush_shell_exec("sudo --non-interactive /usr/local/bin/fix-drupal-platform-ownership.sh --root=%s --script-user=%s --web-group=%s", d()->root, d()->server->script_user, d()->server->web_group);

    provision_file()->writable(d()->root . '/sites')
      ->succeed('Backdrop sites directory @path is writable by the provisioning script')
      ->fail('Backdrop sites directory @path is not writable by the provisioning script', 'PROVISION_SITES_DIR_NOT_WRITABLE');

    // Aegir expects sites/example.sites.php on every platform; Backdrop ships
    // that multisite-template content as sites/sites.php but not under the
    // example.sites.php name, so produce it if missing. Without it Aegir does
    // not set the platform up correctly and a site's config resolves against
    // the platform root instead of its own sites/<uri>/ directory.
    $backdrop_example_sites = d()->root . '/sites/example.sites.php';
    $backdrop_sites_tpl = d()->root . '/sites/sites.php';
    if (!provision_file()->exists($backdrop_example_sites)->status()
      && provision_file()->exists($backdrop_sites_tpl)->status()) {
      provision_file()->copy($backdrop_sites_tpl, $backdrop_example_sites)
        ->succeed('Created sites/example.sites.php from sites/sites.php')
        ->fail('Could not create sites/example.sites.php');
    }

    // Attach the shared Backdrop contrib bundle, mirroring the Drupal layer's
    // o_contrib_* matrix: the bundle lives under the newest /data/all/<NNN>/
    // shared space (Octopus creates and refreshes it on upgrades; the nightly
    // agent repairs missing symlinks). A Backdrop root always has core/, so
    // no /static/ disjunct is needed here -- every Backdrop platform,
    // including user-uploaded ones under static/, takes this path on verify.
    // Placed before the packages option is set below so the bundle's modules
    // land in the platform's package scan on the same verify.
    $pathAll = '/data/all/';
    if (provision_file()->exists($pathAll)->status()) {
      $sortedDirs = array();
      foreach (scandir($pathAll) as $dir) {
        if ($dir != '.' && $dir != '..' && is_dir($pathAll . $dir)) {
          array_push($sortedDirs, $dir);
        }
      }
      $latestDir = empty($sortedDirs) ? '' : max($sortedDirs);
      drush_log(dt('Contrib latestDir: !latest', array('!latest' => $latestDir)), 'message');
      if (is_numeric($latestDir)) {
        $bd_contrib_dir = '/data/all/' . $latestDir . '/o_contrib_backdrop';
      }
      else {
        $bd_contrib_dir = '/data/all/000/modules/o_contrib_backdrop';
      }
      $bd_contrib_symlink = d()->root . '/modules/o_contrib_backdrop';
      $bd_contrib_dot = d()->root . '/modules/.o_contrib_backdrop_dont_use';
      $bd_baked_redis = d()->root . '/modules/redis';
      $bd_baked_redis_dot = d()->root . '/modules/.redis_dont_use';
      $bd_is_static = preg_match("/\/static\//", d()->root);
      if (provision_file()->exists($bd_contrib_dir)->status()) {
        // Make sure the platform root and core modules directories are writable.
        provision_file()->writable(d()->root)
          ->succeed('Platform root directory @path is writable')
          ->fail('Platform root directory @path is not writable', 'PROVISION_DRUPAL_ROOT_DIR_NOT_WRITABLE');
        provision_file()->writable(d()->root . '/modules')
          ->succeed('Core modules directory @path is writable')
          ->fail('Core modules directory @path is not writable', 'PROVISION_CORE_MODULES_DIR_NOT_WRITABLE');
        // Two copies of the same module in one scan directory make Backdrop's
        // module winner readdir-order dependent (and its status report flag
        // the platform), so a platform never carries both the bundle and a
        // modules/redis copy. On BOA-built platforms modules/redis can only
        // be the retired bake older core tarballs carried: demote it to a
        // scan-invisible dot dir -- but only once the shared module is really
        // readable through the bundle, so the fallback stays alive until the
        // store has landed. On user-uploaded static/ platforms modules/redis
        // is the user's content: leave it alone and skip the bundle instead
        // (global-bd.inc keeps wiring Valkey through the modules/redis path).
        if ($bd_is_static) {
          if (provision_file()->exists($bd_baked_redis)->status()) {
            if (is_link($bd_contrib_symlink)) {
              provision_file()->unlink($bd_contrib_symlink)
                ->succeed('Removed symlink @path (platform carries its own modules/redis)')
                ->fail('Could not remove symlink @path');
            }
            drush_log(dt('Platform ships its own modules/redis; shared Backdrop contrib not attached.'), 'message');
            $bd_contrib_dir = FALSE;
          }
        }
        elseif (is_dir($bd_baked_redis) && !is_link($bd_baked_redis)
          && is_readable($bd_contrib_dir . '/redis_backdrop/redis.autoload.inc')) {
          if (provision_file()->exists($bd_baked_redis_dot)->status()) {
            _provision_recursive_delete($bd_baked_redis_dot);
            drush_log(dt('Stale dot-copy of baked redis removed from: !dot', array('!dot' => $bd_baked_redis_dot)), 'message');
          }
          rename($bd_baked_redis, $bd_baked_redis_dot);
          drush_log(dt('Baked redis module renamed to dot-copy : !dot', array('!dot' => $bd_baked_redis_dot)), 'message');
        }
        // Check if a static copy exists first and demote it to a dot dir,
        // exactly like the Drupal arms do.
        if ($bd_contrib_dir && is_dir($bd_contrib_symlink) && !is_link($bd_contrib_symlink)) {
          if (provision_file()->exists($bd_contrib_dot)->status()) {
            _provision_recursive_delete($bd_contrib_dot);
            drush_log(dt('Static dot-copy of BOA contrib removed from: !contrib', array('!contrib' => $bd_contrib_dot)), 'message');
          }
          rename($bd_contrib_symlink, $bd_contrib_dot);
          drush_log(dt('Static BOA contrib renamed to dot-copy : !dot', array('!dot' => $bd_contrib_dot)), 'message');
        }
        if ($bd_contrib_dir && !provision_file()->exists($bd_contrib_symlink)->status()) {
          provision_file()->symlink($bd_contrib_dir, $bd_contrib_symlink)
            ->succeed('Created symlink @path to @target')
            ->fail('Could not create symlink @path to @target: @reason');
        }
      }
    }

    drush_set_option('sites', array_keys((array) provision_drupal_find_sites()), 'drupal');
    drush_log(dt("This platform is running @short_name @version", array('@short_name' => 'Backdrop', '@version' => _provision_backdrop_get_version())), 'info');
    drush_set_option('packages', _scrub_object(provision_backdrop_find_packages()), 'drupal');

    // The Backdrop signal the front end keys on (its presence marks the platform
    // as Backdrop).
    drush_set_option('backdrop', _provision_backdrop_get_version(), 'drupal');

    // Determine latest BOA contrib directory name and add INI template if missing.
    $pathSal = d()->root . '/sites/all';
    $pathDrc = d()->root . '/sites/all/drush';
    $iniFile = d()->root . '/sites/all/modules/default.boa_platform_control.ini';
    $iniTpl = '/data/conf/default.boa_platform_control.ini';

    if (!provision_file()->exists($pathSal)->status()) {
      provision_file()->create_dir($pathSal, dt("Sites All dir created"), 0755);
    }
    if (!provision_file()->exists($pathDrc)->status()) {
      provision_file()->create_dir($pathDrc, dt("Sites All Drush dir created"), 0755);
    }

    if (!provision_file()->exists($iniFile)->status() && provision_file()->exists($iniTpl)->status()) {
      provision_file()->copy($iniTpl, $iniFile);
    }

    if (!preg_match("/\/static\//", d()->root)) {
      if (!provision_is_hostmaster_site()) {
        provision_file()->chmod(d()->root . '/sites/all/modules', 02775)
          ->succeed('Changed permissions of <code>@path</code> to @perm')
          ->fail('Could not change permissions of <code>@path</code> to @perm');
        provision_file()->chmod(d()->root . '/sites/all/themes', 02775)
          ->succeed('Changed permissions of <code>@path</code> to @perm')
          ->fail('Could not change permissions of <code>@path</code> to @perm');
        provision_file()->chmod(d()->root . '/sites/all/libraries', 02775)
          ->succeed('Changed permissions of <code>@path</code> to @perm')
          ->fail('Could not change permissions of <code>@path</code> to @perm');
      }
      provision_file()->chmod(d()->root . '/sites/all', 0755)
        ->succeed('Changed permissions of <code>@path</code> to @perm')
        ->fail('Could not change permissions of <code>@path</code> to @perm');
      provision_file()->chmod(d()->root . '/sites', 0751)
        ->succeed('Changed permissions of <code>@path</code> to @perm')
        ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($pathDrc)->status()) {
      provision_file()->chmod(d()->root . '/sites/all/drush', 0755)
        ->succeed('Changed permissions of <code>@path</code> to @perm')
        ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    provision_drupal_push_site();
  }
  elseif (d()->type === 'site') {

    // Re-set the ROOT PATH for Drush.
    drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);

    // Reload db creds from the persisted drushrc before the package map (which
    // needs db_name) and the settings re-render below, so neither runs against
    // an empty 'site' context (see _provision_backdrop_ensure_db_creds_loaded).
    _provision_backdrop_ensure_db_creds_loaded();
    drush_set_option('packages', _scrub_object(provision_backdrop_system_map()), 'site');
    // This is the actual Backdrop provisioning requirements.
    _provision_backdrop_create_directories();
    _provision_backdrop_maintain_aliases();
    _provision_backdrop_ensure_htaccess_update();
    // Requires at least the database settings to complete.

    _provision_backdrop_create_settings_file();

    provision_drupal_push_site(drush_get_option('override_slave_authority', FALSE));
  }
}

/**
 * Implements drush_hook_provision_post_COMMAND().
 */
function drush_provision_backdrop_post_provision_verify() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }

  // Add platform root auto-discovery to avoid confusing
  // Composer based codebase root with the real root.
  d()->root = provision_auto_fix_platform_root(d()->root);

  if (d()->type === 'site') {
    _provision_backdrop_rebuild_caches();
    drush_set_option('installed', TRUE, 'site');

    _provision_client_create_symlink();
    drush_include_engine('backdrop', 'cron_key');

    $iniFile = d()->root . '/sites/' . d()->uri . '/modules/default.boa_site_control.ini';
    $iniTpl = '/data/conf/default.boa_site_control.ini';

    if (!provision_file()->exists($iniFile)->status() && provision_file()->exists($iniTpl)->status()) {
      provision_file()->copy($iniTpl, $iniFile);
    }
  }
}
