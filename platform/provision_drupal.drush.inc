<?php
/**
 * @file
 * Drupal specific functions for the provisioning framework.
 *
 * This module is responsible for the creation and maintenance of the drupal settings.php file, the sites directory structure
 * and all the install api code.
 */

function provision_drupal_drush_help($section) {
  switch ($section) {
    case 'error:PROVISION_CONFIG_NOT_VALID' :
      return dt('Config file could not be loaded.');
    case 'error:PROVISION_DRUPAL_SITE_INSTALLED' :
      return dt('Site has already been installed.');
    case 'error:PROVISION_DRUPAL_SITE_NOT_FOUND' :
      return dt('Site was not found.');
    case 'error:PROVISION_DRUPAL_INSTALL_FAILED' :
      return dt('Could not complete Drupal installation.');
    case 'error:PROVISION_DRUPAL_UPDATE_FAILED' :
      return dt('Could not complete Drupal update.');
    case 'error:PROVISION_BACKUP_PATH_NOT_FOUND' :
      return dt("Backup directory does not exist.");
    case 'error:PROVISION_DRUPAL_INSTALL_MISSING_REQUIREMENTS' :
      return dt("Could not meet the requirements for installing the drupal profile");
    case 'error:PROVISION_REQUIRES_URL' :
      return dt('You need to specify the URL argument for this command');
  }

}

function provision_drupal_drush_engine_drupal() {
  $engines = array();
  $engines['clear'] = array();
  $engines['cron_key'] = array();
  $engines['deploy'] = array();
  $engines['import'] = array();
  $engines['install'] = array();
  $engines['packages'] = array();
  $engines['verify'] = array();
  return $engines;
}


/**
 * Finalize the platform / site
 *
 * This will run only if there were no errors in any of the previous hooks,
 * and will allow us to cache the successful settings to the site.php/ drushrc.php
 * files for future runs.
 */
function provision_drupal_drush_exit() {
  $command = drush_get_command();
  $command = explode(" ", $command['command']);

  if (preg_match("/^provision-/", $command[0]) && drush_get_option('provision_save_config', TRUE)) {

    // Add platform root auto-discovery to avoid confusing
    // Composer based D8+ codebase root with Drupal real root.
    d()->root = provision_auto_fix_platform_root(d()->root);

    if (d()->type === 'site') {
      // The two permission branches below are Drupal-shaped end to end
      // (sites/all/{modules,themes,libraries}, local.settings.php,
      // civicrm.settings.php) and none of those paths exist on a foreign-CMS
      // platform, so every TXP/Grav site verify logged three chmod failures.
      // Guard the BRANCHES, never the whole exit hook: it also calls
      // provision_save_site_data() / provision_save_platform_data() below,
      // which write the drushrc.php files a foreign-CMS layer boots from.
      $foreign_cms = provision_platform_is_foreign_cms(d());
      if (!$foreign_cms && preg_match("/^provision-login-reset/", $command[0])) {
        // Make site and platform directories world writable temporarily,
        // until the next site verify, but only if apps module and
        // control file both exist.
        $ctrl_modules_apps = d()->root . '/sites/all/modules/apps-allow.info';
        if (provision_file()->exists($ctrl_modules_apps)->status()) {
          $profile = drush_get_option('profile');
          $site_modules_apps = d()->site_path . '/modules/apps/apps.module';
          $allp_modules_apps = d()->root . '/sites/all/modules/apps/apps.module';
          $dist_modules_apps = d()->root . '/profiles/' . $profile . '/modules/apps/apps.module';
          $dist_contrib_apps = d()->root . '/profiles/' . $profile . '/modules/contrib/apps/apps.module';
          if (provision_file()->exists($site_modules_apps)->status() ||
              provision_file()->exists($allp_modules_apps)->status() ||
              provision_file()->exists($dist_modules_apps)->status() ||
              provision_file()->exists($dist_contrib_apps)->status()) {
            provision_file()->chmod(d()->site_path, 0777)
              ->succeed('Changed permissions of <code>@path</code> to @perm')
              ->fail('Could not change permissions of <code>@path</code> to @perm');
            provision_file()->chmod(d()->root . '/sites/all/modules', 0777)
              ->succeed('Changed permissions of <code>@path</code> to @perm')
              ->fail('Could not change permissions of <code>@path</code> to @perm');
            provision_file()->chmod(d()->root . '/sites/all/themes', 0777)
              ->succeed('Changed permissions of <code>@path</code> to @perm')
              ->fail('Could not change permissions of <code>@path</code> to @perm');
            provision_file()->chmod(d()->root . '/sites/all/libraries', 0777)
              ->succeed('Changed permissions of <code>@path</code> to @perm')
              ->fail('Could not change permissions of <code>@path</code> to @perm');
            provision_file()->chmod(d()->root . '/sites/all', 0777)
              ->succeed('Changed permissions of <code>@path</code> to @perm')
              ->fail('Could not change permissions of <code>@path</code> to @perm');
          }
        }
        // Make local.settings.php file group writable temporarily,
        // until the next site verify, but only if control file exists.
        $ctrl_local_settings = d()->site_path . '/modules/local-allow.info';
        $local_settings = d()->site_path . '/local.settings.php';
        if (provision_file()->exists($ctrl_local_settings)->status() &&
            provision_file()->exists($local_settings)->status()) {
          provision_file()->chmod($local_settings, 0660)
            ->succeed('Changed permissions of <code>@path</code> to @perm')
            ->fail('Could not change permissions of <code>@path</code> to @perm');
        }
        // Make civicrm.settings.php file group writable temporarily,
        // until the next site verify, but only if control file exists.
        $ctrl_local_settings = d()->site_path . '/modules/local-allow.info';
        $local_settings = d()->site_path . '/civicrm.settings.php';
        if (provision_file()->exists($ctrl_local_settings)->status() &&
            provision_file()->exists($local_settings)->status()) {
          provision_file()->chmod($local_settings, 0660)
            ->succeed('Changed permissions of <code>@path</code> to @perm')
            ->fail('Could not change permissions of <code>@path</code> to @perm');
        }
      }
      elseif (!$foreign_cms && preg_match("/^provision-verify/", $command[0])) {
        // Revert site and platform directories permissions to default.
        provision_file()->chmod(d()->site_path, 0755)
          ->succeed('Changed permissions of <code>@path</code> to @perm')
          ->fail('Could not change permissions of <code>@path</code> to @perm');
        if (!provision_is_hostmaster_site()) {
          provision_file()->chmod(d()->root . '/sites/all/modules', 02775)
            ->succeed('Changed permissions of <code>@path</code> to @perm')
            ->fail('Could not change permissions of <code>@path</code> to @perm');
          provision_file()->chmod(d()->root . '/sites/all/themes', 02775)
            ->succeed('Changed permissions of <code>@path</code> to @perm')
            ->fail('Could not change permissions of <code>@path</code> to @perm');
          provision_file()->chmod(d()->root . '/sites/all/libraries', 02775)
            ->succeed('Changed permissions of <code>@path</code> to @perm')
            ->fail('Could not change permissions of <code>@path</code> to @perm');
        }
        provision_file()->chmod(d()->root . '/sites/all', 0755)
          ->succeed('Changed permissions of <code>@path</code> to @perm')
          ->fail('Could not change permissions of <code>@path</code> to @perm');
        // Tenant composer codebases (~/static, D8+): core's composer scaffold,
        // run by the shell user, rewrites its files under sites/ and needs a
        // writable parent; keep the owner and the x-only bit for others
        // (omega8cc/boa#1936). Mirrors fix-drupal-platform-permissions.sh.
        $sites_perm = 0751;
        if (preg_match("/\/static\//", d()->root) &&
            provision_file()->exists(d()->root . '/core/lib/Drupal.php')->status()) {
          $sites_perm = 02771;
        }
        // The docroot is group-writable, so sites/ is a name a tenant can
        // plant as a symlink; chmod() would follow it. Never legitimate.
        if (!is_link(d()->root . '/sites')) {
          provision_file()->chmod(d()->root . '/sites', $sites_perm)
            ->succeed('Changed permissions of <code>@path</code> to @perm')
            ->fail('Could not change permissions of <code>@path</code> to @perm');
        }
        // Revert local.settings.php permissions to default.
        $local_settings = d()->site_path . '/local.settings.php';
        if (provision_file()->exists($local_settings)->status()) {
          provision_file()->chmod($local_settings, 0440)
            ->succeed('Changed permissions of <code>@path</code> to @perm')
            ->fail('Could not change permissions of <code>@path</code> to @perm');
        }
        // Revert civicrm.settings.php permissions to default.
        $local_settings = d()->site_path . '/civicrm.settings.php';
        if (provision_file()->exists($local_settings)->status()) {
          provision_file()->chmod($local_settings, 0640)
            ->succeed('Changed permissions of <code>@path</code> to @perm')
            ->fail('Could not change permissions of <code>@path</code> to @perm');
        }
        // Delete solr.php to avoid outdated leftovers on clone etc.
        // $solr_info = d()->site_path . '/solr.php';
        // if (provision_file()->exists($solr_info)->status()) {
        //   _provision_recursive_delete($solr_info);
        //   drush_log(dt('Existing solr.php removed: !solrinfo', array('!solrinfo' => $solr_info)), 'message');
        // }
      }
      if (drush_get_option('installed')) {
        // Don't generate the drushrc.php on provision-save/delete commands.
        if (!preg_match("/^provision-(save|delete)/", $command[0])) {
          provision_save_site_data();
        }
      }
    }
    elseif (d()->type === 'platform') {
      // Don't generate the drushrc.php on provision-save/delete/git-clone commands.
      // Avoiding running on git-clone is needed in order to let it work with makefiles in the git repo.
      // If this function runs, the platform directory is created, and drush make fails because the directory already exists.
      // @TODO: Determine what tasks should save platform data and only act on those.
      if (!preg_match("/^provision-(save|delete|git-clone)/", $command[0])) {
        provision_save_platform_data();
      }
    }
  }
}

/**
 * Test to see if the site settings.php exists
 *
 * @return
 *   If the file exists, return TRUE, else return FALSE.
 */
function _provision_drupal_site_exists() {
  return is_readable(d()->site_path . '/settings.php');
}

/**
 * This command does the actual installation in it's own thread,
 * so we can recover gracefully if things go really wrong.
 */
function drush_provision_drupal_provision_install_backend() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  drush_include_engine('drupal', 'install');
}

