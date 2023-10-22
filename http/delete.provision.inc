<?php

function drush_http_provision_delete() {
  // We don't need to propagate server deletion.
  if (d()->type === 'server') {
    return;
  }

  // Add platform root auto-discovery to avoid confusing
  // Composer based D8+ codebase root with Drupal real root.
  d()->root = provision_auto_fix_platform_root(d()->root);

  // Re-set the ROOT PATH for Drush.
  // In cases where we just build the platform Drush would have cleared this value.
  // See Drush commit 3b74d40b1228f022464b92243c16127eb613c2df
  drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);

  if (d()->type === 'site') {
    d()->service('http')->delete_config('site')
        ->succeed('Deleted platform configuration file', 'success')
        ->fail('Failed to delete platform configuration file', 'DRUSH_PERM_ERROR');
  }
  if (d()->type === 'platform') {
    if (!drush_get_option('force', FALSE) && drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_ROOT) && provision_drupal_find_sites()) {
      drush_set_error(dt('Existing sites were found on this platform. These sites will need to be deleted before this platform can be deleted.'));
    }
    else {
      d()->service('http')->delete_config('platform')
        ->succeed('Deleted platform configuration file', 'success')
        ->fail('Failed to delete platform configuration file', 'DRUSH_PERM_ERROR');
    }
  }
  d()->service('http')->parse_configs();
}
