<?php
/**
 * @file
 * Provision hooks for the provision migrate command (Grav layer).
 *
 * FAIL-CLOSED. Migrate (platform move / rename) is deferred past phase 1
 * (D-002: Clone/Migrate/Rename later), and with the shared Drupal hooks
 * union-guarded away an unimplemented migrate would SILENTLY NO-OP -- a task
 * that reports success having moved nothing (the silent-success class the
 * cross-track round-2 review confirmed live on the TXP side). Refuse loudly
 * instead until the real engine lands.
 */

function drush_provision_grav_provision_migrate_validate($platform = NULL) {
  // Refuse on EITHER side: a Grav source (this hook's own context) and a
  // foreign TARGET platform. Guarding only the source would let a Drupal
  // site migrate ONTO a Grav platform -- the shared Drupal validate returns
  // early only when the SITE is foreign, so nothing else would stop it.
  $source_is_grav = provision_platform_is_grav(d());
  // The TARGET test must be THIS CMS's own predicate, never the foreign-CMS
  // union: every CMS layer ships in the same provision tree and every
  // validate hook fires on every clone/migrate, so a union target test makes
  // this refusal swallow another CMS's supported task and answer with a
  // GRAV-flavoured message. Each CMS layer refuses its own platforms; the others refuse theirs.
  $target_is_grav = FALSE;
  if (!empty($platform)) {
    $target = d($platform);
    if (is_object($target) && isset($target->root)) {
      $target_is_grav = provision_platform_is_grav($target->root);
    }
  }
  if (!$source_is_grav && !$target_is_grav) {
    return;
  }
  return drush_set_error('PROVISION_GRAV_MIGRATE_UNSUPPORTED',
    dt('Migrate/rename is not implemented for Grav sites/platforms yet; refusing rather than silently doing nothing or migrating a Drupal site onto a foreign-CMS platform. A capsule can be copied to a new URI via Backup on the source and Restore on a site created at the new name (spike-4-proven).'));
}
