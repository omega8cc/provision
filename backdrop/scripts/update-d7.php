#!/usr/bin/env php
<?php

/**
 * Convert a Drupal 7 database to Backdrop through the command line.
 *
 * Provision-shipped driver for the D7 -> Backdrop conversion step of
 * provision-backdrop-upgrade. It runs as a SEPARATE process under a modern
 * PHP CLI (Backdrop floor is 7.1; BOA invokes the backend PHP_BINARY), so the
 * provision-side PHP 5.6 floor and the bootstrap-free backend doctrine do not
 * apply here: this process bootstraps Backdrop with core's own
 * backdrop_bootstrap(), never through drush/BackdropBoot.
 *
 * It reproduces core/update.php's staged sequence (update.php cannot be
 * driven headlessly: it is a token- and session-guarded web batch), in TWO
 * processes because the web flow is one request per step and a single process
 * goes stale twice (both VM-caught): (a) module_invoke_all()/system data read
 * through the module world bootstrapped BEFORE update_fix_compatibility()
 * disabled the D7 leftovers, so the requirements gate errors on modules that
 * are already disabled on disk; (b) update_upgrade_enable_dependencies() can
 * enable a Backdrop-original module (layout, required by dashboard) whose
 * functions are not loaded in-process, and the next menu rebuild fatals
 * (dashboard_menu() -> layout_load_multiple_by_path()).
 *
 * Phase 1 (default): update_prepare_bootstrap() -> SESSION -> LANGUAGE ->
 * update_fix_requirements() -> FULL -> backdrop_load_updates() ->
 * update_fix_compatibility() -> D7 dependency report + enables (pre-including
 * each module file it enables) -> re-exec phase 2.
 * Phase 2 (--phase=batch, fresh process, clean statuses): same staged
 * bootstrap (all idempotent) -> requirements gate -> non-progressive
 * update_batch() -> hard success checks -> node access rebuild.
 *
 * Usage (cwd anywhere):
 *   php update-d7.php --root=/path/to/backdrop/platform --url=site.example.com
 *
 * Exit codes: 0 = converted (or nothing pending on an already-converted DB);
 * 1 = failed, with the reason on stdout. The caller treats non-zero as a
 * conversion failure and discards the copy database.
 */

$script_path = $_SERVER['argv'][0];
$script = basename(array_shift($_SERVER['argv']));

if (in_array('--help', $_SERVER['argv']) || empty($_SERVER['argv'])) {
  echo <<<EOF

Convert a Drupal 7 database to Backdrop through the command line.

The site must already be deployed on a Backdrop platform: Backdrop codebase,
Backdrop-shaped settings.php pointing at the (copied) Drupal 7 database, and
config directories in place. Run as the site owner, never root.

Options:
--root   The Backdrop platform root (docroot). Required unless cwd is the root.
--url    The site URI, as mapped in sites/sites.php. Required for multisite.

EOF;
  exit;
}

// Define default server settings (console harness, matching install.sh).
$_SERVER['HTTP_HOST']       = 'default';
$_SERVER['PHP_SELF']        = '/index.php';
$_SERVER['REMOTE_ADDR']     = '127.0.0.1';
$_SERVER['SERVER_SOFTWARE'] = NULL;
$_SERVER['REQUEST_METHOD']  = 'GET';
$_SERVER['QUERY_STRING']    = '';
$_SERVER['PHP_SELF']        = $_SERVER['REQUEST_URI'] = '/';
$_SERVER['HTTP_USER_AGENT'] = 'console';

$options = array(
  'root' => '',
  'url' => '',
  'phase' => '',
);
while ($param = array_shift($_SERVER['argv'])) {
  if (strpos($param, '--') === 0) {
    $param = substr($param, 2);
    if (strpos($param, '=')) {
      list($key, $value) = explode('=', $param, 2);
      $options[$key] = $value;
    }
    else {
      $options[$param] = array_shift($_SERVER['argv']);
    }
  }
}

if ($options['root'] && is_dir($options['root'])) {
  chdir($options['root']);
}

if ($options['url']) {
  $url_parts = parse_url($options['url']);
  if (!empty($url_parts['host'])) {
    $_SERVER['HTTP_HOST'] = $url_parts['host'];
  }
  elseif (!empty($url_parts['path'])) {
    $_SERVER['HTTP_HOST'] = $url_parts['path'];
  }
  else {
    print "--url option is invalid. Specify the site URI as --url=site.example.com\n";
    exit(1);
  }
}

if (!is_file('./core/includes/bootstrap.inc')) {
  print "Not a Backdrop root: " . getcwd() . " (pass --root=/path/to/platform)\n";
  exit(1);
}

