<?php

/**
 * @file
 * Provision import command (Grav layer).
 *
 * Import an existing Grav capsule into Ægir. Fork of
 * backdrop/import.provision.inc minus the FULL bootstrap (no CMS code ever
 * runs under drush on a Grav root): everything import needs is codebase
 * facts. Reachable from the platform-verify auto-import loop and from
 * hosting_site; without this body the version-less Drupal engine hard-fails
 * with DRUSH_INCLUDE_NO_PATH.
 */

/**
 * Import the information about the existing capsule into the context.
 */
function drush_provision_grav_provision_import() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type === 'site') {
    if (!_provision_grav_site_exists()) {
      return drush_set_error('PROVISION_GRAV_SITE_NOT_FOUND',
        dt('No Grav capsule found at !site (no system/defines.php).',
        array('!site' => d()->site_path)));
    }
    drush_log(dt('Importing Grav capsule !site (@version).',
      array('!site' => d()->site_path, '@version' => _provision_grav_get_version(d()->site_path))), 'ok');
    drush_set_option('installed', TRUE, 'site');
  }
}

function drush_provision_grav_post_provision_import() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type === 'site') {
    // The synthetic profile is the platform partition key.
    d()->profile = drush_set_option('profile', drush_get_option('profile', 'grav', 'site'));
    d()->language = drush_set_option('language', drush_get_option('language', 'en', 'site'));
    d()->write_alias();

    // Generate the drushrc.
    provision_save_site_data();

    // Do not automatically save the drushrc at the end of the command.
    drush_set_option('provision_save_config', false);

    // Register packages etc. through a real verify.
    provision_backend_invoke(d()->name, 'provision-verify');

    provision_reload_config('site');
  }
}
