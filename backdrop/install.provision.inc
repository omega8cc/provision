<?php
/**
 * @file
 *   Provision hooks for the provision install command (Backdrop layer).
 *
 * Fork of platform/install.provision.inc: keeps the CMS-agnostic
 * force-reinstall/db-drop scaffolding, replaces the Drupal backend-invoke /
 * site-local-Drush install regimes with a Backdrop install engine (a
 * core/scripts/install.sh shell-out, see backdrop/install.inc).
 */

/**
 * Check that we are trying to install a new site, and a new site only.
 */
function drush_provision_backdrop_provision_install_validate() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }

  if (!d()->uri) {
    return drush_set_error("PROVISION_URL_REQUIRED", dt("You need to specify a valid url to install a site"));
  }
  if (_provision_drupal_site_exists()) {

    // If "force-reinstall" option is set, delete the database and files.
    if (drush_get_option('force-reinstall', FALSE)) {

      drush_log(dt('Forcing reinstall...'), 'ok');

      // Drop the database if we can figure out what it is.
      // If last install failed, the database and the site folder is deleted.
      if (file_exists(d()->site_path . '/drushrc.php')) {

        // Load the current database name from drushrc.php.
        require_once(d()->site_path . '/drushrc.php');
        $old_db_name = $options['db_name'];

        if (d()->service('db')->database_exists($old_db_name)) {
          d()->service('db')->drop_database($old_db_name);
          drush_log(dt('Dropped database @database.', array(
            '@database' => $old_db_name,
          )), 'ok');
        }
      }

      // Destroy site_path.
      if (file_exists(d()->site_path)) {
        provision_file()->chmod(d()->site_path, 0755, TRUE);
        _provision_recursive_delete( d()->site_path );
        drush_log(dt('Deleted @site_path.', array('@site_path' => d()->site_path)), 'ok');
      }
    }

    // Check again if site does not exist after the forced reinstall.
    if (_provision_drupal_site_exists()) {
      return drush_set_error('PROVISION_SITE_INSTALLED',  dt('The file !file still exists. Provision was unable to delete it.', array(
        '!file' => d()->site_path . '/settings.php',
      )));
    }
  }
}

/**
 * Set up the directories and settings.php file that we need.
 */
function drush_provision_backdrop_pre_provision_install() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  // This is the actual Backdrop provisioning requirements, including the
  // file-based config dirs.
  _provision_backdrop_create_directories();
}

/**
 * If the install went south, clean up behind ourselves.
 */
function drush_provision_backdrop_pre_provision_install_rollback() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  provision_file()->chmod(d()->site_path, 0755);
  _provision_recursive_delete( d()->site_path );
  drush_invoke_process('@none', 'provision-save', array(d()->name), array('delete' => TRUE));
  foreach (d()->drush_aliases as $alias) {
    drush_invoke_process('@none', 'provision-save', array($alias), array('delete' => TRUE));
  }
}

/**
 * Install Backdrop with the pre-configured settings.
 *
 * The Backdrop install engine shells out to core/scripts/install.sh (its own PHP
 * process), so -- unlike the Drupal install-backend subprocess and the D11
 * site-local Drush dance -- there is no in-process multi-connection hazard, no
 * psr/log patch-revert, and no need to bootstrap the site here first.
 */
function drush_provision_backdrop_provision_install() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  // Purge any cache namespace left in the shared Valkey/Redis store by a
  // previously deleted site with the same URI, BEFORE the install engine runs
  // any site code. Backdrop namespaces by DB coordinates and Aegir mints the
  // db name deterministically from the URI, so a same-name recreate lands in
  // the deleted predecessor's namespace exactly like the Drupal case. The db
  // service's pre hooks have set the db credentials by now (the settings file
  // below is written from them), so the derived namespace is the one the new
  // site will use. Non-fatal by design.
  _provision_valkey_purge_backdrop_site(d()->uri);
  // Requires at least the database settings to complete.
  provision_prepare_environment();
  _provision_backdrop_create_settings_file();
  provision_drupal_push_site($override_slave_authority = TRUE);
  provision_save_site_data();

  // Install site according to install_method.
  drush_log(dt('Installing site with the "!method" method.', array(
    '!method' => d()->install_method,
  )), 'ok');

  // Run Backdrop installation if option was specified. The hosting_backdrop
  // front-end stamps sites on marker-verified Backdrop platforms with
  // install_method 'backdrop' (HOSTING_BACKDROP_INSTALL_METHOD, set in its
  // hook_hosting_TASK_OBJECT_context_options); site nodes carry no persistent
  // install_method column, so that is the value every front-end-created
  // Backdrop site arrives with. It is the same automated profile-driven
  // install -- only the label differs -- so both values drive the bee engine.
  if (d()->install_method == 'profile' || d()->install_method == 'backdrop') {
    // Drive the install via bee (its own subprocess) and capture the one-time
    // login link. Deliberately NO in-process drush_bootstrap(DRUPAL_FULL) here:
    // an in-process BackdropBoot FULL bootstrap layered onto this parent process
    // reuses a stale DB handle and fatals with "Base table or view not found",
    // rolling the whole install back even though the freshly installed site is
    // fine (it serves and bootstraps cleanly in any fresh process). Instead
    // post_provision_install flushes caches via a fresh bee subprocess and reads
    // the {system}/{state} maps straight from the DB through the provision DB
    // service -- no CMS bootstrap in this process at all.
    drush_include_engine('backdrop', 'install');
    drush_set_option('profile_installed', TRUE, 'site');
  }
  // If install_method is 'manual', do nothing but point at the installer.
  elseif (d()->install_method == 'manual') {
    drush_set_option('login_link', 'http://' . d()->uri . '/core/install.php');
  }
  // Fail closed on anything else: silently skipping the engine while still
  // marking the site installed mints an ENABLED shell site (empty database,
  // empty active config) whose every later deploy -- migrate, rename, clone
  // restore -- dies deep in Backdrop's bootstrap. Erroring here rolls the
  // install back loudly instead.
  else {
    return drush_set_error('PROVISION_BACKDROP_INSTALL_METHOD_UNKNOWN',
      dt('Unknown install_method "!method" for !uri; refusing to mark the site installed.',
      array('!method' => d()->install_method, '!uri' => d()->uri)));
  }

  drush_set_option('installed', TRUE, 'site');
  _provision_backdrop_maintain_aliases();
}

/**
 * Finish the installation, regenerate the caches and save enabled packages.
 */
function drush_provision_backdrop_post_provision_install() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }

  // If a Backdrop profile was installed, rebuild caches and save the package
  // map. 'backdrop' is the front-end's label for the same profile-driven
  // install (see drush_provision_backdrop_provision_install()).
  if ((d()->install_method == 'profile' || d()->install_method == 'backdrop')
    && drush_get_option('profile_installed', FALSE, 'site')) {
    // Backdrop bootstraps under Aegir Drush 8 (BackdropBoot); rebuild caches
    // in-process, then record the package map + cron key for the front end.
    _provision_backdrop_rebuild_caches();
    drush_set_option('packages', _scrub_object(provision_backdrop_system_map()), 'site');
    _provision_client_create_symlink();
    drush_include_engine('backdrop', 'cron_key');
  }
  // For all other install methods, just create the symlink.
  else {
    _provision_client_create_symlink();
  }

  // Native files symlinking for the freshly installed site (see the Drupal layer
  // for the full rationale). The nginx vhost is not written until the post-install
  // verify, so pass the account explicitly. Warn-not-fail.
  _provision_drupal_native_symlink(d()->uri, FALSE, basename(d('@server_master')->aegir_root));
}
