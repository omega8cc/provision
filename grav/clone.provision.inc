<?php
/**
 * @file
 * Provision hooks for the provision clone command (Grav layer).
 *
 * FAIL-CLOSED. Clone is deferred past phase 1 (D-002: Clone/Migrate/Rename
 * later). The shared Drupal clone hooks are union-guarded away, but clone
 * internally drives the deploy chain -- so on a Grav site it would neither
 * cleanly no-op nor cleanly work: a half-run through the grav deploy hooks
 * against un-renamed capsule surfaces (the rename wave carries the per-site
 * URI surfaces; the capsule model adds its own). Refuse loudly until the
 * real engine lands.
 */

function drush_provision_grav_provision_clone_validate($new_uri = NULL, $platform = NULL) {
  // Refuse on EITHER side, same shape as the migrate guard: a Grav source
  // and a foreign TARGET platform (nothing else stops a Drupal site being
  // cloned ONTO a Grav platform).
  $source_is_grav = provision_platform_is_grav(d());
  // The TARGET test must be THIS CMS's own predicate, never the foreign-CMS
  // union: every CMS layer ships in the same provision tree and every
  // validate hook fires on every clone/migrate, so a union target test makes
  // this refusal swallow another CMS's supported task and answer with a
  // GRAV-flavoured message. Observed live: a Textpattern-to-Textpattern clone, which the TXP layer
  // implements and tests, was refused here as an unsupported Grav clone.
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
  return drush_set_error('PROVISION_GRAV_CLONE_UNSUPPORTED',
    dt('Clone is not implemented for Grav sites/platforms yet; refusing rather than half-running the deploy chain. A capsule can be copied to a new URI via Backup on the source and Restore on a site created at the new name (spike-4-proven).'));
}
