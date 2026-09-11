<?php

/**
 * @file
 * The provision-grav-rename command: same-platform rename of a Grav capsule.
 *
 * A Grav site IS its directory (boa-grav D-003: sites/<uri>/ is a complete,
 * self-contained install), so a rename on the same platform is a move plus a
 * re-point plus new front-end plumbing -- no archive, no deploy, no database.
 * That is the whole reason this command exists next to the copy upgrade: the
 * generic migrate path would build a backup, deploy it into a second tree and
 * delete the first, for a capsule that only needs to change its name.
 *
 * Identity is PRESERVED: the nonce key, the api secret, the accounts, the
 * pages and the licences travel with the directory. The only capsule surfaces
 * keyed by the uri are the per-host environment overlay and the .env that
 * pins GRAV_ENVIRONMENT to it, and both are re-pointed here through the same
 * helper the deploy engine's rename/upgrade classes use.
 *
 * What is NOT keyed by the uri for a Grav capsule, checked rather than
 * assumed: this layer never creates a native files store (it never calls
 * _provision_drupal_native_symlink, since a capsule keeps its own files) and
 * never creates a client symlink (install and verify actively delete one),
 * so a rename has neither to move. The per-site BOA control INI lives INSIDE
 * the capsule at sites/<uri>/modules/, so it rides the move untouched.
 * Backups already taken keep their old-name file names, as they do after any
 * rename: they are registered against the site node, not its uri.
 *
 * Ordering is chosen for the shortest possible gap in service: the capsule
 * moves, the new context is saved and verified (which renders the new vhost),
 * and only then is the old vhost removed and the old context retired. The
 * rollback moves the tree back and puts the old vhost back.
 */

/**
 * Implements drush_hook_COMMAND_validate() (collapsed name).
 */
function drush_provision_grav_rename_validate($new_name = NULL) {
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_GRAV_RENAME_BAD_SOURCE',
      dt('provision-grav-rename must run on a site context.'));
  }
  if (!provision_platform_is_grav(d())) {
    return drush_set_error('PROVISION_GRAV_RENAME_BAD_SOURCE',
      dt('provision-grav-rename only applies to sites on Grav platforms.'));
  }
  if (!_provision_grav_site_exists()) {
    return drush_set_error('PROVISION_GRAV_SITE_NOT_FOUND',
      dt('No Grav capsule found at !site.', array('!site' => d()->site_path)));
  }
  if (!d()->site_enabled) {
    return drush_set_error('PROVISION_GRAV_RENAME_DISABLED',
      dt('This site is disabled; enable it before renaming it (a disabled site renders the disabled vhost, and the rename would publish it under the new name).'));
  }

  if (!is_string($new_name) || $new_name === '') {
    return drush_set_error('PROVISION_GRAV_RENAME_BAD_TARGET',
      dt('No new site alias given.'));
  }
  $new_uri = strtolower(ltrim($new_name, '@'));
  if ($new_uri === strtolower(d()->uri)) {
    return drush_set_error('PROVISION_GRAV_RENAME_BAD_TARGET',
      dt('The new uri must differ from the current one; nothing to rename.'));
  }
  // Conservative hostname shape check; the frontend already ran its FQDN
  // validator, this catches a mangled hand-run invocation before the move.
  if (!preg_match('/^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/', $new_uri)
    || strpos($new_uri, '..') !== FALSE) {
    return drush_set_error('PROVISION_GRAV_RENAME_BAD_TARGET',
      dt('The new uri !uri is not a plausible domain name.', array('!uri' => $new_uri)));
  }
  if ($new_uri === 'hostmaster' || $new_uri === strtolower(d('@hostmaster')->uri)) {
    return drush_set_error('PROVISION_GRAV_RENAME_BAD_TARGET',
      dt('Refusing to take the hosting front-end\'s name.'));
  }
  if (file_exists(d()->root . '/sites/' . $new_uri)) {
    return drush_set_error('PROVISION_GRAV_RENAME_TARGET_EXISTS',
      dt('sites/@uri already exists on this platform; delete that site first or choose another name.',
      array('@uri' => $new_uri)));
  }

  // Clear a stale frontend backup-mode ctrl file before the safety copy runs.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }
}

/**
 * Implements drush_hook_pre_COMMAND() (doubled name -- not collapsed).
 */
function drush_provision_grav_pre_provision_grav_rename($new_name = NULL) {
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_without_db_backup');
  }
  drush_set_option('selected_backup_mode', 'site_files_without_db_backup');
  drush_set_option('backup_mode', 'site_files_without_db_backup');
  drush_log(dt('Pre-rename safety copy (site_files_without_db_backup)'), 'info');
  drush_invoke('provision-backup');
}

/**
 * Implements drush_hook_COMMAND() (collapsed name).
 */
