<?php

/**
 * Provision enable command (Grav layer).
 *
 * Enable a site that has been disabled. Fork of backdrop/enable.provision.inc;
 * re-enabling is CMS-agnostic: with site_enabled = true the http service
 * renders the live vhost again on the enable flow's config write.
 */
function drush_provision_grav_provision_enable_validate() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type == 'site') {
    d()->site_enabled = true;
  }
}

function drush_provision_grav_post_provision_enable() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type == 'site') {
    d()->write_alias();
  }
}
