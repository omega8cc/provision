<?php

/**
 * @file
 * provision-txp-rename — same-platform rename (TXP layer).
 *
 * A Textpattern site IS its directory under the shared core: sites/<uri>/, with
 * the uri as the directory name, the vhost name, the context name and the value
 * of half a dozen stored preferences. Renaming it is therefore a path move plus
 * a re-point of every uri-bound surface, not a deploy: the database, its
 * contents and its identity (blog_uid, image ids, AUTO_INCREMENTs) never move
 * and are never rewritten.
 *
 * The order below is the safe one. The move happens first, because everything
 * after it is idempotent and re-runnable while a half-written context pointing
 * at a directory that has not moved is not. The old context and the old vhost
 * go LAST, after the new name has been verified and is serving: until that
 * point the old alias is what a rollback restores the site through.
 *
 * Downtime honesty: the original domain stops serving the moment the directory
 * moves and the new one starts when its vhost is written by the verify leg, so
 * the window covers the re-point and one verify. There is no maintenance mode
 * to hold the door with -- Textpattern has none, and this layer's disable is
 * vhost-level only.
 *
 * Encryption: SSL keys are bound to the old name, so the new context is saved
 * with encryption off. Re-enabling it for the new name is a deliberate
 * operator step and is called out in the task log.
 */

/**
 * Implements drush_hook_COMMAND_validate() (collapsed name).
 */
function drush_provision_txp_rename_validate($new_name = NULL) {
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_TXP_RENAME_FAILED',
      dt('provision-txp-rename must run against a site context.'));
  }
  if (!provision_platform_is_txp(d())) {
    return drush_set_error('PROVISION_TXP_RENAME_FAILED',
      dt('This site is not on a Textpattern platform.'));
  }
  if (_provision_txp_php_bin() === FALSE) {
    return drush_set_error('PROVISION_TXP_PHP_TOO_OLD',
      dt('No PHP >= 8.4 binary found; TXP tasks refuse to run below the enforced floor.'));
  }
  if (!_provision_txp_site_exists()) {
    return drush_set_error('PROVISION_TXP_RENAME_FAILED',
      dt('No Textpattern site found at !site.', array('!site' => d()->site_path)));
  }
  // Clear a stale frontend backup-mode ctrl file before the safety copy runs:
  // inheriting a dead task's mode would produce an archive that cannot restore
  // the site this task is about to move.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }

  if (!is_string($new_name) || $new_name === '') {
    return drush_set_error('PROVISION_TXP_RENAME_BAD_NAME', dt('No new uri given.'));
  }
  $new_uri = strtolower(ltrim($new_name, '@'));
  if ($new_uri === strtolower(d()->uri)) {
    return drush_set_error('PROVISION_TXP_RENAME_BAD_NAME',
      dt('The new uri is the site\'s current uri; nothing to rename.'));
  }
  // Conservative hostname shape check. The frontend runs its own FQDN
  // validator; this catches a mangled hand-run invocation before a directory
  // is created from the value.
  if (!preg_match('/^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/', $new_uri)
    || strpos($new_uri, '..') !== FALSE) {
    return drush_set_error('PROVISION_TXP_RENAME_BAD_NAME',
      dt('The new uri !uri is not a plausible host name.', array('!uri' => $new_uri)));
  }
  if ($new_uri === 'hostmaster' || $new_uri === strtolower(d('@hostmaster')->uri)) {
    return drush_set_error('PROVISION_TXP_RENAME_BAD_NAME',
      dt('Refusing to take the hosting front-end\'s name.'));
  }
  if (file_exists(d()->root . '/sites/' . $new_uri)) {
    return drush_set_error('PROVISION_TXP_RENAME_BAD_NAME',
      dt('sites/@uri already exists on this platform.', array('@uri' => $new_uri)));
  }
  // A context that already holds the name is refused -- unless it demonstrably
  // serves nothing, its recorded site directory being gone. That leftover is
  // exactly what a rolled-back rename leaves behind (the early provision-save
  // mints the new-name alias; the rollback removes it only when it can see
  // it), and the cutover's documented recovery is to re-run, whose second leg
  // is this very rename. Refusing on our own debris would make that recovery
  // impossible; the rename's own provision-save overwrites the file.
  if (provision_file()->exists(drush_server_home() . '/.drush/' . $new_uri . '.alias.drushrc.php')->status()) {
    $leftover = d('@' . $new_uri);
    $leftover_dir = (is_object($leftover) && isset($leftover->site_path) && is_string($leftover->site_path))
      ? $leftover->site_path : '';
    if ($leftover_dir === '' || provision_file()->exists($leftover_dir)->status()) {
      return drush_set_error('PROVISION_TXP_RENAME_BAD_NAME',
        dt('A backend context already holds !uri.', array('!uri' => $new_uri)));
    }
    drush_log(dt('A stale !uri alias with no site directory is left over (a rolled-back rename); this rename overwrites it.',
      array('!uri' => $new_uri)), 'info');
  }
}

