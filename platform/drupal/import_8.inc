<?php
/**
 * Drupal 8 and newer load settings.php inside a method, so the $databases
 * the Drupal 7 script reads is never a global here: the site's facts come
 * through the core APIs. The credentials stay as Drupal holds them; on the
 * command line BOA's settings.php recovers them raw.
 */
$db = \Drupal\Core\Database\Database::getConnectionInfo('default');
if (!empty($db['default'])) {
  $db = $db['default'];
  drush_set_option('db_type', $db['driver'], 'site');
  drush_set_option('db_user', $db['username'], 'site');
  drush_set_option('db_host', $db['host'], 'site');
  drush_set_option('db_passwd', $db['password'], 'site');
  drush_set_option('db_name', $db['database'], 'site');

  $profile = method_exists('\Drupal', 'installProfile') ? \Drupal::installProfile() : '';
  if (empty($profile)) {
    $profile = \Drupal\Core\Site\Settings::get('install_profile', 'standard');
  }
  drush_set_option('profile', $profile, 'site');
  drush_set_option('language', \Drupal::languageManager()->getDefaultLanguage()->getId(), 'site');
}
