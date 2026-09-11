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
 * Implements hook_drush_command().
 *
 * The mirror-fed Grav upgrade, copy-upgrade, rename and cutover commands.
 * Registered here (the owned Grav layer), not in provision.drush.inc, so the
 * whole feature stays additive. Hook functions live in the reversed-dotted
 * include Drush resolves for each command name (provision-grav-upgrade =>
 * upgrade.grav.provision.inc, provision-grav-copy-upgrade =>
 * upgrade.copy.grav.provision.inc, and so on); Drush collapses the
 * commandfile prefix out of the validate/command/rollback hook names because
 * the command name starts with it, while the pre/post variations keep the
 * doubled form.
 */
function provision_grav_drush_command() {
  $items = array();
  $items['provision-grav-upgrade'] = array(
    'description' => 'Upgrade a Grav site capsule in place to a staged official admin-bundle release (mirror-fed, D-004/D-006; safety backup first; user content, accounts and configuration survive).',
    'arguments' => array(
      'artefact' => dt('Optional path to the official grav-admin-v*.zip. Default: the newest one staged under <aegir_root>/static/grav-releases/.'),
    ),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @gravsite provision-grav-upgrade' => 'Upgrade the capsule to the newest staged official release.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );
  $items['provision-grav-copy-upgrade'] = array(
    'description' => 'Upgrade a COPY of a Grav site: copy the capsule to a NEW uri on a target Grav platform, upgrade the copy to a staged official release, and leave the source serving untouched.',
    'arguments' => array(
      '@new_site' => dt('The Drush alias of the copy (a new, unused uri).'),
      '@platform_name' => dt('The Drush alias of the target Grav platform.'),
    ),
    'options' => array(
      'new_db_server' => dt('Ignored for Grav: a capsule has no database and the copy keeps the source\'s "none" server. Accepted so the frontend dialog can pass its usual option set.'),
      'artefact' => dt('Optional path to the official grav-admin-v*.zip. Default: the newest one staged under <aegir_root>/static/grav-releases/.'),
    ),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @gravsite provision-grav-copy-upgrade @newsite @platform_grav' => 'Copy the capsule onto the target platform under the new uri and upgrade the copy.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );
  $items['provision-grav-rename'] = array(
    'description' => 'Rename a Grav site on its own platform: move the capsule to the new uri, re-point the uri-bound surfaces, render the new vhost and retire the old context. Identity (keys, accounts, content) is preserved.',
    'arguments' => array(
      '@new_site' => dt('The Drush alias of the new name (a new, unused uri on this platform).'),
    ),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @gravsite provision-grav-rename @newname' => 'Move the capsule to the new uri on the same platform.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );
  $items['provision-grav-cutover'] = array(
    'description' => 'Make this Grav site answer at a domain currently held by another site (typically its upgrade source): retire that site (backup first), then rename this site to the freed domain. Runs on the upgraded copy; hook functions in cutover.grav.provision.inc.',
    'arguments' => array(
      'target_domain' => dt('The domain to take over (usually the upgrade source\'s original domain).'),
    ),
    'options' => array(
      'cutover_source' => dt('Context name of the site currently holding the domain (resolved by the frontend at execution time; omitted when the domain is free).'),
      'cutover_platform_nid' => dt('Frontend node id of this site\'s platform (carried for parity with the other cutover commands and checked as a frontend/backend agreement).'),
      'cutover_db_server_nid' => dt('Frontend node id of this site\'s db server (same).'),
    ),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @copy.example.com provision-grav-cutover example.com' => 'Retire the site at example.com (backed up first) and rename this Grav site to it.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );
  return $items;
}

/**
 * Resolve the newest staged official admin-bundle artefact.
 *
 * The BOA channel (boa-grav D-004: staticbuild -> mirrors -> box) stages the
 * official grav-admin-v*.zip files under <aegir_root>/static/grav-releases/;
 * newest by version sort wins. Returns FALSE when nothing is staged.
 */
function _provision_grav_newest_artefact() {
  $dir = d('@server_master')->aegir_root . '/static/grav-releases';
  $candidates = glob($dir . '/grav-admin-v*.zip');
  if (!is_array($candidates) || empty($candidates)) {
    return FALSE;
  }
  usort($candidates, 'version_compare');
  return end($candidates);
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
    case 'error:PROVISION_GRAV_NO_ARTEFACT' :
      return dt('No readable official grav-admin-v*.zip was staged or named.');
    case 'error:PROVISION_GRAV_ARTEFACT_UNREADABLE' :
      return dt('The staged artefact\'s own GRAV_VERSION could not be read; an upgrade without it has no verdict.');
    case 'error:PROVISION_GRAV_ARTEFACT_MISMATCH' :
      return dt('The staged artefact\'s file name and its GRAV_VERSION disagree.');
    case 'error:PROVISION_GRAV_ARTEFACT_NOT_NEWER' :
      return dt('The staged artefact is not newer than the capsule; nothing to upgrade to.');
    case 'error:PROVISION_GRAV_UPGRADE_FAILED' :
      return dt('The Grav upgrade did not take; the pre-upgrade safety copy is restorable.');
    case 'error:PROVISION_GRAV_RENAME_FAILED' :
      return dt('Could not rename the Grav site.');
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
  $engines['deploy'] = array();
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
  // A disabled site must render the CMS-agnostic disabled vhost: hook
  // candidates outrank the class's own template swap in load_template(), so
  // returning ours here would keep a disabled Grav site LIVE (proven on the
  // first disable leg). Fall through to the stock machinery.
  if (!$config->site_enabled) {
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
      $capsule = dirname(dirname($marker));
      $uri = basename($capsule);
      // Deploy/restore leftovers (<uri>.restore, <uri>.original and the
      // parked -stale-extract shapes) are full Grav trees too; they are
      // transient debris, never sites. A symlinked entry is not a capsule
      // either: both Grav upgrade paths refuse symlinked trees (D-003), so
      // counting one would register a site every task must then choke on.
      if (strpos($uri, '.restore') !== FALSE
        || strpos($uri, '.original') !== FALSE
        || is_link($capsule)) {
        continue;
      }
      $sites[$uri] = $uri;
    }
  }
  return $sites;
}

