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

    // Verify the platform first.
    _drush_provision_verify_current_platform();

    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
    provision_prepare_environment();

    _provision_drupal_create_settings_file();
    provision_drupal_push_site();

    drush_errors_on();
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL);
  }
}

/**
 * Helper function to run provision-verify logic on the current site's platform.
 * Can only be used when a Site context is active.
 * Called before from site verify and site install commands.
 */
function _drush_provision_verify_current_platform() {
  if (d()->type == 'site' && d()->platform) {
    drush_log(dt('Verifying platform before site...'), 'ok');
    $args = array();
    $options = array();
    $options['runner'] = drush_get_option('runner');
    $provision_log_type = drush_get_option('runner') == 'hosting_task'? 'p_log': 'ok';

    $result = provision_backend_invoke(d()->platform->name, 'provision-verify', $args, $options);
    if ($result['error_status']) {
      drush_log(dt("An error was thrown during the 'drush @alias provision-verify' command.", array(
        '@alias' => d()->platform->name,
      )), 'error');
      return drush_get_error();
    }
    else {
      # @TODO: Platforms are not receiving package data when sites are verifying.
      drush_log(dt('Platform verified without error. Continuing on to site.'), $provision_log_type);
    }
  }
}

/**
 * Implements drush_hook_pre_COMMAND().
 */
