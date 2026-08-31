<?php
/**
 * @file
 * Provision hooks for the provision delete command (Grav layer).
 *
 * Phase 1 carries ONE hook: the platform-delete refusal (boa-grav R0, the
 * capsule model's one new structural hazard). The shared platform delete
 * refuses only when provision_drupal_find_sites() (a settings.php glob) finds
 * sites -- Grav capsules have no settings.php, so without this guard a
 * populated Grav platform would recursively delete every live capsule with no
 * refusal. Site-level capsule delete (backup-first etc.) lands with the
 * backup/restore engines.
 */

/**
 * Refuse to delete a Grav platform that still carries capsules.
 *
 * Validate-phase on purpose: an error here aborts the whole command before any
 * implementation hook (including the shared recursive delete) can run.
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
