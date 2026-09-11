<?php

/**
 * @file
 * provision-txp-upgrade — Textpattern release upgrade (TXP layer).
 *
 * Textpattern's core is SHARED by every site on a platform, so a release
 * upgrade cannot be an in-place capsule swap the way it is for Grav: moving
 * one site to a newer release means moving it onto a platform that carries
 * that release. The supported shape is therefore the clone-preserving one the
 * D7 -> Backdrop upgrade established: back the live source up, save a NEW site
 * alias at a NEW uri on the newer-core platform, deploy the backup there
 * (deploy always imports into a freshly created copy database), run
 * Textpattern's OWN update chain against the COPY in a separate modern-PHP
 * process, prove the result, then verify. The source site, its database and
 * its vhost are never in the write path; rollback at any step discards the
 * copy.
 *
 * Three properties of the chain shape this command, all verified in the 4.9.1
 * source rather than assumed:
 *  - the step selection is driven by the DATABASE's recorded version, not by
 *    the codebase, so the gates here read that row and refuse anything the
 *    chain would handle badly (a missing row makes it replay every historical
 *    step; a pre-4.7.2 source reaches steps that DELETE files from a core this
 *    site shares with its neighbours);
 *  - the chain's own try/catch is silent and the language loop that follows it
 *    still runs under an error handler that throws on any diagnostic, so the
 *    version pref can be left unwritten on a run whose schema work completed.
 *    That is what the structure assertion and the version reconcile below are
 *    for: prove the schema, then finish the bookkeeping the chain dropped --
 *    never by re-running the chain, which is not idempotent;
 *  - update/custom/post-update*.php is a real include glob, so a target core
 *    carrying third-party scripts is refused rather than silently executed.
 *
 * Hook naming: the command name starts with this commandfile's name, so Drush
 * COLLAPSES the prefix for the validate/command/rollback hooks
 * (drush_provision_txp_upgrade*) while the pre variations keep the doubled form
 * (drush_provision_txp_pre_provision_txp_upgrade) -- includes/command.inc:405
 * in the Drush 8 fork. File name: Drush resolves command includes by the
 * reversed-dotted command name, so provision-txp-upgrade becomes
 * upgrade.txp.provision.inc.
 */

/**
 * Implements drush_hook_COMMAND_validate() (collapsed name).
 *
 * Every precondition, fail-closed and bootstrap-free, before anything is
 * created. Nothing here reads the CMS through drush: the source version comes
 * from the site's own database over mysqli and the target version from the
 * target codebase's constants.
 */