/**
 * Remove a capsule-shaped tree, or park it for root-side cleanup.
 *
 * The web process creates cache subdirs at 0755 owned by the per-version web
 * user (FPM umask 022), and the CLI owner can neither delete their contents
 * nor chmod them, so a plain recursive delete leaves web-owned residue
 * behind (proven on the restore leg). The root heal (the sudo fix scripts,
 * which know the capsule model) hands the whole tree back to the instance
 * user first, so the delete then succeeds outright. Where the heal is
 * unavailable or something still survives, the fallback is a RENAME out of
 * the live tree into <backup_path>/pending-cleanup/ (rename needs only the
 * parent-dir write we do have), reported in ONE row: the residue is a
 * leftover of a completed task, not a failure of it, and the parked tree
 * ages out with the account's backup retention sweep.
 */
function _provision_grav_remove_tree_or_park($path, $label) {
  if (!is_dir($path) || is_link($path)) {
    return TRUE;
  }
  _provision_grav_root_heal($path);
  _provision_grav_recursive_delete($path);
  if (!is_dir($path)) {
    return TRUE;
  }
  $park_dir = d('@server_master')->backup_path . '/pending-cleanup';
  if (!is_dir($park_dir)) {
    @mkdir($park_dir, 0750);
  }
  $park = $park_dir . '/' . $label . '-' . date('Ymd.His');
  if (@rename($path, $park)) {
    drush_log(dt('Web-owned residue survived the delete of @path; parked at @park (removed by the backups retention sweep, or by hand).',
      array('@path' => $path, '@park' => $park)), 'ok');
    return TRUE;
  }
  return drush_set_error('PROVISION_GRAV_CLEANUP_FAILED',
    dt('Could not delete or park @path; remove it by hand.', array('@path' => $path)));
}

/**
 * Silent recursive delete: the shared helper logs a warning per inode it
 * cannot remove, which on a web-owned cache tree is an unbounded cascade
 * that colours the task for a condition the caller handles itself.
 * Symlinks are unlinked, never followed.
 */
function _provision_grav_recursive_delete($path) {
  $ok = TRUE;
  if (is_dir($path) && !is_link($path)) {
    $entries = @scandir($path);
    if (is_array($entries)) {
      foreach ($entries as $entry) {
        if ($entry === '.' || $entry === '..') {
          continue;
        }
        $ok = _provision_grav_recursive_delete($path . '/' . $entry) && $ok;
      }
    }
    return @rmdir($path) && $ok;
  }
  return @unlink($path);
}

/**
 * Hand a capsule tree back to the instance user through the root fix scripts.
 *
 * The web tier writes into the capsule as the per-version FPM user (nonce
 * key at 0600, account files, cache subdirs at 0755), and the instance user
 * that runs every task is only a group member: it can neither read nor
 * chmod nor delete those entries. The two sudo wrappers already know the
 * capsule model and are granted NOPASSWD to every instance user; this is
 * the same seam the Drupal platform verify uses for its own tree.
 *
 * Returns TRUE when both scripts ran clean, FALSE when one failed, NULL when
 * the seam is unavailable (a non-BOA host, an account outside /data/disk,
 * or a path this helper refuses to hand to a root tool).
 */
function _provision_grav_root_heal($path) {
  $aegir_root = d('@server_master')->aegir_root;
  if (empty($aegir_root) || strpos($aegir_root, '/data/disk/') !== 0) {
    return NULL;
  }
  $ownership = '/usr/local/bin/fix-drupal-site-ownership.sh';
  $permissions = '/usr/local/bin/fix-drupal-site-permissions.sh';
  if (!is_file($ownership) || !is_file($permissions)) {
    return NULL;
  }
  // Fail-closed validation before the value reaches a root tool: a real
  // directory under this account's tree, one path component below a
  // platform's sites/ directory (a capsule or its .restore/.original
  // sibling), no traversal, and a tree the wrappers recognise as a capsule
  // (a partial extraction has no system/defines.php yet; handed to the
  // wrapper it would be refused as "not a Drupal site", and the caller is
  // about to delete it anyway).
  if (!is_string($path) || $path === '' || !is_dir($path) || is_link($path)
    || strpos($path, '..') !== FALSE
    || strpos($path, $aegir_root . '/') !== 0
    || !preg_match('#/sites/[a-z0-9][a-z0-9._-]*$#i', $path)
    || !is_file($path . '/system/defines.php')
    || !is_file($path . '/bin/grav')) {
    return NULL;
  }
  $user = d('@server_master')->script_user;
  $group = d('@server_master')->web_group;
  if (empty($user) || empty($group)) {
    return NULL;
  }
  $ok = drush_shell_exec('sudo --non-interactive %s --site-path=%s --script-user=%s --web-group=%s',
    $ownership, $path, $user, $group);
  if ($ok) {
    $ok = drush_shell_exec('sudo --non-interactive %s --site-path=%s', $permissions, $path);
  }
  if ($ok) {
    drush_log(dt('Ownership and modes of @path re-asserted through the root heal.', array('@path' => $path)), 'info');
    return TRUE;
  }
  $output = implode("\n", (array) drush_shell_exec_output());
  drush_log(dt('The root heal of @path failed: !out', array('@path' => $path, '!out' => $output)), 'warning');
  return FALSE;
}

/**
 * The per-site environment overlay directory, if the capsule has one.
 *
 * Grav resolves environment:// to user/env/<GRAV_ENVIRONMENT> when user/env
 * exists on disk, else to user/<GRAV_ENVIRONMENT> (the 1.6 shape); .env
 * pins GRAV_ENVIRONMENT to the site uri. Returns the existing directory or
 * '' when the site carries no overlay (the stock capsule shape).
 */
function _provision_grav_env_dir($site_path, $uri) {
  if (is_dir($site_path . '/user/env')) {
    $candidate = $site_path . '/user/env/' . $uri;
  }
  else {
    $candidate = $site_path . '/user/' . $uri;
  }
  return (is_dir($candidate) && !is_link($candidate)) ? $candidate : '';
}

/**
 * The directory Grav reads config:// from for this capsule: the environment
 * overlay's config/ when the overlay exists, else user/config. Both secret
 * files (core nonce key, api plugin JWT secret) are keyed off it.
 */
function _provision_grav_config_dir($site_path, $uri) {
  $env = _provision_grav_env_dir($site_path, $uri);
  if ($env !== '' && is_dir($env . '/config')) {
    return $env . '/config';
  }
  return $site_path . '/user/config';
}

