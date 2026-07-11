<?php
/**
 * @file
 * Backdrop CMS specific functions for the provisioning framework.
 *
 * Parallel of platform/provision_drupal.drush.inc for Backdrop platforms and
 * sites. Every command hook in this layer early-returns unless the context root
 * is a Backdrop root (provision_platform_is_backdrop(), defined once in the
 * always-loaded provision.inc); the mirror provision_drupal hooks carry the
 * inverse guard. Both commandfiles fire for every command -- the guards make
 * them mutually exclusive, so the Drupal layer stays byte-for-byte inert on
 * Backdrop trees and vice versa.
 *
 * Backdrop's numeric major version is 1, which misroutes every version gate in
 * the Drupal layer (settings_6 template, no installer, D8 o_contrib probe); this
 * separate layer exists to bypass that. The CMS-agnostic library functions in
 * platform/provision_drupal.drush.inc (find_sites, push_site, native_symlink,
 * create_directories, find_modules, parse_info_file) are reused verbatim; only
 * the CMS-specific settings writer, engines, packages entry points and platform
 * reporting are forked here.
 */

/**
 * Implements hook_drush_init().
 */
function provision_backdrop_drush_init() {
  provision_backdrop_provision_register_autoload();
}

/**
 * Register our directory as a place to find provision classes.
 */
function provision_backdrop_provision_register_autoload() {
  static $loaded = FALSE;
  if (!$loaded) {
    $loaded = TRUE;
    provision_autoload_register_prefix('Provision_', dirname(__FILE__));
  }
}

function provision_backdrop_drush_help($section) {
  switch ($section) {
    case 'error:PROVISION_BACKDROP_INSTALL_FAILED' :
      return dt('Could not complete Backdrop installation.');
    case 'error:PROVISION_BACKDROP_PHP_TOO_OLD' :
      return dt('The instance PHP CLI is older than the Backdrop 7.1 floor.');
  }
}

/**
 * Declare the Backdrop engines.
 *
 * Mirrors provision_drupal_drush_engine_drupal() (provision_drupal.drush.inc).
 * Engines live in the backdrop/ subdirectory of this commandfile and are pulled
 * in at runtime by drush_include_engine('backdrop', '<engine>'). No version
 * suffix is used: drush_include() resolves <engine>_1.inc (absent) then falls
 * back to the versionless <engine>.inc, so the poisoned major version 1 never
 * selects a wrong file.
 */
/**
 * Implements hook_drush_engine_type_info().
 *
 * Register 'backdrop' as an engine type so drush_include_engine('backdrop', ...)
 * resolves. Drush core registers the 'drupal' type the same way
 * (core_drush_engine_type_info()); without this our engines are "Unknown engine
 * type backdrop" and the reused package helpers never load.
 */
function provision_backdrop_drush_engine_type_info() {
  return array('backdrop' => array());
}

