<?php
/**
 * @file
 * Package management code for Drupal 10.
 */

/**
 * Find themes in a certain scope.
 *
 * This function is based on _system_theme_data in Drupal 6 and Drupal 7.
 * We do not support, nor need information on subthemes at this point.
 */
function _provision_drupal_find_themes($scope, $key = '') {
  $paths = _provision_drupal_search_paths($scope, $key, 'themes');
  $files = array();
  $engines = array();
  foreach ($paths as $path) {
    $files = array_merge($files, drush_scan_directory($path, "/\.info\.yml$/", array('.', '..', 'CVS', '.svn'), 0, TRUE, 'name'));
    $engines = array_merge($engines, drush_scan_directory($path . "/engines", "/\.engine$/", array('.', '..', 'CVS', '.svn'), 0, TRUE, 'name'));
  }

  // Remove '.info' extension from the end of the file name.
  foreach($files as $name => $file) {
    $files[basename($name, '.info')] = $file;
    unset($files[$name]);
  }

  foreach ($files as $name => $file) {
    $files[$name]->info = Symfony\Component\Yaml\Yaml::parse(file_get_contents($file->filename));
    if (!empty($files[$name]->info['name'])) {
      $files[$name]->name = $files[$name]->info['name'];
    }

    // If version is missing, or if version is set to 'VERSION', use "drush_drupal_version()".
    // The VERSION constant used in Git checkouts. Also appears when using DEV modules in composer.
    if (empty($files[$name]->info['version']) || $files[$name]->info['version'] == 'VERSION') {
      $files[$name]->version = drush_drupal_version();
    }
    else {
      $files[$name]->version = $files[$name]->info['version'];
    }

    // @TODO: This is copied from the D7 implementation and I'm almost certain
    // that is doesn't even do anything in D7.
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
  }
  return $files;
}

/**
 * Map the system table to a packages multi-dimensional array component.
 */
function _provision_drupal_system_map() {
  $profiles = _provision_find_profiles();
  foreach ($profiles as $profile => $info) {
    if (empty($info->version)) {
      $info->version = drush_drupal_version();
    }
    $profiles[$profile] = $info;
  }
  $packages['platforms'] = _provision_find_platforms();

  $profile = drush_get_option('profile');
  $packages['profiles'][$profile] = $profiles[$profile];
  if (isset($profile) && isset($packages['profiles'][$profile])) {
    $packages['profiles'][$profile]->status = 1;
  }

  $packages['modules'] = _provision_system_query('module');
  drush_log(dt("Found !count modules", array('!count' => count($packages['modules']))), 'info');

  $packages['themes'] = _provision_system_query('theme');
  drush_log(dt("Found !count themes", array('!count' => count($packages['themes']))), 'info');

  return $packages;
}

/**
 * Query the core system for packages of a certain type.
 *
 * @param string $type
 *   E.g. module or theme.
 *
 * @return array
 *   List of packages.
 */
