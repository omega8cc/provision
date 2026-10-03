<?php
/**
 * @file
 *   Provision hooks for the provision login_reset command.
 */

/**
 * Provision login-reset command
 *
 * These are the hooks that will be executed by the drush_invoke function
 * when generating a one-time login reset URL.
 */

/**
 * Lock the site-local Drush before the link is made.
 *
 * The link comes from a Drush 8 user-login process, which cannot bootstrap a
 * Drupal 10+ site on an unlocked platform; lock it first, as Enable does.
 */
function drush_provision_drupal_provision_login_reset_validate() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  if (d()->type == 'site' && drush_drupal_major_version() >= 8) {
    provision_check_codebase_status();
    if (provision_drush_local_lock_needed()) {
      drush_log(dt('LOGIN/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
      provision_lock_drush_local_vnd();
    }
  }
}

/**
 * Generate a one-time login reset url
 */
function drush_provision_drupal_provision_login_reset() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  drush_set_option('login_link', provision_generate_login_reset());
  drush_log(dt('One-time login link generated; the site page offers it for 24 hours to users allowed to reset the password.'), 'success');
}
