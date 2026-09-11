<?php
/**
 * @file
 * Headless runner for Textpattern's own update chain (Aegir/BOA provision shim).
 *
 * Textpattern has no CLI and no updater entry point: the chain in
 * textpattern/update/_update.php runs only as a side effect of an authenticated
 * admin request (textpattern/index.php reaches it once the site is bootstrapped
 * and the recorded version differs from the codebase's). This shim reproduces
 * exactly that bootstrap, in the order index.php uses, and then includes the
 * chain -- so the upgrade runs the CMS's OWN code, never a re-implementation of
 * it, without a web request and without an admin session.
 *
 * It is deliberately a fork of txp-install-shim.php rather than a branch of it:
 * the install shim consumes setup/setup_lib.php and must NOT be loaded here (a
 * loaded setup changes what _update.php's removeFiles() step is pointed at),
 * and the two bootstraps diverge in the includes they need.
 *
 * Three things make this safe to run headless, all of them properties of the
 * chain rather than of this file:
 *  - _update.php's own try/catch is SILENT (its comment says so): a failed step
 *    aborts the remaining steps and communicates only through the error handler
 *    it installed. So every byte the chain emits is captured here and ANY
 *    output at all is treated as failure.
 *  - The language loop that follows the try block runs while
 *    updateErrorHandler() is still installed, and that handler throws on ANY
 *    diagnostic -- a notice from a language pack aborts the run AFTER the
 *    schema work but BEFORE the version pref is rewritten. That is a real
 *    outcome, not a crash: the caller reconciles the version row itself once it
 *    has proved the schema, and this shim's machine-readable tail is what tells
 *    it which case it is in. The throw only happens when the handler can pick
 *    an output format, which is why an Accept header is announced below.
 *  - The step selection compares the DATABASE's version, so the chain is a
 *    no-op on a site already at the codebase version.
 *
 * Runs under PHP >= 8.4 (enforced by the caller; re-checked here). ONE process
 * per site: the bootstrap defines process-global constants.
 * Exit codes: 0 success, 128 refused/failed (mirrors setup.php).
 */

if (@php_sapi_name() != 'cli') {
    exit(1);
}
if (version_compare(PHP_VERSION, '8.4.0', '<')) {
    fwrite(STDERR, "[ERROR]\tTXP update shim requires PHP >= 8.4, got " . PHP_VERSION . "\n");
    exit(128);
}

// Group-write survival (live-proven on the install shim): the shim runs as the
// instance user (umask 022 default) and the chain writes under the writable
// trees (language packs, theme assets) -- without 0002 those files lose g+w and
// the web user's later writes fatal.
umask(0002);

$params = getopt('', array('config-php:', 'txpath:', 'uri:'));
$config_php = isset($params['config-php']) ? (string) $params['config-php'] : '';
$txpath_arg = isset($params['txpath']) ? (string) $params['txpath'] : '';
$uri_arg = isset($params['uri']) ? (string) $params['uri'] : '';

if ($config_php === '' || $txpath_arg === '' || $uri_arg === '') {
    fwrite(STDERR, "[ERROR]\tusage: --config-php=<file> --txpath=<dir> --uri=<uri>\n");
    exit(128);
}
if (!is_readable($config_php)) {
    fwrite(STDERR, "[ERROR]\tconfig.php not readable: {$config_php}\n");
    exit(128);
}
if (!is_dir($txpath_arg) || !is_readable($txpath_arg . '/update/_update.php')) {
    fwrite(STDERR, "[ERROR]\tno update chain under txpath: {$txpath_arg}\n");
    exit(128);
}

$site_dir = dirname(dirname($config_php));
$site_real = realpath($site_dir);

define('txpinterface', 'admin');
define('txpath', $txpath_arg);
define('MSG_OK', '[OK]');
define('MSG_ALERT', '[WARNING]');
define('MSG_ERROR', '[ERROR]');

error_reporting(E_ALL);
@ini_set('display_errors', '1');

// The chain's error handler (updateErrorHandler) hands the diagnostic to
// adminErrorHandler and throws afterwards -- but adminErrorHandler only ever
// RETURNS when it can pick an output format, and it picks one by asking
// http_accept_format(), i.e. by reading HTTP_ACCEPT. Under CLI that header does
// not exist, no format matches, and adminErrorHandler ends in txp_die(): the
// process dies inside the handler, the throw never happens, and the caller gets
// no verdict at all. Announcing HTML restores the control flow this shim is
// written for -- the diagnostic is echoed into the buffer below and the throw
// reaches the catch. Set before any include: the function caches the parsed
// header in a static on its first call.
if (!isset($_SERVER['HTTP_ACCEPT']) || $_SERVER['HTTP_ACCEPT'] === '') {
    $_SERVER['HTTP_ACCEPT'] = 'text/html';
}

