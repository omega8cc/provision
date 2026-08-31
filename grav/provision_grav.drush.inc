<?php
/**
 * @file
 * Grav 2 specific functions for the provisioning framework.
 *
 * Parallel of txp/provision_txp.drush.inc for Grav 2 platforms and SITE-CAPSULE
 * sites (boa-grav D-003: every Aegir Grav site is a complete, self-contained
 * Grav install at sites/<uri>/ inside a platform that is itself a pristine
 * admin-bundle release -- the seed each capsule is copied from). Every command
 * hook in this layer early-returns unless the context root is a Grav root
 * (provision_platform_is_grav(), defined once in the always-loaded
 * provision.inc); the Drupal-layer hooks carry a single added early-return on
 * the provision_platform_is_foreign_cms() union, so both commandfiles fire for
 * every command and the guards make them mutually exclusive.
 *
 * Grav is the mirror image of TXP: it ships a first-class CLI (bin/grav,
 * bin/gpm, bin/plugin -- the automation seam) but NO database at all in
 * phase 1. No drush bootstrap of the CMS ever happens (GravBoot exists only to
 * satisfy the bootstrap-phase gate); every Grav CLI call runs in a separate
 * enforced-PHP process as the instance user with umask 0002 (both spike-proven
 * requirements -- see boa-grav docs/spikes/2026-08-31-round-1.md).
 *
 * PHP floor (boa-grav D-005): Grav support requires PHP >= 8.3 at task runtime,
 * 8.4 preferred (the BOA default), ENFORCED -- no per-site PHP selection. THIS
 * file and the GravBoot class stay syntax-conservative because drush includes
 * them at commandfile discovery on every box the shared provision tree ships
 * to; the engines under grav/grav/ and the templates load only on guarded Grav
 * paths.
 */

// Templates render via eval() (Provision_Config::render_template), where
// __FILE__ no longer points at the template dir -- this constant, set at real
// include time, is how the templates find this layer on disk.
if (!defined('PROVISION_GRAV_DIR')) {
  define('PROVISION_GRAV_DIR', dirname(__FILE__));
}

/**
 * Implements hook_drush_init().
 */
function provision_grav_drush_init() {
  provision_grav_provision_register_autoload();
}

/**
 * Implements hook_bootstrap_candidates().
 *
 * The BackdropBoot/TxpBoot pattern: the boot class ships HERE (zero drush-fork
 * changes) and only satisfies the bootstrap-phase gate for provision task
 * commands on Grav roots -- no CMS code ever runs under drush.
 */
function provision_grav_bootstrap_candidates() {
  // The hook contract takes CLASS NAMES; drush instantiates and caches them
  // itself (an instance here fatals in drush_get_bootstrap_candidates()).
  require_once dirname(__FILE__) . '/Provision/GravBoot.php';
  return array('Provision_GravBoot');
}

/**
 * Register our directory as a place to find provision classes.
 */
function provision_grav_provision_register_autoload() {
  static $loaded = FALSE;
  if (!$loaded) {
    $loaded = TRUE;
    provision_autoload_register_prefix('Provision_', dirname(__FILE__));
  }
}

function provision_grav_drush_help($section) {
  switch ($section) {
    case 'error:PROVISION_GRAV_INSTALL_FAILED' :
      return dt('Could not complete the Grav site installation.');
    case 'error:PROVISION_GRAV_PHP_TOO_OLD' :
      return dt('No PHP >= 8.3 binary is available; Grav support requires it (8.4 preferred; enforced, no per-site PHP selection).');
    case 'error:PROVISION_GRAV_SEED_INVALID' :
      return dt('The platform root is not a usable Grav seed (vendor/autoload.php missing).');
    case 'error:PROVISION_GRAV_SITES_EXIST' :
      return dt('Grav sites still exist on this platform; delete them first.');
    case 'error:PROVISION_GRAV_CAPSULE_FAILED' :
      return dt('Could not materialise the Grav site capsule from the platform seed.');
  }
}

/**
 * Implements hook_drush_engine_type_info().
 */
function provision_grav_drush_engine_type_info() {
  return array('grav' => array());
}

function provision_grav_drush_engine_grav() {
  $engines = array();
  $engines['clear'] = array();
  $engines['install'] = array();
  $engines['packages'] = array();
  $engines['verify'] = array();
  return $engines;
}