/**
 * Sync the current Drupal platform and, if applicable, site. Call after
 * finishing operations that affect the filesystem.
 *
 * @param boolean $override_slave_authority
 *    Overwrite e.g. the files directory on the slave.
 *    Useful when the master server has donw file operations, such as restoring a backup.
 */
function provision_drupal_push_site($override_slave_authority = FALSE) {

  provision_file()->create_dir(d()->server->http_platforms_path, dt("Platforms"), 0755);
  d()->server->sync(d()->server->http_platforms_path, array(
    'exclude' => d()->server->http_platforms_path . '/*',  // Make sure remote directory is created
  ));

  // Add platform root auto-discovery to avoid confusing
  // Composer based D8+ codebase root with Drupal real root.
  d()->root = provision_auto_fix_platform_root(d()->root);

  // Sync the platform
  $sync_path = d()->root;
  drush_command_invoke_all_ref('provision_platform_sync_path_alter', $sync_path);
  d()->service('http')->sync($sync_path, array('exclude-sites' => TRUE));

  if (d()->type === 'site') {
    // Check whether we're hosted on a cluster, in which case, the master is
    // authoritative.
    $cluster = d()->platform->web_server->http_service_type == 'cluster' ? TRUE : FALSE;
    $options = array();
    if ($override_slave_authority || $cluster) {
      $exclude = NULL;
    }
    else {
      $exclude = 'files/*' . PATH_SEPARATOR . 'private/*';
    }

    // Store the current exclude-path option
    $old_exclude = NULL;
    if(!is_null($exclude)) {
      $old_exclude = drush_get_option('exclude-paths');
      drush_set_option('exclude-paths', $exclude);
    }

    // Sync all filesystem changes to the remote server.
    d()->service('http')->sync(d()->site_path, $options);

    // Reset the exclude-path option
    if(!is_null($exclude)) {
      if(empty($old_exclude)) {
        drush_unset_option('exclude-paths');
      } else {
        drush_set_option('exclude-paths', $old_exclude);
      }
    }
  }
}

/**
 * Sync the current Drupal site BACK from a slave. Call before
 * running operations that need files where the slave is authoritative.
 *
 * E.g. before a backup is made.
 *
 * @param string $alias The site alias to work on, defaults to the current site.
 */
function provision_drupal_fetch_site($alias = NULL) {

  $site = d($alias);
  // synch filesystem changes back from the remote server.
  $site->service('http')->fetch($site->site_path . '/files/');
  $site->service('http')->fetch($site->site_path . '/private/');
  $site->service('http')->fetch($site->site_path . '/modules/');
  $site->service('http')->fetch($site->site_path . '/themes/');
  $site->service('http')->fetch($site->site_path . '/libraries/');
  // Questionable... who is authoritive?
  $site->service('http')->fetch($site->site_path . '/local.settings.php');
}

/**
 * Generate a settings file for the site.
 */
function _provision_drupal_create_settings_file() {
  $config = new Provision_Config_Drupal_Settings(d()->name, drush_get_context('site'));
  $config->write();
}

/**
 * Create the directories needed to host a drupal site
 *
 * Also maintains permissions on existing directories.
 *
 * @param $url
 *   The url of the site being invoked.
  */
function _provision_drupal_create_directories($url = NULL) {
  // Ensure that the stat cache is cleared, so that we have an up-to-date view of the filesystem.
  clearstatcache();
  if (is_null($url)) {
    if (d()->type == 'site') {
      $url = d()->uri;
    }
    else {
      $url = 'all';
    }
  }
  # those directories will be created and their modes changed
  $mkdir = array(
    "sites/$url"                 => 0755,
    # those should be writable by the aegir primary group to ease development
    "sites/$url/themes"          => 02775,
    "sites/$url/modules"         => 02775,
    "sites/$url/libraries"       => 02775, # http://drupal.org/node/496240
    "sites/$url/vendor"          => 02775, # https://github.com/omega8cc/boa/issues/385
  );

  $chgrp = array();
  // special case: platform. do not handle files dir
  if ($url != 'all') {
    $mkdir["sites/$url/files"]            = 02775;
    $chgrp["sites/$url/files"]            = d('@server_master')->web_group;

    $mkdir["sites/$url/files/tmp"]        = 02775;
    $chgrp["sites/$url/files/tmp"]        = d('@server_master')->web_group;

    $mkdir["sites/$url/files/images"]     = 02775;
    $chgrp["sites/$url/files/images"]     = d('@server_master')->web_group;

    $mkdir["sites/$url/files/pictures"]   = 02775;
    $chgrp["sites/$url/files/pictures"]   = d('@server_master')->web_group;

    $mkdir["sites/$url/files/css"]        = 02775;
    $chgrp["sites/$url/files/css"]        = d('@server_master')->web_group;

    $mkdir["sites/$url/files/js"]         = 02775;
    $chgrp["sites/$url/files/js"]         = d('@server_master')->web_group;

    $mkdir["sites/$url/files/advagg_css"] = 02775;
    $chgrp["sites/$url/files/advagg_css"] = d('@server_master')->web_group;

    $mkdir["sites/$url/files/advagg_js"]  = 02775;
    $chgrp["sites/$url/files/advagg_js"]  = d('@server_master')->web_group;

    $mkdir["sites/$url/files/ctools"]     = 02775;
    $chgrp["sites/$url/files/ctools"]     = d('@server_master')->web_group;

    $mkdir["sites/$url/files/imagecache"] = 02775;
    $chgrp["sites/$url/files/imagecache"] = d('@server_master')->web_group;

    $mkdir["sites/$url/files/locations"]  = 02775;
    $chgrp["sites/$url/files/locations"]  = d('@server_master')->web_group;

    $mkdir["sites/$url/files/xmlsitemap"] = 02775;
    $chgrp["sites/$url/files/xmlsitemap"] = d('@server_master')->web_group;

    $mkdir["sites/$url/files/deployment"] = 02775;
    $chgrp["sites/$url/files/deployment"] = d('@server_master')->web_group;

    // d7 support
    $mkdir["sites/$url/private"]          = 02775;
    $chgrp["sites/$url/private"]          = d('@server_master')->web_group;

    $mkdir["sites/$url/private/files"]    = 02775;
    $chgrp["sites/$url/private/files"]    = d('@server_master')->web_group;

    $mkdir["sites/$url/private/temp"]     = 02775;
    $chgrp["sites/$url/private/temp"]     = d('@server_master')->web_group;

    $mkdir["sites/$url/files/styles"]     = 02775;
    $chgrp["sites/$url/files/styles"]     = d('@server_master')->web_group;

    // backup_migrate support
    $mkdir["sites/$url/private/files/backup_migrate"] = 02775;
    $chgrp["sites/$url/private/files/backup_migrate"] = d('@server_master')->web_group;

    $mkdir["sites/$url/private/files/backup_migrate/manual"] = 02775;
    $chgrp["sites/$url/private/files/backup_migrate/manual"] = d('@server_master')->web_group;

    $mkdir["sites/$url/private/files/backup_migrate/scheduled"] = 02775;
    $chgrp["sites/$url/private/files/backup_migrate/scheduled"] = d('@server_master')->web_group;

    // feeds support
    $mkdir["sites/$url/files/feeds"] = 02775;
    $chgrp["sites/$url/files/feeds"] = d('@server_master')->web_group;

    $mkdir["sites/$url/private/files/feeds"] = 02775;
    $chgrp["sites/$url/private/files/feeds"] = d('@server_master')->web_group;

    // d8 support
    $mkdir["sites/$url/private/config"]   = 02775;
    $chgrp["sites/$url/private/config"]   = d('@server_master')->web_group;

    $mkdir["sites/$url/private/config/sync"] = 02775;
    $chgrp["sites/$url/private/config/sync"] = d('@server_master')->web_group;

    // civicrm support
    $mkdir["sites/$url/files/civicrm"] = 02775;
    $chgrp["sites/$url/files/civicrm"] = d('@server_master')->web_group;

    $mkdir["sites/$url/files/civicrm/ConfigAndLog"] = 02775;
    $chgrp["sites/$url/files/civicrm/ConfigAndLog"] = d('@server_master')->web_group;

    $mkdir["sites/$url/files/civicrm/custom"] = 02775;
    $chgrp["sites/$url/files/civicrm/custom"] = d('@server_master')->web_group;

    $mkdir["sites/$url/files/civicrm/dynamic"] = 02775;
    $chgrp["sites/$url/files/civicrm/dynamic"] = d('@server_master')->web_group;

    $mkdir["sites/$url/files/civicrm/persist"] = 02775;
    $chgrp["sites/$url/files/civicrm/persist"] = d('@server_master')->web_group;
  }

  // These paths should not have recursive operations performed on them.
  $chmod_not_recursive = $chgrp_not_recursive = array(
    "sites/$url",
    "sites/$url/files",
    "sites/$url/files/advagg_css",
    "sites/$url/files/advagg_js",
    "sites/$url/files/civicrm",
    "sites/$url/files/civicrm/ConfigAndLog",
    "sites/$url/files/civicrm/custom",
    "sites/$url/files/civicrm/dynamic",
    "sites/$url/files/civicrm/persist",
    "sites/$url/files/css",
    "sites/$url/files/ctools",
    "sites/$url/files/deployment",
    "sites/$url/files/feeds",
    "sites/$url/files/imagecache",
    "sites/$url/files/images",
    "sites/$url/files/js",
    "sites/$url/files/locations",
    "sites/$url/files/pictures",
    "sites/$url/files/private",
    "sites/$url/files/styles",
    "sites/$url/files/tmp",
    "sites/$url/files/xmlsitemap",
    "sites/$url/libraries",
    "sites/$url/modules",
    "sites/$url/private",
    "sites/$url/private/config",
    "sites/$url/private/config/sync",
    "sites/$url/private/files",
    "sites/$url/private/files/backup_migrate",
    "sites/$url/private/files/backup_migrate/manual",
    "sites/$url/private/files/backup_migrate/scheduled",
    "sites/$url/private/files/feeds",
    "sites/$url/private/temp",
    "sites/$url/themes",
    "sites/$url/vendor"
  );

  // Allow other commands to add or alter the directories to be created,
  // chmod'd or chgrp'd.
  // TODO: Figure out a better way to do this.
  drush_command_invoke_all_ref('provision_drupal_create_directories_alter', $mkdir, $url);
  drush_command_invoke_all_ref('provision_drupal_chgrp_directories_alter', $chgrp, $url);
  drush_command_invoke_all_ref('provision_drupal_chgrp_not_recursive_alter', $chgrp_not_recursive, $url);
  drush_command_invoke_all_ref('provision_drupal_chmod_not_recursive_alter', $chmod_not_recursive, $url);

  foreach ($mkdir as $path => $perm) {
    if (!is_dir($path)) {
      provision_file()->mkdir($path)
        ->succeed('Created <code>@path</code>')
        ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
    }
  }
  foreach ($chgrp as $path => $group) {
    if ($group !== FALSE) {
      provision_file()->chgrp($path, $group, !in_array($path, $chgrp_not_recursive))
        ->succeed('Changed group ownership of <code>@path</code> to @gid')
        ->fail('Could not change group ownership <code>@path</code> to @gid');
    }
  }
  // Apply chmod AFTER chgrp because chgrp strips special permissions.
  foreach ($mkdir as $path => $perm) {
    if ($perm !== FALSE) {
      provision_file()->chmod($path, $perm, !in_array($path, $chmod_not_recursive))
        ->succeed('Changed permissions of <code>@path</code> to @perm')
        ->fail('Could not change permissions <code>@path</code> to @perm');
    }
  }
}

