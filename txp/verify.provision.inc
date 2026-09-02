<?php
/**
 * @file
 * Code related to verify tasks (TXP layer).
 *
 * Fork of backdrop/verify.provision.inc, minus everything Drupal-lineage: no
 * drush bootstrap at any stage (TXP has no boot class -- every step here is
 * bootstrap-free by design), no o_contrib bundle machinery (no TXP analogue),
 * no makefile/composer platform builds (TXP platforms come from release
 * tarballs). Verify DEGRADES when the site DB is down: it is the operator's
 * heal action, so config re-render + tree/perms repair always run and only the
 * DB-side checks report unknown.
 */

/**
 * Provision verify command.
 */
function drush_provision_txp_provision_verify_validate() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type === 'site') {
    provision_prepare_environment();

    // Reload db creds from the persisted drushrc BEFORE re-rendering the
    // config, or the re-render blanks the DB connection.
    _provision_txp_ensure_db_creds_loaded();
    _provision_txp_create_settings_file();
    provision_drupal_push_site();

    drush_errors_on();
  }
}

/**
 * Implements drush_hook_pre_COMMAND().
 */
function drush_provision_txp_pre_provision_verify() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type === 'platform') {
    drush_log(dt("Checking for platform at path @path...", array('@path' => d()->root)), 'debug');
    if (!provision_file()->exists(d()->root)->status()) {
      return drush_set_error('PROVISION_TXP_PLATFORM_MISSING',
        dt('Platform path @path does not exist (TXP platforms are staged from release tarballs; no makefile builds).',
        array('@path' => d()->root)));
    }
    if (!is_dir(d()->root . '/sites/site1')) {
      drush_log(dt('Platform ships no sites/site1 skeleton; site installs on it will refuse (use the .tar.gz/.tar.xz archives -- the .zip has no /sites).'), 'warning');
    }
  }
}

/**
 * Implements drush_hook_COMMAND().
 */
function drush_provision_txp_provision_verify() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type === 'platform') {
    provision_file()->writable(d()->root . '/sites')
      ->succeed('Sites directory @path is writable')
      ->fail('Sites directory @path is not writable', 'PROVISION_SITES_DIR_NOT_WRITABLE');

    drush_set_option('sites', array_keys((array) provision_txp_find_sites()), 'drupal');
    drush_log(dt("This platform is running @short_name @version",
      array('@short_name' => 'Textpattern', '@version' => _provision_txp_get_version())), 'info');
    drush_set_option('packages', _scrub_object(provision_txp_find_packages()), 'drupal');

    // The TXP signal the front end keys on.
    drush_set_option('txp', _provision_txp_get_version(), 'drupal');

    provision_drupal_push_site();
  }
  elseif (d()->type === 'site') {
    _provision_txp_ensure_db_creds_loaded();

    // Tree/symlink/perms repair -- always runs, DB up or not.
    _provision_txp_create_directories();

    // Re-render the config (idempotent; the settings writer is the single
    // authority for private/config.php).
    _provision_txp_create_settings_file();

    // DB-side checks + the site package map (degrades to 'unknown' on a dead
    // DB rather than failing the heal path).
    drush_include_engine('txp', 'verify');

    drush_set_option('packages', _scrub_object(provision_txp_system_map()), 'site');

    provision_drupal_push_site(drush_get_option('override_slave_authority', FALSE));
  }
}

/**
 * Implements drush_hook_provision_post_COMMAND().
 */
function drush_provision_txp_post_provision_verify() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type === 'site') {
    drush_set_option('installed', TRUE, 'site');

    // No client symlink for this site: a Grav capsule / Textpattern site is the
    // whole codebase, so linking it under ~/clients/<client>/ would hand a
    // per-client shell sub-account full code and database access there, against
    // the files-only policy those accounts have on Drupal sites. Drop a link an
    // earlier release created; the sub-account's lshell path skips these too.
    _provision_client_delete_symlink();

    // Per-site BOA control INI, exactly like every other CMS on the stack
    // (sites/<uri>/modules is inert to TXP itself).
    $iniFile = d()->site_path . '/modules/default.boa_site_control.ini';
    $iniTpl = '/data/conf/default.boa_site_control.ini';
    if (!provision_file()->exists($iniFile)->status() && provision_file()->exists($iniTpl)->status()) {
      provision_file()->copy($iniTpl, $iniFile);
    }
  }
}
