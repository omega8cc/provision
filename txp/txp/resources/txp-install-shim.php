<?php
/**
 * @file
 * Headless multisite installer for Textpattern (Aegir/BOA provision shim).
 *
 * Mirrors Textpattern's official CLI installer (textpattern/setup/setup.php,
 * CLI-SAPI-only since 4.7.0) with the three things it lacks for our layout:
 * the is_multisite + multisite_root_path constants, and consumption of a
 * PRE-RENDERED per-site private/config.php (the provision settings writer is
 * the single authority for that file; this shim never generates one).
 *
 * Runs under PHP >= 8.4 (enforced by the caller; re-checked here). ONE process
 * per site: TXP's own setup code defines process-global constants.
 * Exit codes: 0 success, 128 refused/failed (mirrors setup.php).
 */

if (@php_sapi_name() != 'cli') {
  exit(1);
}
if (version_compare(PHP_VERSION, '8.4.0', '<')) {
  fwrite(STDERR, "[ERROR]\tTXP shim requires PHP >= 8.4, got " . PHP_VERSION . "\n");
  exit(128);
}

$params = getopt('', array('config:', 'debug::'));
if (!($file = $params['config'] ?? '')) {
  exit("Usage: php txp-install-shim.php --config=site.json\n");
}
$cfg = json_decode((string) file_get_contents($file), true);
if (empty($cfg)) {
  fwrite(STDERR, "[ERROR]\tbad JSON config\n");
  exit(128);
}

$config_php = $cfg['paths']['config_php'] ?? '';
if ($config_php === '' || !is_readable($config_php)) {
  fwrite(STDERR, "[ERROR]\tpre-rendered config.php not readable: {$config_php}\n");
  exit(128);
}

define('txpinterface', 'admin');
define('txpath', $cfg['paths']['txpath']);
define('is_multisite', true);
define('multisite_root_path', $cfg['paths']['multisite_root_path']);
define('MSG_OK', '[OK]');
define('MSG_ALERT', '[WARNING]');
define('MSG_ERROR', '[ERROR]');

error_reporting(E_ALL);
@ini_set('display_errors', '1');

include_once txpath . '/lib/class.trace.php';
$trace = new Trace();
include_once txpath . '/lib/constants.php';
include_once txpath . '/lib/txplib_misc.php';
include_once txpath . '/lib/txplib_admin.php';
include_once txpath . '/vendors/Textpattern/Loader.php';

$loader = new \Textpattern\Loader(txpath . '/vendors');
$loader->register();
$loader = new \Textpattern\Loader(txpath . '/lib');
$loader->register();

include_once txpath . '/lib/txplib_html.php';
include_once txpath . '/lib/txplib_forms.php';
include_once txpath . '/include/txp_auth.php';
include_once txpath . '/setup/setup_lib.php';

assert_system_requirements();
setup_load_lang($cfg['site']['language_code'] ?? 'en-gb');

// Already-installed guard belongs to the caller (it owns force-reinstall);
// here only the config authority rule: config.php must pre-exist (checked
// above) and setup_db()'s own "tables exist" guard still protects the DB.
setup_connect();

// The single config authority: include the provision-rendered file verbatim.
// $txpcfg lands at GLOBAL scope here, which setup_db()'s include of
// txplib_db.php requires (PFX + the immediate $DB connection).
include $config_php;

if (empty($cfg['user']['login_name'])) {
  msg(gTxt('name_required'), MSG_ERROR);
}
if (empty($cfg['user']['password'])) {
  msg(gTxt('pass_required'), MSG_ERROR);
}
if (!is_valid_email($cfg['user']['email'])) {
  msg(gTxt('email_required'), MSG_ERROR);
}

setup_db($cfg);
msg('install complete');
setup_die(0);

function msg($msg, $class = MSG_OK, $back = false)
{
  echo "$class\t" . strip_tags((string) $msg) . "\n";
  if ($class == MSG_ERROR) {
    setup_die(128);
  }
}

function setup_die($code = 0)
{
  global $trace, $params;
  if (isset($params['debug'])) {
    echo $trace->summary();
    echo $trace->result();
  }
  exit((int) $code);
}
