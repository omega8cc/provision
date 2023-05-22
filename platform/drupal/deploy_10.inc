<?php

/**
 * @file
 *
 * Update paths inside database content for sites cloned/migrated/renamed.
 */

$new_url = drush_get_option('uri');
$old_url = drush_get_option('old_uri');

/**
 * @file
 *   Handle site migration tasks for redeployed sites.
 *   This is primarily to handle the rename of the sites
 *   directories.
 */

if ($new_url != $old_url) {
  $url_changed  = TRUE;
}
else {
  $url_changed  = FALSE;
}

if ($url_changed) {
  _provision_platform_drupal_deploy_9_replace($old_url, $new_url);
  _provision_platform_drupal_deploy_9_replace('default', $new_url);

  // Update files paths
  \Drupal::configFactory()->getEditable('system.file')
    ->set('path.private', "sites/$new_url/private/files")
    ->set('path.temporary', "sites/$new_url/private/temp")
    ->save();

  // Detect and update default site name, if needed
  $old_name = \Drupal::config('system.site')->get('name');
  if ($old_name == $old_url) {
    \Drupal::configFactory()->getEditable('system.site')
      ->set('name', "$new_url")
      ->save();
  }
}

function _provision_platform_drupal_deploy_9_replace($old_url, $new_url) {
  // Update absolute URLs to files for sites cloned/migrated/renamed in the multisite install.
  $replace_abs_patterns = array(
    ':old' => '//' . $old_url . '/sites/' . $old_url . '/files',
    ':new' => '//' . $new_url . '/sites/' . $new_url . '/files',
  );

  // Update paths for sites cloned/migrated/renamed in the multisite install.
  $replace_patterns = array(
    ':old' => 'sites/' . $old_url,
    ':new' => 'sites/' . $new_url,
  );

  drush_log(
    dt('Changed paths from sites/@old_url to sites/@new_url',
    array('@old_url' => $old_url, '@new_url' => $new_url)));
}

