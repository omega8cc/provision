<?php
/**
 * @file
 * Textpattern (TXP) specific functions for the provisioning framework.
 *
 * Parallel of backdrop/provision_backdrop.drush.inc for Textpattern platforms
 * and sites. Every command hook in this layer early-returns unless the context
 * root is a TXP root (provision_platform_is_txp(), defined once in the
 * always-loaded provision.inc); the Drupal-layer hooks carry a single added
 * early-return on the provision_platform_is_foreign_cms() union, so both
 * commandfiles fire for every command and the guards make them mutually
 * exclusive.
 *
 * TXP has no Drupal lineage at all: no version integer for the Drupal layer's
 * gates (the whole reason this layer exists), no drush bootstrap class (nothing
 * here ever bootstraps the CMS through drush -- engines act on files and the
 * DB directly, and the install/upgrade shims run Textpattern's own libs in a
 * separate modern-PHP process), and no CMS cron (core needs none).
 *
 * PHP floor: TXP support requires PHP >= 8.4 at task runtime (enforced in the
 * task validate hooks). THIS file and the per-command hook include files stay
 * syntax-conservative because drush includes them at commandfile discovery on
 * every box the shared provision tree ships to; the engines under txp/txp/ and
 * the classes/templates load only on guarded TXP paths.
 */

// The admin side of every TXP multisite site is path-mapped on the site vhost
// (no admin subdomain): https://<uri>/<this path>/ -> sites/<uri>/admin/.
// Single URL segment only; the on-disk directory is always 'admin'.
if (!defined('PROVISION_TXP_ADMIN_PATH')) {
  define('PROVISION_TXP_ADMIN_PATH', 'txpadmin');
}

// Templates render via eval() (Provision_Config::render_template), where
// __FILE__ no longer points at the template dir -- this constant, set at real
// include time, is how the templates find this layer on disk.
if (!defined('PROVISION_TXP_DIR')) {
  define('PROVISION_TXP_DIR', dirname(__FILE__));
}

/**
 * Implements hook_drush_init().
 */
function provision_txp_drush_init() {
  provision_txp_provision_register_autoload();
}

/**
 * Implements hook_bootstrap_candidates().
 *
 * The BackdropBoot pattern: the boot class ships HERE (zero drush-fork
 * changes) and only satisfies the bootstrap-phase gate for provision task
 * commands on TXP roots -- no CMS code ever runs under drush.
 */
function provision_txp_bootstrap_candidates() {
  // The hook contract takes CLASS NAMES; drush instantiates and caches them
  // itself (an instance here fatals in drush_get_bootstrap_candidates()).
  require_once dirname(__FILE__) . '/Provision/TxpBoot.php';
  return array('Provision_TxpBoot');
}

/**
 * Register our directory as a place to find provision classes.
 */
function provision_txp_provision_register_autoload() {
  static $loaded = FALSE;
  if (!$loaded) {
    $loaded = TRUE;
    provision_autoload_register_prefix('Provision_', dirname(__FILE__));
  }
}

function provision_txp_drush_help($section) {
  switch ($section) {
    case 'error:PROVISION_TXP_INSTALL_FAILED' :
      return dt('Could not complete Textpattern installation.');
    case 'error:PROVISION_TXP_PHP_TOO_OLD' :
      return dt('No PHP >= 8.4 binary is available; TXP support requires it (enforced, no per-site PHP selection).');
    case 'error:PROVISION_TXP_SKELETON_MISSING' :
      return dt('The platform ships no sites/site1 multisite skeleton; cannot materialise the site tree.');
    case 'error:PROVISION_TXP_SITES_EXIST' :
      return dt('Textpattern sites still exist on this platform; delete them first.');
  }
}

/**
 * Implements hook_drush_engine_type_info().
 */
function provision_txp_drush_engine_type_info() {
  return array('txp' => array());
}

function provision_txp_drush_engine_txp() {
  $engines = array();
  $engines['clear'] = array();
  $engines['deploy'] = array();
  $engines['install'] = array();
  $engines['packages'] = array();
  $engines['verify'] = array();
  return $engines;
}

