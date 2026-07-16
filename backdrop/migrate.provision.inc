<?php

/**
 * Migrate command — Backdrop layer (guard + Backdrop-to-Backdrop support).
 *
 * Backdrop-to-Backdrop migrate is SUPPORTED here (D-028): same-domain moves
 * onto another (typically newer) Backdrop platform, and renames — mirroring
 * the D-lineage orchestration in platform/migrate.provision.inc with the
 * Backdrop settings writer and alias helpers. The http/db service hooks
 * (source vhost delete in pre, source database drop in post) are
 * CMS-agnostic and correct once this layer performs the real work between
 * them; they were only dangerous while the platform hooks no-opped.
 *
 * Cross-lineage stays REFUSED, fail-closed in validate: a Drupal source
 * with a Backdrop target (or the reverse) never runs — the Drupal 7 ->
 * Backdrop path is the clone-preserving provision-backdrop-upgrade, which
 * never dispatches provision-migrate.
 *
 * Hook naming: the stock command provision-migrate does not start with this
 * commandfile's prefix, so nothing collapses — every hook here carries the
 * doubled drush_provision_backdrop_(pre_|post_)provision_migrate form, and
 * runs alongside the D-lineage hooks (each layer early-returns on the other
 * layer's sites; the D-014 root-keyed pattern).
 */

/**
 * Implements drush_hook_COMMAND_validate() for provision-migrate.
 *
 * Fail cross-lineage closed before any service hook can act; bootstrap the
 * site for the Backdrop-to-Backdrop case exactly as the D-layer validate
 * does for Drupal. Unlike the other files in this layer it must NOT
 * early-return on a non-Backdrop source: the target check has to run for
 * Drupal sources too.
 *
 * @param string $platform
 *   The target platform context name — the command's first positional
 *   argument (see drush_provision_drupal_provision_migrate_validate()).
 */
function drush_provision_backdrop_provision_migrate_validate($platform = NULL) {
  $source_is_backdrop = provision_platform_is_backdrop(d());
  $target_is_backdrop = FALSE;
  if (is_string($platform) && $platform !== '') {
    // d() on an unknown name yields a context without a usable root, for
    // which provision_platform_is_backdrop() returns FALSE — migrate itself
    // rejects unknown targets later.
    $target_is_backdrop = provision_platform_is_backdrop(d($platform));
  }

  if (!$source_is_backdrop && !$target_is_backdrop) {
    // Pure Drupal migrate — the D-lineage hooks own it.
    return;
  }

  if ($source_is_backdrop != $target_is_backdrop) {
    return drush_set_error('PROVISION_BACKDROP_MIGRATE_UNSUPPORTED',
      dt('provision-migrate never crosses the Drupal/Backdrop lineage (source is !src; target is !tgt). For Drupal 7 -> Backdrop upgrades use provision-backdrop-upgrade.',
      array(
        '!src' => $source_is_backdrop ? 'Backdrop' : 'not Backdrop',
        '!tgt' => $target_is_backdrop ? 'Backdrop' : 'not Backdrop',
      )));
  }

  // Reject a no-op self-migrate: same platform AND no rename. Such a task
  // only risks the destructive teardown (the shared db post-hook drops the
  // live database of an in-place re-deploy) while moving nothing. The
  // frontend never generates this — it needs a different platform or a new
  // uri — but a direct `drush @site provision-migrate @its_own_platform`
  // could, so fail it closed. A rename on the same platform stays allowed:
  // the new name arrives as the command's second positional argument
  // (drush_get_arguments()[0] is the command, [1] the target platform, [2]
  // the optional new name), so its presence distinguishes a rename from an
  // in-place no-op.
  if (is_string($platform) && $platform !== ''
    && d($platform)->name == d()->platform->name) {
    $args = drush_get_arguments();
    $new_name = isset($args[2]) ? $args[2] : '';
    if ($new_name === '' || $new_name === d()->name) {
      return drush_set_error('PROVISION_BACKDROP_MIGRATE_NOOP',
        dt('Migrating a site to its own platform with no new name is a no-op that would drop its live database; nothing to do.'));
    }
  }

  // Backdrop -> Backdrop: mirror the D-layer validate.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_SITE);
}

/**
 * Implements drush_hook_pre_COMMAND() for provision-migrate.
 *
 * Backup before any change, and record the rename/replace intent — the
 * Backdrop mirror of drush_provision_drupal_pre_provision_migrate().
 */