// $txpcfg must land at GLOBAL scope: txplib_db.php reads it at include time
// (PFX and the immediate connection), and _update.php reads
// $txpcfg['multisite_root_path'] to decide which setup directory to remove.
include $config_php;

if (!isset($txpcfg) || !is_array($txpcfg)) {
    fwrite(STDERR, "[ERROR]\tconfig.php defined no \$txpcfg\n");
    exit(128);
}

// Re-assert txpath: the site's config.php carries its own value and a mismatch
// means the site is not on the core this run was pointed at.
$cfg_txpath = isset($txpcfg['txpath']) ? realpath($txpcfg['txpath']) : false;
if ($cfg_txpath === false || $cfg_txpath !== realpath($txpath_arg)) {
    fwrite(STDERR, "[ERROR]\tconfig.php txpath does not resolve to {$txpath_arg}\n");
    exit(128);
}

// The multisite assertion. _update.php's last file-side act is
// removeFiles($txpcfg['multisite_root_path'] . '/admin', 'setup') -- and when
// the key is absent it removes the SHARED core's setup directory instead,
// taking it away from every other site on the platform. Refuse anything but a
// multisite_root_path that resolves to this site's own directory.
$cfg_root = isset($txpcfg['multisite_root_path']) ? realpath($txpcfg['multisite_root_path']) : false;
if ($site_real === false || $cfg_root === false || $cfg_root !== $site_real) {
    fwrite(STDERR, "[ERROR]\tmultisite_root_path does not resolve to the site directory {$site_dir}\n");
    exit(128);
}

// Third-party post-update scripts are a real include glob at the end of the
// chain and are outside what this upgrade can vouch for.
$custom = glob(txpath . '/update/custom/post-update*.php');
if (is_array($custom) && !empty($custom)) {
    fwrite(STDERR, "[ERROR]\tthird-party post-update scripts present: " . implode(', ', array_map('basename', $custom)) . "\n");
    exit(128);
}

// The bootstrap, in textpattern/index.php's proven order. $trace exists before
// anything that may reference it; the two Loaders are registered before
// txplib_db.php so the chain's Txp::get() calls resolve.
include txpath . '/lib/class.trace.php';
$trace = new Trace();
include_once txpath . '/lib/constants.php';
include txpath . '/lib/txplib_misc.php';
include txpath . '/lib/txplib_admin.php';

include txpath . '/vendors/Textpattern/Loader.php';
$loader = new \Textpattern\Loader(txpath . '/vendors');
$loader->register();
$loader = new \Textpattern\Loader(txpath . '/lib');
$loader->register();

include txpath . '/lib/txplib_db.php';
include txpath . '/lib/txplib_forms.php';
include txpath . '/lib/txplib_html.php';
include txpath . '/lib/admin_config.php';

if (empty($connected)) {
    fwrite(STDERR, "[ERROR]\tcould not connect to the site database\n");
    exit(128);
}
if (!numRows(safe_query("SHOW TABLES LIKE '" . PFX . "textpattern'"))) {
    fwrite(STDERR, "[ERROR]\tthe database carries no Textpattern schema\n");
    exit(128);
}

// Global site preferences, exactly as index.php loads them. extract() is what
// puts $version, $language, $path_to_site, $img_dir and $dbupdatetime into
// scope for the chain; $prefs itself stays global for get_pref().
$prefs = get_prefs();
extract($prefs);

$version_before = isset($version) ? (string) $version : '';

// The two constants the chain's language loop and its callees expect from a
// real admin request.
if (!defined('LANG')) {
    define('LANG', isset($language) ? $language : TEXTPATTERN_DEFAULT_LANG);
}
if (!defined('IMPATH')) {
    define('IMPATH', (isset($path_to_site) ? $path_to_site : dirname(txpath)) . DS . (isset($img_dir) ? $img_dir : 'images') . DS);
}

// The chain's own globals. $dbversion is what the step selection compares;
// $event/$step/$app_mode stand in for the request variables index.php derives
// from gps(); $production_status is forced live so a diagnostic never prints
// trace or SQL detail into the captured output.
$dbversion = $version_before;
$event = 'diag';
$step = '';
$app_mode = '';
$production_status = 'live';

