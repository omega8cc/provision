<?php
/**
 * @file
 * Provision hooks for the provision deploy command (TXP layer).
 *
 * Deploy = "make this extracted archive serve at THIS uri": clone, migrate,
 * rename and restore all land here. The DB import is the CMS-agnostic db
 * service's job; this layer extracts, swaps, re-renders the config from the
 * TARGET context (the settings writer is the config authority, so the config
 * half of the rewrite list happens by re-render), repairs the tree, and then
 * runs the SQL rewrite + security scrub engine (txp/txp/deploy.inc).
 *
 * The post hook additionally owns the two cleanups the shared post hook would
 * have done (it is union-guarded away): dropping the OLD database and removing
 * the sites/<uri>.restore tree. Neither is optional -- a leftover .restore
 * carries a full copy of private/config.php AND is matched by
 * provision_txp_find_sites(), which would deadlock platform delete and show a
 * phantom site in the frontend.
 */

function drush_provision_txp_provision_deploy_validate($backup_file = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (_provision_txp_php_bin() === FALSE) {
    return drush_set_error('PROVISION_TXP_PHP_TOO_OLD',
      dt('No PHP >= 8.4 binary found; TXP tasks refuse to run below the enforced floor.'));
  }
  $backup_exists = provision_file()->exists($backup_file)
    ->succeed('Deploying site from @path')
    ->fail('Could not find backup file @path', 'PROVISION_BACKUP_NOT_FOUND')
    ->status();
  if ($backup_exists) {
    drush_set_option('backup_file', $backup_file);
  }
  provision_file()->exists(d()->root)
    ->succeed('Platform path @path exists')
    ->fail('Platform path @path does not exist', 'PROVISION_ROOT_NOT_FOUND')
    ->status();

  // Extraction target + replace-mode flags (the shared validate that computes
  // these is union-guarded away). Keyed on DIRECTORY existence like both
  // precedents -- NOT on a readable private/config.php: a site dir left
  // config-less by an earlier half-failed deploy must still take the
  // extract-beside-and-swap path, never an extract-into-the-live-directory
  // merge with no atomic point and no old_db_name captured.
  $site_present = provision_file()->exists(d()->site_path)->status();
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
    // Before anything writes into the extracted tree: a copy landing at a new
    // uri must not inherit the source's file-store link as its private/ (see
    // the helper).
    $deploy_class = drush_get_option('deploy_class', '');
    if (!is_string($deploy_class) || $deploy_class === '') {
      $old_uri = drush_get_option('old_uri', '');
      $deploy_class = (drush_get_option('is_restore', FALSE) || ($old_uri !== '' && $old_uri === d()->uri)) ? 'restore' : 'clone';
    }
    if ($deploy_class !== 'restore') {
      _provision_txp_deploy_materialise_private(drush_get_option('extract_path'));
      if (drush_get_error()) {
        return FALSE;
      }
    }

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
    // from the TARGET context (new uri/db creds).
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
    // ORIGINAL tree live, not the extracted archive.
    $old = d()->site_path . '.restore';
    $new = d()->site_path;
    provision_file()->switch_paths($old, $new)
      ->succeed('Swapping out the @path1 and @path2 directories was successful.')
      ->fail('Swapping the @path1 and @path2 directories has failed.', 'DRUSH_PERM_ERROR');

    // Re-render config.php with the ORIGINAL credentials, read from the
    // swapped-back site's own drushrc.php into LOCALS.
    //
    // Two traps, both live-proven, both avoided here by never touching the
    // 'site' option context: (1) _provision_txp_ensure_db_creds_loaded() is a
    // NO-OP at this point -- the db service's create_site_database() has
    // already overwritten db_name/db_passwd in that context with the NEW
    // site's values, so its "already loaded" guard trips and the config would
    // be rendered with credentials about to be dropped; (2) writing the OLD
    // credentials back into the 'site' context would make the db layer's own
    // rollback (destroy_site_database(), which reads that context) drop the
    // site's REAL database instead of the new one.
    $creds = _provision_txp_drushrc_creds(d()->site_path . '/drushrc.php');
    if (!empty($creds)) {
      $data = array_merge((array) drush_get_context('site'), $creds);
      $config = new Provision_Config_Txp_Settings(d()->name, $data);
      $config->write();
      drush_log(dt('Restored the original database credentials in private/config.php after the failed deploy.'), 'ok');
    }
    else {
      drush_log(dt('Could not read the original credentials from @rc; run Verify on this site to re-render private/config.php.',
        array('@rc' => d()->site_path . '/drushrc.php')), 'warning');
    }

    // The extracted archive tree (now back under .restore) is spent.
    _provision_recursive_delete(d()->site_path . '.restore');
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

  // Remove the .restore tree. MUST happen on success: it holds a full copy of
  // the pre-deploy site incl. private/config.php with literal credentials, and
  // provision_txp_find_sites() would otherwise report it as a phantom site
  // forever (deadlocking platform delete).
  if (!drush_get_error() && drush_get_option('deploy_replace_site', FALSE)) {
    _provision_recursive_delete(drush_get_option('extract_path'));
  }

  // Drop the old database (mirrors both precedents; the shared post hook that
  // would have done it is union-guarded away, so without this every
  // replace-deploy leaves an orphaned database and its grants behind).
  if (!drush_get_error() && drush_get_option('deploy_replace_site', FALSE)) {
    if ($old_database = drush_get_option('old_db_name', '')) {
      if (!d()->service('db')->drop_database($old_database)) {
        drush_log(dt('Failed to drop database @old_database', array('@old_database' => $old_database)), 'warning');
      }
      else {
        drush_log(dt('Dropped the old database (@old_database).', array('@old_database' => $old_database)), 'info');
      }
    }
  }
}

