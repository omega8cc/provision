<?php

/**
 * @file
 * Deploy command implementation (Grav layer).
 *
 * Fork of backdrop/deploy.provision.inc for the capsule: the CMS-agnostic
 * extract/swap/rollback scaffolding is kept byte-similar (the rollback
 * swap-back is load-bearing -- a failed replace-deploy must never leave the
 * extracted archive serving); updatedb, schema comparison and sites.php are
 * gone. For a site with a database the db layer creates the new database and
 * imports the dump around these hooks. The replaced database is not dropped
 * here: the restore's own post hook (db/restore.provision.inc) drops it once
 * the deploy and the verify after it both succeeded, so a restore that fails
 * at its verify still has it. Restore drives this via provision-deploy
 * exactly as the Drupal/Backdrop flows do.
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

  // A copy (Clone, Upgrade a copy) is unpacked beside its final path and
  // moved in whole once it is held (see the pre hook): BOA's minute runner
  // ticks any enabled site whose tree carries the store plugin, the copy's
  // alias is already saved enabled, and until its drushrc.php is rewritten
  // the archive's names the SOURCE's database.
  $deploy_class = (string) drush_get_option('deploy_class', '');
  $old_uri = (string) drush_get_option('old_uri', '');
  $copy = ($deploy_class === 'clone' || $deploy_class === 'upgrade')
    && $old_uri !== '' && $old_uri !== d()->uri;

  // A copy never replaces a tree: the replace path is a restore's, which
  // swaps the copy in at once and settles the hold as a restore does.
  if ($exists && $copy) {
    return drush_set_error('PROVISION_GRAV_COPY_TARGET',
      dt('@path already exists; a copy is never deployed over an existing site directory. Remove it first.', array('@path' => d()->site_path)));
  }

  if ($exists) {
    drush_set_option('extract_path', d()->site_path . '.restore');
  }
  elseif ($copy) {
    // A hidden name: never a site's (a domain has no leading dot) and never
    // matched by the platform's site list, which globs sites/*.
    drush_set_option('extract_path', dirname(d()->site_path) . '/.' . basename(d()->site_path) . '.copy');
  }
  else {
    drush_set_option('extract_path', d()->site_path);
  }

  drush_set_option('deploy_replace_site', $exists);
  drush_set_option('deploy_copy_staged', !$exists && $copy);
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
  // A pre-existing extract path is stale debris from an older failed deploy
  // (on the serial queue, by definition): extracting over its web-owned 0755
  // cache subdirs fails. Park it first so extraction always starts clean.
  if (is_dir($extract_path)
    && (drush_get_option('deploy_replace_site', FALSE) || drush_get_option('deploy_copy_staged', FALSE))) {
    if (!_provision_grav_remove_tree_or_park($extract_path, d()->uri . '-stale-extract')) {
      return FALSE;
    }
  }
  $extracted = provision_file()->extract($backup_file, $extract_path)
    ->succeed('Successfully extracted the contents of @path')
    ->fail('Failed to extract the contents of @path to @target', 'PROVISION_BACKUP_EXTRACTION_FAILED')
    ->status();

  if ($extracted) {
    // Recreate the volatile dirs the backup excludes and restore the group
    // ownership model on the extracted tree (spike-proven restore recipe).
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
      provision_file()->chgrp($extract_path . '/' . $dir, provision_path_web_group($extract_path . '/' . $dir, $web_group), TRUE)
        ->succeed('Changed group ownership of <code>@path</code> to @gid')
        ->fail('Could not change group ownership of <code>@path</code> to @gid');
    }
    foreach (_provision_grav_writable_dirs() as $dir) {
      _provision_grav_chmod_tree($extract_path . '/' . $dir);
    }

    if (drush_get_option('deploy_replace_site', FALSE)) {
      // The hold follows the tree being replaced, never the archive: a
      // backup taken while the site was a copy (Cutover's own pre-rename
      // copy among them) would otherwise hold a live store's worker for
      // good, and a copy restored from its source's backup would lose it.
      $held_marker = _provision_grav_copy_marker(d()->site_path);
      $staged_marker = _provision_grav_copy_marker($extract_path);
      if (is_file($held_marker) && !is_link($held_marker)) {
        if (!is_file($staged_marker) || is_link($staged_marker)) {
          if (is_link($staged_marker)) {
            @unlink($staged_marker);
          }
          _provision_grav_mark_copy($extract_path, trim((string) @file_get_contents($held_marker)), @filemtime($held_marker));
        }
      }
      elseif (file_exists($staged_marker) || is_link($staged_marker)) {
        provision_file()->unlink($staged_marker)
          ->succeed('The archive was taken while this site was a copy; its hold marker is not restored: the site stays as it was')
          ->fail('Could not remove the archive\'s copy marker @path');
      }

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
    elseif (drush_get_option('deploy_copy_staged', FALSE)) {
      // Held before it becomes visible: the marker is in the staged tree
      // when the rename puts it at the site path. The archive carries the
      // file only when its source was a copy itself; it is written again
      // either way, naming this copy's source.
      if (_provision_grav_has_db()) {
        _provision_grav_mark_copy($extract_path, _provision_grav_copy_source($extract_path, (string) drush_get_option('old_uri', '')));
        if (!is_file(_provision_grav_copy_marker($extract_path))) {
          return drush_set_error('PROVISION_GRAV_COPY_MARK',
            dt('Could not hold the copy at @path before moving it into place.', array('@path' => $extract_path)));
        }
      }
      if (file_exists(d()->site_path) || is_link(d()->site_path)) {
        return drush_set_error('PROVISION_GRAV_COPY_TARGET',
          dt('@path appeared while the copy was being unpacked; refusing to move the copy over it.', array('@path' => d()->site_path)));
      }
      if (!@rename($extract_path, d()->site_path)) {
        return drush_set_error('PROVISION_GRAV_COPY_MOVE',
          dt('Could not move the unpacked copy from @from to @to.', array('@from' => $extract_path, '@to' => d()->site_path)));
      }
      drush_set_option('deploy_copy_moved', TRUE);
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
       if (_provision_grav_has_db()) {
         // The original capsule is back with its own drushrc.php, which names
         // the database it runs on. The 'site' context holds the credentials
         // of the database this deploy made and its rollback drops: writing
         // them, here or in the exit hook, would point the original site at a
         // dropped database.
         drush_set_option('provision_save_config', FALSE);
       }
       else {
         provision_save_site_data();
       }
    }
  }

  if (drush_get_option('deploy_copy_moved', FALSE) && !drush_get_option('deploy_replace_site', FALSE)
    && is_dir(d()->site_path) && !is_link(d()->site_path)) {
    _provision_grav_remove_tree_or_park(d()->site_path, d()->uri . '-copy');
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

  // The identity engine (grav/grav/deploy.inc): a restore keeps the archive's
  // keys and overlay, a clone gets its own. Before the .restore removal so a
  // failure here leaves the swap-back path intact.
  drush_include_engine('grav', 'deploy');

  // Remove the restore directory (or park web-owned residue).
  if (!drush_get_error() && drush_get_option('deploy_replace_site', FALSE)) {
    _provision_grav_remove_tree_or_park(drush_get_option('extract_path'), d()->uri . '-restore');
  }
}