function drush_provision_txp_upgrade_validate($new_name = NULL, $platform = NULL) {
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_SOURCE',
      dt('provision-txp-upgrade must run against a site context.'));
  }
  if (!provision_platform_is_txp(d())) {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_SOURCE',
      dt('The source site is not on a Textpattern platform.'));
  }
  // Clear a stale frontend backup-mode ctrl file before the safety backup
  // runs: inheriting a dead task's mode would build the source archive in
  // e.g. "database only" mode and deploy a files-less copy that still reports
  // success.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }

  // The enforced floor, and the one extension the chain asserts for itself:
  // assert_system_requirements() (called from _update.php) txp_die()s without
  // SimpleXML, which from a headless run is an unexplained empty failure.
  $php_bin = _provision_txp_php_bin();
  if ($php_bin === FALSE) {
    return drush_set_error('PROVISION_TXP_PHP_TOO_OLD',
      dt('No PHP >= 8.4 binary found; TXP tasks refuse to run below the enforced floor.'));
  }
  if (!drush_shell_exec('%s -r %s', $php_bin, 'exit(extension_loaded("simplexml") ? 0 : 1);')) {
    return drush_set_error('PROVISION_TXP_PHP_TOO_OLD',
      dt('The PHP binary !bin has no SimpleXML extension; Textpattern\'s update chain asserts it and dies without one.',
      array('!bin' => $php_bin)));
  }

  // The target platform: this layer's OWN predicate on the target root, never
  // the foreign-CMS union -- every CMS layer ships in the same provision tree.
  if (!is_string($platform) || $platform === '') {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
      dt('No target platform alias given.'));
  }
  $target = d($platform);
  $target_root = (is_object($target) && isset($target->root)) ? $target->root : '';
  if (empty($target_root) || !provision_platform_is_txp($target_root)) {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
      dt('The target platform !platform is not a Textpattern platform.', array('!platform' => $platform)));
  }

  // The new uri must differ from the source and be free on the target.
  if (!is_string($new_name) || $new_name === '') {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
      dt('No new site alias given.'));
  }
  $new_uri = ltrim($new_name, '@');
  if ($new_uri === d()->uri) {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
      dt('The new uri must differ from the source uri (the source keeps serving until you cut over to the copy).'));
  }
  // The same refusals the rename carries, and for the same reason: the value
  // becomes a directory name, a vhost name and a context name, and
  // provision-save rewrites whichever alias file it lands on. The shape check
  // is deliberately strict (the frontend hands down a cleaned, lower-cased
  // domain) so a mangled hand-run stops here rather than three steps later.
  if (!preg_match('/^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/', $new_uri)
    || strpos($new_uri, '..') !== FALSE) {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
      dt('The new uri !uri is not a plausible host name.', array('!uri' => $new_uri)));
  }
  if ($new_uri === 'hostmaster' || $new_uri === strtolower(d('@hostmaster')->uri)) {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
      dt('Refusing to take the hosting front-end\'s name.'));
  }
  // A context that already holds the name is refused: saving the copy over it
  // would point a live site's alias at the target platform's root and orphan
  // it from its own directory, with nothing to roll that back. The one
  // tolerated case is the leftover that demonstrably serves nothing -- its
  // recorded site directory is gone -- which is the same orphan the cutover
  // tolerates, and which the copy's own provision-save overwrites.
  if (provision_file()->exists(drush_server_home() . '/.drush/' . $new_uri . '.alias.drushrc.php')->status()) {
    $leftover = d('@' . $new_uri);
    $leftover_dir = (is_object($leftover) && isset($leftover->site_path) && is_string($leftover->site_path))
      ? $leftover->site_path : '';
    if ($leftover_dir === '' || provision_file()->exists($leftover_dir)->status()) {
      return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
        dt('A backend context already holds !uri.', array('!uri' => $new_uri)));
    }
    drush_log(dt('A stale !uri alias with no site directory is left over; the copy\'s own context overwrites it.',
      array('!uri' => $new_uri)), 'info');
  }
  if (is_dir($target_root . '/sites/' . $new_uri)) {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
      dt('sites/@uri already exists on the target platform.', array('@uri' => $new_uri)));
  }

  // The version gate. The SOURCE reading is the database's, not the source
  // platform's: a site whose own update is still pending records the older
  // version and it is that value the chain will start from.
  $connect_error = '';
  $mysqli = _provision_txp_db_connect(_provision_txp_site_creds(), $connect_error);
  if ($mysqli === FALSE) {
    return drush_set_error('PROVISION_TXP_UPGRADE_VERSION_UNREADABLE',
      dt('Could not read the source site database (!msg); the upgrade needs its recorded version.',
      array('!msg' => $connect_error)));
  }
  $source_version = _provision_txp_db_version($mysqli, _provision_txp_config_prefix(d()->site_path . '/private/config.php'));
  $mysqli->close();

  if ($source_version === '' || !preg_match('/^[0-9]+\.[0-9]+/', $source_version)) {
    return drush_set_error('PROVISION_TXP_UPGRADE_VERSION_UNREADABLE',
      dt('The source site records no usable Textpattern version (txp_prefs.version is "!v"). The update chain treats an empty value as 0.9.9 and would replay every historical step against this database; fix the row before upgrading.',
      array('!v' => $source_version)));
  }
  $target_version = _provision_txp_get_version($target_root);
  if ($target_version === '') {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
      dt('Could not read the Textpattern version of the target platform !platform.', array('!platform' => $platform)));
  }
  if (version_compare($source_version, $target_version, '>=')) {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
      dt('The target platform carries Textpattern !target and this site records !source; an upgrade needs a NEWER target. Use Clone to copy a site onto a same-version platform.',
      array('!target' => $target_version, '!source' => $source_version)));
  }
  // The floor. _to_4.6.0.php and _to_4.7.0.php delete files from the core
  // directory (gated only on is_writable(txpath), and the chain runs as the
  // core's owner) -- on a SHARED multisite core that is every other site's
  // codebase, not just this one's.
  if (version_compare($source_version, '4.7.2', '<')) {
    return drush_set_error('PROVISION_TXP_UPGRADE_VERSION_TOO_OLD',
      dt('This site records Textpattern !source; the supported upgrade floor is 4.7.2. The steps below it (textpattern/update/_to_4.6.0.php and textpattern/update/_to_4.7.0.php) DELETE files from the core directory, which on a multisite platform is shared with every other site on it.',
      array('!source' => $source_version)));
  }

  // The multisite assertion, on the SOURCE config the shim will consume:
  // _update.php's last file-side act removes the setup directory named by
  // $txpcfg['multisite_root_path'], and with the key absent it removes the
  // SHARED core's setup directory instead. Regex, never an include -- the
  // config file connects to the database at include time.
  $config_php = d()->site_path . '/private/config.php';
  if (!is_readable($config_php)) {
    return drush_set_error('PROVISION_TXP_UPGRADE_LAYOUT',
      dt('No readable private/config.php for the source site at !file.', array('!file' => $config_php)));
  }
  $config_body = file_get_contents($config_php);
  $multisite = '';
  if (preg_match('/\\$txpcfg\\s*\\[\\s*[\'"]multisite_root_path[\'"]\\s*\\]\\s*=\\s*[\'"]([^\'"]*)[\'"]/', $config_body, $m)) {
    $multisite = $m[1];
  }
  if ($multisite === '' || realpath($multisite) === FALSE || realpath($multisite) !== realpath(d()->site_path)) {
    return drush_set_error('PROVISION_TXP_UPGRADE_LAYOUT',
      dt('The source config.php does not declare multisite_root_path as this site\'s own directory (!site). Run Verify on the site first: the settings writer is the only author of that file.',
      array('!site' => d()->site_path)));
  }

  // Third-party post-update scripts on the TARGET core are a real include glob
  // at the end of the chain.
  $custom = glob($target_root . '/textpattern/update/custom/post-update*.php');
  if (is_array($custom) && !empty($custom)) {
    $names = array();
    foreach ($custom as $file) {
      $names[] = basename($file);
    }
    return drush_set_error('PROVISION_TXP_UPGRADE_CUSTOM_STEPS',
      dt('The target platform carries third-party post-update scripts the chain would include (!files). Remove them from textpattern/update/custom/ or upgrade onto a platform without them.',
      array('!files' => implode(', ', $names))));
  }

  // Announce the work: the exact step files the chain will include, recomputed
  // from the TARGET core's own selection logic, so the operator sees the plan
  // before the copy is built rather than the outcome afterwards.
  $steps = _provision_txp_upgrade_steps($target_root, $source_version, $target_version);
  if ($steps === FALSE) {
    return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
      dt('Could not read the update-step list from the target core (textpattern/update/_update.php).'));
  }
  foreach ($steps as $step) {
    if (!is_readable($target_root . '/textpattern/update/_to_' . $step . '.php')) {
      return drush_set_error('PROVISION_TXP_UPGRADE_BAD_TARGET',
        dt('The target core is missing the update step _to_!step.php the chain would include.', array('!step' => $step)));
    }
  }
  if (empty($steps)) {
    drush_log(dt('Upgrade !source -> !target: no schema step applies; the chain rewrites the version, the update stamp and the installed language packs.',
      array('!source' => $source_version, '!target' => $target_version)), 'ok');
  }
  else {
    drush_log(dt('Upgrade !source -> !target will run !n update step(s): !steps.',
      array('!source' => $source_version, '!target' => $target_version, '!n' => count($steps), '!steps' => implode(', ', $steps))), 'ok');
  }

  drush_set_option('txp_upgrade_source_version', $source_version);
  drush_set_option('txp_upgrade_target_version', $target_version);
}

