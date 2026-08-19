<?php

/**
 * Delete command implementation (Backdrop layer).
 *
 * Fork of platform/delete.provision.inc: CMS-agnostic recursive delete + alias
 * flips. The D8+ site-local Drush lock/unlock blocks are dropped (Backdrop has
 * no site-local vendor/drush).
 */

/**
 * Before starting to delete the site, make a backup.
 */
function drush_provision_backdrop_pre_provision_delete($backup_file = NULL) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type === 'site') {
    // Backup mode ctrl file cleanup, only where the safety backup follows:
    // a platform or server delete takes no backup, and unlinking there would
    // destroy a concurrent backup task's live mode for nothing.
    if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
      unlink(AEGIR_BACKUP_MODE_CTRL);
    }
    drush_set_option('force', true, 'process');
    if (!empty($backup_file) && !strpos($backup_file, '/')) {
      drush_set_error('PROVISION_DELETE_BACKUP_NAME_RELATIVE', dt('The passed backup name is a relative path, storing that in the site directory which is going to be deleted is probably not intended. Try an absolute name, or no name to let one be generated.'));
    }
    else {
      drush_log(dt('Invoking drush provision-backup in drush_provision_backdrop_pre_provision_delete'));
      drush_invoke("provision-backup", $backup_file);
      drush_unset_option('force', 'process');
    }
  }
}

/**
 * If we're deleting a site, remove any directories for the site in sites folder.
 * If we're deleting a platform, remove the whole platform.
 * This can't be rolled back, so won't even try.
 */
function drush_provision_backdrop_provision_delete() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type === 'site') {

    // Add platform root auto-discovery to avoid confusing
    // Composer based codebase root with the real root.
    d()->root = provision_auto_fix_platform_root(d()->root);

    // Re-set the ROOT PATH for Drush.
    drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);

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
    _provision_backdrop_delete_aliases();

    // Remove the symlink in the clients directory.
    _provision_client_delete_symlink();

    provision_drupal_push_site($override_slave_authority = TRUE);
  }
  elseif (d()->type === 'platform') {

    // Add platform root auto-discovery to avoid confusing
    // Composer based codebase root with the real root.
    d()->root = provision_auto_fix_platform_root(d()->root);

    // Make sure if we are within parent codebase directory
    $test_parent = dirname(d()->root);
    $local_delete_parent = FALSE;
    if (is_dir($test_parent . '/vendor') &&
      is_file($test_parent . '/composer.lock')) {
      $local_delete_parent = $test_parent;
    }

    // Re-set the ROOT PATH for Drush.
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

/**
 * Purge the deleted site's cache namespace from the shared Valkey/Redis store.
 *
 * Backdrop counterpart of the Drupal post-delete purge: the Backdrop redis
 * integration namespaces keys by the site's DB coordinates (see
 * _provision_valkey_purge_backdrop_site()), and Aegir mints db names
 * deterministically from the URI, so a site recreated (or cloned into) the
 * same name inherits the deleted predecessor's keys. The db credentials are
 * read from the in-memory task context (the site's files are already gone by
 * the post phase, but the options loaded with the alias are not). Runs as a
 * post hook so the vhost is already removed and nothing repopulates the
 * namespace mid-purge. Non-fatal by design; the install and deploy paths
 * purge the same namespace again defensively before any site code runs.
 */
function drush_provision_backdrop_post_provision_delete() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  if (d()->type === 'site') {
    _provision_valkey_purge_backdrop_site(d()->uri);
  }
}
