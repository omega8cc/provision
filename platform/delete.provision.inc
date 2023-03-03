<?php

/**
 * Before starting to delete the site, make a backup
 */
function drush_provision_drupal_pre_provision_delete($backup_file = NULL) {
  if (d()->type === 'site') {
    drush_set_option('force', true, 'process');

    provision_file()->chmod(d()->site_path, 0755);

    if (!empty($backup_file) && !strpos($backup_file, '/')) {
      drush_set_error('PROVISION_DELETE_BACKUP_NAME_RELATIVE', dt('The passed backup name is a relative path, storing that in the site directory which is going to be deleted is probably not intended. Try an absolute name, or no name to let one be generated.'));
    }
    else {
      drush_log(dt('Invoking drush provision-backup...'));
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
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_ROOT);

    drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
    foreach (d()->drush_aliases as $alias) {
      drush_invoke_process('@none', 'provision-save', array($alias), array('delete' => TRUE));
    }

    // Do not automatically save the drushrc at the end of the command.
    drush_set_option('provision_save_config', false);

    _provision_recursive_delete(d()->site_path);

    // If files paths are not in the default location, throw a warning saying
    // they will not be deleted automatically.
    foreach (array('file_public_path', 'file_private_path', 'file_temporary_path') as $property_name) {
      $path = d()->{$property_name};

      if (!drush_is_absolute_path($path)) {
        $path = d()->root . DIRECTORY_SEPARATOR . '';
      }

      // If files path is not inside site path, don't delete it because there
      // may be files from other sites.
      // @TODO: Create a site property like 'file_directory_force_delete' to force
      // deletion of files dirs that are outside of the site root.
      if (strpos($path, d()->site_path) !== 0) {
        drush_log(dt('Warning: File path "!name" was not deleted, because it may contain files from other sites. You must manually delete the folder.', array(
          '!name' => $path,
        )), 'warning');
      }
    }

    // we remove the aliases even if redirection is enabled as a precaution
    // if redirection is enabled, keep silent about errors
    _provision_drupal_delete_aliases();

    // Remove the symlink in the clients directory.
    _provision_client_delete_symlink();

    provision_drupal_push_site($override_slave_authority = TRUE);
  }
  elseif (d()->type === 'platform') {
    if (!drush_get_option('force', FALSE) && drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_ROOT) && provision_drupal_find_sites()) {
      drush_set_error(dt('Existing sites were found on this platform. These sites will need to be deleted before this platform can be deleted.'));
    }
    else {
      drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
      _provision_recursive_delete(d()->root);
      d()->service('http')->sync(d()->root);
    }
  }
  elseif (d()->type === 'server') {
    drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
  }
}
