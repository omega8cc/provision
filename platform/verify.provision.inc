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
    // _drush_provision_verify_current_platform();

    // Check if index.php, because root might exist but no files in a composer installation.
    if (file_exists(d()->root . '/index.php')) {
      drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
      provision_prepare_environment();
    }

    drush_errors_on();

    // @TODO: Move to post-hosting-task, if needed at all.
    // drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL);
  }
}

///**
// * Helper function to run provision-verify logic on the current site's platform.
// * Can only be used when a Site context is active.
// * Called before from site verify and site install commands.
// */
//function _drush_provision_verify_current_platform() {
//  $drush_log_type = drush_get_option('runner') == 'hosting_task'? 'p_log': 'ok';
//  if (d()->type == 'site' && d()->platform) {
//    drush_log(dt('Verifying codebase at git reference @git_reference...', ['@git_reference' => d()->platform->git_reference]), $drush_log_type);
//    $args = array();
//    $options = drush_get_merged_options();
//    $options['git_reference'] = d()->platform->git_reference;
//    $options['git_remote'] = d()->platform->git_remote;
//
//    $options['runner'] = drush_get_option('runner');
//    $options['root'] = d()->root;
//    $provision_log_type = drush_get_option('runner') == 'hosting_task'? 'p_log': 'ok';
//
//    chdir(d('@hostmaster')->root);
//    $result = provision_backend_invoke(d()->platform->name, 'provision-verify', $args, $options);
//    if ($result['error_status']) {
//      drush_log(dt("An error was thrown during the 'drush @alias provision-verify' command.", array(
//        '@alias' => d()->platform->name,
//      )), 'error');
//      return drush_get_error();
//    }
//    else {
//      # @TODO: Platforms are not receiving package data when sites are verifying.
//      drush_log(dt('Platform verified without error, verifying site...'), $drush_log_type);
//
//      # DEBUG to figure out where the data is.
//      # drush_log(print_r(array_keys($result), 1), 'warning');
//    }
//  }
//}

/**
 * Implements drush_hook_pre_COMMAND().
 */