/**
 * Implements drush_hook_pre_COMMAND() (doubled name — not collapsed).
 *
 * The source safety copy: live, never taken offline (clone semantics), and in
 * the self-contained mode this layer forces for every copy that a person may
 * later be offered as a restore point. Left to resolve on its own it comes out
 * modeless, and a modeless archive carries the database through a transient
 * directory rather than inside the file.
 */
function drush_provision_txp_pre_provision_txp_upgrade($new_name = NULL, $platform = NULL) {
  // The classic dump creates the file with fopen($path, 'x'), which FAILS on
  // an existing file, so a dump left behind by a killed earlier backup would
  // wedge every later upgrade of this site. Clear it first.
  if (is_file(d()->site_path . '/database.sql')) {
    provision_file()->unlink(d()->site_path . '/database.sql')
      ->succeed('Removed a stale database.sql before the upgrade safety copy')
      ->fail('Could not remove a stale database.sql before the upgrade safety copy');
  }
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_with_mysqldump');
  }
  drush_set_option('selected_backup_mode', 'site_files_with_mysqldump');
  drush_set_option('backup_mode', 'site_files_with_mysqldump');
  drush_log(dt('Upgrade source copy forced to the restorable mode: site_files_with_mysqldump'), 'info');
  drush_log(dt('Invoking drush provision-backup in drush_provision_txp_pre_provision_txp_upgrade'));
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, "Internal-DB-Backup \n")
      ->succeed('Generated blank Internal-DB-Backup Flag-File (txp upgrade)')
      ->fail('Could not generate Internal-DB-Backup Flag-File (txp upgrade)');
  }
  drush_invoke('provision-backup');
}

/**
 * Implements drush_hook_pre_COMMAND_rollback().
 */
function drush_provision_txp_pre_provision_txp_upgrade_rollback($new_name = NULL, $platform = NULL) {
  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused upgrade site package')
    ->fail('Could not remove unused upgrade site package');
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (is_file($internal_backup_flag)) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in rollback')
      ->fail('Could not remove Internal-DB-Backup Flag-File in rollback');
  }
}

/**
 * Implements drush_hook_COMMAND() (collapsed name).
 *
 * The clone chain, then the conversion: save the copy's alias on the target
 * platform, deploy the source archive into it with the identity-preserving
 * deploy class, run the update chain, prove the schema, reconcile the version
 * row, verify.
 */
