<?php

/**
 * @file
 * The provision-grav-upgrade command: mirror-fed in-place capsule upgrade.
 *
 * Spike-3-proven mechanics (boa-grav docs/spikes/2026-08-31-round-1.md):
 * `bin/gpm direct-install -y -n <local official admin-bundle zip>` upgrades a
 * capsule in place; user/, accounts, and foreign root files (drushrc.php,
 * .env) survive (the installer replaces only entries the package ships);
 * `official_gpm_only` stays enforced because the artefact is a LOCAL file —
 * this box never talks to getgrav.org (D-004 mirror-first; D-006: the admin
 * bundle carries the managed plugin set, the long tail stays client-side).
 *
 * Two consequences of that same installer shape drive everything below.
 * DirectInstallCommand::serve() returns 0 after a FAILED core install (the
 * failure is only printed, in red), so the exit code is never the verdict:
 * the printed output is read AND the capsule is asked afterwards what version
 * it now is. And Install::$ignores skips user/ entirely, so the core install
 * leaves the bundled plugins and theme at the version the capsule was born
 * with — the bundle refresh takes them from the same archive.
 */

/**
 * Validate: a Grav site, the enforced PHP, and a usable staged artefact.
 */
function drush_provision_grav_upgrade_validate($artefact = NULL) {
  if (!provision_platform_is_grav(d())) {
    return drush_set_error('PROVISION_GRAV_NOT_GRAV',
      dt('provision-grav-upgrade only applies to sites on Grav platforms.'));
  }
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_GRAV_NOT_GRAV',
      dt('provision-grav-upgrade runs on a SITE context (per-capsule upgrades, D-003).'));
  }
  if (_provision_grav_php_bin() === FALSE) {
    return drush_set_error('PROVISION_GRAV_PHP_TOO_OLD',
      dt('No PHP >= 8.3 binary found; Grav tasks refuse to run below the enforced floor.'));
  }
  if (!_provision_grav_site_exists()) {
    return drush_set_error('PROVISION_GRAV_SITE_NOT_FOUND',
      dt('No Grav capsule found at !site.', array('!site' => d()->site_path)));
  }
  // Clear a stale frontend backup-mode ctrl file before the safety copy runs:
  // a dead task's database-only mode would make the grav backup refuse.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }

  // The whole verdict before anything is written: the artefact is what its
  // name says it is, and it is newer than this capsule.
  $resolved = _provision_grav_resolve_artefact(_provision_grav_get_version(d()->site_path), $artefact);
  if ($resolved === FALSE) {
    return FALSE;
  }
  drush_set_option('grav_upgrade_artefact', $resolved['artefact']);
  drush_set_option('grav_upgrade_artefact_version', $resolved['version']);
}

/**
 * Safety copy before touching the capsule (the truthful grav mode).
 */
function drush_provision_grav_pre_provision_grav_upgrade($artefact = NULL) {
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_without_db_backup');
  }
  drush_set_option('selected_backup_mode', 'site_files_without_db_backup');
  drush_set_option('backup_mode', 'site_files_without_db_backup');
  drush_log(dt('Pre-upgrade safety copy (site_files_without_db_backup)'), 'info');
  drush_invoke('provision-backup');
}

/**
 * Run the in-place upgrade through the guarded CLI seam.
 */
function drush_provision_grav_upgrade($artefact = NULL) {
  $artefact = drush_get_option('grav_upgrade_artefact');
  $version = drush_get_option('grav_upgrade_artefact_version');

  drush_log(dt('Upgrading capsule !site from Grav @before to Grav @after using @artefact',
    array(
      '!site' => d()->site_path,
      '@before' => _provision_grav_get_version(d()->site_path),
      '@after' => $version,
      '@artefact' => basename($artefact),
    )), 'ok');

  // Seed, install, prove, refresh the bundle, clear the cache — the same
  // sequence the copy upgrade runs, so the two can never drift apart.
  return _provision_grav_apply_artefact(d()->site_path, d()->uri, $artefact, $version);
}

/**
 * Implements drush_hook_COMMAND_rollback() (collapsed name).
 *
 * Deliberately non-mutating. The capsule is upgraded IN PLACE, so there is no
 * copy to discard and no safe automatic reverse: a half-installed tree is
 * recovered from the pre-upgrade safety copy the pre hook took.
 *
 * Name that archive by its ON-DISK path. Registering it against the site node
 * is the front end's post-task step, and a task that errored never reaches
 * its post hooks, so the path is the only pointer that is true on every
 * failure; the front end may still register it from the task rollback, which
 * is why the Backups tab is offered as a possibility and not as the recovery.
 * Say exactly that, at the level operator notes are logged at, and touch
 * nothing.
 */
function drush_provision_grav_upgrade_rollback($artefact = NULL) {
  $archive = drush_get_option('backup_file', '');
  if (!is_string($archive) || $archive === '') {
    drush_log(dt('provision-grav-upgrade failed on !site. The capsule was not reverted automatically and is left exactly as the failed install left it. The pre-upgrade safety copy is the archive named in the backup rows above, under this account\'s backups directory.',
      array('!site' => d()->site_path)), 'advisory');
    return;
  }
  drush_log(dt('provision-grav-upgrade failed on !site. The capsule was not reverted automatically and is left exactly as the failed install left it, so it can be compared against the pre-upgrade safety copy at !archive. That archive is registered on the site, and listed on its Backups tab, when the task succeeds; on this path it may also appear there if the front end registers it from the task rollback. Restore it from the Backups tab when it is listed, and from the path above when it is not.',
    array('!site' => d()->site_path, '!archive' => $archive)), 'advisory');
}

/**
 * Re-verify the capsule (integrity + frontend reporting) after the upgrade.
 */
function drush_provision_grav_post_provision_grav_upgrade($artefact = NULL) {
  provision_backend_invoke(d()->name, 'provision-verify');
}
