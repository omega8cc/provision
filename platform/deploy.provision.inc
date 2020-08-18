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
}

/**
 * Implements drush_pre_COMMAND().
 */
function drush_provision_pre_provision_deploy() {
  drush_log(dt('Pre-deploy phase begun...'), 'ok');
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
  drush_log(dt('Deploy phase begun...'), 'ok');
}

/**
 * Implements drush_post_COMMAND().
 */
function drush_provision_post_provision_deploy() {
  drush_log(dt('Post-deploy phase begun...'), 'ok');
}


