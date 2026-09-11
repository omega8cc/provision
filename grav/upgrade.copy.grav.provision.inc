<?php

/**
 * @file
 * The provision-grav-copy-upgrade command: upgrade a COPY of a Grav capsule.
 *
 * The migration shape the other CMS families use, on a family that also
 * supports upgrading in place: the capsule is copied to a NEW uri on a target
 * Grav platform, the COPY is upgraded to a staged official release, and the
 * source keeps serving at its own domain, untouched, until the operator is
 * satisfied and runs Cutover. The source capsule is never opened for writing
 * by this command -- everything after the backup happens on the copy.
 *
 * The chain IS the clone chain (provision-save of the new context on the
 * target platform, provision-deploy of the source's archive into it), with
 * two differences: the deploy class is 'upgrade', so the deploy engine keeps
 * the site's identity instead of scrubbing it (a copy that is going to take
 * over the original's domain must still be the same site), and the core
 * install plus bundle refresh run on the copy afterwards.
 *
 * Hook naming: the command name starts with this commandfile's name, so Drush
 * COLLAPSES the prefix for the validate/command/rollback hooks
 * (drush_provision_grav_copy_upgrade*) while the pre/post variations keep the
 * doubled form (drush_provision_grav_pre_provision_grav_copy_upgrade).
 *
 * File name: Drush resolves command includes by the reversed-dotted command
 * name, so provision-grav-copy-upgrade => upgrade.copy.grav.provision.inc.
 */

/**
 * Implements drush_hook_COMMAND_validate() (collapsed name).
 *
 * Fail-closed: every precondition is checked before anything is created, and
 * the artefact verdict is formed here, off the SOURCE capsule's version, so a
 * run that could only end in "nothing newer is staged" never makes a copy.
 */
