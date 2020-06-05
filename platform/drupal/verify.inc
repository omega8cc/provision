<?php
/**
 *  @file
 *    Find modules, themes and themes engines
 */

if (drush_drupal_major_version() >= 8) {
  // system_rebuild_module_data() is deprecated in D8 and removed from D9
  // https://www.drupal.org/project/drupal/issues/2926068
  $extensions_rset = \Drupal::service('extension.list.module')->reset();
  drush_set_option('modules', _scrub_object(\Drupal::service('extension.list.module')->getList()), 'site');
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
