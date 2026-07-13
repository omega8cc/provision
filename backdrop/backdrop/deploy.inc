<?php

/**
 * @file
 *   Handle Backdrop site rename tasks for redeployed (cloned/migrated) sites.
 *   Primarily the rename of the sites/<url> directory references and the site
 *   name.
 *
 * Backdrop equivalent of platform/drupal/deploy_7.inc. Runs in-process under a
 * FULL bootstrap. The site name lives in the system.core config object (JSON),
 * NOT the {variable} table (D7) or \Drupal::config (D8+), so it is rewritten via
 * the config API.
 */

$new_url = drush_get_option('uri');
$old_url = drush_get_option('old_uri');

if ($new_url != $old_url) {
  // Rewrite the site name if it embeds the old URL, via the config API.
  if (function_exists('config_get') && function_exists('config_set')) {
    $site_name = config_get('system.core', 'site_name');
    $old_url_pattern_short = '/' . preg_quote($old_url, '/') . '/';
    if ($site_name && preg_match($old_url_pattern_short, $site_name)) {
      config_set('system.core', 'site_name', preg_replace($old_url_pattern_short, $new_url, $site_name));
    }
  }

  // Update sites/<old_url> path references in text content.
  _provision_backdrop_deploy_replace($old_url, $new_url);
  _provision_backdrop_deploy_replace('default', $new_url);
}

function _provision_backdrop_deploy_replace($old_url, $new_url) {
  // Update absolute URLs to files for sites cloned/migrated/renamed.
  $replace_abs_patterns = array(
    ':old' => '//' . $old_url . '/sites/' . $old_url . '/files',
    ':new' => '//' . $new_url . '/sites/' . $new_url . '/files',
  );

  // Update relative paths for sites cloned/migrated/renamed.
  $replace_patterns = array(
    ':old' => 'sites/' . $old_url,
    ':new' => 'sites/' . $new_url,
  );

  // Backdrop is D7-shaped; guard every table/API so a missing one is skipped.
  if (function_exists('db_table_exists') && db_table_exists('block_custom')) {
    db_query("UPDATE {block_custom} SET body = REPLACE(body, :old, :new)", $replace_abs_patterns);
    db_query("UPDATE {block_custom} SET body = REPLACE(body, :old, :new)", $replace_patterns);
  }

  if (function_exists('field_info_field_map')) {
    $field_map = field_info_field_map();
    foreach ($field_map as $key => $info) {
      $tablekey = db_escape_table($key);
      // Replace in all known text fields.
      if (in_array($info['type'], array('text_long', 'text_with_summary'))) {
        drush_log('Replacing in ' . $key, 'debug');
        db_query("UPDATE {field_data_${tablekey}} SET ${tablekey}_value     = REPLACE(${tablekey}_value,   :old, :new)", $replace_abs_patterns);
        db_query("UPDATE {field_revision_${tablekey}} SET ${tablekey}_value = REPLACE(${tablekey}_value,   :old, :new)", $replace_abs_patterns);
        db_query("UPDATE {field_data_${tablekey}} SET ${tablekey}_value     = REPLACE(${tablekey}_value,   :old, :new)", $replace_patterns);
        db_query("UPDATE {field_revision_${tablekey}} SET ${tablekey}_value = REPLACE(${tablekey}_value,   :old, :new)", $replace_patterns);
      }

      // Replace in the optional summary.
      if ($info['type'] == 'text_with_summary') {
        drush_log('Replacing summary in ' . $key, 'debug');
        db_query("UPDATE {field_data_${tablekey}} SET ${tablekey}_summary     = REPLACE(${tablekey}_summary, :old, :new)", $replace_abs_patterns);
        db_query("UPDATE {field_revision_${tablekey}} SET ${tablekey}_summary = REPLACE(${tablekey}_summary, :old, :new)", $replace_abs_patterns);
        db_query("UPDATE {field_data_${tablekey}} SET ${tablekey}_summary     = REPLACE(${tablekey}_summary, :old, :new)", $replace_patterns);
        db_query("UPDATE {field_revision_${tablekey}} SET ${tablekey}_summary = REPLACE(${tablekey}_summary, :old, :new)", $replace_patterns);
      }
    }
  }
  drush_log(
    dt('Changed paths from sites/@old_url to sites/@new_url',
    array('@old_url' => $old_url, '@new_url' => $new_url)));
}
