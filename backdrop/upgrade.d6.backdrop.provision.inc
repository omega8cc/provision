<?php

/**
 * provision-backdrop-d6-upgrade — D6 -> D7 upgrade (hop 1 of the D6 ->
 * Backdrop chain; the produced D7 intermediate is then upgraded with the
 * existing provision-backdrop-upgrade, unchanged).
 *
 * Clone-preserving shape, mirroring provision-backdrop-upgrade: backup the
 * live Pressflow/Drupal 6 source, save a NEW site alias at a NEW uri on the
 * Drupal 7 target platform, deploy the backup there with the transient
 * d6_d7_upgrade deploy option (the shared Drupal deploy quarantines the
 * copied D6 site-local modules, skips the pre-updatedb registry rebuild, and
 * its own updatedb performs the staged D6 -> D7 core conversion), then give
 * the copy its own files store, stage the pinned D7 contrib kit from the BOA
 * mirror cache, enable only what the source site used (plus content_migrate
 * and core file/image), run the kit schema updates, migrate the CCK field
 * data with a HARD-gated content_migrate script (any field left without its
 * provider module aborts the hop — unmigrated CCK data strands silently),
 * and verify. The source site, database and vhost are never in the write
 * path; rollback at any step = discard the copy.
 *
 * Hook naming: the command name starts with this commandfile's name, so Drush
 * COLLAPSES the prefix for the validate/command/rollback hooks
 * (drush_provision_backdrop_d6_upgrade*) while the pre variation keeps the
 * doubled form (drush_provision_backdrop_pre_provision_backdrop_d6_upgrade)
 * — see includes/command.inc:405 in the Drush 8 fork.
 *
 * File name: Drush resolves command includes by the reversed-dotted command
 * name, so provision-backdrop-d6-upgrade => upgrade.d6.backdrop.provision.inc.
 */

/**
 * The on-box cache directory the pinned D7 contrib kit is staged into.
 *
 * Octopus stages the mirror tarball here (kit-v-*.txt stamped, idempotent);
 * provision never fetches over the network — it only copies module
 * directories from this cache into the copy's site dir.
 */
define('PROVISION_BACKDROP_D6_KIT_DIR', '/data/all/000/d6d7kit');

/**
 * Implements drush_hook_COMMAND_validate() (collapsed name).
 *
 * Every precondition, fail-closed, before anything is created.
 */
