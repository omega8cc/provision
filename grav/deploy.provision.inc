<?php

/**
 * @file
 * Deploy command implementation (Grav layer).
 *
 * Fork of backdrop/deploy.provision.inc collapsed to the DB-less capsule: the
 * CMS-agnostic extract/swap/rollback scaffolding is kept byte-similar (the
 * rollback swap-back is load-bearing -- a failed replace-deploy must never
 * leave the extracted archive serving); everything database (old-db capture,
 * updatedb, schema comparison, sites.php) is gone. Restore drives this via
 * provision-deploy exactly as the Drupal/Backdrop flows do.
 */

/**
 * Implementation of drush_hook_COMMAND_validate().
 */
function drush_provision_grav_provision_deploy_validate($backup_file = NULL) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  $exists = provision_file()->exists($backup_file)
    ->succeed('Deploying site from @path')
    ->fail('Could not find backup file @path', 'PROVISION_BACKUP_NOT_FOUND')
    ->status();
  if ($exists) {
    drush_set_option('backup_file', $backup_file);
  }

  $exists = provision_file()->exists(d()->site_path)
    ->succeed('Replacing the existing site at @path')
    ->status();

  if ($exists) {
    drush_set_option('extract_path', d()->site_path . '.restore');
  }
  else {
    drush_set_option('extract_path', d()->site_path);
  }

  drush_set_option('deploy_replace_site', $exists);
}

/**
 * Extract the archive and swap it into place.
 *
 * Implements drush_hook_pre_COMMAND().
 */
function drush_provision_grav_pre_provision_deploy($backup_file) {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  $extract_path = drush_get_option('extract_path');
  $extracted = provision_file()->extract($backup_file, $extract_path)
    ->succeed('Successfully extracted the contents of @path')
    ->fail('Failed to extract the contents of @path to @target', 'PROVISION_BACKUP_EXTRACTION_FAILED')
    ->status();

  if ($extracted) {
    // Recreate the volatile dirs the backup excludes and restore the group
    // ownership model on the extracted tree (spike-4-proven restore recipe).
    $web_group = d('@server_master')->web_group;
    foreach (_provision_grav_writable_dirs() as $dir) {
      $path = $extract_path . '/' . $dir;
      if (!is_dir($path)) {
        provision_file()->mkdir($path)
          ->succeed('Created <code>@path</code>')
          ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
      }
    }
    foreach (_provision_grav_writable_dirs() as $dir) {
      provision_file()->chgrp($extract_path . '/' . $dir, $web_group, TRUE)
        ->succeed('Changed group ownership of <code>@path</code> to @gid')
        ->fail('Could not change group ownership of <code>@path</code> to @gid');
    }
    foreach (_provision_grav_writable_dirs() as $dir) {
      provision_file()->chmod($extract_path . '/' . $dir, 02775, TRUE)
        ->succeed('Changed permissions of <code>@path</code> to @perm')
        ->fail('Could not change permissions of <code>@path</code> to @perm');
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

    provision_prepare_environment();
    provision_save_site_data();
    _provision_grav_write_env();
    provision_drupal_push_site($override_slave_authority = TRUE);
  }
}

/**
 * Swap back and remove the extracted site directory.
 *
 * Implementation of drush_hook_pre_COMMAND_rollback().
 */
function drush_provision_grav_pre_provision_deploy_rollback() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (drush_get_option('deploy_replace_site', FALSE)) {
    if (drush_get_option('site_dirs_swapped', FALSE)) {

      // Swap the site directories back if necessary: without this a failed
      // replace-deploy leaves the extracted archive serving (proven on the
      // TXP track).
      $old = d()->site_path . '.restore';
      $new = d()->site_path;

       provision_file()->switch_paths($old, $new)
         ->succeed('Swapping out the @path1 and @path2 directories was successful.')
         ->fail('Swapping the @path1 and @path2 directories has failed.', 'DRUSH_PERM_ERROR');

       _provision_grav_write_env();
       provision_save_site_data();
    }
  }

  if (provision_file()->exists(drush_get_option('extract_path'))->status()) {
    _provision_grav_remove_tree_or_park(drush_get_option('extract_path'), d()->uri . '-extract');
  }
}

/**
 * Implements drush_hook_COMMAND().
 */
function drush_provision_grav_provision_deploy() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  // Purge any cache namespace left in the shared Valkey/Redis store by a
  // previously deleted site of the same name (host-keyed; a Grav site never
  // uses the store itself in phase 1, but the front-end page-cache namespace
  // <host>_z_* belongs to the URI, not the CMS). Non-fatal by design.
  if (function_exists('_provision_valkey_purge_site')) {
    _provision_valkey_purge_site(d()->uri);
  }
  _provision_grav_create_directories();
}

/**
 * Implements drush_hook_post_COMMAND().
 */
function drush_provision_grav_post_provision_deploy() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  // No updatedb and no schema machinery for a DB-less capsule; the deployed
  // tree's integrity gate is the verify engine (vendor/ + index.php + version)
  // run by the restore flow's provision-verify.

  // Remove the restore directory (or park web-owned residue).
  if (!drush_get_error() && drush_get_option('deploy_replace_site', FALSE)) {
    _provision_grav_remove_tree_or_park(drush_get_option('extract_path'), d()->uri . '-restore');
  }
}
