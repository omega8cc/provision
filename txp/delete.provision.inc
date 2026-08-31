<?php
/**
 * @file
 * Provision hooks for the provision delete command (TXP layer).
 *
 * Phase 1 carries ONE hook: the platform-delete refusal. The shared platform
 * delete refuses only when provision_drupal_find_sites() (a settings.php glob)
 * finds sites -- TXP site dirs have no settings.php, so without this guard a
 * populated TXP platform would recursively delete with no refusal. Site-level
 * TXP delete (backup-first etc.) lands with the backup/restore engines.
 */

/**
 * Refuse to delete a TXP platform that still carries sites.
 *
 * Validate-phase on purpose: an error here aborts the whole command before any
 * implementation hook (including the shared recursive delete) can run.
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