function drush_provision_txp_upgrade($new_name = NULL, $platform = NULL) {
  drush_set_option('old_platform', d()->platform->name);

  $new_uri = ltrim($new_name, '@');
  $target_root = d($platform)->root;
  $copy_path = $target_root . '/sites/' . $new_uri;
  $target_version = drush_get_option('txp_upgrade_target_version', '');
  $source_version = drush_get_option('txp_upgrade_source_version', '');

  $options = d()->options;
  $options['uri'] = $new_uri;
  $hash_name = drush_get_option('#name') ? '#name' : 'name';
  $options[$hash_name] = $new_name;
  $options['platform'] = $platform;
  $options['root'] = $target_root;
  $options['aliases'] = array();
  $options['redirection'] = 0;

  // The copy ships dark: no SSL/LE (keys are bound to the source name) and no
  // CDN. Cutover is where it takes over the source's name and its encryption.
  $options['ssl_enabled'] = 0;
  unset($options['ssl_key']);
  $options['cdn'] = 0;
  unset($options['cdn']);

  // Never carry the source's package map onto the copy's alias: the copy's own
  // verify rebuilds it from the target core, and a stale map on the saved
  // context would only mislead later consumers.
  unset($options['packages']);

  // Auto-disable any migration-source association: the copy gets a brand-new
  // database user with no grant on the referenced database, so a carried
  // association composes a connection it cannot open. The wired latch is the
  // SOURCE site's history, not the copy's.
  unset($options['migrate_source_db']);
  unset($options['migrate_source_uri']);
  unset($options['migrate_source_wired']);

  if ($db_server = drush_get_option('new_db_server', FALSE)) {
    $options['db_server'] = $db_server;
  }

  drush_invoke_process('@none', 'provision-save', array($new_name), $options);

  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'txp_upgrade');

  // Match the transport the safety copy above was written with. The deploy
  // runs as a SEPARATE backend process and resolves the backup mode from
  // scratch: without this it resolves to modeless, takes the fast-import
  // branch and looks for a dump directory a mysqldump-mode backup never wrote.
  // Derived from the resolver rather than repeating the literal, so the two
  // halves cannot disagree, and set AFTER the alter hook so a contributed
  // alter cannot drop it.
  $resolved_backup_mode = provision_backup_mode_resolve();
  if (!empty($resolved_backup_mode)) {
    $deploy_options['selected_backup_mode'] = $resolved_backup_mode;
  }

  // 'upgrade' is the identity-PRESERVING deploy class: the copy is the same
  // publication under a temporary name, so blog_uid (the feed GUID seed) and
  // the contact data stay, while the sessions and the uri-bound paths move.
  // client_email travels with it because this command runs on the SOURCE
  // alias and the deploy is a separate backend process that would otherwise
  // never see it. Both set AFTER the alter for the same reason the backup
  // mode is: a contributed alter must not be able to drop them.
  $deploy_options['deploy_class'] = 'upgrade';
  $deploy_options['client_email'] = drush_get_option('client_email', '');

  provision_backend_invoke($new_name, 'provision-deploy', array(drush_get_option('backup_file')), $deploy_options);

  if (!drush_get_error()) {
    _provision_txp_upgrade_convert($copy_path, $target_root, $new_uri, $source_version, $target_version);
  }

  if (!drush_get_error()) {
    provision_backend_invoke($new_name, 'provision-verify');

    $aegir_root = d('@server_master')->aegir_root;
    if (!is_file($aegir_root . '/static/control/FastTrack.info')) {
      sleep(5); // A small trick to avoid high load and race conditions.
      // Re-verify the source site via the frontend, mirroring clone (#1004526).
      provision_backend_invoke('@hostmaster', 'hosting-task', array('@' . d()->uri, 'verify'), array('force' => TRUE));
    }

    $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_txp_upgrade')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_txp_upgrade');
  }

  provision_reload_config('site', d()->site_path . '/drushrc.php');
}

/**
 * Implements drush_hook_COMMAND_rollback() (collapsed name).
 *
 * Discard the COPY, nothing else. The source was never in the write path: it
 * kept serving throughout, its database was only ever read, and its directory
 * was only ever archived.
 *
 * The copy's credentials are read into LOCALS from its own persisted
 * drushrc.php and never written into the 'site' option context: that context
 * holds the SOURCE's credentials, and a database teardown reading it would
 * drop the source's database.
 */