function drush_provision_backdrop_pre_provision_migrate($platform, $new_name = NULL) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  // Mark the context disabled and re-render settings, mirroring the D-layer
  // pre-hook. NOTE: unlike Drupal's $conf['site_offline'], Backdrop keeps
  // maintenance mode in {state}, which this settings render does NOT set, so
  // the source is NOT made read-only for the backup window — same live-backup
  // posture as the Backdrop clone/backup tasks. The window is the sqldump
  // duration; a write landing mid-dump is not in the moved copy and is lost
  // when the source is retired. Acceptable for an operator-initiated move of
  // their own site; a future hardening could set Backdrop maintenance state
  // on the source before the backup.
  drush_log(dt("Putting site under maintenance"));
  d()->site_enabled = FALSE;
  _provision_backdrop_create_settings_file();
  drush_log(dt('Invoking drush provision-backup in drush_provision_backdrop_pre_provision_migrate'));
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  $internal_backup_flag_blank = "Internal-DB-Backup \n";
  $local_description = 'Adding Internal-DB-Backup Flag-File drush_provision_backdrop_pre_provision_migrate';
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
 * Implements drush_hook_pre_COMMAND_rollback() for provision-migrate.
 *
 * Bring the source back online and restore its vhost — the Backdrop mirror
 * of drush_provision_drupal_pre_provision_migrate_rollback().
 */
function drush_provision_backdrop_pre_provision_migrate_rollback($platform, $new_name = NULL) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  drush_log(dt("Bringing site out of maintenance"));
  d()->site_enabled = TRUE;
  _provision_backdrop_create_settings_file();
  provision_drupal_push_site();

  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused migration site package')
    ->fail('Could not remove unused migration site package');
  d()->service('http')->create_config('site');
  d()->service('http')->parse_configs();
}

/**
 * Implements drush_hook_COMMAND() for provision-migrate.
 *
 * Orchestrate the move: re-target the alias, deploy the backup onto the
 * target platform, verify, and give the site its own native files store —
 * the Backdrop mirror of drush_provision_drupal_provision_migrate(). The
 * deploy dispatches to the Backdrop deploy fork automatically (fresh copy
 * database, scalar $database settings, pinned config directories, module
 * schema comparison against the target platform, backdrop-updatedb tail).
 */
