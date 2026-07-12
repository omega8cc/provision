#!/usr/bin/env php
<?php

/**
 * Convert a Drupal 7 database to Backdrop through the command line.
 *
 * Provision-shipped driver for the D7 -> Backdrop conversion step of
 * provision-backdrop-upgrade. It runs as a SEPARATE process under a modern
 * PHP CLI (Backdrop floor is 7.1; BOA invokes /opt/php84/bin/php), so the
 * provision-side PHP 5.6 floor and the bootstrap-free backend doctrine do not
 * apply here: this process bootstraps Backdrop with core's own
 * backdrop_bootstrap(), never through drush/BackdropBoot.
 *
 * It reproduces core/update.php's staged sequence faithfully (update.php
 * cannot be driven headlessly: it is a token- and session-guarded web batch):
 * update_prepare_bootstrap() -> SESSION -> LANGUAGE -> update_fix_requirements()
 * -> FULL -> backdrop_load_updates() -> update_fix_compatibility() ->
 * requirements gate -> D7 dependency enable -> non-progressive update_batch().
 * The LANGUAGE-before-update_fix_requirements order is core's, and core marks
 * it deliberate — keep it (core/update.php:566-576).
 *
 * Usage (cwd anywhere):
 *   /opt/php84/bin/php update-d7.php --root=/path/to/backdrop/platform --url=site.example.com
 *
 * Exit codes: 0 = converted (or nothing pending on an already-converted DB);
 * 1 = failed, with the reason on stdout. The caller treats non-zero as a
 * conversion failure and discards the copy database.
 */

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

try {
  // The D7-tolerant pre-bootstrap: config dirs, {state} table, role/langcode
  // schema surgery — and the REQUIRED_D7_SCHEMA_VERSION requirement.
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

  // D7 upgrade bookkeeping: sets the update_d7_upgrade state and reports,
  // then enables the Backdrop modules the enabled set depends on (e.g. the
  // core namesakes of absorbed D7 contrib) — core runs both before the batch.
  $dependency_report = update_upgrade_check_dependencies();
  if ($dependency_report) {
    print trim(strip_tags($dependency_report)) . "\n";
  }
  update_upgrade_enable_dependencies();

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
  exit(1);
}