function drush_provision_backdrop_d6_upgrade_validate($new_name = NULL, $platform = NULL) {
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_BAD_SOURCE',
      dt('provision-backdrop-d6-upgrade must run against a site context.'));
  }
  // Backup mode ctrl file cleanup, as the other lifecycle validates do.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }

  // The source must be Drupal 6 lineage, never Backdrop, never Drupal 7+.
  if (provision_platform_is_backdrop(d())) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_BAD_SOURCE',
      dt('The source site is Backdrop; provision-backdrop-d6-upgrade only upgrades Drupal 6 sites.'));
  }
  if (drush_drupal_major_version() != 6) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_BAD_SOURCE',
      dt('The source platform is Drupal @v; only Drupal 6 sites take this hop. Drupal 7 sites upgrade with provision-backdrop-upgrade directly.',
      array('@v' => drush_drupal_major_version())));
  }

  // The D6 -> D7 update hooks, the pinned kit and content_migrate all run
  // under the instance CLI; PHP 7.4 is the proven (and only supported)
  // interpreter for this legacy chain. The backend wrapper runs under the
  // instance CLI, so PHP_VERSION here IS the instance CLI version.
  if (version_compare(PHP_VERSION, '7.4.0', '<') || version_compare(PHP_VERSION, '7.5.0', '>=')) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_PHP',
      dt('The instance CLI runs PHP @v; the D6 -> D7 hop requires a PHP 7.4-pinned instance (set cli.info/fpm.info to 7.4 — note D8+ platforms on the same instance would break).',
      array('@v' => PHP_VERSION)));
  }
  if (!is_file('/opt/php74/bin/php')) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_PHP',
      dt('/opt/php74/bin/php is missing; the D6 -> D7 hop requires PHP 7.4 on the box.'));
  }

  // Target platform must be a positive Drupal 7 root: D7 keeps
  // includes/bootstrap.inc (D8+ moved it under core/, Backdrop too) and only
  // D7 declares core compatibility 7.x there (D6 has no such constant).
  if (!is_string($platform) || $platform === '') {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_BAD_TARGET',
      dt('No target platform alias given.'));
  }
  $target_root = d($platform)->root;
  if (empty($target_root) || provision_platform_is_backdrop(d($platform))
    || !_provision_backdrop_d6_upgrade_root_is_drupal7($target_root)) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_BAD_TARGET',
      dt('The target platform !platform is not a Drupal 7 platform.', array('!platform' => $platform)));
  }

  // The new uri must differ from the source and be free on the target.
  if (!is_string($new_name) || $new_name === '') {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_BAD_TARGET',
      dt('No new site alias given.'));
  }
  $new_uri = ltrim($new_name, '@');
  if ($new_uri === d()->uri) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_BAD_TARGET',
      dt('The new uri must differ from the source uri (the source keeps serving until you retire it).'));
  }
  if (is_dir($target_root . '/sites/' . $new_uri)) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_BAD_TARGET',
      dt('sites/@uri already exists on the target platform.', array('@uri' => $new_uri)));
  }

  // The recorded profile must exist on the D7 target. A custom D6 profile
  // must be present on the target under the same name (pass --profile).
  $profile = _provision_backdrop_d6_upgrade_profile();
  if (!is_dir($target_root . '/profiles/' . $profile)) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_BAD_TARGET',
      dt('The @profile profile does not exist on the target platform.', array('@profile' => $profile)));
  }

  // The deploy's own updatedb IS the D6 -> D7 core conversion; a DisAutoUpDb
  // control file would skip it, leaving a still-D6 copy that wedges the rest
  // of the deploy into a confusing rollback. Refuse up front instead.
  $aegir_root = d('@server_master')->aegir_root;
  $dis_auto_up_db_global = $aegir_root . '/static/control/DisAutoUpDb.info';
  $dis_auto_up_db_site = $aegir_root . '/static/control/' . $new_uri . '_DisAutoUpDb.info';
  if (is_file($dis_auto_up_db_global) || is_file($dis_auto_up_db_site)) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_BAD_TARGET',
      dt('A DisAutoUpDb control file is present (@global / @site); it would skip the updatedb this upgrade depends on. Remove it and retry.',
      array('@global' => $dis_auto_up_db_global, '@site' => $dis_auto_up_db_site)));
  }

  // The source schema must be Drupal 6 (a 6xxx system schema). Bootstrap-free
  // via the provision PDO with the source-db-qualified table name.
  $db_name = _provision_backdrop_site_db_name();
  if (empty($db_name) || !preg_match('/^[A-Za-z0-9_]+$/', $db_name)) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_NO_DB',
      dt('Could not resolve the source site database name.'));
  }
  $db = d()->service('db');
  $db->connect();
  $schema = 0;
  $result = $db->query("SELECT schema_version FROM `" . $db_name . "`.`system` WHERE name = '%s' AND type = '%s'", 'system', 'module');
  if ($result) {
    $row = $result->fetch(PDO::FETCH_NUM);
    if (isset($row[0])) {
      $schema = intval($row[0]);
    }
  }
  if ($schema < 6000 || $schema >= 7000) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_SCHEMA',
      dt('The source system schema is @schema; provision-backdrop-d6-upgrade expects a Drupal 6 site (6xxx). Update the site to the most recent Drupal 6 release first.',
      array('@schema' => $schema ? $schema : dt('unreadable'))));
  }
  drush_log(dt('Source system schema @schema is Drupal 6; proceeding.', array('@schema' => $schema)), 'ok');

  // The pinned D7 contrib kit must be staged on the box (Octopus fetches it
  // from the BOA mirror; provision never downloads).
  $kit_dir = PROVISION_BACKDROP_D6_KIT_DIR;
  if (!is_dir($kit_dir) || !is_file($kit_dir . '/cck/modules/content_migrate/content_migrate.module')) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_NO_KIT',
      dt('The pinned D6->D7 contrib kit is not staged at @dir (content_migrate missing). Run the BOA/Octopus update for this instance first (it stages the kit from the mirror when Backdrop support is enabled).',
      array('@dir' => $kit_dir)));
  }

  // Advisory kit-coverage report; the same map drives the data-driven enable
  // in the command phase. Unmapped modules are parked disabled by the D7
  // conversion (data kept) — a warning, never a gate. The CCK field-data
  // gate is content_migrate's, enforced hard in the command phase.
  _provision_backdrop_d6_upgrade_kit_report($db, $db_name);
}

