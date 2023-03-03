<?php

/**
 * @file
 * Code related to deploy tasks.
 */

/**
 * Implements drush_hook_COMMAND_validate().
 * @TODO: Finish this later
function drush_provision_deploy_validate($target_git_reference = '') {
  if (empty($target_git_reference)) {
    drush_log(dt('No target git reference specified. Using stored git_reference @branch', array(
      '@branch' => d()->platform->git_reference,
    )), 'ok');
  }
  else {
    drush_log(dt('Target git reference: @branch', array(
      '@branch' => $target_git_reference,
    )), 'ok');

    drush_log(dt('Reset changes: @reset', array(
      '@reset' => drush_get_option('reset', 0)? dt('Yes'): dt('No'),
    )), 'ok');
  }

  // Load deploy commands from possible locations.
  $rows = array();
  $warnings = array();

  // Parse deploy options and commands.
  foreach (array(
    'git' => 1,
    'build' => 2,
    'install' => 3,
    'deploy' => 4,
    'test' => 5
  ) as $command_name => $phase) {
    // Did the user specify --COMMAND? Does the site have a "COMMAND_command" configured?
    if (drush_get_option($command_name) || $command_name == 'git') {
      // Step number
      $i++;

      // Check if command exists
      if (empty(d()->commands[$command_name])) {
        // Save rows to output to users.
        $rows[] = array("{$i}. {$command_name}: ","**MISSING**");

        // Save a warning.
        $warnings[] = dt('Command "@name" was requested, but not defined. Define commands in composer.json:config.devshop.commands.@name', array(
          '@name' => $command_name,
        ));
      }
      else {
        // Save rows to output to users.
        $rows[] = array("{$i}. {$command_name}: ", d()->commands[$command_name] . ' ' . $suffix);

        // Save actual commands to run to an array.
        $deploy_commands[$command_name] = d()->commands[$command_name];
      }
    }
  }

  // Save to drush option
  drush_set_option('deploy_commands', $deploy_commands);

  drush_log(dt('Deploy phases requested:') . PHP_EOL . drush_format_table($rows), 'ok');

  foreach ($warnings as $warning) {
    drush_log(dt('Warning: @warning', array(
      '@warning' => $warning,
    )) . PHP_EOL, 'warning');
  }
}

/**
 * Implements drush_pre_COMMAND().
 *
 * Pre-deploy phase. Deployment options:
 *   1. Build: composer install
 *   2. Install: drush site-install / sql-sync
function drush_provision_pre_provision_deploy() {
  drush_log(dt('Pre-deploy phase begun...'), 'ok');
  sleep(1);
  drush_provision_run_deploy_command('git');
  sleep(1);
  drush_provision_run_deploy_command('build');
  sleep(1);
  drush_provision_run_deploy_command('install');
}
 */

/**
 * Check for and run a deploy command, if the command exists and --COMMAND option was passed.
 * @param $command_name
function drush_provision_run_deploy_command($command_name) {
  $deploy_commands = drush_get_option('deploy_commands', array());
  if (!empty($deploy_commands[$command_name])) {
    // @TODO: Convert all d() properties to ENV VARS.
    $environment = array(
      'GIT_REFERENCE' => d()->git_reference,
      'DRUSH_ALIAS' => '@' . d()->name,
    );
    provision_process($deploy_commands[$command_name], d()->git_root, $command_name, $environment);
  }
  elseif (empty($deploy_commands[$command_name])) {
    drush_log(dt('No commands specified for "%command_name" phase.', array(
      '%command_name' => $command_name,
    )) . PHP_EOL, 'warning');
  }
}

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