/**
 * Implements drush_hook_pre_COMMAND() (doubled name — not collapsed).
 *
 * The safety copy, in the self-contained mode this layer forces wherever the
 * copy may be offered back to a person as a restore point. It is taken with
 * the site live and its database included: the rename rewrites preference rows
 * in that database, so a files-only copy would not be a restore point at all.
 */
function drush_provision_txp_pre_provision_txp_rename($new_name = NULL) {
  if (is_file(d()->site_path . '/database.sql')) {
    provision_file()->unlink(d()->site_path . '/database.sql')
      ->succeed('Removed a stale database.sql before the rename safety copy')
      ->fail('Could not remove a stale database.sql before the rename safety copy');
  }
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_with_mysqldump');
  }
  drush_set_option('selected_backup_mode', 'site_files_with_mysqldump');
  drush_set_option('backup_mode', 'site_files_with_mysqldump');
  drush_log(dt('Rename safety copy forced to the restorable mode: site_files_with_mysqldump'), 'info');
  drush_log(dt('Invoking drush provision-backup in drush_provision_txp_pre_provision_txp_rename'));
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, "Internal-DB-Backup \n")
      ->succeed('Generated blank Internal-DB-Backup Flag-File (txp rename)')
      ->fail('Could not generate Internal-DB-Backup Flag-File (txp rename)');
  }
  drush_invoke('provision-backup');
}

/**
 * Implements drush_hook_pre_COMMAND_rollback().
 */
function drush_provision_txp_pre_provision_txp_rename_rollback($new_name = NULL) {
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (is_file($internal_backup_flag)) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in rollback')
      ->fail('Could not remove Internal-DB-Backup Flag-File in rollback');
  }
}

/**
 * Implements drush_hook_COMMAND() (collapsed name).
 */