/**
 * The profile to record for the new D7 site.
 *
 * The --profile option falls through to the SOURCE alias options (the
 * backend runs in the D6 site context, so the site's persisted profile
 * rides drush_get_option), and the stock D6 profile is literally named
 * 'default' — which no D7 platform ships, so the raw value would fail both
 * the validate is_dir check and, saved onto the alias, the later hop-2
 * validate. The D6 -> D7 core upgrade renames the DB install_profile
 * default -> standard; map the context the same way.
 */
function _provision_backdrop_d6_upgrade_profile() {
  $profile = drush_get_option('profile', 'standard');
  if (!is_string($profile) || $profile === '' || $profile === 'default') {
    $profile = 'standard';
  }
  return $profile;
}

/**
 * Positive, bootstrap-free "this root is Drupal 7" check.
 */
function _provision_backdrop_d6_upgrade_root_is_drupal7($root) {
  if (!is_string($root) || $root === '' || !is_dir($root)) {
    return FALSE;
  }
  $bootstrap = $root . '/includes/bootstrap.inc';
  if (!is_file($root . '/index.php') || !is_file($bootstrap)) {
    return FALSE;
  }
  $body = file_get_contents($bootstrap);
  if ($body === FALSE) {
    return FALSE;
  }
  return strpos($body, "define('DRUPAL_CORE_COMPATIBILITY', '7.x')") !== FALSE;
}

/**
 * Load the D6 -> D7 kit map (data, not code).
 *
 * @return
 *   The decoded map array, or array() when missing/unreadable.
 */
function _provision_backdrop_d6_upgrade_kit_map() {
  $map_file = dirname(__FILE__) . '/data/d6_d7_kit_map.json';
  if (is_readable($map_file)) {
    $decoded = json_decode(file_get_contents($map_file), TRUE);
    if (is_array($decoded) && isset($decoded['modules']) && is_array($decoded['modules'])) {
      return $decoded;
    }
  }
  drush_log(dt('Kit map @file missing or unreadable.', array('@file' => $map_file)), 'warning');
  return array();
}

/**
 * Enumerate the source site's enabled modules, bootstrap-free.
 *
 * Read from the LIVE source database (never the copy): after the copy's
 * D6 -> D7 updatedb, update_fix_compatibility() parks every contrib at
 * status 0, so a post-conversion status=1 read would under-report what the
 * source actually used.
 *
 * @return
 *   Array of module names, or FALSE when the read fails.
 */
function _provision_backdrop_d6_upgrade_source_modules($db, $db_name) {
  $result = $db->query("SELECT name FROM `" . $db_name . "`.`system` WHERE type = '%s' AND status = 1 ORDER BY name", 'module');
  if (!$result) {
    return FALSE;
  }
  $modules = array();
  while ($row = $result->fetch(PDO::FETCH_NUM)) {
    $modules[] = $row[0];
  }
  return $modules;
}

/**
 * Advisory kit-coverage report (validate helper).
 */
function _provision_backdrop_d6_upgrade_kit_report($db, $db_name) {
  $map = _provision_backdrop_d6_upgrade_kit_map();
  if (empty($map)) {
    drush_log(dt('Skipping the kit-coverage report (no map).'), 'warning');
    return;
  }
  $modules = _provision_backdrop_d6_upgrade_source_modules($db, $db_name);
  if ($modules === FALSE) {
    drush_log(dt('Could not enumerate enabled modules for the kit-coverage report.'), 'warning');
    return;
  }
  $covered = 0;
  $core = 0;
  $unmapped = array();
  foreach ($modules as $name) {
    if (!isset($map['modules'][$name])) {
      $unmapped[] = $name;
      continue;
    }
    $entry = $map['modules'][$name];
    if (!empty($entry['kit_projects'])) {
      $covered++;
    }
    else {
      $core++;
    }
  }
  foreach ($unmapped as $name) {
    drush_log(dt('D6-UPGRADE/KIT @name has no D6->D7 kit mapping; the conversion will park it disabled at its D6 schema (data tables are kept).', array('@name' => $name)), 'warning');
  }
  drush_log(dt('D6-UPGRADE/KIT coverage: @covered kit-mapped, @core core-mapped, @unmapped unmapped of @total enabled modules.',
    array('@covered' => $covered, '@core' => $core, '@unmapped' => count($unmapped), '@total' => count($modules))), 'ok');
}