/**
 * Implements hook_provision_config_load_templates().
 *
 * The zero-shared-edit vhost seam (the txp precedent): when the http service
 * renders a site vhost for a Grav platform, swap in the Grav templates
 * (per-site CAPSULE docroot at sites/<uri>/ + the Grav location contract
 * inline -- the shared nginx_vhost_common.conf is Drupal-shaped end to end and
 * raw-serves a Grav tree's internals). Exact-class match on purpose:
 * subclasses (subdir vhosts etc.) keep their own templates.
 */
function provision_grav_provision_config_load_templates($config) {
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
  $root = $config->root;
  if (!is_string($root) || $root === '' || !provision_platform_is_grav($root)) {
    return array();
  }
  $base = dirname(__FILE__) . '/templates';
  if ($class === 'Provision_Config_Nginx_Site') {
    return array($base . '/vhost_grav.tpl.php');
  }
  return array($base . '/vhost_ssl_grav.tpl.php');
}

/**
 * Pick the enforced PHP CLI binary for Grav (>= 8.3 floor, 8.4 preferred).
 *
 * No per-site or per-instance PHP selection for Grav (D-005 addendum,
 * enforced, unlike Drupal): the BOA default 8.4 first, then newer, then the
 * 8.3 floor, then the running backend CLI when it already satisfies the
 * floor. FALSE when the box cannot satisfy the floor -- callers refuse the
 * task.
 */
function _provision_grav_php_bin() {
  static $bin = NULL;
  if (!is_null($bin)) {
    return $bin;
  }
  $candidates = array('/opt/php84/bin/php', '/opt/php85/bin/php', '/opt/php83/bin/php');
  foreach ($candidates as $candidate) {
    if (is_executable($candidate)) {
      $bin = $candidate;
      return $bin;
    }
  }
  if (version_compare(PHP_VERSION, '8.3.0', '>=') && defined('PHP_BINARY') && PHP_BINARY) {
    $bin = PHP_BINARY;
    return $bin;
  }
  $bin = FALSE;
  return $bin;
}

/**
 * Does this Grav site capsule exist on disk? (The settings.php-probe analogue.)
 *
 * A capsule is a full install: its own system/defines.php is the existence
 * marker (same file the platform detector keys on, one level down).
 */
function _provision_grav_site_exists() {
  return is_readable(d()->site_path . '/system/defines.php');
}

/**
 * Resolve the Grav core version string (e.g. "2.0.21") for a given root.
 *
 * Regex over system/defines.php -- a codebase fact, no CMS bootstrap and no
 * modern-PHP requirement (never executes Grav code), valid for platform roots
 * and capsules alike.
 */
function _provision_grav_get_version($root = NULL) {
  if (is_null($root)) {
    $root = d()->root;
  }
  $version = '';
  $defines = $root . '/system/defines.php';
  if (is_readable($defines)) {
    $data = file_get_contents($defines);
    if (preg_match('/define\\s*\\(\\s*[\'"]GRAV_VERSION[\'"]\\s*,\\s*[\'"]([^\'"]+)[\'"]/', $data, $match)) {
      $version = $match[1];
    }
  }
  return $version;
}

/**
 * Find the Grav capsules on this platform: sites/<name>/system/defines.php.
 *
 * Feeds the platform verify 'sites' option AND the platform-delete refusal
 * (boa-grav R0: the shared guard is a settings.php glob and is EMPTY for Grav
 * -- without this, deleting a populated Grav platform would recursively
 * destroy every capsule). Returns uri => uri, the
 * provision_drupal_find_sites() shape.
 */
function provision_grav_find_sites($root = NULL) {
  if (is_null($root)) {
    $root = d()->root;
  }
  $sites = array();
  $markers = glob($root . '/sites/*/system/defines.php');
  if (is_array($markers)) {
    foreach ($markers as $marker) {
      $uri = basename(dirname(dirname($marker)));
      $sites[$uri] = $uri;
    }
  }
  return $sites;
}

/**
 * The Grav writable set, relative to a capsule root.
 *
 * Everything the web user writes at runtime; group-model perms
 * (<user>:<web_group> 02775 -- FPM writes via GROUP; never chown to the
 * per-version web user, spike-proven version-flip hazard).
 */
function _provision_grav_writable_dirs() {
  return array('user', 'cache', 'logs', 'tmp', 'backup', 'images', 'assets');
}

