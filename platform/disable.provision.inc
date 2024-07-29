<?php

/**
 * Provision disable command
 *
 * Disable an enabled and installed site
 */

/**
 * Make a backup of the site before we mess with it
 */
function drush_provision_drupal_pre_provision_disable($backup_file = null) {
  if (d()->type == 'site') {
    d()->site_enabled = false;
  }
}

/**
 * Set the enabled property of the site to false
 */
function drush_provision_drupal_post_provision_disable() {
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

