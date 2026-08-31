<?php
/**
 * @file
 * Mint a Textpattern password-reset confirmation token headlessly.
 *
 * Bootstraps the same minimal lib set as the install shim, then drives TXP's
 * OWN Security\Token machinery (never a hand-replicated hash): reads the
 * user's stored pass hash + nonce, generates a password_reset token row
 * (which TXP writes to txp_token itself), and prints CONFIRM=<token+selector>
 * — the value the admin's ?confirm= endpoint expects.
 *
 * Runs under PHP >= 8.4 (enforced). umask 0002 like the install shim: any
 * dir TXP code creates under the writable trees must keep group-write.
 */

if (@php_sapi_name() != 'cli') {
  exit(1);
}
if (version_compare(PHP_VERSION, '8.4.0', '<')) {
  fwrite(STDERR, "[ERROR]\tTXP token shim requires PHP >= 8.4\n");
  exit(128);
}
umask(0002);

$params = getopt('', array('txpath:', 'config-php:', 'user:'));
$txpath_arg = $params['txpath'] ?? '';
$config_php = $params['config-php'] ?? '';
$user = $params['user'] ?? 'admin';
if ($txpath_arg === '' || !is_dir($txpath_arg) || !is_readable($config_php)) {
  fwrite(STDERR, "[ERROR]\tusage: --txpath=<dir> --config-php=<file> [--user=admin]\n");
  exit(128);
}

define('txpinterface', 'admin');
define('txpath', $txpath_arg);
define('MSG_OK', '[OK]');
define('MSG_ALERT', '[WARNING]');
define('MSG_ERROR', '[ERROR]');

error_reporting(E_ALL);
@ini_set('display_errors', '1');

include_once txpath . '/lib/class.trace.php';
$trace = new Trace();
include_once txpath . '/lib/constants.php';
include_once txpath . '/lib/txplib_misc.php';
include_once txpath . '/vendors/Textpattern/Loader.php';
$loader = new \Textpattern\Loader(txpath . '/vendors');
$loader->register();
$loader = new \Textpattern\Loader(txpath . '/lib');
$loader->register();

// $txpcfg at global scope, then the DB layer (connects at include time).
include $config_php;
include_once txpath . '/lib/txplib_db.php';

$safe_user = doSlash($user);
$row = safe_row('user_id, pass, nonce', 'txp_users', "name = '" . $safe_user . "'");
if (!$row) {
  fwrite(STDERR, "[ERROR]\tno such user: {$user}\n");
  exit(128);
}

if (!defined('RESET_EXPIRY_MINUTES')) {
  define('RESET_EXPIRY_MINUTES', 90);
}
$expiry = time() + (60 * RESET_EXPIRY_MINUTES);

$token = \Txp::get('\Textpattern\Security\Token')->generate(
  $row['user_id'], 'password_reset', $expiry, $row['pass'], $row['nonce']
);
if (!$token) {
  fwrite(STDERR, "[ERROR]\ttoken generation failed\n");
  exit(128);
}
echo 'CONFIRM=' . $token . "\n";
exit(0);