/**
 * Native files/private symlinking for a single site, delegated to the BOA
 * autosymlink tool running as root via its hardened sudo wrapper.
 *
 * Provision runs unprivileged (the Octopus account user), and the per-account
 * static store (<account>/static/files) is root-managed (the backup subsystem
 * owns it), so the move-and-symlink MUST run privileged -- doing it in
 * unprivileged PHP fails with EACCES creating the store dir. The wrapper
 * validates its arguments and invokes only the narrow single-site autosymlink,
 * which MOVES the site's existing files/private dirs into the store and replaces
 * them with symlinks (it does NOT create empty store dirs). Warn-not-fail: any
 * failure is logged and the site keeps plain dirs. No-op for platforms, the
 * box-wide / per-account kill-switch, or a missing wrapper.
 *
 * @param $url
 *   The site URL.
 * @param $force_unshare
 *   TRUE on clone (break an inherited cross-account share; a fresh clone never
 *   opted into sharing); FALSE on a fresh install.
 * @param $account
 *   Optional Octopus account dir name (e.g. o8). Pass it from the install hook,
 *   which runs BEFORE the site's first verify writes the nginx vhost (autosymlink
 *   then trusts the Drush alias alone). Omit on the clone path (post-verify),
 *   where autosymlink resolves the account from the vhost+alias pair.
 */
function _provision_drupal_native_symlink($url, $force_unshare = FALSE, $account = NULL) {
  if (empty($url) || $url == 'all') {
    return;
  }

  $aegir_root = d('@server_master')->aegir_root;
  // Scope to Octopus-hosted accounts; the master hostmaster account is left as
  // plain dirs.
  if (empty($aegir_root) || strpos($aegir_root, '/data/disk/') !== 0) {
    return;
  }

  // Kill-switch: disable native symlinking without a code change, box-wide
  // (/data/conf) or per-account (static/control).
  if (is_file('/data/conf/disable_native_files_symlink.cnf')
      || is_file($aegir_root . '/static/control/no_native_files_symlink.info')) {
    return;
  }

  // The privileged entry point; absent on non-BOA Aegir or early bootstrap.
  if (!is_file('/usr/local/bin/fix-drupal-site-symlinks.sh')) {
    return;
  }

  // Fail-closed token validation before the values reach a root-level tool.
  if (!preg_match('/^[a-z0-9][a-z0-9._-]*$/i', $url) || strpos($url, '..') !== FALSE) {
    drush_log(dt('[native-symlink] Skipped: unexpected site token @url', array('@url' => $url)), 'warning');
    return;
  }

  // Build the wrapper command. drush_shell_exec sprintf-escapes each %s arg via
  // drush_escapeshellarg, returns a bool, and never throws -- inherently
  // warn-not-fail. The wrapper itself re-validates and runs ONLY the narrow
  // single-site apply (it cannot reach autosymlink's global modes).
  $cmd = 'sudo --non-interactive /usr/local/bin/fix-drupal-site-symlinks.sh --site=%s';
  $args = array($url);
  if (!empty($account)) {
    $cmd .= ' --account=%s';
    $args[] = $account;
  }
  if ($force_unshare) {
    $cmd .= ' --force-unshare';
  }
  array_unshift($args, $cmd);
  $ok = call_user_func_array('drush_shell_exec', $args);

  if ($ok) {
    drush_log(dt('[native-symlink] @url: files/private moved into the per-account store and symlinked', array('@url' => $url)), 'success');
  }
  else {
    // Non-fatal, but operator-actionable: the site keeps plain dirs (or, on a
    // clone, may still share the source store). The install/clone succeeded.
    drush_log(dt('[native-symlink] @url: could not move files into the store (non-fatal); left as plain dirs - manual review if symlinking was expected', array('@url' => $url)), 'warning');
  }
}

/**
 * Set a site's per-account static store aside at delete time.
 *
 * The Delete task removes the site directory and its two links, never the
 * store (<account>/static/files/<uri>), which is root-managed; until now the
 * leftover waited for the nightly autosymlink sweep (an opt-in on self-hosted
 * boxes), and a site recreated under the same name found it archived aside
 * only then. This asks the same privileged wrapper for the same move now:
 * static/files/.archived/<stamp>/<uri>/, never a deletion. Warn-not-fail;
 * runs BEFORE the alias is removed because the wrapper proves ownership
 * through it. No-op without a store, outside Octopus accounts, or without
 * the wrapper.
 * $prefix names the task in the log lines: DELETE for the site delete,
 * RENAME for a migrate under a new name, whose old-name store is left
 * behind the same way once the target has its own copy.
 */
function _provision_drupal_archive_static_store($url, $prefix = 'DELETE') {
  if (empty($url) || $url == 'all') {
    return;
  }
  $prefix = preg_replace('/[^A-Z]/', '', strtoupper((string) $prefix));
  if ($prefix === '') {
    $prefix = 'DELETE';
  }
  $aegir_root = d('@server_master')->aegir_root;
  if (empty($aegir_root) || strpos($aegir_root, '/data/disk/') !== 0) {
    return;
  }
  if (!is_file('/usr/local/bin/fix-drupal-site-symlinks.sh')) {
    return;
  }
  if (!preg_match('/^[a-z0-9][a-z0-9._-]*$/i', $url) || strpos($url, '..') !== FALSE) {
    drush_log(dt($prefix . '/STORE/SKIP unexpected site token @url', array('@url' => $url)), 'warning');
    return;
  }
  $store = $aegir_root . '/static/files/' . $url;
  if (!is_dir($store) && !is_link($store)) {
    drush_log(dt($prefix . '/STORE/NONE no per-account static store at @store', array('@store' => $store)), 'info');
    return;
  }
  // The operator's switch for report-only orphan handling holds this hand
  // too: the store stays at its live name, and the task says so.
  if (is_file('/data/conf/disable_orphan_store_archiving.cnf')) {
    drush_log(dt($prefix . '/STORE/KEPT @url: store left at @store (operator switch /data/conf/disable_orphan_store_archiving.cnf)', array('@url' => $url, '@store' => $store)), 'info');
    return;
  }
  $account = basename($aegir_root);
  drush_log(dt($prefix . '/STORE/ARCHIVE setting @store aside under static/files/.archived (the wrapper, as root)', array('@store' => $store)), 'info');
  $ok = drush_shell_exec('sudo --non-interactive /usr/local/bin/fix-drupal-site-symlinks.sh --site=%s --account=%s --archive-store', $url, $account);
  clearstatcache();
  if ($ok && !is_dir($store) && !is_link($store)) {
    drush_log(dt($prefix . '/STORE/ARCHIVED @url: store set aside under @arch, nothing deleted', array('@url' => $url, '@arch' => $aegir_root . '/static/files/.archived/')), 'success');
  }
  else {
    drush_log(dt($prefix . '/STORE/LEFT @url: store still at @store (the wrapper declined or failed; see /var/log/boa/autosymlink.log): a share names it, a registered site still reads it through its link, or the move failed; review by hand, the nightly sweep takes it only once no share names it and nothing reads it', array('@url' => $url, '@store' => $store)), 'warning');
  }
}

/**
 * Make a directory tree owner-writable so a recursive delete can proceed.
 *
 * Drupal's installer leaves a fresh site directory without write bits
 * (measured 0555 on a site deleted before its first Verify; a Verify sets
 * 0755); unlink() of anything inside then fails and the directory survives
 * the Delete task with its settings.php. Directories only, symlinks never
 * followed, best effort (chmod needs ownership, not write permission).
 */
function _provision_drupal_make_tree_writable($path) {
  if (!is_dir($path) || is_link($path)) {
    return;
  }
  $perms = @fileperms($path);
  if ($perms !== FALSE && (($perms & 0700) != 0700)) {
    @chmod($path, ($perms & 0777) | 0700);
  }
  $d = @dir($path);
  if (empty($d)) {
    return;
  }
  while (($entry = $d->read()) !== FALSE) {
    if ($entry == '.' || $entry == '..') {
      continue;
    }
    $entry_path = $path . '/' . $entry;
    if (is_dir($entry_path) && !is_link($entry_path)) {
      _provision_drupal_make_tree_writable($entry_path);
    }
  }
  $d->close();
}

/**
 * Re-establish native files symlinking after a restore.
 *
 * A files-carrying archive extracts files/private as populated real dirs, so
 * the deploy swap leaves the site with plain dirs on the platform partition
 * and its per-account store orphaned: run the narrow conversion (as clone and
 * migrate do), WITHOUT force-unshare - restore re-deploys the same site, and
 * a deliberately shared store must stay shared (sharing is honoured
 * everywhere except cloning).
 *
 * A files-LESS archive (the DB-only backup mode) extracts files/private as
 * near-empty real dirs holding only tar exclusion residue. Converting those
 * into the store would archive the live store content aside and serve empty
 * dirs - and merely skipping would defer the same damage to the nightly
 * sweep. Re-point such dirs at the existing store instead: the DB comes from
 * the archive, the files keep their pre-restore state.
 */
