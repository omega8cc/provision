<?php

/**
 * Provision disable command (Backdrop layer).
 *
 * Disable an enabled and installed site. Fork of platform/disable.provision.inc.
 * Taking the site offline is CMS-agnostic and handled by the http service:
 * with site_enabled = false, Provision_Config_Http_Site renders the disabled
 * vhost (vhost_disabled.tpl.php) on the next verify, so the site is unreachable
 * at the nginx layer. Backdrop's in-CMS maintenance_mode (state) is deliberately
 * NOT toggled here -- it would need a full bootstrap + DB write and is redundant
 * once the vhost is disabled (the Drupal fork's $conf['site_offline'] is likewise
 * only a secondary measure).
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
