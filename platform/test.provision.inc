<?php

include_once 'deploy.provision.inc';

/**
 * @file
 * Code related to test tasks.
 */

/**
 * drush provision-test command.
 */
function drush_provision_test() {
  drush_provision_deploy_code();
}

/**
 * Implements drush_post_COMMAND().
 */
function drush_provision_post_provision_test() {
  drush_provision_post_provision_deploy_code();
}