function provision_backdrop_drush_engine_backdrop() {
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
 * Generate a Backdrop settings.php for the site.
 *
 * Backdrop equivalent of _provision_drupal_create_settings_file(): must use the
 * Backdrop Settings config class (scalar $database, $config_directories,
 * hash_salt) rather than the Drupal one hard-wired to settings_6 for major 1.
 */
function _provision_backdrop_create_settings_file() {
  $config = new Provision_Config_Backdrop_Settings(d()->name, drush_get_context('site'));
  $config->write();
}

/**
 * Create the directories needed to host a Backdrop site.
 *
 * Reuses the CMS-agnostic Drupal directory scaffolding, then pins Backdrop's
 * file-based config dirs. Backdrop's web process writes JSON config to the
 * active dir at runtime on every UI/CLI config change, so the dir must stay
 * web-group writable and live inside sites/$url/ (the backup tar scope) -- it is
 * created here, empty and web-group 02775, BEFORE settings.php is rendered so
 * the installer never rewrites a read-only settings.php to relocate it.
 *
 * @param $url
 *   The site url, or NULL to derive it (site => uri, platform => 'all').
 */
function _provision_backdrop_create_directories($url = NULL) {
  if (is_null($url)) {
    if (d()->type == 'site') {
      $url = d()->uri;
    }
    else {
      $url = 'all';
    }
  }

  // Shared scaffolding (also creates sites/$url/private + private/config).
  _provision_drupal_create_directories($url);

  // Platform-level pass has no per-site config dir.
  if ($url == 'all') {
    return;
  }

  clearstatcache();
  $web_group = d('@server_master')->web_group;
  $config_dirs = array(
    "sites/$url/private/config/active",
    "sites/$url/private/config/staging",
  );
  foreach ($config_dirs as $path) {
    if (!is_dir($path)) {
      provision_file()->mkdir($path)
        ->succeed('Created <code>@path</code>')
        ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
    }
  }
  foreach ($config_dirs as $path) {
    provision_file()->chgrp($path, $web_group)
      ->succeed('Changed group ownership of <code>@path</code> to @gid')
      ->fail('Could not change group ownership <code>@path</code> to @gid');
  }
  // chmod AFTER chgrp because chgrp strips the setgid bit.
  foreach ($config_dirs as $path) {
    provision_file()->chmod($path, 02775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions <code>@path</code> to @perm');
  }
}

/**
 * Resolve the Backdrop core version string (e.g. "1.34.0").
 *
 * Prefers the defined BACKDROP_VERSION constant (present once the site/platform
 * is bootstrapped), otherwise parses the define() out of core/includes/
 * bootstrap.inc. Parsing avoids BackdropBoot::get_version()'s relative-include
 * quirk (which can fatal drush_chalk in the backend cwd) and works at the
 * DRUSH_BOOTSTRAP_DRUPAL_ROOT stage the platform verify reaches.
 *
 * @param $root
 *   Platform root, or NULL for d()->root.
 * @return
 *   The version string, or '' if it could not be resolved.
 */
function _provision_backdrop_get_version($root = NULL) {
  if (defined('BACKDROP_VERSION')) {
    return BACKDROP_VERSION;
  }
  if (is_null($root)) {
    $root = d()->root;
  }
  $version = '';
  $bootstrap = $root . '/core/includes/bootstrap.inc';
  if (is_readable($bootstrap)) {
    $data = file_get_contents($bootstrap);
    if (preg_match("/define\\(\\s*'BACKDROP_VERSION'\\s*,\\s*'([^']+)'/", $data, $match)) {
      $version = $match[1];
    }
  }
  return $version;
}

/**
 * Build the mysql:// DB URL install.sh expects, from the site db credentials.
 *
 * install.sh parse_url()s the value then urldecode()s each component, so the
 * user/pass are rawurlencode()d here to survive credentials with URL-reserved
 * characters. Backdrop's PDO layer wants driver 'mysql'; the shared db service
 * can leave 'mysqli' in the alias for major<7 (Backdrop is 1), so normalise it.
 */
function _provision_backdrop_db_url() {
  $type = drush_get_option('db_type', 'mysql', 'site');
  if ($type == 'mysqli') {
    $type = 'mysql';
  }
  $user = urldecode(drush_get_option('db_user', '', 'site'));
  $pass = urldecode(drush_get_option('db_passwd', '', 'site'));
  $host = drush_get_option('db_host', 'localhost', 'site');
  $port = drush_get_option('db_port', '', 'site');
  $name = drush_get_option('db_name', '', 'site');
  $url = $type . '://' . rawurlencode($user) . ':' . rawurlencode($pass) . '@' . $host;
  if (!empty($port)) {
    $url .= ':' . $port;
  }
  $url .= '/' . $name;
  return $url;
}

/**
 * Reload the persisted site options (db credentials + installed flag).
 *
 * The db creds enter the 'site' context only at install (the db service's
 * generate_site_credentials() sets them). At verify nothing regenerates them,
 * and drush's DRUPAL_SITE bootstrap does not reload the Backdrop site's
 * drushrc.php creds into the context for us -- so the context comes up empty and
 * the settings.php + drushrc re-render blanks the whole DB connection (empty
 * $database => dead site: anonymous pages still serve from the page cache while
 * every DB-touching request 500s). Reload any missing db_* option straight from
 * the persisted site drushrc.php (regex, no include, so the file's $_SERVER side
 * effects never run) BEFORE any re-render, keeping every write credential-
 * complete. No-op at install (creds already present) and idempotent.
 */
function _provision_backdrop_ensure_db_creds_loaded() {
  if (d()->type !== 'site') {
    return;
  }
  // Both a name and a password present => already loaded, nothing to do.
  if (drush_get_option('db_name', '', 'site') && drush_get_option('db_passwd', '', 'site')) {
    return;
  }
  $drushrc = d()->site_path . '/drushrc.php';
  if (!is_readable($drushrc)) {
    return;
  }
  $body = file_get_contents($drushrc);
  if ($body === FALSE) {
    return;
  }
  foreach (array('db_type', 'db_user', 'db_passwd', 'db_host', 'db_name', 'db_port') as $key) {
    if (drush_get_option($key, '', 'site')) {
      continue;
    }
    if (preg_match('/\\$options\\s*\\[\\s*[\'"]' . $key . '[\'"]\\s*\\]\\s*=\\s*[\'"]([^\'"]*)[\'"]/', $body, $m)) {
      drush_set_option($key, $m[1], 'site');
    }
  }
  // The persisted 'installed' flag has the same problem: it is written to the
  // site drushrc at install/verify but is not reloaded into the context on a
  // later `drush @site <cmd>` (backup/clone/migrate...), so those commands see
  // the site as never installed and abort with PROVISION_DRUPAL_SITE_NOT_FOUND.
  // Restore it from the same persisted drushrc (unquoted TRUE/true/1 value).
  if (!drush_get_option('installed')
    && preg_match('/\\$options\\s*\\[\\s*[\'"]installed[\'"]\\s*\\]\\s*=\\s*(TRUE|true|1)\\s*;/', $body)) {
    drush_set_option('installed', TRUE, 'site');
  }
}

/**
 * Resolve the site's database name without a CMS bootstrap.
 *
 * The bootstrap-free package-map and cron-key reads (backdrop/packages.inc,
 * backdrop/cron_key.inc) need the site db name, but where it lives depends on
 * the task phase: at install the db service has just written it into the 'site'
 * option context (generate_site_credentials()); at verify nothing repopulates
 * that context and the value instead comes from the site's saved drushrc.php,
 * which drush merges into the general option stack (not the 'site'-restricted
 * one). Try the 'site' context, then the merged stack, then parse the saved
 * drushrc.php directly, so resolution works in every phase.
 *
 * @return
 *   The db name, or '' if it cannot be resolved.
 */
function _provision_backdrop_site_db_name() {
  $db_name = drush_get_option('db_name', '', 'site');
  if (empty($db_name)) {
    $db_name = drush_get_option('db_name', '');
  }
  if (empty($db_name) && d()->type === 'site') {
    $drushrc = d()->site_path . '/drushrc.php';
    if (is_readable($drushrc)) {
      // Regex-parse (no include) to avoid the file's $_SERVER side effects.
      $body = file_get_contents($drushrc);
      if (preg_match('/\\$options\\s*\\[\\s*[\'"]db_name[\'"]\\s*\\]\\s*=\\s*[\'"]([A-Za-z0-9_]+)[\'"]/', $body, $m)) {
        $db_name = $m[1];
      }
    }
  }
  return is_string($db_name) ? $db_name : '';
}

/**
 * Rebuild the Backdrop caches after install/verify.
 *
 * Backdrop equivalent of _provision_drupal_rebuild_caches(): loads the versionless
 * Backdrop clear engine (backdrop_flush_all_caches()), never the Drupal one.
 */
function _provision_backdrop_rebuild_caches() {
  if (d()->type === 'site') {
    drush_include_engine('backdrop', 'clear');
  }
}

/**
 * Ensure the files-dir .htaccess protection for a Backdrop site.
 *
 * Backdrop branch of _provision_drupal_ensure_htaccess_update() (which only
 * covers major 6/7). file_ensure_htaccess() (re)creates the protective .htaccess
 * in the public and private files dirs. nginx deny rules are the authoritative
 * protection; this is defence-in-depth and best-effort (non-fatal).
 */
function _provision_backdrop_ensure_htaccess_update() {
  d()->root = provision_auto_fix_platform_root(d()->root);
  if (function_exists('file_ensure_htaccess')) {
    file_ensure_htaccess();
    drush_log(dt('Ensured files-dir .htaccess via file_ensure_htaccess()'), 'notice');
  }
}

/**
 * Report the packages available on a Backdrop platform.
 *
 * Fork of provision_find_packages() using the Backdrop package engine and
 * reporting the platform as Backdrop. The scan helpers (_provision_find_packages,
 * _provision_drupal_find_modules, provision_parse_info_file) are reused from the
 * commandfile; the version-specific helpers they depend on
 * (_provision_drupal_search_paths, _provision_drupal_parse_info_file,
 * _provision_drupal_find_themes, _provision_find_profiles) come from the Backdrop
 * packages engine loaded first here.
 */
function provision_backdrop_find_packages() {
  drush_include_engine('backdrop', 'packages');

  $packages['base'] = _provision_find_packages('base');

  $packages['sites-all'] = _provision_find_packages('sites', 'all');

  // Create a package for the Backdrop release.
  $packages['base']['platforms'] = _provision_backdrop_find_platforms();

  // Find install profiles.
  $profiles = _provision_find_profiles();
  drush_set_option('profiles', array_keys((array) $profiles), 'drupal');

  foreach ($profiles as $profile => $info) {
    if (empty($info->version)) {
      $info->version = _provision_backdrop_get_version();
    }
    $packages['base']['profiles'][$profile] = $info;
    $packages['profiles'][$profile] = _provision_find_packages('profiles', $profile);
  }

  return $packages;
}

function _provision_backdrop_find_platforms() {
  $version = _provision_backdrop_get_version();
  return array(
    'backdrop' => array(
      'short_name' => 'backdrop', 'version' => $version,
      'description' => dt("This platform is running @short_name @version", array('@short_name' => 'Backdrop', '@version' => $version))));
}

/**
 * Map the system table to a packages multi-dimensional array component.
 *
 * Fork of provision_drupal_system_map() using the Backdrop package engine.
 */
function provision_backdrop_system_map() {
  drush_include_engine('backdrop', 'packages');

  return _provision_drupal_system_map();
}
