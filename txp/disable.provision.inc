<?php
/**
 * @file
 * Provision hooks for the provision disable command (TXP layer).
 *
 * WITHOUT this file the command silently no-ops and reports success while the
 * site keeps serving: the shared Drupal hooks return early on the foreign-CMS
 * union, so `site_enabled` never flips and the vhost is never re-rendered from
 * the disabled template. Same silent-success class the migrate fail-close
 * closed; this one is a routine frontend button, so it gets a real body.
 *
 * TXP itself has no maintenance mode (only production_status), so disable is
 * purely vhost-level -- exactly like the Drupal layer, whose bodies these
 * mirror (site_enabled flip + write_alias; the vhost re-render rides the
 * shared http service).
 */

function drush_provision_txp_pre_provision_disable($backup_file = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type === 'site') {
    d()->site_enabled = FALSE;
  }
}

function drush_provision_txp_post_provision_disable() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type === 'site') {
    d()->write_alias();
  }
}