function drush_provision_txp_upgrade_rollback($new_name = NULL, $platform = NULL) {
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_txp_upgrade_rollback')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_txp_upgrade_rollback');
  }

  $new_uri = is_string($new_name) ? ltrim($new_name, '@') : '';
  if ($new_uri === '' || !is_string($platform) || $platform === '') {
    drush_log(dt('provision-txp-upgrade failed before the copy was named; the source site !uri is untouched and still serving.',
      array('!uri' => d()->uri)), 'warning');
    return;
  }
  $target = d($platform);
  $target_root = (is_object($target) && isset($target->root)) ? $target->root : '';
  $copy_path = ($target_root === '') ? '' : $target_root . '/sites/' . $new_uri;

  // The copy's database, through the copy's OWN db service (a copy may have
  // been placed on another db server) and with the copy's own credentials.
  $alias_file = drush_server_home() . '/.drush/' . $new_uri . '.alias.drushrc.php';
  if ($copy_path !== '' && is_file($alias_file)) {
    $creds = _provision_txp_creds_from_drushrc($copy_path . '/drushrc.php');
    $source_db = drush_get_option('db_name', '', 'site');
    if (!empty($creds['db_name']) && $creds['db_name'] !== $source_db) {
      d('@' . $new_uri)->service('db')->destroy_site_database($creds);
      drush_log(dt('Dropped the copy database and revoked its grants.'), 'ok');
    }
    elseif (!empty($creds['db_name'])) {
      // Identical names mean the copy never got its own database; leaving the
      // source's alone is the only safe reading.
      drush_log(dt('The copy records the source\'s database name; left untouched.'), 'advisory');
    }
  }

  if ($copy_path !== '' && is_dir($copy_path)) {
    _provision_recursive_delete($copy_path);
    drush_log(dt('Removed the half-built copy at !path.', array('!path' => $copy_path)), 'ok');
  }
  if (is_file($alias_file)) {
    // Any vhost a verify leg managed to write for the copy: it now names a
    // directory that no longer exists, so it must go before the alias that
    // resolves it does.
    $copy_context = d('@' . $new_uri);
    if (is_object($copy_context)) {
      $copy_context->service('http')->delete_config('site');
      $copy_context->service('http')->parse_configs();
    }
    drush_invoke_process('@none', 'provision-save', array($new_name), array('delete' => TRUE));
  }

  drush_log(dt('provision-txp-upgrade failed; the source site !uri is untouched and still serving (it was never in the write path). The copy has been discarded — fix the cause from the log above and re-run the upgrade.',
    array('!uri' => d()->uri)), 'warning');
}

/**
 * Run the CMS update chain on the copy, then prove and finish the result.
 *
 * Sequence, and the reason for it: the chain first (it is the only thing
 * allowed to change the schema), then the structure assertion (the falsifiable
 * verdict -- a chain that aborted mid-way reports nothing of its own), then the
 * version reconcile (bookkeeping the chain can legitimately have lost, and
 * which may only be written once the schema has been proved).
 *
 * A chain that reported a diagnostic is NOT the end of the task. Its own
 * land mine -- the language pass, which runs while an error handler that
 * throws on any diagnostic is still installed, and BEFORE the version row is
 * rewritten -- produces exactly that: a run whose schema work completed and
 * whose bookkeeping did not. The two proofs below tell that apart from a real
 * abort without trusting the chain's word for anything: the schema is compared
 * against the target core's own declarations, and the update stamp (written as
 * the LAST act inside the chain's try block, so a failed step never reaches it)
 * must be from this run. A copy that passes both is complete; the diagnostic
 * still goes on the task, as an advisory, because something did go wrong even
 * though the result is sound -- which is also why the chain's rows are held
 * back until the proofs have spoken and then levelled by what they said.
 */
