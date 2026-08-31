<?php
/**
 * @file
 * Minimal Drush 8 bootstrap class for Grav 2 roots.
 *
 * Exists ONLY to satisfy the bootstrap-phase gate: provision task commands
 * declare DRUSH_BOOTSTRAP_DRUPAL_ROOT/SITE, and without a candidate whose
 * valid_root() accepts a Grav root, drush refuses to dispatch them at all
 * ("needs a higher bootstrap level"). Registered via
 * hook_bootstrap_candidates() from the provision_grav commandfile -- the
 * BackdropBoot/TxpBoot pattern, zero changes in the drush fork itself.
 *
 * The phases deliberately do ALMOST NOTHING: Grav is never bootstrapped
 * in-process (no CMS code runs under drush -- Grav CLI calls happen in a
 * separate enforced-PHP process); the root phase loads the platform-level
 * drushrc + pins the root context, the site phase loads the site-level
 * drushrc (sites/<uri>/drushrc.php -- the BOA option carrier). Every phase
 * above SITE is intentionally absent: a command asking for CONFIGURATION+ on
 * a Grav root fails the phase gate loudly, which is correct.
 *
 * Parse-conservative on purpose (boa-grav D-005): bootstrap candidates are
 * probed on every box the shared provision tree ships to.
 */

class Provision_GravBoot extends \Drush\Boot\BaseBoot {

  function valid_root($path) {
    if (!function_exists('provision_platform_is_grav')) {
      return FALSE;
    }
    return provision_platform_is_grav($path);
  }

  function get_version($root) {
    if (function_exists('_provision_grav_get_version')) {
      return _provision_grav_get_version($root);
    }
    return '';
  }

  function bootstrap_phases() {
    return array(
      DRUSH_BOOTSTRAP_DRUSH => '_drush_bootstrap_drush',
      DRUSH_BOOTSTRAP_DRUPAL_ROOT => 'bootstrap_grav_root',
      DRUSH_BOOTSTRAP_DRUPAL_SITE => 'bootstrap_grav_site',
    );
  }

  function bootstrap_init_phases() {
    return array(DRUSH_BOOTSTRAP_DRUSH);
  }

  /**
   * No phase contributes commandfile search paths: a Grav tree (site content,
   * web-writable dirs included) must never be scanned for drush commandfiles.
   */
  function commandfile_searchpaths($phase, $phase_max = FALSE) {
    return array();
  }

  function command_defaults() {
    return array(
      'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    );
  }

  function bootstrap_grav_root_validate() {
    $root = drush_get_context('DRUSH_SELECTED_DRUPAL_ROOT');
    if (!$root) {
      $root = drush_get_option('root');
    }
    if (!$root || !$this->valid_root($root)) {
      return drush_bootstrap_error('DRUSH_INVALID_DRUPAL_ROOT',
        dt('The path !root is not a valid Grav root.', array('!root' => (string) $root)));
    }
    drush_bootstrap_value('drupal_root', $root);
    return TRUE;
  }

  function bootstrap_grav_root() {
    // Platform-level drushrc (sites/all/drush/drushrc.php -- the CMS-agnostic
    // Drushrc config the platform verify renders).
    drush_load_config('drupal');
    $root = drush_set_context('DRUSH_DRUPAL_ROOT', drush_bootstrap_value('drupal_root'));
    chdir($root);
    if (!defined('DRUSH_DRUPAL_CORE')) {
      define('DRUSH_DRUPAL_CORE', $root);
    }
    drush_set_context('DRUSH_DRUPAL_CORE', $root);
    _drush_preflight_global_options();
  }

  function bootstrap_grav_site_validate() {
    $uri = drush_get_context('DRUSH_SELECTED_URI');
    if (!$uri) {
      $uri = drush_get_option('uri');
    }
    if ($uri) {
      drush_bootstrap_value('site', $uri);
    }
    return TRUE;
  }

  function bootstrap_grav_site() {
    // Site-level drushrc (sites/<uri>/drushrc.php -- the BOA option carrier).
    drush_load_config('site');
    $uri = drush_bootstrap_value('site');
    if ($uri) {
      drush_set_context('DRUSH_URI', $uri);
    }
  }
}