/**
 * Implements drush_hook_pre_COMMAND() (doubled name — not collapsed).
 *
 * Take the source backup: live, never taken offline (clone semantics).
 */
function drush_provision_backdrop_pre_provision_backdrop_d6_upgrade($new_name = NULL, $platform = NULL) {
  drush_log(dt('Invoking drush provision-backup in drush_provision_backdrop_pre_provision_backdrop_d6_upgrade'));
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  $internal_backup_flag_blank = "Internal-DB-Backup \n";
  $local_description = 'Adding Internal-DB-Backup Flag-File drush_provision_backdrop_pre_provision_backdrop_d6_upgrade';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, $internal_backup_flag_blank)
      ->succeed('Generated blank ' . $local_description)
      ->fail('Could not generate ' . $local_description);
  }
  drush_invoke('provision-backup');
}

/**
 * Implements drush_hook_COMMAND() (collapsed name).
 *
 * Orchestration, mirroring the VM-proven provision-backdrop-upgrade command
 * hook: save the target alias, deploy the backup with the d6_d7_upgrade
 * option (the deploy converts the copy database to Drupal 7), unshare the
 * native files store, stage + enable the pinned kit, run the kit schema
 * updates, migrate the CCK data hard-gated, verify.
 */
function drush_provision_backdrop_d6_upgrade($new_name = NULL, $platform = NULL) {
  drush_set_option('old_platform', d()->platform->name);

  $aegir_root = d('@server_master')->aegir_root;
  $enable_fast_track = $aegir_root . '/static/control/FastTrack.info';

  if (!is_file($enable_fast_track)) {
    // Refresh the collected info about both platforms and the source site
    // first, exactly as the clone command does.
    if (!is_null(d($platform)->name) && (d($platform)->name != d()->platform->name)) {
      provision_backend_invoke('@hostmaster', 'hosting-task', array(d()->platform->name, 'verify'), array('force' => TRUE));
      sleep(5); // A small trick to avoid high load and race conditions.
      provision_backend_invoke('@hostmaster', 'hosting-task', array(d($platform)->name, 'verify'), array('force' => TRUE));
      sleep(5); // A small trick to avoid high load and race conditions.
    }
    $local_uri_verify = '@' . d()->uri;
    provision_backend_invoke('@hostmaster', 'hosting-task', array($local_uri_verify, 'verify'), array('force' => TRUE));
    sleep(5); // A small trick to avoid high load and race conditions.
  }

  // Data-driven enable list: what the LIVE source actually uses, mapped
  // through the kit map, plus the always-on set (content_migrate + core
  // file/image — the D6 upgrade only enables file when D6 upload was in use,
  // and content_migrate misclassifies file/image fields as missing without
  // them). Resolved BEFORE the deploy so a map problem aborts early.
  $db_name = _provision_backdrop_site_db_name();
  $db = d()->service('db');
  $db->connect();
  $source_modules = _provision_backdrop_d6_upgrade_source_modules($db, $db_name);
  if ($source_modules === FALSE) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_NO_DB',
      dt('Could not enumerate the source site modules for the data-driven enable.'));
  }
  $map = _provision_backdrop_d6_upgrade_kit_map();
  if (empty($map)) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_NO_KIT',
      dt('The D6->D7 kit map is missing; cannot compute the enable list.'));
  }
  $kit_projects = array('cck' => TRUE);
  $enable = array('content_migrate' => TRUE, 'file' => TRUE, 'image' => TRUE);
  foreach ($source_modules as $name) {
    if (!isset($map['modules'][$name])) {
      continue;
    }
    $entry = $map['modules'][$name];
    if (!empty($entry['kit_projects']) && is_array($entry['kit_projects'])) {
      foreach ($entry['kit_projects'] as $kit_name) {
        $kit_projects[$kit_name] = TRUE;
      }
    }
    if (!empty($entry['enable']) && is_array($entry['enable'])) {
      foreach ($entry['enable'] as $enable_name) {
        $enable[$enable_name] = TRUE;
      }
    }
  }
  drush_log(dt('D6-UPGRADE/PLAN kit projects: @kits; modules to enable: @modules.',
    array('@kits' => implode(', ', array_keys($kit_projects)), '@modules' => implode(', ', array_keys($enable)))), 'ok');

  $options = d()->options;
  $options['uri'] = ltrim($new_name, '@');
  $hash_name = drush_get_option('#name') ? '#name' : 'name';
  $options[$hash_name] = $new_name;
  $options['platform'] = $platform;
  $options['root'] = d($platform)->root;
  $options['aliases'] = array();
  $options['redirection'] = 0;

  // The copy ships dark: no SSL/LE (keys are bound to the source name), no
  // CDN; cron stays disabled until the operator enables it.
  $options['ssl_enabled'] = 0;
  unset($options['ssl_key']);
  $options['cdn'] = 0;
  unset($options['cdn']);

  // Never carry the source's D6 package map onto the new alias: a stale map
  // would only mislead later consumers, and without it the deploy pre-hook
  // module schema comparison is skipped entirely (nothing to compare).
  unset($options['packages']);

  // The D6 -> D7 core upgrade renames the DB install_profile default ->
  // standard; record the matching (validated) profile on the alias so the
  // context and the converted database agree. Never the raw option: the
  // source alias's 'default' leaks through drush_get_option.
  $options['profile'] = _provision_backdrop_d6_upgrade_profile();

  if ($db_server = drush_get_option('new_db_server', FALSE)) {
    $options['db_server'] = $db_server;
  }

  drush_invoke_process('@none', 'provision-save', array($new_name), $options);

  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
    // Tells the shared Drupal deploy this copy database is still Drupal 6:
    // quarantine the copied site-local modules and skip the pre-updatedb
    // registry rebuild; the deploy's own updatedb performs the staged
    // D6 -> D7 core conversion.
    'd6_d7_upgrade' => 1,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'backdrop_d6_upgrade');

  provision_backend_invoke($new_name, 'provision-deploy', array(drush_get_option('backup_file')), $deploy_options);

  if (!drush_get_error()) {
    // Give the copy its OWN native files/private store BEFORE the contrib
    // leg, so every write from here on lands in the copy's store, never the
    // source's.
    _provision_drupal_native_symlink($options['uri'], TRUE);

    // Stage the pinned D7 kit projects into the copy's site dir from the
    // on-box cache (Octopus fetched it from the mirror; provision never
    // downloads).
    _provision_backdrop_d6_upgrade_stage_kit($options['root'], $options['uri'], array_keys($kit_projects));
  }

  if (!drush_get_error()) {
    // Every module to enable must already ship on the copy: in backend mode
    // pm-enable auto-answers YES, and an unknown name would trigger a
    // drupal.org download attempt at task time (provision never downloads).
    // A miss here means the kit map drifted from the kit — fail closed.
    _provision_backdrop_d6_upgrade_assert_enable_targets($options['root'], $options['uri'], array_keys($enable));
  }

  if (!drush_get_error()) {
    // Enable the data-driven set. pm-enable full-bootstraps the copy — valid
    // now that the deploy's updatedb converted it to Drupal 7.
    provision_backend_invoke($new_name, 'pm-enable', array_keys($enable), array('yes' => 1));
  }

  if (!drush_get_error()) {
    // Second updatedb: the kit modules' 6.x -> 7.x schema updates fire from
    // the schema versions the conversion parked them at.
    provision_backend_invoke($new_name, 'updatedb -y');
  }

  if (!drush_get_error()) {
    // The CCK field-data leg, HARD-gated (no force flag): any field whose
    // provider module is still missing aborts the hop — skipping
    // content_migrate strands the CCK data silently (the upgrade would
    // complete green with the content simply absent).
    $script = dirname(__FILE__) . '/scripts/content-migrate-d6.php';
    if (!is_file($script)) {
      drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_NO_SCRIPT',
        dt('content_migrate driver missing at @script.', array('@script' => $script)));
    }
    else {
      provision_backend_invoke($new_name, 'php-script', array($script));
    }
  }

  if (!drush_get_error()) {
    // Verify the converted copy so the context carries installed/verified
    // state (the frontend import promotes the node to ENABLED only then).
    provision_backend_invoke($new_name, 'provision-verify');

    if (!is_file($enable_fast_track)) {
      sleep(5); // A small trick to avoid high load and race conditions.
      // Re-verify the source site via the frontend, mirroring clone (#1004526).
      provision_backend_invoke('@hostmaster', 'hosting-task', array('@' . d()->uri, 'verify'), array('force' => TRUE));
    }

    $backup_path = d('@server_master')->backup_path;
    $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_backdrop_d6_upgrade')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_backdrop_d6_upgrade');
  }

  provision_reload_config('site', d()->site_path . '/drushrc.php');
}