function _provision_txp_upgrade_convert($copy_path, $target_root, $new_uri, $source_version, $target_version) {
  $php_bin = _provision_txp_php_bin();
  $shim = dirname(__FILE__) . '/txp/resources/txp-update-shim.php';
  $config_php = $copy_path . '/private/config.php';
  if (!is_readable($config_php)) {
    return drush_set_error('PROVISION_TXP_UPGRADE_CHAIN_FAILED',
      dt('The deployed copy has no readable private/config.php at !file.', array('!file' => $config_php)));
  }
  $prefix = _provision_txp_config_prefix($config_php);

  drush_log(dt('Running the Textpattern update chain on the copy !uri (!source -> !target).',
    array('!uri' => $new_uri, '!source' => $source_version, '!target' => $target_version)), 'ok');

  // Captured before the chain so the update stamp it writes can be told apart
  // from one an earlier run left behind.
  $run_start = time();
  $chain_ok = drush_shell_exec('%s %s --config-php=%s --txpath=%s --uri=%s',
    $php_bin, $shim, $config_php, $target_root . '/textpattern', $new_uri);
  $output = (array) drush_shell_exec_output();
  $tail = array();
  $rows = array();
  foreach ($output as $line) {
    $rows[] = 'TXP/UPDATE ' . $line;
    if (preg_match('/^([A-Z_]+)=(.*)$/', trim($line), $m)) {
      $tail[$m[1]] = $m[2];
    }
  }
  if ($chain_ok) {
    // 'ok' so the conversion record is visible in a normal task log, not only
    // under --verbose. A clean chain has nothing to hold back.
    foreach ($rows as $row) {
      drush_log($row, 'ok');
    }
  }

  $version_after = isset($tail['VERSION_AFTER']) ? trim($tail['VERSION_AFTER']) : '';
  $dbupdatetime = isset($tail['DBUPDATETIME']) ? trim($tail['DBUPDATETIME']) : '';

  $connect_error = '';
  $creds = _provision_txp_creds_from_drushrc($copy_path . '/drushrc.php');
  $mysqli = _provision_txp_db_connect($creds, $connect_error);
  if ($mysqli === FALSE) {
    return drush_set_error('PROVISION_TXP_UPGRADE_CHAIN_FAILED',
      dt('Could not reach the copy\'s database after the update chain (!msg). The source is untouched.',
      array('!msg' => $connect_error)));
  }

  $proved = _provision_txp_upgrade_assert_structure($mysqli, $target_root, $prefix);
  if ($proved) {
    $proved = _provision_txp_upgrade_reconcile_version($mysqli, $prefix, $target_version, $version_after, $dbupdatetime, $run_start);
  }
  $mysqli->close();

  if (!$chain_ok) {
    // Held back until the two proofs had spoken, because the chain's exit code
    // is not the verdict: its own land mine reports a diagnostic on a run whose
    // schema work completed. Where that is what happened, these rows are an
    // operator note on a task that succeeded -- levelling them 'warning' would
    // colour a completed upgrade yellow and teach operators to ignore the
    // colour. Where a proof failed, the error is already set and they are part
    // of the explanation.
    $level = $proved ? 'advisory' : 'warning';
    foreach ($rows as $row) {
      drush_log($row, $level);
    }
  }
  if (!$proved) {
    // The assertion or the reconcile has already set the error and said what
    // it saw; a chain that also reported a diagnostic is now explained by it.
    return FALSE;
  }
  if (!$chain_ok) {
    drush_log(dt('The update chain reported the diagnostic above and did not finish its own bookkeeping. The copy\'s schema has been proved independently against the target core and its version row written here, so the upgrade is complete — read the rows above before cutting over to it.'), 'advisory');
  }
  return TRUE;
}

/**
 * The falsifiable verdict: does the copy's schema carry what the target core
 * declares?
 *
 * Textpattern declares its schema as CREATE bodies under
 * textpattern/vendors/Textpattern/DB/Tables/*.table (one file per table, read
 * by Textpattern\DB\Core::getStructure(); the file name is the table name).
 * Every column and index named there is compared against
 * information_schema for the copy's own database.
 *
 * Missing table, column or index is a failure: it means a step did not run, or
 * ran and aborted. Type, length and collation differences are RECORDED
 * TOLERANCE -- an advisory row, never a failure and never a heal -- because a
 * site that has lived through several releases legitimately carries wider
 * columns and older collations, and because healing them here would be this
 * layer inventing schema changes the CMS never asked for.
 *
 * Type and length differences get a row each (they are rare and each one is a
 * different fact). Collation drift is reported one row per TABLE listing the
 * columns: it comes from how the table was created, so a drifted table drifts
 * in every text column at once and a row each would bury the rest of the log
 * under a single fact repeated a hundred times.
 *
 * Columns and indexes the core does NOT declare are ignored: plugins add them.
 */
