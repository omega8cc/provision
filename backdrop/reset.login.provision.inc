<?php
/**
 * @file
 *   Provision hooks for the provision login_reset command (Backdrop layer).
 *
 * Fork of platform/reset.login.provision.inc. CMS-agnostic: spawns a
 * `drush @site user-login` subprocess, which the backdrop-drush-extension's
 * command_alter maps to backdrop-user-login under BackdropBoot.
 */

/**
 * Generate a one-time login reset url.
 */
function drush_provision_backdrop_provision_login_reset() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  $link = provision_generate_login_reset();
  if ($link === '') {
    return drush_set_error('PROVISION_LOGIN_RESET_FAILED', dt('Drush user-login returned no one-time login link; the site page offers none.'));
  }
  drush_set_option('login_link', $link);
  drush_log(provision_login_link_message(), 'success');
}
