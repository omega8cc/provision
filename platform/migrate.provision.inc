<?php
/**
 * @file
 * Migrate command implementation
 */

/**
 * Make sure we have a valid site being migrated, and that the file being migrated from exists
 *
 * Implementation of drush_hook_COMMAND_validate().
 */
function drush_provision_drupal_provision_migrate_validate($platform = NULL) {
  // Backdrop migrate is not implemented in Phase 1; guard so this D-lineage
  // migrate never runs against a Backdrop site (it would clobber settings.php
  // with a legacy Drupal file). The Backdrop migrate fork lands in Phase 2.
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  // Backup mode ctrl file cleanup
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }
  if (drush_drupal_major_version() >= 8) {
    // Lock Site-Local Drush if Unlocked
    provision_check_codebase_status();
    if (!d()->isConsolePatched || !d()->isCorePatched) {
      drush_log(dt('MIGRATE/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
      provision_lock_drush_local_vnd();
    }
  }
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
}

/**
 * Make a backup before making any changes, and add extract the file we are restoring from
 *
 * Implementation of drush_hook_pre_COMMAND().
 */
function drush_provision_drupal_pre_provision_migrate($platform, $new_name = NULL) {
  // Backdrop migrate is not implemented in Phase 1; guard so this D-lineage
  // migrate never runs against a Backdrop site (it would clobber settings.php
  // with a legacy Drupal file). The Backdrop migrate fork lands in Phase 2.
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  // Set offline mode to true and re-generate the settings.php file.
  // This will write a $conf['site_offline'] = 1; to the settings.php file.
  drush_log(dt("Putting site under maintenance"));
  d()->site_enabled = FALSE;
  _provision_drupal_create_settings_file();
  drush_log(dt('Invoking drush provision-backup in drush_provision_drupal_pre_provision_migrate'));
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  $internal_backup_flag_blank = "Internal-DB-Backup \n";
  $local_description = 'Adding Internal-DB-Backup Flag-File drush_provision_drupal_pre_provision_migrate';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, $internal_backup_flag_blank)
      ->succeed('Generated blank ' . $local_description)
      ->fail('Could not generate ' . $local_description);
  }
  drush_invoke('provision-backup');
  provision_drupal_push_site();

  drush_set_option('old_platform', d()->platform->name);

  // If we are renaming the site, pick this up early.
  if (!is_null($new_name) && ($new_name != d()->name)) {
    drush_set_option('target_name', $new_name);
    drush_set_option('source_name', d()->uri);
  }
  else {
    drush_set_option('target_name', d()->name);

    // Detect if the platform stays the same.
    if (d()->platform->name == $platform) {
      drush_log('Replacing the existing site on the same platform');
      // deploying over existing site.
      drush_set_option('deploy_replace_site', TRUE);
    }
  }
}

/**
 * Remove the extracted site directory
 * Restore the vhost conf per the original platform
 *
 * Implementation of drush_hook_pre_COMMAND_rollback().
 */
function drush_provision_drupal_pre_provision_migrate_rollback($platform, $new_name = NULL) {
  // Backdrop migrate is not implemented in Phase 1; guard so this D-lineage
  // migrate never runs against a Backdrop site (it would clobber settings.php
  // with a legacy Drupal file). The Backdrop migrate fork lands in Phase 2.
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  // Set site_offline to false and regenerate the config.
  drush_log(dt("Bringing site out of maintenance"));
  d()->site_enabled = true;
  _provision_drupal_create_settings_file();
  provision_drupal_push_site();

  $success =  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused migration site package')
    ->fail('Could not remove unused migration site package');
  // Belt: a crashed migrate must not leave the internal-backup flag behind -
  // a leftover one makes every later backup on the account skip the MyQuick
  // tmp_expim rotation and can leak a dump into a files-only archive.
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (is_file($internal_backup_flag)) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in rollback')
      ->fail('Could not remove Internal-DB-Backup Flag-File in rollback');
  }
  d()->service('http')->create_config('site');
  d()->service('http')->parse_configs();
}

/**
 * Switch the migrate directories around now that we have the new db installed
 *
 * Implementation of drush_hook_COMMAND().
 */
