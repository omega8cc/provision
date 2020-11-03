<?php
/**
 *  @file
 *    Find modules, themes and themes engines
 */

if (drush_drupal_major_version() >= 8) {
  // system_rebuild_module_data() is deprecated in D8 and removed from D9
  // https://www.drupal.org/project/drupal/issues/2926068
  if (version_compare(drush_drupal_version(), '8.7.0', '>=') ||
    !function_exists('system_rebuild_module_data')) {
    $extensions_rset = \Drupal::service('extension.list.module')->reset();
    $extensions_data = \Drupal::service('extension.list.module')->getList();
  }
  else {
    $extensions_data = system_rebuild_module_data();
  }
  drush_set_option('modules', _scrub_object($extensions_data), 'site');
  // Could be replaced with:
  // \Drupal::service('extension.list.theme')->reset()
  // \Drupal::service('extension.list.theme')->getList()
  // https://www.drupal.org/project/drupal/issues/2942001
  drush_set_option('themes', _scrub_object(\Drupal::service('theme_handler')->rebuildThemeData()), 'site');
}
elseif (drush_drupal_major_version() == 7) {
  drush_set_option('modules', _scrub_object(system_rebuild_module_data()), 'site');
  drush_set_option('themes', _scrub_object(system_rebuild_theme_data()), 'site');
}
else {
  drush_set_option('modules', _scrub_object(module_rebuild_cache()), 'site');
  drush_set_option('themes', _scrub_object(system_theme_data()), 'site');
}

drush_set_option('engines', _scrub_object(drupal_system_listing('\.engine$', 'themes/engines')), 'site');
