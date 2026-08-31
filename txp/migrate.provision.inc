<?php
/**
 * @file
 * Provision hooks for the provision migrate command (TXP layer).
 *
 * Round-2 state: FAIL-CLOSED. Migrate (platform move / rename) is not built
 * yet, and with the shared Drupal hooks union-guarded away an unimplemented
 * migrate would SILENTLY NO-OP — a task that reports success having moved
 * nothing. Refuse loudly instead until the real engine lands (map §2.1
 * item 7 rename-as-path-move semantics; phase-1 migrate is restricted to
 * same-core platforms by design either way).
 */

function drush_provision_txp_provision_migrate_validate($platform = NULL) {
  // Refuse on EITHER side: a TXP source (this hook's own context) and a
  // TXP/foreign TARGET platform. Guarding only the source would let a Drupal
  // site migrate ONTO a TXP platform -- the shared Drupal validate returns
  // early only when the SITE is foreign, so nothing else would stop it.
  $source_is_txp = provision_platform_is_txp(d());
  $target_is_foreign = FALSE;
  if (!empty($platform)) {
    $target = d($platform);
    if (is_object($target) && isset($target->root)) {
      $target_is_foreign = provision_platform_is_foreign_cms($target->root);
    }
  }
  if (!$source_is_txp && !$target_is_foreign) {
    return;
  }
  return drush_set_error('PROVISION_TXP_MIGRATE_UNSUPPORTED',
    dt('Migrate/rename is not implemented for Textpattern sites/platforms yet; refusing rather than silently doing nothing or migrating a Drupal site onto a foreign-CMS platform. Use Clone to copy the site to a new name/platform.'));
}
