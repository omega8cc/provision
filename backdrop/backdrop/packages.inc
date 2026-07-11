<?php

/**
 * @file Package management code for Backdrop CMS.
 *
 * Backdrop equivalent of platform/drupal/packages_7.inc. Backdrop is D7-shaped
 * (.info files, {system} table, .module files), so most helpers are reused
 * verbatim from the Drupal engine; the Backdrop-specific divergences are:
 *   - search paths add the core/ tier (Backdrop core modules/themes live under
 *     core/modules and core/themes, contrib under the top-level modules/themes);
 *   - install profiles live under core/profiles (core) as well as profiles/
 *     (contrib);
 *   - the platform is reported as Backdrop.
 *
 * These helpers keep the _provision_drupal_* names the commandfile scan helpers
 * (_provision_find_packages, _provision_drupal_find_modules) call into; they are
 * loaded only for Backdrop platforms (drush_include_engine('backdrop',...)), so
 * the identically-named Drupal engine is never co-loaded in the same process.
 */

// provision_parse_info_file() (a reused Drupal helper, always loaded) defaults
// the .info 'php' key to DRUPAL_MINIMUM_PHP, a constant Drupal core defines only
// on bootstrap. A Backdrop platform never bootstraps Drupal, so provide a
// fallback (Backdrop's own PHP floor) to keep the reused parser working. This
// engine is loaded before any provision_parse_info_file() call in the chain.
if (!defined('DRUPAL_MINIMUM_PHP')) {
  define('DRUPAL_MINIMUM_PHP', '7.1.0');
}


/**
 * Find themes in a certain scope.
 */
function _provision_drupal_find_themes($scope, $key = '') {
  $paths = _provision_drupal_search_paths($scope, $key, 'themes');
  $files = array();
  $engines = array();
  foreach ($paths as $path) {
    $files = array_merge($files, drush_scan_directory($path, "/\.info$/", array('.', '..', 'CVS', '.svn'), 0, true, 'name'));
    $engines = array_merge($engines, drush_scan_directory($path . "/engines", "/\.engine$/", array('.', '..', 'CVS', '.svn'), 0, true, 'name'));
  }
  foreach ($files as $name => $file) {
    $files[$name]->info = _provision_drupal_parse_info_file($file->filename);
    if (!empty($files[$name]->info['name'])) {
      $files[$name]->name = $files[$name]->info['name'];
    }
    if (empty($files[$name]->info['engine'])) {
      $filename = dirname($files[$name]->filename) . '/' . $files[$name]->name . '.theme';
      if (file_exists($filename)) {
        $files[$name]->owner = $filename;
        $files[$name]->prefix = $name;
      }
    }
    else {
      $engine = $files[$name]->info['engine'];
      if (isset($engines[$engine])) {
        $files[$name]->owner = $engines[$engine]->filename;
        $files[$name]->prefix = $engines[$engine]->name;
        $files[$name]->template = TRUE;
      }
    }
    if (!empty($files[$name]->info['version'])) {
      $files[$name]->version = $files[$name]->info['version'];
    }
    else {
      $files[$name]->version = NULL;
    }
  }
  return $files;
}

/**
 * This code is based on the Drupal 6 and Drupal 7 drupal_parse_info_file.
 */