function _provision_drupal_native_symlink_after_restore($url) {
  $aegir_root = d('@server_master')->aegir_root;
  // Scope to Octopus-hosted accounts, like _provision_drupal_native_symlink.
  if (empty($aegir_root) || strpos($aegir_root, '/data/disk/') !== 0) {
    return;
  }
  $symlink_disabled = is_file('/data/conf/disable_native_files_symlink.cnf')
    || is_file($aegir_root . '/static/control/no_native_files_symlink.info');
  $site_path = d()->site_path;
  $store_base = $aegir_root . '/static/files/' . $url;
  $convert = FALSE;
  foreach (array('files', 'private') as $type) {
    $path = $site_path . '/' . $type;
    if (is_link($path) || !is_dir($path)) {
      // Symlink-preserving archive member (or absent): nothing was
      // materialised for this type; the link still points where it pointed
      // when the archive was taken.
      continue;
    }
    if (_provision_drupal_extracted_dir_has_content($path, $type)) {
      $convert = TRUE;
      continue;
    }
    $store = $store_base . '/' . $type;
    if (!is_dir($store)) {
      // Nothing to preserve; leave the empty dir to normal conversion.
      $convert = TRUE;
      continue;
    }
    if ($symlink_disabled) {
      drush_log(dt('[native-symlink] @url: restored archive carried no @type content; native symlinking is disabled, so the pre-restore content stays unlinked in the store at @store', array('@url' => $url, '@type' => $type, '@store' => $store)), 'warning');
      continue;
    }
    // The residue-only verdict above makes a recursive delete safe: the tree
    // holds only regenerated skeleton dirs and administrative stamp files
    // (deploy re-creates the standard subdir skeleton before this hook runs,
    // so a bare rmdir would fail).
    _provision_recursive_delete($path);
    if (!@file_exists($path) && @symlink($store, $path)) {
      // This is the preservation design working as intended (DB from the
      // archive, files keep their pre-restore state) - inform, don't alarm.
      drush_log(dt('[native-symlink] @url: restored archive carried no @type content; kept the existing store content and re-linked it', array('@url' => $url, '@type' => $type)), 'notice');
    }
    else {
      drush_log(dt('[native-symlink] @url: restored archive carried no @type content and the extracted dir could not be re-linked to the store - manual review required', array('@url' => $url, '@type' => $type)), 'warning');
    }
  }
  if ($convert) {
    _provision_drupal_native_symlink($url, FALSE);
  }
}

/**
 * TRUE when an extracted dir holds anything beyond regenerated residue.
 *
 * Deploy re-creates the standard skeleton (empty subdirs, core .htaccess,
 * the BOA ownership/permissions stamp files, tar exclusion tags) inside a
 * freshly extracted files/private dir before the restore hook runs, so only
 * a real file, a symlink, or an unexpectedly deep tree counts as content.
 * The backup-excluded cache subtrees (css/js/php aggregates, compiled
 * containers on Valkey-less D8+ sites, CiviCRM template caches) are never
 * carried by any archive, so anything inside them was regenerated by the
 * deploy/verify bootstraps after extraction - skip them wholesale.
 */
function _provision_drupal_extracted_dir_has_content($path, $type, $rel = '', $depth = 0) {
  if ($depth > 6) {
    return TRUE;
  }
  $skips = array();
  if (defined('PROVISION_BACKUP_EXCLUDED_DIRECTORIES')) {
    $prefix = './' . $type . '/';
    foreach (PROVISION_BACKUP_EXCLUDED_DIRECTORIES as $excluded) {
      if (strpos($excluded, $prefix) === 0) {
        $skips[] = substr($excluded, strlen($prefix));
      }
    }
  }
  elseif ($type === 'files') {
    $skips = array('advagg_css', 'advagg_js', 'civicrm/templates_c', 'css', 'js', 'php', 'tmp');
  }
  else {
    $skips = array('temp');
  }
  $entries = @scandir($path);
  if (!is_array($entries)) {
    return TRUE;
  }
  foreach ($entries as $entry) {
    if ($entry === '.' || $entry === '..') {
      continue;
    }
    if ($entry === 'exclude.tag' || $entry === '.htaccess'
      || preg_match('/^(ownership|permissions)-fixed-[0-9]+\.pid$/', $entry)) {
      continue;
    }
    $entry_rel = ($rel === '') ? $entry : $rel . '/' . $entry;
    $full = $path . '/' . $entry;
    if (is_link($full)) {
      return TRUE;
    }
    if (is_dir($full)) {
      if (in_array($entry_rel, $skips, TRUE)) {
        continue;
      }
      if (_provision_drupal_extracted_dir_has_content($full, $type, $entry_rel, $depth + 1)) {
        return TRUE;
      }
      continue;
    }
    return TRUE;
  }
  return FALSE;
}

/**
 * Call the core file_create_htaccess() functions.
 *
 * Unlink the files first to avoid permission issues.
 * If drupal already created the file then it's owned by e.g. www-data and aegir can not chmod those.
 *
 * @see https://drupal.org/SA-CORE-2013-003
 */
function _provision_drupal_ensure_htaccess_update() {

  // Add platform root auto-discovery to avoid confusing
  // Composer based D8+ codebase root with Drupal real root.
  d()->root = provision_auto_fix_platform_root(d()->root);

  if (drush_drupal_major_version() == 7) {

    // Copied from modules/system/system.install system_requirements()
    $htaccess_files['public://.htaccess'] = array(
      'directory' => variable_get('file_public_path', conf_path() . '/files'),
    );
    if ($private_files_directory = variable_get('file_private_path')) {
      $htaccess_files['private://.htaccess'] = array(
        'directory' => $private_files_directory,
      );
    }
    $htaccess_files['temporary://.htaccess'] = array(
      'directory' => variable_get('file_temporary_path', file_directory_temp()),
    );
    foreach ($htaccess_files as $htaccess_file => $info) {
      // Check for the string which was added to the recommended .htaccess file
      // in the latest security update.
      if (!file_exists($htaccess_file) || !($contents = @file_get_contents($htaccess_file)) || strpos($contents, 'Drupal_Security_Do_Not_Remove_See_SA_2013_003') === FALSE) {

        // Aegir specific
        @unlink($htaccess_file);
        file_create_htaccess($info['directory'], FALSE);
        drush_log(dt('Updated @file to match http://drupal.org/SA-CORE-2013-003', array('@file' => $info['directory'] . '/.htaccess')), 'notice');
        $path = file_stream_wrapper_get_instance_by_uri($htaccess_file)->getDirectoryPath();
        d()->service('http')->sync(d()->root . '/' . $info['directory'] . '/.htaccess');
      }
    }
  }
  elseif (drush_drupal_major_version() == 6 && function_exists('file_create_htaccess')) {

    // Copied from modules/system/system.install system_requirements()
    $htaccess_files['files_htaccess'] = array(
      'directory' => file_directory_path(),
    );
    $htaccess_files['temporary_files_htaccess'] = array(
      'directory' => file_directory_temp(),
    );
    foreach ($htaccess_files as $key => $info) {
      // Check for the string which was added to the recommended .htaccess file
      // in the latest security update.
      $htaccess_file = $info['directory'] . '/.htaccess';
      if (!file_exists($htaccess_file) || !($contents = @file_get_contents($htaccess_file)) || strpos($contents, 'Drupal_Security_Do_Not_Remove_See_SA_2013_003') === FALSE) {

        // Aegir specific
        @unlink($info['directory'] . '/.htaccess');
        file_create_htaccess($info['directory'], NULL);
        drush_log(dt('Updated @file to match http://drupal.org/SA-CORE-2013-003', array('@file' => $info['directory'] . '/.htaccess')), 'notice');
        d()->service('http')->sync(d()->root . '/' . $info['directory'] . '/.htaccess');
      }
    }
  }
}

/**
 * Runs an external script to reload all the various drupal caches
 */
function _provision_drupal_rebuild_caches() {
  if (d()->type === 'site') {
    if (drush_drupal_major_version() < 12) {
      drush_include_engine('drupal', 'clear');
    }
  }
}

/**
 * The shell line for one site-local Drush call, run by the backend's own PHP.
 *
 *   exec '<php>' 'vendor/drush/drush/drush.php' '<arg>' ...
 *
 * from the app root. Two shells run this line the same way: websh, BOA's
 * /bin/sh, execs a quoted absolute argv[0] verbatim (its fast lane for a
 * caller that already resolved the binary), and plain dash, which BOA leaves
 * as /bin/sh for the length of a Barracuda pass, runs the exec as written.
 * The bare launcher form ('vendor/drush/drush/drush.php ...') depends on
 * websh rewriting it with the account's PHP: under dash it is executed as a
 * file, and Drush 11's drush.php carries no shebang, so the line died as a
 * shell script ("cannot open ?php") whenever a task ran during a pass.
 * Measured on a BOA box, 2026-09-08.
 *
 * The interpreter is the one this backend runs under: the account's pinned
 * CLI PHP, chosen by websh when it launched Aegir's Drush 8, the same one it
 * would choose for the site-local Drush (_provision_site_local_php()), and
 * it runs on that interpreter's own php.ini (-c, _provision_site_local_php_ini()):
 * a modern Drush refuses to start under any non-empty open_basedir, and the
 * account's ~/.drush/php.ini for Aegir's Drush 8 carries one, so the ini
 * must never be left to the environment (PHPRC) of whatever shell or
 * launcher stands between this backend and the child. Every
 * argument is escaped here; the caller passes the string to
 * drush_shell_cd_and_exec() as ONE argument (no sprintf, so a '%' in an
 * argument is safe), or prefixes it with its own 'cd <dir> && ' when it
 * needs the exit status from exec().
 *
 * @param array $args      Drush arguments and options, unescaped.
 * @param array $php_opts  Interpreter options placed before the launcher
 *                         (the D11+ site-install's -d ini settings), unescaped.
 * @return string
 */
function _provision_site_local_drush_cmd($args, $php_opts = array()) {
  return _provision_php_script_cmd('vendor/drush/drush/drush.php', $args, $php_opts);
}

/**
 * Command line running any PHP script on the backend's own interpreter and
 * that interpreter's own php.ini: exec '<php>' -c '<ini>' [opts] '<script>' args.
 *
 * The shell's own ini policy never reaches the child: websh hands every
 * account launch the account's Drush 8 php.ini (PHPRC), whose open_basedir
 * every modern Drush refuses at preflight, so a launcher line such as
 * `drush10 ...` run through the shell dies before it does anything, while
 * this form runs the same script on the stock ini. -c beats PHPRC.
 */
