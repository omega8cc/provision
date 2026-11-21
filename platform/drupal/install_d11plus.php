<?php

/**
 * @file
 * Install a Drupal 11+ site with core's own installer, without Drush.
 *
 * Standalone CLI script, like rebuild_d8plus.php beside it: provision runs it
 * in a subprocess on the account's CLI PHP, as the site owner, from the site's
 * docroot, for a codebase whose Drush has no drush.php (Drush 15 and newer run
 * only under core's dr, whose own install command writes SQLite) or that
 * carries no Drush at all:
 *
 *   php install_d11plus.php <docroot> <site uri> <profile> <langcode> <account mail> <site name>
 *
 * It calls install_drupal() non-interactively for sites/<site uri>, the way
 * core's install command and Drush's site:install do. The database comes from
 * the settings.php provision wrote for the site before this runs: the
 * installer verifies it and skips its settings form, so it never rewrites that
 * file, and no password travels on a command line. Then, in the same process,
 * the steps provision ran through the site-local Drush after a site-install:
 * the one-time login link for user 1, the stored usage data choice
 * (drupal_cms_telemetry FALSE) and the removal of automatic_updates and
 * package_manager, which must never rewrite a BOA codebase.
 *
 * Lines it prints, each on its own line:
 *   INSTALL/D11PLUS/TASK <task>      an installer task began
 *   INSTALL/D11PLUS/MSG <type> <text> a message the installer left
 *   INSTALL/D11PLUS/WARN <text>      a post-install step that did not complete
 *   INSTALL/D11PLUS/LOGIN <path>     the login link's path (provision keeps it
 *                                    out of the task log)
 *   INSTALL/D11PLUS/DONE <site path> drupal=<version> php=<version>
 * Exit 0 with the DONE line on success. Any failure prints a
 * INSTALL/D11PLUS/FAIL line on stderr and exits non-zero (2 a usage or
 * docroot error, 3 no settings.php, 1 the installer failed); a fatal is
 * reported by the shutdown function below, whatever the account's php.ini says.
 */

ini_set('display_errors', 'stderr');
ini_set('log_errors', '0');
error_reporting(E_ALL & ~E_DEPRECATED & ~E_USER_DEPRECATED);

$root = isset($argv[1]) ? $argv[1] : '';
$uri = isset($argv[2]) ? $argv[2] : '';
$profile = isset($argv[3]) ? $argv[3] : '';
$langcode = isset($argv[4]) ? $argv[4] : 'en';
$mail = isset($argv[5]) ? $argv[5] : '';
$name = isset($argv[6]) ? $argv[6] : '';

function _install_d11plus_fail($message, $code, $e = NULL) {
  fwrite(STDERR, 'INSTALL/D11PLUS/FAIL ' . $message . "\n");
  // Where an exception came from: its own line and the first frames of its
  // trace, so a failed install says where it stopped.
  if (is_object($e) && method_exists($e, 'getTrace')) {
    fwrite(STDERR, 'INSTALL/D11PLUS/AT ' . $e->getFile() . ':' . $e->getLine() . "\n");
    $n = 0;
    foreach ($e->getTrace() as $frame) {
      if (++$n > 15) {
        break;
      }
      fwrite(STDERR, 'INSTALL/D11PLUS/TRACE ' . (isset($frame['class']) ? $frame['class'] . (isset($frame['type']) ? $frame['type'] : '::') : '') . (isset($frame['function']) ? $frame['function'] : '?') . ' ' . (isset($frame['file']) ? $frame['file'] . ':' . (isset($frame['line']) ? $frame['line'] : '?') : '') . "\n");
    }
  }
  exit($code);
}

register_shutdown_function(function () {
  $error = error_get_last();
  if ($error && in_array($error['type'], array(E_ERROR, E_PARSE, E_CORE_ERROR, E_COMPILE_ERROR, E_RECOVERABLE_ERROR), TRUE)) {
    fwrite(STDERR, 'INSTALL/D11PLUS/FATAL ' . $error['message'] . ' in ' . $error['file'] . ':' . $error['line'] . "\n");
  }
});

