<?php

/**
 * Migrate command guard (Backdrop layer).
 *
 * There is no Backdrop migrate implementation yet. The D-lineage orchestration
 * in platform/migrate.provision.inc early-returns on a Backdrop source, but
 * the db/http service hooks (http/migrate.provision.inc delete_config('site'),
 * db/migrate.provision.inc destroy_site_database()) carry no Backdrop guard,
 * so a Backdrop-source migrate would still delete the source vhost and drop
 * the source database around fully no-op platform hooks — a destructive no-op.
 * A Backdrop TARGET is equally unsupported: the D7 -> Backdrop upgrade is a
 * separate clone-preserving command (provision-backdrop-upgrade) that never
 * dispatches provision-migrate.
 *
 * This validate hook therefore fails the command closed whenever the source
 * OR the target platform is Backdrop — a validate-phase error aborts the run
 * before any pre/post service hook can act. Unlike the other files in this
 * layer it must NOT early-return on a non-Backdrop source: the target check
 * has to run for Drupal sources too.
 */

/**
 * Implements drush_hook_COMMAND_validate() for provision-migrate.
 *
 * @param string $platform
 *   The target platform context name — the command's first positional
 *   argument (see drush_provision_drupal_provision_migrate_validate()).
 */
function drush_provision_backdrop_provision_migrate_validate($platform = NULL) {
  $source_is_backdrop = provision_platform_is_backdrop(d());
  $target_is_backdrop = FALSE;
  if (is_string($platform) && $platform !== '') {
    // d() on an unknown name yields a context without a usable root, for
    // which provision_platform_is_backdrop() returns FALSE — migrate itself
    // rejects unknown targets later.
    $target_is_backdrop = provision_platform_is_backdrop(d($platform));
  }
  if ($source_is_backdrop || $target_is_backdrop) {
    drush_set_error('PROVISION_BACKDROP_MIGRATE_UNSUPPORTED',
      dt('provision-migrate does not support Backdrop platforms (source is !src; target is !tgt). For Drupal 7 -> Backdrop upgrades use provision-backdrop-upgrade; Backdrop-to-Backdrop migrate is not implemented yet.',
      array(
        '!src' => $source_is_backdrop ? 'Backdrop' : 'not Backdrop',
        '!tgt' => $target_is_backdrop ? 'Backdrop' : 'not Backdrop',
      )));
  }
}