function _provision_php_script_cmd($script, $args, $php_opts = array()) {
  $cmd = 'exec ' . escapeshellarg(_provision_site_local_php());
  $ini = _provision_site_local_php_ini();
  if ($ini !== '') {
    $cmd .= ' -c ' . escapeshellarg($ini);
  }
  foreach ((array) $php_opts as $opt) {
    $cmd .= ' ' . escapeshellarg((string) $opt);
  }
  $cmd .= ' ' . escapeshellarg((string) $script);
  foreach ((array) $args as $arg) {
    $cmd .= ' ' . escapeshellarg((string) $arg);
  }
  return $cmd;
}

/**
 * The account's Drush 10 launcher as a script path for _provision_php_script_cmd():
 * /usr/bin/drush10 resolves to a PHP script (vendor/bin/drush) on a BOA box;
 * empty when no such launcher exists.
 */
function _provision_drush10_script() {
  $launcher = '/usr/bin/drush10';
  if (is_link($launcher)) {
    $target = realpath($launcher);
    if ($target !== FALSE && is_file($target)) {
      return $target;
    }
  }
  elseif (is_file($launcher)) {
    return $launcher;
  }
  return '';
}

/**
 * The PHP interpreter every site-local subprocess of this backend runs under.
 *
 * PHP_BINARY is the interpreter of this very process: the account's pinned
 * CLI PHP, which websh selected when it launched Aegir's Drush 8. 'php' only
 * when the constant is unusable (never on a real box).
 *
 * @return string
 */
function _provision_site_local_php() {
  return (defined('PHP_BINARY') && PHP_BINARY !== '' && is_executable(PHP_BINARY)) ? PHP_BINARY : 'php';
}

/**
 * The php.ini a site-local subprocess runs on: the interpreter's own.
 *
 * BOA installs each PHP under /opt/phpNN with its php.ini at
 * /opt/phpNN/lib/php.ini; that file is what the web and the box's CLI run on,
 * and it carries no open_basedir. Empty when the interpreter is not laid out
 * that way (the caller then adds no -c and the child inherits the environment).
 *
 * @return string
 */
function _provision_site_local_php_ini() {
  $php = _provision_site_local_php();
  if (preg_match('#^(/opt/php[0-9]+)/bin/php$#', $php, $m) && is_file($m[1] . '/lib/php.ini')) {
    return $m[1] . '/lib/php.ini';
  }
  return '';
}

/**
 * Rebuild the Drupal 8+ service container and flush all caches.
 *
 * The D8+ analog of the D<8 'registry-rebuild' + cache-clear that the deploy,
 * verify and install paths run for older sites, and the body of the
 * hosting_tasks_extra 'Flush all caches' / 'Rebuild registry' tasks on D8+.
 *
 * D10+ is rebuilt by CORE ITSELF, in a subprocess with no Drush loaded:
 * platform/drupal/rebuild_d8plus.php runs drupal_rebuild() (the core/rebuild.php
 * path) as the site owner, from the docroot, under the same PHP that runs this
 * backend. Two facts decide that shape, both measured on a BOA box under
 * PHP 8:
 *
 * - A LOCKED D10/D11 platform and a running site-local Drush exclude each
 *   other: the lock de-types the abstract Symfony Console Output while
 *   StreamOutput stays typed, and swaps in a psr/log v1 overlay that modern
 *   Drush's Consolidation\Log\Logger is typed against v3 for -- so ANY
 *   vendor/drush/drush/drush.php invocation fatals at class load, whatever the
 *   exec bits say. The former chmod-only exec window opened the bits and
 *   nothing else; its 'cr' exited 255 into a task log that showed only the
 *   window's own EXEC/CMD/RELOCK lines. No chmod can bridge that.
 * - Aegir's Drush 8 must not be the compiler either: it binds psr/log v1 at
 *   preflight, its FULL bootstrap is for read-only introspection on a locked
 *   platform, and its container would carry Drush 8 service definitions.
 *
 * Core, patched to the same overlay the web serves in, loads on a locked
 * platform exactly as index.php does; on an unlocked (stock) platform it loads
 * as stock. Either way the compiled container is the web's own -- no
 * logger.drupaltodrush, no Drush command services -- so this is safe as the
 * LAST container write after any Drush-driven work in the stock state (the
 * deploy updatedb window, the D11+ site-install), and safe from verify,
 * install, a front-end task, or the developer account's unlock window.
 * D8/D9 keep Aegir Drush 8 'cache-rebuild', which still bootstraps them.
 *
 * Non-fatal by design: logs and continues, never drush_set_error() -- a cache
 * rebuild must not fail a deploy/verify/install/task. But never silent: a
 * subprocess that does not exit 0 is logged as a WARNING with its output
 * (the script reports a class-load fatal itself, independent of the
 * account's php.ini), so a rebuild that did not happen is visible in the
 * task log.
 */
function _provision_drupal_rebuild_d8plus() {
  // D7 and hostmaster have no D8+ container; nothing to rebuild here.
  if (provision_is_hostmaster_site() || drush_drupal_major_version() < 8) {
    return;
  }

  if (drush_drupal_major_version() >= 10) {
    // D10+: core's own rebuild in a Drush-free subprocess (see above).
    $root = d()->root;
    if (!is_file($root . '/core/includes/utility.inc')) {
      // A site context registered on the composer root rather than the
      // docroot (the verify post hook corrects this itself; other callers may
      // not have).
      $root = provision_auto_fix_platform_root($root);
    }
    $uri = d()->uri;
    $script = dirname(__FILE__) . '/drupal/rebuild_d8plus.php';
    // The interpreter this backend runs under is the account's pinned CLI
    // PHP, the one every site-local tool of this account runs under too.
    $php = (defined('PHP_BINARY') && PHP_BINARY !== '' && is_executable(PHP_BINARY)) ? PHP_BINARY : 'php';

    if (!is_file($script) || !is_file($root . '/core/includes/utility.inc') || $uri === '') {
      drush_log(dt('REBUILD/CORE/SKIP no core rebuild possible for @uri (script @script, docroot @root); container and caches left to the next request', array('@uri' => $uri, '@script' => $script, '@root' => $root)), 'warning');
      return;
    }

    drush_log(dt('REBUILD/CORE/CMD @php rebuild_d8plus.php @root @uri (core drupal_rebuild, no Drush)', array('@php' => $php, '@root' => $root, '@uri' => $uri)), 'info');
    // Cloaked credentials reach the subprocess only through the real
    // environment; harmless when credentials are not cloaked. Every argument
    // is passed through the sprintf form, which escapeshellarg()s each one;
    // stderr is captured with stdout by drush_shell_exec().
    provision_prepare_environment_subprocess();
    $ok = drush_shell_cd_and_exec($root, '%s %s %s %s', $php, $script, $root, $uri);
    provision_clear_environment_subprocess();
    $output = drush_shell_exec_output();
    if ($ok) {
      foreach ($output as $line) {
        drush_log($line, 'notice');
      }
      drush_log(dt('REBUILD/CORE/OK service container and caches rebuilt for @uri', array('@uri' => $uri)), 'ok');
    }
    else {
      drush_log(dt('REBUILD/CORE/FAIL the core rebuild for @uri did not complete; container and caches left to the next request', array('@uri' => $uri)), 'warning');
      foreach ($output as $line) {
        drush_log($line, 'warning');
      }
    }
  }
  else {
    // D8/D9: Aegir Drush 8 can bootstrap the site, so 'cache-rebuild' works.
    // provision_backend_invoke runs with integrate => TRUE, which replays a
    // child error into the parent's drush_get_error(); snapshot and restore the
    // error code so a cosmetic rebuild failure can never fail the caller (e.g.
    // the deploy's restore-dir / old-DB cleanup gated on !drush_get_error()).
    drush_log(dt('REBUILD/AEGIR/DRUSH Invoking cache-rebuild with Drush 8'), 'info');
    $pre_error = drush_get_context('DRUSH_ERROR_CODE', DRUSH_SUCCESS);
    provision_backend_invoke(d()->name, 'cache-rebuild');
    drush_set_context('DRUSH_ERROR_CODE', $pre_error);
  }
}

function provision_drupal_find_sites() {
  $sites = array();
  if ($dir = opendir("./sites")) {
    while (FALSE !== ($subdir = readdir($dir))) {
      // skip internal directory pointers
      if ($subdir != '.' && $subdir != '..') {
        $file = "./sites/$subdir/settings.php";
        if (file_exists("$file") && ($subdir != 'default') && !is_link("./sites/$subdir")) {
          $sites[$subdir] = $file;
        }
      }
    }
    closedir($dir);
  }
  else {
    drush_log(dt("Cannot find sites directory"), 'error');
    $sites = FALSE;
  }
  return $sites;
}

/**
 * Retrieve a list of aliases for the curent site.
 */
function provision_drupal_find_aliases() {

  // Add platform root auto-discovery to avoid confusing
  // Composer based D8+ codebase root with Drupal real root.
  d()->root = provision_auto_fix_platform_root(d()->root);

  $aliases = array();
  if (d()->type === 'site') {
    if (drush_drupal_major_version() >= 7) {
      $config = new Provision_Config_Drupal_Alias_Store(d()->name);
      $aliases = $config->find();
    }
    else {
      if ($dir = opendir(d()->root . "/sites")) {
        while (FALSE !== ($subdir = readdir($dir))) {
          // skip internal directory pointers
          if ($subdir != '.' && $subdir != '..') {
            $path = d()->root . '/sites/' . $subdir;
            if (is_link($path)) {
              if (d()->uri === readlink($path)) {
                $aliases[] = $subdir;
              }
            }
          }
        }
        closedir($dir);
      }
    }
  }

  return $aliases;
}

/**
 * Create and remove symlinks for each of the possible domain aliases of an
 * existing site.
 */
function _provision_drupal_maintain_aliases() {

  // Add platform root auto-discovery to avoid confusing
  // Composer based D8+ codebase root with Drupal real root.
  d()->root = provision_auto_fix_platform_root(d()->root);

  if (d()->type === 'site') {
    if (drush_drupal_major_version() >= 7) {
      $config = new Provision_Config_Drupal_Alias_Store(d()->name);
      $config->maintain();
      $config->write();
      d()->service('http')->sync($config->filename());
    }
    else {
      _provision_drupal_delete_aliases();
      if (!d()->redirection) {
        foreach (d()->aliases as $alias) {
          if ($alias = trim($alias)) {
            provision_file()->symlink(d()->uri, d()->root . '/sites/' . str_replace('/', '.', $alias))
              ->succeed('Created symlink for alias @target')
              ->fail('Could not create symlink for alias @target');
            d()->service('http')->sync(d()->root . '/sites/' . $alias);
          }
        }
      }
    }
  }
}