if ($root === '' || $uri === '' || $profile === '') {
  _install_d11plus_fail('usage: install_d11plus.php <docroot> <site uri> <profile> <langcode> <account mail> <site name>', 2);
}
if (!is_dir($root)) {
  _install_d11plus_fail('no such docroot: ' . $root, 2);
}
$root = realpath($root);
if (!is_file($root . '/autoload.php') || !is_file($root . '/core/includes/install.core.inc')) {
  _install_d11plus_fail('not a Drupal 8+ docroot: ' . $root, 2);
}
if (!preg_match('/^[A-Za-z0-9.\-]+$/', $uri)) {
  _install_d11plus_fail('refusing an unexpected site uri', 2);
}
if (!preg_match('/^[a-z][a-z0-9_]*$/', $profile)) {
  _install_d11plus_fail('refusing an unexpected install profile name', 2);
}
if (!preg_match('/^[a-z]{2,3}(-[a-z0-9]{1,8})*$/', $langcode)) {
  $langcode = 'en';
}
if ($mail === '' || !filter_var($mail, FILTER_VALIDATE_EMAIL)) {
  $mail = 'admin@example.com';
}
if ($name === '') {
  $name = $uri;
}
if (!chdir($root)) {
  _install_d11plus_fail('cannot chdir to ' . $root, 2);
}
$site_path = 'sites/' . $uri;
if (!is_file($root . '/' . $site_path . '/settings.php')) {
  _install_d11plus_fail('no settings.php under ' . $site_path, 3);
}

// The request the installer resolves the site from, shaped as for a web
// request to the site (rebuild_d8plus.php does the same): the cache prefix and
// every absolute URL built during the install then carry the site's own name.
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

define('MAINTENANCE_MODE', 'install');
$autoloader = require $root . '/autoload.php';
require_once $root . '/core/includes/install.core.inc';

// BOA's settings send the bootstrap, discovery and config bins of a process
// without APCu (every command line here) to its chained backend over the
// site's default backend (global-valkey.inc, global-chained-d8.services.yml),
// which persists. Core's installer starts on in-memory caches, and from the
// base system on it rebuilds the container as each group of modules goes in,
// within this one process; with those bins persistent it read the entity
// definitions cached when only the system module was in ("The "user" entity
// type does not exist." in the module batch, Drupal 12.0.0-beta2 and 11.5.0,
// with Valkey on and off). So for this install only, that backend gives
// in-memory bins, as the installer's own first phase does for every bin. The
// site's settings are untouched: once installed, its bins are BOA's again, and
// provision's core rebuild after the install starts them clean.
if (!class_exists('InstallD11PlusMemoryBins', FALSE)) {
  class InstallD11PlusMemoryBins implements \Drupal\Core\DependencyInjection\ServiceModifierInterface {
    public function alter(\Drupal\Core\DependencyInjection\ContainerBuilder $container) {
      if ($container->hasDefinition('cache.backend.boa_chained')) {
        $container->getDefinition('cache.backend.boa_chained')
          ->setClass('Drupal\Core\Cache\MemoryBackendFactory')
          ->setArguments(array(new \Symfony\Component\DependencyInjection\Reference('datetime.time')))
          ->setMethodCalls(array());
      }
    }
  }
}
$GLOBALS['conf']['container_service_providers']['InstallD11PlusMemoryBins'] = 'InstallD11PlusMemoryBins';

// A random admin password, as core and Drush make one; never printed: the
// one-time login link is the way in, and the task log is readable by every
// login allowed to see the site's tasks.
$pass = rtrim(strtr(base64_encode(random_bytes(12)), '+/', '-_'), '=');

// The values Drush's site:install passes on today's Drupal 11 path, in the
// keys core's own install command uses (core/lib/Drupal/Core/Command/
// InstallCommand.php), with no install_settings_form: the site's own
// settings.php carries the database.
$settings = array(
  'interactive' => FALSE,
  'site_path' => $site_path,
  'parameters' => array(
    'profile' => $profile,
    'langcode' => $langcode,
    'existing_config' => FALSE,
  ),
  'forms' => array(
    'install_configure_form' => array(
      'site_name' => $name,
      'site_mail' => 'admin@example.com',
      'account' => array(
        'name' => 'admin',
        'mail' => $mail,
        'pass' => array(
          'pass1' => $pass,
          'pass2' => $pass,
        ),
      ),
      'enable_update_status_module' => TRUE,
      'enable_update_status_emails' => TRUE,
    ),
  ),
);

