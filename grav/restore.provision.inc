<?php

/**
 * @file
 * Restore command (Grav layer).
 *
 * Fork of txp/restore.provision.inc: the restore is the standard backup ->
 * provision-deploy -> provision-verify chain. The safety copy is forced to
 * the mode that tells the truth about the site (_provision_grav_force_safety_mode()):
 * files only on the 'none' server, files with the database dump for a site
 * with a database, which also carries the internal-backup flag and hands the
 * deploy its current database (is_restore, restore_source_db) the way the
 * Textpattern restore does.
 */

/**
 * Make sure we have a valid site being restored, and that the file being restored from exists.
 */
function drush_provision_grav_provision_restore_validate($restore_file = null) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  // Backup mode ctrl file cleanup: on the serial queue any file present at
  // task start is a dead leftover by definition (same rationale as the
  // Drupal flow); the safety copy below force-defines its own mode.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }

  $exists = provision_file()->exists($restore_file)
    ->succeed('Restoring site from @path')
    ->fail('Could not find backup file @path', 'PROVISION_BACKUP_NOT_FOUND')
    ->status();
  if ($exists) {
    drush_set_option('restore_file', $restore_file);
    // Refuse a restore that cannot fit before its safety copy starts (the
    // shared pre-flight).
    if (!provision_copy_task_space_preflight('restore', NULL, NULL, $restore_file)) {
      return FALSE;
    }
  }
}

/**
 * Make a safety copy before making any changes.
 */
function drush_provision_grav_pre_provision_restore($restore_file) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  drush_log(dt("Putting site under maintenance"));
  d()->site_enabled = false;
  // Render it: Grav has no in-tree offline flag, so the disabled vhost is
  // the window (the twins write a settings file here). Without this the
  // flag lived in memory only and the site served all through the swap.
  d()->service('http')->create_config('site');
  d()->service('http')->parse_configs();

  // The safety copy is registered in the panel as a real backup of this site,
  // so it must honour the backup contract for what this site is.
  _provision_grav_force_safety_mode('Restore', TRUE);
  drush_invoke('provision-backup');
}

function drush_provision_grav_pre_provision_restore_rollback($restore_file) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused restore safety copy')
    ->fail('Could not remove unused restore safety copy');
  _provision_grav_internal_backup_flag(FALSE);
}

/**
 * Switch the restore directories around via the deploy chain.
 */
function drush_provision_grav_provision_restore($restore_file) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  $has_db = _provision_grav_has_db();
  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
    // Identity-preserving: the deploy engine keeps the archive's own keys
    // and overlay (the twins carry the same flag). For a site with a
    // database it also makes the import take the archive's own database.sql,
    // never a tmp_expim fast-import of the pre-restore safety dump.
    'is_restore' => TRUE,
  );
  if ($has_db) {
    // The site's current database, carried across when the archive has no
    // dump (an archive taken before this site had a database).
    $deploy_options['restore_source_db'] = drush_get_option('db_name', '', 'site');
  }
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'restore');
  // The explicit class, set AFTER the alter so nothing can drop it: the
  // deploy scrub must never decide the identity outcome from which files
  // happened to be readable at backup time.
  $deploy_options['deploy_class'] = 'restore';

  // What the deploy is about to replace, for a site with a database: a
  // deploy that died without rolling back leaves its copy and the database
  // it made (see the failure branch below).
  if ($has_db) {
    provision_target_copy_probe('restore_target_copy', d()->root, d()->uri, isset(d()->options['db_server']) ? d()->options['db_server'] : '');
  }
  $deploy_result = provision_backend_invoke(d()->name, 'provision-deploy', array(drush_get_option('restore_file')), $deploy_options);
  if ($has_db) {
    provision_target_copy_deployed('restore_target_copy', $deploy_result);
  }
  if (!drush_get_error()) {
    provision_backend_invoke(d()->name, 'provision-verify');
    drush_set_option('installed', false);
  }
  else {
    // The pre-hook took the site into the maintenance (disabled) vhost. A
    // FAILED restore must not leave it there: the deploy has already swapped
    // the original tree back, so the only thing still wrong is the vhost.
    // Re-render it from the persisted (enabled) state -- site_enabled was
    // only ever flipped in memory, never written to the alias. Without this
    // the capsule stays on "Under Construction" until an operator runs
    // Enable/Verify by hand. Same shape as the Textpattern twin. A deploy
    // that died never swapped back: put the original tree back and remove
    // the restored copy and its database first.
    if ($has_db) {
      provision_remove_unfinished_target_copy('restore_target_copy');
    }
    d()->site_enabled = true;
    d()->service('http')->create_config('site');
    d()->service('http')->parse_configs();
    // The deploy's own error already made the task red; this row reports
    // the recovery that worked.
    drush_log(dt('Restore failed; the original site tree and vhost have been put back.'), 'ok');
  }

  // Always clear the flag, success or failure (a leftover one silently ends
  // dated DB-dump retention on the account).
  _provision_grav_internal_backup_flag(FALSE);
}
