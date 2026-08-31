<?php
/**
 * @file
 * Provision hooks for the provision install command (TXP layer).
 *
 * Fork of backdrop/install.provision.inc: same force-reinstall/db-drop
 * scaffolding, but the install itself is the TXP install engine (a headless
 * shim over Textpattern's own setup libs in a separate >= 8.4 PHP process --
 * TXP ships no CLI and is never bootstrapped through drush).
 */

/**
 * Check that we are trying to install a new site, and a new site only.
 */
function drush_provision_txp_provision_install_validate() {
  if (!provision_platform_is_txp(d())) {
    return;
  }

  if (!d()->uri) {
    return drush_set_error("PROVISION_URL_REQUIRED", dt("You need to specify a valid url to install a site"));
  }
  // D-008 enforcement: refuse the task outright when the box has no >= 8.4 PHP.
  if (_provision_txp_php_bin() === FALSE) {
    return drush_set_error('PROVISION_TXP_PHP_TOO_OLD',
      dt('No PHP >= 8.4 binary found; TXP tasks refuse to run below the enforced floor.'));
  }
  if (_provision_txp_site_exists()) {

    if (drush_get_option('force-reinstall', FALSE)) {

      drush_log(dt('Forcing reinstall...'), 'ok');

      if (file_exists(d()->site_path . '/drushrc.php')) {
        require_once(d()->site_path . '/drushrc.php');
        $old_db_name = $options['db_name'];

        if (d()->service('db')->database_exists($old_db_name)) {
          d()->service('db')->drop_database($old_db_name);
          drush_log(dt('Dropped database @database.', array(
            '@database' => $old_db_name,
          )), 'ok');
        }
      }

      if (file_exists(d()->site_path)) {
        provision_file()->chmod(d()->site_path, 0755, TRUE);
        _provision_recursive_delete(d()->site_path);
        drush_log(dt('Deleted @site_path.', array('@site_path' => d()->site_path)), 'ok');
      }
    }

    if (_provision_txp_site_exists()) {
      return drush_set_error('PROVISION_SITE_INSTALLED', dt('The file !file still exists. Provision was unable to delete it.', array(
        '!file' => d()->site_path . '/private/config.php',
      )));
    }
  }
}

/**
 * Set up the site tree and the private/config.php we need.
 */
function drush_provision_txp_pre_provision_install() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  _provision_txp_create_directories();
}

/**
 * If the install went south, clean up behind ourselves.
 */
function drush_provision_txp_pre_provision_install_rollback() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  provision_file()->chmod(d()->site_path, 0755);
  _provision_recursive_delete(d()->site_path);
  drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
  foreach (d()->drush_aliases as $alias) {
    drush_invoke_process('@none', 'provision-save', array($alias), array('delete' => TRUE));
  }
}

/**
 * Install Textpattern with the pre-configured settings.
 *
 * The engine runs the vendored shim in its own >= 8.4 PHP process (mirrors the
 * official CLI installer, setup/setup.php, plus the two multisite constants it
 * lacks), against the config.php the settings writer has already rendered --
 * one authority for the config from birth, re-rendered on every verify.
 */
function drush_provision_txp_provision_install() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  provision_prepare_environment();
  _provision_txp_create_settings_file();
  provision_drupal_push_site($override_slave_authority = TRUE);
  provision_save_site_data();

  drush_log(dt('Installing site with the "!method" method.', array(
    '!method' => d()->install_method,
  )), 'ok');

  // 'profile' is what headless/CLI-created sites arrive with (the context
  // default); 'txp' is the label the hosting_txp frontend will stamp. Same
  // automated install either way.
  if (d()->install_method == 'profile' || d()->install_method == 'txp') {
    drush_include_engine('txp', 'install');
    if (drush_get_error()) {
      return FALSE;
    }
    drush_set_option('profile_installed', TRUE, 'site');
  }
  // 'manual' cannot work headless-safely on a shared TXP platform: the
  // browser installer writes through the web user and its multisite setup dir
  // is removed post-install. Fail closed like every unknown method.
  else {
    return drush_set_error('PROVISION_TXP_INSTALL_METHOD_UNKNOWN',
      dt('Unknown install_method "!method" for !uri; refusing to mark the site installed.',
      array('!method' => d()->install_method, '!uri' => d()->uri)));
  }

  drush_set_option('installed', TRUE, 'site');
}

/**
 * Finish the installation and save the package map.
 */
function drush_provision_txp_post_provision_install() {
  if (!provision_platform_is_txp(d())) {
    return;
  }

  drush_set_option('packages', _scrub_object(provision_txp_system_map()), 'site');
  _provision_client_create_symlink();
}