/**
 * Give the extracted copy a private/ directory of its own.
 *
 * The site archive preserves symlinks on purpose (see the file header of
 * txp/backup.provision.inc), so a source whose private/ was moved into the
 * per-account static file store and replaced by a link arrives here as that
 * same ABSOLUTE link, naming the SOURCE's store. Everything the deploy does
 * next then writes through it: the web-group pass follows it, and the settings
 * writer renders THIS copy's database credentials into the source's
 * private/config.php -- after which the source reads the copy's database and
 * the two sites share one private directory.
 *
 * So a private/ that is a link resolving OUTSIDE the tree being deployed is
 * replaced by a real directory of this site's own, carrying the regular files
 * the link pointed at when they are readable; an unreadable store still leaves
 * a correct empty directory, because the settings writer re-renders config.php
 * a few lines further down. A link that resolves inside the extracted tree is
 * the site's own and is left alone.
 *
 * Never called for 'restore': a restore re-deploys the same site at the same
 * uri, so the archived link names that site's own store and must survive.
 */
function _provision_txp_deploy_materialise_private($extract_path) {
  $private = $extract_path . '/private';
  if (!is_link($private)) {
    return;
  }
  $extract_real = realpath($extract_path);
  $target = realpath($private);
  if ($target !== FALSE && $extract_real !== FALSE
    && strpos($target . '/', $extract_real . '/') === 0) {
    return;
  }
  $named = ($target === FALSE) ? dt('an unresolvable path') : $target;

  // Gathered before the link goes. Regular files only: a nested link would
  // point out of the copy again, and a directory under private/ is not part of
  // what this layer puts there.
  $carry = array();
  if ($target !== FALSE && is_dir($target)) {
    $handle = @dir($target);
    if (!empty($handle)) {
      while (($entry = $handle->read()) !== FALSE) {
        if ($entry === '.' || $entry === '..') {
          continue;
        }
        $file = $target . '/' . $entry;
        if (is_file($file) && !is_link($file) && is_readable($file)) {
          $carry[$entry] = $file;
        }
      }
      $handle->close();
    }
  }

  provision_file()->unlink($private)
    ->fail('Could not remove <code>@path</code>');
  clearstatcache();
  if (is_link($private) || file_exists($private)) {
    return drush_set_error('PROVISION_TXP_DEPLOY_PRIVATE_STORE',
      dt('!path is a link into !target and could not be replaced. Deploying through it would write this copy\'s database credentials into another site\'s file store.',
      array('!path' => $private, '!target' => $named)));
  }
  provision_file()->mkdir($private)
    ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
  if (!is_dir($private)) {
    return;
  }
  foreach ($carry as $name => $file) {
    provision_file()->copy($file, $private . '/' . $name)
      ->fail('Could not copy <code>@source</code> to <code>@destination</code>');
  }
  provision_file()->chgrp($private, d('@server_master')->web_group)
    ->fail('Could not change group ownership of <code>@path</code> to @gid');
  provision_file()->chmod($private, 0750)
    ->fail('Could not change permissions of <code>@path</code> to @perm');

  drush_log(dt('Materialised private/ for this copy: the archive carried a link into !target, replaced here by a directory of this site\'s own with !n file(s) carried over.',
    array('!target' => $named, '!n' => count($carry))), 'ok');
}
