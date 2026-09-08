<?php

/**
 * @file
 * Rebuild a Drupal 8+ site's service container and flush every cache bin,
 * without Drush.
 *
 * Standalone CLI script, unlike the *.inc engines beside it: provision runs it
 * in a subprocess under the same PHP that runs the backend itself, as the site
 * owner, from the site's docroot:
 *
 *   php rebuild_d8plus.php <docroot> <site uri>
 *
 * It is core's own rebuild (core/rebuild.php minus the HTTP token gate):
 * bootstrap the environment, initialise settings for the site the URI names,
 * then drupal_rebuild(), which invalidates and recompiles the container and
 * flushes every cache. Nothing from vendor/drush is loaded, so the platform's
 * lock state does not matter -- on a LOCKED D10/D11 platform every site-local
 * Drush fatals at class load under PHP 8 (the de-typed Symfony Console Output
 * against the typed StreamOutput, then Consolidation\Log\Logger against the
 * psr/log v1 overlay), while core itself, patched to that overlay, loads
 * exactly as the web serves it -- and the compiled container carries no Drush
 * service, so it can never be the poisoned container that a Drush-driven
 * rebuild in the stock state bakes (logger.drupaltodrush).
 *
 * Exit 0 with a REBUILD/CORE/DONE line on success. Any failure prints on
 * stderr and exits non-zero: a fatal at class load is reported by the
 * shutdown function below, which does not depend on the account's php.ini.
 * The caller logs the output either way.
 */

// Errors go to stderr, which the caller captures with stdout; deprecations
// are not reported (core under a newer PHP emits them at class compile and
// during the rebuild, the web hides them, and they are not what a task log
// is for). Fatals are reported by the shutdown function below regardless of
// the account's php.ini.
ini_set('display_errors', 'stderr');
ini_set('log_errors', '0');
error_reporting(E_ALL & ~E_DEPRECATED & ~E_USER_DEPRECATED);

$root = isset($argv[1]) ? $argv[1] : '';
$uri = isset($argv[2]) ? $argv[2] : '';

function _rebuild_d8plus_fail($message, $code) {
  fwrite(STDERR, 'REBUILD/CORE/FAIL ' . $message . "\n");
  exit($code);
}

register_shutdown_function(function () {
  $error = error_get_last();
  if ($error && in_array($error['type'], array(E_ERROR, E_PARSE, E_CORE_ERROR, E_COMPILE_ERROR, E_RECOVERABLE_ERROR), TRUE)) {
    fwrite(STDERR, 'REBUILD/CORE/FATAL ' . $error['message'] . ' in ' . $error['file'] . ':' . $error['line'] . "\n");
  }
});

if ($root === '' || $uri === '') {
  _rebuild_d8plus_fail('usage: rebuild_d8plus.php <docroot> <site uri>', 2);
}
if (!is_dir($root)) {
  _rebuild_d8plus_fail('no such docroot: ' . $root, 2);
}
$root = realpath($root);
if (!is_file($root . '/autoload.php') || !is_file($root . '/core/includes/utility.inc')) {
  _rebuild_d8plus_fail('not a Drupal 8+ docroot: ' . $root, 2);
}
if (!preg_match('/^[A-Za-z0-9.\-]+$/', $uri)) {
  _rebuild_d8plus_fail('refusing an unexpected site uri', 2);
}
if (!chdir($root)) {
  _rebuild_d8plus_fail('cannot chdir to ' . $root, 2);
}

// The request the kernel resolves the site directory from, the way Drush
// shapes it for --uri: the host is the site, the script is index.php.
$_SERVER['HTTP_HOST'] = $uri;
$_SERVER['SERVER_NAME'] = $uri;
$_SERVER['SERVER_PORT'] = 80;
$_SERVER['SCRIPT_NAME'] = '/index.php';
$_SERVER['SCRIPT_FILENAME'] = $root . '/index.php';
$_SERVER['PHP_SELF'] = '/index.php';
$_SERVER['REQUEST_URI'] = '/';
$_SERVER['REQUEST_METHOD'] = 'GET';
$_SERVER['REMOTE_ADDR'] = '127.0.0.1';
$_SERVER['SERVER_SOFTWARE'] = NULL;

$autoloader = require $root . '/autoload.php';
require_once $root . '/core/includes/bootstrap.inc';
require_once $root . '/core/includes/utility.inc';

try {
  $request = \Symfony\Component\HttpFoundation\Request::createFromGlobals();
  \Drupal\Core\DrupalKernel::bootEnvironment();
  // bootEnvironment() widens the reporting again; keep deprecations out of
  // the rebuild's own output the same way (drupal_rebuild() restores PHP's
  // default handler for its duration, so they would print otherwise).
  error_reporting(E_ALL & ~E_DEPRECATED & ~E_USER_DEPRECATED);
  $site_path = \Drupal\Core\DrupalKernel::findSitePath($request);
  if (!is_file($root . '/' . $site_path . '/settings.php')) {
    _rebuild_d8plus_fail('no settings.php under ' . $site_path . ' for ' . $uri, 3);
  }
  \Drupal\Core\Site\Settings::initialize($root, $site_path, $autoloader);
  drupal_rebuild($autoloader, $request);
}
catch (\Exception $e) {
  _rebuild_d8plus_fail(get_class($e) . ': ' . $e->getMessage(), 1);
}
catch (\Throwable $e) {
  _rebuild_d8plus_fail(get_class($e) . ': ' . $e->getMessage(), 1);
}

echo 'REBUILD/CORE/DONE ' . $site_path . ' drupal=' . \Drupal::VERSION . ' php=' . PHP_VERSION . "\n";
exit(0);
