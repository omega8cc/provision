<?php

/**
 * @file
 * Code related to deploy tasks.
 */

/**
 * Implements drush_hook_COMMAND_validate().
 */
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
 */
function drush_provision_pre_provision_deploy() {
  drush_log(dt('Pre-deploy phase begun...'), 'ok');
  sleep(1);
  drush_provision_run_deploy_command('git');
  sleep(1);
  drush_provision_run_deploy_command('build');
  sleep(1);
  drush_provision_run_deploy_command('install');
}

/**
 * Check for and run a deploy command, if the command exists and --COMMAND option was passed.
 * @param $command_name
 */
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
 *
 *
 *
 */
function drush_provision_deploy($target_git_reference = '') {
  drush_log( dt('Deploy phase begun...'), 'ok');
  drush_provision_run_deploy_command('deploy');
}

/**
 * Implements drush_post_COMMAND().
 */
function drush_provision_post_provision_deploy() {
  drush_log(dt('Post-deploy phase begun...'), 'ok');
  drush_provision_run_deploy_command('test');
}