/**
 * Implements hook_provision_config_load_templates().
 *
 * The zero-shared-edit vhost seam: when the http service renders a site vhost
 * for a TXP platform, swap in the TXP templates (per-site docroot at
 * sites/<uri>/public + the TXP location contract inline -- the shared
 * nginx_vhost_common.conf is Drupal-shaped end to end and is NOT addable-to).
 * Exact-class match on purpose: subclasses (subdir vhosts etc.) keep their own
 * templates until they get a ruled TXP variant.
 */
function provision_txp_provision_config_load_templates($config) {
  if (!is_object($config)) {
    return array();
  }
  // Class check FIRST: Provision_Config::__get proxies unknown properties to
  // the context (and has no __isset -- isset() is always FALSE), so only read
  // ->root once we know this is a vhost config.
  $class = get_class($config);
  if ($class !== 'Provision_Config_Nginx_Site' && $class !== 'Provision_Config_Nginx_Ssl_Site') {
    return array();
  }
  // A hook-supplied template OUTRANKS the class's own selection
  // (Provision_Config::load_template() consults hooks before $this->template),
  // and Provision_Config_Http_Site::process() expresses "site disabled" by
  // swapping $this->template to the disabled vhost. Returning a template here
  // for a disabled site would therefore keep it SERVING. Stand aside.
  if (!$config->site_enabled) {
    return array();
  }
  $root = $config->root;
  if (!is_string($root) || $root === '' || !provision_platform_is_txp($root)) {
    return array();
  }
  $base = dirname(__FILE__) . '/templates';
  if ($class === 'Provision_Config_Nginx_Site') {
    return array($base . '/vhost_txp.tpl.php');
  }
  return array($base . '/vhost_ssl_txp.tpl.php');
}

/**
 * Pick the enforced PHP CLI binary for TXP shims (>= 8.4, D-008).
 *
 * No per-site or per-instance PHP selection for TXP (enforced, unlike Drupal):
 * the running backend CLI when it already satisfies the floor, else the newest
 * installed /opt/phpNN >= 8.4. FALSE when the box cannot satisfy the floor --
 * callers refuse the task.
 */
function _provision_txp_php_bin() {
  if (version_compare(PHP_VERSION, '8.4.0', '>=') && defined('PHP_BINARY') && PHP_BINARY) {
    return PHP_BINARY;
  }
  $candidates = array('/opt/php85/bin/php', '/opt/php84/bin/php');
  foreach ($candidates as $bin) {
    if (is_executable($bin)) {
      return $bin;
    }
  }
  return FALSE;
}

/**
 * Does this TXP site exist on disk? (The settings.php-probe analogue.)
 */
function _provision_txp_site_exists() {
  return is_readable(d()->site_path . '/private/config.php');
}

/**
 * Resolve the Textpattern core version string (e.g. "4.9.1").
 *
 * Parses $thisversion out of the shared core's lib/constants.php -- a codebase
 * fact, no DB and no CMS bootstrap needed, valid for platform contexts.
 */
function _provision_txp_get_version($root = NULL) {
  if (is_null($root)) {
    $root = d()->root;
  }
  $version = '';
  $constants = $root . '/textpattern/lib/constants.php';
  if (is_readable($constants)) {
    $data = file_get_contents($constants);
    if (preg_match("/\\\$thisversion\\s*=\\s*'([^']+)'/", $data, $match)) {
      $version = $match[1];
    }
  }
  return $version;
}

/**
 * Find the TXP sites on this platform: sites/<name>/private/config.php.
 *
 * The shipped sites/site1 skeleton carries only config-dist.php and never
 * matches. Returns uri => uri, the provision_drupal_find_sites() shape.
 */
function provision_txp_find_sites($root = NULL) {
  if (is_null($root)) {
    $root = d()->root;
  }
  $sites = array();
  $configs = glob($root . '/sites/*/private/config.php');
  if (is_array($configs)) {
    foreach ($configs as $config) {
      $dir = dirname(dirname($config));
      $uri = basename($dir);
      // Skip revert artefacts (<uri>.restore / <uri>.original): they carry a
      // full config.php but are NOT sites. Counting them would deadlock
      // platform delete and report phantom sites to the frontend; they are the
      // D-035 leftover subsystem's business, not ours. Symlinked site dirs are
      // skipped for the same reason the Drupal scan skips them.
      if (preg_match('/\.(restore|original)$/', $uri) || is_link($dir)) {
        continue;
      }
      $sites[$uri] = $uri;
    }
  }
  return $sites;
}

