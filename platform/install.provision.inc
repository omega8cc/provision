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
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }

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
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  // Purge any cache namespace left in the shared Valkey/Redis store by a
  // previously deleted site with the same URI, BEFORE the installer runs any
  // site code: an observed delete+recreate cycle inherited ~1700 stale keys
  // (the deleted site's compiled container and routing among them), and the
  // fresh install then rebuilt its router table FROM the stale container --
  // cache poison propagated into durable site state. The Delete task purges
  // too; this is the defensive half covering sites deleted before that fix
  // ran, or keys written in between. Non-fatal: an unreachable cache store
  // must not fail an install, and the common case has nothing to purge.
  _provision_valkey_purge_site(d()->uri);
  // This is the actual drupal provisioning requirements.
  _provision_drupal_create_directories();
}

/**
 * If the install went south, and the site is not PROVISION_SITE_INSTALLED, clean up behind ourselves
 */
function drush_provision_drupal_pre_provision_install_rollback() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
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
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
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

      // The site-local MODERN Drush cannot run site-install against the Aegir
      // patched codebase: the D10/D11 core/console/validator/psr-log patches
      // downgrade the site to Aegir's OWN Drush 8 world (old psr/log v1, logger
      // signatures with the ": void" return types stripped) so Aegir Drush 8 and
      // the patched web can load it -- but a fresh MODERN Drupal 11 installer
      // bootstrap fatals against that de-typed psr/log/Console. The fatal lands on
      // stderr, which drush_shell_exec does not capture, so the site-install
      // subprocess exits non-zero with no captured output (the observed break).
      // So revert the patches to modern stock for the install (this also chmods
      // the site-local Drush executable), then re-apply them at the end. The
      // post-install hook's _provision_drupal_rebuild_d8plus() rebuilds the D8+
      // container in the re-patched state as the last write, so the stock
      // install-time container never reaches the web. This is why the deploy
      // updatedb / cr chmod-only window (which operate on an already-installed
      // site) is NOT a valid precedent for a fresh install. Honour
      // _no_codebase_lock_unlock.ctrl ($lock_unlock).
      if ($lock_unlock) {
        drush_log(dt('D11/INSTALL2/LOCAL/DRUSH/UNLOCK with provision_unlock_drush_local_vnd'), 'info');
        provision_unlock_drush_local_vnd();
        sleep(5);
      }

      // Define Drush Command
      $drushCommand = 'site-install';

      // Find The App Root Directory
      $appRoot = findDrupalAppRoot(d()->root);

      // Define Local Drush Path
      $drushPath = 'vendor/drush/drush/drush.php';

      // Check If The Local Drush Exists And Is Executable
      $drushAbsolutePath = $appRoot . '/' . $drushPath;
      drush_log(dt('D11/INSTALL3/LOCAL/DRUSH/CHECK Path @path', array('@path' => $drushAbsolutePath)), 'info');

      if (!is_executable($drushAbsolutePath)) {
        // Log The Useful Info
        drush_log(dt('D11/INSTALL4/LOCAL/DRUSH/CHECK 404 Not Found or is not executable at @drush_path', array('@drush_path' => $drushAbsolutePath)), 'error');
        drush_log(print_r(drush_get_error_log(), 1), 'error');
        // Re-apply the Aegir patches (restore the at-rest posture) so a
        // degraded/aborted install never leaves the shared platform vendor in the
        // reverted/unlocked state the patched web cannot serve.
        if ($lock_unlock) {
          drush_log(dt('D11/INSTALL4b/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
          provision_lock_drush_local_vnd();
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
        	// Cloaked settings.php carries no literal db_url -- it reads the
        	// credentials from $_SERVER, which the vhost supplies at web request
        	// time only. Build the URL from the site context instead: the same
        	// source provision_prepare_environment() primes $_SERVER from, so it
        	// is correct whether or not credentials are cloaked.
        	$db_url = _provision_install_db_url_from_context();
        	if (empty($db_url)) {
        	  drush_log("D11/INSTALL/ERR Could not find db_url in $settings_path", 'error');
        	  if ($lock_unlock) {
        	    provision_lock_drush_local_vnd();
        	  }
        	  return FALSE;
        	}
          }
        } else {
          drush_log("D11/INSTALL/ERR settings.php not found at $settings_path", 'error');
          // Re-apply the Aegir patches so this early abort never leaves the shared
          // platform vendor in the reverted/unlocked state.
          if ($lock_unlock) {
            provision_lock_drush_local_vnd();
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
            // Re-apply the Aegir patches so this early abort restores the at-rest posture.
            if ($lock_unlock) {
              provision_lock_drush_local_vnd();
            }
            return FALSE;
          }
        }

        // Resolve what the site-local Drush will run under: its
        // "#!/usr/bin/env php" shebang finds php on PATH, so the same lookup
        // reports the memory_limit the child inherits. BOA sizes the account
        // CLI limit for the box, not for a Drupal 11 distro install, which is
        // a single job that can need far more than a pool worker (a Varbase
        // install peaks near 600 MB; a 4 GB guest with a 2 GB reserve derived
        // 466M and died after its last "module installed" line). Floor the
        // child at a quarter of installed RAM, capped at 1024M, unless
        // <aegir_root>/static/control/install-memory.info pins a value in MB.
        $mem = _provision_d11_install_memory($appRoot);
        // The child's PHP errors go to a file this task controls: the account
        // php.ini points error_log at a root-owned path the account cannot
        // write, and on this backend leg the stderr fallback never reached the
        // task log (the observed break: a non-zero exit with no fatal text).
        // The tail of that file is logged on failure; it is removed on success.
        $php_errlog = _provision_d11_install_errlog_path();
        @unlink($php_errlog);
        // Belt and braces: the application can turn log_errors off or move
        // error_log at runtime (observed: the fatal never reached the file
        // while Drush's own "terminated abnormally" shutdown line did), so a
        // prepended shutdown hook records the last fatal into the same file
        // and to stderr regardless of what the runtime did to the ini values.
        $php_hook = _provision_d11_install_hook_write($php_errlog);
        $php_prefix = array('php', '-d', 'log_errors=1', '-d', 'error_log=' . $php_errlog);
        if ($php_hook !== FALSE) {
          $php_prefix[] = '-d';
          $php_prefix[] = 'auto_prepend_file=' . $php_hook;
        }
        if ($mem['child_mb'] > 0) {
          $php_prefix[] = '-d';
          $php_prefix[] = 'memory_limit=' . $mem['child_mb'] . 'M';
        }
        drush_log(dt('D11/INSTALL/MEM php cli memory_limit @cur; site-install child runs with @child (@why; installed RAM @ram MB)', array(
          '@cur' => $mem['current'],
          '@child' => ($mem['child_mb'] > 0) ? $mem['child_mb'] . 'M' : $mem['current'],
          '@why' => $mem['why'],
          '@ram' => $mem['ram_mb'],
        )), 'info');
        $command = array_merge($php_prefix, $command);

        // Call The Drush site-install Command with Site-Local Drush, keeping
        // the exit status: drush_shell_cd_and_exec() returns only a bool, and
        // the status is the one fact a silent death leaves behind (255 = PHP
        // fatal, 137 = SIGKILL, 143 = SIGTERM).
        $cmd_string = implode(' ', array_map('escapeshellarg', $command));
        $output = array();
        $rc = 0;
        exec('cd ' . escapeshellarg($appRoot) . ' && ' . $cmd_string . ' 2>&1', $output, $rc);
        $result = ($rc === 0);
        // Drive the per-line log status from the exit status (mirrors the
        // deploy updatedb fix: $success was read below but never set, so every
        // output line was logged 'notice', never 'success').
        $success = $result;
        foreach ($output as $index => $line) {
          if (!$success) { // Log the entire error as a notice.
            $line_status = 'notice';
          }
          else { // Only log the last line as success.
            $line_status = $index+1 == count($output) ? 'success' : 'notice';
          }
          drush_log($line, $line_status);
        }

        // Detect a failed site-install. Without this the D11+ path reported a
        // broken install as success (Aegir then marked the site installed and
        // skipped rollback). The subprocess output was already logged as
        // notices above; add the exit status and the PHP error log tail, then
        // re-apply the Aegir patches (restore the at-rest posture the patched
        // web serves in), register the canonical install error and abort
        // before the login-link derivation and the pm:list cleanup run against
        // a broken site.
        if (!$result) {
          drush_log(dt('D11/INSTALL/RC site-install exit code @rc@note', array(
            '@rc' => $rc,
            '@note' => _provision_d11_exit_note($rc, $output),
          )), 'error');
          _provision_d11_log_php_errlog($php_errlog);
          if ($php_hook !== FALSE) {
            @unlink($php_hook);
          }
          drush_log(dt('D11/INSTALL/ERR site-install failed (non-zero exit) in @appRoot', array('@appRoot' => $appRoot)), 'error');
          if ($lock_unlock) {
            drush_log(dt('D11/INSTALL/LOCK re-apply Aegir patches after failed site-install'), 'info');
            provision_lock_drush_local_vnd();
          }
          return drush_set_error('PROVISION_DRUPAL_INSTALL_FAILED', dt('Drupal site-install failed for @uri', array('@uri' => d()->uri)));
        }
        @unlink($php_errlog);
        if ($php_hook !== FALSE) {
          @unlink($php_hook);
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
          'pm:list',
          '--root=' . d()->root,
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
            '--root=' . d()->root,
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
            '--root=' . d()->root,
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

      // Re-apply the Aegir core/console/validator/psr-log patches (restore the
      // at-rest posture the patched web serves in) now that the modern site-local
      // Drush has finished the install. The post-install hook's
      // _provision_drupal_rebuild_d8plus() then rebuilds the D8+ container in this
      // re-patched state as the last container write, so the stock install-time
      // container never reaches the web; D<12 also gets the Aegir Drush 8 FULL
      // bootstrap tail in the caller.
      if ($lock_unlock) {
        provision_check_codebase_status();
        if (!d()->isConsolePatched || !d()->isCorePatched) {
          drush_log(dt('D11/INSTALL/LOCAL8/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
          provision_lock_drush_local_vnd();
        }
        sleep(5);
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
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }

  // If Drupal profile was installed, rebuild drupal caches and save enabled packages for aegir.
  if (d()->install_method == 'profile' && drush_get_option('profile_installed', FALSE, 'site')) {
    if (drush_drupal_major_version() >= 10) {
      // D10+: working container + cache rebuild via the site-local Drush; the
      // in-process _provision_drupal_rebuild_caches() must not recompile a D10+
      // container inside Aegir Drush 8 (version-mismatched; its FULL bootstrap
      // here is read-only introspection on a locked platform, f9466d37). The
      // helper self-manages the vendor/drush lock window. Non-fatal.
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

/**
 * Build a Drupal --db-url from the site context credentials.
 *
 * The D10+ installer needs a db-url to hand to the site-local drush. Reading it
 * back out of the freshly rendered settings.php only works while credentials are
 * uncloaked; the cloaked variant resolves them from $_SERVER at request time and
 * carries no literal URL. drush_get_option(..., 'site') is the source
 * provision_prepare_environment() itself primes $_SERVER from, so it holds in
 * both modes.
 *
 * @return string
 *   The db-url, or an empty string when the context carries no credentials.
 */
function _provision_install_db_url_from_context() {
  $type = drush_get_option('db_type', 'mysql', 'site');
  $user = drush_get_option('db_user', '', 'site');
  $pass = drush_get_option('db_passwd', '', 'site');
  $host = drush_get_option('db_host', 'localhost', 'site');
  $port = drush_get_option('db_port', '', 'site');
  $name = drush_get_option('db_name', '', 'site');
  if (empty($name) || empty($user)) {
    return '';
  }
  // As of Drupal 7 there is no more mysqli type.
  if ($type == 'mysqli') {
    $type = 'mysql';
  }
  $url = $type . '://' . urlencode($user) . ':' . urlencode($pass) . '@' . $host;
  if (!empty($port)) {
    $url .= ':' . $port;
  }
  return $url . '/' . $name;
}

/**
 * Installed RAM in MB from /proc/meminfo, 0 when unreadable.
 */
function _provision_d11_installed_ram_mb() {
  if (is_readable('/proc/meminfo')) {
    $meminfo = file_get_contents('/proc/meminfo');
    if (preg_match('/^MemTotal:\s+(\d+)\s*kB/m', $meminfo, $m)) {
      return (int) ($m[1] / 1024);
    }
  }
  return 0;
}

/**
 * A php.ini memory shorthand (466M, 1G, 536870912, -1) in MB.
 * Returns 0 for unlimited and -1 for an unreadable value.
 */
function _provision_d11_ini_to_mb($value) {
  $value = trim((string) $value);
  if ($value === '') {
    return -1;
  }
  if ($value === '-1') {
    return 0;
  }
  $unit = strtoupper(substr($value, -1));
  $num = (float) $value;
  if ($unit === 'G') {
    return (int) ($num * 1024);
  }
  if ($unit === 'M') {
    return (int) $num;
  }
  if ($unit === 'K') {
    return (int) ($num / 1024);
  }
  return (int) ($num / 1048576);
}

/**
 * Decide the memory_limit the D11 site-install child runs with.
 *
 * Asks the php the child's shebang resolves to for its memory_limit, then
 * applies the install floor (a quarter of installed RAM, capped at 1024M)
 * when that limit is lower. <aegir_root>/static/control/install-memory.info
 * holding a number of MB (minimum 64) pins the child's limit instead, up or
 * down. Returns current (the inherited value as text), child_mb (0 = leave
 * the inherited value), why and ram_mb.
 */
function _provision_d11_install_memory($appRoot) {
  $out = array();
  $rc = 1;
  exec('cd ' . escapeshellarg($appRoot) . ' && php -d display_errors=0 -r ' . escapeshellarg('echo ini_get("memory_limit");') . ' 2>/dev/null', $out, $rc);
  $current = ($rc === 0 && !empty($out)) ? trim(end($out)) : '';
  $current_mb = _provision_d11_ini_to_mb($current);
  $ret = array(
    'current' => ($current === '') ? 'unknown' : $current,
    'child_mb' => 0,
    'why' => 'unchanged',
    'ram_mb' => _provision_d11_installed_ram_mb(),
  );
  $aegir_root = d('@server_master')->aegir_root;
  if (!empty($aegir_root)) {
    $pin_file = $aegir_root . '/static/control/install-memory.info';
    if (is_file($pin_file)) {
      $pin = (int) trim(file_get_contents($pin_file));
      if ($pin >= 64) {
        $ret['child_mb'] = $pin;
        $ret['why'] = 'pinned by static/control/install-memory.info';
        return $ret;
      }
      drush_log(dt('D11/INSTALL/MEM ignoring static/control/install-memory.info: expected a number of MB (minimum 64)'), 'warning');
    }
  }
  if ($current_mb === 0) {
    $ret['why'] = 'unlimited in php.ini';
    return $ret;
  }
  $floor = 0;
  if ($ret['ram_mb'] > 0) {
    $floor = (int) ($ret['ram_mb'] / 4);
    if ($floor > 1024) {
      $floor = 1024;
    }
  }
  if ($floor > 0 && $current_mb < $floor) {
    $ret['child_mb'] = $floor;
    $ret['why'] = 'raised to a quarter of installed RAM, capped at 1024M';
  }
  return $ret;
}

/**
 * Where the site-install child's PHP error log goes: the account log
 * directory when writable, else the system temp directory. Deliberately
 * outside the site directory, which the rollback of a failed install removes.
 */
function _provision_d11_install_errlog_path() {
  $dir = sys_get_temp_dir();
  $aegir_root = d('@server_master')->aegir_root;
  if (!empty($aegir_root) && is_dir($aegir_root . '/log') && is_writable($aegir_root . '/log')) {
    $dir = $aegir_root . '/log';
  }
  return $dir . '/php-site-install-' . preg_replace('/[^a-zA-Z0-9._-]/', '_', d()->uri) . '.log';
}

/**
 * Write the shutdown hook the site-install child prepends: on a fatal error it
 * appends the message to the child's PHP error log and prints it to stderr,
 * so the text survives a runtime that turned log_errors off. Returns the hook
 * path, or FALSE when it could not be written (the install then runs without
 * it). The hook body runs under the platform's modern PHP; it stays 5.3-safe.
 */
function _provision_d11_install_hook_write($php_errlog) {
  $hook = preg_replace('/\.log$/', '', $php_errlog) . '.hook.php';
  $body = "<?php\n"
    . "// Written by Aegir provision for one site-install run; removed after it.\n"
    . "register_shutdown_function(function () {\n"
    . "  \$e = error_get_last();\n"
    . "  if (!\$e || !in_array(\$e['type'], array(E_ERROR, E_PARSE, E_CORE_ERROR, E_COMPILE_ERROR, E_USER_ERROR, E_RECOVERABLE_ERROR), TRUE)) {\n"
    . "    return;\n"
    . "  }\n"
    . "  \$line = 'PHP Fatal error (shutdown hook): ' . \$e['message'] . ' in ' . \$e['file'] . ' on line ' . \$e['line'];\n"
    . "  @fwrite(STDERR, \$line . \"\\n\");\n"
    . "  @file_put_contents(" . var_export($php_errlog, TRUE) . ", '[' . date('d-M-Y H:i:s') . '] ' . \$line . \"\\n\", FILE_APPEND);\n"
    . "});\n";
  @unlink($hook);
  if (@file_put_contents($hook, $body) === FALSE) {
    drush_log(dt('D11/INSTALL/MEM could not write the shutdown hook at @path, installing without it', array('@path' => $hook)), 'warning');
    return FALSE;
  }
  @chmod($hook, 0644);
  return $hook;
}

/**
 * Log the tail of the site-install child's PHP error log as task errors.
 */
function _provision_d11_log_php_errlog($path) {
  if (!is_file($path) || !is_readable($path)) {
    drush_log(dt('D11/INSTALL/PHPLOG no PHP error log was written at @path', array('@path' => $path)), 'warning');
    return;
  }
  $lines = file($path, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
  if (empty($lines)) {
    drush_log(dt('D11/INSTALL/PHPLOG the PHP error log at @path is empty', array('@path' => $path)), 'warning');
    return;
  }
  $tail = array_slice($lines, -20);
  drush_log(dt('D11/INSTALL/PHPLOG last @n of @t PHP error log lines, full file kept at @path', array(
    '@n' => count($tail),
    '@t' => count($lines),
    '@path' => $path,
  )), 'error');
  foreach ($tail as $line) {
    drush_log($line, 'error');
  }
}

/**
 * A short reading of a non-zero site-install exit status.
 */
function _provision_d11_exit_note($rc, $output = array()) {
  $rc = (int) $rc;
  if ($rc == 255) {
    return ' (PHP fatal error: see the D11/INSTALL/PHPLOG lines)';
  }
  // Modern Drush turns a fatal into its own shutdown line and exit 1.
  foreach ((array) $output as $line) {
    if (strpos($line, 'Drush command terminated abnormally') !== FALSE) {
      return ' (PHP fatal error inside Drush: see the D11/INSTALL/PHPLOG lines)';
    }
  }
  if ($rc == 137 || $rc == 9) {
    return ' (killed by SIGKILL: out-of-memory killer or a watchdog)';
  }
  if ($rc == 143 || $rc == 15) {
    return ' (terminated by SIGTERM)';
  }
  if ($rc == 124) {
    return ' (timed out)';
  }
  if ($rc == 1) {
    return ' (Drush reported the error in its output above)';
  }
  return '';
}