/**
 * Delete a list of aliases
 */
function _provision_drupal_delete_aliases() {

  // Add platform root auto-discovery to avoid confusing
  // Composer based D8+ codebase root with Drupal real root.
  d()->root = provision_auto_fix_platform_root(d()->root);

  if (d()->type === 'site') {
    if (drush_drupal_major_version() >= 7) {
        $config = new Provision_Config_Drupal_Alias_Store(d()->name);
        $config->delete();
        $config->write();
        d()->service('http')->sync($config->filename());
    }
    else {
      $aliases = provision_drupal_find_aliases();
      foreach ($aliases as $alias) {
        $path = d()->root . '/sites/' . $alias;
        provision_file()->unlink($path)
          ->succeed('Removed symlink for alias @path')
          ->fail('Could not remove symlink for alias @path');
        d()->service('http')->sync($path);
      }
    }
  }
}

function provision_find_packages() {
  // Load the version specific include files.
  drush_include_engine('drupal', 'packages', drush_drupal_major_version());

  $packages['base'] = _provision_find_packages('base');

  $packages['sites-all'] = _provision_find_packages('sites', 'all');

  // Create a package for the Drupal release
  $packages['base']['platforms'] = _provision_find_platforms();

  // Find install profiles.
  $profiles = _provision_find_profiles();
  drush_set_option('profiles', array_keys((array) $profiles), 'drupal');

  // Iterate through the install profiles, finding the profile specific packages
  foreach ($profiles as $profile => $info) {
    if (empty($info->version)) {
      $info->version = drush_drupal_version();
    }
    $packages['base']['profiles'][$profile] = $info;
    $packages['profiles'][$profile] =  _provision_find_packages('profiles', $profile);
  }

  return $packages;
}

function _provision_find_platforms() {
  return array(
    'drupal' => array(
    'short_name' => 'drupal', 'version' => drush_drupal_version(),
    'description' => dt("This platform is running @short_name @version", array('@short_name' => 'Drupal', '@version' => drush_drupal_version()))));
}

/**
 * A small helper function to reduce code duplication
 */
function _provision_find_packages($scope, $key = '') {
  $packages = array();
  $scope_text = ($key) ? "$scope/$key" : $scope;
  foreach (array('modules', 'themes') as $type) {
    $packages[$type] = array();
    $func = "_provision_drupal_find_$type";
    $result = $func($scope, $key);
    if (count($result)) {
      $packages[$type] = $result;
      drush_log(dt("Found !count !type in !scope",
        array('!count' => count($result),
        '!scope' => $scope_text, '!type' => $type)));
    }
  }
  return $packages;
}

/**
 * Map the system table to a packages multi-dimensional array component
 */
function provision_drupal_system_map() {
  // Load the version specific include files.
  drush_include_engine('drupal', 'packages');

  return _provision_drupal_system_map();
}

/**
 * Find modules in a certain scope.
 *
 * This function is general enough that it works for all supported
 * versions of Drupal.
 */
function _provision_drupal_find_modules($scope, $key = '') {
  $paths = _provision_drupal_search_paths($scope, $key, 'modules');
  $files = array();

  if (drush_drupal_major_version() >= 8) {
    foreach ($paths as $path) {
      $files = array_merge($files, drush_scan_directory($path, "/\.info\.yml$/", array('.', '..', 'CVS', '.svn'), 0, TRUE, 'name'));
    }
    // Remove '.info' extension from the end of the file name.
    foreach($files as $name => $file) {
      $files[basename($name, '.info')] = $file;
      unset($files[$name]);
    }
  }
  else {
    foreach ($paths as $path) {
      $files = array_merge($files, drush_scan_directory($path, "/\.module$/", array('.', '..', 'CVS', '.svn'), 0, TRUE, 'name'));
    }
  }

  foreach ($files as $name => $info) {
    if (drush_drupal_major_version() >= 8) {
      $yaml_file = sprintf("%s/%s.info.yml", dirname($info->filename), $name);
      if(!file_exists($yaml_file)) {
        drush_log(dt("@name.info.yml not found.", array("@name" => $name)), 'notice');
        unset($files[$name]);
        continue;
      }
      $files[$name]->info = Symfony\Component\Yaml\Yaml::parse(file_get_contents($yaml_file));
      if (!empty($files[$name]->info['name'])) {
        $files[$name]->name = $files[$name]->info['name'];
      }
    }
    else {
      $info_file = sprintf("%s/%s.info", dirname($info->filename), $name);
      $files[$name]->info = provision_parse_info_file($info_file);
    }
    // Skip hidden modules
    if (isset($files[$name]->info['hidden']) && $files[$name]->info['hidden'] != FALSE) {
      unset($files[$name]);
      continue;
    }
    $install_file = sprintf("%s/%s.install", dirname($info->filename), $name);
    // Drupal now enforces a minimum schema version see: https://www.drupal.org/node/2186315
    if (drush_drupal_major_version() >= 8) {
      $schema_version = drush_drupal_major_version() * 1000;
    }
    else {
      $schema_version = 0;
    }
    if (file_exists($install_file)) {
      $source = file_get_contents(trim($install_file));
      // Some modules keep update hooks in an include file (webform).
      $install_file_include = sprintf("%s/includes/%s.install.update.inc", dirname($info->filename), $name);
      if (file_exists($install_file_include)) {
        $source .= "\n" . file_get_contents(trim($install_file_include));
      }
      $source = str_replace("\r\n", "\n", $source);
      $source = str_replace("\r", "\n", $source);
      // Also search included files for function signature
      $include_matches = array();
      preg_match_all('!^\s*(include|require)(_once)?[\s\.\w\(]+([\'\"]{1}([\\\s\w\-\.\/]*)[\'\"]{1})!m', $source, $include_matches);
      if (count($include_matches[0])) {
        foreach($include_matches[4] as $include) {
          if ($include_file = realpath(dirname($info->filename) . DIRECTORY_SEPARATOR . $include)) {
            $source .= "\n" . file_get_contents($include_file);
          }
        }
      }
      $function_matches = array();
        preg_match_all('!function\s*&?([a-zA-Z0-9_]+)_update_([0-9]+)\s*\(.*?\s*\{!', $source, $function_matches);

      if (count($function_matches[0])) {
        $schema_version = max($schema_version, max($function_matches[2]));
      }
    }
    $files[$name]->schema_version = $schema_version;

    // If version is missing, or if version is set to 'VERSION', use "drush_drupal_version()".
    // The VERSION constant used in Git checkouts. Also appears when using DEV modules in composer.
    if (empty($files[$name]->info['version']) || $files[$name]->info['version'] == 'VERSION') {
      $files[$name]->version = drush_drupal_version();
    }
    else {
      $files[$name]->version = $files[$name]->info['version'];
    }
  }
  return $files;
}

function provision_parse_info_file($filename) {
  $info = array();

  $defaults = array(
    'dependencies' => array(),
    'description' => '',
    'version' => NULL,
    'php' => DRUPAL_MINIMUM_PHP,
  );

  if (file_exists($filename)) {
    $info = _provision_drupal_parse_info_file($filename);
  }

  // Merge in defaults and return
  return $info + $defaults;
}

/**
 * Set up the $_SERVER environment variable so that drupal can correctly parse the settings.php file.
 * The real credentials are stored in the Apache vhost of the relevant site, to prevent leaking of
 * sensitive data to site administrators with PHP access who might otherwise access such credentials
 * potentially of other sites' settings.php in a multisite set-up.
 *
 * Fire hook_provision_prepare_environment() so other scripts can react to this
 * step and add more information or take other actions.
 */
function provision_prepare_environment() {
  $fields = array('db_type', 'db_host', 'db_user', 'db_passwd', 'db_name', 'db_port');
  foreach ($fields as $key) {
    $_SERVER[$key] = drush_get_option($key, NULL, 'site');
  }

  // As of Drupal 7 there is no more mysqli type
  if (drush_drupal_major_version() >= 7) {
    $_SERVER['db_type'] = ($_SERVER['db_type'] == 'mysqli') ? 'mysql' : $_SERVER['db_type'];
  }

  // Invoke provision_prepare_environment: Allow other scripts to react to the
  // preparation of the environment variables.
  drush_command_invoke_all('provision_prepare_environment');

}

/**
 * Export the site DB credentials into the real process environment.
 *
 * provision_prepare_environment() assigns $_SERVER in THIS process only, which
 * a spawned site-local Drush never inherits. With cloaked credentials that child
 * bootstraps a settings.php whose $databases is defined only when
 * $_SERVER['db_name'] is set, so without this it cannot connect. putenv() puts
 * the values in the environment the child is forked with; the CLI SAPI exposes
 * them through $_SERVER there regardless of variables_order.
 *
 * Call immediately before any drush_shell_cd_and_exec() of a site-local Drush.
 */
function provision_prepare_environment_subprocess() {
  $fields = array('db_type', 'db_host', 'db_user', 'db_passwd', 'db_name', 'db_port');
  foreach ($fields as $key) {
    $value = drush_get_option($key, NULL, 'site');
    if ($value === NULL) {
      continue;
    }
    if ($key == 'db_type' && $value == 'mysqli') {
      $value = 'mysql';
    }
    putenv($key . '=' . $value);
  }
}

/**
 * Drop the site DB credentials from the process environment.
 *
 * The exported values outlive the subprocess that needed them. A later site in
 * the same run would otherwise inherit them, and because the cloaked
 * settings.php only consults its own credentials when $_SERVER['db_name'] is
 * absent, a stale value silently wins -- connecting the wrong database. Always
 * pair with provision_prepare_environment_subprocess().
 */
function provision_clear_environment_subprocess() {
  $fields = array('db_type', 'db_host', 'db_user', 'db_passwd', 'db_name', 'db_port');
  foreach ($fields as $key) {
    putenv($key);
  }
}


