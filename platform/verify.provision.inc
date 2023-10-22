<?php

/**
 * @file
 * Code related to verify tasks.
 */

/**
 * Provision verify command.
 *
 * Disable an enabled and installed site
 */
function drush_provision_drupal_provision_verify_validate() {
  if (d()->type === 'site') {
    provision_lock_some_vnd();
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
    provision_prepare_environment();

    _provision_drupal_create_settings_file();
    provision_drupal_push_site();

    drush_errors_on();
    if (drush_drupal_major_version() < 10) {
      drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL);
    }
    else {
      drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_DATABASE);
      drush_log("Skipping FULL bootstrap for drupal 10+ in drush_provision_drupal_provision_verify_validate");
    }
  }
}


/**
 * Implements drush_hook_pre_COMMAND().
 */
function drush_provision_drupal_pre_provision_verify() {
  if (d()->type === 'server') {

    $config_root = dirname(d()->config_path);
    provision_file()->create_dir($config_root, dt('Provision configuration root'), 0711);
    provision_file()->create_dir(d()->config_path, dt('Provision configuration'), 0711);
    provision_file()->create_dir(d()->clients_path, dt('Provision client home'), 0750);
    if (d()->name == '@server_master') {
      provision_file()->create_dir(d()->backup_path, dt('Backup'), 0750);
      provision_file()->create_dir(d()->include_path, dt('Provision PHP configuration'), 0711);
      if (!provision_file()->exists(d()->include_path . '/global.inc')->succeed('Global configuration file exists')->status()) {
        // Create an empty global.inc so the include doesn't fail with
        // open_basedir restrictions.
        $config = new Provision_Config_Global_Settings(d()->name);
        $config->write();
      }
    }
    else {
      if (provision_file()->exists(d()->include_path)) {
        d()->service('http')->sync(d()->include_path);
      }
    }
  }
  elseif (d()->type === 'platform') {

    // Add platform root auto-discovery to avoid confusing
    // Composer based D8+ codebase root with Drupal real root.
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
      // @TODO: Remove this once is committed.
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
      // Step 1: Look for composer directory. Could be Drupal root. Could be the git repo_root.
      if (provision_file()->exists(d()->root . DIRECTORY_SEPARATOR . 'composer.json')->status()) {
        $composer_directory = d()->root;
      }
      elseif (d()->repo_path && provision_file()->exists(d()->repo_path . DIRECTORY_SEPARATOR . 'composer.json')->status()) {
        $composer_directory = d()->repo_path;
      }

      // Step 2: Run composer install if composer.json is present.
      // If drush option provision_composer_install_platforms_verify_always is set to
      // false, only run `composer-install` if ./vendor directory is missing.
      if (isset($composer_directory) && (!file_exists($composer_directory . DIRECTORY_SEPARATOR . 'vendor') || drush_get_option('provision_composer_install_platforms_verify_always', FALSE))) {

        // Composer Install command: Do not interact, do not show download progress.
        // Customizable by setting drush option 'provision_composer_install_command'
        $composer_command = drush_get_option('provision_composer_install_command', 'composer install --no-interaction --no-progress --no-dev');

        // Implement Symfony Process component for line-by-line output logging.
        provision_process($composer_command, $composer_directory);

      }
    }

    // Re-set the ROOT PATH for Drush.
    // In cases where we just build the platform Drush would have cleared this value.
    // See Drush commit 3b74d40b1228f022464b92243c16127eb613c2df
    drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);

    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_ROOT);
    // If we couldn't bootstrap, there's no sense in continuing past this point.
    if (drush_get_error()) {
      $errors = drush_get_error_log();
      foreach ($errors as $type => &$error) {
        $error = "$type: {$error[0]}";
      }
      return drush_set_error("DRUSH_INVALID_DRUPAL_ROOT",
        dt("A Drupal codebase was not found in @path. Please check that the directory exists and Drupal's index.php file is present. Check if the Publish Path of the platform exists, contains Drupal and is readable, then try again. The errors were: @errors", array(
          '@path' => d()->root,
          '@errors' => implode('|', $errors)
        )));
    }

    // Fix ownership early enough
    $success = drush_shell_exec("sudo /usr/local/bin/fix-drupal-platform-ownership.sh --root=%s --script-user=%s --web-group=%s", d()->root, d()->server->script_user, d()->server->web_group);
    $result = drush_shell_exec_output();
    foreach ($result as $index => $line) {
      if (!$success) { // Log the entire error as a warning.
        $line_status = 'warning';
      }
      else { // Only log the last line as success.
        $line_status = $index+1 == count($result) ? 'success' : 'notice';
      }
      drush_log($line, $line_status);
    }

    provision_file()->writable(d()->root . '/sites')
      ->succeed('Drupal sites directory @path is writable by the provisioning script')
      ->fail('Drupal sites directory @path is not writable by the provisioning script', 'PROVISION_SITES_DIR_NOT_WRITABLE');

    // Ensure sites.php file exists for Drupal 8 and above.
    $sitesfile = "sites/sites.php";
    if (drush_drupal_major_version() >= 8 && !file_exists($sitesfile)) {
      if (!drush_op('copy', 'sites/example.sites.php', $sitesfile) && !drush_get_context('DRUSH_SIMULATE')) {
        return drush_set_error(dt('Failed to copy sites/sites.php to @sitesfile', array('@sitesfile' => $sitesfile)));
      }
    }

    drush_set_option('sites', array_keys((array) provision_drupal_find_sites()), 'drupal');
    drush_log(dt("This platform is running @short_name @version", array('@short_name' => 'drupal', '@version' => drush_drupal_version())), 'info');
    drush_set_option('packages', _scrub_object(provision_find_packages()), 'drupal');

    // Determine latest BOA contrib directory name and add INI template if missing.
    $pathAll = '/data/all/';
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

    $core8_version_id = d()->root . '/core';
    $core9_version_id = d()->root . '/core/themes/olivero';
    $core10_version_f = d()->root . '/core/themes/classy';

    if (provision_file()->exists($pathAll)->status() &&
       (provision_file()->exists($core8_version_id)->status() ||
        preg_match("/\/static\//", d()->root))) {
      $sortedDirs = array();
      foreach(scandir($pathAll) as $dir) {
        if(is_dir($pathAll . $dir)) {
          array_push($sortedDirs, $dir);
        }
      }
      $latestDir = max($sortedDirs);
      drush_log(dt('Contrib latestDir: !latest', array('!latest' => $latestDir)), 'message');

      if (is_numeric($latestDir)) {
        $core6_contrib_dir = '/data/all/' . $latestDir . '/o_contrib';
        $core7_contrib_dir = '/data/all/' . $latestDir . '/o_contrib_seven';
        $core8_contrib_dir = '/data/all/' . $latestDir . '/o_contrib_eight';
        $core9_contrib_dir = '/data/all/' . $latestDir . '/o_contrib_nine';
        $core10_contrib_dir = '/data/all/' . $latestDir . '/o_contrib_ten';
      }
      else {
        $core6_contrib_dir = '/data/all/000/modules/o_contrib';
        $core7_contrib_dir = '/data/all/000/modules/o_contrib_seven';
        $core8_contrib_dir = '/data/all/000/modules/o_contrib_eight';
        $core9_contrib_dir = '/data/all/000/modules/o_contrib_nine';
        $core10_contrib_dir = '/data/all/000/modules/o_contrib_ten';
      }

      $core6_version_id = d()->root . '/modules/path_alias_cache';
      $core6_contrib_symlink = d()->root . '/modules/o_contrib';
      $core6_contrib_dot = d()->root . '/modules/.o_contrib_dont_use';
      $core7_version_id = d()->root . '/modules/overlay';
      $core7_contrib_symlink = d()->root . '/modules/o_contrib_seven';
      $core7_contrib_dot = d()->root . '/modules/.o_contrib_seven_dont_use';
      $core8_version_id = d()->root . '/core';
      $core8_contrib_symlink = d()->root . '/modules/o_contrib_eight';
      $core8_contrib_dot = d()->root . '/modules/.o_contrib_eight_dont_use';
      $core9_version_id = d()->root . '/core/themes/olivero';
      $core9_contrib_symlink = d()->root . '/modules/o_contrib_nine';
      $core9_contrib_dot = d()->root . '/modules/.o_contrib_nine_dont_use';
      $core10_version_f = d()->root . '/core/themes/classy';
      $core10_contrib_symlink = d()->root . '/modules/o_contrib_ten';
      $core10_contrib_dot = d()->root . '/modules/.o_contrib_ten_dont_use';

      $core6_contrib_js_file = $core6_contrib_dir . '/js/js.php';
      $core7_contrib_js_file = $core7_contrib_dir . '/js/js.php';
      $core6_contrib_js_symlink = d()->root . '/js.php';
      $core7_contrib_js_symlink = d()->root . '/js.php';

      // Make sure the platform root and core modules directories are writable (BOA).
      if (provision_file()->exists($core6_contrib_dir)->status() ||
          provision_file()->exists($core7_contrib_dir)->status() ||
          provision_file()->exists($core8_contrib_dir)->status() ||
          provision_file()->exists($core9_contrib_dir)->status() ||
          provision_file()->exists($core10_contrib_dir)->status()) {
        provision_file()->writable(d()->root)
          ->succeed('Drupal root directory @path is writable by the provisioning script')
          ->fail('Drupal root directory @path is not writable by the provisioning script', 'PROVISION_DRUPAL_ROOT_DIR_NOT_WRITABLE');
        provision_file()->writable(d()->root . '/modules')
          ->succeed('Drupal core modules directory @path is writable by the provisioning script')
          ->fail('Drupal core modules directory @path is not writable by the provisioning script', 'PROVISION_CORE_MODULES_DIR_NOT_WRITABLE');
      }

      // Add symlink to BOA specific contrib in Pressflow 6.
      // Check if static copy exists first
      if (is_dir($core6_contrib_symlink) && !is_link($core6_contrib_symlink)) {
        // Run cleanup of previous dot dir if exists
        // and current contrib is not a symlink again.
        if (provision_file()->exists($core6_contrib_dot)->status()) {
          _provision_recursive_delete($core6_contrib_dot);
          drush_log(dt('Static dot-copy of BOA contrib removed from: !contrib', array('!contrib' => $core6_contrib_dot)), 'message');
        }
        // Rename static contrib to dot dir if exists
        rename($core6_contrib_symlink, $core6_contrib_dot);
        drush_log(dt('Static BOA contrib renamed to dot-copy : !dot', array('!dot' => $core6_contrib_dot)), 'message');
      }
      if (provision_file()->exists($core6_contrib_dir)->status() &&
          provision_file()->exists($core6_version_id)->status() &&
          !provision_file()->exists($core6_contrib_symlink)->status()) {
        provision_file()->symlink($core6_contrib_dir, $core6_contrib_symlink)
          ->succeed('Created symlink @path to @target')
          ->fail('Could not create symlink @path to @target: @reason');
      }

      // Add symlink to BOA specific contrib in Drupal 7.
      // Check if static copy exists first
      if (is_dir($core7_contrib_symlink) && !is_link($core7_contrib_symlink)) {
        // Run cleanup of previous dot dir if exists
        // and current contrib is not a symlink again.
        if (provision_file()->exists($core7_contrib_dot)->status()) {
          _provision_recursive_delete($core7_contrib_dot);
          drush_log(dt('Static dot-copy of BOA contrib removed from: !contrib', array('!contrib' => $core7_contrib_dot)), 'message');
        }
        // Rename static contrib to dot dir if exists
        rename($core7_contrib_symlink, $core7_contrib_dot);
        drush_log(dt('Static BOA contrib renamed to dot-copy : !dot', array('!dot' => $core7_contrib_dot)), 'message');
      }
      if (provision_file()->exists($core7_contrib_dir)->status() &&
         provision_file()->exists($core7_version_id)->status() &&
         !provision_file()->exists($core7_contrib_symlink)->status()) {
        provision_file()->symlink($core7_contrib_dir, $core7_contrib_symlink)
          ->succeed('Created symlink @path to @target')
          ->fail('Could not create symlink @path to @target: @reason');
      }

      // Add symlink to BOA specific contrib in Drupal 8.
      // Check if static copy exists first
      if (is_dir($core8_contrib_symlink) && !is_link($core8_contrib_symlink)) {
        // Run cleanup of previous dot dir if exists
        // and current contrib is not a symlink again.
        if (provision_file()->exists($core8_contrib_dot)->status()) {
          _provision_recursive_delete($core8_contrib_dot);
          drush_log(dt('Static dot-copy of BOA contrib removed from: !contrib', array('!contrib' => $core8_contrib_dot)), 'message');
        }
        // Rename static contrib to dot dir if exists
        rename($core8_contrib_symlink, $core8_contrib_dot);
        drush_log(dt('Static BOA contrib renamed to dot-copy : !dot', array('!dot' => $core8_contrib_dot)), 'message');
      }
      if (provision_file()->exists($core8_contrib_dir)->status() &&
          provision_file()->exists($core8_version_id)->status() &&
          !provision_file()->exists($core9_version_id)->status() &&
          !provision_file()->exists($core8_contrib_symlink)->status()) {
        provision_file()->symlink($core8_contrib_dir, $core8_contrib_symlink)
          ->succeed('Created symlink @path to @target')
          ->fail('Could not create symlink @path to @target: @reason');
      }
      if (provision_file()->exists($core8_contrib_dir)->status() &&
          provision_file()->exists($core8_version_id)->status()) {
        if (provision_file()->exists($core9_contrib_symlink)->status()) {
          // Rename core9_contrib_symlink to core9_contrib_dot if exists in D8 platform
          rename($core9_contrib_symlink, $core9_contrib_dot);
          drush_log(dt('D9 BOA contrib renamed to dot-copy : !dot', array('!dot' => $core9_contrib_dot)), 'message');
        }
        if (provision_file()->exists($core10_contrib_symlink)->status()) {
          // Rename core10_contrib_symlink to core10_contrib_dot if exists in D8 platform
          rename($core10_contrib_symlink, $core10_contrib_dot);
          drush_log(dt('D10 BOA contrib renamed to dot-copy : !dot', array('!dot' => $core10_contrib_dot)), 'message');
        }
      }

      // Add symlink to BOA specific contrib in Drupal 9.
      // Check if static copy exists first
      if (is_dir($core9_contrib_symlink) && !is_link($core9_contrib_symlink)) {
        // Run cleanup of previous dot dir if exists
        // and current contrib is not a symlink again.
        if (provision_file()->exists($core9_contrib_dot)->status()) {
          _provision_recursive_delete($core9_contrib_dot);
          drush_log(dt('Static dot-copy of BOA contrib removed from: !contrib', array('!contrib' => $core9_contrib_dot)), 'message');
        }
        // Rename static contrib to dot dir if exists
        rename($core9_contrib_symlink, $core9_contrib_dot);
        drush_log(dt('Static BOA contrib renamed to dot-copy : !dot', array('!dot' => $core9_contrib_dot)), 'message');
      }
      if (provision_file()->exists($core9_contrib_dir)->status() &&
          provision_file()->exists($core9_version_id)->status() &&
          provision_file()->exists($core10_version_f)->status() &&
          !provision_file()->exists($core9_contrib_symlink)->status()) {
        provision_file()->symlink($core9_contrib_dir, $core9_contrib_symlink)
          ->succeed('Created symlink @path to @target')
          ->fail('Could not create symlink @path to @target: @reason');
      }
      if (provision_file()->exists($core9_contrib_dir)->status() &&
          provision_file()->exists($core9_version_id)->status()) {
        if (provision_file()->exists($core8_contrib_symlink)->status()) {
          // Rename core8_contrib_symlink to core8_contrib_dot if exists in D9 platform
          rename($core8_contrib_symlink, $core8_contrib_dot);
          drush_log(dt('D8 BOA contrib renamed to dot-copy : !dot', array('!dot' => $core8_contrib_dot)), 'message');
        }
        if (provision_file()->exists($core10_contrib_symlink)->status()) {
          // Rename core10_contrib_symlink to core10_contrib_dot if exists in D9 platform
          rename($core10_contrib_symlink, $core10_contrib_dot);
          drush_log(dt('D10 BOA contrib renamed to dot-copy : !dot', array('!dot' => $core10_contrib_dot)), 'message');
        }
      }

      // Add symlink to BOA specific contrib in Drupal 10.
      // Check if static copy exists first
      if (is_dir($core10_contrib_symlink) && !is_link($core10_contrib_symlink)) {
        // Run cleanup of previous dot dir if exists
        // and current contrib is not a symlink again.
        if (provision_file()->exists($core10_contrib_dot)->status()) {
          _provision_recursive_delete($core10_contrib_dot);
          drush_log(dt('Static dot-copy of BOA contrib removed from: !contrib', array('!contrib' => $core10_contrib_dot)), 'message');
        }
        // Rename static contrib to dot dir if exists
        rename($core10_contrib_symlink, $core10_contrib_dot);
        drush_log(dt('Static BOA contrib renamed to dot-copy : !dot', array('!dot' => $core10_contrib_dot)), 'message');
      }
      if (provision_file()->exists($core10_contrib_dir)->status() &&
          provision_file()->exists($core9_version_id)->status() &&
          !provision_file()->exists($core10_version_f)->status() &&
          !provision_file()->exists($core10_contrib_symlink)->status()) {
        provision_file()->symlink($core10_contrib_dir, $core10_contrib_symlink)
          ->succeed('Created symlink @path to @target')
          ->fail('Could not create symlink @path to @target: @reason');
      }
      if (provision_file()->exists($core10_contrib_dir)->status() &&
          provision_file()->exists($core9_version_id)->status() &&
          !provision_file()->exists($core10_version_f)->status()) {
        if (provision_file()->exists($core8_contrib_symlink)->status()) {
          // Rename core8_contrib_symlink to core8_contrib_dot if exists in D10 platform
          rename($core8_contrib_symlink, $core8_contrib_dot);
          drush_log(dt('D8 BOA contrib renamed to dot-copy : !dot', array('!dot' => $core8_contrib_dot)), 'message');
        }
        if (provision_file()->exists($core9_contrib_symlink)->status()) {
          // Rename core9_contrib_symlink to core9_contrib_dot if exists in D10 platform
          rename($core9_contrib_symlink, $core9_contrib_dot);
          drush_log(dt('D9 BOA contrib renamed to dot-copy : !dot', array('!dot' => $core9_contrib_dot)), 'message');
        }
      }

      // Add symlink to BOA specific JS Callback Handler support in Pressflow 6.
      if (provision_file()->exists($core6_contrib_js_file)->status() &&
          provision_file()->exists($core6_version_id)->status() &&
          !provision_file()->exists($core6_contrib_js_symlink)->status()) {
        provision_file()->symlink($core6_contrib_js_file, $core6_contrib_js_symlink)
          ->succeed('Created symlink @path to @target')
          ->fail('Could not create symlink @path to @target: @reason');
      }

      // Add symlink to BOA specific JS Callback Handler support in Drupal 7.
      if (provision_file()->exists($core7_contrib_js_file)->status() &&
         provision_file()->exists($core7_version_id)->status() &&
         !provision_file()->exists($core7_contrib_js_symlink)->status()) {
        provision_file()->symlink($core7_contrib_js_file, $core7_contrib_js_symlink)
          ->succeed('Created symlink @path to @target')
          ->fail('Could not create symlink @path to @target: @reason');
      }
    }
    provision_drupal_push_site();
  }
  elseif (d()->type === 'site') {

    // Re-set the ROOT PATH for Drush.
    // In cases where we just build the platform Drush would have cleared this value.
    // See Drush commit 3b74d40b1228f022464b92243c16127eb613c2df
    drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);

    drush_set_option('packages', _scrub_object(provision_drupal_system_map()), 'site');
    // This is the actual drupal provisioning requirements.
    _provision_drupal_create_directories();
    _provision_drupal_maintain_aliases();
    _provision_drupal_ensure_htaccess_update();
    // Requires at least the database settings to complete.

    _provision_drupal_create_settings_file();

    // If this is the hostmaster site, save the ~/.drush/drushrc.php file.
    $hostmaster = provision_sitealias_get_record('@hostmaster');
    if (d()->root == $hostmaster['root'] && d()->uri == $hostmaster['uri']) {
      $aegir_drushrc = new Provision_Config_Drushrc_Aegir();
      $aegir_drushrc->write();

      drush_invoke_process('@none', "cache-clear", array('drush'));
    }

    provision_drupal_push_site(drush_get_option('override_slave_authority', FALSE));
  }
}

/**
 * Implements drush_hook_provision_post_COMMAND().
 */
function drush_provision_drupal_post_provision_verify() {

  // Add platform root auto-discovery to avoid confusing
  // Composer based D8+ codebase root with Drupal real root.
  d()->root = provision_auto_fix_platform_root(d()->root);

  if (d()->type === 'site') {
    if (drush_drupal_major_version() < 10) {
      _provision_drupal_rebuild_caches();
    }
    drush_set_option('installed', TRUE, 'site');

    _provision_client_create_symlink();
    drush_include_engine('drupal', 'cron_key');

    $iniFile = d()->root . '/sites/' . d()->uri . '/modules/default.boa_site_control.ini';
    $iniTpl = '/data/conf/default.boa_site_control.ini';

    if (!provision_file()->exists($iniFile)->status() && provision_file()->exists($iniTpl)->status()) {
      provision_file()->copy($iniTpl, $iniFile);
    }

    provision_unlock_some_vnd();
  }
}
