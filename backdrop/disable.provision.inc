<?php

/**
 * Provision disable command (Backdrop layer).
 *
 * Disable an enabled and installed site. Fork of platform/disable.provision.inc;
 * the maintenance/offline state is emitted by the Backdrop settings template
 * (state maintenance_mode) from d()->site_enabled on the next settings render.
 */

/**
 * Make a backup of the site before we mess with it.
 */
function drush_provision_backdrop_pre_provision_disable($backup_file = null) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type == 'site') {
    d()->site_enabled = false;
  }
}

/**
 * Set the enabled property of the site to false.
 */
function drush_provision_backdrop_post_provision_disable() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type == 'site') {
    d()->write_alias();
  }
}
