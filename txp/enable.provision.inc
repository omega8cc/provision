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
