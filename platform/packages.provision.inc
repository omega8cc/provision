<?php

/**
 * @file
 * Code related to provision-packages tasks.
 */

/**
 * Provision packages command.
 *
 * Disable an enabled and installed site
 */
function drush_provision_drupal_provision_packages_validate() {
  if (d()->type === 'site') {
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
  }
}

/**
 * Implements drush_hook_pre_COMMAND().
 */
function drush_provision_drupal_pre_provision_packages() {
  if (d()->type === 'platform') {
    drush_log(dt("This platform is running @short_name @version", array('@short_name' => 'drupal', '@version' => drush_drupal_version())), 'ok');
    $packages_list =  _scrub_object(provision_find_packages());
    foreach ($packages_list as $type => $packages) {

      // Print out human readable messages about the packages.
      if ($type == 'profiles') {
        drush_log(dt("Found @count '@type' packages in platform: @packages", array(
            '@count' => count($packages),
            '@type' => $type,
            '@packages' => implode(', ', array_keys($packages)))
        ), 'ok');
      }
      else {
        foreach ($packages as $package_type => $type_list) {
          drush_log(dt("Found @count packages of type '@package_type' in platform (@type): @packages", array(
              '@count' => count($type_list),
              '@type' => $type,
              '@package_type' => $package_type,
              '@packages' => implode(', ', array_keys($type_list)))
          ), 'ok');
        }
      }
    }
  }
  elseif (d()->type === 'site') {
    $packages_list = _scrub_object(provision_drupal_system_map());
  }

  // Using drush_set_option makes the data visible when using "--backend" option.
  drush_set_option('packages', $packages_list);

}

/**
 * Implements drush_hook_provision_post_COMMAND().
 */
function drush_provision_drupal_post_provision_packages() {
}