/**
 * Write one private-secret file in the shape Grav's own writers use.
 *
 * Grav mints user/config/security-private.php (CSRF nonce key, admin
 * rate-limit hash) and the api plugin mints config/plugins/api-private.php
 * (JWT signing secret) lazily from the FIRST process that needs them, which
 * is the web tier: the file then lands owned by the per-version FPM user at
 * an explicit 0600, unreadable by the instance user, so every backup skips
 * it and every safety copy warns. Seeding it here, as the instance user,
 * keeps the CLI owner on the file: 0660 with the web group so FPM reads it
 * via the group, and both readers short-circuit on an existing non-empty
 * file and never re-chmod. An existing file is left alone unless $rotate
 * (a clone must not share the source's keys); an existing file this user
 * cannot read is left for the root heal, never overwritten blind.
 */
function _provision_grav_seed_secret($file, $rotate = FALSE) {
  if (!$rotate && is_file($file)) {
    return is_readable($file) ? TRUE : NULL;
  }
  $dir = dirname($file);
  if (!is_dir($dir)) {
    if (!@mkdir($dir, 02770, TRUE)) {
      return FALSE;
    }
    @chgrp($dir, d('@server_master')->web_group);
  }
  if (function_exists('random_bytes')) {
    $value = bin2hex(random_bytes(32));
  }
  else {
    $value = bin2hex(openssl_random_pseudo_bytes(32));
  }
  $contents = "<?php\n\n"
    . "// Auto-generated private secret. Do NOT commit to version control.\n"
    . "// Seeded by the hosting framework so the site owner can read it (backups);\n"
    . "// the web tier reads it through the group. Delete to rotate.\n\n"
    . 'return ' . var_export($value, TRUE) . ";\n";
  $tmp = $file . '.provision-tmp';
  if (@file_put_contents($tmp, $contents, LOCK_EX) === FALSE) {
    return FALSE;
  }
  @chmod($tmp, 0660);
  @chgrp($tmp, d('@server_master')->web_group);
  if (!@rename($tmp, $file)) {
    @unlink($tmp);
    return FALSE;
  }
  return TRUE;
}

/**
 * Seed the capsule's private secrets: the core nonce key always, the api
 * plugin's JWT secret when the plugin is present. Runs on every install,
 * deploy and verify (create_directories) so a rotation by deletion is
 * re-seeded by the next task instead of by the web tier. With $rotate the
 * files are replaced: the identity half of a clone.
 */
function _provision_grav_seed_secrets($site_path, $uri, $rotate = FALSE) {
  $config = _provision_grav_config_dir($site_path, $uri);
  $targets = array($config . '/security-private.php');
  if (is_dir($site_path . '/user/plugins/api')) {
    $targets[] = $config . '/plugins/api-private.php';
  }
  $ok = 0;
  foreach ($targets as $file) {
    $result = _provision_grav_seed_secret($file, $rotate);
    if ($result === FALSE) {
      drush_log(dt('Could not seed the private secret @file; the web tier will mint it on first use.',
        array('@file' => $file)), 'warning');
    }
    elseif ($result === TRUE) {
      $ok++;
    }
  }
  if ($rotate && $ok === count($targets)) {
    drush_log(dt('Rotated the private secrets of the copy (@n file(s)): it signs its own nonces and tokens.',
      array('@n' => $ok)), 'ok');
  }
  return $ok;
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
    _provision_grav_chmod_tree($site_path . '/' . $dir);
  }

  // AFTER the generic writable pass (which would re-widen them): the secret
  // subtrees drop their world bits (boa-grav D-008).
  _provision_grav_secure_secrets($site_path);

  // The private secrets exist before the web tier ever needs them, owned by
  // the instance user: a web-minted key is 0600 to the FPM user and every
  // later backup would have to skip it.
  _provision_grav_seed_secrets($site_path, d()->uri);

  _provision_grav_write_env();
}

/**
 * Drop world access on the capsule's secret surfaces (boa-grav D-008).
 *
 * user/accounts (password hashes, reset tokens), user/config (session salt,
 * SMTP/API credentials) and user/env are group-rw for FPM and owner-rw for
 * the CLI, with NO world bits: dirs 02770, files 0660. nginx never serves
 * or reads them (the vhost denies the paths; only FPM does, via group).
 * The root .env (phase 2 carries DB credentials) is 0640 with the web
 * group, so FPM keeps its read once the world bit is gone. Runs after every
 * generic pass that would re-widen (install, deploy, verify, and mirrored
 * in the sudo heal scripts) -- the cross-track review caught the same class
 * on the TXP side (private/config.php widened 0440 -> 0644 on every heal).
 */
