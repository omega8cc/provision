<?php
/**
 * @file
 * Provision hooks for the provision enable command (TXP layer).
 *
 * Twin of txp/disable.provision.inc -- see its header for why these bodies
 * must exist rather than inheriting the union-guarded shared ones. The
 * Drupal layer's site-local-Drush locking and DRUPAL_SITE bootstrap are
 * deliberately absent: TXP has neither.
 */

function drush_provision_txp_provision_enable_validate() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type === 'site') {
    d()->site_enabled = TRUE;
    // The http enable hook renders the vhost from the 'site' context, which
    // carries no db credentials here; config.php reads them from that vhost,
    // so reload them first or the site answers with the 'none' placeholders.
    _provision_txp_ensure_db_creds_loaded();
  }
}

function drush_provision_txp_post_provision_enable() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type === 'site') {
    d()->write_alias();
  }
}