// The task the installer runs, by its name, as Drush's own callback logs it.
// Nothing else: a callback that asks the installer for its task list makes it
// read the profile and its extensions between two tasks, and on a box whose
// caches persist (BOA's Valkey and chained bins) that stored the entity type
// definitions of a half-installed site ("The "user" entity type does not
// exist." in the module batch, measured on Drupal 12.0.0-beta2).
$task_seen = '';
$callback = function ($install_state) use (&$task_seen) {
  $task = isset($install_state['active_task']) ? (string) $install_state['active_task'] : '';
  if ($task !== '' && $task !== $task_seen) {
    $task_seen = $task;
    echo 'INSTALL/D11PLUS/TASK ' . $task . "\n";
  }
};

try {
  install_drupal($autoloader, $settings, $callback);
}
catch (\Exception $e) {
  _install_d11plus_fail(get_class($e) . ': ' . $e->getMessage(), 1, $e);
}
catch (\Throwable $e) {
  _install_d11plus_fail(get_class($e) . ': ' . $e->getMessage(), 1, $e);
}

if (!class_exists('\Drupal') || !\Drupal::hasContainer()) {
  _install_d11plus_fail('the installer returned without a site container', 1);
}

// Messages the installer left (a translation it could not fetch, a warning
// from a profile): the task log keeps them.
try {
  foreach (\Drupal::messenger()->all() as $type => $messages) {
    foreach ((array) $messages as $message) {
      echo 'INSTALL/D11PLUS/MSG ' . $type . ' ' . preg_replace('/\s+/', ' ', strip_tags((string) $message)) . "\n";
    }
  }
  \Drupal::messenger()->deleteAll();
}
catch (\Throwable $e) {
}

// The usage data choice, stored as the site-local Drush stored it (inert on a
// site without drupal_cms_helper).
try {
  \Drupal::state()->set('drupal_cms_telemetry', FALSE);
}
catch (\Throwable $e) {
  echo 'INSTALL/D11PLUS/WARN could not store drupal_cms_telemetry FALSE: ' . $e->getMessage() . "\n";
}

// Web UI tools must never rewrite the codebase: automatic_updates first (it
// depends on package_manager), then package_manager, each only if installed.
foreach (array('automatic_updates', 'package_manager') as $module) {
  try {
    if (\Drupal::moduleHandler()->moduleExists($module)) {
      \Drupal::service('module_installer')->uninstall(array($module));
      echo 'INSTALL/D11PLUS/UNINSTALL ' . $module . "\n";
    }
  }
  catch (\Throwable $e) {
    echo 'INSTALL/D11PLUS/WARN could not uninstall ' . $module . ': ' . $e->getMessage() . "\n";
  }
}

// The one-time login link's path for user 1, as core's user.reset.login route
// shapes it (user/reset/{uid}/{timestamp}/{hash}/login); provision puts the
// site's address in front of it.
try {
  $account = \Drupal\user\Entity\User::load(1);
  if ($account) {
    $timestamp = time();
    echo 'INSTALL/D11PLUS/LOGIN /user/reset/' . $account->id() . '/' . $timestamp . '/' . user_pass_rehash($account, $timestamp) . "/login\n";
  }
  else {
    echo "INSTALL/D11PLUS/WARN no user 1 to make a login link for\n";
  }
}
catch (\Throwable $e) {
  echo 'INSTALL/D11PLUS/WARN could not make the login link: ' . $e->getMessage() . "\n";
}

echo 'INSTALL/D11PLUS/DONE ' . $site_path . ' drupal=' . \Drupal::VERSION . ' php=' . PHP_VERSION . ' profile=' . $profile . "\n";
exit(0);