function _provision_grav_secure_secrets($site_path) {
  $web_group = d('@server_master')->web_group;
  foreach (array('user/accounts', 'user/config', 'user/env') as $dir) {
    $path = $site_path . '/' . $dir;
    if (!is_dir($path)) {
      continue;
    }
    provision_file()->chmod($path, 02770, TRUE)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    $it = new RecursiveIteratorIterator(
      new RecursiveDirectoryIterator($path, FilesystemIterator::SKIP_DOTS),
      RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $entry) {
      if ($entry->isFile() && !$entry->isLink()) {
        @chmod($entry->getPathname(), 0660);
      }
    }
  }
  $env = $site_path . '/.env';
  if (is_file($env)) {
    provision_file()->chgrp($env, $web_group);
    provision_file()->chmod($env, 0640);
  }
  // 0440, matching Provision_Config_Drushrc's own $mode: a heal that
  // disagrees with the writer makes the file flap on every cadence
  // (cross-track lesson -- the TXP pass widened its drushrc the same way).
  $drushrc = $site_path . '/drushrc.php';
  if (is_file($drushrc)) {
    provision_file()->chmod($drushrc, 0440);
  }
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
 *
 * $site_path/$uri name a capsule OTHER than the context's own: the rename
 * command writes the new name's .env the moment the directory lands there,
 * so the capsule is never live with a GRAV_ENVIRONMENT pointing at an
 * overlay directory that no longer exists. Both default to the context.
 */
function _provision_grav_write_env($site_path = NULL, $uri = NULL) {
  if (is_null($site_path) || $site_path === '') {
    $site_path = d()->site_path;
  }
  if (is_null($uri) || $uri === '') {
    $uri = d()->uri;
  }
  $lines = array(
    '# Written by provision (hosting framework); do not edit -- re-rendered on verify.',
    'GRAV_ENVIRONMENT=' . $uri,
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
  // The WRITER owns the mode (D-008): without this, deploy-path rewrites
  // leave .env at the process umask (world-readable) until the next verify
  // or heal tightens it. FPM reads it via the web group.
  provision_file()->chgrp($env, d('@server_master')->web_group);
  provision_file()->chmod($env, 0640);
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
 *
 * $site_path targets a capsule OTHER than the context's own (the copy an
 * upgrade just deployed on another platform): the seam is one function for
 * both, so a capsule reached through the copy path runs under exactly the
 * same guards as one reached in place. Defaults to the context's capsule.
 */
function _provision_grav_cli($tool, $args, $site_path = NULL) {
  if (is_null($site_path) || $site_path === '') {
    $site_path = d()->site_path;
  }
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
  // The login plugin rewrites this file atomically from FPM (a reset token,
  // a profile save), leaving it owned by the web user: the write below would
  // then fail and the caller would hand out a password that does not work.
  // Hand the tree back first; the heal is the operator's own verify action.
  if (is_file($yaml) && !is_writable($yaml)) {
    _provision_grav_root_heal($site_path);
  }
  $written = provision_file()->file_put_contents($yaml, implode("\n", $lines))
    ->succeed('Generated config file <code>@path</code>')
    ->fail('Could not generate config file <code>@path</code>')
    ->status();
  if (!$written) {
    return drush_set_error('PROVISION_GRAV_INSTALL_FAILED',
      dt('Could not write the account file !yaml; run Verify on this site first.', array('!yaml' => $yaml)));
  }
  $web_group = d('@server_master')->web_group;
  provision_file()->chgrp($yaml, $web_group);
  // 0660, not 0640: the login plugin WRITES here from FPM (reset tokens,
  // admin profile edits) -- the D-008 secrets mode, group-rw and no world.
  provision_file()->chmod($yaml, 0660);
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
 * The site-level package map: the capsule's own core version. Under the
 * capsule model each site is an independent copy upgraded in place, so the
 * platform seed's version is not the site's.
 */
function provision_grav_system_map() {
  drush_include_engine('grav', 'packages');
  return _provision_grav_packages(d()->site_path);
}

/**
 * The writable-tree mode pass: directories 02775, regular files 0664.
 *
 * provision_file()->chmod(..., TRUE) applies one mode to every entry it
 * walks, so 02775 landed on the files too: user content world-executable
 * and setgid after every verify. Links are left alone (chmod follows them).
 */
function _provision_grav_chmod_tree($path, $log = TRUE) {
  if (!is_dir($path) || is_link($path)) {
    return;
  }
  $it = new RecursiveIteratorIterator(
    new RecursiveDirectoryIterator($path, FilesystemIterator::SKIP_DOTS),
    RecursiveIteratorIterator::CHILD_FIRST);
  foreach ($it as $entry) {
    if ($entry->isLink()) {
      continue;
    }
    if ($entry->isDir()) {
      @chmod($entry->getPathname(), 02775);
    }
    elseif ($entry->isFile()) {
      @chmod($entry->getPathname(), 0664);
    }
  }
  @chmod($path, 02775);
  if ($log) {
    drush_log(dt('Set the modes of <code>@path</code>: directories 2775, files 0664', array('@path' => $path)), 'success');
  }
}

/**
 * Resolve the Grav schema string (e.g. "1.7.0") for a given root.
 *
 * The twin of _provision_grav_get_version(): GRAV_SCHEMA is the version the
 * update scripts under system/src/Grav/Installer/updates/ are selected
 * against, and it is what a minted user/config/versions.yaml records.
 */
function _provision_grav_get_schema($root = NULL) {
  if (is_null($root)) {
    $root = d()->root;
  }
  $schema = '';
  $defines = $root . '/system/defines.php';
  if (is_readable($defines)) {
    $data = file_get_contents($defines);
    if (preg_match('/define\\s*\\(\\s*[\'"]GRAV_SCHEMA[\'"]\\s*,\\s*[\'"]([^\'"]+)[\'"]/', $data, $match)) {
      $schema = $match[1];
    }
  }
  return $schema;
}

/**
 * Read GRAV_VERSION and GRAV_SCHEMA out of an artefact WITHOUT unpacking it.
 *
 * The artefact's own claim about itself, which is what every verdict on an
 * upgrade is measured against: the file name is a label anybody can rename,
 * the constants inside system/defines.php are the release. `unzip -p` streams
 * the single member to stdout; where the box has no unzip binary the PHP
 * extension reads the same member. Grav code is never executed to learn this.
 *
 * Returns array('version' => string, 'schema' => string); '' for anything
 * that could not be read, so callers fail closed on an empty version.
 */
function _provision_grav_artefact_defines($artefact) {
  $defines = array('version' => '', 'schema' => '');
  if (!is_string($artefact) || $artefact === '' || !is_readable($artefact)) {
    return $defines;
  }
  $data = '';
  if (drush_shell_exec('unzip -p %s %s', $artefact, '*/system/defines.php')) {
    $data = implode("\n", (array) drush_shell_exec_output());
  }
  if ($data === '' && class_exists('ZipArchive')) {
    $zip = new ZipArchive();
    if ($zip->open($artefact) === TRUE) {
      for ($i = 0; $i < $zip->numFiles; $i++) {
        $entry = $zip->getNameIndex($i);
        if (is_string($entry) && preg_match('#(^|/)system/defines\\.php$#', $entry)) {
          $data = $zip->getFromIndex($i);
          break;
        }
      }
      $zip->close();
    }
  }
  if (!is_string($data) || $data === '') {
    return $defines;
  }
  if (preg_match('/define\\s*\\(\\s*[\'"]GRAV_VERSION[\'"]\\s*,\\s*[\'"]([^\'"]+)[\'"]/', $data, $match)) {
    $defines['version'] = $match[1];
  }
  if (preg_match('/define\\s*\\(\\s*[\'"]GRAV_SCHEMA[\'"]\\s*,\\s*[\'"]([^\'"]+)[\'"]/', $data, $match)) {
    $defines['schema'] = $match[1];
  }
  return $defines;
}

/**
 * The version an official artefact's FILE NAME claims (grav-admin-v<V>.zip).
 */
function _provision_grav_artefact_filename_version($artefact) {
  if (!is_string($artefact) || $artefact === '') {
    return '';
  }
  if (preg_match('/^grav-admin-v([0-9][0-9a-zA-Z._-]*)\\.zip$/', basename($artefact), $match)) {
    return $match[1];
  }
  return '';
}

/**
 * Both readings of an artefact's version must agree before it is installed.
 *
 * The file name is the label the mirror channel staged; system/defines.php is
 * what the archive will actually install. A disagreement means the staged
 * file is not what it says it is -- refuse rather than upgrade to an unknown
 * release. Returns the agreed version, or '' after setting the error.
 */
function _provision_grav_artefact_version($artefact) {
  $named = _provision_grav_artefact_filename_version($artefact);
  $defines = _provision_grav_artefact_defines($artefact);
  if ($named === '') {
    drush_set_error('PROVISION_GRAV_NO_ARTEFACT',
      dt('Artefact @file is not named like an official release (grav-admin-v<version>.zip).',
      array('@file' => basename($artefact))));
    return '';
  }
  if ($defines['version'] === '') {
    drush_set_error('PROVISION_GRAV_ARTEFACT_UNREADABLE',
      dt('Could not read GRAV_VERSION from system/defines.php inside @file; without it the upgrade has no verdict to check itself against.',
      array('@file' => basename($artefact))));
    return '';
  }
  if ($named !== $defines['version']) {
    drush_set_error('PROVISION_GRAV_ARTEFACT_MISMATCH',
      dt('Artefact @file is named for Grav @named but carries Grav @inside; refusing to install an archive that is not what it claims to be.',
      array('@file' => basename($artefact), '@named' => $named, '@inside' => $defines['version'])));
    return '';
  }
  return $defines['version'];
}

/**
 * Mint user/config/versions.yaml for a capsule that has none.
 *
 * Grav's VersionUpdater replays every system/src/Grav/Installer/updates/*.php
 * newer than the schema recorded in versions.yaml. The file is minted by the
 * first WEB request, so a capsule that has only ever been installed, cloned
 * and upgraded from the CLI has none -- and the first upgrade then replays
 * the whole historical chain, including the 1.7-era script that writes 1.7
 * compatibility flags into a 2.x site. Writing the same content the web tier
 * would have written, before the install, keeps the replay to the steps that
 * really are new.
 *
 * The path is fixed. Unlike the secret files, which Grav resolves through
 * the config:// stream and therefore through the environment overlay, this
 * file is a literal in the installer (Versions::instance() and the schema
 * lookup in Install::install()) and in the web tier's InitializeProcessor:
 * user/config/versions.yaml and nowhere else. A copy in an overlay is not
 * the file Grav reads, so it must not stand in for a missing one here.
 *
 * Instance-user owned, group web, 0660: the web tier rewrites it through the
 * group on the next update it performs itself, and 0660 is the mode the
 * secret pass (boa-grav D-008) asserts on every regular file under
 * user/config. A wider seed would simply be corrected by the verify that
 * follows the upgrade, leaving two writers disagreeing about one file.
 */
function _provision_grav_seed_versions_yaml($site_path) {
  $config_dir = $site_path . '/user/config';
  if (is_file($config_dir . '/versions.yaml')) {
    return TRUE;
  }
  $version = _provision_grav_get_version($site_path);
  $schema = _provision_grav_get_schema($site_path);
  if ($version === '' || $schema === '') {
    drush_log(dt('Capsule @site carries no versions.yaml and its GRAV_VERSION/GRAV_SCHEMA could not be read; the update scripts will be selected by Grav alone.',
      array('@site' => $site_path)), 'advisory');
    return FALSE;
  }
  if (!is_dir($config_dir)) {
    provision_file()->mkdir($config_dir)
      ->succeed('Created <code>@path</code>')
      ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
  }
  $contents = "# Written by provision before an upgrade; the version this capsule was at.\n"
    . "core:\n"
    . "  grav:\n"
    . "    version: '" . $version . "'\n"
    . "    schema: '" . $schema . "'\n";
  $written = provision_file()->file_put_contents($config_dir . '/versions.yaml', $contents)
    ->succeed('Generated config file <code>@path</code>')
    ->fail('Could not generate config file <code>@path</code>')
    ->status();
  if (!$written) {
    return FALSE;
  }
  provision_file()->chgrp($config_dir . '/versions.yaml', d('@server_master')->web_group);
  provision_file()->chmod($config_dir . '/versions.yaml', 0660);
  drush_log(dt('Seeded versions.yaml for @site at Grav @version (schema @schema), so the upgrade replays only the steps newer than it.',
    array('@site' => $site_path, '@version' => $version, '@schema' => $schema)), 'ok');
  return TRUE;
}

/**
 * The `version:` a Grav package's blueprints.yaml declares, or ''.
 *
 * Anchored at column 0 with /m so only the package's own top-level key is
 * read, never a nested one from a form field or a dependency block.
 */
function _provision_grav_blueprint_version($dir) {
  $file = $dir . '/blueprints.yaml';
  if (!is_readable($file)) {
    return '';
  }
  $data = file_get_contents($file);
  if (!is_string($data)) {
    return '';
  }
  if (preg_match('/^version:\\s*[\'"]?([0-9][^\'"\\s]*)/m', $data, $match)) {
    return $match[1];
  }
  return '';
}

/**
 * A private extraction directory for the bundle refresh, or '' when none can
 * be made. The capsule's own tmp/ first (excluded from every backup, on the
 * capsule's filesystem so the swap below is a rename), the instance's tmp
 * next; 0700, so an extracted bundle is never readable by the web tier.
 */
function _provision_grav_bundle_tmpdir($site_path) {
  $bases = array($site_path . '/tmp');
  $aegir_root = d('@server_master')->aegir_root;
  if (!empty($aegir_root)) {
    $bases[] = $aegir_root . '/tmp';
  }
  foreach ($bases as $base) {
    if (!is_dir($base)) {
      continue;
    }
    $dir = $base . '/provision-grav-bundle-' . getmypid() . '-' . date('Ymd.His');
    if (@mkdir($dir, 0700)) {
      return $dir;
    }
  }
  return '';
}

/**
 * Copy a directory tree as the instance user, group-writable.
 *
 * The tar pipe the capsule materialisation already uses: modes and symlinks
 * survive, and umask 0002 keeps the group write bit a Grav tree needs (a
 * 022-created directory fatals the next web save).
 */
function _provision_grav_copy_tree($source, $target) {
  $old_umask = umask(02);
  if (!is_dir($target) && !@mkdir($target, 02775, TRUE)) {
    umask($old_umask);
    return FALSE;
  }
  $result = drush_shell_exec('tar -C %s -cf - . | tar -C %s -xf -', $source, $target);
  umask($old_umask);
  return $result;
}

/**
 * Refresh the capsule's bundled plugins and themes from the staged artefact.
 *
 * Grav's own installer ignores user/ entirely (Install::$ignores), so a core
 * upgrade leaves the admin bundle's plugins and theme at the version the
 * capsule was born with -- indefinitely, since this box never talks to the
 * package repository. This is the offline half: everything comes out of the
 * SAME official archive the core came from, so the pair the release ships is
 * the pair the capsule ends up with.
 *
 * Rules, in order of importance:
 *  - never downgrade: a slug is replaced only when the bundle's
 *    blueprints.yaml `version:` is strictly newer than the capsule's;
 *  - never guess: a version that cannot be read on either side leaves the
 *    capsule's copy alone (an advisory row says which);
 *  - a slug the bundle ships and the capsule lacks is installed (it is part
 *    of the managed set);
 *  - only user/plugins and user/themes are touched. user/config,
 *    user/accounts, user/pages and user/data are the site's own content and
 *    are never opened here;
 *  - the old directory is moved into the private tmp dir before the new one
 *    lands, so a failed copy is put back rather than left half-written;
 *  - the group/mode model is re-applied per replaced directory, and the cache
 *    is cleared in a separate CLI process at the end (one install per process
 *    is a hard Grav constraint, and the same applies to a tree that changed
 *    under a warm cache).
 */
function _provision_grav_refresh_bundle($site_path, $artefact) {
  $web_group = d('@server_master')->web_group;
  $tmp = _provision_grav_bundle_tmpdir($site_path);
  if ($tmp === '') {
    drush_log(dt('Could not create a private extraction directory for the bundle refresh; the capsule keeps the plugins and theme it had.'), 'warning');
    _provision_grav_cli('grav', 'clearcache -n', $site_path);
    return FALSE;
  }

  $old_umask = umask(02);
  $extracted = drush_shell_exec('unzip -q -o %s -d %s', $artefact, $tmp);
  umask($old_umask);
  $bundle = '';
  if ($extracted) {
    $tops = glob($tmp . '/*', GLOB_ONLYDIR);
    foreach ((array) $tops as $candidate) {
      if (is_dir($candidate . '/user/plugins') || is_dir($candidate . '/user/themes')) {
        $bundle = $candidate;
        break;
      }
    }
    if ($bundle === '' && (is_dir($tmp . '/user/plugins') || is_dir($tmp . '/user/themes'))) {
      $bundle = $tmp;
    }
  }
  if ($bundle === '') {
    _provision_grav_recursive_delete($tmp);
    drush_log(dt('Could not read the bundled plugins and themes out of @file; the capsule keeps the ones it had.',
      array('@file' => basename($artefact))), 'warning');
    _provision_grav_cli('grav', 'clearcache -n', $site_path);
    return FALSE;
  }

  $replaced = 0;
  $installed = 0;
  $kept = 0;
  $failed = 0;
  foreach (array('plugins', 'themes') as $kind) {
    $sources = glob($bundle . '/user/' . $kind . '/*', GLOB_ONLYDIR);
    foreach ((array) $sources as $source) {
      $slug = basename($source);
      $target = $site_path . '/user/' . $kind . '/' . $slug;
      $new_version = _provision_grav_blueprint_version($source);

      if (is_link($target)) {
        drush_log(dt('Bundle refresh: @kind @slug is a symlink on this capsule and was left alone.',
          array('@kind' => $kind, '@slug' => $slug)), 'advisory');
        $kept++;
        continue;
      }
      if (is_dir($target)) {
        $old_version = _provision_grav_blueprint_version($target);
        if ($new_version === '' || $old_version === '') {
          drush_log(dt('Bundle refresh: @kind @slug has no readable blueprints version on one side (bundle "@new", capsule "@old"); left as it is.',
            array('@kind' => $kind, '@slug' => $slug, '@new' => $new_version, '@old' => $old_version)), 'advisory');
          $kept++;
          continue;
        }
        if (!version_compare($new_version, $old_version, '>')) {
          $kept++;
          continue;
        }
        $stash = $tmp . '/replaced-' . $kind . '-' . $slug;
        if (!@rename($target, $stash)) {
          // Web-owned entries inside the package dir: the heal hands the
          // capsule back, then the move is retried exactly once.
          _provision_grav_root_heal($site_path);
          if (!@rename($target, $stash)) {
            drush_log(dt('Bundle refresh: could not set @kind @slug aside; it keeps version @old.',
              array('@kind' => $kind, '@slug' => $slug, '@old' => $old_version)), 'warning');
            $failed++;
            continue;
          }
        }
        if (!_provision_grav_copy_tree($source, $target)) {
          _provision_grav_recursive_delete($target);
          @rename($stash, $target);
          drush_log(dt('Bundle refresh: copying @kind @slug @new into the capsule failed; the previous @old is back in place.',
            array('@kind' => $kind, '@slug' => $slug, '@new' => $new_version, '@old' => $old_version)), 'warning');
          $failed++;
          continue;
        }
        provision_file()->chgrp($target, $web_group, TRUE);
        _provision_grav_chmod_tree($target, FALSE);
        drush_log(dt('Bundle refresh: @kind @slug @old -> @new.',
          array('@kind' => $kind, '@slug' => $slug, '@old' => $old_version, '@new' => $new_version)), 'ok');
        $replaced++;
        continue;
      }

      // Absent from the capsule: part of the managed bundle, so install it.
      if (!_provision_grav_copy_tree($source, $target)) {
        _provision_grav_recursive_delete($target);
        drush_log(dt('Bundle refresh: could not install the bundled @kind @slug.',
          array('@kind' => $kind, '@slug' => $slug)), 'warning');
        $failed++;
        continue;
      }
      provision_file()->chgrp($target, $web_group, TRUE);
      _provision_grav_chmod_tree($target, FALSE);
      drush_log(dt('Bundle refresh: @kind @slug installed at @new (shipped by the bundle, absent from the capsule).',
        array('@kind' => $kind, '@slug' => $slug, '@new' => ($new_version === '' ? dt('unknown') : $new_version))), 'ok');
      $installed++;
    }
  }

  _provision_grav_recursive_delete($tmp);
  drush_log(dt('Bundle refresh from @file: @replaced replaced, @installed installed, @kept already current or newer.',
    array('@file' => basename($artefact), '@replaced' => $replaced, '@installed' => $installed, '@kept' => $kept)), 'ok');
  if ($failed > 0) {
    drush_log(dt('Bundle refresh left @failed package(s) unchanged after a failed replacement; the capsule is consistent but not fully refreshed.',
      array('@failed' => $failed)), 'warning');
  }

  // One install per CLI process is a hard Grav constraint, and a tree that
  // changed under a warm cache needs the same separation: always a new
  // process, always after every write above.
  _provision_grav_cli('grav', 'clearcache -n', $site_path);
  return ($failed === 0);
}

/**
 * Did the Grav installer report a failure in the output it printed?
 *
 * DirectInstallCommand::serve() returns 0 even when the install FAILED -- the
 * failure is only printed, in red, from Installer::lastErrorMsg(). So the
 * exit code is never the verdict: the printed lines are read, and the
 * capsule's own version is checked afterwards. Returns the offending lines.
 */
function _provision_grav_install_output_errors($output) {
  $errors = array();
  foreach ((array) $output as $line) {
    if (!is_string($line)) {
      continue;
    }
    // The installer's own failure shapes, and nothing looser: an "ERROR"
    // line (the package, symlink and name refusals), the extraction line
    // reporting "failed", or the closing "'-" line carrying anything but
    // "Success!" (the installer's last error message goes there). A loose
    // word match would refuse a healthy install over a package name.
    $clean = trim(preg_replace('/<\/?[a-z]+>/', '', $line));
    if (preg_match('/(^|\s)ERROR\b/', $clean)
      || preg_match('/Extracting package\.\.\.\s+failed/', $clean)
      || (preg_match("/^'-\s/", $clean) && strpos($clean, 'Success!') === FALSE)) {
      $errors[] = $clean;
    }
  }
  return $errors;
}

/**
 * Install a staged official artefact into a capsule and prove it took.
 *
 * The one sequence both upgrade paths run, so an upgrade in place and an
 * upgrade of a copy can never drift apart: seed versions.yaml, direct-install
 * the core, read the printed output, REQUIRE the capsule's own GRAV_VERSION
 * to equal the artefact's afterwards, then refresh the bundled plugins and
 * themes out of the same archive and clear the cache.
 *
 * $artefact_version is the agreed version from _provision_grav_artefact_version().
 * Returns TRUE, or FALSE with the error already set.
 */
function _provision_grav_apply_artefact($site_path, $uri, $artefact, $artefact_version) {
  $before = _provision_grav_get_version($site_path);

  // Before the install: a capsule that never served a web request has no
  // versions.yaml, and the updater would replay every historical step.
  _provision_grav_seed_versions_yaml($site_path);

  $ran = _provision_grav_cli('gpm', 'direct-install -y -n ' . escapeshellarg($artefact), $site_path);
  if (!$ran) {
    // The CLI seam already logged the command and its output.
    return drush_set_error('PROVISION_GRAV_UPGRADE_FAILED',
      dt('gpm direct-install failed for !site; the capsule was not upgraded.',
      array('!site' => $site_path)));
  }
  // The installer's own account of what it did, in the task log at a healthy
  // level: this is the only place a failure shows up before the version check.
  $output = (array) drush_shell_exec_output();
  foreach ($output as $line) {
    if (is_string($line) && trim($line) !== '') {
      drush_log('GRAV/INSTALL ' . trim($line), 'ok');
    }
  }
  $errors = _provision_grav_install_output_errors($output);
  if (!empty($errors)) {
    return drush_set_error('PROVISION_GRAV_UPGRADE_FAILED',
      dt('The Grav installer reported a failure for !site while exiting 0 (it always does): !lines',
      array('!site' => $site_path, '!lines' => implode(' / ', $errors))));
  }

  // The verdict. The exit code lies, the printed output can be quiet, so the
  // capsule itself is asked what it now is.
  $after = _provision_grav_get_version($site_path);
  if ($after !== $artefact_version) {
    return drush_set_error('PROVISION_GRAV_UPGRADE_FAILED',
      dt('Capsule !site reports Grav @after after installing the @wanted artefact (it was @before); the upgrade did not take.',
      array('!site' => $site_path, '@after' => ($after === '' ? dt('nothing readable') : $after), '@wanted' => $artefact_version, '@before' => $before)));
  }
  drush_log(dt('Capsule !site now runs Grav @after (was @before).',
    array('!site' => $site_path, '@after' => $after, '@before' => $before)), 'success');

  // The bundled plugins and themes the core install deliberately ignores,
  // out of the same archive; it clears the cache as its last step.
  _provision_grav_refresh_bundle($site_path, $artefact);
  return TRUE;
}

/**
 * Move a capsule's uri-bound surfaces onto a new uri (identity preserved).
 *
 * Grav resolves environment:// to user/env/<GRAV_ENVIRONMENT> (or user/<..>
 * in the 1.6 shape) and REFUSES to register a directory that does not exist:
 * a capsule whose overlay still carries the old name silently loses every
 * per-host config, blueprint and language override. Nothing else in a capsule
 * is keyed by uri -- the secrets, accounts, pages and licences are the site's
 * identity and are deliberately untouched here.
 */
function _provision_grav_repoint_uri($site_path, $old_uri, $new_uri) {
  if (!is_string($old_uri) || !is_string($new_uri)
    || $old_uri === '' || $new_uri === '' || $old_uri === $new_uri) {
    return TRUE;
  }
  $ok = TRUE;
  foreach (array($site_path . '/user/env', $site_path . '/user') as $base) {
    $old_env = $base . '/' . $old_uri;
    $new_env = $base . '/' . $new_uri;
    if (!is_dir($old_env) || is_link($old_env)) {
      continue;
    }
    if (is_dir($new_env)) {
      drush_log(dt('Both @old and @new exist on this capsule; neither overlay was moved -- merge them by hand.',
        array('@old' => $old_env, '@new' => $new_env)), 'advisory');
      continue;
    }
    if (@rename($old_env, $new_env)) {
      drush_log(dt('Moved the environment overlay @old to @new so the site keeps its per-host overrides.',
        array('@old' => $old_env, '@new' => $new_env)), 'ok');
    }
    else {
      drush_log(dt('Could not move the environment overlay @old to @new; the site will ignore it until it is renamed by hand.',
        array('@old' => $old_env, '@new' => $new_env)), 'warning');
      $ok = FALSE;
    }
  }
  return $ok;
}

/**
 * The context options for a COPY of this site at a new uri.
 *
 * One authority for both copy commands (clone and copy-upgrade): the two must
 * ship the copy identically dark, or the difference is only ever found on a
 * live site. $label names the caller in the Encryption row.
 */
function _provision_grav_copy_options($new_name, $platform = NULL, $label = 'clone') {
  $options = d()->options;
  $new_uri = ltrim($new_name, '@');
  $options['uri'] = $new_uri;
  $hash_name = drush_get_option('#name') ? '#name' : 'name';
  $options[$hash_name] = $new_name;
  $options['platform'] = (isset($platform)) ? $platform : $options['platform'];
  $options['root'] = d($options['platform'])->root;
  $options['aliases'] = array();
  $options['redirection'] = 0;

  // Encryption auto-disable on the copy (SSL keys are source-name-bound).
  $options['ssl_enabled'] = 0;
  unset($options['ssl_key']);
  drush_log(dt('[!label] Encryption disabled for new site @name before final verify',
    array('!label' => $label, '@name' => $new_name)), 'info');

  // Do not copy CDN aliases to the copy.
  unset($options['cdn']);

  // A migration-source association is the source site's history, never the
  // copy's (latent while migrate is fail-closed; must be here before it).
  unset($options['migrate_source_db']);
  unset($options['migrate_source_uri']);
  unset($options['migrate_source_wired']);

  if ($profile = drush_get_option('profile', FALSE)) {
    $options['profile'] = $profile;
  }

  // DB-less pin: the copy keeps the source's database server (the 'none'
  // null service the validate hook asserted). A different server named by
  // the task would make the copy's deploy provision a database and grants
  // for a site that has none, so it is deliberately not honoured; said once
  // here rather than silently dropped. Gated on the source's service being
  // the null one, so database-backed Grav (the coming cart) is unaffected.
  $new_db_server = drush_get_option('new_db_server', FALSE);
  if ($new_db_server && isset($options['db_server'])
    && ltrim($new_db_server, '@') !== ltrim($options['db_server'], '@')) {
    drush_log(dt('The database server @server requested for the copy is ignored: a Grav capsule has no database and keeps the source\'s "none" server.',
      array('@server' => $new_db_server)), 'ok');
  }

  return $options;
}

/**
 * Remove a half-built copy: its capsule (parked if web-owned residue
 * survives), its vhost and its context. Without this a failed copy leaves a
 * capsule the platform-delete guard counts as a live site and a context the
 * front end never registered. Shared by the clone and copy-upgrade rollbacks
 * so both discard exactly the same set.
 */
function _provision_grav_discard_copy($new_name, $platform = NULL) {
  $new_uri = ltrim($new_name, '@');
  if ($new_uri === '' || $new_uri === d()->uri) {
    return;
  }
  // Context names carry the leading @ whatever the caller was handed.
  $new_name = '@' . $new_uri;
  $target_root = d()->root;
  if (!empty($platform)) {
    $target = d($platform, FALSE, FALSE);
    if (is_object($target) && isset($target->root) && $target->root !== '') {
      $target_root = $target->root;
    }
  }

  $capsule = $target_root . '/sites/' . $new_uri;
  if (is_dir($capsule) && !is_link($capsule)) {
    // The helper reports its own outcome (deleted silently, parked in one
    // row, or a cleanup error); only a tree that is really gone is "removed".
    if (_provision_grav_remove_tree_or_park($capsule, $new_uri . '-copy-failed') && !is_dir($capsule)) {
      drush_log(dt('Removed the half-built copy at @path.', array('@path' => $capsule)), 'ok');
    }
  }

  $vhost = d('@server_master')->http_vhostd_path . '/' . $new_uri;
  if (is_file($vhost)) {
    provision_file()->unlink($vhost)
      ->succeed('Removed the vhost of the half-built copy @path')
      ->fail('Could not remove the vhost of the half-built copy @path');
    d('@server_master')->service('http')->parse_configs();
  }

  // The alias record, not d(): loading a context that does not exist logs
  // an error row of its own, and a copy that failed before provision-save
  // has none.
  $record = provision_sitealias_get_record($new_name);
  if (!empty($record)) {
    drush_invoke_process('@none', 'provision-save', array($new_name), array('delete' => TRUE));
    drush_log(dt('Removed the context of the half-built copy @name.', array('@name' => $new_name)), 'ok');
  }
}

/**
 * Resolve and vet the artefact an upgrade is about to install.
 *
 * Fail-closed, and the whole verdict is formed BEFORE anything is written:
 * the file exists and is named like an official release, its file name and
 * its own GRAV_VERSION agree, and that version is strictly newer than the
 * capsule's. "Nothing newer is staged" is a refusal, not a no-op that reports
 * success. Returns array('artefact' => path, 'version' => version) or FALSE
 * with the error already set.
 */
function _provision_grav_resolve_artefact($capsule_version, $artefact = NULL) {
  if (empty($artefact)) {
    $artefact = _provision_grav_newest_artefact();
    if ($artefact === FALSE) {
      drush_set_error('PROVISION_GRAV_NO_ARTEFACT',
        dt('No staged official release found under <aegir_root>/static/grav-releases/ and no artefact argument given.'));
      return FALSE;
    }
  }
  if (!is_readable($artefact) || !preg_match('/grav-admin-v[0-9][^\\/]*\\.zip$/', basename($artefact))) {
    drush_set_error('PROVISION_GRAV_NO_ARTEFACT',
      dt('Artefact @file is not a readable official grav-admin-v*.zip.', array('@file' => $artefact)));
    return FALSE;
  }
  $version = _provision_grav_artefact_version($artefact);
  if ($version === '') {
    return FALSE;
  }
  if (!is_string($capsule_version) || $capsule_version === '') {
    drush_set_error('PROVISION_GRAV_SEED_INVALID',
      dt('Could not read the capsule\'s own GRAV_VERSION; run Verify on the site before upgrading it.'));
    return FALSE;
  }
  if (!version_compare($version, $capsule_version, '>')) {
    drush_set_error('PROVISION_GRAV_ARTEFACT_NOT_NEWER',
      dt('Nothing newer is staged: the capsule runs Grav @capsule and the staged artefact @file carries Grav @artefact.',
      array('@capsule' => $capsule_version, '@file' => basename($artefact), '@artefact' => $version)));
    return FALSE;
  }
  return array('artefact' => $artefact, 'version' => $version);
}
