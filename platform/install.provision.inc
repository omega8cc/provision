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
  $drush_log_type = drush_get_option('runner') == 'hosting_task'? 'p_log': 'ok';
  drush_set_option('drush_log_type', $drush_log_type);

  if (!d()->uri) {
    return drush_set_error("PROVISION_URL_REQUIRED", dt("You need to specify a valid url to install a site"));
  }
  if (_provision_drupal_site_exists()) {

    // If "force-reinstall" option is set, delete the database and files.
    if (drush_get_option('force-reinstall', FALSE)) {

      drush_log(dt('Forcing reinstall...'), drush_set_option('drush_log_type', 'ok'));

      // Drop the database if we can figure out what it is.
      // If last install failed, the database and the site folder is deleted.
      if (file_exists(d()->site_path . '/drushrc.php')) {

        // Load the current database name from drushrc.php.
        // I cannot find another way to find the current db_name!
        require_once(d()->site_path . '/drushrc.php');
        if (!empty($options['db_name']) && d()->service('db')->database_exists($options['db_name'])) {
          d()->service('db')->drop_database($options['db_name']);
          drush_log(dt('Dropped database @database.', array(
            '@database' => $options['db_name'],
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
      return drush_set_error('PROVISION_SITE_INSTALLED',  dt('The site still exists (Assumed because the file !file exists). Use the --force-reinstall option to destroy the existing site and reinstall.', array(
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
//  _provision_drupal_create_directories();
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
 * Install Drupal with the pre-configured settings, by calling an external script
 *
 * This is an external script so that php is running in it's own namespace, and
 * weird problems such as the multiple database connections don't confuse drupal.
 */
function drush_provision_drupal_provision_install() {
 // Requires at least the database settings to complete.
  // _provision_drupal_create_directories();
  provision_prepare_environment();
  _provision_drupal_create_settings_file();
  provision_drupal_push_site($override_slave_authority = TRUE);

  // Write drushrc.php
  // provision_save_site_data();

  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);

  // Install site according to install_method.
  $hook = "drush_provision_drupal_provision_install_" . d()->install_method;
  if (empty(d()->install_method)) {
    return drush_set_error(DRUSH_APPLICATION_ERROR, dt('Install Method not found. Make sure site settings.install.method is set.'));
  }
  if (!function_exists($hook)) {
    return drush_set_error(DRUSH_APPLICATION_ERROR, dt('Function for install method @method not found. Expecting @hook', [
      '@method' => d()->install_method,
      '@hook' => $hook,
    ]));
  }
  else {
    $hook();
  }

  drush_set_option('installed', TRUE, 'site');
  _provision_drupal_maintain_aliases();

}

/**
 * Implements provision-install for METHOD.
 * @return void
 */
function drush_provision_drupal_provision_install_METHOD(){
}

/**
 * Implements provision-install for METHOD.
 * @return void
 */
function drush_provision_drupal_provision_install_manual(){
  drush_set_option('login_link', 'http://' . d()->uri . '/install.php');

}

/**
 * Implements provision-install for METHOD.
 * @return void
 */
function drush_provision_drupal_provision_install_profile(){
  if (d()->name == '@hostmaster') {
    // call a backend task to do the actual installation.
    $result = provision_backend_invoke(d()->name, "provision-install-backend", array(), array('client_email' => drush_get_option('client_email')));

    // pass the login link back to the front end.
    drush_set_option('login_link', $result['context']['login_link']);
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL);
    drush_set_option('profile_installed', TRUE, 'site');
  }
  else {
    $options = drush_get_merged_options();
    $command = ['drush'];
    $command[] = d()->site_install_command;
    $command[] = d()->profile;
    $command[] = '--sites-subdir=' . d()->uri;
    $site_name = escapeshellarg($options['#name']);
    $command[] = "--site-name=$site_name";

    if ($options['client_email']) {
      $command[] = "--account-mail={$options['client_email']}";
    }
    $command[] = '--yes';
    $env = [];
    provision_process($command, d()->root, dt('Installing site...'));
    $process = drush_get_context('provision_process_result');

    provision_process([
      'chmod',
      '+w',
      "sites/" . d()->uri,
    ], d()->root, dt('Making sites dir writable...'));

    // chdir(d('@hostmaster')->root);
    // $result = provision_backend_invoke(d()->name, "provision-verify");

  }
}

/**
 * Implements provision-install for METHOD clone.
 * @return void
 */
function drush_provision_drupal_provision_install_clone(){

  $source = d()->settings['install']['clone_source'];

  // If "clone_source" is set to "_other", use the drush alias in "clone_source_drush".
  if ($source == '_other') {
    if (empty(d()->settings['install']['clone_source_drush'])) {
      return drush_set_error(DRUSH_APPLICATION_ERROR, dt('settings.install.clone_source set to _other, but no'));
    }
    $source = d()->settings['install']['clone_source_drush'];
  }

  $destination = '@' . d()->hosting_group . '.' . d()->hosting_environment;
  $extra_dump = escapeshellarg('--extra-dump="--single-transaction=false"');
  $database_command = "drush sql-sync $extra_dump $source $destination";
  $files_command = "drush rsync $source:%files $destination:%files";

  $environment = [];

  if (!drush_get_option('skip-database', FALSE)) {
    provision_process($database_command, drush_server_home(), dt('Syncing database...'), $environment, TRUE);
  }
  if (!drush_get_option('skip-files', FALSE)) {
    provision_process($files_command, drush_server_home(), dt('Syncing files...'), $environment, true);
  }
}

/**
 * Implements provision-install for METHOD.
 * @return void
 */
function drush_provision_drupal_provision_install_import(){

  // Detect database import URL/path
  $path_or_url = d()->settings['install']['import'] ?? '';
  if (empty($path_or_url)) {
    return drush_set_error(DRUSH_APPLICATION_ERROR, 'Import setting was blank. Check site settings.install.import');
  }

  // Detect file path.
  $import_files_path = d()->settings['install']['import_files'] ?? '';

  $url = parse_url($path_or_url);
  if ($url['scheme'] == 'mysql') {

    // Dump the file
    $temp_file_name = tempnam('/tmp', 'devshop_remote_db.sql.');
    $db_name = ltrim($url['path'], '/');
    $command = "mysqldump -u{$url['user']} -p{$url['pass']} -h{$url['host']} {$db_name}  --result-file={$temp_file_name}";
    provision_process($command, NULL, dt('Saving database file...'));

    $file_to_import = $temp_file_name;
  }

  // If file exists but is not readable, alert the user.
  elseif (file_exists($path_or_url) && !is_readable($path_or_url)){
    $error = dt('Unable to import SQL file !file: not readable.', array(
      '!file' => $path_or_url,
    ));
    if (drush_get_option('devshop_sql_import_fail_if_import_fails', false)) {
      return drush_set_error('DEVSHOP_IMPORT_ERROR', $error);
    }
    else {
      drush_log($error, 'warning');
      return;
    }
  }

  // If file exists and is readable, set it as the file to import.
  elseif (file_exists($path_or_url) && is_readable($path_or_url)){
    $file_to_import = $path_or_url;
  }
  elseif (!file_exists($path_or_url))  {
    $error = dt('The database file !file does not exist. Unable to import.', array(
      '!file' => $path_or_url,
    ));
    if (drush_get_option('devshop_sql_import_fail_if_import_fails', false)) {
      return drush_set_error('PROVISION_DATABASE_INSTALL', $error);
    }
    else {
      drush_log($error, 'warning');
      return;
    }
  }

  // Import the database file
  $alias = d()->drush_alias;
  $command = "drush {$alias} sqlq 'SOURCE {$file_to_import}'";
  provision_process($command, NULL, dt('Importing database file...'));
  provision_process('drush cc drush', NULL, 'Clearing drush caches');

  // Clear all caches.
  if (drush_drupal_major_version(d()->root) >= 8) {
    provision_process("drush {$alias} cache-rebuild", NULL, dt('Clear all Caches'));
  }
  else {
    provision_process("drush {$alias} cache-clear all", NULL, dt('Clear all Caches'));
  }

  provision_process("drush {$alias} sqlq 'SHOW TABLES'", NULL, dt('Show Tables'));

  // Import files snapshot, just rsync or cp over
  // @todo Make work with different sources, local tar, tgz, or http:// etc.
  // @todo Exclude non-essential files directories e.g. css, php, js etc.
  if (!empty($import_files_path)) {

    if (!file_exists($import_files_path)) {
      $error = dt('The import file specified (!file) does not exist. Unable to import.',
        array(
        '!file' => $import_files_path,
      ));
      if (drush_get_option('devshop_sql_import_fail_if_file_import_fails',
        false)) {
        return drush_set_error('PROVISION_DATABASE_INSTALL', $error);
      }
    }

    provision_process("drush --yes rsync {$import_files_path} {$alias}:%files", NULL, dt('Importing files snapshot...'));
    $files_path = d()->site_path . '/files/';
    // Recursively chmod/chown all the files.
    // Overriding this https://www.drupal.org/node/874716
    provision_process("chown --recursive :apache {$files_path}");
    provision_process("chmod --recursive 2770 {$files_path}");
    provision_process("ls -al {$files_path}");

  }
}

/**
 * Finish the installation, regenerate the caches on the site so that
 * any changes to things such as available modules/ themes can take affect.
 */
function drush_provision_drupal_post_provision_install() {

  // If Drupal profile was installed, rebuild drupal caches and save enabled packages for aegir.
  if (d()->install_method == 'profile' && drush_get_option('profile_installed', FALSE, 'site')) {
//    _provision_drupal_rebuild_caches();
//    drush_set_option('packages', _scrub_object(provision_drupal_system_map()), 'site');
    _provision_client_create_symlink();
//    drush_include_engine('drupal', 'cron_key');
  }
  // For all other install methods, just create the symlink.
  else {
    _provision_client_create_symlink();
  }
}

