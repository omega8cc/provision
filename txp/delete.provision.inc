<?php
/**
 * @file
 * Provision hooks for the provision delete command (TXP layer).
 *
 * Two halves: the platform-delete REFUSAL (the shared platform delete refuses
 * only via a settings.php glob — TXP site dirs have no settings.php, so
 * without the validate guard a populated TXP platform would recursively
 * delete with no refusal), and the site/platform delete bodies (the shared
 * ones are union-guarded away). The site's safety backup rides the shared
 * pre-delete machinery contract: mirror the Backdrop pre hook.
 */

/**
 * Refuse to delete a TXP platform that still carries sites.
 *
 * Validate-phase on purpose: an error here aborts the whole command before
 * any implementation hook (including any recursive delete) can run.
 */
function drush_provision_txp_provision_delete_validate() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type !== 'platform') {
    return;
  }
  if (drush_get_option('force', FALSE)) {
    return;
  }
  $sites = provision_txp_find_sites();
  if (!empty($sites)) {
    return drush_set_error('PROVISION_TXP_SITES_EXIST',
      dt('Textpattern sites still exist on this platform (!sites). These sites must be deleted before the platform can be deleted.',
      array('!sites' => implode(', ', array_keys($sites)))));
  }
}

/**
 * Take a final self-contained backup of the site before deleting it.
 */
function drush_provision_txp_pre_provision_delete($backup_file = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type === 'site') {
    _provision_txp_ensure_db_creds_loaded();
    // Clear a stale mode ctrl file, ONLY on the path that takes a backup (a
    // platform/server delete takes none, and unlinking there would destroy a
    // concurrent backup task's live mode for nothing).
    if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
      unlink(AEGIR_BACKUP_MODE_CTRL);
    }
    // The classic dump creates the file with fopen($path, 'x'), which FAILS
    // on an existing file, so a dump left behind by a killed earlier backup
    // would wedge the final safety copy -- on the one flow where there is no
    // second chance. Clear it first.
    if (is_file(d()->site_path . '/database.sql')) {
      provision_file()->unlink(d()->site_path . '/database.sql')
        ->succeed('Removed a stale database.sql before the final safety copy')
        ->fail('Could not remove a stale database.sql before the final safety copy');
    }
    // The last copy of a site must be restorable: force the self-contained
    // mode exactly like the restore safety copy.
    if (!defined('SELECTED_BACKUP_MODE')) {
      define('SELECTED_BACKUP_MODE', 'site_files_with_mysqldump');
    }
    drush_set_option('selected_backup_mode', 'site_files_with_mysqldump');
    drush_set_option('backup_mode', 'site_files_with_mysqldump');
    // NB force is SET here (precedent), never tested: it carries the backup
    // past provision-backup's own force-gated guards. Testing it -- the
    // round-2 shape -- skipped the final safety backup for exactly the
    // force-flagged deletes that most need one.
    drush_set_option('force', TRUE, 'process');
    if (!empty($backup_file) && !strpos($backup_file, '/')) {
      drush_unset_option('force', 'process');
      return drush_set_error('PROVISION_DELETE_BACKUP_NAME_RELATIVE',
        dt('The passed backup name is a relative path; storing the final backup inside the site directory that is about to be deleted is probably not intended. Use an absolute name, or none to let one be generated.'));
    }
    drush_log(dt('Invoking drush provision-backup in drush_provision_txp_pre_provision_delete'));
    drush_invoke('provision-backup', $backup_file);
    drush_unset_option('force', 'process');
  }
}

/**
 * Remove the site or platform.
 */
function drush_provision_txp_provision_delete() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type === 'site') {
    drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);

    drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
    foreach (d()->drush_aliases as $alias) {
      drush_invoke_process('@none', 'provision-save', array($alias), array('delete' => TRUE));
    }

    // Do not automatically save the drushrc at the end of the command.
    drush_set_option('provision_save_config', FALSE);

    _provision_recursive_delete(d()->site_path);

    // Remove the symlink in the clients directory.
    _provision_client_delete_symlink();

    provision_drupal_push_site($override_slave_authority = TRUE);
  }
  elseif (d()->type === 'platform') {
    drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);

    // Defensive re-check (validate already refused; force bypasses both).
    if (!drush_get_option('force', FALSE) && provision_txp_find_sites()) {
      drush_set_error('PROVISION_TXP_SITES_EXIST',
        dt('Existing Textpattern sites were found on this platform; delete them first.'));
    }
    else {
      drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
      _provision_recursive_delete(d()->root);
      d()->service('http')->sync(d()->root);
    }
  }
}