/**
 * Materialise (or repair) the site capsule from the platform seed.
 *
 * First run: copy the ENTIRE platform tree (a pristine admin-bundle release)
 * into sites/<uri>/, excluding sites/ itself (no recursion) and the seed's
 * own volatile dirs' contents. Every run: assert vendor/, enforce the
 * ownership model on the writable set, refresh the per-site .env, and keep
 * the capsule's modules/ dir (inert to Grav; the BOA per-site control-INI
 * home, and the nightly machinery mkdirs it anyway) in place.
 */
function _provision_grav_create_directories() {
  if (d()->type !== 'site') {
    return;
  }
  $site_path = d()->site_path;
  $root = d()->root;

  if (!is_dir($site_path . '/system')) {
    if (!is_readable($root . '/vendor/autoload.php')) {
      return drush_set_error('PROVISION_GRAV_SEED_INVALID',
        dt('Platform seed @root has no vendor/autoload.php; refusing to materialise @site (a CLI run on a vendor-less tree would trigger an unpinned network composer run).',
        array('@root' => $root, '@site' => $site_path)));
    }
    provision_file()->create_dir($site_path, dt('Grav site capsule'), 0755);
    // tar pipe copy: preserves modes/symlinks, excludes the sites/ tree (no
    // capsule recursion) and volatile seed state that must start empty.
    $excludes = '--exclude=./sites --exclude=./cache/* --exclude=./logs/* --exclude=./tmp/* --exclude=./backup/*';
    if (!drush_shell_exec('tar -C %s ' . $excludes . ' -cf - . | tar -C %s -xf -', $root, $site_path)) {
      return drush_set_error('PROVISION_GRAV_CAPSULE_FAILED',
        dt('Copying the platform seed @root into @site failed.',
        array('@root' => $root, '@site' => $site_path)));
    }
    if (!is_readable($site_path . '/vendor/autoload.php')) {
      return drush_set_error('PROVISION_GRAV_CAPSULE_FAILED',
        dt('Capsule @site has no vendor/autoload.php after the seed copy.', array('@site' => $site_path)));
    }
    drush_log(dt('Materialised Grav capsule @site from the platform seed (@version).',
      array('@site' => $site_path, '@version' => _provision_grav_get_version($root))), 'ok');
  }

  $web_group = d('@server_master')->web_group;

  // The BOA per-site control-INI dir (inert to Grav itself) + the writable set.
  $mkdirs = array($site_path . '/modules');
  foreach (_provision_grav_writable_dirs() as $dir) {
    $mkdirs[] = $site_path . '/' . $dir;
  }
  foreach ($mkdirs as $path) {
    if (!is_dir($path)) {
      provision_file()->mkdir($path)
        ->succeed('Created <code>@path</code>')
        ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
    }
  }

  // Ownership model (spike-proven): group = web group, 02775, recursive.
  // chmod AFTER chgrp (chgrp strips setgid).
  foreach (_provision_grav_writable_dirs() as $dir) {
    $path = $site_path . '/' . $dir;
    provision_file()->chgrp($path, $web_group, TRUE)
      ->succeed('Changed group ownership of <code>@path</code> to @gid')
      ->fail('Could not change group ownership of <code>@path</code> to @gid');
  }
  foreach (_provision_grav_writable_dirs() as $dir) {
    $path = $site_path . '/' . $dir;
    provision_file()->chmod($path, 02775, TRUE)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
  }

  _provision_grav_write_env();
}

/**
 * Write the per-site .env (Grav 2 native, loaded before any constant --
 * the settings.php analogue; boa-grav map par.3.1.3).
 *
 * Idempotent single authority: GRAV_ENVIRONMENT pinned to the site uri so the
 * environment:// stream never keys off a request hostname (aliases would
 * otherwise fork per-host config), plus a provenance header. Foreign root
 * files survive Grav core upgrades (spike-proven: the installer replaces only
 * entries the package ships).
 */
function _provision_grav_write_env() {
  $site_path = d()->site_path;
  $lines = array(
    '# Written by provision (hosting framework); do not edit -- re-rendered on verify.',
    'GRAV_ENVIRONMENT=' . d()->uri,
    '',
  );
  $data = implode("\n", $lines);
  $env = $site_path . '/.env';
  if (is_readable($env) && file_get_contents($env) === $data) {
    return;
  }
  provision_file()->file_put_contents($env, $data)
    ->succeed('Generated config file <code>@path</code>')
    ->fail('Could not generate config file <code>@path</code>');
}