define('BACKDROP_ROOT', getcwd());
define('MAINTENANCE_MODE', 'update');

// Some unavoidable errors happen because the database is not yet up-to-date;
// suppress them exactly as core/update.php does, re-enable after FULL.
ini_set('display_errors', FALSE);

require_once BACKDROP_ROOT . '/core/includes/bootstrap.inc';
require_once BACKDROP_ROOT . '/core/includes/update.inc';
require_once BACKDROP_ROOT . '/core/includes/common.inc';
require_once BACKDROP_ROOT . '/core/includes/file.inc';
require_once BACKDROP_ROOT . '/core/includes/unicode.inc';

// core/includes/update.inc calls update_extra_requirements() (the store the
// requirements gate records into, update.inc:144) but the function is defined
// in core/update.php, the web front controller — every headless driver must
// supply it (the drush extension port does the same, includes/update.inc:32).
if (!function_exists('update_extra_requirements')) {
  function update_extra_requirements($requirements = NULL) {
    static $extra_requirements = array();
    if (isset($requirements)) {
      $extra_requirements += $requirements;
    }
    return $extra_requirements;
  }
}

/**
 * Build the re-exec command line for the next phase (fresh process).
 */
function _update_d7_phase_cmd($script_path, $phase) {
  return escapeshellarg(PHP_BINARY) . ' ' . escapeshellarg($script_path)
    . ' --root=' . escapeshellarg(BACKDROP_ROOT)
    . ' --url=' . escapeshellarg($_SERVER['HTTP_HOST'])
    . ' --phase=' . escapeshellarg($phase) . ' 2>&1';
}

