<?php
/**
 * @file
 *   Provision hooks for the provision install command.
 */

/**
 * Provision install command
 *
 * These are the hooks that will be executed by the drush_invoke function
 * when doing a provision_install.
 */

/**
 * Check that we are trying to install a new site , and a new site only
 */
function drush_provision_drupal_provision_install_validate() {

  if (!d()->uri) {
    return drush_set_error("PROVISION_URL_REQUIRED", dt("You need to specify a valid url to install a site"));
  }
  if (_provision_drupal_site_exists()) {

    // If "force-reinstall" option is set, delete the database and files.
    if (drush_get_option('force-reinstall', FALSE)) {

      drush_log(dt('Forcing reinstall...'), 'ok');

      // Drop the database if we can figure out what it is.
      // If last install failed, the database and the site folder is deleted.
      if (file_exists(d()->site_path . '/drushrc.php')) {

        // Load the current database name from drushrc.php.
        // I cannot find another way to find the current db_name!
        require_once(d()->site_path . '/drushrc.php');
        $old_db_name = $options['db_name'];

        if (d()->service('db')->database_exists($old_db_name)) {
          d()->service('db')->drop_database($old_db_name);
          drush_log(dt('Dropped database @database.', array(
            '@database' => $old_db_name,
          )), 'ok');
        }
      }

      // Destroy site_path.
      if (file_exists(d()->site_path)) {
        provision_file()->chmod(d()->site_path, 0755, TRUE);
        _provision_recursive_delete( d()->site_path );
        drush_log(dt('Deleted @site_path.', array('@site_path' => d()->site_path)), 'ok');
      }
    }

    // Check again if site does not exist after the forced reinstall.
    if (_provision_drupal_site_exists()) {
      return drush_set_error('PROVISION_SITE_INSTALLED',  dt('The file !file still exists. Provision was unable to delete it.', array(
        '!file' => d()->site_path . '/settings.php',
      )));
    }
  }
}

/**
 * Set up the directories and settings.php file that we need.
 */
function drush_provision_drupal_pre_provision_install() {
  // This is the actual drupal provisioning requirements.
  _provision_drupal_create_directories();
}

/**
 * If the install went south, and the site is not PROVISION_SITE_INSTALLED, clean up behind ourselves
 */
function drush_provision_drupal_pre_provision_install_rollback() {
  provision_file()->chmod(d()->site_path, 0755);
  _provision_recursive_delete( d()->site_path );
  drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
  foreach (d()->drush_aliases as $alias) {
    drush_invoke_process('@none', 'provision-save', array($alias), array('delete' => TRUE));
  }
}


/**
 * Custom helper function findDrupalAppRoot
 */
function findDrupalAppRoot($startDir) {
  $dir = realpath($startDir);
  while ($dir !== '/' && !is_dir($dir . '/vendor')) {
    $dir = dirname($dir);
  }
  return is_dir($dir . '/vendor') ? $dir : null;
}


/**
 * Install Drupal with the pre-configured settings, by calling an external script
 *
 * This is an external script so that php is running in it's own namespace, and
 * weird problems such as the multiple database connections don't confuse drupal.
 */