/**
 * Read the db credentials out of a persisted site drushrc.php into an array.
 *
 * Regex parse (no include, so the file's $_SERVER side effects never run).
 * Returns the db_* subset found, or an empty array. Used where the credentials
 * must NOT pass through the 'site' option context -- notably the deploy
 * rollback, where that context already holds the credentials of a database
 * that is about to be dropped.
 */
function _provision_txp_drushrc_creds($drushrc) {
  $creds = array();
  if (!is_readable($drushrc)) {
    return $creds;
  }
  $body = file_get_contents($drushrc);
  if ($body === FALSE) {
    return $creds;
  }
  foreach (array('db_type', 'db_user', 'db_passwd', 'db_host', 'db_name', 'db_port') as $key) {
    if (preg_match('/\\$options\\s*\\[\\s*[\'"]' . $key . '[\'"]\\s*\\]\\s*=\\s*[\'"]([^\'"]*)[\'"]/', $body, $m)) {
      $creds[$key] = $m[1];
    }
  }
  return $creds;
}

/**
 * Generate the per-site private/config.php from the settings template.
 */
function _provision_txp_create_settings_file() {
  $config = new Provision_Config_Txp_Settings(d()->name, drush_get_context('site'));
  $config->write();
}

/**
 * Reload persisted site options (db credentials + installed flag).
 *
 * Same problem and same cure as the Backdrop layer: the db creds enter the
 * 'site' context only at install; later tasks must reload them from the
 * persisted site drushrc.php (regex, no include) before any re-render, or the
 * config.php re-render blanks the DB connection. Namespaced copy rather than a
 * cross-commandfile call so the layers stay independent.
 */
function _provision_txp_ensure_db_creds_loaded() {
  if (d()->type !== 'site') {
    return;
  }
  if (drush_get_option('db_name', '', 'site') && drush_get_option('db_passwd', '', 'site')) {
    return;
  }
  $drushrc = d()->site_path . '/drushrc.php';
  if (!is_readable($drushrc)) {
    return;
  }
  $body = file_get_contents($drushrc);
  if ($body === FALSE) {
    return;
  }
  foreach (array('db_type', 'db_user', 'db_passwd', 'db_host', 'db_name', 'db_port') as $key) {
    if (drush_get_option($key, '', 'site')) {
      continue;
    }
    if (preg_match('/\\$options\\s*\\[\\s*[\'"]' . $key . '[\'"]\\s*\\]\\s*=\\s*[\'"]([^\'"]*)[\'"]/', $body, $m)) {
      drush_set_option($key, $m[1], 'site');
    }
  }
  if (!drush_get_option('installed')
    && preg_match('/\\$options\\s*\\[\\s*[\'"]installed[\'"]\\s*\\]\\s*=\\s*(TRUE|true|1)\\s*;/', $body)) {
    drush_set_option('installed', TRUE, 'site');
  }
}

/**
 * Materialise (or repair) the site tree: sites/<uri>/{admin,private,public,tmp}.
 *
 * Copies the platform's shipped sites/site1 skeleton on first run (symlinks
 * preserved -- they are load-bearing), then enforces the ownership model every
 * run: FPM-writable = public/{files,images,themes} + admin/plugins + tmp
 * (group web, 02775); private/ is the credential store (group web for
 * traverse, 0750, config.php itself 0440); everything else stays read-only to
 * the web group. Also recreates the 4 admin symlinks and refreshes the 6-file
 * copy set (upstream's txp-multisite-sync.sh contract) -- the FPM pools all
 * disable symlink(), so the provisioner is the only repair path. A dir found
 * where a symlink belongs (a dereferenced restore) is rebuilt.
 */