function drush_provision_drupal_provision_migrate($platform, $new_name = NULL) {
  // Backdrop migrate is not implemented in Phase 1; guard so this D-lineage
  // migrate never runs against a Backdrop site (it would clobber settings.php
  // with a legacy Drupal file). The Backdrop migrate fork lands in Phase 2.
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }

  $target = drush_get_option('target_name');
  $options = array();
  $options = d()->options;
  $options['uri'] = d()->uri;

  $aegir_root = d('@server_master')->aegir_root;
  $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

  if (!is_file($enable_fast_track)) {
    // If the site is migrated between platforms and not just renamed,
    // we should update the info collected about source and target platform first.
    // Note that we have to exclude Hostmaster platform from this extra verify.
    if (!provision_is_hostmaster_site() && drush_get_option('provision_verify_platforms_before_migrate', TRUE)) {
      if (!is_null(d($platform)->name) && (d($platform)->name != d()->platform->name)) {
        provision_backend_invoke('@hostmaster', 'hosting-task', array(d()->platform->name, 'verify'), array('force' => TRUE));
        sleep(5); // A small trick to avoid high load and race conditions.
        provision_backend_invoke('@hostmaster', 'hosting-task', array(d($platform)->name, 'verify'), array('force' => TRUE));
        sleep(5); // A small trick to avoid high load and race conditions.
      }
    }
  }

  if (!is_null($new_name) && ($new_name != d()->name)) {
    // Auto-disable Encryption on rename. The SSL key/cert is bound to the old
    // machine-name and is invalid for the new name; it must be re-created only
    // after the operator re-enables encryption for the new domain. The
    // front-end (hosting_migrate) persists this as the operator-visible state.
    $options['ssl_enabled'] = 0;
    unset($options['ssl_key']);
    drush_log(dt('[migrate] Encryption disabled for renamed site @name before final verify', array('@name' => $new_name)), 'info');

    $options['uri'] = ltrim($new_name, '@');
    $hash_name = drush_get_option('#name') ? '#name' : 'name';
    $options[$hash_name] = $new_name;
    $new_aliases = array();
    $old_aliases = (is_array($options['aliases'])) ? $options['aliases'] : FALSE;
    $pre_old_uri = ltrim(d()->uri, '@');
    $pre_new_uri = ltrim($new_name, '@');
    $raw_old_uri = preg_replace('/^www\./', '', $pre_old_uri);
    $raw_new_uri = preg_replace('/^www\./', '', $pre_new_uri);
    $www_old_uri = "www." . $raw_old_uri;
    $www_new_uri = "www." . $raw_new_uri;
    if (is_array($old_aliases) && !empty($old_aliases)) {
      foreach ($old_aliases as $alias) {
        drush_log(dt("LOG/RENAME old_aliases is @this", array('@this' => $alias)), 'info');
        // Rewrite only the alias which is exactly the old name's counterpart
        // variant; an unanchored str_replace() would mangle any custom alias
        // merely containing the old domain as a substring, e.g. on the
        // www.example.com => example.com rename shop.example.com would become
        // shop.www.example.com and my-example.com.pl => my-www.example.com.pl.
        if (!preg_match("/^www\./", $pre_old_uri) && preg_match("/^www\./", $pre_new_uri)) {
          $new_aliases[] = ($alias === $www_old_uri) ? $raw_new_uri : $alias;
        }
        elseif (preg_match("/^www\./", $pre_old_uri) && !preg_match("/^www\./", $pre_new_uri)) {
          $new_aliases[] = ($alias === $raw_old_uri) ? $www_new_uri : $alias;
        }
        elseif (!preg_match("/^www\./", $pre_old_uri) && !preg_match("/^www\./", $pre_new_uri)) {
          $new_aliases[] = ($alias === $www_old_uri) ? $www_new_uri : $alias;
        }
        elseif (preg_match("/^www\./", $pre_old_uri) && preg_match("/^www\./", $pre_new_uri)) {
          $new_aliases[] = ($alias === $raw_old_uri) ? $raw_new_uri : $alias;
        }
      }
      $unique_aliases = array_unique($new_aliases); // Make sure there are no duplicates.
      $options['aliases'] = array(); // Reset original aliases array.
      $options['aliases'] = $unique_aliases; // Use rewritten aliases array.
      if (!is_null(d($platform)->name) && (d($platform)->name == d()->platform->name)) {
        drush_set_option('rename_only', TRUE);
        foreach ($new_aliases as $alias) {
          drush_log(dt("LOG/RENAME new_aliases is @this", array('@this' => $alias)), 'info');
        }
        foreach ($unique_aliases as $alias) {
          drush_log(dt("LOG/RENAME unique_aliases is @this", array('@this' => $alias)), 'info');
        }
      }
      drush_set_option('aliases', $unique_aliases);
    }
    else {
      $detected_aliases = provision_drupal_find_aliases();
      foreach ($detected_aliases as $alias) {
        drush_log(dt("LOG/RENAME detected_aliases is @this", array('@this' => $alias)), 'info');
      }
      drush_set_option('aliases', $detected_aliases);
    }
    // A same-platform rename onto the site's own current alias (the one-step
    // www/non-www swap) leaves sites.php mapping the NEW uri at the OLD site
    // directory until the final verify rewrites it. The provision-deploy
    // sub-process bootstraps the new uri through that stale mapping
    // (conf_path), loads the OLD site's drushrc.php into the 'site' scope,
    // imports the dump into the OLD database and writes the new settings.php
    // against it — the database drush_db_post_provision_migrate() then drops,
    // leaving the renamed site with no database at all. Drop that one record
    // before the deploy so the new uri resolves to its own site directory.
    // Platform moves are immune: the deploy bootstraps under the target
    // platform root, whose sites.php never mapped the source's aliases.
    if (!is_null(d($platform)->name) && (d($platform)->name == d()->platform->name)) {
      if (drush_drupal_major_version() >= 7) {
        $store = new Provision_Config_Drupal_Alias_Store(d()->name);
        $merged = $store->merged_records();
        if (isset($merged[$pre_new_uri]) && $merged[$pre_new_uri] == $pre_old_uri) {
          // maintain() before the drop: it re-keys this site's records from
          // the live alias set, so subdir aliases keep the slash-form keys
          // the template's slash->dot conversion expects — hand-unsetting
          // alone leaves them dot-keyed and the template nulls them out.
          $store->maintain();
          unset($store->records[$pre_new_uri]);
          unset($store->loaded_records[$pre_new_uri]);
          $store->write();
          d()->service('http')->sync($store->filename());
          drush_log(dt('[migrate] Dropped stale sites.php record @alias => @site before deploy', array('@alias' => $pre_new_uri, '@site' => $pre_old_uri)), 'info');
        }
      }
      else {
        // Pre-D7 alias symlink: sites/<alias> -> <uri>.
        $stale_link = d()->root . '/sites/' . $pre_new_uri;
        if (is_link($stale_link) && (readlink($stale_link) == $pre_old_uri)) {
          provision_file()->unlink($stale_link)
            ->succeed('Removed stale alias symlink @path before deploy')
            ->fail('Could not remove stale alias symlink @path');
          d()->service('http')->sync($stale_link);
        }
      }
    }
    // Warning: do not try to re-verify the original site here
    // even with backend-only verify, because it would create
    // (and never delete) duplicate vhost with old domain
    // and all aliases included - see issue #1067702.
  }
  else {

    $aegir_root = d('@server_master')->aegir_root;
    $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

    if (!is_file($enable_fast_track)) {

      // We have to exclude Hostmaster site from any extra verify steps.
      if (!provision_is_hostmaster_site()) {
        // We should update also the info collected about the site before running migrate task.
        // Doing this is safe only when the site is migrated with the same name - see issue #1067702.
        $local_uri_verify = '@' . d()->uri;
        // XXX: right now we just pause SSL configuration when migrating,
        // even if the site name doesn't change, because of possible
        // edge case situations where the task had errors preventing it
        // from managing vhost and respective SSL keys updates properly.
        if ($options['ssl_enabled'] && $options['ssl_key']) {
          $encryption_was_enabled = "YES";
          $temp_options_ssl_enabled = $options['ssl_enabled'];
          $temp_options_ssl_key = $options['ssl_key'];
          $temp_uri_verify = $local_uri_verify;
          drush_log(dt("LOG/TO DISABLE ssl_enabled WAS @this", array('@this' => $options['ssl_enabled'])), 'info');
          drush_log(dt("LOG/TO DISABLE ssl_key WAS @this", array('@this' => $options['ssl_key'])), 'info');
          drush_log(dt("LOG/TO DISABLE temp_uri_verify WAS @this", array('@this' => $temp_uri_verify)), 'info');
          drush_set_option('encryption_was_enabled', TRUE);
          drush_set_option('temp_options_ssl_enabled', $options['ssl_enabled']);
          drush_set_option('temp_options_ssl_key', $options['ssl_key']);
          drush_set_option('temp_uri_verify', $temp_uri_verify);
          $options['ssl_enabled'] = 0;
          unset($options['ssl_key']);
          drush_log(dt("LOG/DISABLED ssl_enabled WAS @this", array('@this' => $options['ssl_enabled'])), 'info');
          drush_log(dt("LOG/DISABLED ssl_key WAS @this", array('@this' => $options['ssl_key'])), 'info');
          drush_log(dt("LOG/DISABLED temp_uri_verify WAS @this", array('@this' => $temp_uri_verify)), 'info');
          drush_invoke_process('@none', 'provision-save', array($temp_uri_verify), $options);
          sleep(5); // A small trick to avoid high load and race conditions.
        }
        provision_backend_invoke('@hostmaster', 'hosting-task', array($local_uri_verify, 'verify'), array('force' => TRUE));
        sleep(5); // A small trick to avoid high load and race conditions.
      }
    }
  }
  $options['platform'] = $platform;
  $options['root'] = d($platform)->root;

  if ($profile = drush_get_option('profile', FALSE)) {
    $options['profile'] = $profile;
  }

  if ($db_server = drush_get_option('new_db_server', FALSE)) {
    $options['db_server'] = $db_server;
  }

  drush_set_option('new_site_path', "{$options['root']}/sites/{$options['uri']}");

  drush_invoke_process('@none', 'provision-save', array($target), $options);

  // Build an options array to pass to the deploy task. This allows injection
  // of data into the deploy task. See: https://www.drupal.org/node/2471805
  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'migrate');

  provision_backend_invoke($target, 'provision-deploy', array(drush_get_option('backup_file')), $deploy_options);
  if (!drush_get_error()) {
    drush_log(dt("Bringing site out of maintenance"));
    d()->site_enabled = TRUE;
    drush_set_option('provision_save_config', FALSE);
    provision_backend_invoke($target, 'provision-verify');

    // Native files symlinking for the migrated/renamed site: give it its OWN
    // copy of files/private in its own store and re-symlink -- mirroring the
    // clone path (clone.provision.inc). A migrate deploys from a backup that
    // preserves the source symlink, so the target lands pointing its files/private
    // at the SOURCE (old-name) store; --force-unshare breaks that inherited
    // cross-name link, copies the data into the target's own static/files/<uri>
    // store, and repoints the in-site symlinks (a pre-existing store at the target
    // name is moved aside first). Without this the renamed site keeps plain dirs on
    // the platform partition and its store link is lost. The old-name store then
    // becomes a normal reported orphan (operator prunes; never auto-deleted).
    // Delegated to the root autosymlink tool via its sudo wrapper; the verify above
    // wrote the vhost so the account auto-resolves. Warn-not-fail.
    _provision_drupal_native_symlink($options['uri'], TRUE);

    $aegir_root = d('@server_master')->aegir_root;
    $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

    if (!is_file($enable_fast_track)) {
      sleep(5); // A small trick to avoid high load and race conditions.
      // Re-enable encryption again if needed and only when platform has changed
      $encryption_was_enabled = drush_get_option('encryption_was_enabled');
      if ($encryption_was_enabled) {
        drush_log(dt("LOG/ENABLE encryption_was_enabled WAS @this", array('@this' => $encryption_was_enabled)), 'info');
        $temp_options_ssl_enabled = drush_get_option('temp_options_ssl_enabled');
        $temp_options_ssl_key = drush_get_option('temp_options_ssl_key');
        $temp_uri_verify = drush_get_option('temp_uri_verify');
        if ($temp_options_ssl_enabled && $temp_options_ssl_key) {
          drush_log(dt("LOG/ENABLE temp_options_ssl_enabled WAS @this", array('@this' => $temp_options_ssl_enabled)), 'info');
          drush_log(dt("LOG/ENABLE temp_options_ssl_key WAS @this", array('@this' => $temp_options_ssl_key)), 'info');
          drush_log(dt("LOG/ENABLE temp_uri_verify WAS @this", array('@this' => $temp_uri_verify)), 'info');
          $options['ssl_enabled'] = $temp_options_ssl_enabled;
          $options['ssl_key'] = $temp_options_ssl_key;
          drush_invoke_process('@none', 'provision-save', array($temp_uri_verify), $options);
          sleep(5); // A small trick to avoid high load and race conditions.
          provision_backend_invoke('@hostmaster', 'hosting-task', array($temp_uri_verify, 'verify'), array('force' => TRUE));
          sleep(5); // A small trick to avoid high load and race conditions.
        }
      }
    }
  }
}