function drush_provision_backdrop_provision_migrate($platform, $new_name = NULL) {
  if (!provision_platform_is_backdrop(d())) {
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
    // we should update the info collected about source and target platform
    // first. The hostmaster site can never be Backdrop, so no exclusion is
    // needed here.
    if (drush_get_option('provision_verify_platforms_before_migrate', TRUE)) {
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
    $old_aliases = (isset($options['aliases']) && is_array($options['aliases'])) ? $options['aliases'] : FALSE;
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
      }
      drush_set_option('aliases', $unique_aliases);
    }
    else {
      // Backdrop host aliases live in the saved context options and land in
      // sites.php via the alias store; there are no D6-style alias symlinks
      // to scan, so an empty source alias list stays empty.
      drush_set_option('aliases', array());
    }
    // Warning: do not try to re-verify the original site here
    // even with backend-only verify, because it would create
    // (and never delete) duplicate vhost with old domain
    // and all aliases included - see issue #1067702.
  }
  else {

    if (!is_file($enable_fast_track)) {
      // We should update also the info collected about the site before
      // running the migrate task. Doing this is safe only when the site is
      // migrated with the same name - see issue #1067702.
      $local_uri_verify = '@' . d()->uri;
      // XXX: right now we just pause SSL configuration when migrating,
      // even if the site name doesn't change, because of possible
      // edge case situations where the task had errors preventing it
      // from managing vhost and respective SSL keys updates properly.
      if (!empty($options['ssl_enabled']) && !empty($options['ssl_key'])) {
        $temp_uri_verify = $local_uri_verify;
        drush_set_option('encryption_was_enabled', TRUE);
        drush_set_option('temp_options_ssl_enabled', $options['ssl_enabled']);
        drush_set_option('temp_options_ssl_key', $options['ssl_key']);
        drush_set_option('temp_uri_verify', $temp_uri_verify);
        $options['ssl_enabled'] = 0;
        unset($options['ssl_key']);
        drush_log(dt('[migrate] Encryption paused for @name during the move', array('@name' => $temp_uri_verify)), 'info');
        drush_invoke_process('@none', 'provision-save', array($temp_uri_verify), $options);
        sleep(5); // A small trick to avoid high load and race conditions.
      }
      provision_backend_invoke('@hostmaster', 'hosting-task', array($local_uri_verify, 'verify'), array('force' => TRUE));
      sleep(5); // A small trick to avoid high load and race conditions.
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

  // Trim the source package map from the provision-save options for the
  // re-targeted alias; the deploy's schema comparison reads packages from the
  // extracted backup's site drushrc, and the final verify rebuilds the map
  // from the target platform, so a stale source map is neither needed here
  // nor persisted on the alias.
  unset($options['packages']);

  // Strip the SOURCE database credentials so the deploy mints a fresh copy
  // database and the rendered settings match it. Carried through from
  // d()->options they would populate the target's config data and be read by
  // the Backdrop settings writer, while the deploy's generate_site_credentials
  // writes the fresh name only into the 'site' option sub-context — the copy
  // then boots against the (about-to-be-dropped) source database name. The
  // source creds are already captured on disk in the old-platform drushrc for
  // the post-hook retire; they must not ride the new alias.
  unset($options['db_name']);
  unset($options['db_user']);
  unset($options['db_passwd']);
  unset($options['db_type']);
  unset($options['db_host']);
  unset($options['db_port']);

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

    // Native files symlinking, mirroring the D-lineage migrate: the deploy
    // extracts a backup whose files/private are symlinks into the SOURCE
    // name's native store; --force-unshare gives the target its own
    // static/files/<uri> store and repoints the in-site symlinks. For a
    // same-domain platform move the store name is unchanged and the call is
    // an idempotent no-op-or-repair; for a rename it breaks the inherited
    // cross-name link (the old-name store becomes a reported orphan).
    // Warn-not-fail.
    _provision_drupal_native_symlink($options['uri'], TRUE);

    if (!is_file($enable_fast_track)) {
      sleep(5); // A small trick to avoid high load and race conditions.
      // Re-enable encryption again if it was paused for a same-name move.
      if (drush_get_option('encryption_was_enabled')) {
        $temp_options_ssl_enabled = drush_get_option('temp_options_ssl_enabled');
        $temp_options_ssl_key = drush_get_option('temp_options_ssl_key');
        $temp_uri_verify = drush_get_option('temp_uri_verify');
        if ($temp_options_ssl_enabled && $temp_options_ssl_key) {
          $options['ssl_enabled'] = $temp_options_ssl_enabled;
          $options['ssl_key'] = $temp_options_ssl_key;
          drush_log(dt('[migrate] Encryption re-enabled for @name after the move', array('@name' => $temp_uri_verify)), 'info');
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
 * Implements drush_hook_COMMAND_rollback() for provision-migrate.
 *
 * Re-target the alias back to the old platform — the Backdrop mirror of
 * drush_provision_drupal_provision_migrate_rollback().
 */
function drush_provision_backdrop_provision_migrate_rollback($platform) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  drush_invoke_process('@none', 'provision-save', array(d()->name), array('platform' => drush_get_option('old_platform'), 'root' => NULL, 'uri' => NULL));
}

/**
 * Implements drush_hook_post_COMMAND() for provision-migrate.
 *
 * Retire the old copy: delete the source site directory and its host
 * aliases (plus the old-name artifacts on a rename) — the Backdrop mirror
 * of drush_provision_drupal_post_provision_migrate(). The db service hook
 * drops the source database in this same phase.
 */
function drush_provision_backdrop_post_provision_migrate($platform, $new_name = NULL) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  drush_set_option('installed', FALSE);

  if (!drush_get_option('deploy_replace_site', FALSE)) {
    // we remove the aliases even if redirection is enabled as a precaution
    // if redirection is enabled, keep silent about errors
    _provision_backdrop_delete_aliases();
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
    // Remove old site name aliases from sites.php.
    _provision_backdrop_delete_aliases();
    // Remove the symlink in the clients directory.
    _provision_client_delete_symlink();
  }

  if (drush_get_option('rename_only')) {
    // We run this extra tricky backend verify to avoid issue #1004526.
    drush_log(dt("LOG/RENAME new_uri is @this", array('@this' => $new_name)), 'info');
    provision_reload_config('site', drush_get_option('new_site_path', d($new_name)->site_path) . '/drushrc.php');
    $final_aliases = drush_get_option('aliases');
    d($new_name)->aliases = drush_set_option('aliases', $final_aliases);
    d($new_name)->write_alias();
    drush_set_option('aliases', drush_get_option('aliases'), 'site');
    drush_set_option('provision_save_config', FALSE);
    provision_backend_invoke($new_name, 'provision-verify');
  }
  else {
    // Load the config file of the newly migrated site and return it to hosting.
    provision_reload_config('site', drush_get_option('new_site_path', d()->site_path) . '/drushrc.php');
  }

  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  provision_file()->unlink($internal_backup_flag)
    ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_backdrop_post_provision_migrate')
    ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_backdrop_post_provision_migrate');
}
