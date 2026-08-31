<?php
/**
 * @file
 * Package reporting for Grav platforms and sites.
 *
 * Grav has no .info modules, no themes-as-packages, no install profiles; the
 * frontend contract still needs a well-formed structure:
 *  - packages['base'] with the core platform row and ONE synthetic profile,
 *  - packages['sites-all'] ALWAYS present (hosting_platform.drush.inc
 *    dereferences it unguarded -- absent is fatal, empty is fine),
 *  - a distinct profile short_name ('grav', never 'standard'): the profile is
 *    the partition key that keeps Grav platforms out of the Drupal platform
 *    list on the site-create form (and the string the GRV distro table must
 *    match when the BOA build channel lands).
 */

/**
 * Build the packages structure for a Grav platform (and, unchanged, the
 * site-level map -- Grav has nothing site-enabled to report beyond the core).
 */
function _provision_grav_packages() {
  $version = _provision_grav_get_version();

  $profile = new stdClass();
  $profile->name = 'grav';
  $profile->short_name = 'grav';
  $profile->filename = d()->root . '/system/defines.php';
  $profile->project = 'grav';
  $profile->version = $version;
  $profile->info = array(
    'name' => 'Grav',
    'description' => 'Grav 2 flat-file CMS (site capsules)',
    'version' => $version,
    'distribution_name' => 'Grav',
  );

  $packages = array();
  $packages['base'] = array();
  $packages['base']['platforms'] = array(
    'grav' => array(
      'short_name' => 'grav',
      'version' => $version,
      'description' => dt('This platform is running @short_name @version',
        array('@short_name' => 'Grav', '@version' => $version)),
    ),
  );
  $packages['base']['profiles']['grav'] = $profile;
  $packages['profiles']['grav'] = array();
  $packages['sites-all'] = array();

  drush_set_option('profiles', array('grav'), 'drupal');

  return $packages;
}
