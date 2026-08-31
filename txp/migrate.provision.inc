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
  if (!provision_platform_is_txp(d())) {
    return;
  }
  return drush_set_error('PROVISION_TXP_MIGRATE_UNSUPPORTED',
    dt('Migrate/rename is not implemented for Textpattern sites yet; refusing rather than silently doing nothing. Use Clone to copy the site to a new name/platform.'));
}
