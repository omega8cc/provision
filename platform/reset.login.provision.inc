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
 * Generate a one-time login reset url
 */
function drush_provision_drupal_provision_login_reset() {
  $uri = d()->redirection ?: d()->uri;
  $root = d()->root;
  $alias = d()->drush_alias;
  $command = "drush --uri={$uri} {$alias} user-login";
  $output = provision_process($command, d()->root, dt('Requesting one time login link'), [], TRUE);

  drush_set_option('login_link', $output);
  drush_log(dt('Login url: !onetime', array('!onetime' => $output)), 'success');
}
