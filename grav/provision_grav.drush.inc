<?php
/**
 * @file
 * Grav 2 specific functions for the provisioning framework.
 *
 * Parallel of txp/provision_txp.drush.inc for Grav 2 platforms and SITE-CAPSULE
 * sites (every Aegir Grav site is a complete, self-contained
 * Grav install at sites/<uri>/ inside a platform that is itself a pristine
 * admin-bundle release -- the seed each capsule is copied from). Every command
 * hook in this layer early-returns unless the context root is a Grav root
 * (provision_platform_is_grav(), defined once in the always-loaded
 * provision.inc); the Drupal-layer hooks carry a single added early-return on
 * the provision_platform_is_foreign_cms() union, so both commandfiles fire for
 * every command and the guards make them mutually exclusive.
 *
 * Grav is the mirror image of TXP: it ships a first-class CLI (bin/grav,
 * bin/gpm, bin/plugin -- the automation seam) and keeps its pages in flat
 * files. A site on hosting_grav's 'none' server has no database at all; one
 * created on a MySQL server (a shop) has a database and a login, carried
 * through every task the way the Textpattern layer carries them
 * (_provision_grav_has_db() gates each database branch). No drush bootstrap
 * of the CMS ever happens (GravBoot exists only to
 * satisfy the bootstrap-phase gate); every Grav CLI call runs in a separate
 * enforced-PHP process as the instance user with umask 0002 (both spike-proven
 * requirements).
 *
 * PHP floor: Grav support requires PHP >= 8.3 at task runtime,
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
 * The shelf the BOA channel stages the official admin-bundle zips on: one
 * directory of root's for the whole server (staticbuild -> mirrors -> box).
 */