function _provision_drupal_parse_info_file($filename) {
  $info = array();
  $constants = get_defined_constants();

  if (!file_exists($filename)) {
    return $info;
  }

  $data = file_get_contents($filename);
  if (preg_match_all('
    @^\s*                           # Start at the beginning of a line, ignoring leading whitespace
    ((?:
      [^=;\[\]]|                    # Key names cannot contain equal signs, semi-colons or square brackets,
      \[[^\[\]]*\]                  # unless they are balanced and not nested
    )+?)
    \s*=\s*                         # Key/value pairs are separated by equal signs (ignoring white-space)
    (?:
      ("(?:[^"]|(?<=\\\\)")*")|     # Double-quoted string, which may contain slash-escaped quotes/slashes
      (\'(?:[^\']|(?<=\\\\)\')*\')| # Single-quoted string, which may contain slash-escaped quotes/slashes
      ([^\r\n]*?)                   # Non-quoted string
    )\s*$                           # Stop at the next end of a line, ignoring trailing whitespace
    @msx', $data, $matches, PREG_SET_ORDER)) {
    foreach ($matches as $match) {
      // Fetch the key and value string
      $i = 0;
      foreach (array('key', 'value1', 'value2', 'value3') as $var) {
        $$var = isset($match[++$i]) ? $match[$i] : '';
      }
      $value = stripslashes(substr($value1, 1, -1)) . stripslashes(substr($value2, 1, -1)) . $value3;

      // Remove any invalid UTF-8 sequences to prevent serialization errors.
      if (function_exists('mb_convert_encoding')) {
        $value = mb_convert_encoding($value, 'UTF-8', 'UTF-8');
      }

      // Parse array syntax
      $keys = preg_split('/\]?\[/', rtrim($key, ']'));
      $last = array_pop($keys);
      $parent = &$info;

      // Create nested arrays
      foreach ($keys as $key) {
        if ($key == '') {
          $key = count($parent);
        }
        if (!isset($parent[$key]) || !is_array($parent[$key])) {
          $parent[$key] = array();
        }
        $parent = &$parent[$key];
      }

      // Handle PHP constants.
      if (isset($constants[$value])) {
        $value = $constants[$value];
      }

      // Insert actual value
      if ($last == '') {
        $last = count($parent);
      }
      $parent[$last] = $value;
    }
  }

  return $info;
}

/**
 * Query the core system for packages of a certain type.
 */
function _provision_system_query($type) {
  $entries = array();
  $db_name = _provision_backdrop_site_db_name();
  if (empty($db_name) || !preg_match('/^[A-Za-z0-9_]+$/', $db_name)) {
    drush_log(dt('Backdrop system map: unsafe or empty db_name (@n); skipping.', array('@n' => $db_name)), 'warning');
    return $entries;
  }
  // Read the site {system} table straight through the provision DB service (its
  // own PDO handle, connected as the master admin with no default schema
  // selected) so the package map needs no in-process Backdrop bootstrap -- the
  // parent process never gets a complete one (see backdrop/install.inc).
  // Fully-qualify and backtick the table: `system` is a reserved word in
  // MySQL 8, and no default schema is selected on this connection. The type
  // value goes through the service's quoted %s placeholder; db_name is
  // identifier-validated above.
  $result = d()->service('db')->query("SELECT * FROM `" . $db_name . "`.`system` WHERE type = '%s'", $type);
  if ($result) {
    $result->setFetchMode(PDO::FETCH_OBJ);
    foreach ($result as $entry) {
      $entries[] = $entry;
    }
  }
  return $entries;
}


/**
 * Map the system table to a packages multi-dimensional array component.
 */
function _provision_drupal_system_map() {
  $profiles = _provision_find_profiles();
  foreach ($profiles as $profile => $info) {
    if (!isset($info->version)) {
      $info->version = _provision_backdrop_get_version();
    }
    $profiles[$profile] = $info;
  }
  $packages['platforms'] = _provision_backdrop_find_platforms();

  $profile = drush_get_option('profile');
  $packages['profiles'][$profile] = $profiles[$profile];
  if (isset($profile) && isset($packages['profiles'][$profile])) {
    $packages['profiles'][$profile]->status = 1;
  }

  foreach (_provision_system_query("module") as $module) {
    // Skip profiles, these have a separate section above.
    if (preg_match('/\.profile$/', $module->filename)) {
      continue;
    }
    $info_file = sprintf("%s/%s.info", dirname($module->filename), $module->name);
    $module->info = provision_parse_info_file($info_file);

    // Skip hidden modules
    if (isset($module->info['hidden']) && $module->info['hidden'] == 1) {
      continue;
    }
    $frags = explode("/", $module->filename);
    // flag site-specific modules
    if ($frags[0] == 'sites' && $frags[1] != 'all') {
      $module->platform = -1;
    }

    $module->filename = realpath($module->filename);
    if ($module->schema_version == -1) {
      $module->schema_version = 0;
    }
    $packages['modules'][$module->name] = $module;
    $module->version = $module->info['version'];
  }

  drush_log(dt("Found !count modules", array('!count' => count(isset($packages['modules']) ? $packages['modules'] : array()))), 'info');

  // XXX: mostly a copy-paste from above
  foreach (_provision_system_query("theme") as $theme) {
    $frags = explode("/", $theme->filename);
    // flag site-specific themes
    if ($frags[0] == 'sites' && $frags[1] != 'all') {
      $theme->platform = -1;
    }
    $info_file = sprintf("%s/%s.info", dirname($theme->filename), $theme->name);
    $theme->info = provision_parse_info_file($info_file);
    $theme->filename = realpath($theme->filename);

    if ($theme->schema_version == -1) {
      $theme->schema_version = 0;
    }

    $packages['themes'][$theme->name] = $theme;
    $theme->version = $theme->info['version'];
  }
  drush_log(dt("Found !count themes", array('!count' => count(isset($packages['themes']) ? $packages['themes'] : array()))), 'info');
  return $packages;
}

/**
 * Find available profiles on this platform.
 *
 * Backdrop core profiles live under core/profiles; contrib/custom under
 * profiles. Scan both.
 */
function _provision_find_profiles() {
  $profiles = array();

  if (file_exists('core/includes/install.inc')) {
    include_once('core/includes/install.inc');
  }
  elseif (file_exists('includes/install.inc')) {
    include_once('includes/install.inc');
  }

  $profiles_subdirs = array();
  if (is_dir('./core/profiles')) {
    $profiles_subdirs[] = "./core/profiles";
  }
  $profiles_subdirs[] = "./profiles";

  foreach($profiles_subdirs as $profiles_subdir) {
    if (!$dir = @opendir($profiles_subdir)) {
      drush_log(dt("Cannot find profiles directory @dir", array('@dir' => $profiles_subdir)), 'notice');
      continue;
    }

    while (FALSE !== ($name = readdir($dir))) {
      $languages = array();
      if (($name == '..') || ($name == '.') || (!is_dir("$profiles_subdir/$name"))) {
        continue;
      }

      $profile = new stdClass();
      $profile->name = $name;
      $profile->info = array();

      $info_file = "$profiles_subdir/$name/$name.info";
      if (file_exists($info_file)) {
        $profile->info = provision_parse_info_file($info_file);
        // Skip hidden profiles
        if (isset($profile->info['hidden']) && $profile->info['hidden'] == 1) {
          continue;
        }
      }
      $profile->filename = $info_file;

      // Include code from the profile.
      if (file_exists($profile_file = "$profiles_subdir/$name/$name.profile")) {
        require_once($profile_file);
      }

      $func = $profile->name . "_profile_details";
      if (function_exists($func)) {
        $profile->info = array_merge($profile->info, $func());
      }

      $profile_path = $profiles_subdir . '/' . $name;
      $profile->info['languages'] = _provision_find_profile_languages($profile_path);

      // Aegir allows projects to specify an "old short name" to provide an
      // upgrade path when projects get renamed.
      if ($profile->name == 'standard') {
        $profile->info['old_short_name'] = 'default';
      }
      $profile->info['old_short_name'] = isset($profile->info['old_short_name']) ? $profile->info['old_short_name'] : 'standard';


      $profiles[$name] = $profile;
      drush_log(dt('Found install profile %name', array('%name' => $name)), 'info');
    }
  }
  return $profiles;
}

/**
 * Retrieve a list of paths to search in a certain scope.
 *
 * The base scope adds Backdrop's core/ tier: core modules/themes live under
 * core/modules and core/themes, contrib under the top-level modules/themes.
 */
function _provision_drupal_search_paths($scope, $key = '', $type = 'modules') {
  $searchpaths = array();
  // BackdropBoot's DRUPAL_ROOT bootstrap does not populate the DRUSH_DRUPAL_ROOT
  // context, so drush_get_context() returns its array() default -- which would
  // make every search path "Array/core/modules" and register ZERO module/theme
  // packages (only the profile, found CWD-relative, shows up). That breaks the
  // front-end package map the migrate/clone tasks diff between platforms. Fall
  // back to the explicitly-set selected root, then the context root.
  $drupal_root = drush_get_context('DRUSH_DRUPAL_ROOT');
  if (!is_string($drupal_root) || $drupal_root === '') {
    $drupal_root = drush_get_context('DRUSH_SELECTED_DRUPAL_ROOT');
  }
  if (!is_string($drupal_root) || $drupal_root === '') {
    $drupal_root = d()->root;
  }
  switch ($scope) {
    case 'base' :
      $searchpaths[] = sprintf("%s/core/%s", $drupal_root, $type);
      $searchpaths[] = sprintf("%s/%s", $drupal_root, $type);
      $searchpaths[] = sprintf("%s/sites/all/%s", $drupal_root, $type);
      break;
    default :
      if ($key) {
        $searchpaths[] = sprintf("%s/%s/%s/%s", $drupal_root, $scope, $key, $type);
      }
      break;

  }
  return $searchpaths;
}