// $txp_user is a real account, never a synthetic one: _to_4.2.0 stamps author
// columns with it, and a name with no txp_users row would write dangling
// authorship into content. The lowest-numbered publisher (privs = 1) is the
// site's first administrator.
$txp_user = safe_field('name', 'txp_users', "privs = 1 ORDER BY user_id LIMIT 1");
if (empty($txp_user)) {
    fwrite(STDERR, "[ERROR]\tno publisher account (txp_users privs = 1) to run the update chain as\n");
    exit(128);
}

/**
 * Read one marker pref straight from the still-open connection.
 *
 * The die path has no in-process globals worth trusting (the chain rewrites the
 * rows and then the process ends inside someone else's handler), so the two
 * markers the caller reconciles on are re-read from the database. A diagnostic
 * raised while reading must not reach the chain's error handler, which throws:
 * a swallowing handler holds the door for the duration.
 */
function txp_update_shim_marker($name)
{
    if (empty($GLOBALS['connected']) || !function_exists('safe_field')) {
        return '';
    }
    $name = preg_replace('/[^a-z_]/', '', $name);
    set_error_handler(function () {
        return true;
    });
    $value = '';
    try {
        $value = (string) safe_field('val', 'txp_prefs', "name = '" . $name . "' AND user_name = ''");
    } catch (\Throwable $e) {
        $value = '';
    }
    restore_error_handler();

    return $value;
}

// A fatal inside the chain would otherwise exit silently with the output buffer
// discarded; this hands the caller the same machine-readable verdict every
// other exit path produces -- including the two markers, re-read from the
// database, because a chain that died after its schema work still leaves a
// reconcilable state and reporting it as empty would throw that copy away.
$txp_update_done = false;
register_shutdown_function(function () use (&$txp_update_done) {
    if ($txp_update_done) {
        return;
    }
    $buffered = '';
    while (ob_get_level() > 0) {
        $buffered .= (string) ob_get_clean();
    }
    $error = error_get_last();
    if ($buffered !== '') {
        fwrite(STDERR, "[ERROR]\tchain output before the fatal:\n" . $buffered . "\n");
    }
    fwrite(STDERR, "[ERROR]\tthe update chain terminated: "
        . ($error === null ? 'unknown fatal' : $error['message'] . ' in ' . $error['file'] . ':' . $error['line']) . "\n");
    echo "THROWN=fatal\n";
    echo "VERSION_BEFORE=" . (isset($GLOBALS['version_before']) ? $GLOBALS['version_before'] : '') . "\n";
    echo "VERSION_AFTER=" . txp_update_shim_marker('version') . "\n";
    echo "DBUPDATETIME=" . txp_update_shim_marker('dbupdatetime') . "\n";
    echo "OUTPUT_BYTES=" . strlen($buffered) . "\n";
    exit(128);
});

define('TXP_UPDATE', 1);
$thrown = '';
ob_start();
try {
    include txpath . '/update/_update.php';
}
catch (\Throwable $e) {
    $thrown = get_class($e) . ': ' . $e->getMessage();
}
$captured = (string) ob_get_clean();
$txp_update_done = true;

// Re-read both markers from the database rather than from the in-process
// globals: the chain rewrites the rows, and what the NEXT admin request will
// see is what the caller must be told.
$version_after = (string) safe_field('val', 'txp_prefs', "name = 'version' AND user_name = ''");
$dbupdatetime = (string) safe_field('val', 'txp_prefs', "name = 'dbupdatetime' AND user_name = ''");

if ($captured !== '') {
    fwrite(STDERR, "[ERROR]\tthe update chain produced output (any output is a diagnostic):\n" . strip_tags($captured) . "\n");
}
if ($thrown !== '') {
    fwrite(STDERR, "[ERROR]\tthe update chain threw: " . $thrown . "\n");
}

echo "THROWN=" . ($thrown === '' ? 'no' : 'yes') . "\n";
echo "VERSION_BEFORE=" . $version_before . "\n";
echo "VERSION_AFTER=" . $version_after . "\n";
echo "DBUPDATETIME=" . $dbupdatetime . "\n";
echo "OUTPUT_BYTES=" . strlen($captured) . "\n";

exit(($captured === '' && $thrown === '') ? 0 : 128);
