<?php

/**
 * Provision enable command
 *
 * Enable a site that has been disabled
 */
function drush_provision_drupal_provision_enable_validate() {
  if (d()->type == 'site') {
    d()->site_enabled = true;
    provision_lock_some_vnd(d()->root);
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
    provision_unlock_some_vnd(d()->root);
  }
}

function drush_provision_drupal_post_provision_enable() {
  if (d()->type == 'site') {
    d()->write_alias();
  }
}
