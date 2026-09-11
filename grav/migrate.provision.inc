<?php
/**
 * @file
 * Provision hooks for the provision migrate command (Grav layer).
 *
 * FAIL-CLOSED. The GENERIC migrate is Drupal-shaped end to end (sites.php
 * records, a database move, the native files store) and none of it applies to
 * a self-contained capsule, so it is not implemented here -- and with the
 * shared Drupal hooks union-guarded away an unimplemented migrate would
 * SILENTLY NO-OP: a task that reports success having moved nothing. Refuse
 * loudly, and name the Grav commands that do the two jobs an operator
 * actually reaches for migrate to do: provision-grav-rename moves a site to a
 * new name on its own platform, provision-grav-copy-upgrade places an
 * upgraded copy on another Grav platform.
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
  // Defaults to the source's verdict, so an unresolvable target platform can
  // never manufacture a refusal (the clone twin's shape).
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
  return drush_set_error('PROVISION_GRAV_MIGRATE_UNSUPPORTED',
    dt('The generic migrate does not apply to Grav sites/platforms; refusing rather than silently doing nothing or migrating a Drupal site onto a foreign-CMS platform. Use the Rename task to move this site to a new name on its own platform, or Upgrade a copy to place an upgraded copy on another Grav platform.'));
}