try {
  // Both phases share the staged, D7-tolerant bootstrap; every step in it is
  // idempotent (schema surgery is guarded, requirement fixes re-apply clean).
  update_prepare_bootstrap();

  backdrop_bootstrap(BACKDROP_BOOTSTRAP_SESSION);
  // The interface language global has been renamed in Backdrop; keep it valid
  // while language settings are upgraded (core/update.php:531).
  $GLOBALS[LANGUAGE_TYPE_INTERFACE] = language_default();

  // update_fix_requirements() needs to run before bootstrapping beyond path;
  // bootstrap to LANGUAGE first — core's deliberate ordering.
  backdrop_bootstrap(BACKDROP_BOOTSTRAP_LANGUAGE);
  include_once BACKDROP_ROOT . '/core/includes/unicode.inc';
  update_fix_requirements();

  backdrop_bootstrap(BACKDROP_BOOTSTRAP_FULL);
  ini_set('display_errors', TRUE);

  include_once BACKDROP_ROOT . '/core/includes/install.inc';
  include_once BACKDROP_ROOT . '/core/includes/batch.inc';
  backdrop_load_updates();

  // Disable extensions whose .info lacks backdrop = 1.x (D7 leftovers).
  update_fix_compatibility();

  if ($options['phase'] === '') {
    // ---- PHASE compat: nothing beyond the shared update_fix_compatibility()
    // above. On a fresh copy the FULL bootstrap of THIS process loaded the
    // still-enabled D7 contrib; any further machinery (dependency enables,
    // cache flushes) would fire their hooks against not-yet-converted data
    // (VM-caught: a D7 hook reached filter_default_format() -> TypeError on
    // the missing format config). Persist the disables, then hand over to a
    // fresh process that bootstraps the corrected module world.
    print "Phase compat complete; continuing in a fresh process.\n";
    passthru(_update_d7_phase_cmd($script_path, 'enable'), $exit_code);
    exit($exit_code);
  }

  if ($options['phase'] === 'enable') {
    // ---- PHASE enable: D7 dependency report + enables, on a clean bootstrap
    // (the D7 leftovers are disabled now, so they are not loaded here). ----
    $dependency_report = update_upgrade_check_dependencies();
    if ($dependency_report) {
      print trim(strip_tags($dependency_report)) . "\n";
    }

    // Pre-include the module files about to be enabled: enabling can trigger
    // cache/menu rebuilds in THIS process, and an already-loaded module
    // (dashboard, via the still-enabled D7 row) may call into the module
    // being enabled (layout) before any fresh bootstrap would load it
    // (VM-caught: dashboard_menu() -> layout_load_multiple_by_path()).
    if (backdrop_get_installed_schema_version('system') > 7000) {
      $modules_to_enable = update_upgrade_modules_to_enable();
      if (!empty($modules_to_enable)) {
        $files = system_rebuild_module_data();
        foreach (array_keys($modules_to_enable) as $module_to_enable) {
          if (isset($files[$module_to_enable]->uri) && is_file(BACKDROP_ROOT . '/' . $files[$module_to_enable]->uri)) {
            include_once BACKDROP_ROOT . '/' . $files[$module_to_enable]->uri;
            print "Pre-loaded $module_to_enable for in-process enable.\n";
          }
        }
      }
    }
    update_upgrade_enable_dependencies();

    // The batch runs with the module world bootstrapped from the NOW
    // corrected + extended {system} statuses — the web flow's next request.
    print "Phase enable complete; running the update batch in a fresh process.\n";
    passthru(_update_d7_phase_cmd($script_path, 'batch'), $exit_code);
    exit($exit_code);
  }

  // ---- PHASE batch: requirements gate + the update batch. ----

  // CLI form of update_check_requirements(): the web version prints an HTML
  // page and exit()s. REQUIREMENT_ERROR here includes the < 7069 schema gate
  // recorded by update_prepare_bootstrap() via update_extra_requirements().
  $requirements = module_invoke_all('requirements', 'update');
  $requirements += update_extra_requirements();
  $severity = backdrop_requirements_severity($requirements);
  if ($severity == REQUIREMENT_ERROR) {
    print "Update requirements not met:\n";
    foreach ($requirements as $requirement) {
      if (isset($requirement['severity']) && $requirement['severity'] == REQUIREMENT_ERROR) {
        $title = isset($requirement['title']) ? $requirement['title'] : '?';
        $value = isset($requirement['value']) ? strip_tags($requirement['value']) : '';
        $description = isset($requirement['description']) ? strip_tags($requirement['description']) : '';
        print "- $title: $value $description\n";
      }
    }
    exit(1);
  }

  // Build the per-module start map, as bee's update-db does.
  $pending = update_get_update_list();
  $system_schema = backdrop_get_installed_schema_version('system');
  if (empty($pending)) {
    if ($system_schema > 7000) {
      print "No pending updates found but the system schema is still Drupal 7 ($system_schema) — refusing to call this converted.\n";
      exit(1);
    }
    print "No pending database updates.\n";
    exit(0);
  }
  $start = array();
  foreach ($pending as $module => $updates) {
    if (!isset($updates['start'])) {
      $warning = !empty($updates['warning'])
        ? strip_tags($updates['warning'])
        : "$module can not be updated due to unresolved requirements.";
      print "WARNING: $warning\n";
      continue;
    }
    $start[$module] = $updates['start'];
  }
  if (empty($start)) {
    print "Every pending update is blocked by unresolved requirements.\n";
    exit(1);
  }

  // Run the whole batch synchronously in this process (bee's pattern);
  // update_batch() itself flips maintenance mode around the run and
  // update_finished() flushes all caches.
  $batch = &batch_get();
  $batch['progressive'] = FALSE;
  update_batch($start);

  // Hard success check: schema versions must have advanced. A failed update
  // aborts its module's chain, leaving entries pending.
  $still_pending = update_get_update_list();
  $failed = array();
  if (isset($_SESSION['update_results']) && is_array($_SESSION['update_results'])) {
    foreach ($_SESSION['update_results'] as $module => $numbers) {
      if (!is_array($numbers)) {
        continue;
      }
      foreach ($numbers as $number => $result) {
        if (is_array($result) && !empty($result['#abort'])) {
          $failed[] = "$module $number";
        }
      }
    }
  }
  if (!empty($failed) || !empty($still_pending)) {
    if (!empty($failed)) {
      print "Failed updates: " . implode(', ', $failed) . "\n";
    }
    if (!empty($still_pending)) {
      print "Updates still pending after the batch: " . implode(', ', array_keys($still_pending)) . "\n";
    }
    exit(1);
  }

  // Results-page equivalent of core/update.php:702.
  state_del('update_d7_upgrade');

  // Rebuild node access if the conversion flagged it.
  if (function_exists('node_access_needs_rebuild') && node_access_needs_rebuild()) {
    if (function_exists('node_access_rebuild')) {
      node_access_rebuild();
      print "Rebuilt the node access table.\n";
    }
  }

  $final_schema = backdrop_get_installed_schema_version('system');
  print "Conversion complete. system schema: $system_schema -> $final_schema.\n";
  exit(0);
}
catch (Throwable $e) {
  print "Conversion failed:\n";
  print get_class($e) . ': ' . $e->getMessage() . ' in ' . $e->getFile() . ':' . $e->getLine() . "\n";
  print $e->getTraceAsString() . "\n";
  exit(1);
}
