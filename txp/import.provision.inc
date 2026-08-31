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

  // An adopted site must not keep a working browser installer on disk: today
  // only the vhost's nested `\.php$ -> 404` blocks it, so a single nginx
  // regression would expose a live re-install endpoint.
  $site_setup = d()->site_path . '/admin/setup';
  if (is_dir($site_setup)) {
    _provision_recursive_delete($site_setup);
    drush_log(dt('Removed the spent per-site admin/setup directory from the adopted site.'), 'ok');
  }
}

function drush_provision_txp_post_provision_import() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type !== 'site') {
    return;
  }
  drush_set_option('packages', _scrub_object(provision_txp_system_map()), 'site');

  // Persist the adopted site's alias + drushrc and run the registration verify
  // (the precedent's post-import tail; without it an imported site lands with
  // no alias record and never gets its first verify pass).
  d()->write_alias();
  provision_save_site_data();
  drush_set_option('provision_save_config', FALSE);
  provision_backend_invoke(d()->name, 'provision-verify');
  provision_reload_config('site', d()->site_path . '/drushrc.php');
}
