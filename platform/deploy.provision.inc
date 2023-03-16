<?php

/**
 * @file
 * Code related to deploy tasks.
 */

/**
 * drush provision-deploy command.
 * Formerly the provision-devshop-deploy command
 *
 * Meta command that is used for multiple use cases:
 *
 * - Deploy: Update code run hooks.
 * - Deploy: Update code, reinstall, run hooks.
 * - Deploy: Update code, reinstall, run hooks, run tests.
 *
 * Steps:
 *   1. Fetch origin.
 *   2. Check out git_reference
 *   3. Reset to origin/$git_reference (--hard if drush_option git_reset)
 *   4.
 *
 */
function drush_provision_deploy($target_git_reference = '') {
  // drush_provision_run_deploy_command('deploy');

  // Pass the "runner" option to provision-verify
  $options['runner'] = drush_get_option('runner');

  // Tell provision-verify to skip composer install.
  $options['no-composer-install'] = TRUE;
  
  // Verify command handles git and composer stages.
  provision_backend_invoke(d()->name, 'provision-verify', [], $options);
}

/**
 * Implements drush_post_COMMAND().
 */
function drush_provision_post_provision_deploy($git_ref = NULL) {

  $provision_log_type = drush_get_option('runner') == 'hosting_task'? 'p_info': 'ok';
  $drush_log_type = drush_get_option('runner') == 'hosting_task'? 'p_notice': 'ok';

  if (empty($git_ref)) {
    $git_ref = d()->platform->git_reference;
  }

  // Get post deploy options
  $revert = drush_get_option('revert');
  $update = drush_get_option('update');
  $cache = drush_get_option('cache');

  $drush_alias = d()->name;

  $commands = array();

  drush_log("{$drush_alias}: " . dt('Running deploy hooks.'), $drush_log_type);

  // Built in Hooks
  if ($update) {
    $commands[] = "drush updatedb --yes";
  }
  else {
    drush_log(dt('Skipped updating database...'), $drush_log_type);
  }

  // Clear the whole cache, unless option is false
  if ($cache) {
    if (drush_drupal_major_version(d()->root) >= 8) {
      $commands[] = "drush cache-rebuild";
    }
    else {
      $commands[] = "drush cache-clear all";
    }
  }
  else {
    drush_log(dt('Skipped rebuilding caches...'), $drush_log_type);
  }

  // Revert All Features, unless option is false
  if ($revert) {
    $commands[] = "drush features-revert-all --yes";
  }
  else {
    drush_log(dt('Skipped reverting all features...'), $drush_log_type);
  }

  foreach ($commands as $command) {
    $output = provision_process($command, d()->platform->git_root, NULL, [
      'DRUSH_OPTIONS_URI' => d()->uri
    ], TRUE, NULL, TRUE, $provision_log_type);

    // Detect common errors and help the user.
    // Composer install with incorrect PHP version.
    if (strpos($command, 'composer install') !== FALSE && strpos($output, 'your PHP version') !== FALSE && strpos($output, 'does not satisfy that requirement') !== FALSE) {
      drush_log(dt('Composer indicated that it could not install packages because the site codebase requires a higher version of PHP than what is running on the server.'), $drush_log_type);
      drush_log(dt('You can either upgrade your version of PHP on this server or set the maximum PHP version in your composer.json file. See !link for instructions for how to modify your composer.json file, then make sure to run `composer update` on your codebase to pin your packages to a lower PHP version.', [
        '!link' => '<a href="https://getcomposer.org/doc/06-config.md#platform">https://getcomposer.org/doc/06-config.md#platform</a>',
      ]), $drush_log_type);
    }

  }
}