/**
 * Copy the needed kit project directories into the copy's site dir.
 *
 * The kit cache is root-owned and world-readable; cp -a run as the instance
 * user yields instance-owned copies (a non-root cp cannot chown), which is
 * exactly the ownership the site needs. Existing destinations are never
 * overwritten. A missing project is an error: the enable list references it,
 * so proceeding would only move the failure to a less clear place.
 */
function _provision_backdrop_d6_upgrade_stage_kit($root, $uri, $kit_projects) {
  $modules_dir = $root . '/sites/' . $uri . '/modules';
  if (!is_dir($modules_dir) && !mkdir($modules_dir, 0755, TRUE)) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_NO_KIT',
      dt('Could not create @dir for the kit staging.', array('@dir' => $modules_dir)));
  }
  foreach ($kit_projects as $project) {
    if (!preg_match('/^[a-z0-9_]+$/', $project)) {
      return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_NO_KIT',
        dt('Invalid kit project name @project in the kit map.', array('@project' => $project)));
    }
    $src = PROVISION_BACKDROP_D6_KIT_DIR . '/' . $project;
    $dest = $modules_dir . '/' . $project;
    if (is_dir($dest)) {
      drush_log(dt('D6-UPGRADE/KIT @project already present in the site dir; kept as-is.', array('@project' => $project)), 'ok');
      continue;
    }
    if (!is_dir($src)) {
      return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_NO_KIT',
        dt('Kit project @project is not staged at @src; re-run the BOA/Octopus update to stage the kit.', array('@project' => $project, '@src' => $src)));
    }
    if (drush_shell_exec('cp -a %s %s', $src, $dest)) {
      drush_log(dt('D6-UPGRADE/KIT staged @project into the copy site dir.', array('@project' => $project)), 'ok');
    }
    else {
      return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_NO_KIT',
        dt('Could not copy kit project @project into @dest.', array('@project' => $project, '@dest' => $dest)));
    }
  }
}