function drush_provision_txp_rename($new_name = NULL) {
  $old_uri = d()->uri;
  $old_name = d()->name;
  $old_path = d()->site_path;
  $root = d()->root;
  $new_uri = strtolower(ltrim($new_name, '@'));
  $new_path = $root . '/sites/' . $new_uri;

  // The database credentials enter the 'site' option context only at install;
  // every later task reloads them from the persisted drushrc.php. That file is
  // INSIDE the directory about to move, so the reload has to happen while the
  // old path still exists -- afterwards there is nothing to read and both the
  // config re-render and the re-point below would run credential-less.
  _provision_txp_ensure_db_creds_loaded();

  // 1. The move. Same platform, same filesystem, so rename() is atomic and the
  // relative admin symlinks inside the tree keep resolving at the same depth.
  if (!@rename($old_path, $new_path)) {
    return drush_set_error('PROVISION_TXP_RENAME_FAILED',
      dt('Could not move !old to !new.', array('!old' => $old_path, '!new' => $new_path)));
  }
  drush_set_option('txp_rename_moved', TRUE);
  drush_log(dt('Moved the site directory to !new.', array('!new' => $new_path)), 'ok');

  // 2. The new context. Encryption off (keys are bound to the old name),
  // aliases and redirection reset, CDN dropped: a name change invalidates all
  // three, and carrying them would point the new name at the old one's
  // certificate and redirects.
  $options = d()->options;
  $options['uri'] = $new_uri;
  $hash_name = drush_get_option('#name') ? '#name' : 'name';
  $options[$hash_name] = $new_name;
  $options['aliases'] = array();
  $options['redirection'] = 0;
  $options['ssl_enabled'] = 0;
  unset($options['ssl_key']);
  $options['cdn'] = 0;
  unset($options['cdn']);
  drush_invoke_process('@none', 'provision-save', array($new_name), $options);

  // The context must resolve to the directory we just created, or everything
  // below would write somewhere else entirely.
  $new_context = d($new_name);
  if (!is_object($new_context) || $new_context->site_path !== $new_path) {
    return drush_set_error('PROVISION_TXP_RENAME_FAILED',
      dt('The saved context for !name does not resolve to !path.', array('!name' => $new_name, '!path' => $new_path)));
  }

  // 3. private/config.php, re-rendered from the NEW context by the settings
  // writer -- the single authority for that file. It carries the site's own
  // multisite_root_path, admin_url, cookie_domain and txpath, every one of
  // them uri-bound or path-bound.
  $settings = new Provision_Config_Txp_Settings($new_name, drush_get_context('site'));
  $settings->write();
  drush_log(dt('Re-rendered private/config.php from the new context.'), 'ok');

  // 4. Prove the write landed where it was meant to. Everything below reads
  // that file (the table prefix) or depends on the site being able to connect
  // at all, so a silent write failure must stop here rather than three steps
  // further on.
  if (!is_readable($new_path . '/private/config.php')) {
    return drush_set_error('PROVISION_TXP_RENAME_FAILED',
      dt('The moved site has no readable private/config.php at !path.', array('!path' => $new_path)));
  }

  // 5. The database side: the same database, the same content, the same
  // identity -- only the uri-bound and path-bound rows move, plus the session
  // invalidation a name change requires. blog_uid, txp_image ids and every
  // AUTO_INCREMENT are deliberately untouched.
  $connect_error = '';
  $mysqli = _provision_txp_db_connect(_provision_txp_site_creds(), $connect_error);
  if ($mysqli === FALSE) {
    return drush_set_error('PROVISION_TXP_RENAME_FAILED',
      dt('Could not reach the site database to re-point it (!msg).', array('!msg' => $connect_error)));
  }
  $prefix = _provision_txp_config_prefix($new_path . '/private/config.php');
  try {
    _provision_txp_truncate_sessions($mysqli, $prefix);
    $rotated = _provision_txp_rotate_nonces($mysqli, $prefix);
    $repointed = _provision_txp_repoint_uri($mysqli, $new_uri, $new_path, $prefix);
    $mysqli->close();
  }
  catch (Exception $e) {
    // Deliberately not closed here: mysqli is in exception mode, so closing a
    // link that has just failed can throw a second time and bury the first
    // message. The process ends in moments either way.
    return drush_set_error('PROVISION_TXP_RENAME_FAILED',
      dt('Re-pointing the database at !uri failed: !msg', array('!uri' => $new_uri, '!msg' => $e->getMessage())));
  }
  drush_log(dt('Re-pointed the site at !uri and rotated the session nonce of !n user row(s); tokens, logs and comment nonces cleared, identity and content untouched.',
    array('!uri' => $new_uri, '!n' => $rotated)), 'ok');
  if (!empty($repointed['created'])) {
    drush_log(dt('These prefs rows were absent and have been recreated with core metadata: !list.',
      array('!list' => implode(', ', $repointed['created']))), 'ok');
  }
  if (!empty($repointed['missing'])) {
    drush_log(dt('These prefs rows could be neither updated nor created: !list. Textpattern recreates them from defaults on the next admin login (siteurl defaults to the ADMIN host) -- fix them in the Preferences panel.',
      array('!list' => implode(', ', $repointed['missing']))), 'warning');
  }

  // 6. Serve the new name: verify renders its vhost and runs the tree repair
  // (_provision_txp_create_directories: the four admin symlinks, the copy set
  // and the ownership model) in the new context's own process, which is the
  // only place that helper can resolve the moved site.
  provision_backend_invoke($new_name, 'provision-verify');
  if (drush_get_error()) {
    return drush_set_error('PROVISION_TXP_RENAME_FAILED',
      dt('Verify failed on the renamed site !uri; the old context is still in place.', array('!uri' => $new_uri)));
  }

  // 7. The per-account static file store follows the name, the way a migrate
  // under a new name does it. The store is root-managed, so both halves go
  // through the privileged wrapper: the first gives the new name its OWN store
  // (the old name's content is copied into it and the in-site links are
  // re-pointed), the second sets the old name's store aside under
  // static/files/.archived. Position is load-bearing on both sides -- the
  // wrapper resolves the account from the vhost the verify above just wrote,
  // and it proves ownership of the old store through the alias that is deleted
  // in step 8. Warn-not-fail: a store that could not follow leaves the site
  // serving from the one it has.
  _provision_drupal_native_symlink($new_uri, TRUE);

  // 8. Retire the old name. Deliberately last, and deliberately not fatal: at
  // this point the site is serving under its new name, and a leftover vhost or
  // alias is a tidiness problem, never a reason to roll a working rename back.
  d()->service('http')->delete_config('site')
    ->succeed('Deleted the old site configuration file')
    ->fail('Failed to delete the old site configuration file');
  d()->service('http')->parse_configs();
  _provision_drupal_archive_static_store($old_uri, 'RENAME');
  drush_invoke_process('@none', 'provision-save', array($old_name), array('delete' => TRUE));
  foreach (d()->drush_aliases as $alias) {
    drush_invoke_process('@none', 'provision-save', array($alias), array('delete' => TRUE));
  }
  // The old context is gone; nothing may re-save it on the way out.
  drush_set_option('provision_save_config', FALSE);

  drush_log(dt('!old now answers at !new.', array('!old' => $old_uri, '!new' => $new_uri)), 'ok');
  drush_log(dt('Encryption is OFF for !new (the rename disables it — keys are name-bound). Re-enable it deliberately on the site node to issue a certificate for the new name.',
    array('!new' => $new_uri)), 'advisory');

  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (is_file($internal_backup_flag)) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_txp_rename')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_txp_rename');
  }
}