function _provision_system_query($type) {
  $packages = array();

  if ($type == 'theme') {
    /** @var \Drupal\Core\Extension\Extension[] $extensions_data */
    // Could be replaced with:
    // \Drupal::service('extension.list.theme')->reset()
    // \Drupal::service('extension.list.theme')->getList()
    // https://www.drupal.org/project/drupal/issues/2942001
    $extensions_data = \Drupal::service('theme_handler')->rebuildThemeData();
  }
  // Modules.
  else {
    /** @var \Drupal\Core\Extension\Extension[] $extensions_data */
    // $extensions_data = system_rebuild_module_data();
    // system_rebuild_module_data() is deprecated in D8 and removed in D9
    // https://www.drupal.org/project/drupal/issues/2926068
    $extensions_rset = \Drupal::service('extension.list.module')->reset();
    $extensions_data = \Drupal::service('extension.list.module')->getList();
  }

  foreach ($extensions_data as $name => $extension_data) {
    $package = (object) $extension_data->info;

    if ($type == 'module') {
      $package->filename = \Drupal::service('extension.list.module')->getPathname($name);
    }
    elseif ($type == 'theme') {
      $package->filename = \Drupal::service('extension.list.theme')->getPathname($name);
    }

    $frags = explode("/", $package->filename);
    // Flag site-specific packages.
    if ($frags[0] == 'sites' && $frags[1] != 'all') {
      $package->platform = -1;
    }
    $package->status = $extension_data->status;
    $package->filename = realpath($package->filename);

    if ($type == 'module') {
      if (drush_drupal_major_version() <= 9) {
        $package->schema_version = drupal_get_installed_schema_version($name);
      }
      else {
        $package->schema_version = \Drupal::service('update.update_hook_registry')->getInstalledVersion($name);
      }

      if(!file_exists($package->filename)) {
        drush_log(dt("@name.info.yml not found.", array("@name" => $name)), 'notice');
        continue;
      }
      else {
        $info = Symfony\Component\Yaml\Yaml::parse(file_get_contents($package->filename));
        // Skip hidden modules
        if (isset($info['hidden']) && $info['hidden'] != FALSE) {
          continue;
        }
        // The 'drupal_get_filename()' function is returning at least one profile as a module
        // (profile 'standard').  Make sure this is really a module.  If not, skip it.
        if (isset($info['type']) && $info['type'] != 'module') {
          continue;
        }
      }
    }

    $packages[$name] = $package;
  }

  return $packages;
}

/**
 * Find available profiles on this platform.
 */
function _provision_find_profiles() {
  $profile_dirs = array('./core/profiles', './profiles');

  $profiles = array();
  foreach($profile_dirs as $profile_dir) {
    if (!is_dir($profile_dir)) {
      drush_log(dt("Cannot find profiles directory %dir", array('%dir' => $profile_dir)), 'error');
      return FALSE;
    }

    $info_files = drush_scan_directory($profile_dir, "/\.info.yml$/");
    foreach ($info_files as $path => $info_file) {
      $path = realpath($path);
      $info = Symfony\Component\Yaml\Yaml::parse(file_get_contents($path));
      if ($info['type'] == 'profile' && (!isset($info['hidden']) || !$info['hidden'])) {
        $profile = new stdClass();
        $profile->name = $info['name'];
        $profile->info = $info;
        $profile->filename = $path;
        $profile->path = dirname($path);
        $profile->info['languages'] = _provision_find_profile_languages($profile->path);

        $profiles[basename($profile->path)] = $profile;
        drush_log(dt('Found install profile %name', array('%name' => $profile->name)), 'info');
      }
    }
  }
  return $profiles;
}

/**
 * Retrieve a list of paths to search in a certain scope
 */
function _provision_drupal_search_paths($scope, $key = '', $type = 'modules') {
  $searchpaths = array();
  $drupal_root = drush_get_context('DRUSH_DRUPAL_ROOT');
  switch ($scope) {
    case 'base':
      $searchpaths[] = sprintf("%s/%s", $drupal_root, $type);
      $searchpaths[] = sprintf("%s/core/%s", $drupal_root, $type);
      $searchpaths[] = sprintf("%s/sites/all/%s", $drupal_root, $type);
      break;
    case 'profiles':
      $profiles = _provision_find_profiles();
      if ($key) {
        if (isset($profiles[$key])) {
          // Find $type within this single profile.
          $searchpaths[] = sprintf("%s/%s", $profiles[$key]->path, $type);
        }
      }
      else {
        // Find $type within all profiles.
        foreach ($profiles as $profile) {
          $searchpaths[] = sprintf("%s/%s", $profile->path, $type);
        }
      }
      break;
    default:
      if ($key) {
        $searchpaths[] = sprintf("%s/%s/%s/%s", $drupal_root, $scope, $key, $type);
      }
      break;

  }
  return $searchpaths;
}