/**
 * Reload drushrc files (if available) from several possible locations.
 *
 * Because the base drush_load_config method only uses an include_once,
 * we run into issues when provision commands call other commands that
 * modify these config files.
 *
 * For the changes to become available, and more importantly passed to the
 * front end, we need to call this function after calling provision commands.
 */
function provision_reload_config($context, $file = NULL) {
  $file = ($file) ? $file : _drush_config_file($context);
  if (file_exists($file)) {
    drush_log("Reloading $context drushrc.php from $file");
    include($file);
    // $options will be defined by the config file included above.
    if (count($options)) {
      $options = array_merge(drush_get_context($context, array()), $options);
      drush_set_context($context, $options);
    }
  }
}

/**
 * Maintain a symlink to the site within a client directory
 *
 * This creates a directory structure like this:
 *
 * ~/clients/foo/example.org -> ~/platforms/.../sites/example.org
 * ~/clients/bar/bar.example.com -> ~/platforms/.../sites/bar.example.com
 *
 * @todo this probably doesn't belong in this file
 */
function _provision_client_create_symlink() {
  if (d()->client_name) {
    $sites_dir = d()->server->clients_path . '/' . d()->client_name;
    provision_file()->create_dir($sites_dir, dt('Client home directory for @client', array('@client' => d()->client_name)), 0750);
    _provision_client_delete_old_symlink();
    provision_file()->symlink(d()->site_path, $sites_dir . '/' . d()->uri)
      ->succeed('Created symlink @path to @target')
      ->fail('Could not create symlink @path to @target: @reason');
  }
}

/**
 * Delete dangling symlinks for this site.
 *
 * This is a crude implementation, as we do not have the old client name so we
 * need to iterate over the directories. We only remove the first entry we
 * find to save some I/O.
 */
function _provision_client_delete_old_symlink() {
  $previous = d()->server->clients_path . '/' . d()->client_name . '/' . d()->uri;
  // this is necessary because unlink doesn't fail on missing files (!)
  $found = (is_file($previous) || is_link($previous));
  provision_file()->unlink($previous);
  if (!$found) {
    drush_log(dt("couldn't find previous client symlink, iterating through all sites"));
    // only iterate if the symlink location changed
    if ($dh = @opendir(d()->server->clients_path)) {
      while (($file = readdir($dh)) !== false) {
        if ($file != '.' && $file != '..') {
          $path = d()->server->clients_path . '/' . $file . '/' . d()->uri;
          if (is_file($path) || is_link($path)) {
            provision_file()->unlink($path);
            drush_log(dt("removed previous symlink in @path", array("@path" => $path)), 'success');
            break; // found it
          }
        }
      }
      closedir($dh);
    }
  }
}

/**
 * Delete the site symlink within the client directory
 *
 * This deletes the site symlink created on verify/install
 *
 * @see _provision_client_create_symlink()
 */
function _provision_client_delete_symlink() {
  if (d()->client_name) {
    provision_file()->unlink(d()->server->clients_path . '/' . d()->client_name . '/' . d()->uri)
      ->succeed('Deleted symlink @path')
      ->fail('Failed to delete symlink @path: @reason');
  }
}

/**
 * Find available languages for a profile.
 */
function _provision_find_profile_languages($profile_path) {
  $languages = array();

  // English is default.
  $languages['en'] = 1;

  // Find languages available.
  $files = array_keys(drush_scan_directory($profile_path . '/translations', '/\.po$/', array('.', '..', 'CVS'), 0, FALSE, 'filepath'));
  $files = array_merge($files, array_keys(drush_scan_directory($profile_path, '/\.po$/', array('.', '..', 'CVS'), 0, FALSE, 'filepath')));
  if (is_array($files)) {
    foreach ($files as $file) {
      if (preg_match('!(/|\.)([^\./]+)\.po$!', $file, $langcode)) {
        $languages[$langcode[2]] = 1; // use the language name as an index to weed out duplicates
      }
    }
  }
  return array_keys($languages);
}

/**
 * Resolve the box-level Valkey/Redis wiring the hosted sites cache through.
 *
 * The rendered settings chain partial (global-valkey.inc, or global-redis.inc
 * on Redis-legacy boxes) carries the connection literals every site's
 * settings.php consumes, and the live password additionally sits in a pass
 * file the same chain prefers. Both are world-readable by design (the web
 * runtime reads them as the site user), so the account user running
 * Provision tasks can read them too. Parse the rendered literals instead of
 * re-deriving any value: these files are the system of record for what the
 * sites actually connect to. Returns FALSE when the box has no such wiring
 * (e.g. vanilla Aegir), which callers treat as nothing-to-purge.
 */
function _provision_valkey_conf() {
  $conf_file = '/data/conf/global/global-valkey.inc';
  if (!is_readable($conf_file)) {
    $conf_file = '/data/conf/global/global-redis.inc';
  }
  if (!is_readable($conf_file)) {
    return FALSE;
  }
  $data = file_get_contents($conf_file);
  if (!is_string($data) || $data === '') {
    return FALSE;
  }
  $host = '';
  $port = 0;
  $pass = '';
  if (preg_match("/\\\$settings\\['redis\\.connection'\\]\\['host'\\]\\s*=\\s*'([^']+)'/", $data, $m)) {
    $host = $m[1];
  }
  if (preg_match("/\\\$settings\\['redis\\.connection'\\]\\['port'\\]\\s*=\\s*'?(\\d+)'?/", $data, $m)) {
    $port = (int) $m[1];
  }
  if (preg_match("/\\\$settings\\['redis\\.connection'\\]\\['password'\\]\\s*=\\s*'([^']*)'/", $data, $m)) {
    $pass = $m[1];
  }
  if ($host === '') {
    return FALSE;
  }
  // The pass file overrides the rendered literal, mirroring the runtime chain
  // (global-if-valkey.inc reads the same file before probing the server).
  $pass_file = '/data/conf/valkey/pass.inc';
  if (is_readable($pass_file)) {
    $pass_live = trim((string) @file_get_contents($pass_file));
    if ($pass_live !== '') {
      $pass = $pass_live;
    }
  }
  return array('host' => $host, 'port' => $port, 'pass' => $pass);
}

/**
 * Read one RESP reply from an open Valkey/Redis stream.
 *
 * Returns array('type' => status|error|int|bulk|array, 'value' => mixed),
 * or FALSE on a broken/timed-out stream. Array replies nest the same shape.
 */
function _provision_valkey_read_reply($fp) {
  $line = fgets($fp);
  if ($line === FALSE) {
    return FALSE;
  }
  $type = substr($line, 0, 1);
  $payload = rtrim(substr($line, 1), "\r\n");
  if ($type === '+') {
    return array('type' => 'status', 'value' => $payload);
  }
  if ($type === '-') {
    return array('type' => 'error', 'value' => $payload);
  }
  if ($type === ':') {
    return array('type' => 'int', 'value' => (int) $payload);
  }
  if ($type === '$') {
    $blen = (int) $payload;
    if ($blen < 0) {
      return array('type' => 'bulk', 'value' => NULL);
    }
    $data = '';
    $need = $blen + 2;
    while (strlen($data) < $need) {
      $chunk = fread($fp, $need - strlen($data));
      if ($chunk === FALSE || $chunk === '') {
        return FALSE;
      }
      $data .= $chunk;
    }
    return array('type' => 'bulk', 'value' => substr($data, 0, $blen));
  }
  if ($type === '*') {
    $count = (int) $payload;
    if ($count < 0) {
      return array('type' => 'array', 'value' => NULL);
    }
    $items = array();
    for ($i = 0; $i < $count; $i++) {
      $item = _provision_valkey_read_reply($fp);
      if ($item === FALSE) {
        return FALSE;
      }
      $items[] = $item;
    }
    return array('type' => 'array', 'value' => $items);
  }
  return FALSE;
}

/**
 * Send one command to Valkey/Redis over an open stream and read the reply.
 *
 * Speaks raw RESP so the purge works from the Aegir Drush 8 CLI on any PHP
 * the box pins (no phpredis dependency, PHP 5.6 floor) and the password never
 * appears on a command line. Returns the _provision_valkey_read_reply() shape
 * or FALSE on a broken stream.
 */
function _provision_valkey_cmd($fp, $args) {
  $out = '*' . count($args) . "\r\n";
  foreach ($args as $arg) {
    $arg = (string) $arg;
    $out .= '$' . strlen($arg) . "\r\n" . $arg . "\r\n";
  }
  $len = strlen($out);
  $sent = 0;
  while ($sent < $len) {
    $n = fwrite($fp, substr($out, $sent));
    if ($n === FALSE || $n === 0) {
      return FALSE;
    }
    $sent += $n;
  }
  return _provision_valkey_read_reply($fp);
}

/**
 * Delete every key matching one prefix in the currently SELECTed database.
 *
 * Purge-to-empty: one SCAN pass can miss keys written mid-iteration by
 * still-in-flight workers of a dying site -- observed live, where a single
 * pass left survivors and the freshly installed same-name site then rebuilt
 * its router table from the stale cached container. Re-sweeps until a full
 * pass finds nothing, bounded so a persistent concurrent writer cannot make
 * the task spin forever. UNLINK keeps the deletion off the server's main
 * thread; ancient pre-4.0 Redis without it falls back to DEL for the rest of
 * the run (via $use_del, shared across calls by reference).
 *
 * Returns the number of keys deleted, or FALSE on a protocol/stream failure.
 */
function _provision_valkey_purge_prefix($fp, $prefix, &$use_del) {
  $deleted = 0;
  for ($sweep = 0; $sweep < 8; $sweep++) {
    $found = 0;
    $cursor = '0';
    do {
      $reply = _provision_valkey_cmd($fp, array('SCAN', $cursor, 'MATCH', $prefix . '*', 'COUNT', '500'));
      if ($reply === FALSE || $reply['type'] !== 'array' || !is_array($reply['value']) || count($reply['value']) < 2) {
        return FALSE;
      }
      $cursor = (string) $reply['value'][0]['value'];
      $keys = array();
      if (is_array($reply['value'][1]['value'])) {
        foreach ($reply['value'][1]['value'] as $key_reply) {
          if (isset($key_reply['value']) && is_string($key_reply['value'])) {
            $keys[] = $key_reply['value'];
          }
        }
      }
      if (count($keys)) {
        $found += count($keys);
        $verb = $use_del ? 'DEL' : 'UNLINK';
        $dreply = _provision_valkey_cmd($fp, array_merge(array($verb), $keys));
        if ($dreply !== FALSE && $dreply['type'] === 'error' && !$use_del) {
          $use_del = TRUE;
          $dreply = _provision_valkey_cmd($fp, array_merge(array('DEL'), $keys));
        }
        if ($dreply === FALSE) {
          return FALSE;
        }
        if ($dreply['type'] === 'int') {
          $deleted += $dreply['value'];
        }
      }
    } while ($cursor !== '0');
    if ($found === 0) {
      break;
    }
    if ($sweep == 7) {
      drush_log(dt('VALKEY/PURGE sweep bound reached for prefix !prefix -- something kept writing; keys removed so far: !n', array('!prefix' => $prefix, '!n' => $deleted)), 'warning');
    }
  }
  return $deleted;
}

