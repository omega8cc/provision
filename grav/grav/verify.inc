<?php
/**
 * @file
 * The Grav verify engine: bootstrap-free capsule integrity.
 *
 * No database and no CMS bootstrap -- integrity is codebase facts: the
 * autoloader present (its absence turns any CLI entry into an unpinned
 * network composer run AND means a broken restore), the front controller
 * present, and a readable core version.
 */

function _provision_grav_verify() {
  $site_path = d()->site_path;

  if (!is_readable($site_path . '/vendor/autoload.php')) {
    return drush_set_error('PROVISION_GRAV_SEED_INVALID',
      dt('Capsule !site has no vendor/autoload.php -- the site cannot boot (restore from a pre-damage backup or re-run install).',
      array('!site' => $site_path)));
  }
  if (!is_readable($site_path . '/index.php')) {
    return drush_set_error('PROVISION_GRAV_SEED_INVALID',
      dt('Capsule !site has no index.php front controller.', array('!site' => $site_path)));
  }

  $version = _provision_grav_get_version($site_path);
  if ($version === '') {
    drush_log(dt('Could not read GRAV_VERSION from !site/system/defines.php.',
      array('!site' => $site_path)), 'warning');
  }
  else {
    drush_log(dt('This site is running Grav @version.', array('@version' => $version)), 'info');
  }
  return TRUE;
}

_provision_grav_verify();