/**
 * Assert every module to enable ships on the copy before pm-enable runs.
 *
 * Scans the copy's module roots (core modules/, sites/all/modules,
 * sites/<uri>/modules) for <name>.info files and fails closed on anything
 * unresolved, so pm-enable can never be asked for a module it would try to
 * download.
 */
function _provision_backdrop_d6_upgrade_assert_enable_targets($root, $uri, $enable) {
  $available = array();
  $scan_roots = array(
    $root . '/modules',
    $root . '/sites/all/modules',
    $root . '/sites/' . $uri . '/modules',
  );
  foreach ($scan_roots as $scan_root) {
    if (!is_dir($scan_root)) {
      continue;
    }
    $iterator = new RecursiveIteratorIterator(
      new RecursiveDirectoryIterator($scan_root, FilesystemIterator::SKIP_DOTS),
      RecursiveIteratorIterator::SELF_FIRST
    );
    foreach ($iterator as $item) {
      if ($item->isFile() && substr($item->getFilename(), -5) === '.info') {
        $available[substr($item->getFilename(), 0, -5)] = TRUE;
      }
    }
  }
  $missing = array();
  foreach ($enable as $name) {
    if (!isset($available[$name])) {
      $missing[] = $name;
    }
  }
  if (!empty($missing)) {
    return drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_NO_KIT',
      dt('Modules to enable are not present on the copy: @missing. The kit map names something the staged kit (or Drupal 7 core) does not ship; aborting before pm-enable could try to download it.',
      array('@missing' => implode(', ', $missing))));
  }
  return TRUE;
}

/**
 * Implements drush_hook_COMMAND_rollback() (collapsed name).
 *
 * Deliberately non-destructive: the copy's artifacts (target alias, copy
 * database, target site dir) are harmless residue while the source keeps
 * serving; deleting them automatically from a failure path risks more than
 * it saves. Report exactly what may exist and how to discard it.
 */
function drush_provision_backdrop_d6_upgrade_rollback($new_name = NULL, $platform = NULL) {
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  if (provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_backdrop_d6_upgrade_rollback')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_backdrop_d6_upgrade_rollback');
  }
  drush_log(dt('provision-backdrop-d6-upgrade failed; the source site @uri is untouched and still serving. Partial copy artifacts may remain (site alias @new, its site directory and copy database on the target platform); discard them with: drush @new provision-delete (or remove the alias with provision-save --delete) before retrying.',
    array('@uri' => d()->uri, '@new' => is_string($new_name) ? $new_name : '@<new site>')), 'warning');
}
