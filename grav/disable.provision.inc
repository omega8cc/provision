<?php

/**
 * Provision disable command (Grav layer).
 *
 * Disable an enabled and installed site. Fork of
 * backdrop/disable.provision.inc: taking the site offline is CMS-agnostic and
 * handled by the http service -- with site_enabled = false,
 * Provision_Config_Http_Site renders the disabled vhost
 * (vhost_disabled.tpl.php) on the next config write, so the site is
 * unreachable at the nginx layer. Grav has no in-CMS state to toggle (no DB;
 * the maintenance-page story inside Grav is Q11, a later wave).
 */

/**
 * Flip the enabled property before the backup/config machinery runs.
 */
function drush_provision_grav_pre_provision_disable($backup_file = null) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type == 'site') {
    d()->site_enabled = false;
  }
}

/**
 * Persist the enabled property of the site.
 */
function drush_provision_grav_post_provision_disable() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type == 'site') {
    d()->write_alias();
  }
}
