<?php

/**
 * @file
 *   Provision import command (Backdrop layer).
 *
 * Import an existing Backdrop site into Ægir. Fork of
 * platform/import.provision.inc collapsed to the single Backdrop path (the
 * Drupal layer has no import_1.inc / import.inc, so import hard-fails there --
 * this engine supplies the missing body).
 */

/**
 * Import the information about the existing site, and return it in the $data context array.
 */
function drush_provision_backdrop_provision_import() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type === 'site') {
    // Backdrop is major 1 (< 12); Aegir Drush 8 FULL-bootstraps it via BackdropBoot.
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL);
    drush_include_engine('backdrop', 'import');
    drush_set_option('installed', TRUE, 'site');
  }
}

function drush_provision_backdrop_post_provision_import() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type === 'site') {
    // Backdrop installs from a profile; default to 'standard' when the import
    // engine could not read the active profile.
    d()->profile = drush_set_option('profile', drush_get_option('profile', 'standard', 'site'));
    d()->language = drush_set_option('language', drush_get_option('language', 'en', 'site'));
    d()->aliases = drush_set_option('aliases', provision_drupal_find_aliases());
    d()->write_alias();

    // set this in the site options too so it recurrs.
    drush_set_option('aliases', drush_get_option('aliases'),  'site');

    // generate the drushrc
    provision_save_site_data();

    // Do not automatically save the drushrc at the end of the command.
    drush_set_option('provision_save_config', false);

    // We still run this extra backend sub-task to properly register packages etc.
    provision_backend_invoke(d()->name, 'provision-verify');

    $aegir_root = d('@server_master')->aegir_root;
    $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

    if (!is_file($enable_fast_track)) {
      sleep(5); // A small trick to avoid high load and race conditions.
      // We run this extra verify via frontend to avoid issue #1004526.
      provision_backend_invoke('@hostmaster', 'hosting-task', array(d()->name, 'verify'), array('force' => TRUE));
    }

    drush_include_engine('backdrop', 'cron_key');

    provision_reload_config('site');
  }
}