/**
 * Run a Grav CLI command inside a capsule (the automation seam).
 *
 * Contract (spike-proven, boa-grav docs/spikes/2026-08-31-round-1.md): the
 * enforced PHP binary; cwd = the capsule; umask 0002 for the child (a 022
 * CLI-created dir loses group-write and fatals the next web save); vendor/
 * asserted first (a vendor-less tree turns any CLI entry into an unpinned
 * network composer run); ~/.grav asserted absent (it silently flips GPM into
 * symlink-from-local-checkout mode). Never root -- the backend already runs
 * as the instance user. $tool is 'grav', 'gpm' or 'plugin'; $args is ONE
 * pre-escaped argument string built by the caller with escapeshellarg().
 */
function _provision_grav_cli($tool, $args) {
  $site_path = d()->site_path;
  $bin = _provision_grav_php_bin();
  if ($bin === FALSE) {
    return drush_set_error('PROVISION_GRAV_PHP_TOO_OLD',
      dt('No PHP >= 8.3 binary found; Grav tasks refuse to run below the enforced floor.'));
  }
  if (!is_readable($site_path . '/vendor/autoload.php')) {
    return drush_set_error('PROVISION_GRAV_SEED_INVALID',
      dt('Capsule @site has no vendor/autoload.php; refusing to run the Grav CLI.', array('@site' => $site_path)));
  }
  $home = getenv('HOME');
  if ($home && is_dir($home . '/.grav')) {
    drush_log(dt('@home/.grav exists -- GPM would silently prefer symlink-from-local-checkout mode; remove it if package operations misbehave.',
      array('@home' => $home)), 'warning');
  }
  $old_umask = umask(02);
  $result = drush_shell_cd_and_exec($site_path, $bin . ' bin/' . $tool . ' ' . $args);
  umask($old_umask);
  if (!$result) {
    $output = implode("\n", drush_shell_exec_output());
    drush_log(dt('Grav CLI failed: bin/!tool !args -- !output',
      array('!tool' => $tool, '!args' => $args, '!output' => $output)), 'warning');
  }
  return $result;
}

/**
 * Create the first admin account by writing the account YAML directly.
 *
 * Deliberately NOT via `bin/plugin login new-user`: its password lands on the
 * process list (its own help warns so) and it prompts interactively on any
 * missing answer (spike trap). A pre-hashed user/accounts/<user>.yaml is the
 * documented account store; access covers classic admin, Admin2 (api.*) and
 * the site, matching --admin-type both. Returns the generated password so the
 * install task can surface it once (never persisted by this layer).
 */
function _provision_grav_create_admin_account($username, $email) {
  $site_path = d()->site_path;
  $accounts = $site_path . '/user/accounts';
  if (!is_dir($accounts)) {
    provision_file()->mkdir($accounts);
  }
  $password = provision_password();
  $hash = password_hash($password, PASSWORD_DEFAULT);
  if (!is_string($hash) || $hash === '') {
    return drush_set_error('PROVISION_GRAV_INSTALL_FAILED', dt('Could not hash the admin password.'));
  }
  $lines = array(
    'state: enabled',
    'email: ' . $email,
    'fullname: Site Administrator',
    'title: Administrator',
    'language: en',
    'hashed_password: ' . $hash,
    'access:',
    '  admin:',
    '    login: true',
    '    super: true',
    '  api:',
    '    login: true',
    '    super: true',
    '  site:',
    '    login: true',
    '',
  );
  $yaml = $accounts . '/' . $username . '.yaml';
  provision_file()->file_put_contents($yaml, implode("\n", $lines))
    ->succeed('Generated config file <code>@path</code>')
    ->fail('Could not generate config file <code>@path</code>');
  $web_group = d('@server_master')->web_group;
  provision_file()->chgrp($yaml, $web_group);
  provision_file()->chmod($yaml, 0640);
  return $password;
}

/**
 * Report the packages available on a Grav platform.
 *
 * Grav has no .info modules/profiles; the frontend contract still needs
 * packages['base'], packages['sites-all'] (dereferenced UNGUARDED by
 * hosting_platform.drush.inc -- always emit it) and one synthetic profile row
 * as the platform partition key (distinct short_name 'grav', never
 * 'standard': the profile IS what keeps Grav platforms out of the Drupal
 * platform list on the site-create form).
 */
function provision_grav_find_packages() {
  drush_include_engine('grav', 'packages');
  return _provision_grav_packages();
}

/**
 * The site-level package map (Grav: core version only, nothing enabled to map).
 */
function provision_grav_system_map() {
  drush_include_engine('grav', 'packages');
  return _provision_grav_packages();
}
