<?php

/**
 * Before starting to delete the site, make a backup
 */
function drush_provision_drupal_pre_provision_delete($backup_file = NULL) {
  if (d()->type === 'site') {
    drush_set_option('force', true, 'process');
    if (!empty($backup_file) && !strpos($backup_file, '/')) {
      drush_set_error('PROVISION_DELETE_BACKUP_NAME_RELATIVE', dt('The passed backup name is a relative path, storing that in the site directory which is going to be deleted is probably not intended. Try an absolute name, or no name to let one be generated.'));
    }
    else {
      drush_log(dt('Invoking drush provision-backup in drush_provision_drupal_pre_provision_delete'));
      drush_invoke("provision-backup", $backup_file);
      drush_unset_option('force', 'process');
    }
  }
}

/**
 * If we're deleting a site, remove any directories for the site in sites folder
 * If we're deleting a platform, remove the whole platform
 * This can't be rolled back. so won't even try.
 */
function drush_provision_drupal_provision_delete() {
  if (d()->type === 'site') {

    // Add platform root auto-discovery to avoid confusing
    // Composer based D8+ codebase root with Drupal real root.
    d()->root = provision_auto_fix_platform_root(d()->root);

    // Re-set the ROOT PATH for Drush.
    // In cases where we just build the platform Drush would have cleared this value.
    // See Drush commit 3b74d40b1228f022464b92243c16127eb613c2df
    drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);

    // Lock site-local Drush if unlocked
    $drush_lock_ctrl = d()->root . '/local_drush_locked.pid';
    $drush_unlock_ctrl = d()->root . '/local_drush_unlocked.pid';
    if (provision_file()->exists($drush_unlock_ctrl)->status() || !provision_file()->exists($drush_lock_ctrl)->status()) {
      drush_log(dt('LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
      provision_lock_drush_local_vnd();
    }

    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_ROOT);

    drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
    foreach (d()->drush_aliases as $alias) {
      drush_invoke_process('@none', 'provision-save', array($alias), array('delete' => TRUE));
    }

    // Do not automatically save the drushrc at the end of the command.
    drush_set_option('provision_save_config', false);

    _provision_recursive_delete(d()->site_path);
    // we remove the aliases even if redirection is enabled as a precaution
    // if redirection is enabled, keep silent about errors
    _provision_drupal_delete_aliases();

    // Remove the symlink in the clients directory.
    _provision_client_delete_symlink();

    provision_drupal_push_site($override_slave_authority = TRUE);
  }
  elseif (d()->type === 'platform') {

    // Add platform root auto-discovery to avoid confusing
    // Composer based D8+ codebase root with Drupal real root.
    d()->root = provision_auto_fix_platform_root(d()->root);

    provision_unlock_some_vnd();

    // Make sure if we are within parent codebase directory
    $test_parent = dirname(d()->root);
    $local_delete_parent = FALSE;
    if (is_dir($test_parent . '/vendor') &&
      is_file($test_parent . '/composer.lock')) {
      $local_delete_parent = $test_parent;
    }

    // Re-set the ROOT PATH for Drush.
    // In cases where we just build the platform Drush would have cleared this value.
    // See Drush commit 3b74d40b1228f022464b92243c16127eb613c2df
    drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);

    if (!drush_get_option('force', FALSE) && drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_ROOT) && provision_drupal_find_sites()) {
      $local_delete_parent = FALSE;
      drush_set_error(dt('Existing sites were found on this platform. These sites will need to be deleted before this platform can be deleted.'));
    }
    else {
      drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
      _provision_recursive_delete(d()->root);
      if ($local_delete_parent) {
        sleep(5);
        _provision_recursive_delete($local_delete_parent);
        drush_log(dt('Parent vendor dir deleted too: !vendor', array('!vendor' => $local_delete_parent)), 'message');
      }
      d()->service('http')->sync(d()->root);
    }
  }
  elseif (d()->type === 'server') {
    drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
  }
}
