<?php
/**
 * @file
 * Code related to verify tasks (Grav layer).
 *
 * Fork of txp/verify.provision.inc minus the DB half entirely: a phase-1 Grav
 * site has no database, so verify is capsule integrity (vendor/ present,
 * version readable), tree/perms repair on the group model, the .env re-render,
 * and the frontend reporting contract (sites + packages + the 'grav' signal --
 * re-reported EVERY run or the per-platform instance-sync wipe removes the
 * synthetic profile, boa-grav R8).
 */

/**
 * Provision verify command.
 */
function drush_provision_grav_provision_verify_validate() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type === 'site') {
    provision_prepare_environment();
    provision_drupal_push_site();
    drush_errors_on();
  }
}

/**
 * Implements drush_hook_pre_COMMAND().
 */
function drush_provision_grav_pre_provision_verify() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type === 'platform') {
    drush_log(dt("Checking for platform at path @path...", array('@path' => d()->root)), 'debug');
    if (!provision_file()->exists(d()->root)->status()) {
      return drush_set_error('PROVISION_GRAV_PLATFORM_MISSING',
        dt('Platform path @path does not exist (Grav platforms are staged from official release archives; no makefile builds).',
        array('@path' => d()->root)));
    }
    if (!is_readable(d()->root . '/vendor/autoload.php')) {
      return drush_set_error('PROVISION_GRAV_SEED_INVALID',
        dt('Platform @path has no vendor/autoload.php -- not a usable seed (stage the official admin-bundle archive; a vendor-less tree turns any CLI entry into an unpinned network composer run).',
        array('@path' => d()->root)));
    }
  }
}

/**
 * Implements drush_hook_COMMAND().
 */
function drush_provision_grav_provision_verify() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type === 'platform') {
    if (!is_dir(d()->root . '/sites')) {
      provision_file()->mkdir(d()->root . '/sites')
        ->succeed('Created <code>@path</code>')
        ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
    }
    provision_file()->writable(d()->root . '/sites')
      ->succeed('Sites directory @path is writable')
      ->fail('Sites directory @path is not writable', 'PROVISION_SITES_DIR_NOT_WRITABLE');

    drush_set_option('sites', array_keys((array) provision_grav_find_sites()), 'drupal');
    drush_log(dt("This platform is running @short_name @version",
      array('@short_name' => 'Grav', '@version' => _provision_grav_get_version())), 'info');
    drush_set_option('packages', _scrub_object(provision_grav_find_packages()), 'drupal');

    // The Grav signal the front end keys on.
    drush_set_option('grav', _provision_grav_get_version(), 'drupal');

    provision_drupal_push_site();
  }
  elseif (d()->type === 'site') {
    // Capsule tree/perms repair + .env re-render -- always runs (verify is the
    // operator's heal action).
    _provision_grav_create_directories();

    // Capsule integrity + version (bootstrap-free).
    drush_include_engine('grav', 'verify');

    drush_set_option('packages', _scrub_object(provision_grav_system_map()), 'site');

    provision_drupal_push_site(drush_get_option('override_slave_authority', FALSE));
  }
}

/**
 * Implements drush_hook_provision_post_COMMAND().
 */
function drush_provision_grav_post_provision_verify() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type === 'site') {
    drush_set_option('installed', TRUE, 'site');

    _provision_client_create_symlink();

    // Per-site BOA control INI, exactly like every other CMS on the stack
    // (sites/<uri>/modules is inert to Grav itself; the Grav vhost denies the
    // path outright).
    $iniFile = d()->site_path . '/modules/default.boa_site_control.ini';
    $iniTpl = '/data/conf/default.boa_site_control.ini';
    if (!provision_file()->exists($iniFile)->status() && provision_file()->exists($iniTpl)->status()) {
      provision_file()->copy($iniTpl, $iniFile);
    }
  }
}