function drush_provision_grav_rename($new_name = NULL) {
  // Context names carry the leading @; a frontend that passes the bare uri
  // would otherwise have a context saved under a name nothing resolves.
  $new_uri = strtolower(ltrim($new_name, '@'));
  $new_name = '@' . $new_uri;
  $old_uri = d()->uri;
  $old_name = d()->name;
  $old_path = d()->site_path;
  $new_path = d()->root . '/sites/' . $new_uri;

  // The drush exit hook writes the site drushrc for the context the command
  // ran in, and Provision_Config::write() creates the directory it is missing:
  // left on, it would recreate sites/<old>/ the moment the capsule left it,
  // and the platform would count a directory nothing serves as a site.
  drush_set_option('provision_save_config', FALSE);

  if (!@rename($old_path, $new_path)) {
    return drush_set_error('PROVISION_GRAV_RENAME_FAILED',
      dt('Could not move the capsule !old to !new; nothing was changed.',
      array('!old' => $old_path, '!new' => $new_path)));
  }
  drush_set_option('grav_rename_moved', TRUE);
  drush_log(dt('Moved the capsule !old to !new.', array('!old' => $old_path, '!new' => $new_path)), 'ok');

  // The uri-bound surfaces, identity untouched.
  _provision_grav_repoint_uri($new_path, $old_uri, $new_uri);
  _provision_grav_write_env($new_path, $new_uri);
  _provision_grav_secure_secrets($new_path);

  // The new context. Encryption goes off with the name (SSL keys are
  // name-bound) and the domain aliases do not follow a rename; the extra
  // Drush aliases do, because they name the SITE and the new context rewrites
  // their alias files at the new uri.
  $options = d()->options;
  $options['uri'] = $new_uri;
  $hash_name = drush_get_option('#name') ? '#name' : 'name';
  $options[$hash_name] = $new_name;
  $options['site_path'] = $new_path;
  $options['aliases'] = array();
  $options['redirection'] = 0;
  $options['ssl_enabled'] = 0;
  unset($options['ssl_key']);
  drush_invoke_process('@none', 'provision-save', array($new_name), $options);
  drush_set_option('grav_rename_saved', TRUE);

  // Renders the new vhost and re-asserts the capsule's tree under the new
  // name (ownership model, .env, secrets, package map).
  provision_backend_invoke($new_name, 'provision-verify');
  if (drush_get_error()) {
    return drush_set_error('PROVISION_GRAV_RENAME_FAILED',
      dt('Verify of !new failed after the move; the rename is being rolled back.', array('!new' => $new_name)));
  }

  // Only now retire the old name: its vhost through the http service's own
  // delete for this context (the same call the delete and migrate commands
  // make), then its context.
  d()->service('http')->delete_config('site');
  d('@server_master')->service('http')->parse_configs();
  drush_invoke_process('@none', 'provision-save', array($old_name), array('delete' => TRUE));
  drush_log(dt('Retired the previous name !old; the site answers at !new.',
    array('!old' => $old_uri, '!new' => $new_uri)), 'ok');

  // The front-end page-cache namespace is host-keyed, not CMS-keyed: a site
  // later created at the freed name must not inherit this one's keys.
  if (function_exists('_provision_valkey_purge_site')) {
    _provision_valkey_purge_site($old_uri);
  }

  drush_log(dt('Encryption is OFF for !new (a rename disables it -- certificates are name-bound). Re-enable it deliberately on the site node to issue a certificate for the new name.',
    array('!new' => $new_uri)), 'advisory');
}

/**
 * Implements drush_hook_COMMAND_rollback() (collapsed name).
 *
 * Move the capsule back, put the old vhost back, and remove whatever the new
 * name managed to acquire. The capsule is only ever discarded at the NEW path
 * when it is demonstrably back at the old one -- a move-back that failed
 * leaves the site where it is and says so, because the alternative is
 * deleting the only copy.
 */
function drush_provision_grav_rename_rollback($new_name = NULL) {
  $new_uri = strtolower(ltrim($new_name, '@'));
  $new_name = '@' . $new_uri;
  $old_uri = d()->uri;
  $old_path = d()->site_path;
  $new_path = d()->root . '/sites/' . $new_uri;

  drush_set_option('provision_save_config', FALSE);

  $restored = FALSE;
  if (!drush_get_option('grav_rename_moved', FALSE) || (is_dir($old_path) && !is_dir($new_path))) {
    // Nothing moved, or it is already back.
    $restored = TRUE;
  }
  elseif (is_dir($new_path) && !file_exists($old_path) && @rename($new_path, $old_path)) {
    _provision_grav_repoint_uri($old_path, $new_uri, $old_uri);
    _provision_grav_write_env($old_path, $old_uri);
    _provision_grav_secure_secrets($old_path);
    $restored = TRUE;
    drush_log(dt('Moved the capsule back to !old.', array('!old' => $old_path)), 'ok');
  }

  if (!$restored) {
    return drush_set_error('PROVISION_GRAV_RENAME_FAILED',
      dt('The rename failed and the capsule could not be moved back: it is at !new while the site context is !old. Nothing was deleted. Fix the cause, then either move the directory back by hand or complete the rename by re-running it.',
      array('!new' => $new_path, '!old' => $old_uri)));
  }

  // The old name serves again: its vhost is re-created the way the migrate
  // rollback does, from the context that still describes it. Only when the
  // capsule actually moved -- a failure before that removed nothing.
  if (drush_get_option('grav_rename_moved', FALSE)) {
    d()->service('http')->create_config('site');
    d('@server_master')->service('http')->parse_configs();
  }

  // Whatever the new name acquired (its vhost, its context) goes; the capsule
  // is back at the old path, so the helper finds nothing to remove there.
  if (drush_get_option('grav_rename_saved', FALSE)) {
    _provision_grav_discard_copy($new_name, NULL);
  }
  drush_log(dt('provision-grav-rename rolled back; the site still answers at !old.', array('!old' => $old_uri)), 'advisory');
}
