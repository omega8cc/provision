<?php
/**
 * @file
 * Provision hooks for the provision clone command (Grav layer).
 *
 * The same chain the Grav restore already drives, with a different target:
 * backup(source, files-only mode) -> provision-save(new context) ->
 * provision-deploy(archive into the new context) -> provision-verify(new).
 * Nothing new materialises the capsule, renders the vhost or asserts the
 * ownership model -- the deploy engine does all of it; what is new is the
 * target-context wiring, the identity scrub the deploy engine runs for the
 * clone class (grav/grav/deploy.inc), and the rollback of a half-built copy.
 *
 * Mirrors txp/clone.provision.inc minus everything database: a phase-1 Grav
 * capsule is DB-less by design, its truthful safety-copy mode is
 * site_files_without_db_backup, and the copy keeps the source's database
 * server on purpose (the 'none' service) whatever the task says.
 */

function drush_provision_grav_provision_clone_validate($new_name = NULL, $platform = NULL) {
  // Refuse a MIXED pair on EITHER side: guarding only the source would let a
  // Drupal site be cloned ONTO a Grav platform (the shared Drupal validate
  // returns early only when the SITE is foreign), and a Grav site onto a
  // Drupal one.
  $source_is_grav = provision_platform_is_grav(d());
  // The TARGET test must be THIS CMS's own predicate, never the foreign-CMS
  // union: every CMS layer ships in the same provision tree and every
  // validate hook fires on every clone, so a union target test makes this
  // refusal swallow another CMS's supported task and answer with a
  // Grav-flavoured message. Defaults to the source's verdict, so an
  // unresolvable target platform can never manufacture a refusal.
  $target_is_grav = $source_is_grav;
  if (!empty($platform)) {
    $target = d($platform);
    if (is_object($target) && isset($target->root)) {
      $target_is_grav = provision_platform_is_grav($target->root);
    }
  }
  if (!$source_is_grav && !$target_is_grav) {
    return;
  }
  if ($source_is_grav != $target_is_grav) {
    return drush_set_error('PROVISION_GRAV_CLONE_CROSS_CMS',
      dt('Refusing a cross-CMS clone: the source site and the target platform must both be Grav.'));
  }
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_CLONE_CONTEXT', dt('provision-clone must run on a site context.'));
  }
  // A Grav capsule carries no database yet, so its archives are files-only.
  // Attached to a real database server the copy's deploy would provision a
  // database and look for a dump the backup never wrote; refuse up front
  // with the way out, exactly as the restore does.
  if (!(d()->service('db') instanceof Provision_Service_null)) {
    return drush_set_error('PROVISION_GRAV_CLONE_DB_SERVER',
      dt('This Grav site is attached to a database server, and its files-only archive cannot be deployed through the database machinery. Attach the site to the "none" database server (hosting_grav) and retry.'));
  }
  // The deploy keys its replace path on directory existence: a target that
  // already carries a capsule would be swapped out as if this were a
  // restore of that site. Refuse instead.
  $target_root = d()->root;
  if (!empty($platform) && is_object($target) && isset($target->root)) {
    $target_root = $target->root;
  }
  $new_uri = ltrim($new_name, '@');
  if (is_readable($target_root . '/sites/' . $new_uri . '/system/defines.php')) {
    return drush_set_error('PROVISION_GRAV_CLONE_TARGET_EXISTS',
      dt('A Grav capsule already exists at !path; delete that site first or choose another name.',
      array('!path' => $target_root . '/sites/' . $new_uri)));
  }
  // Clear a stale frontend backup-mode ctrl file before the internal safety
  // backup runs: a dead task's database-only mode would make the grav
  // backup refuse, or a different mode change the archive's transport.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }
}

function drush_provision_grav_pre_provision_clone($new_name, $platform = NULL) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  // The pre-clone copy is registered in the panel as "Pre-clone backup" and
  // offered as a restore point, so it must be self-contained. Force the
  // truthful grav-restorable mode (full tree, no dump; the dereferencing tar
  // branch), the same way the restore and delete safety copies do. No
  // database.sql pre-clear and no internal-backup flag: both are MyQuick
  // machinery for a database this site does not have.
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_without_db_backup');
  }
  drush_set_option('selected_backup_mode', 'site_files_without_db_backup');
  drush_set_option('backup_mode', 'site_files_without_db_backup');
  drush_log(dt('Clone safety copy forced to the grav-restorable mode: site_files_without_db_backup'), 'info');
  drush_log(dt('Invoking drush provision-backup in drush_provision_grav_pre_provision_clone'));
  drush_invoke('provision-backup');
}

function drush_provision_grav_pre_provision_clone_rollback($new_name, $platform = NULL) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  $backup_file = drush_get_option('backup_file');
  if (!empty($backup_file) && is_file($backup_file)) {
    provision_file()->unlink($backup_file)
      ->succeed('Removed unused clone site package')
      ->fail('Could not remove unused clone site package');
  }
}

function drush_provision_grav_provision_clone($new_name, $platform = NULL) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  drush_set_option('old_platform', d()->platform->name);

  // One authority for both copy commands (clone and copy-upgrade): the new
  // uri and name, the target platform, no aliases, no redirection, no
  // Encryption, no CDN, no inherited migration source, and the DB-less pin
  // that keeps the copy on the source's 'none' server.
  $options = _provision_grav_copy_options($new_name, $platform, 'clone');

  drush_invoke_process('@none', 'provision-save', array($new_name), $options);

  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'clone');

  // Set AFTER the alter hook so a contributed alter cannot drop them: the
  // deploy is a SEPARATE backend process that resolves the backup mode from
  // scratch (both halves must agree on the transport), decides the identity
  // scrub from the class, and needs the copy's own contact address.
  $resolved_backup_mode = provision_backup_mode_resolve();
  if (!empty($resolved_backup_mode)) {
    $deploy_options['selected_backup_mode'] = $resolved_backup_mode;
  }
  $deploy_options['deploy_class'] = 'clone';
  $deploy_options['client_email'] = drush_get_option('client_email', '');

  provision_backend_invoke($new_name, 'provision-deploy', array(drush_get_option('backup_file')), $deploy_options);

  if (!drush_get_error()) {
    provision_backend_invoke($new_name, 'provision-verify');
  }

  provision_reload_config('site', d()->site_path . '/drushrc.php');
}

/**
 * Remove the half-built copy (capsule, vhost, context) -- see
 * _provision_grav_discard_copy(), shared with the copy-upgrade rollback.
 */
function drush_provision_grav_provision_clone_rollback($new_name, $platform = NULL) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  // The same set a failed copy-upgrade discards: capsule, vhost, context.
  _provision_grav_discard_copy($new_name, $platform);
}