/**
 * Purge every cache namespace belonging to one site from the shared store.
 *
 * The hosted sites share one Valkey/Redis instance; each site's keys are
 * scoped only by prefix: md5(<host>)_sn_* (D8+ integration, compiled
 * container and routing included), md5(<host>)_n_* (D7 integration) and
 * <host>_z_* (front-end page cache, also consumed as cache_prefix when the
 * front-end block is active). All three use the www-stripped host, and for
 * subdirectory sites the host part only -- so a subdir purge flushes its
 * siblings' shared namespace, which is safe (cache only) and unavoidable
 * (the namespace does not distinguish them). The md5 scoping makes matching
 * another site's keys impossible; the plain-host _z_ pattern cannot match a
 * different host either (the match is anchored at the key start).
 *
 * Databases are enumerated from INFO keyspace (falling back to 0..15) rather
 * than trusting the single rendered base literal: the poison was observed in
 * the configured base, but containers have also been observed in db0, and a
 * box whose base changed over time leaves keys behind in the old base. The
 * prefixes are site-unique, so sweeping every populated database stays
 * strictly site-scoped.
 *
 * Non-fatal by design: logs and returns FALSE on any failure -- a cache
 * purge must never fail a delete or install task. Boxes without the wiring
 * (vanilla Aegir) skip silently.
 */
function _provision_valkey_purge_site($uri) {
  $host = strtolower(trim((string) $uri));
  if ($host === '') {
    return FALSE;
  }
  $slash = strpos($host, '/');
  if ($slash !== FALSE) {
    $host = substr($host, 0, $slash);
  }
  // Strip the leading www. exactly like the runtime prefix builders do.
  $host = preg_replace('`^www\.`', '', $host);
  if (!is_string($host) || $host === '') {
    return FALSE;
  }
  // The plain-host <host>_z_ prefix goes into a SCAN MATCH glob verbatim, so
  // a host carrying glob metacharacters (*, ?, [) could match other sites'
  // keys. Site URIs are hostnames, so gate on the DNS-safe charset and skip
  // anything else (the md5 prefixes would be safe, but a URI this malformed
  // has no namespace of its own to purge anyway).
  if (!preg_match('/^[a-z0-9._-]+$/', $host)) {
    drush_log(dt('VALKEY/PURGE host part of !uri is not a plain hostname; skipping purge', array('!uri' => $uri)), 'warning');
    return FALSE;
  }
  $prefixes = array(
    md5($host) . '_sn_',
    md5($host) . '_n_',
    $host . '_z_'
  );
  return _provision_valkey_purge_prefixes($prefixes, $uri);
}

/**
 * Purge every cache namespace belonging to one Backdrop site.
 *
 * The Backdrop redis integration namespaces keys by the site's DB
 * coordinates, not its host: md5(db_host . db_name . db_table_prefix)
 * (redis_backdrop Redis_Client::getGlobalPrefix(), mirroring what core does
 * with database caching). The rendered Backdrop settings always write an
 * empty table prefix, and the runtime host string is the same db_host value
 * Provision holds (the settings template serves it from the creds this task
 * wrote), so the md5 rebuilt here byte-matches the runtime one. Aegir derives
 * the db name deterministically from the URI, so a site recreated (or cloned
 * into) the same name lands in the same namespace and inherits the deleted
 * predecessor's keys -- the same poisoning the Drupal purge closes.
 *
 * An md5 is 32 fixed hex chars, so the bare-prefix match cannot touch another
 * site's namespace, and no charset gate is needed (nothing user-shaped enters
 * the pattern). Missing db options (nothing provisioned yet) skip silently:
 * with no db there is no namespace to purge.
 */
function _provision_valkey_purge_backdrop_site($uri) {
  $db_host = (string) drush_get_option('db_host', '');
  $db_name = (string) drush_get_option('db_name', '');
  if ($db_host === '' || $db_name === '') {
    drush_log(dt('VALKEY/PURGE no db credentials in context for !uri; nothing to purge', array('!uri' => $uri)), 'info');
    return TRUE;
  }
  return _provision_valkey_purge_prefixes(array(md5($db_host . $db_name)), $uri);
}

/**
 * Purge every key under the given prefixes from the shared store.
 *
 * The connection/sweep core shared by the Drupal (host-keyed) and Backdrop
 * (db-keyed) namespace purges; see _provision_valkey_purge_site() for the
 * full rationale. The caller is responsible for prefixes that cannot match
 * another site's keys and are safe as SCAN MATCH globs.
 */
function _provision_valkey_purge_prefixes($prefixes, $uri) {
  $conf = _provision_valkey_conf();
  if ($conf === FALSE) {
    drush_log(dt('VALKEY/PURGE no cache store wiring on this box; nothing to purge for !uri', array('!uri' => $uri)), 'info');
    return TRUE;
  }
  $remote = 'tcp://' . $conf['host'] . ':' . $conf['port'];
  if (substr($conf['host'], 0, 1) === '/') {
    $remote = 'unix://' . $conf['host'];
  }
  $errno = 0;
  $errstr = '';
  $fp = @stream_socket_client($remote, $errno, $errstr, 3);
  if (!$fp) {
    // The rendered settings chain is deployed on every box, store or not, so
    // a refused connection on a box with no server installed just means
    // nothing was ever cached -- do not raise a false alarm there.
    if (!is_file('/etc/valkey/valkey.conf') && !is_file('/etc/redis/redis.conf')) {
      drush_log(dt('VALKEY/PURGE no cache store on this box; nothing to purge for !uri', array('!uri' => $uri)), 'info');
      return TRUE;
    }
    drush_log(dt('VALKEY/PURGE could not connect to !remote (!err); stale cache keys for !uri may remain', array('!remote' => $remote, '!err' => $errstr, '!uri' => $uri)), 'warning');
    return FALSE;
  }
  stream_set_timeout($fp, 5);
  if ($conf['pass'] !== '') {
    $reply = _provision_valkey_cmd($fp, array('AUTH', $conf['pass']));
    if ($reply === FALSE || $reply['type'] !== 'status') {
      drush_log(dt('VALKEY/PURGE authentication failed; stale cache keys for !uri may remain', array('!uri' => $uri)), 'warning');
      fclose($fp);
      return FALSE;
    }
  }
  // Enumerate populated databases; INFO keyspace only lists dbs with keys.
  // A FALSE reply means the stream itself died (EOF/timeout) -- a broken
  // socket cannot be reused and continuing would silently no-op the whole
  // purge while reporting a clean store, so abort loudly instead. An error
  // REPLY (server alive, INFO renamed/restricted) keeps the standard-range
  // fallback: probing past the configured databases count is benign (see the
  // SELECT handling below).
  $dbs = array();
  $reply = _provision_valkey_cmd($fp, array('INFO', 'keyspace'));
  if ($reply === FALSE) {
    drush_log(dt('VALKEY/PURGE connection lost while enumerating databases; stale cache keys for !uri may remain', array('!uri' => $uri)), 'warning');
    fclose($fp);
    return FALSE;
  }
  if ($reply['type'] === 'bulk' && is_string($reply['value'])) {
    if (preg_match_all('/^db(\d+):/m', $reply['value'], $m)) {
      $dbs = $m[1];
    }
  }
  else {
    $dbs = range(0, 15);
  }
  $total = 0;
  $failed = FALSE;
  $use_del = FALSE;
  foreach ($dbs as $db_n) {
    $reply = _provision_valkey_cmd($fp, array('SELECT', (string) $db_n));
    if ($reply === FALSE) {
      // Stream died mid-run: nothing after this point would execute, so a
      // silent continue would report success on a purge that never happened.
      $failed = TRUE;
      drush_log(dt('VALKEY/PURGE connection lost at database !db; stale cache keys for !uri may remain', array('!db' => $db_n, '!uri' => $uri)), 'warning');
      break;
    }
    if ($reply['type'] !== 'status') {
      $select_err = is_string($reply['value']) ? $reply['value'] : '';
      if ($reply['type'] === 'error' && stripos($select_err, 'out of range') !== FALSE) {
        // Benign: the standard-range fallback probes past the server's
        // configured databases count (the shipped configs use 8 or 16).
        continue;
      }
      // Any other refusal (NOAUTH, NOPERM, LOADING, ...) refuses every
      // database the same way -- abort loudly rather than sweep 16 rejections
      // into a false all-clear.
      $failed = TRUE;
      drush_log(dt('VALKEY/PURGE database !db refused: !err; stale cache keys for !uri may remain', array('!db' => $db_n, '!err' => $select_err, '!uri' => $uri)), 'warning');
      break;
    }
    foreach ($prefixes as $prefix) {
      $n = _provision_valkey_purge_prefix($fp, $prefix, $use_del);
      if ($n === FALSE) {
        // Protocol/stream failure: the socket may be desynchronised, so no
        // later reply on it can be trusted -- abort the whole run.
        $failed = TRUE;
        drush_log(dt('VALKEY/PURGE scan failed in db !db for prefix !prefix; stale cache keys for !uri may remain', array('!db' => $db_n, '!prefix' => $prefix, '!uri' => $uri)), 'warning');
        break 2;
      }
      if ($n > 0) {
        $total += $n;
        drush_log(dt('VALKEY/PURGE removed !n stale cache keys in db !db (prefix !prefix) for !uri', array('!n' => $n, '!db' => $db_n, '!prefix' => $prefix, '!uri' => $uri)), 'success');
      }
    }
  }
  fclose($fp);
  if ($total === 0 && !$failed) {
    drush_log(dt('VALKEY/PURGE no stale cache keys found for !uri', array('!uri' => $uri)), 'info');
  }
  return $failed ? FALSE : TRUE;
}