function _provision_txp_upgrade_assert_structure($mysqli, $target_root, $prefix) {
  $tables_dir = $target_root . '/textpattern/vendors/Textpattern/DB/Tables';
  $files = glob($tables_dir . '/*.table');
  if (!is_array($files) || empty($files)) {
    drush_set_error('PROVISION_TXP_UPGRADE_STRUCTURE',
      dt('The target core declares no table structures at !dir; the upgrade cannot be proved.',
      array('!dir' => $tables_dir)));
    return FALSE;
  }

  // The database's own default collation is the only baseline a .table file
  // can be compared against: the files declare no collation, the connection
  // does, and a column left behind on an older collation by a past upgrade is
  // exactly the drift worth naming.
  $default_collation = '';
  $rs = $mysqli->query("SELECT DEFAULT_COLLATION_NAME FROM information_schema.SCHEMATA WHERE SCHEMA_NAME = DATABASE()");
  if ($rs !== FALSE && ($row = $rs->fetch_row())) {
    $default_collation = (string) $row[0];
  }

  $failures = array();
  $advisories = array();
  $collation_drift = array();
  $checked = 0;
  foreach ($files as $file) {
    $table = $prefix . basename($file, '.table');
    $declared = _provision_txp_upgrade_parse_table(file_get_contents($file));
    if (empty($declared['columns'])) {
      continue;
    }
    if (!_provision_txp_table_exists($mysqli, $table)) {
      $failures[] = dt('table !table is missing', array('!table' => $table));
      continue;
    }
    $checked++;

    $have = array();
    $stmt = $mysqli->prepare("SELECT COLUMN_NAME, DATA_TYPE, CHARACTER_MAXIMUM_LENGTH, COLLATION_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ?");
    $stmt->bind_param('s', $table);
    $stmt->execute();
    $stmt->bind_result($col_name, $col_type, $col_length, $col_collation);
    while ($stmt->fetch()) {
      $have[$col_name] = array(
        'type' => strtolower((string) $col_type),
        'length' => $col_length,
        'collation' => (string) $col_collation,
      );
    }
    $stmt->close();

    foreach ($declared['columns'] as $name => $spec) {
      if (!isset($have[$name])) {
        $failures[] = dt('!table.!column is missing', array('!table' => $table, '!column' => $name));
        continue;
      }
      $actual = $have[$name];
      if ($spec['type'] !== '' && $actual['type'] !== $spec['type']) {
        $advisories[] = dt('!table.!column is !actual where the core declares !declared',
          array('!table' => $table, '!column' => $name, '!actual' => $actual['type'], '!declared' => $spec['type']));
      }
      elseif ($spec['type'] === 'varchar' && !is_null($spec['length'])
        && !is_null($actual['length']) && (int) $actual['length'] !== (int) $spec['length']) {
        $advisories[] = dt('!table.!column is varchar(!actual) where the core declares varchar(!declared)',
          array('!table' => $table, '!column' => $name, '!actual' => (int) $actual['length'], '!declared' => (int) $spec['length']));
      }
      if ($default_collation !== '' && $actual['collation'] !== '' && $actual['collation'] !== $default_collation) {
        if (!isset($collation_drift[$table])) {
          $collation_drift[$table] = array('collation' => $actual['collation'], 'columns' => array());
        }
        $collation_drift[$table]['columns'][] = $name;
      }
    }

    if (!empty($declared['indexes'])) {
      $seen = array();
      $stmt = $mysqli->prepare("SELECT DISTINCT INDEX_NAME FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ?");
      $stmt->bind_param('s', $table);
      $stmt->execute();
      $stmt->bind_result($index_name);
      while ($stmt->fetch()) {
        $seen[(string) $index_name] = TRUE;
      }
      $stmt->close();
      foreach ($declared['indexes'] as $index) {
        if (!isset($seen[$index])) {
          $failures[] = dt('index !index on !table is missing', array('!index' => $index, '!table' => $table));
        }
      }
    }
  }

  foreach ($advisories as $note) {
    drush_log(dt('TXP/STRUCTURE tolerated difference: !note.', array('!note' => $note)), 'advisory');
  }
  foreach ($collation_drift as $table => $drift) {
    drush_log(dt('TXP/STRUCTURE tolerated difference: !table collates as !actual where the database defaults to !default (!n column(s): !columns).',
      array(
        '!table' => $table,
        '!actual' => $drift['collation'],
        '!default' => $default_collation,
        '!n' => count($drift['columns']),
        '!columns' => implode(', ', $drift['columns']),
      )), 'advisory');
  }
  if (!empty($failures)) {
    drush_set_error('PROVISION_TXP_UPGRADE_STRUCTURE',
      dt('The upgraded copy does not carry the schema the target core declares: !list. The update chain did not complete; the source is untouched — discard the copy and retry.',
      array('!list' => implode('; ', $failures))));
    return FALSE;
  }
  drush_log(dt('Structure assertion passed: !n table(s) carry every column and index the target core declares.',
    array('!n' => $checked)), 'ok');
  return TRUE;
}

/**
 * Parse one .table file into its declared column and index names.
 *
 * The files are CREATE bodies: one column per line ("<name> <TYPE>[(len)] ..."),
 * then the key lines (PRIMARY KEY, INDEX/UNIQUE/FULLTEXT <name>). Key lines are
 * matched FIRST and by a name-plus-parenthesis shape, so a column called
 * Keywords is never mistaken for a KEY clause.
 */
function _provision_txp_upgrade_parse_table($body) {
  $out = array('columns' => array(), 'indexes' => array());
  if (!is_string($body) || $body === '') {
    return $out;
  }
  $lines = preg_split('/\r\n|\r|\n/', $body);
  foreach ($lines as $line) {
    $line = rtrim(trim($line), ',');
    if ($line === '') {
      continue;
    }
    if (preg_match('/^PRIMARY\s+KEY\s*\(/i', $line)) {
      $out['indexes'][] = 'PRIMARY';
      continue;
    }
    if (preg_match('/^(INDEX|UNIQUE|FULLTEXT|KEY)\s+([A-Za-z0-9_]+)\s*\(/i', $line, $m)) {
      $out['indexes'][] = $m[2];
      continue;
    }
    if (preg_match('/^([A-Za-z0-9_]+)\s+([A-Za-z]+)\s*(\(\s*([0-9]+)\s*\))?/', $line, $m)) {
      $out['columns'][$m[1]] = array(
        'type' => strtolower($m[2]),
        'length' => isset($m[4]) && $m[4] !== '' ? (int) $m[4] : NULL,
      );
    }
  }
  return $out;
}