define('PROVISION_GRAV_RELEASES_DIR', '/data/all/000/grav-releases');

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
    'description' => 'Upgrade a Grav site capsule in place to a staged official admin-bundle release (mirror-fed; safety backup first; user content, accounts and configuration survive).',
    'arguments' => array(
      'artefact' => dt('Optional path to the official grav-admin-v*.zip. Default: the newest one staged on the shelf of the server, /data/all/000/grav-releases/.'),
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
      'new_db_server' => dt('The database server of the copy, for a site with a database (never the "none" server). A site on the "none" server keeps it for its copy and this option is ignored.'),
      'artefact' => dt('Optional path to the official grav-admin-v*.zip. Default: the newest one staged on the shelf of the server, /data/all/000/grav-releases/.'),
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
 * The BOA channel (staticbuild -> mirrors -> box) stages the official
 * grav-admin-v*.zip files on PROVISION_GRAV_RELEASES_DIR, one shelf of
 * root's for the whole server; the newest version the file name claims
 * wins. The per-account shelf an earlier BOA release staged under
 * <aegir_root>/static/grav-releases/ is still read until the account's next
 * BOA run removes it, so an upgrade queued across that change still finds
 * its artefact. Returns FALSE when nothing is staged.
 */
function _provision_grav_newest_artefact() {
  $dirs = array(PROVISION_GRAV_RELEASES_DIR);
  $server = d('@server_master');
  if (!empty($server->aegir_root)) {
    $dirs[] = $server->aegir_root . '/static/grav-releases';
  }
  $best = FALSE;
  $best_version = '';
  foreach ($dirs as $dir) {
    $candidates = glob($dir . '/grav-admin-v*.zip');
    if (!is_array($candidates)) {
      continue;
    }
    foreach ($candidates as $candidate) {
      $version = _provision_grav_artefact_filename_version($candidate);
      if ($version === '') {
        continue;
      }
      if ($best === FALSE || version_compare($version, $best_version, '>')) {
        $best = $candidate;
        $best_version = $version;
      }
    }
  }
  return $best;
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
 * No per-site or per-instance PHP selection for Grav (the floor is
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
  $candidates = array('/opt/php84/bin/php', '/opt/php85/bin/php', '/opt/php86/bin/php', '/opt/php83/bin/php');
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
 * Does this site have a database of its own?
 *
 * A Grav site on hosting_grav's 'none' server (Provision_Service_null) has no
 * database at all. One created on a MySQL server (a shop) has a database and
 * a login of its own, which every task carries the way the Textpattern layer
 * does. Every database branch in this layer is gated on this test, so a site
 * on the 'none' server behaves exactly as it did before. The database must
 * also be on record, in the running context or the site's drushrc.php (read,
 * never loaded): see _provision_grav_db_unrecorded() for a site that has the
 * server but not the record.
 */
function _provision_grav_has_db() {
  if (!_provision_grav_db_service_real()) {
    return FALSE;
  }
  if (drush_get_option('db_name', '', 'site') !== '') {
    return TRUE;
  }
  $creds = _provision_grav_drushrc_creds(d()->site_path . '/drushrc.php');
  return !empty($creds['db_name']);
}

/**
 * Whether the site's db service is a real one (not the 'none' server).
 */
function _provision_grav_db_service_real() {
  $db = d()->service('db');
  return is_object($db) && !($db instanceof Provision_Service_null);
}

/**
 * A site on a database server with no database on record.
 *
 * A Grav site created on a MySQL server before the Grav tasks carried a
 * database got one at install, and its first Verify or Backup rewrote
 * drushrc.php without the credentials. Nothing can find that database again,
 * so the site is files-only: its safety copies and backups carry no dump, and
 * the copying tasks, which would make the copy a database it cannot fill,
 * refuse it.
 */
function _provision_grav_db_unrecorded() {
  return _provision_grav_db_service_real() && !_provision_grav_has_db();
}

/**
 * The refusal of a copying task on a site with no database on record.
 */
function _provision_grav_refuse_unrecorded($what) {
  if (!_provision_grav_db_unrecorded()) {
    return TRUE;
  }
  return drush_set_error('PROVISION_GRAV_DB_UNRECORDED',
    dt('This Grav site is on a database server but no database is on record for it (it was created before Grav tasks carried a database, and its drushrc.php no longer names one), so its @what cannot be built through the database deploy. Move the site to the "none" database server, or put its db_* lines back into drushrc.php, and retry.',
    array('@what' => $what)));
}

/**
 * Read the db credentials out of a persisted site drushrc.php into an array.
 *
 * Regex parse (no include, so the file's side effects never run). Returns the
 * db_* subset found, or an empty array. For the flows that must NOT pass the
 * credentials through the 'site' option context: a deploy, where that context
 * is about to hold a NEW database's credentials, and a forced reinstall.
 */
function _provision_grav_drushrc_creds($drushrc) {
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
 * Reload persisted site options (db credentials + installed flag).
 *
 * Provision commands dispatch at the Drush bootstrap level, so GravBoot never
 * reaches the site phase that would load the site drushrc.php: the db
 * credentials enter the 'site' context only at install. A later task that
 * ends with the exit hook's drushrc write (verify, backup) or acts on the
 * database (delete, restore) must reload them first, or the drushrc is
 * rewritten without them and Delete leaves the database and its login
 * behind. Namespaced copy of the Textpattern helper; on a site with no
 * database it restores the installed flag alone.
 */
function _provision_grav_ensure_db_creds_loaded() {
  if (d()->type !== 'site') {
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
  if (!(drush_get_option('db_name', '', 'site') && drush_get_option('db_passwd', '', 'site'))) {
    foreach (array('db_type', 'db_user', 'db_passwd', 'db_host', 'db_name', 'db_port') as $key) {
      if (drush_get_option($key, '', 'site')) {
        continue;
      }
      if (preg_match('/\\$options\\s*\\[\\s*[\'"]' . $key . '[\'"]\\s*\\]\\s*=\\s*[\'"]([^\'"]*)[\'"]/', $body, $m)) {
        drush_set_option($key, $m[1], 'site');
      }
    }
  }
  if (!drush_get_option('installed')
    && preg_match('/\\$options\\s*\\[\\s*[\'"]installed[\'"]\\s*\\]\\s*=\\s*(TRUE|true|1)\\s*;/', $body)) {
    drush_set_option('installed', TRUE, 'site');
  }
}

/**
 * Set or clear the internal-backup flag a copying task's safety copy uses.
 *
 * The flag tells the db layer that this backup feeds a deploy (a clone or a
 * restore): it dumps whatever mode a concurrent panel task leaked in, and the
 * dated tmp_expim retention leaves the dump alone. A leftover flag silently
 * ends that retention on the account, so every path that sets it clears it.
 */
function _provision_grav_internal_backup_flag($on) {
  $flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if ($on) {
    if (!provision_file()->exists($flag)->status()) {
      provision_file()->file_put_contents($flag, "Internal-DB-Backup \n")
        ->succeed('Generated blank Internal-DB-Backup Flag-File (grav)')
        ->fail('Could not generate Internal-DB-Backup Flag-File (grav)');
    }
    return;
  }
  if (is_file($flag)) {
    provision_file()->unlink($flag)
      ->succeed('Removed Internal-DB-Backup Flag-File (grav)')
      ->fail('Could not remove Internal-DB-Backup Flag-File (grav)');
  }
}

/**
 * Force the self-contained mode of a safety copy, for what this site is.
 *
 * A safety copy is registered in the panel as a real backup of the site, so
 * it must restore everything the site has: a site on the 'none' server is
 * files only (site_files_without_db_backup, the label that tells the truth
 * and whose tar dereferences), a site with a database carries its dump inside
 * the archive (site_files_with_mysqldump, the Textpattern contract). The
 * classic dump creates database.sql with fopen('x'), which fails on a file a
 * killed earlier backup left behind, so that goes first. $internal raises the
 * internal-backup flag for a copy that feeds a deploy (clone, restore,
 * copy-upgrade); the caller clears it.
 *
 * @return string
 *   The mode forced.
 */
function _provision_grav_force_safety_mode($what, $internal = FALSE) {
  $mode = _provision_grav_has_db() ? 'site_files_with_mysqldump' : 'site_files_without_db_backup';
  if ($mode === 'site_files_with_mysqldump') {
    _provision_grav_ensure_db_creds_loaded();
    if (is_file(d()->site_path . '/database.sql')) {
      provision_file()->unlink(d()->site_path . '/database.sql')
        ->succeed('Removed a stale database.sql before the safety copy')
        ->fail('Could not remove a stale database.sql before the safety copy');
    }
    if ($internal) {
      _provision_grav_internal_backup_flag(TRUE);
    }
  }
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', $mode);
  }
  drush_set_option('selected_backup_mode', $mode);
  drush_set_option('backup_mode', $mode);
  drush_log(dt('@what safety copy forced to the restorable mode: @mode', array('@what' => $what, '@mode' => $mode)), 'info');
  return $mode;
}

/**
 * The database settings of a site with a database, as Grav config overrides.
 *
 * KahunaCart reads its connection from plugins.kahunacart.database. Grav
 * applies GRAV_CONFIG__<key, dots as __> from $_SERVER/$_ENV on every
 * request and command once GRAV_CONFIG is set, and its admin never writes
 * such a value back to disk, so the credentials live only in the root-owned
 * vhost and, for one CLI call, in that call's environment -- never in a file
 * the shared web group can read. 'localhost' with a port other than 3306
 * becomes 127.0.0.1: PDO takes 'localhost' as the socket and would skip the
 * port (ProxySQL). auto_migrate stays manual: Verify migrates.
 *
 * @return array
 *   name => value; empty when the credentials are incomplete.
 */
function _provision_grav_db_overrides($db_name, $db_user, $db_passwd, $db_host, $db_port) {
  $db_name = (string) $db_name;
  $db_user = (string) $db_user;
  $db_passwd = (string) $db_passwd;
  if ($db_name === '' || $db_user === '' || $db_passwd === '') {
    return array();
  }
  $port = ((string) $db_port === '') ? '3306' : (string) $db_port;
  $host = ((string) $db_host === '') ? 'localhost' : (string) $db_host;
  if ($host === 'localhost' && $port !== '3306') {
    $host = '127.0.0.1';
  }
  $key = 'GRAV_CONFIG__plugins__kahunacart__database__';
  return array(
    'GRAV_CONFIG' => '1',
    $key . 'type' => 'mysql',
    $key . 'auto_migrate' => 'manual',
    $key . 'mysql__host' => $host,
    $key . 'mysql__port' => $port,
    $key . 'mysql__dbname' => $db_name,
    $key . 'mysql__username' => $db_user,
    $key . 'mysql__password' => $db_passwd,
  );
}

/**
 * The same overrides as server-level fastcgi_param lines, for the vhost.
 *
 * Every value must be one nginx takes unquoted (provision's own names and
 * passwords always are); one that is not drops the whole set rather than
 * write a line that could break the nginx reload of every site on the box.
 * A site on the 'none' server has no credentials and gets no line at all.
 */
function _provision_grav_db_fastcgi_lines($db_name, $db_user, $db_passwd, $db_host, $db_port) {
  $out = '';
  foreach (_provision_grav_db_overrides($db_name, $db_user, $db_passwd, $db_host, $db_port) as $name => $value) {
    if (!preg_match('/^[A-Za-z0-9._:-]+$/', $value)) {
      return '';
    }
    $out .= '  fastcgi_param ' . $name . ' ' . $value . ";\n";
  }
  return $out;
}

/**
 * Whether a capsule carries KahunaCart, switched on.
 *
 * The plugin tree is the test; a plugin switched off in the capsule's own
 * config or its host overlay registers no command, so it counts as absent.
 */
function _provision_grav_kahunacart_present($site_path, $uri) {
  if (!is_file($site_path . '/user/plugins/kahunacart/kahunacart.php')) {
    return FALSE;
  }
  foreach (array($site_path . '/user/env/' . $uri . '/config/plugins/kahunacart.yaml',
    $site_path . '/user/config/plugins/kahunacart.yaml') as $yaml) {
    if (is_file($yaml) && is_readable($yaml)) {
      $body = file_get_contents($yaml);
      if ($body !== FALSE && preg_match('/^enabled:\s*(\S+)/m', $body, $m)) {
        return !in_array(strtolower(trim($m[1], '\'"')), array('false', '0', 'no', 'off'), TRUE);
      }
    }
  }
  return TRUE;
}

/**
 * Bring a capsule's KahunaCart schema up to date (Verify's step).
 *
 * KahunaCart never migrates a server database by itself, and its migrations
 * only go forward, so every task that ends with a Verify (install, restore,
 * clone, both upgrades, a client's plugin update followed by Verify) applies
 * them here, as the instance user with the capsule's own credentials. A
 * failure fails the task: a store on a stale schema answers 500s.
 */
function _provision_grav_kahunacart_migrate($site_path, $uri) {
  if (!_provision_grav_kahunacart_present($site_path, $uri)) {
    return TRUE;
  }
  if (!_provision_grav_cli('plugin', 'kahunacart migrate -n', $site_path)) {
    return drush_set_error('PROVISION_GRAV_KAHUNACART_MIGRATE',
      dt('The KahunaCart schema of !uri could not be brought up to date; see the Grav CLI row above.', array('!uri' => $uri)));
  }
  drush_log(dt('KahunaCart schema of !uri is up to date.', array('!uri' => $uri)), 'ok');
  return TRUE;
}

/**
 * Make a cloned store sign with a secret of its own.
 *
 * KahunaCart keeps its per-site signing secret in its own database
 * (kahunacart_settings, name security.secret, the empty namespace) and
 * derives from it the key that seals the payment provider credentials in its
 * settings files. A clone that kept it would sign links the source honours
 * and could decrypt, and use, the source's live payment keys. Deleting the
 * row is the vendor's own rotation: the copy makes a fresh secret on first
 * use, and its provider keys are entered again before it can take a payment.
 * Runs in the copy's deploy, on the copy's database; a database without the
 * table (no store yet) is left alone.
 */
function _provision_grav_rotate_store_secret() {
  $db_name = drush_get_option('db_name', '', 'site');
  $db_user = drush_get_option('db_user', '', 'site');
  if ($db_name === '' || $db_user === '' || !function_exists('mysqli_init')) {
    return TRUE;
  }
  $db_port = drush_get_option('db_port', '', 'site');
  mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);
  try {
    $mysqli = mysqli_init();
    $mysqli->real_connect(
      drush_get_option('db_host', 'localhost', 'site'),
      $db_user,
      drush_get_option('db_passwd', '', 'site'),
      $db_name,
      ((string) $db_port === '') ? 3306 : (int) $db_port
    );
    $table = $mysqli->query("SHOW TABLES LIKE 'kahunacart_settings'");
    if (!$table || $table->num_rows === 0) {
      $mysqli->close();
      return TRUE;
    }
    $namespaced = $mysqli->query("SHOW COLUMNS FROM kahunacart_settings LIKE 'namespace'");
    $sql = ($namespaced && $namespaced->num_rows > 0)
      ? "DELETE FROM kahunacart_settings WHERE name = 'security.secret' AND namespace = ''"
      : "DELETE FROM kahunacart_settings WHERE name = 'security.secret'";
    $mysqli->query($sql);
    $removed = $mysqli->affected_rows;
    $mysqli->close();
  }
  catch (Exception $e) {
    return drush_set_error('PROVISION_GRAV_STORE_SECRET',
      dt('Could not rotate the store signing secret of the copy: @error', array('@error' => $e->getMessage())));
  }
  drush_log(dt('The copy\'s store signs with a secret of its own (@n source secret removed); its payment provider keys must be entered again before it takes a payment.',
    array('@n' => $removed)), 'ok');
  return TRUE;
}

/**
 * Blank the licences add-on's response signing secret in a copy's config.
 *
 * KahunaCart's licences add-on signs its validate and activate responses with
 * api.signing_secret, which the merchant's shipped software trusts; empty
 * means unsigned. A Clone that kept it would sign as the live store. The
 * site's own config and every host overlay are covered; a line-level edit
 * inside the top-level api: block, the file's owner and mode kept. Returns
 * the number of files changed.
 */
function _provision_grav_blank_licences_signing_secret($site_path) {
  $files = array($site_path . '/user/config/plugins/kahunacart-licenses.yaml');
  // Host overlays: user/env/<host>/ and, where user/env/ does not exist, the
  // Grav 1.6 shape user/<host>/ that Grav still falls back to.
  foreach (array('/user/env/*/config/plugins/kahunacart-licenses.yaml', '/user/*/config/plugins/kahunacart-licenses.yaml') as $pattern) {
    $found = glob($site_path . $pattern);
    if (is_array($found)) {
      $files = array_merge($files, $found);
    }
  }
  $changed = 0;
  foreach (array_unique($files) as $file) {
    if (is_link($file)) {
      drush_log(dt('@file is a link, so the copy\'s licence signing secret there was not cleared; replace it with a file of the copy\'s own and rotate the secret.', array('@file' => $file)), 'warning');
      continue;
    }
    if (!is_file($file)) {
      continue;
    }
    $lines = file($file);
    if ($lines === FALSE) {
      drush_log(dt('Could not read @file to clear the copy\'s licence signing secret; rotate it on the copy.', array('@file' => $file)), 'warning');
      continue;
    }
    $in_api = FALSE;
    $done = FALSE;
    foreach ($lines as $i => $line) {
      $bare = rtrim($line, "\r\n");
      if (preg_match('/^api:\s*(#.*)?$/', $bare)) {
        $in_api = TRUE;
        continue;
      }
      if ($in_api && preg_match('/^\S/', $bare)) {
        $in_api = FALSE;
      }
      if ($in_api && preg_match('/^(\s+signing_secret:)\s*(.*)$/', $bare, $m)) {
        $value = trim(preg_replace('/\s+#.*$/', '', $m[2]));
        if ($value !== '' && $value !== "''" && $value !== '""' && $value !== '~' && $value !== 'null') {
          $lines[$i] = $m[1] . " ''" . substr($line, strlen($bare));
          $done = TRUE;
        }
        break;
      }
    }
    if (!$done) {
      continue;
    }
    if (@file_put_contents($file, implode('', $lines), LOCK_EX) !== FALSE) {
      $changed++;
    }
    else {
      drush_log(dt('Could not write @file: the copy still signs licence responses with the source\'s secret there; rotate it on the copy.', array('@file' => $file)), 'warning');
    }
  }
  return $changed;
}

/**
 * The marker a copy of a store carries: <capsule>/.boa-copy. Its first line
 * names the live store the copy stands in for; a second line names the site
 * the copy was made from, when that is another one. A `store-db <name>` line
 * names that store's database and a `source-db <name>` line the source's,
 * when it is another: a Rename keeps a site's database, so Cutover knows
 * both sites by them, whatever domain they have now. A marker written before
 * a line existed lacks it, and Cutover then goes by the domains.
 *
 * A store's worker sends its mail and runs its renewals on every scheduler
 * tick, paused or not. A copy (a Clone, an Upgrade a copy) shares the
 * source's customers and subscriptions, so BOA's scheduler leaves its
 * worker alone while the marker is there: a staging copy must not mail or
 * bill the live store's customers, and an upgraded copy must not run the
 * same renewals as the source it will replace. Cutover removes the marker
 * from the copy that takes over; an owner releases a copy that should run
 * with the control file the scheduler documents. A dotfile at the capsule
 * root: nginx never serves it, and it travels with the capsule on Rename and
 * in its backups.
 *
 * Returns the store, the source, the store's database and the source's
 * database, each '' when absent or malformed.
 */
function _provision_grav_copy_names($site_path) {
  $names = array('', '', '', '');
  $marker = _provision_grav_copy_marker($site_path);
  if (is_file($marker) && !is_link($marker)) {
    $lines = explode("\n", (string) @file_get_contents($marker, FALSE, NULL, 0, 1024));
    foreach (array(0, 1) as $i) {
      $name = isset($lines[$i]) ? strtolower(trim($lines[$i])) : '';
      if (preg_match('/^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/', $name)) {
        $names[$i] = $name;
      }
    }
    foreach ($lines as $line) {
      if (preg_match('/^(store|source)-db ([A-Za-z0-9_]{1,64})$/', trim($line), $m)) {
        $names[($m[1] === 'store') ? 2 : 3] = $m[2];
      }
    }
  }
  return $names;
}

/**
 * The database the drushrc.php of the site at $site_path names, '' when it
 * names none.
 */
function _provision_grav_site_db($site_path) {
  $rc = $site_path . '/drushrc.php';
  if (!is_file($rc) || is_link($rc)) {
    return '';
  }
  if (preg_match('/^\$options\[\'db_name\'\] = \'([A-Za-z0-9_]{1,64})\';$/m', (string) @file_get_contents($rc, FALSE, NULL, 0, 65536), $m)) {
    return $m[1];
  }
  return '';
}

/**
 * The enabled site of this server, other than those in $except, whose
 * drushrc.php names the database $db, '' when there is none; with $running,
 * only one that runs a store worker now (KahunaCart in its capsule, not a
 * held copy). Reads the account's site aliases: Cutover only.
 */
function _provision_grav_db_site($db, $except = array(), $running = TRUE) {
  if ($db === '') {
    return '';
  }
  $files = glob(drush_server_home() . '/.drush/*.alias.drushrc.php');
  if (!is_array($files)) {
    return '';
  }
  foreach ($files as $file) {
    $uri = basename($file, '.alias.drushrc.php');
    if (in_array($uri, $except, TRUE) || !preg_match('/^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/', $uri)) {
      continue;
    }
    $record = drush_sitealias_get_record('@' . $uri);
    if (empty($record['site_path']) || empty($record['site_enabled'])) {
      continue;
    }
    if (_provision_grav_site_db($record['site_path']) !== $db) {
      continue;
    }
    if (!$running || (_provision_grav_kahunacart_present($record['site_path'], $uri)
      && !_provision_grav_copy_held($record['site_path'], $uri))) {
      return $uri;
    }
  }
  return '';
}

/**
 * Whether the copy at $site_path is held: its marker is there and no
 * release file for $uri is newer, the test BOA's scheduler applies. File
 * times are read to the second here, so a release made in the marker's own
 * second counts as not newer: the copy reads as held.
 */
function _provision_grav_copy_held($site_path, $uri) {
  $marker = _provision_grav_copy_marker($site_path);
  if (!is_file($marker) || is_link($marker)) {
    return FALSE;
  }
  clearstatcache();
  $release = d('@server_master')->aegir_root . '/static/control/kahunacart-run.' . $uri . '.info';
  $marked = @filemtime($marker);
  $released = @filemtime($release);
  return !($marked !== FALSE && $released !== FALSE && $released > $marked);
}

/**
 * The live store a copy of the site at $site_path stands in for: the store
 * the site's own marker names when it is a copy itself, held or released (a
 * release can be taken back), otherwise the site itself. Cutover decides
 * from the state of the site it retires, read before it goes.
 */
function _provision_grav_copy_root($site_path, $uri) {
  $names = _provision_grav_copy_names($site_path);
  return ($names[0] !== '') ? $names[0] : strtolower((string) $uri);
}

/**
 * The database of that live store: the one the site's own marker names when
 * it is a copy itself ('' from a marker that predates the line), otherwise
 * the site's own.
 */
function _provision_grav_copy_root_db($site_path) {
  $names = _provision_grav_copy_names($site_path);
  return ($names[0] !== '') ? $names[2] : _provision_grav_site_db($site_path);
}

/**
 * What a deploy writes into a new copy's marker: the live store and its
 * database, as the copy command decided them in the source's context (the
 * grav_copy_of and grav_copy_of_db options), else as the archive's own
 * marker names them, else the source; the source itself, and its database
 * (grav_copy_from_db).
 */
function _provision_grav_copy_names_for_deploy($staged_path, $old_uri) {
  $old_uri = strtolower((string) $old_uri);
  $root = strtolower(trim((string) drush_get_option('grav_copy_of', '')));
  $root_db = trim((string) drush_get_option('grav_copy_of_db', ''));
  $from_db = trim((string) drush_get_option('grav_copy_from_db', ''));
  if (!preg_match('/^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/', $root)) {
    $names = _provision_grav_copy_names($staged_path);
    $root = ($names[0] !== '') ? $names[0] : $old_uri;
    $root_db = ($names[0] !== '') ? $names[2] : '';
    $from_db = '';
  }
  if (!preg_match('/^[A-Za-z0-9_]{1,64}$/', $root_db)) {
    $root_db = '';
  }
  if (!preg_match('/^[A-Za-z0-9_]{1,64}$/', $from_db)) {
    $from_db = '';
  }
  return array($root, $old_uri, $root_db, $from_db);
}

/**
 * Whether the site at $uri on this server runs a store worker now: an
 * enabled site whose capsule carries KahunaCart and is not a held copy (a
 * released copy runs).
 */
function _provision_grav_store_runs($uri) {
  if ($uri === '' || !is_file(drush_server_home() . '/.drush/' . $uri . '.alias.drushrc.php')) {
    return FALSE;
  }
  $record = drush_sitealias_get_record('@' . $uri);
  if (empty($record['site_path']) || empty($record['site_enabled'])) {
    return FALSE;
  }
  return _provision_grav_kahunacart_present($record['site_path'], $uri)
    && !_provision_grav_copy_held($record['site_path'], $uri);
}

function _provision_grav_copy_marker($site_path) {
  return $site_path . '/.boa-copy';
}

function _provision_grav_mark_copy($site_path, $root_uri, $mtime = NULL, $from_uri = '', $root_db = '', $from_db = '') {
  $marker = _provision_grav_copy_marker($site_path);
  $body = (string) $root_uri . "\n";
  if ((string) $from_uri !== '' && (string) $from_uri !== (string) $root_uri) {
    $body .= (string) $from_uri . "\n";
  }
  if ((string) $root_db !== '') {
    $body .= 'store-db ' . $root_db . "\n";
  }
  if ((string) $from_db !== '' && (string) $from_db !== (string) $root_db) {
    $body .= 'source-db ' . $from_db . "\n";
  }
  provision_file()->file_put_contents($marker, $body)
    ->succeed('Marked the copy at @path: BOA\'s scheduler leaves its store worker alone until cutover or release')
    ->fail('Could not mark the copy at @path');
  // A hold put back keeps its old time: BOA counts a release file only when
  // it is newer than the marker, so a release given before stays given.
  if ($mtime !== NULL && $mtime !== FALSE && is_file($marker)) {
    @touch($marker, (int) $mtime);
  }
}

function _provision_grav_unmark_copy($site_path) {
  $marker = _provision_grav_copy_marker($site_path);
  if (is_file($marker) && !is_link($marker)) {
    provision_file()->unlink($marker)
      ->succeed('Removed the copy marker @path: this site is the live store now')
      ->fail('Could not remove the copy marker @path');
  }
}

/**
 * Refuse a copy of a site with a database onto the 'none' server.
 *
 * The copy of a shop needs a database to hold the shop: on the 'none' server
 * its deploy would create none and the archive's dump would be thrown away.
 * A site on the 'none' server keeps that server for its copy (see
 * _provision_grav_copy_options()), so only the database-backed direction
 * is checked.
 */
function _provision_grav_copy_db_server_valid($what) {
  if (!_provision_grav_has_db()) {
    return TRUE;
  }
  $new_db_server = drush_get_option('new_db_server', FALSE);
  if (!$new_db_server) {
    return TRUE;
  }
  $server = d($new_db_server);
  if (is_object($server) && $server->service('db') instanceof Provision_Service_null) {
    return drush_set_error('PROVISION_GRAV_COPY_DB_NONE',
      dt('This Grav site has a database; its @what needs a database server too, not @server.',
      array('@what' => $what, '@server' => $new_db_server)));
  }
  return TRUE;
}

/**
 * Whether this site runs a KahunaCart store with no database on record.
 *
 * Given no database server, KahunaCart keeps its store in its own SQLite file
 * inside the capsule (its shipped default). The copy safeguards (a signing
 * secret of its own, a paused store, a held scheduler) act on a database on
 * record, and BOA's scheduler runs only such a store, so a store on its own
 * file gets none of them.
 */
function _provision_grav_file_store() {
  if (_provision_grav_has_db()) {
    return FALSE;
  }
  return _provision_grav_kahunacart_present(d()->site_path, d()->uri);
}

/**
 * The refusal of a Clone of a store with no database on record.
 *
 * The copy would carry the source's signing secret in the copied SQLite file,
 * and with it the key that seals the source's payment provider credentials:
 * a staging copy could use the live store's payment keys.
 */
function _provision_grav_refuse_file_store($what) {
  if (!_provision_grav_file_store()) {
    return TRUE;
  }
  return drush_set_error('PROVISION_GRAV_FILE_STORE',
    dt('This Grav site runs a KahunaCart store on its own SQLite file, with no database server: its @what would sign with the source store\'s secret and could use its payment keys. A shop belongs on a site created on a database server.',
    array('@what' => $what)));
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
 * (the shared guard is a settings.php glob and is EMPTY for Grav
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
      // either: both Grav upgrade paths refuse symlinked trees, so
      // counting one would register a site every task must then choke on.
      if (strpos($uri, '.restore') !== FALSE
        || strpos($uri, '.original') !== FALSE
        || is_link($capsule)) {
        continue;
      }
      $sites[$uri] = $uri;
    }
  }
  // A copy is unpacked as sites/.<uri>.copy and moved in only once it is
  // held; a task that died before the move leaves it there. The glob above
  // never sees a hidden name, so it is named here to get it removed: its
  // drushrc.php names the SOURCE store's database.
  $unfinished = glob($root . '/sites/.*.copy/system/defines.php');
  if (is_array($unfinished)) {
    foreach ($unfinished as $marker) {
      drush_log(dt('@path is a copy whose task never finished (it names its source\'s database); it is not a site. Remove it.',
        array('@path' => dirname(dirname($marker)))), 'warning');
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
    // The group decided for the place it lands on (see
    // provision_path_web_group_chgrp()), as for the file below.
    provision_path_web_group_chgrp($dir);
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
  provision_path_web_group_chgrp($tmp);
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
 * Drop a BOA control INI from a Grav tree, and its dir while empty.
 *
 * Grav never reads boa_site_control.ini or boa_platform_control.ini (the
 * global.inc chain does not run for a capsule), so BOA no longer seeds either
 * of them; this clears what an earlier release, the nightly or an
 * old archive left. Both names go whatever they hold: nothing reads them
 * here, and BOA's own nightly uncommented lines in them, so their content
 * says nothing about intent. The directory goes only when that leaves it
 * empty, so nothing else in it is ever lost.
 * A link at the directory is left alone; a link at a file name goes as a link.
 * Raw unlink()/rmdir() with ok/advisory rows on purpose: a provision_file()
 * ->fail() logs a warning, and one warning row colours the whole task.
 *
 * @param string $dir
 *   The directory (sites/<uri>/modules, sites/all/modules, ...).
 * @param string $ini
 *   boa_site_control.ini or boa_platform_control.ini; '' drops only an empty
 *   directory.
 */
function _provision_grav_drop_boa_ini_dir($dir, $ini) {
  if (is_link($dir) || !is_dir($dir) || is_link(dirname($dir))) {
    return;
  }
  $files = ($ini === '') ? array() : array('default.' . $ini, $ini);
  foreach ($files as $file) {
    $path = $dir . '/' . $file;
    if (!is_link($path) && !is_file($path)) {
      continue;
    }
    if (@unlink($path)) {
      drush_log(dt('Removed @path: Grav sites have no BOA control INI.', array('@path' => $path)), 'ok');
    }
    else {
      drush_log(dt('Could not remove @path.', array('@path' => $path)), 'advisory');
    }
  }
  clearstatcache();
  $entries = @scandir($dir);
  if (is_array($entries) && count($entries) === 2) {
    if (@rmdir($dir)) {
      drush_log(dt('Removed the empty @path.', array('@path' => $dir)), 'ok');
    }
    else {
      drush_log(dt('Could not remove the empty @path.', array('@path' => $dir)), 'advisory');
    }
  }
}

/**
 * Materialise (or repair) the site capsule from the platform seed.
 *
 * First run: copy the ENTIRE platform tree (a pristine admin-bundle release)
 * into sites/<uri>/, excluding sites/ itself (no recursion) and the seed's
 * own volatile dirs' contents. Every run: assert vendor/, enforce the
 * ownership model on the writable set and refresh the per-site .env. A
 * capsule carries no BOA control INI: whatever an earlier
 * release or an old archive left in modules/ is cleared first.
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

  // No BOA control INI in a capsule: clear what an earlier
  // release, the nightly or an old archive left, then lay down the writable
  // set.
  _provision_grav_drop_boa_ini_dir($site_path . '/modules', 'boa_site_control.ini');

  // The account owns the names in its capsule, so each writable dir is
  // judged once where it really is (_provision_drupal_store_place(), the
  // capsule as the site dir, which logs STORE/SKIP for one that leads out of
  // the account's own places) and is made, given the group and the modes
  // from inside the directory entered (Provision_FileSystem's *_in()
  // helpers). A writable dir that is itself a link is left alone, as chgrp
  // and the mode walk leave a link. By path only where the place rule is not
  // loaded.
  $judge = function_exists('_provision_drupal_store_place');
  $places = array();
  foreach (_provision_grav_writable_dirs() as $dir) {
    $path = $site_path . '/' . $dir;
    $places[$dir] = $judge ? _provision_drupal_store_place($path, $site_path) : NULL;
    if (is_dir($path) || $places[$dir] === FALSE) {
      continue;
    }
    if ($judge) {
      provision_file()->mkdir_in($places[$dir]['dir'], $places[$dir]['rest'], $path)
        ->succeed('Created <code>@path</code>')
        ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
    }
    else {
      provision_file()->mkdir($path)
        ->succeed('Created <code>@path</code>')
        ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
    }
  }

  // Ownership model (spike-proven): group = web group, 02775, recursive.
  // The web group where each dir really is, decided on the dir judged as it
  // stands: outside the account's tree, the shared one
  // (provision_path_web_group()).
  // chmod AFTER chgrp (chgrp strips setgid).
  foreach (_provision_grav_writable_dirs() as $dir) {
    $path = $site_path . '/' . $dir;
    if (!$judge) {
      provision_path_web_group_chgrp($path, $web_group, TRUE)
        ->succeed('Changed group ownership of <code>@path</code> to @gid')
        ->fail('Could not change group ownership of <code>@path</code> to @gid');
    }
    elseif ($places[$dir] !== FALSE && !$places[$dir]['link']) {
      $at = _provision_drupal_place_path($places[$dir]);
      provision_file()->chgrp_in($at, provision_path_web_group(($at === NULL) ? $path : $at, $web_group, $at !== NULL), TRUE, $path)
        ->succeed('Changed group ownership of <code>@path</code> to @gid')
        ->fail('Could not change group ownership of <code>@path</code> to @gid');
    }
  }
  foreach (_provision_grav_writable_dirs() as $dir) {
    _provision_grav_chmod_tree($site_path . '/' . $dir, TRUE, $site_path);
  }

  // AFTER the generic writable pass (which would re-widen them): the secret
  // subtrees drop their world bits.
  _provision_grav_secure_secrets($site_path);

  // The private secrets exist before the web tier ever needs them, owned by
  // the instance user: a web-minted key is 0600 to the FPM user and every
  // later backup would have to skip it.
  _provision_grav_seed_secrets($site_path, d()->uri);

  _provision_grav_write_env();
}

/**
 * Drop world access on the capsule's secret surfaces.
 *
 * user/accounts (password hashes, reset tokens), user/config (session salt,
 * SMTP/API credentials) and user/env are group-rw for FPM and owner-rw for
 * the CLI, with NO world bits: dirs 02770, files 0660. nginx never serves
 * or reads them (the vhost denies the paths; only FPM does, via group).
 * The root .env is 0640 with the web group, so FPM keeps its read once the
 * world bit is gone; it never carries database credentials, because tenant
 * pools share the web group. Runs after every
 * generic pass that would re-widen (install, deploy, verify, and mirrored
 * in the sudo heal scripts) -- the cross-track review caught the same class
 * on the TXP side (private/config.php widened 0440 -> 0644 on every heal).
 */
function _provision_grav_secure_secrets($site_path) {
  $web_group = d('@server_master')->web_group;
  // Judged where each really is, the capsule as the site dir, and changed
  // from inside the directory entered (as _provision_grav_create_directories()
  // does); by path only where the place rule is not loaded.
  if (function_exists('_provision_drupal_store_place')) {
    _provision_grav_secure_secrets_in($site_path, $web_group);
    return;
  }
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
    provision_path_web_group_chgrp($env, $web_group);
    provision_file()->chmod($env, 0640);
  }
  // 0440, matching Provision_Config_Drushrc's own $mode: a heal that
  // disagrees with the writer makes the file flap on every cadence
  // (cross-track lesson -- the TXP pass widened its drushrc the same way).
  $drushrc = $site_path . '/drushrc.php';
  if (is_file($drushrc)) {
    provision_file()->chmod($drushrc, 0440);
  }
  // Premium licence keys: written by the admin's licence manager (FPM) and
  // read by GPM (the CLI owner), so group-rw for FPM like the account files,
  // no world bits.
  $licenses = $site_path . '/user/data/licenses.yaml';
  if (is_file($licenses) && !is_link($licenses)) {
    provision_path_web_group_chgrp($licenses, $web_group);
    provision_file()->chmod($licenses, 0660);
  }
}

/**
 * _provision_grav_secure_secrets() where each secret really is.
 *
 * Each secret tree is judged once (_provision_drupal_store_place(), the
 * capsule as the site dir) and walked from inside it with directories
 * 02770 and files 0660 (Provision_FileSystem::chmod_in()); the root .env
 * and drushrc.php are changed by name in the capsule entered for real.
 * A secret tree that is itself a link has its files narrowed where it
 * leads, its directories left as they are, only when the place rule finds
 * that inside the account's own files store, as the by-path walk narrowed
 * it; a secret file that is a link is left alone.
 */
function _provision_grav_secure_secrets_in($site_path, $web_group) {
  $fs = provision_file();
  foreach (array('user/accounts', 'user/config', 'user/env') as $dir) {
    $path = $site_path . '/' . $dir;
    if (!is_dir($path)) {
      continue;
    }
    $place = _provision_drupal_store_place($path, $site_path);
    if ($place === FALSE || $place['dir'] === NULL || $place['rest']) {
      continue;
    }
    $fs->chmod_in($place['dir'], $place['link'] ? NULL : 02770, TRUE, $path, 0660)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
  }
  $place = _provision_drupal_store_place($site_path, $site_path);
  if ($place === FALSE || $place['dir'] === NULL || $place['rest']) {
    return;
  }
  $cwd = getcwd();
  if ($fs->_enter($place['dir'])) {
    clearstatcache();
    if (is_file('.env') && !is_link('.env')) {
      $fs->_lchgrp_here('.env', provision_path_web_group($place['dir'] . '/.env', $web_group, TRUE));
      $fs->_chmod_here_nofollow(array('.env'), 0640);
    }
    // 0440, matching Provision_Config_Drushrc's own $mode (see above).
    if (is_file('drushrc.php') && !is_link('drushrc.php')) {
      $fs->_chmod_here_nofollow(array('drushrc.php'), 0440);
    }
  }
  $fs->_leave($cwd);
  // Premium licence keys (see above), judged where the file really is.
  $licenses_dir = $site_path . '/user/data';
  if (is_dir($licenses_dir) && is_file($licenses_dir . '/licenses.yaml')) {
    $place = _provision_drupal_store_place($licenses_dir, $site_path);
    if ($place !== FALSE && $place['dir'] !== NULL && !$place['rest'] && !$place['link']) {
      if ($fs->_enter($place['dir'])) {
        clearstatcache();
        if (is_file('licenses.yaml') && !is_link('licenses.yaml')) {
          $fs->_lchgrp_here('licenses.yaml', provision_path_web_group($place['dir'] . '/licenses.yaml', $web_group, TRUE));
          $fs->_chmod_here_nofollow(array('licenses.yaml'), 0660);
        }
      }
      $fs->_leave($cwd);
    }
  }
}

/**
 * Write the per-site .env (Grav 2 native, loaded before any constant --
 * the settings.php analogue).
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
  // The WRITER owns the mode: without this, deploy-path rewrites
  // leave .env at the process umask (world-readable) until the next verify
  // or heal tightens it. FPM reads it via the web group, decided for the
  // place it lands on.
  provision_path_web_group_chgrp($env);
  provision_file()->chmod($env, 0640);
}

/**
 * Run a Grav CLI command inside a capsule (the automation seam).
 *
 * Contract (spike-proven): the
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
  // A capsule with a database: its own credentials (read from its own
  // drushrc.php, so a copy never runs with the source's) reach the child as
  // config overrides for this one call, and are taken away again after it.
  $creds = _provision_grav_drushrc_creds($site_path . '/drushrc.php');
  $env = _provision_grav_db_overrides(
    isset($creds['db_name']) ? $creds['db_name'] : '',
    isset($creds['db_user']) ? $creds['db_user'] : '',
    isset($creds['db_passwd']) ? $creds['db_passwd'] : '',
    isset($creds['db_host']) ? $creds['db_host'] : '',
    isset($creds['db_port']) ? $creds['db_port'] : '');
  foreach ($env as $name => $value) {
    putenv($name . '=' . $value);
  }
  $old_umask = umask(02);
  $result = drush_shell_cd_and_exec($site_path, $bin . ' bin/' . $tool . ' ' . $args);
  umask($old_umask);
  foreach (array_keys($env) as $name) {
    putenv($name);
  }
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
  provision_path_web_group_chgrp($yaml, $web_group);
  // 0660, not 0640: the login plugin WRITES here from FPM (reset tokens,
  // admin profile edits) -- the secrets mode, group-rw and no world.
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
function _provision_grav_chmod_tree($path, $log = TRUE, $site_dir = NULL) {
  if (!is_dir($path) || is_link($path)) {
    return;
  }
  // Given the capsule it lies in, the tree is judged once where it really is
  // and walked from inside it (Provision_FileSystem::chmod_in() with a mode
  // for the files), so a link on the way is never followed.
  if ($site_dir !== NULL && function_exists('_provision_drupal_store_place')) {
    $place = _provision_drupal_store_place($path, $site_dir);
    if ($place === FALSE || $place['dir'] === NULL || $place['rest'] || $place['link']) {
      return;
    }
    if (provision_file()->chmod_in($place['dir'], 02775, TRUE, $path, 0664)->status() && $log) {
      drush_log(dt('Set the modes of <code>@path</code>: directories 2775, files 0664', array('@path' => $path)), 'success');
    }
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
 * secret pass asserts on every regular file under
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
  provision_path_web_group_chgrp($config_dir . '/versions.yaml');
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
        provision_path_web_group_chgrp($target, $web_group, TRUE);
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
      provision_path_web_group_chgrp($target, $web_group, TRUE);
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

  // A site with a database: its copy gets a database of its own, on the
  // server the task names (the validate refused the 'none' one) or else the
  // source's. A site on the 'none' server: the copy keeps that server, since
  // a different one would make the copy's deploy provision a database and
  // grants for a site that has none; said once here rather than silently
  // dropped.
  $new_db_server = drush_get_option('new_db_server', FALSE);
  if (_provision_grav_has_db()) {
    if ($new_db_server) {
      $options['db_server'] = $new_db_server;
    }
  }
  elseif ($new_db_server && isset($options['db_server'])
    && ltrim($new_db_server, '@') !== ltrim($options['db_server'], '@')) {
    drush_log(dt('The database server @server requested for the copy is ignored: this Grav site has no database and its copy keeps the "none" server.',
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
 *
 * $copy_option names the record provision_target_copy_probe() kept for a
 * copy of a site with a database: the copy's database goes first, through
 * provision_remove_unfinished_target_copy() and its guards (never the
 * source's database, never one another site names), while the copy still
 * names it on disk.
 */
function _provision_grav_discard_copy($new_name, $platform = NULL, $copy_option = '') {
  $new_uri = ltrim($new_name, '@');
  if ($new_uri === '' || $new_uri === d()->uri) {
    return;
  }
  if ($copy_option !== '' && _provision_grav_has_db()) {
    provision_remove_unfinished_target_copy($copy_option);
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
  // The copy is unpacked as sites/.<uri>.copy and moved in only once it is
  // held: a deploy that died before the move leaves it there.
  $unpacked = $target_root . '/sites/.' . $new_uri . '.copy';
  if (is_dir($unpacked) && !is_link($unpacked)) {
    if (_provision_grav_remove_tree_or_park($unpacked, $new_uri . '-copy-unpacked') && !is_dir($unpacked)) {
      drush_log(dt('Removed the unpacked copy at @path.', array('@path' => $unpacked)), 'ok');
    }
  }
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
        dt('No staged official release found on /data/all/000/grav-releases/ and no artefact argument given.'));
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