function _provision_txp_create_directories() {
  if (d()->type !== 'site') {
    return;
  }
  $site_path = d()->site_path;
  $root = d()->root;

  if (!is_dir($site_path)) {
    $skeleton = $root . '/sites/site1';
    if (!is_dir($skeleton)) {
      return drush_set_error('PROVISION_TXP_SKELETON_MISSING',
        dt('No @skel on this platform; cannot materialise @site.',
        array('@skel' => $skeleton, '@site' => $site_path)));
    }
    // cp -a preserves the relative admin symlinks; provision_file copy would
    // dereference them.
    drush_shell_exec('cp -a %s %s', $skeleton, $site_path);
    if (!is_dir($site_path)) {
      return drush_set_error('PROVISION_TXP_SKELETON_MISSING',
        dt('Copying @skel to @site failed.', array('@skel' => $skeleton, '@site' => $site_path)));
    }
    drush_log(dt('Materialised TXP site tree @site from the platform skeleton.', array('@site' => $site_path)), 'ok');
  }

  $web_group = d('@server_master')->web_group;

  // The per-site tmp dir (the shared textpattern/tmp must never be used) and
  // the BOA per-site control-INI dir (inert to TXP; keeps the fleet INI
  // machinery uniform).
  $mkdirs = array(
    $site_path . '/tmp',
    $site_path . '/modules',
    $site_path . '/public/files',
    $site_path . '/public/images',
    $site_path . '/public/images/thumb',
    $site_path . '/public/themes',
    $site_path . '/admin/plugins',
  );
  foreach ($mkdirs as $path) {
    if (!is_dir($path)) {
      provision_file()->mkdir($path)
        ->succeed('Created <code>@path</code>')
        ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
    }
  }

  // The 4 load-bearing admin symlinks (relative, platform-depth-correct).
  $links = array(
    'vendors' => '../../../textpattern/vendors',
    'admin-themes' => '../../../textpattern/admin-themes',
    'preview.css' => '../../../textpattern/preview.css',
    'textpattern.css' => '../../../textpattern/textpattern.css',
  );
  foreach ($links as $name => $target) {
    $path = $site_path . '/admin/' . $name;
    if (is_dir($path) && !is_link($path)) {
      // A dereferenced restore baked a real copy; rebuild the link.
      _provision_recursive_delete($path);
      drush_log(dt('Removed dereferenced copy at @path (symlink expected).', array('@path' => $path)), 'warning');
    }
    elseif (is_file($path) && !is_link($path)) {
      provision_file()->unlink($path);
    }
    if (!is_link($path)) {
      provision_file()->symlink($target, $path)
        ->succeed('Created symlink @path to @target')
        ->fail('Could not create symlink @path to @target');
    }
  }

  // The 6-file copy set (upstream .github/txp-multisite-sync.sh): refresh from
  // the shared core so a core bump propagates. public/index.php and
  // admin/index.php are the site's OWN files (from the skeleton), never synced.
  $copies = array(
    '/admin/textpattern.js' => '/textpattern/textpattern.js',
    '/public/css.php' => '/css.php',
  );
  foreach ($copies as $dest => $src) {
    if (is_readable($root . $src)) {
      $current = is_readable($site_path . $dest) ? md5_file($site_path . $dest) : '';
      if ($current !== md5_file($root . $src)) {
        provision_file()->copy($root . $src, $site_path . $dest)
          ->succeed('Refreshed <code>@path</code> from the shared core')
          ->fail('Could not refresh <code>@path</code>');
      }
    }
  }

  // Ownership model. chmod AFTER chgrp (chgrp strips setgid).
  $writable = array(
    $site_path . '/tmp',
    $site_path . '/modules',
    $site_path . '/public/files',
    $site_path . '/public/images',
    $site_path . '/public/images/thumb',
    $site_path . '/public/themes',
    $site_path . '/admin/plugins',
  );
  foreach ($writable as $path) {
    provision_file()->chgrp($path, $web_group)
      ->succeed('Changed group ownership of <code>@path</code> to @gid')
      ->fail('Could not change group ownership of <code>@path</code> to @gid');
  }
  foreach ($writable as $path) {
    provision_file()->chmod($path, 02775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
  }
  $private = $site_path . '/private';
  provision_file()->chgrp($private, $web_group)
    ->succeed('Changed group ownership of <code>@path</code> to @gid')
    ->fail('Could not change group ownership of <code>@path</code> to @gid');
  provision_file()->chmod($private, 0750)
    ->succeed('Changed permissions of <code>@path</code> to @perm')
    ->fail('Could not change permissions of <code>@path</code> to @perm');
}

/**
 * Report the packages available on a TXP platform.
 *
 * TXP has no .info modules/profiles; the frontend contract still needs
 * packages['base'], packages['sites-all'] (dereferenced UNGUARDED by
 * hosting_platform.drush.inc -- always emit it) and one synthetic profile row
 * as the platform partition key (distinct short_name 'textpattern', never
 * 'standard': the profile IS what keeps TXP platforms out of the Drupal
 * platform list on the site-create form).
 */
function provision_txp_find_packages() {
  drush_include_engine('txp', 'packages');
  return _provision_txp_packages();
}

/**
 * The site-level package map (TXP: core version only, nothing enabled to map).
 */
function provision_txp_system_map() {
  drush_include_engine('txp', 'packages');
  return _provision_txp_packages();
}

/**
 * Mint a TXP password-reset confirmation link for the admin user via the
 * token shim. Returns the full https URL, or FALSE. Lives in the shell (not
 * the reset.login hook file) because the install engine reuses it for the
 * welcome mail.
 */
function _provision_txp_mint_login_link() {
  $php_bin = _provision_txp_php_bin();
  if ($php_bin === FALSE) {
    return FALSE;
  }
  $shim = dirname(__FILE__) . '/txp/resources/txp-token-shim.php';
  $config_php = d()->site_path . '/private/config.php';
  if (!is_readable($config_php)) {
    return FALSE;
  }
  $txpath = d()->root . '/textpattern';
  $result = drush_shell_exec('%s %s --txpath=%s --config-php=%s --user=%s',
    $php_bin, $shim, $txpath, $config_php, 'admin');
  $output = (array) drush_shell_exec_output();
  if (!$result || empty($output)) {
    drush_log(dt('Token shim failed: !out', array('!out' => implode("\n", $output))), 'warning');
    return FALSE;
  }
  // The shim prints exactly one line: CONFIRM=<token>
  $confirm = '';
  foreach ($output as $line) {
    if (strpos($line, 'CONFIRM=') === 0) {
      $confirm = substr($line, 8);
    }
  }
  if ($confirm === '') {
    drush_log(dt('Token shim returned no confirmation code: !out', array('!out' => implode("\n", $output))), 'warning');
    return FALSE;
  }
  return 'https://' . d()->uri . '/' . PROVISION_TXP_ADMIN_PATH . '/index.php?lang=en-gb&confirm=' . rawurlencode($confirm);
}

/**
 * Compose + best-effort send the welcome mail (D-006: box-level MTA/relay
 * only; on boxes without a relay this logs and moves on). Content per the
 * map OQ-6 proposal: site URL, admin URL, username and a password-SET link —
 * never a plaintext password.
 */
function _provision_txp_send_welcome_mail($client_email, $login_link) {
  if (!$client_email || !filter_var($client_email, FILTER_VALIDATE_EMAIL)) {
    drush_log(dt('No valid client email; welcome mail skipped.'), 'notice');
    return;
  }
  // Header-injection hygiene: every value that reaches a header line is
  // CR/LF-stripped, not just the (already FILTER_VALIDATE_EMAIL'd) recipient.
  $uri = str_replace(array("\r", "\n"), '', d()->uri);
  $host = str_replace(array("\r", "\n"), '', php_uname('n'));
  $subject = '[' . $uri . '] ' . dt('Your new Textpattern site');
  $body = dt("Your new Textpattern site is ready.") . "\n\n"
    . dt("Site: ") . 'https://' . $uri . '/' . "\n"
    . dt("Admin: ") . 'https://' . $uri . '/' . PROVISION_TXP_ADMIN_PATH . '/' . "\n"
    . dt("Username: ") . 'admin' . "\n\n"
    . dt("Set your password here (the link expires):") . "\n"
    . $login_link . "\n";
  $headers = 'From: noreply@' . $host;
  if (@mail($client_email, $subject, $body, $headers)) {
    drush_log(dt('Welcome mail sent to @to.', array('@to' => $client_email)), 'ok');
  }
  else {
    drush_log(dt('Welcome mail could not be sent (no MTA/relay on this box?) — the login link is recorded on the task instead.'), 'notice');
  }
}