/**
 * Implements drush_hook_COMMAND_rollback() (collapsed name).
 *
 * Put the site back under its old name. The old context was never deleted at
 * any point a rollback can reach (the retirement is the last step and is not
 * fatal), so restoring the site is a move back plus a re-save; the new name's
 * half-built context and vhost are discarded.
 */
function drush_provision_txp_rename_rollback($new_name = NULL) {
  $old_name = d()->name;
  $old_uri = d()->uri;
  $old_path = d()->site_path;
  $new_uri = is_string($new_name) ? strtolower(ltrim($new_name, '@')) : '';
  $new_path = ($new_uri === '') ? '' : d()->root . '/sites/' . $new_uri;
  $alias_file = ($new_uri === '') ? '' : drush_server_home() . '/.drush/' . $new_uri . '.alias.drushrc.php';

  // Any vhost the verify leg managed to write for the new name.
  if ($alias_file !== '' && is_file($alias_file)) {
    $new_context = d('@' . $new_uri);
    if (is_object($new_context)) {
      $new_context->service('http')->delete_config('site');
      $new_context->service('http')->parse_configs();
    }
  }

  if (drush_get_option('txp_rename_moved', FALSE) && $new_path !== '' && is_dir($new_path) && !is_dir($old_path)) {
    if (@rename($new_path, $old_path)) {
      drush_log(dt('Moved the site directory back to !old.', array('!old' => $old_path)), 'ok');
      _provision_txp_rename_rollback_repoint($old_uri, $old_path);
    }
    else {
      drush_log(dt('Could not move !new back to !old; the site is on disk under the NEW path and its old context still names the old one. Move it back by hand, then run Verify.',
        array('!new' => $new_path, '!old' => $old_path)), 'warning');
    }
  }

  // Re-assert the old alias from its own persisted record (the no-option
  // re-save provision-verify itself uses), then drop the new name's. The old
  // record was never rewritten by this command, so this is a no-op wherever
  // the rename got no further than the move -- which is the point.
  drush_invoke_process('@none', 'provision-save', array($old_name));
  if ($alias_file !== '' && is_file($alias_file)) {
    drush_invoke_process('@none', 'provision-save', array($new_name), array('delete' => TRUE));
  }

  drush_log(dt('The rename failed and the site has been returned to !old.',
    array('!old' => $old_name)), 'warning');
}

/**
 * Put the database back on the old uri after a rolled-back rename.
 *
 * The command re-points the uri-bound preference rows before the leg that can
 * still fail, and siteurl is one of them: publish.php composes every permalink,
 * asset URL, feed URL and redirect from it, so a site returned to its old
 * directory while its database still names the interim host publishes a site
 * whose every link points at a name that was never created. Nothing else puts
 * that row back at rollback time, so this is where it happens: the same single
 * writer the command uses, and idempotent.
 */
function _provision_txp_rename_rollback_repoint($old_uri, $old_path) {
  $connect_error = '';
  $mysqli = _provision_txp_db_connect(_provision_txp_site_creds(), $connect_error);
  if ($mysqli === FALSE) {
    drush_log(dt('Could not reach the site database to put it back on !uri (!msg). Until it is reachable the siteurl preference still names the uri this rename was taking and the site publishes links to a host that does not exist: run Verify on !uri, or correct siteurl in the Preferences panel.',
      array('!uri' => $old_uri, '!msg' => $connect_error)), 'warning');
    return;
  }
  $prefix = _provision_txp_config_prefix($old_path . '/private/config.php');
  try {
    _provision_txp_repoint_uri($mysqli, $old_uri, $old_path, $prefix);
    $mysqli->close();
  }
  catch (Exception $e) {
    drush_log(dt('Could not put the database back on !uri (!msg): the siteurl preference still names the uri this rename was taking. Run Verify on !uri, or correct it in the Preferences panel.',
      array('!uri' => $old_uri, '!msg' => $e->getMessage())), 'warning');
    return;
  }
  drush_log(dt('Re-pointed the database at !uri: the site publishes its own name again.', array('!uri' => $old_uri)), 'ok');
}
