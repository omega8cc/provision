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
  $uri = d()->redirection ?: d()->uri;
  $result = drush_invoke_process(d()->name, 'user-login', array(), array('uri' => $uri, 'no-browser' => TRUE));

  drush_set_option('login_link', $result['output']);
  drush_log(dt('One-time login link generated; the site page offers it for 24 hours to users allowed to reset the password.'), 'success');
}
