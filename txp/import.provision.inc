<?php
/**
 * @file
 * Provision hooks for the provision import command (TXP layer).
 *
 * Adopt an existing multisite-layout TXP site: assert the on-disk shape and
 * the DB schema, refresh the tree/config through the standard repair path,
 * and emit the package map. Single-site-layout conversion is out of scope
 * (map open question 4).
 */

function drush_provision_txp_provision_import() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type !== 'site') {
    return;
  }
  if (!_provision_txp_site_exists()) {
    return drush_set_error('PROVISION_TXP_IMPORT_NO_SITE',
      dt('No multisite TXP site found at @path (private/config.php missing).',
      array('@path' => d()->site_path)));
  }
  _provision_txp_ensure_db_creds_loaded();
  // Schema presence + version via the verify engine (degrades on a dead DB).
  drush_include_engine('txp', 'verify');
  drush_set_option('installed', TRUE, 'site');
}

function drush_provision_txp_post_provision_import() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type !== 'site') {
    return;
  }
  drush_set_option('packages', _scrub_object(provision_txp_system_map()), 'site');
}
