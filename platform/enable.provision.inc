<?php

/**
 * Provision enable command
 *
 * Enable a site that has been disabled
 */
function drush_provision_drupal_provision_enable_validate() {
  if (d()->type == 'site') {
    d()->site_enabled = true;
    provision_lock_drush_local_vnd();
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
  }
}

function drush_provision_drupal_post_provision_enable() {
  if (d()->type == 'site') {
    d()->write_alias();
    // Unlock site-local Drush if locked
    //$drush_lock_ctrl = d()->root . '/local_drush_locked.pid';
    //$drush_unlock_ctrl = d()->root . '/local_drush_unlocked.pid';
    //if (!provision_file()->exists($drush_unlock_ctrl)->status() || provision_file()->exists($drush_lock_ctrl)->status()) {
    //  drush_log(dt('LOCAL/DRUSH/UNLOCK with provision_unlock_some_vnd'), 'info');
    //  provision_unlock_some_vnd();
    //}
  }
}