/**
 * Finish the version bookkeeping the chain can legitimately have lost.
 *
 * _update.php writes its update stamp INSIDE the try block and the version
 * pref AFTER the language loop -- and that loop runs while the chain's own
 * error handler, which throws on ANY diagnostic, is still installed. A single
 * notice from a language pack therefore leaves a database whose schema work is
 * complete and whose version row still names the old release, which on the
 * next admin request makes the chain run AGAIN from that old version. The
 * steps are not idempotent (_to_4.0.3.php least of all), so that re-run is the
 * damage this function exists to prevent.
 *
 * Two facts must both hold before the row is written: the structure assertion
 * passed (the caller gates on it), and the update stamp is from THIS run. A
 * stale stamp means the chain did not reach the end of its try block at all,
 * which is a failure, not a bookkeeping gap.
 *
 * The row is written with create_pref()'s exact shape from
 * textpattern/lib/txplib_misc.php: a global (user_name = '') PREF_HIDDEN
 * (type 2) 'publish' row. The chain is never re-run.
 */
function _provision_txp_upgrade_reconcile_version($mysqli, $prefix, $target_version, $version_after, $dbupdatetime, $run_start) {
  if ($version_after === $target_version) {
    drush_log(dt('The copy records Textpattern !v.', array('!v' => $version_after)), 'ok');
    return TRUE;
  }
  if ($dbupdatetime === '' || !ctype_digit($dbupdatetime) || (int) $dbupdatetime < $run_start) {
    return drush_set_error('PROVISION_TXP_UPGRADE_CHAIN_FAILED',
      dt('The copy still records Textpattern !have and carries no update stamp from this run; the chain did not complete. The source is untouched — discard the copy and retry.',
      array('!have' => $version_after === '' ? dt('nothing') : $version_after)));
  }

  $table = '`' . $mysqli->real_escape_string($prefix . 'txp_prefs') . '`';
  $written = FALSE;
  try {
    // remove_pref('version', 'publish') with no user_name argument: core's own
    // call adds no user_name clause, so a per-user row left behind by an
    // adopted install goes too. Matching it exactly is the point -- a row this
    // delete misses is a row the next admin request reads instead of ours.
    $mysqli->query("DELETE FROM " . $table . " WHERE name = 'version' AND event = 'publish'");
    $stmt = $mysqli->prepare("INSERT INTO " . $table . " (name, val, type, event, collection, html, position, user_name) VALUES ('version', ?, 2, 'publish', '', 'text_input', 0, '')");
    $stmt->bind_param('s', $target_version);
    $stmt->execute();
    $stmt->close();
    $written = (_provision_txp_db_version($mysqli, $prefix) === $target_version);
  }
  catch (Exception $e) {
    $written = FALSE;
  }
  if (!$written) {
    return drush_set_error('PROVISION_TXP_UPGRADE_CHAIN_FAILED',
      dt('The schema is at !target but the version row could not be rewritten; the next admin request would replay the whole update chain. Discard the copy and retry.',
      array('!target' => $target_version)));
  }
  drush_log(dt('Recorded Textpattern !target on the copy: the schema assertion passed and this run wrote the update stamp, but the chain lost its own version write (its language pass runs under an error handler that throws on any diagnostic, before the version row is rewritten). Written once, here; the chain is never re-run.',
    array('!target' => $target_version)), 'ok');
  return TRUE;
}

/**
 * Recompute the update steps _update.php will include, from the TARGET core.
 *
 * The step list is read out of the target's own _update.php rather than
 * repeated here, so a core that adds a step is reported truthfully. The
 * selection is the chain's: a step runs when the database version is below it
 * AND the step is at or below the codebase's base version (the part before any
 * -dev suffix), and each step that runs advances the comparison version.
 *
 * Returns the ordered step list, or FALSE when the target's list is unreadable.
 */
function _provision_txp_upgrade_steps($target_root, $source_version, $target_version) {
  $update_php = $target_root . '/textpattern/update/_update.php';
  if (!is_readable($update_php)) {
    return FALSE;
  }
  $body = file_get_contents($update_php);
  if (!preg_match('/\\$dbupdates\\s*=\\s*array\\s*\\((.*?)\\)\\s*;/s', $body, $m)) {
    return FALSE;
  }
  if (!preg_match_all('/[\'"]([0-9][0-9.]*)[\'"]/', $m[1], $found)) {
    return FALSE;
  }
  $parts = explode('-', $target_version);
  $base_version = $parts[0];
  $steps = array();
  $at = $source_version;
  foreach ($found[1] as $step) {
    if (version_compare($at, $step, '<') && version_compare($step, $base_version, '<=')) {
      $steps[] = $step;
      $at = $step;
    }
  }
  return $steps;
}
