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

  if (empty($artefact)) {
    $artefact = _provision_grav_newest_artefact();
    if ($artefact === FALSE) {
      return drush_set_error('PROVISION_GRAV_NO_ARTEFACT',
        dt('No staged official release found under <aegir_root>/static/grav-releases/ and no artefact argument given.'));
    }
  }
  if (!is_readable($artefact) || !preg_match('/grav-admin-v[0-9][^\/]*\.zip$/', basename($artefact))) {
    return drush_set_error('PROVISION_GRAV_NO_ARTEFACT',
      dt('Artefact @file is not a readable official grav-admin-v*.zip.', array('@file' => $artefact)));
  }
  drush_set_option('grav_upgrade_artefact', $artefact);
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
  $before = _provision_grav_get_version(d()->site_path);

  drush_log(dt('Upgrading capsule !site from Grav @before using @artefact',
    array('!site' => d()->site_path, '@before' => $before, '@artefact' => basename($artefact))), 'ok');

  if (!_provision_grav_cli('gpm', 'direct-install -y -n ' . escapeshellarg($artefact))) {
    return drush_set_error('PROVISION_GRAV_UPGRADE_FAILED',
      dt('gpm direct-install failed for !site; the pre-upgrade safety copy is registered and restorable.',
      array('!site' => d()->site_path)));
  }

  // NEVER chain a second install in the same CLI process (Install::prepare()
  // throws on a loaded Grav\Installer class) -- the cache warm below is a
  // separate invocation by construction.
  _provision_grav_cli('grav', 'clearcache -n');

  $after = _provision_grav_get_version(d()->site_path);
  if ($after === '') {
    return drush_set_error('PROVISION_GRAV_UPGRADE_FAILED',
      dt('Post-upgrade capsule at !site has no readable GRAV_VERSION; restore the safety copy.',
      array('!site' => d()->site_path)));
  }
  drush_log(dt('Capsule !site now runs Grav @after (was @before).',
    array('!site' => d()->site_path, '@after' => $after, '@before' => $before)),
    $after === $before ? 'info' : 'success');
}

/**
 * Re-verify the capsule (integrity + frontend reporting) after the upgrade.
 */
function drush_provision_grav_post_provision_grav_upgrade($artefact = NULL) {
  provision_backend_invoke(d()->name, 'provision-verify');
}
