<?php
/**
 * @file
 * Package reporting for Textpattern platforms and sites.
 *
 * TXP has no .info modules, no themes-as-packages, no install profiles; the
 * frontend contract still needs a well-formed structure:
 *  - packages['base'] with the core platform row and ONE synthetic profile,
 *  - packages['sites-all'] ALWAYS present (hosting_platform.drush.inc
 *    dereferences it unguarded -- absent is fatal, empty is fine),
 *  - a distinct profile short_name ('textpattern', never 'standard'): the
 *    profile is the partition key that keeps TXP platforms out of the Drupal
 *    platform list on the site-create form.
 */

/**
 * Build the packages structure for a TXP platform (and, unchanged, the
 * site-level map -- TXP has nothing site-enabled to report beyond the core).
 */
function _provision_txp_packages() {
  $version = _provision_txp_get_version();

  $profile = new stdClass();
  $profile->name = 'textpattern';
  $profile->short_name = 'textpattern';
  $profile->filename = d()->root . '/textpattern/lib/constants.php';
  $profile->project = 'textpattern';
  $profile->version = $version;
  $profile->info = array(
    'name' => 'Textpattern',
    'description' => 'Textpattern CMS (multisite)',
    'version' => $version,
    'distribution_name' => 'Textpattern',
  );

  $packages = array();
  $packages['base'] = array();
  $packages['base']['platforms'] = array(
    'textpattern' => array(
      'short_name' => 'textpattern',
      'version' => $version,
      'description' => dt('This platform is running @short_name @version',
        array('@short_name' => 'Textpattern', '@version' => $version)),
    ),
  );
  $packages['base']['profiles']['textpattern'] = $profile;
  $packages['profiles']['textpattern'] = array();
  $packages['sites-all'] = array();

  drush_set_option('profiles', array('textpattern'), 'drupal');

  return $packages;
}