function drush_provision_drupal_provision_install() {
  // Requires at least the database settings to complete.

  // Determine if codebase locking/unlocking and patching is allowed (default)
  $no_codebase_lock_unlock = '/data/conf/_no_codebase_lock_unlock.ctrl';
  if (is_readable($no_codebase_lock_unlock)) {
    $lock_unlock = FALSE;
  }
  else {
    $lock_unlock = TRUE;
  }

  if ($lock_unlock) {
    if (drush_drupal_major_version() > 7 && drush_drupal_major_version() < 11) {
      // Lock Site-Local Drush if Unlocked
      provision_check_codebase_status();
      if (!d()->isConsolePatched || !d()->isCorePatched) {
        drush_log(dt('INSTALL1/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
        provision_lock_drush_local_vnd();
      }
    }
  }

  provision_prepare_environment();
  _provision_drupal_create_settings_file();
  provision_drupal_push_site($override_slave_authority = TRUE);
  provision_save_site_data();
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);

  // Install site according to install_method.
  drush_log(dt('Installing site with the "!method" method.', array(
    '!method' => d()->install_method,
  )), 'ok');

  // Run Drupal installation if option was specified
  if (d()->install_method == 'profile') {

    // call a backend task to do the actual installation.
    if (drush_drupal_major_version() > 5 && drush_drupal_major_version() < 11) {
      $result = provision_backend_invoke(d()->name, "provision-install-backend", array(), array('client_email' => drush_get_option('client_email')));
    }
    elseif (drush_drupal_major_version() > 10) {

      // Define Drush Command
      $drushCommand = 'site-install';

      // Find The App Root Directory
      $appRoot = findDrupalAppRoot(d()->root);

      // Define Local Drush Path
      $drushPath = 'vendor/drush/drush/drush.php';

      // Check If The Local Drush Exists And Is Executable
      $drushAbsolutePath = $appRoot . '/' . $drushPath;
      drush_log(dt('D11/INSTALL3/LOCAL/DRUSH/CHECK Path @path', array('@path' => $drushAbsolutePath)), 'info');

      // Open a chmod-only executable window WITHOUT reverting the Aegir
      // core/console/validator/psr-log patches. Those patches exist to make a
      // D10/D11 site loadable by Aegir's OWN Drush 8 (old psr/log v1, logger
      // signatures without ": void"); the site-local MODERN Drush used for
      // site-install does not need them reverted. Reverting them here -- the old
      // provision_unlock_drush_local_vnd() dance -- makes site-install compile the
      // D8+ service container in the stock state, baking the site-local Drush's
      // tagged logger.drupaltodrush (Drush\Log\DrushLog) into the persisted
      // (Valkey) container, which the patched web then fatals resolving (circular
      // reference on D11, "Class Drush\Log\DrushLog not found" on D10). Flipping
      // only the exec bits keeps the patches applied, so site-install compiles in
      // the same state the web serves and never poisons the container -- the
      // analog of the cr fix (_provision_drupal_rebuild_d8plus()) and the deploy
      // updatedb fix. Honour _no_codebase_lock_unlock.ctrl ($lock_unlock) and
      // preserve the at-rest 0400 posture (relock chmod-only below, only if we
      // opened the window).
      $was_executable = is_executable($drushAbsolutePath);
      if ($lock_unlock && !$was_executable) {
        drush_log(dt('D11/INSTALL2/LOCAL/DRUSH/EXEC chmod-only exec window (patches kept applied) for site-install'), 'info');
        _provision_drupal_chmod_drush_vnd_for_cr($appRoot, TRUE);
      }

      if (!is_executable($drushAbsolutePath)) {
        // Log The Useful Info
        drush_log(dt('D11/INSTALL4/LOCAL/DRUSH/CHECK 404 Not Found or is not executable at @drush_path', array('@drush_path' => $drushAbsolutePath)), 'error');
        drush_log(print_r(drush_get_error_log(), 1), 'error');
        // Relock the chmod-only window if we opened it, so a degraded/aborted
        // install never leaves the platform vendor writable (patches untouched).
        if ($lock_unlock && !$was_executable) {
          drush_log(dt('D11/INSTALL4b/LOCAL/DRUSH/RELOCK chmod-only 0400 (patches untouched)'), 'info');
          _provision_drupal_chmod_drush_vnd_for_cr($appRoot, FALSE);
        }
        return FALSE;
      }
      else {
        // Extract the DB URL from settings.php
        $settings_path = d()->root . '/sites/' . d()->uri . '/settings.php';
        $db_url = NULL;

        if (file_exists($settings_path)) {
          $contents = file_get_contents($settings_path);
          if (preg_match('/\$db_url\[\'default\'\]\s*=\s*"([^"]+)"/', $contents, $matches)) {
        	$db_url = $matches[1];
          } else {
        	drush_log("D11/INSTALL/ERR Could not find db_url in $settings_path", 'error');
        	if ($lock_unlock && !$was_executable) {
        	  _provision_drupal_chmod_drush_vnd_for_cr($appRoot, FALSE);
        	}
        	return FALSE;
          }
        } else {
          drush_log("D11/INSTALL/ERR settings.php not found at $settings_path", 'error');
          // Relock the chmod-only window if we opened it, so this early abort
          // never leaves the platform vendor writable (patches untouched).
          if ($lock_unlock && !$was_executable) {
            _provision_drupal_chmod_drush_vnd_for_cr($appRoot, FALSE);
          }
          return FALSE;
        }

        $options = drush_get_merged_options();
        $command = [
          $drushPath,
          $drushCommand,
          d()->profile,
          '--sites-subdir=' . d()->uri,
          '--site-name=' . $options['#name'],
        ];
        if (!empty($options['client_email'])) {
          $command[] = '--account-mail=' . $options['client_email'];
        }
        $command[] = '--db-url=' . $db_url;
        $command[] = '--yes';
        $command[] = '-vvv';

        // Display The Drush site-install Command
        drush_log(dt('D11/INSTALL/LOCAL5/DRUSH/CMD @cmd in @appRoot', array('@cmd' => implode(' ', $command), '@appRoot' => $appRoot)), 'info');

        // Validate the command items
        foreach ($command as $i => $arg) {
          if (!is_scalar($arg)) {
            drush_log("D11/INSTALL Invalid command part at index $i: " . print_r($arg, TRUE), 'error');
            // Relock the chmod-only window if we opened it (patches untouched).
            if ($lock_unlock && !$was_executable) {
              _provision_drupal_chmod_drush_vnd_for_cr($appRoot, FALSE);
            }
            return FALSE;
          }
        }

        // Call The Drush site-install Command with Site-Local Drush
        $cmd_string = implode(' ', array_map('escapeshellarg', $command));
        $result = drush_shell_cd_and_exec($appRoot, $cmd_string);
        // drush_shell_cd_and_exec() returns TRUE on exit 0; drive the per-line log
        // status from it (mirrors the deploy updatedb fix: $success was read below
        // but never set, so every output line was logged 'notice', never 'success').
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

        // Generate the one-time admin login link with the site-local modern
        // Drush, inside the still-open chmod-only exec window. On the D6-D10
        // path login_link comes from the provision-install-backend context
        // array (set after this block); on D11+ $result is the
        // drush_shell_cd_and_exec() bool, so we must derive the link here via
        // "user:login" -- indexing the bool below would store null and emit a
        // PHP 8 "array offset on value of type bool" warning. Bootstrap the
        // freshly installed site with an explicit absolute --root (d()->root is
        // the docroot: it carries sites/<uri>/settings.php, checked above) plus
        // --uri. No -vvv: the command prints just the URL, which we parse (the
        // line containing "/user/reset/"). Non-fatal -- if no link is derived,
        // login_link is left unset, which the front end already tolerates (it
        // guards on a truthy login_link).
        if ($success) {
          $uli_command = array(
            $drushPath,
            'user:login',
            '--root=' . d()->root,
            '--uri=' . d()->uri,
          );
          $uli_cmd_string = implode(' ', array_map('escapeshellarg', $uli_command));
          drush_log(dt('D11/INSTALL/LOCAL/DRUSH/ULI generate one-time login link via user:login in @appRoot', array('@appRoot' => $appRoot)), 'info');
          $uli_result = drush_shell_cd_and_exec($appRoot, $uli_cmd_string);
          $uli_output = drush_shell_exec_output();
          $login_link = '';
          foreach ((array) $uli_output as $uli_line) {
            $uli_line = trim((string) $uli_line);
            if ($uli_line !== '' && strpos($uli_line, '/user/reset/') !== FALSE) {
              $login_link = $uli_line;
              break;
            }
          }
          if ($login_link !== '') {
            drush_set_option('login_link', $login_link);
            drush_log(dt('D11/INSTALL/LOCAL/DRUSH/ULI login link: !onetime', array('!onetime' => $login_link)), 'success');
          }
          else {
            drush_log(dt('D11/INSTALL/LOCAL/DRUSH/ULI could not derive a one-time login link from user:login output'), 'warning');
            foreach ((array) $uli_output as $uli_line) {
              drush_log($uli_line, 'notice');
            }
          }
        }

        // Enforce Aegir policy: never allow web UI tools to overwrite the codebase.
        // Drupal core Automatic Updates emits Status report errors on symlinked/linked codebases by design,
        // so we remove the module right after install if it is enabled.
        $enabled_modules = [];
        $pml_command = [
          $drushPath,
          '-r web pm:list',
          '--type=module',
          '--status=enabled',
          '--format=list',
          '--uri=' . d()->uri,
        ];
        $pml_command[] = '-vvv';
        $pml_cmd_string = implode(' ', array_map('escapeshellarg', $pml_command));
        $pml_result = drush_shell_cd_and_exec($appRoot, $pml_cmd_string);
        $pml_output = drush_shell_exec_output();
        foreach ((array) $pml_output as $pml_line) {
          $pml_line = trim((string) $pml_line);
          if ($pml_line !== '') {
            $enabled_modules[$pml_line] = TRUE;
          }
        }

        if (!empty($enabled_modules['automatic_updates'])) {
          $uninstall_command = [
            $drushPath,
            'pm:uninstall',
            'automatic_updates',
            '--yes',
            '--no-interaction',
            '--uri=' . d()->uri,
          ];
          $uninstall_command[] = '-vvv';
          $uninstall_cmd_string = implode(' ', array_map('escapeshellarg', $uninstall_command));
          drush_log(dt('D11/INSTALL/LOCAL6/DRUSH/UNINSTALL automatic_updates in @appRoot', array('@appRoot' => $appRoot)), 'info');
          $uninstall_result = drush_shell_cd_and_exec($appRoot, $uninstall_cmd_string);
          $uninstall_output = drush_shell_exec_output();
          foreach ((array) $uninstall_output as $uninstall_line) {
            drush_log($uninstall_line, 'notice');
          }
        }

        if (!empty($enabled_modules['package_manager'])) {
          $uninstall_command = [
            $drushPath,
            'pm:uninstall',
            'package_manager',
            '--yes',
            '--no-interaction',
            '--uri=' . d()->uri,
          ];
          $uninstall_command[] = '-vvv';
          $uninstall_cmd_string = implode(' ', array_map('escapeshellarg', $uninstall_command));
          drush_log(dt('D11/INSTALL/LOCAL7/DRUSH/UNINSTALL package_manager in @appRoot', array('@appRoot' => $appRoot)), 'info');
          $uninstall_result = drush_shell_cd_and_exec($appRoot, $uninstall_cmd_string);
          $uninstall_output = drush_shell_exec_output();
          foreach ((array) $uninstall_output as $uninstall_line) {
            drush_log($uninstall_line, 'notice');
          }
        }
      }

      // Relock the chmod-only window if we opened it, leaving the patches
      // untouched (they were never reverted). Restores the at-rest 0400 posture.
      // The post-install hook's _provision_drupal_rebuild_d8plus() then rebuilds
      // the container patched (its own chmod-only window), and D<12 also gets the
      // Aegir Drush 8 FULL bootstrap tail in the caller.
      if ($lock_unlock && !$was_executable) {
        drush_log(dt('D11/INSTALL/LOCAL8/DRUSH/RELOCK chmod-only 0400 (patches untouched)'), 'info');
        _provision_drupal_chmod_drush_vnd_for_cr($appRoot, FALSE);
      }
    }

    // Pass the login link back to the front end. Only the D6-D10 path sets
    // $result to a provision-install-backend context array; on D11+ $result is
    // the drush_shell_cd_and_exec() bool and login_link was already set from the
    // site-local Drush user:login above. Indexing the bool here would store null
    // and emit a PHP 8 "array offset on value of type bool" warning.
    if (drush_drupal_major_version() > 5 && drush_drupal_major_version() < 11) {
      drush_set_option('login_link', $result['context']['login_link']);
    }

    if (drush_drupal_major_version() < 12) {
      drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL);
    }
    else {
      drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_CONFIGURATION);
      drush_log("Skipping FULL bootstrap for drupal 12+ in drush_provision_drupal_provision_install");
    }

    drush_set_option('profile_installed', TRUE, 'site');
  }
  // If install_method is 'manual', do nothing.
  elseif (d()->install_method == 'manual') {
    drush_set_option('login_link', 'http://' . d()->uri . '/install.php');
  }

  drush_set_option('installed', TRUE, 'site');
  _provision_drupal_maintain_aliases();

}

