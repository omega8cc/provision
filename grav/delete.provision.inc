<?php
/**
 * @file
 * Provision hooks for the provision delete command (Grav layer).
 *
 * The platform-delete refusal (boa-grav R0) plus the site/platform delete
 * implementations, forked from platform/delete.provision.inc minus everything
 * database and Drupal-alias-shaped. The shared Drupal hooks early-return on
 * the foreign-CMS union, so without these a grav delete would be a silent
 * no-op (site) or unreachable (platform).
 */

/**
 * Refuse to delete a Grav platform that still carries capsules.
 *
 * Validate-phase on purpose: an error here aborts the whole command before any
 * implementation hook (including the recursive delete) can run.
 */
function drush_provision_grav_provision_delete_validate() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type !== 'platform') {
    return;
  }
  if (drush_get_option('force', FALSE)) {
    return;
  }
  $sites = provision_grav_find_sites();
  if (!empty($sites)) {
    return drush_set_error('PROVISION_GRAV_SITES_EXIST',
      dt('Grav sites still exist on this platform (!sites). These sites must be deleted before the platform can be deleted.',
      array('!sites' => implode(', ', array_keys($sites)))));
  }
}

/**
 * Before starting to delete the site, make a safety backup.
 */
function drush_provision_grav_pre_provision_delete($backup_file = NULL) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type === 'site') {
    // Dead-leftover ctrl cleanup, site deletes only (platform deletes take no
    // backup; unlinking there would destroy a concurrent backup task's mode).
    if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
      unlink(AEGIR_BACKUP_MODE_CTRL);
    }
    drush_set_option('force', true, 'process');
    if (!empty($backup_file) && !strpos($backup_file, '/')) {
      drush_set_error('PROVISION_DELETE_BACKUP_NAME_RELATIVE', dt('The passed backup name is a relative path, storing that in the site directory which is going to be deleted is probably not intended. Try an absolute name, or no name to let one be generated.'));
    }
    else {
      drush_log(dt('Invoking drush provision-backup in drush_provision_grav_pre_provision_delete'));
      drush_invoke("provision-backup", $backup_file);
      drush_unset_option('force', 'process');
    }
  }
}

/**
 * Delete the capsule (site) or the platform tree.
 */
function drush_provision_grav_provision_delete() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type === 'site') {
    drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
    foreach (d()->drush_aliases as $alias) {
      drush_invoke_process('@none', 'provision-save', array($alias), array('delete' => TRUE));
    }

    // Do not automatically save the drushrc at the end of the command.
    drush_set_option('provision_save_config', false);

    _provision_recursive_delete(d()->site_path);

    // No Drupal alias store / alias symlinks to clean: grav aliases exist
    // only in the rendered vhost's server_name.

    // Remove the symlink in the clients directory.
    _provision_client_delete_symlink();

    provision_drupal_push_site($override_slave_authority = TRUE);
  }
  elseif (d()->type === 'platform') {
    // The R0 validate refusal has already run (and --force bypasses it, the
    // shared semantics); belt here for the force path over a populated
    // platform: log what is being destroyed.
    $sites = provision_grav_find_sites();
    if (!empty($sites)) {
      drush_log(dt('FORCED delete of a populated Grav platform; destroying capsules: !sites',
        array('!sites' => implode(', ', array_keys($sites)))), 'warning');
    }
    drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
    _provision_recursive_delete(d()->root);
    d()->service('http')->sync(d()->root);
  }
}

/**
 * Purge the deleted site's front-end cache namespace from the shared store.
 *
 * Host-keyed, not CMS-keyed: the <host>_z_* page-cache namespace belongs to
 * the URI. A Grav site recreated under a name previously used by a Drupal
 * site (or vice versa) must not inherit stale keys. Non-fatal by design.
 */
function drush_provision_grav_post_provision_delete() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type === 'site' && function_exists('_provision_valkey_purge_site')) {
    _provision_valkey_purge_site(d()->uri);
  }
}