function drush_provision_grav_copy_upgrade_validate($new_name = NULL, $platform = NULL) {
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_BAD_SOURCE',
      dt('provision-grav-copy-upgrade must run on a site context.'));
  }
  if (!provision_platform_is_grav(d())) {
    return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_BAD_SOURCE',
      dt('provision-grav-copy-upgrade only applies to sites on Grav platforms.'));
  }
  if (_provision_grav_php_bin() === FALSE) {
    return drush_set_error('PROVISION_GRAV_PHP_TOO_OLD',
      dt('No PHP >= 8.3 binary found; Grav tasks refuse to run below the enforced floor.'));
  }
  if (!_provision_grav_site_exists()) {
    return drush_set_error('PROVISION_GRAV_SITE_NOT_FOUND',
      dt('No Grav capsule found at !site.', array('!site' => d()->site_path)));
  }
  // A Grav capsule carries no database yet, so its archives are files-only.
  // Attached to a real database server the copy's deploy would provision a
  // database and look for a dump the backup never wrote; refuse up front
  // with the way out, exactly as the clone does.
  if (!(d()->service('db') instanceof Provision_Service_null)) {
    return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_DB_SERVER',
      dt('This Grav site is attached to a database server, and its files-only archive cannot be deployed through the database machinery. Attach the site to the "none" database server (hosting_grav) and retry.'));
  }

  // The target platform, tested with THIS layer's own predicate and never the
  // foreign-CMS union: every CMS layer ships in the same provision tree, and
  // a union test here would let a capsule land on another CMS's platform.
  if (!is_string($platform) || $platform === '') {
    return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_BAD_TARGET',
      dt('No target platform alias given.'));
  }
  $target = d($platform);
  $target_root = (is_object($target) && isset($target->root)) ? $target->root : '';
  if ($target_root === '' || !provision_platform_is_grav($target_root)) {
    return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_BAD_TARGET',
      dt('The target platform !platform is not a Grav platform.', array('!platform' => $platform)));
  }

  if (!is_string($new_name) || $new_name === '') {
    return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_BAD_TARGET',
      dt('No new site alias given.'));
  }
  $new_uri = ltrim($new_name, '@');
  if ($new_uri === d()->uri) {
    return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_BAD_TARGET',
      dt('The new uri must differ from the source uri (the source keeps serving until you retire it).'));
  }
  // Conservative hostname shape check; the frontend already ran its FQDN
  // validator, this catches a mangled hand-run invocation before a context is
  // saved and a directory is created from the value.
  if (!preg_match('/^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/', $new_uri)
    || strpos($new_uri, '..') !== FALSE) {
    return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_BAD_TARGET',
      dt('The new uri !uri is not a plausible domain name (lower-case letters, digits, dots and hyphens only).',
      array('!uri' => $new_uri)));
  }
  if (strtolower($new_uri) === 'hostmaster'
    || strtolower($new_uri) === strtolower(d('@hostmaster')->uri)) {
    return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_BAD_TARGET',
      dt('Refusing to take the hosting front-end\'s name.'));
  }
  // provision-save has no validate of its own and writes whatever alias it is
  // given, so an occupied name would silently re-point a live site's context
  // at this platform. Refuse on a context that still resolves to a site
  // directory. A leftover alias whose recorded directory is gone is a
  // rolled-back rename's own debris: log it and carry on, the same tolerance
  // the cutover carries, so our own leftover never blocks a retry.
  if (provision_file()->exists(drush_server_home() . '/.drush/' . $new_uri . '.alias.drushrc.php')->status()) {
    $leftover = d('@' . $new_uri);
    $leftover_dir = (is_object($leftover) && isset($leftover->site_path) && is_string($leftover->site_path))
      ? $leftover->site_path : '';
    if ($leftover_dir === '' || provision_file()->exists($leftover_dir)->status()) {
      return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_TARGET_EXISTS',
        dt('A backend context already holds @uri; choose another name.',
        array('@uri' => $new_uri)));
    }
    drush_log(dt('A stale @uri alias with no site directory is left over (a rolled-back rename); the copy overwrites it.',
      array('@uri' => $new_uri)), 'info');
  }

  // The deploy keys its replace path on directory existence: a target that
  // already carries a capsule would be swapped out as if this were a restore
  // of that site.
  if (is_dir($target_root . '/sites/' . $new_uri)) {
    return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_TARGET_EXISTS',
      dt('sites/@uri already exists on the target platform; delete that site first or choose another name.',
      array('@uri' => $new_uri)));
  }

  // Clear a stale frontend backup-mode ctrl file before the internal safety
  // backup runs: a dead task's database-only mode would make the grav backup
  // refuse, or a different mode change the archive's transport.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }

  // The artefact verdict, measured against the SOURCE capsule -- the copy is
  // materialised from the source's archive, so the source's version is the
  // one the upgrade has to beat. The target platform's own seed version is
  // deliberately not consulted: under the capsule model a site carries its
  // own core and the platform is only where it lives.
  $resolved = _provision_grav_resolve_artefact(
    _provision_grav_get_version(d()->site_path),
    drush_get_option('artefact', NULL));
  if ($resolved === FALSE) {
    return FALSE;
  }
  drush_set_option('grav_upgrade_artefact', $resolved['artefact']);
  drush_set_option('grav_upgrade_artefact_version', $resolved['version']);
}

/**
 * Implements drush_hook_pre_COMMAND() (doubled name -- not collapsed).
 *
 * The safety copy of the LIVE source, in the truthful grav-restorable mode:
 * it is both the archive the copy is built from and the restore point the
 * front end registers on the source.
 */
function drush_provision_grav_pre_provision_grav_copy_upgrade($new_name = NULL, $platform = NULL) {
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_without_db_backup');
  }
  drush_set_option('selected_backup_mode', 'site_files_without_db_backup');
  drush_set_option('backup_mode', 'site_files_without_db_backup');
  drush_log(dt('Pre-upgrade safety copy of the source, forced to the grav-restorable mode: site_files_without_db_backup'), 'info');
  drush_invoke('provision-backup');
}

/**
 * Implements drush_hook_pre_COMMAND_rollback().
 */
