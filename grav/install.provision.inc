<?php
/**
 * @file
 * Provision hooks for the provision install command (Grav layer).
 *
 * Fork of txp/install.provision.inc minus everything database: a phase-1 Grav
 * site is DB-less by design (boa-grav D-003 capsule model), so there is no db
 * drop on force-reinstall and no credential plumbing -- install is capsule
 * materialisation + the .env + the first admin account.
 */

/**
 * Check that we are trying to install a new site, and a new site only.
 */
function drush_provision_grav_provision_install_validate() {
  if (!provision_platform_is_grav(d())) {
    return;
  }

  if (!d()->uri) {
    return drush_set_error("PROVISION_URL_REQUIRED", dt("You need to specify a valid url to install a site"));
  }
  // D-005 enforcement: refuse the task outright when the box has no >= 8.3 PHP.
  if (_provision_grav_php_bin() === FALSE) {
    return drush_set_error('PROVISION_GRAV_PHP_TOO_OLD',
      dt('No PHP >= 8.3 binary found; Grav tasks refuse to run below the enforced floor.'));
  }
  if (_provision_grav_site_exists()) {

    if (drush_get_option('force-reinstall', FALSE)) {

      drush_log(dt('Forcing reinstall...'), 'ok');

      if (file_exists(d()->site_path)) {
        provision_file()->chmod(d()->site_path, 0755, TRUE);
        _provision_recursive_delete(d()->site_path);
        drush_log(dt('Deleted @site_path.', array('@site_path' => d()->site_path)), 'ok');
      }
    }

    if (_provision_grav_site_exists()) {
      return drush_set_error('PROVISION_SITE_INSTALLED', dt('The file !file still exists. Provision was unable to delete it.', array(
        '!file' => d()->site_path . '/system/defines.php',
      )));
    }
  }
}

/**
 * Materialise the capsule and its .env.
 */
function drush_provision_grav_pre_provision_install() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  _provision_grav_create_directories();
}

/**
 * If the install went south, clean up behind ourselves.
 */
function drush_provision_grav_pre_provision_install_rollback() {
  if (!provision_platform_is_grav(d())) {
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
 * Install the Grav site: the capsule is already materialised (pre hook); the
 * engine creates the first admin account and warms the cache through the Grav
 * CLI (the enforced-PHP seam).
 */
function drush_provision_grav_provision_install() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  provision_prepare_environment();
  provision_drupal_push_site($override_slave_authority = TRUE);
  provision_save_site_data();

  drush_log(dt('Installing site with the "!method" method.', array(
    '!method' => d()->install_method,
  )), 'ok');

  // 'profile' is what headless/CLI-created sites arrive with (the context
  // default); 'grav' is the label the hosting_grav frontend will stamp. Same
  // automated install either way.
  if (d()->install_method == 'profile' || d()->install_method == 'grav') {
    drush_include_engine('grav', 'install');
    if (drush_get_error()) {
      return FALSE;
    }
    drush_set_option('profile_installed', TRUE, 'site');
  }
  // No other method can exist for a capsule (Grav ships no installer wizard at
  // all -- an unpacked release IS a working site). Fail closed on anything
  // unknown.
  else {
    return drush_set_error('PROVISION_GRAV_INSTALL_METHOD_UNKNOWN',
      dt('Unknown install_method "!method" for !uri; refusing to mark the site installed.',
      array('!method' => d()->install_method, '!uri' => d()->uri)));
  }

  drush_set_option('installed', TRUE, 'site');
}

/**
 * Finish the installation and save the package map.
 */
function drush_provision_grav_post_provision_install() {
  if (!provision_platform_is_grav(d())) {
    return;
  }

  drush_set_option('packages', _scrub_object(provision_grav_system_map()), 'site');
  // No client symlink for this site: a Grav capsule / Textpattern site is the
  // whole codebase, so linking it under ~/clients/<client>/ would hand a
  // per-client shell sub-account full code and database access there, against
  // the files-only policy those accounts have on Drupal sites. Drop a link an
  // earlier release created; the sub-account's lshell path skips these too.
  _provision_client_delete_symlink();
}
