<?php

/**
 * Provision enable command (Backdrop layer).
 *
 * Enable a site that has been disabled. Fork of platform/enable.provision.inc;
 * the D8+ site-local Drush lock is dropped. The maintenance/offline state is
 * emitted by the Backdrop settings template (state maintenance_mode) from
 * d()->site_enabled on the next settings render.
 */
function drush_provision_backdrop_provision_enable_validate() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type == 'site') {
    d()->site_enabled = true;
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
    // The http enable hook renders the vhost from the 'site' context, which
    // this bootstrap leaves without the db credentials: reload them first, or
    // the vhost carries the 'none' placeholders until the next verify.
    _provision_backdrop_ensure_db_creds_loaded();
  }
}

function drush_provision_backdrop_post_provision_enable() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type == 'site') {
    d()->write_alias();
  }
}
