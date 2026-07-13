<?php
/**
 * @file
 *   Import an existing Backdrop site's settings into Ægir.
 *
 * Backdrop equivalent of platform/drupal/import_7.inc. Runs in-process under a
 * FULL bootstrap: Backdrop's bootstrap normalises the scalar $database into
 * $databases['default']['default'] (driver/username/host/password/database), so
 * the D7-shaped read works unchanged. Backdrop keeps the active install profile
 * in the system.core config object, not the {variable} table.
 */

global $databases;
if ($db = $databases['default']['default']) {
  drush_set_option('db_type', urldecode($db['driver']), 'site');
  drush_set_option('db_user', urldecode($db['username']), 'site');
  drush_set_option('db_host', urldecode($db['host']), 'site');
  drush_set_option('db_passwd', urldecode($db['password']), 'site');
  drush_set_option('db_name', urldecode($db['database']), 'site');

  // Active install profile: read from the system.core config, default 'standard'.
  $profile = 'standard';
  if (function_exists('config_get')) {
    $configured = config_get('system.core', 'install_profile');
    if (!empty($configured)) {
      $profile = $configured;
    }
  }
  drush_set_option('profile', $profile, 'site');

  // Default site language.
  $langcode = 'en';
  if (function_exists('language_default')) {
    $language = language_default();
    if (is_object($language)) {
      if (isset($language->langcode)) {
        $langcode = $language->langcode;
      }
      elseif (isset($language->language)) {
        $langcode = $language->language;
      }
    }
  }
  drush_set_option('language', $langcode, 'site');
}