function drush_provision_drupal_pre_provision_verify() {

  // If the "runner" of the command is "hosting_task", use "p_log". Otherwise, use 'ok' so it is visible in CLI calls.
  // @TODO: Update provision_process to hide/show all "p_*" commands. Or... can drush_get_option be used inside provision_process instead?
  $provision_log_type = drush_get_option('runner') == 'hosting_task'? 'p_command_log': 'ok';
  $drush_log_type = drush_get_option('runner') == 'hosting_task'? 'p_notice': 'ok';

  // If the runner of this command was hosting_task, use drush log to print processes.
  $log_output = drush_get_option('runner') == 'hosting_task';

  if ($log_output) {
    drush_log(dt('Task is being run from Aegir hosting-task command.'), 'debug');
  }

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
  elseif (d()->type === 'platform' || d()->type === 'site') {

    // Translation strings
    $t = array();
    $t['@root'] = d()->root;
    $t['@makefile'] = d()->makefile;

    // Add git info to translations. (Working copy might not exist yet.)
    if (!empty(d()->git_root)) {
      // Desired state.
      $t['@git_root'] = d()->git_root;
      $t['@git_remote'] = d()->git_remote;
      $t['@git_reference'] = d()->git_reference;
      $t['@git_docroot'] = d()->git_docroot;
    }

    // There are 6 states possible here:
    // 1. Git Platform, path does not exist.
    // 2. Git Platform, path does exist, but is not a git repo. (error)
    // 2. Git Platform, path does exist.
    // 3. makefile platform, path does not exist.
    // 4. makefile platform, path does exist.
    // 5. adhoc platform, path does not exist.
    // 6. adhoc platform, path does exist.

    // 1. Git Platform, does not exist.
    if (!empty(d()->git_root) && !file_exists(d()->git_root)) {
      drush_log(dt("Path @git_root does not exist. Cloning git repository @git_remote to @git_root ...", $t), 'debug');

      // git_remote is required if git_root is set.
      if (empty(d()->git_remote)) {
        return drush_set_error("DRUSH_GIT_FAILED", dt('The git_remote property is empty. Cannot continue.'));
      }

      // Clone the git repo.
      provision_process([
          "git",
          "clone",
          "--recursive",
          d()->git_remote,
          d()->git_root,
      ], null, dt('Cloning @git_remote to @git_root...', $t), [], $log_output, NULL, TRUE, $provision_log_type);
      drush_log('', 'ok');
    }

    // 2. Git Platform, path does exist, but is not a git repo.
    elseif (!empty(d()->git_root) && !d()->isRepo()) {
      return drush_set_error("DRUSH_GIT_FAILED", dt('The path @git_root exists, but is not a git repository. Check the path and try again.', $t));
    }

    // 2. Git Platform, does exist.
    elseif (!empty(d()->git_root) && d()->isRepo()) {
      // @TODO: Confirm git remote and ref matches.
      drush_log(dt('Existing git repository found at @git_root.', $t), $drush_log_type);
    }

    // 3. makefile platform, path does not exist.
    elseif (!empty(d()->makefile) && !file_exists(d()->root)) {
      drush_log(dt('Platform makefile destination path (@root) does not exist, building from drush makefile (@makefile)', $t), $drush_log_type);
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
      drush_log(dt('Platform path found (@root).', [
        '@root' => d()->root
      ]), 'notice');
    }

    // If this is a git platform...
    if (d()->isRepo()) {

      // Current State
      $t['@git_reference_current'] = d()->getBranchOrTag();
      $t['@git_sha'] = d()->getSha();
      // Only branch checkouts have "current" remotes.
      $git_branch = d()->getBranch();
      if ($git_branch) {
        $t['@git_remote_current_name'] = d()->getCurrentRemoteName();
        $t['@git_remote_current'] = d()->getCurrentRemoteUrl();
      }
      $t['@date'] = date('r');

      $t['@type'] = $git_ref_type = d()->isBranchOrTag() ?? 'reference';
      drush_log(dt(''), 'p_command');
      drush_log(dt('Git Information as of @date:', $t), $drush_log_type);

      drush_log(dt('Git repository clone detected in @git_root', $t), $drush_log_type);
      drush_log(dt('Current State:'), $drush_log_type);
      drush_log(dt('    Git @type: @git_reference_current', $t), $drush_log_type);
      drush_log(dt('              SHA: @git_sha', $t), $drush_log_type);
      if ($git_branch) {
        drush_log(dt('    Git Remote: @git_remote_current', $t), $drush_log_type);
      }
      else {
        drush_log(dt('    Git Remote: None (detached HEAD)', $t), $drush_log_type);
      }

      drush_log(dt('Desired State:'), $drush_log_type);
      drush_log(dt('    Git Reference: @git_reference', $t), $drush_log_type);
      drush_log(dt('    Git Remote: @git_remote', $t), $drush_log_type);

      // Set git remote
      // @TODO:  Add the --set-remote option to hosting_task if the user changed it on the platform node.
      // If there is a current and desired git_remote...
      if (!empty($t['@git_remote_current']) && !empty($t['@git_remote'])) {
        // and the current remote differs from the desired remote, throw a warning.
        $remotes_differ = $t['@git_remote_current'] != $t['@git_remote'];

        // If remotes differ, and site is not "protected", update remote.
        if ($remotes_differ) {
          provision_process([
            "git",
            "remote",
            "set-url",
            $t['@git_remote_current_name'],
            d()->git_remote
          ],
            d()->git_root, dt('Setting git remote @git_remote_current_name to @git_remote...', $t), [], $log_output, NULL, TRUE, $provision_log_type);
        }
      }

      // Fetch all remote changes.
      if (drush_get_option('no-fetch', FALSE)) {
        drush_log(dt('Skipping git fetch due to --no-fetch option.'), $drush_log_type);
      } else {
        // Fetch latest changes
        provision_process("git fetch --all --tags --recurse-submodules", d()->git_root, dt('Fetching latest branches and tags....'), NULL, TRUE, dt('Unable to fetch from git remote.'), FALSE, $provision_log_type);
      }

      d()->displayGitStatus();

      // @TODO: Check dirty git status before checkout force and throw a warning.

      // Check out the git reference, unless --no-checkout was specified or git_reference is empty.
      if (drush_get_option('no-checkout', FALSE)) {
        drush_log(dt('Skipping git checkout due to --no-checkout option.'), $drush_log_type);
      }
      elseif (empty(d()->git_reference)) {
        drush_log(dt("Skipping git checkout because the platform's git_reference is empty."), $drush_log_type);
      }
      else {
        // Allow callers to set git-checkout-options drush option. For example, set to "-b" to create a branch.
        $checkout_options = explode(' ', drush_get_option('git-checkout-options', NULL));
        $checkout_command = [
          "git",
          "checkout",
          d()->git_reference,
        ];
        // If git_reset is set, show the diff and a message.
        // We don't need to run git reset, git checkout --force is used instead.
        if (d()->git_reset && drush_get_option('no-reset', FALSE) == TRUE) {

          $log_message = dt('Checking out git reference @git_reference. Skipping git reset due to --no-reset option.', $t);
          $run_git_reset = FALSE;
        }
        elseif (d()->git_reset && drush_get_option('no-reset', FALSE) == FALSE) {
          $log_message = dt('Checking out git reference @git_reference, discarding local changes (--force)...', $t);
          $run_git_reset = TRUE;
        }
        elseif (!d()->git_reset) {
          $run_git_reset = FALSE;
          // @TODO: Detect if this will fail because --force wasn't used.
          $log_message = dt('Checking out git reference @git_reference, preserving any working copy changes...', $t);
        }
        else {
          $run_git_reset = FALSE;
          $log_message = dt('Checking out git reference @git_reference...', $t);
        }

        // Checkout could fail for many reasons so lets allow it to continue.
        $exit_if_checkout_failed = drush_get_option('require-checkout', FALSE);

        //  Git fetch after command so that we are sure we have the right FETCH_HEAD.
        $checkout_command = implode(" ", array_filter(array_merge($checkout_command, $checkout_options)));
        provision_process($checkout_command, d()->git_root, $log_message, [], $log_output, dt('Git checkout failed.'), $exit_if_checkout_failed, $provision_log_type);

        // Reset to @{upstream}, only if there are not un-pushed commits.
        $reset_command = '';
        $log_message = dt('Resetting to git upstream...');
        $new_git_ref_type = d()->isBranchOrTag();
        if ($new_git_ref_type == 'branch') {
          if ($run_git_reset) {
            $reset_command = "git fetch && git reset --hard FETCH_HEAD";
          }
          else {
            $reset_command = "git fetch && git reset FETCH_HEAD";
          }
        }
        elseif ($new_git_ref_type == 'tag' && $run_git_reset) {
          $reset_command = "git reset --hard";
          $log_message = dt('Resetting to git tag...');
        }

        if (d()->isAhead()) {
          drush_log(dt('Skipping git reset because git repository has un-pushed commits. Make sure your local commits are pushed or they may be lost.'), 'warning');
        }
        elseif ($reset_command) {

          // If the repo cannot be written to, git reset will fail.
          // Ths code assumes allowing the `git reset` command to fail with a warning is the desired behavior.
          $error_on_reset_fail = drush_get_option('error_on_git_reset_fail', false);
          provision_process($reset_command, d()->git_root, $log_message, [], $log_output, dt('Unable to reset.'), $error_on_reset_fail, $provision_log_type);
        }
      }

      // Use Git Submodules
      if (d()->hasGitSubmodules() && drush_get_option('no-submodules', FALSE) == TRUE) {
        drush_log(dt('Git Submodules file was found, but submodules were not updated due to --no-submodules option.', $t), 'warning');
      }
      elseif (d()->hasGitSubmodules() && drush_get_option('no-submodules', FALSE) == FALSE) {
        $submodules_command = [
          "git",
          "submodule",
          "update",
          "--init",
          "--recursive",
        ];
        // If git_reset is true and the --no-reset option was not used, add "force" to submodules.
        if (d()->git_reset && drush_get_option('no-reset', FALSE) == FALSE) {
          $submodules_command[] = '--force';
        }
        provision_process($submodules_command, d()->git_root, dt('Update git submodules...'), [], $log_output, NULL, TRUE, $provision_log_type);
      }


      d()->displayGitStatus(FALSE);

      drush_log(dt('Git operations complete. Current status: %git_reference %git_remote %git_sha', [
        '%git_reference' => drush_get_option('git_reference'),
        '%git_remote' => drush_get_option('git_remote'),
        '%git_sha' => drush_get_option('git_sha'),
      ]), 'debug');
    }

    // Composer Install Support
    // Step 0: if drush variable allows composer install on platforms, and check for composer executable fails, show warning.
    $composer_bin = shell_exec('which composer');
    if (drush_get_option('provision_composer_install_platforms', TRUE) && !shell_exec($composer_bin)) {
      return drush_set_error('DRUSH_COMPOSER_ERROR', dt('The composer executable was not found. Install composer using instructions located at https://getcomposer.org/doc/00-intro.md#globally or, if you wish to disable composer install, set "provision_composer_install_platforms" to FALSE in the ~/.drush/drushrc.php file.'));
    }
    // If drush variable allows composer install on platforms (and composer exec passed)...
    elseif (drush_get_option('provision_composer_install_platforms', TRUE)) {
      drush_log(dt('Composer bin found: @composer_bin', array(
        '@composer_bin' => $composer_bin,
      )), 'debug');

      // Detect composer.json and run composer install.
      // Step 1: Look for composer directory in git_root.
      $composer_directory = d()->git_root;
      $t['@composer_root'] = $composer_directory;

      // Step 2: Run composer install if composer.json is present, unless drush option provision_composer_install_platforms_verify_always is set to false.
      if (!drush_get_option('build') && !drush_get_option('no-composer-install')) {

        // Composer Install command: Do not interact, do not show download progress.
        // Customizable by setting drush option 'provision_composer_install_command'
        // @TODO: Change to provision platform property "build_command".
//        $composer_command = drush_get_option('provision_composer_install_command', 'composer install --no-interaction --no-progress --no-dev --no-suggest --ansi');
        d()->runDeployStep('build');
        // provision_process($composer_command, $composer_directory, dt('Running composer install in @composer_root', $t), [], $log_output, dt('Composer Install failed.'), FALSE, $provision_log_type);

      }
      elseif (!$composer_directory) {
        drush_set_error('DRUSH_COMPOSER_ERROR', dt('A composer.json file was not found anywhere above @composer_root.', $t), 'warning');
      }
      elseif ($composer_directory && drush_get_option('no-composer-install', FALSE) == TRUE) {
        drush_log(dt('Composer file was found, but composer install was not run due to --no-composer-install option.'), 'ok');
      }
    }

    // Detect missing index.php. Look for composer config to reset.
    if (!file_exists(d()->root . '/index.php')) {
      if (file_exists(d()->git_root . '/composer.json')) {

        $r = d()->getComposer();
        if (!empty($r['extra']['drupal-scaffold']['locations']['web-root'])) {
          $path = realpath(d()->git_root . '/' . $r['extra']['drupal-scaffold']['locations']['web-root']);
          if ($path && file_exists( $path . '/index.php')) {
            drush_log(dt('Drupal Scaffold Web root detected: @root', [
              '@root' => $path,
            ]), $drush_log_type);

            d()->setProperty('root', $path);
            d()->root = $path;
            drush_set_option('root', $path);

            // To remove ./
            $git_docroot = $r['extra']['drupal-scaffold']['locations']['web-root'];
            d()->setProperty('git_docroot', $git_docroot);
            d()->git_docroot = $git_docroot;
            drush_set_option('git_docroot', $git_docroot);
          }
          else {
            drush_set_error(DRUSH_APPLICATION_ERROR, dt('Drupal Scaffold config was found, but no index.php was found in that location (@file). Composer install should have run before this step. Try a manual composer install and run the task again.', [
              '@file' => $path ?? d()->git_root . '/' . $r['extra']['drupal-scaffold']['locations']['web-root'],
            ]));
          }
        }
        else {
          drush_set_error(DRUSH_APPLICATION_ERROR, dt('Index.php file was not found in platform root (@root). A composer.json file was found, but Drupal Scaffold config is not present. Add to @composer:extras.drupal-scaffold.locations.web-root', [
            '@root' => d()->root,
            '@composer' => d()->git_root . '/composer.json',
          ]));
        }
      }
      else {
        drush_set_error(DRUSH_APPLICATION_ERROR, dt('Index.php not was found at @root. Composer.json file was not found at @git_root', [
          '@root' => d()->root,
          '@git_root' => d()->git_root,
        ]));
      }
    }
    else {
      drush_log(dt('Index.php was found at @path.', [
        '@path' => d()->root,
      ]), 'ok');
    }


    // Re-set the ROOT PATH for Drush.
    // In cases where we just build the platform Drush would have cleared this value.
    // See Drush commit 3b74d40b1228f022464b92243c16127eb613c2df
    drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);
    chdir(d()->root);

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

//    drush_log("what is current dir? " . getcwd(),'warning');

    drush_set_option('sites', array_keys((array) provision_drupal_find_sites()), 'drupal');
    drush_log(dt("This platform is running @short_name @version", array('@short_name' => 'drupal', '@version' => drush_drupal_version())));
    drush_set_option('packages', _scrub_object(provision_find_packages()), 'drupal');

    provision_drupal_push_site();

    // Save git status after all the operations so we can see any changes made by composer install.
    drush_set_option('git_status', d()->getStatus(), 'drupal');
//
//  }
//  elseif (d()->type === 'site') {
    // @TODO: Move to post-hosting-task.
    // drush_set_option('packages', _scrub_object(provision_drupal_system_map()), 'site');

    // Set these in addition to platform so verify site tasks get the latest data from platforms.
    drush_set_option('git_sha', d()->getSha(), 'drupal');
    drush_set_option('git_last', d()->getLast(), 'drupal');
    drush_set_option('git_status', d()->getStatus(), 'drupal');
    drush_set_option('git_refs', d()->getRemoteReferences(), 'drupal');
    drush_set_option('git_reference', d()->getBranchOrTag(), 'drupal');
    drush_set_option('git_remote', d()->getCurrentRemoteUrl(), 'drupal');
    drush_set_option('git_reset', d()->git_reset, 'drupal');
    drush_set_option('git_root', d()->git_root, 'drupal');
    drush_set_option('git_docroot', d()->git_docroot, 'drupal');

    // This is the actual drupal provisioning requirements.
    // Don't run _provision_drupal_create_directories() here because we might be in the right folder.
    if (file_exists(d()->root)) {
      chdir(d()->root);
      _provision_drupal_create_directories();
      _provision_drupal_maintain_aliases();
      
      // _provision_drupal_ensure_htaccess_update();

      // Rewrite the settings file with latest settings.
      if (!empty($_SERVER['db_host'])) {
        _provision_drupal_create_settings_file();
      }
    }

    // Requires at least the database settings to complete.

    // If this is the hostmaster site, save the ~/.drush/drushrc.php file.
    $hostmaster = provision_sitealias_get_record('@hostmaster');
    if (d()->root == $hostmaster['root'] && d()->uri == $hostmaster['uri']) {
      drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL);
      $aegir_drushrc = new Provision_Config_Drushrc_Aegir();
      $aegir_drushrc->write();

      drush_invoke_process('@none', "cache-clear", array('drush'), array(
        'root' => d()->root,
      ));
    }

    provision_drupal_push_site(drush_get_option('override_slave_authority', FALSE));
  }
}

