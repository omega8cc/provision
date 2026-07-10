<?php
/**
 *  @file
 *    Find modules, themes and theme engines on a Backdrop site.
 *
 * Backdrop equivalent of the D7 branch of platform/drupal/verify.inc. Backdrop
 * is D7-lineage, so system_rebuild_module_data() / system_rebuild_theme_data()
 * exist and behave as on D7; backdrop_system_listing() is the native listing
 * helper (Backdrop has no theme engines, so 'engines' is normally empty).
 * Runs in-process under the FULL bootstrap the verify_validate hook performs.
 */

drush_set_option('modules', _scrub_object(system_rebuild_module_data()), 'site');
drush_set_option('themes', _scrub_object(system_rebuild_theme_data()), 'site');
drush_set_option('engines', _scrub_object(backdrop_system_listing('/\.engine$/', 'themes/engines')), 'site');
