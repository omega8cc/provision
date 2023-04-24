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

  $provision_log_type = drush_get_option('runner') == 'hosting_task'? 'p_command_log': 'ok';
  $drush_log_type = drush_get_option('runner') == 'hosting_task'? 'p_notice': 'ok';

  if (empty($git_ref)) {
    $git_ref = d()->platform->git_reference;
  }

  $drush_alias = d()->name;

  drush_log("{$drush_alias}: " . dt('Running deploy hooks.'), $drush_log_type);

  $steps = d()->getDeploySteps();

  foreach ($steps as $step_name => $step) {
    if (drush_get_option($step_name)) {
      $step_succeeded = d()->runDeployStep($step_name);
      if (!$step_succeeded){
        return drush_set_error(DRUSH_APPLICATION_ERROR, dt('Deploy aborted: commands failed.'));
      }
    }
  }
}