/**
 * Implements drush_hook_provision_post_COMMAND().
 */
function drush_provision_drupal_post_provision_verify() {
  if (d()->type === 'site') {
    // @TODO: Move to post-hosting-task.
//    _provision_drupal_rebuild_caches();
    drush_set_option('installed', TRUE, 'site');

    _provision_client_create_symlink();
    // @TODO: Move to post-hosting-task.
//    drush_include_engine('drupal', 'cron_key');

    # Cleanup useless `aegir.services.yml` files.
    # See: https://www.drupal.org/project/provision/issues/3066538
    $path = d()->site_path . '/aegir.services.yml';
    if (provision_file()->exists($path)->status()) {
      provision_file()->unlink($path)
        ->succeed('Cleaned up unused Aegir services.yml file.')
        ->fail('Failed to clean up unused Aegir services.yml file.', 'PROVISION_DELETE_AEGIR_SERVICES_YML_FAILED');
    }
  }

  if (d()->type === 'site' || d()->type === 'platform') {
    // Save options for post hosting tasks to pick up.
    drush_set_option('git_sha', d()->getSha(), 'drupal');
    drush_set_option('git_last', d()->getLast(), 'drupal');
    drush_set_option('git_status', d()->getStatus(), 'drupal');
    drush_set_option('git_refs', d()->getRemoteReferences(), 'drupal');
    drush_set_option('git_reference', d()->getBranchOrTag(), 'drupal');
    drush_set_option('git_remote', d()->getCurrentRemoteUrl(), 'drupal');
    drush_set_option('git_reset', d()->git_reset, 'drupal');
    drush_set_option('git_root', d()->git_root, 'drupal');
    drush_set_option('git_docroot', d()->git_docroot, 'drupal');
  }

}