function drush_provision_grav_pre_provision_grav_copy_upgrade_rollback($new_name = NULL, $platform = NULL) {
  $backup_file = drush_get_option('backup_file');
  if (!empty($backup_file) && is_file($backup_file)) {
    provision_file()->unlink($backup_file)
      ->succeed('Removed unused copy site package')
      ->fail('Could not remove unused copy site package');
  }
}

/**
 * Implements drush_hook_COMMAND() (collapsed name).
 */
function drush_provision_grav_copy_upgrade($new_name = NULL, $platform = NULL) {
  drush_set_option('old_platform', d()->platform->name);
  $artefact = drush_get_option('grav_upgrade_artefact');
  $version = drush_get_option('grav_upgrade_artefact_version');
  // Context names carry the leading @; a frontend that passes the bare uri
  // would otherwise have a context saved under a name nothing resolves.
  $new_uri = ltrim($new_name, '@');
  $new_name = '@' . $new_uri;

  // The clone's option handling, verbatim: one authority for both copy
  // commands, so the two can never ship the copy differently.
  $options = _provision_grav_copy_options($new_name, $platform, 'copy-upgrade');

  drush_invoke_process('@none', 'provision-save', array($new_name), $options);

  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'grav_copy_upgrade');

  // Set AFTER the alter hook so a contributed alter cannot drop them: the
  // deploy is a SEPARATE backend process that resolves the backup mode from
  // scratch (both halves must agree on the transport) and decides from the
  // class whether the site keeps its identity. 'upgrade' keeps it: this copy
  // is meant to take the source's place, so its keys, accounts and contact
  // addresses are the source's own.
  $resolved_backup_mode = provision_backup_mode_resolve();
  if (!empty($resolved_backup_mode)) {
    $deploy_options['selected_backup_mode'] = $resolved_backup_mode;
  }
  $deploy_options['deploy_class'] = 'upgrade';
  $deploy_options['client_email'] = drush_get_option('client_email', '');

  provision_backend_invoke($new_name, 'provision-deploy', array(drush_get_option('backup_file')), $deploy_options);

  if (!drush_get_error()) {
    $copy_path = $options['root'] . '/sites/' . $new_uri;
    if (!is_readable($copy_path . '/system/defines.php')) {
      return drush_set_error('PROVISION_GRAV_COPY_UPGRADE_NO_COPY',
        dt('No Grav capsule at !path after the deploy; nothing was upgraded and the source is untouched.',
        array('!path' => $copy_path)));
    }
    drush_log(dt('Upgrading the copy at !path from Grav @before to Grav @after using @artefact',
      array(
        '!path' => $copy_path,
        '@before' => _provision_grav_get_version($copy_path),
        '@after' => $version,
        '@artefact' => basename($artefact),
      )), 'ok');

    // Seed, install, prove, refresh the bundle, clear the cache -- the same
    // sequence the in-place upgrade runs, against the copy's path.
    if (!_provision_grav_apply_artefact($copy_path, $new_uri, $artefact, $version)) {
      return FALSE;
    }
  }

  if (!drush_get_error()) {
    provision_backend_invoke($new_name, 'provision-verify');

    $enable_fast_track = d('@server_master')->aegir_root . '/static/control/FastTrack.info';
    if (!is_file($enable_fast_track)) {
      sleep(5); // A small trick to avoid high load and race conditions.
      // Re-verify the source site via the frontend, mirroring clone (#1004526).
      provision_backend_invoke('@hostmaster', 'hosting-task', array('@' . d()->uri, 'verify'), array('force' => TRUE));
    }
  }

  provision_reload_config('site', d()->site_path . '/drushrc.php');
}

/**
 * Implements drush_hook_COMMAND_rollback() (collapsed name).
 *
 * Discard the copy and nothing else: the source was never in the write path,
 * and its pre-upgrade safety copy stays registered as a restore point.
 */
function drush_provision_grav_copy_upgrade_rollback($new_name = NULL, $platform = NULL) {
  _provision_grav_discard_copy($new_name, $platform);
  drush_log(dt('provision-grav-copy-upgrade failed; the source site @uri is untouched and still serving at its own domain.',
    array('@uri' => d()->uri)), 'advisory');
}