/**
 * Finish the installation, regenerate the caches on the site so that
 * any changes to things such as available modules/ themes can take affect.
 */
function drush_provision_drupal_post_provision_install() {

  // If Drupal profile was installed, rebuild drupal caches and save enabled packages for aegir.
  if (d()->install_method == 'profile' && drush_get_option('profile_installed', FALSE, 'site')) {
    if (drush_drupal_major_version() >= 10) {
      // D10+: working container + cache rebuild via the site-local Drush; the
      // in-process _provision_drupal_rebuild_caches() no-ops on D10+ (it needs an
      // Aegir Drush 8 FULL bootstrap the site cannot satisfy). The helper
      // self-manages the vendor/drush lock window. Non-fatal.
      _provision_drupal_rebuild_d8plus();
    }
    elseif (drush_drupal_major_version() < 12) {
      _provision_drupal_rebuild_caches();
    }
    drush_set_option('packages', _scrub_object(provision_drupal_system_map()), 'site');
    _provision_client_create_symlink();
    drush_include_engine('drupal', 'cron_key');
  }
  // For all other install methods, just create the symlink.
  else {
    _provision_client_create_symlink();
  }

  // Native files symlinking for the freshly installed site: move its
  // files/private dirs into the per-account static store and symlink them back,
  // via the root autosymlink tool (the store is root-managed, so Provision -- as
  // the unprivileged account user -- must delegate). The nginx vhost is not
  // written until the post-install verify, so pass the account explicitly
  // (autosymlink trusts the Drush alias). Warn-not-fail.
  _provision_drupal_native_symlink(d()->uri, FALSE, basename(d('@server_master')->aegir_root));
}