/**
 * Something went wrong
 *
 * Implementation of drush_hook_COMMAND_rollback().
 */
function drush_provision_drupal_provision_migrate_rollback($platform, $new_name = NULL) {
  // Backdrop migrate is not implemented in Phase 1; guard so this D-lineage
  // migrate never runs against a Backdrop site (it would clobber settings.php
  // with a legacy Drupal file). The Backdrop migrate fork lands in Phase 2.
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  // A same-platform rename may have dropped the rename target's sites.php
  // record (pre-D7: its alias symlink) before invoking the deploy; a failed
  // deploy must not leave the source's alias routing incomplete. Rebuild the
  // full mapping from the source's still-unchanged alias set.
  if (!is_null($new_name) && ($new_name != d()->name)
    && !is_null(d($platform)->name) && (d($platform)->name == d()->platform->name)) {
    _provision_drupal_maintain_aliases();
  }
  drush_invoke_process('@none', 'provision-save', array(d()->name), array('platform' => drush_get_option('old_platform'), 'root' => NULL, 'uri' => NULL));
}

/**
 * Delete the old site directory and recreate the settings file
 *
 * Implementation of drush_hook_post_COMMAND().
 */
function drush_provision_drupal_post_provision_migrate($platform, $new_name = NULL) {
  // Backdrop migrate is not implemented in Phase 1; guard so this D-lineage
  // migrate never runs against a Backdrop site (it would clobber settings.php
  // with a legacy Drupal file). The Backdrop migrate fork lands in Phase 2.
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  drush_set_option('installed', FALSE);

  if (!drush_get_option('deploy_replace_site', FALSE)) {
    // we remove the aliases even if redirection is enabled as a precaution
    // if redirection is enabled, keep silent about errors
    _provision_drupal_delete_aliases();
    _provision_recursive_delete(d()->site_path);
    d()->service('http')->sync(d()->site_path);
  }

  if (!is_null($new_name) && ($new_name != d()->name)) {
    // Use correct context for cleanup operations.
    $source_name = drush_get_option('source_name');
    drush_log(dt("LOG/RENAME source_name is @this", array('@this' => $source_name)), 'info');
    // Remove the existing drush alias.
    $config = new Provision_Config_Drushrc_Alias(d()->name);
    $config->unlink();
    // Delete the old site directory.
    _provision_recursive_delete(d($source_name)->site_path);
    d()->service('http')->sync(d($source_name)->site_path);
    // Remove old site name aliases.
    _provision_drupal_delete_aliases();
    // Remove the symlink in the clients directory.
    _provision_client_delete_symlink();
  }

  $rename_only = drush_get_option('rename_only');
  if ($rename_only) {
    // We run this extra tricky backend verify to avoid issue #1004526.
    if (!provision_is_hostmaster_site()) {
      drush_log(dt("LOG/RENAME new_uri is @this", array('@this' => $new_name)), 'info');
      $deploy_replace_site = drush_get_option('deploy_replace_site');
      drush_log(dt("LOG/RENAME deploy_replace_site is @this", array('@this' => $deploy_replace_site)), 'info');
      $final_aliases = drush_get_option('aliases');
      foreach ($final_aliases as $alias) {
        drush_log(dt("LOG/RENAME final_aliases is @this", array('@this' => $alias)), 'info');
      }
      provision_reload_config('site', drush_get_option('new_site_path', d($new_name)->site_path) . '/drushrc.php');
      d($new_name)->aliases = drush_set_option('aliases', $final_aliases);
      d($new_name)->write_alias();
      drush_set_option('aliases', drush_get_option('aliases'),  'site');
      drush_set_option('provision_save_config', false);
      provision_backend_invoke($new_name, 'provision-verify');
    }
  }
  else {
    // Load the config file of the newly migrated site and return it to hosting.
    provision_reload_config('site', drush_get_option('new_site_path', d()->site_path) . '/drushrc.php');
  }

  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  provision_file()->unlink($internal_backup_flag)
    ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_drupal_post_provision_migrate')
    ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_drupal_post_provision_migrate');
}