function drush_provision_drupal_pre_provision_verify() {

  // If the "runner" of the command is "hosting_task", use "p_log". Otherwise, use 'ok' so it is visible in CLI calls.
  // @TODO: Update provision_process to hide/show all "p_*" commands. Or... can drush_get_option be used inside provision_process instead?
  $provision_log_type = drush_get_option('runner') == 'hosting_task'? 'p_info': 'ok';
  if (d()->type === 'server') {

    $config_root = dirname(d()->config_path);
    provision_file()->create_dir($config_root, dt('Provision configuration root'), 0711);
    provision_file()->create_dir(d()->config_path, dt('Provision configuration'), 0711);
    provision_file()->create_dir(d()->clients_path, dt('Provision client home'), 0711);
    if (d()->name == '@server_master') {
      provision_file()->create_dir(d()->backup_path, dt('Backup'), 0700);
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

    // Translation strings
    $t = array();
    $t['@root'] = d()->root;
    $t['@makefile'] = d()->makefile;
    $t['@git_root'] = d()->git_root;
    $t['@git_remote'] = d()->git_remote;
    $t['@git_reference'] = d()->git_reference;

    // There are 6 states possible here:
    // 1. Git Platform, path does not exist.
    // 2. Git Platform, path does exist.
    // 3. makefile platform, path does not exist.
    // 4. makefile platform, path does exist.
    // 5. adhoc platform, path does not exist.
    // 6. adhoc platform, path does exist.

    // 1. Git Platform, does not exist.
    if (!empty(d()->git_root) && !file_exists(d()->git_root)) {
      drush_log(dt("Directory @git_root does not exist. Cloning git repository @git_remote to @git_root ...", $t), 'ok');

      // git_remote is required if git_root is set.
      if (empty(d()->git_remote)) {
        return drush_set_error("DRUSH_GIT_FAILED", dt('The git_remote property is required when git_root is used.'));
      }

      // Clone the git repo.
      provision_process([
          "git",
          "clone",
          "--recursive",
          d()->git_remote,
          d()->git_root,
      ], null, dt('Cloning git repository to @directory ...', array('@directory' => d()->git_root)), [], TRUE, NULL, TRUE, $provision_log_type);
    }

    // 2. Git Platform, does exist.
    elseif (!empty(d()->git_root) && file_exists(d()->git_root)) {
      // @TODO: Confirm git remote and ref matches.
      drush_log(dt('Directory already exists at specified git_root: @git_root', $t), 'ok');
    }

    // 3. makefile platform, path does not exist.
    elseif (!empty(d()->makefile) && !file_exists(d()->root)) {
      drush_log(dt('Platform makefile destination path (@root) does not exist, building from drush makefile (@makefile)', $t), 'notice');
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

    // 4. makefile platform, path does exist.
    elseif (!empty(d()->makefile) && file_exists(d()->root)) {
      drush_log(dt('Platform makefile destination path (@root) already exists.'), 'notice');
    }

    // 5. adhoc platform, path does not exist.
    elseif (!file_exists(d()->root)) {
      return drush_set_error("DRUSH_PLATFORM_VERIFY_FAILED",
          dt("Nothing found at platform root (@root). Install code manually or set 'makefile' or 'git_remote' properties.", $t));

    }

    // 6. adhoc platform, path does exist.
    elseif (file_exists(d()->root)) {
      drush_log(dt('Platform path found (@root).'), 'notice');
    }


    // Run things if git  git_reference
    if (!empty(d()->git_root)) {

      // Set git remote
      if (!empty(d()->git_remote)) {

        // Check that git_root is an actual .git working copy first
        $t = [
          '%git_remote' => d()->git_remote,
          '%git_root' => d()->git_root,
          '%git_reference' => d()->git_reference,
          '%git_docroot' => d()->git_docroot,
        ];
        $expected_git_config_path = d()->git_root . '/.git';
        if (drush_get_option('provision_set_remote_url', false) && file_exists($expected_git_config_path)) {
          provision_process([
            "git",
            "remote",
            "set-url",
            "origin",
            d()->git_remote
          ],
            d()->git_root, dt('Setting git remote to %git_remote...', $t), [], TRUE, NULL, TRUE, $provision_log_type);
        }
      }

      // Fetch latest changes
      provision_process([
        "git",
        "fetch",
        "-a",
      ],
        d()->git_root, dt('Fetching latest changes...'), [], TRUE, NULL, TRUE, $provision_log_type);

      // Check out the git reference.
      if (!empty(d()->git_reference)) {
        // Allow callers to set git_checkout_option drush option. For example, set to "-b" to create a branch.
        $checkout_option = drush_get_option('git_checkout_option', NULL);
        provision_process([
          "git",
          "checkout",
          d()->git_reference
        ],
          d()->git_root, dt('Checking out git reference %git_reference ...', $t), [], TRUE, NULL, TRUE, $provision_log_type);
      }

      // Output Git Information
      drush_log(dt('Current working copy status'), 'ok');
      foreach ([
                 ['git', 'log', '-3'],
                 ['git', 'status'],
                 ['git', 'remote',  '-v'],
               ] as $command) {
        provision_process($command, d()->git_root, dt('Git Information'), [], TRUE, NULL, TRUE, $provision_log_type);
        /** @var \Symfony\Component\Process\Process $process */
        $process = drush_get_context('provision_process_result');
        drush_log($process->getOutput(), 'ok');
      }
    }

    // Composer Install Support
    // Step 0: if drush variable allows composer install on platforms, and check for composer executable fails, show warning.
    $composer_bin = shell_exec('which composer');
    if (drush_get_option('provision_composer_install_platforms', TRUE) && !shell_exec($composer_bin)) {
      drush_log(dt('The composer executable was not found. Install composer using instructions located at https://getcomposer.org/doc/00-intro.md#globally or, if you wish to disable composer install, set "provision_composer_install_platforms" to FALSE in the ~/.drush/drushrc.php file.'), 'warning');
    }
    // If drush variable allows composer install on platforms (and composer exec passed)...
    elseif (drush_get_option('provision_composer_install_platforms', TRUE)) {
      drush_log(dt('Composer bin found: @composer_bin', array(
        '@composer_bin' => $composer_bin,
      )), 'ok');

      // Detect composer.json and run composer install.
      // Step 1: Look for composer directory in git_root or root.
      // Copied from composer Application.php
      $dir = d()->root;
      $home = realpath(getenv('HOME') ?: getenv('USERPROFILE') ?: '/');
      while (dirname($dir) !== $dir && $dir !== $home) {
        // Copied from Factory::getComposerFile()
        $getComposerFile = trim(getenv('COMPOSER')) ?: './composer.json';
        if (file_exists($dir.'/'.$getComposerFile)) {
          $oldWorkingDir = getcwd();
          break;
        }
        $dir = dirname($dir);
      }
      $composer_directory = $dir;

      // Step 2: Run composer install if composer.json is present, unless drush option provision_composer_install_platforms_verify_always is set to false.
      if ($composer_directory && drush_get_option('provision_composer_install_platforms_verify_always', TRUE)) {

        // Composer Install command: Do not interact, do not show download progress.
        // Customizable by setting drush option 'provision_composer_install_command'
        $composer_command = drush_get_option('provision_composer_install_command', 'composer install --no-interaction --no-progress --no-dev --no-suggest --ansi');

        provision_process($composer_command, $composer_directory, dt('Running command in @directory: @command', array(
          '@directory' => $composer_directory,
          '@command' => $composer_command,
        )), array(), TRUE, dt('Composer Install failed.'), TRUE, $provision_log_type);
        drush_log(dt('Composer install complete.'), 'ok');
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
        dt("An error occured while preparing code in @path: @errors", array(
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

    // @TODO: Convert to a call to "provision-packages".
    drush_log(dt("This platform is running @short_name @version", array('@short_name' => 'drupal', '@version' => drush_drupal_version())), 'ok');
    // THIS WORKS:
    // Found 3 'profiles' packages in platform: standard, minimal, devmaster[ok]
    // THIS IS NO LONGER RUN IN PROVISION_BACKEND_INVOKE so ...
    $packages_list =  _scrub_object(provision_find_packages());
    foreach ($packages_list as $type => $packages) {
      drush_log(dt("Found @count '@type' packages in platform: @packages", array(
          '@count' => count($packages),
          '@type' => $type,
          '@packages' => implode(', ', array_keys($packages)))
      ), 'ok');
    }
    drush_set_option('packages', $packages_list, 'drupal');
    provision_drupal_push_site();
  }
  elseif (d()->type === 'site') {

    // @TODO: Remove once we are sure provision-packages works.
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
  if (d()->type === 'site') {
    _provision_drupal_rebuild_caches();
    drush_set_option('installed', TRUE, 'site');

    _provision_client_create_symlink();
    drush_include_engine('drupal', 'cron_key');

    # Cleanup useless `aegir.services.yml` files.
    # See: https://www.drupal.org/project/provision/issues/3066538
    $path = d()->site_path . '/aegir.services.yml';
    if (provision_file()->exists($path)->status()) {
      provision_file()->unlink($path)
        ->succeed('Cleaned up unused Aegir services.yml file.')
        ->fail('Failed to clean up unused Aegir services.yml file.', 'PROVISION_DELETE_AEGIR_SERVICES_YML_FAILED');
    }
  }
}
