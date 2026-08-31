<?php
/**
 * @file
 * Provision hooks for the provision deploy command (TXP layer).
 *
 * Deploy = "make this extracted archive serve at THIS uri": clone, migrate,
 * rename and restore all land here. The DB import is the CMS-agnostic db
 * service's job; this layer extracts, swaps, re-renders the config from the
 * TARGET context (the settings writer is the config authority, so the rename
 * of config.php keys is automatic), repairs the tree, and then runs the SQL
 * rewrite + security scrub engine (txp/txp/deploy.inc) — the part with teeth:
 * prefs re-point, blog_uid regeneration (feed GUID seed AND CSRF salt),
 * txp_token truncation (the source's live reset tokens must not authenticate
 * on the copy), log/nonce truncation.
 */

function drush_provision_txp_provision_deploy_validate($backup_file = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (_provision_txp_php_bin() === FALSE) {
    return drush_set_error('PROVISION_TXP_PHP_TOO_OLD',
      dt('No PHP >= 8.4 binary found; TXP tasks refuse to run below the enforced floor.'));
  }
  $exists = provision_file()->exists($backup_file)
    ->succeed('Deploying site from @path')
    ->fail('Could not find backup file @path', 'PROVISION_BACKUP_NOT_FOUND')
    ->status();
  if ($exists) {
    drush_set_option('backup_file', $backup_file);
  }
  $exists = provision_file()->exists(d()->root)
    ->succeed('Platform path @path exists')
    ->fail('Platform path @path does not exist', 'PROVISION_ROOT_NOT_FOUND')
    ->status();

  // Extraction target + replace-mode flags (the shared validate that computes
  // these is union-guarded away): extract beside the live site when replacing
  // (restore), straight into place for a fresh target (clone/migrate).
  $site_present = _provision_txp_site_exists();
  if ($site_present) {
    drush_set_option('extract_path', d()->site_path . '.restore');
    drush_set_option('old_db_name', drush_get_option('db_name', ''));
  }
  else {
    drush_set_option('extract_path', d()->site_path);
  }
  drush_set_option('deploy_replace_site', $site_present);
}

function drush_provision_txp_pre_provision_deploy($backup_file) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  $extracted = provision_file()->extract($backup_file, drush_get_option('extract_path'))
    ->succeed('Successfully extracted the contents of @path')
    ->fail('Failed to extract the contents of @path to @target', 'PROVISION_BACKUP_EXTRACTION_FAILED')
    ->status();

  if ($extracted) {
    // Web-group access for the per-site writable set of the extracted tree.
    $web_group = d('@server_master')->web_group;
    foreach (array('/public/files', '/public/images', '/public/themes', '/admin/plugins', '/tmp', '/private') as $sub) {
      $path = drush_get_option('extract_path') . $sub;
      if (is_dir($path)) {
        provision_file()->chgrp($path, $web_group, TRUE)
          ->succeed('Changed group ownership of <code>@path</code> to @gid')
          ->fail('Could not change group ownership of <code>@path</code> to @gid');
      }
    }

    if (drush_get_option('deploy_replace_site', FALSE)) {
      $old = d()->site_path . '.restore';
      $new = d()->site_path;
      $swapped = provision_file()->switch_paths($old, $new)
        ->succeed('Swapping out the @path1 and @path2 directories was successful.')
        ->fail('Swapping the @path1 and @path2 directories has failed.', 'DRUSH_PERM_ERROR')
        ->status();
      if ($swapped) {
        drush_set_option('site_dirs_swapped', TRUE);
      }
      else {
        return FALSE;
      }
    }

    // The new database is created; persist the info and re-render the config
    // from the TARGET context (new uri/db creds — the whole config-key half of
    // the rewrite list happens right here, by re-render).
    provision_prepare_environment();
    provision_save_site_data();
    _provision_txp_create_settings_file();
    provision_drupal_push_site($override_slave_authority = TRUE);
  }
}

function drush_provision_txp_pre_provision_deploy_rollback() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (drush_get_option('deploy_replace_site', FALSE)
    && drush_get_option('site_dirs_swapped', FALSE)) {
    // Swap the site directories back: a failed replace-deploy must leave the
    // ORIGINAL tree live, not the extracted archive (live-proven: the first
    // failed restore left the archive tree serving and .restore holding the
    // real site).
    $old = d()->site_path . '.restore';
    $new = d()->site_path;
    provision_file()->switch_paths($old, $new)
      ->succeed('Swapping out the @path1 and @path2 directories was successful.')
      ->fail('Swapping the @path1 and @path2 directories has failed.', 'DRUSH_PERM_ERROR');
    _provision_txp_ensure_db_creds_loaded();
    _provision_txp_create_settings_file();
    provision_save_site_data();
  }
  elseif (!drush_get_option('site_dirs_swapped', FALSE)) {
    _provision_recursive_delete(drush_get_option('extract_path'));
  }
}

function drush_provision_txp_provision_deploy() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  // Tree/symlink/perm repair on the deployed dir (recomputes the 4 admin
  // symlinks for THIS platform depth, heals dereferenced-restore states,
  // re-asserts the ownership model).
  _provision_txp_create_directories();
}

function drush_provision_txp_post_provision_deploy() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  // The SQL rewrite + security scrub (see txp/txp/deploy.inc).
  drush_include_engine('txp', 'deploy');
  // Fresh package map for the frontend.
  drush_set_option('packages', _scrub_object(provision_txp_system_map()), 'site');
}
