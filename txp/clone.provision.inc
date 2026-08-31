<?php
/**
 * @file
 * Provision hooks for the provision clone command (TXP layer).
 *
 * Mirrors the Backdrop clone flow: safety-backup the source (internal-backup
 * flag honoured), build the new site's options (Encryption auto-disabled —
 * SSL keys are bound to the source name; aliases/redirection reset), then
 * provision-save + provision-deploy on the new name and verify it. The
 * security half of cloning (blog_uid regen, token truncation) rides the
 * deploy engine.
 */

function drush_provision_txp_provision_clone_validate($new_name = NULL, $platform = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_CLONE_CONTEXT', dt('provision-clone must run on a site context.'));
  }
  // Clear a stale frontend backup-mode ctrl file before the internal safety
  // backup runs: inheriting a dead task's mode would build the clone source
  // archive in e.g. "database only" mode and deploy a files-less copy that
  // still reports success.
  if (defined('AEGIR_BACKUP_MODE_CTRL') && is_file(AEGIR_BACKUP_MODE_CTRL)) {
    unlink(AEGIR_BACKUP_MODE_CTRL);
  }
}

function drush_provision_txp_pre_provision_clone($new_name, $platform = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  // The pre-clone copy is registered in the panel as "Pre-clone backup" and
  // offered to the operator as a restore point, so it must be self-contained.
  // Left to resolve on its own it comes out modeless: MyQuick then carries the
  // database through the transient tmp_expim directory instead of the archive,
  // and once that directory is gone the registered "backup" cannot restore the
  // database at all. Force the same restorable mode the restore and delete
  // safety copies already use. The deploy half below is switched to match --
  // both halves MUST agree on the transport, or the clone breaks.
  if (!defined('SELECTED_BACKUP_MODE')) {
    define('SELECTED_BACKUP_MODE', 'site_files_with_mysqldump');
  }
  drush_set_option('selected_backup_mode', 'site_files_with_mysqldump');
  drush_set_option('backup_mode', 'site_files_with_mysqldump');
  drush_log(dt('Clone safety copy forced to the restorable mode: site_files_with_mysqldump'), 'info');
  drush_log(dt('Invoking drush provision-backup in drush_provision_txp_pre_provision_clone'));
  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  if (!provision_file()->exists($internal_backup_flag)->status()) {
    provision_file()->file_put_contents($internal_backup_flag, "Internal-DB-Backup \n")
      ->succeed('Generated blank Internal-DB-Backup Flag-File (txp clone)')
      ->fail('Could not generate Internal-DB-Backup Flag-File (txp clone)');
  }
  drush_invoke('provision-backup');
}

function drush_provision_txp_pre_provision_clone_rollback($new_name, $platform = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  provision_file()->unlink(drush_get_option('backup_file'))
    ->succeed('Removed unused clone site package')
    ->fail('Could not remove unused clone site package');
  $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
  if (is_file($internal_backup_flag)) {
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in rollback')
      ->fail('Could not remove Internal-DB-Backup Flag-File in rollback');
  }
}

function drush_provision_txp_provision_clone($new_name, $platform = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  drush_set_option('old_platform', d()->platform->name);

  $options = d()->options;
  $options['uri'] = ltrim($new_name, '@');
  $hash_name = drush_get_option('#name') ? '#name' : 'name';
  $options[$hash_name] = $new_name;
  $options['platform'] = (isset($platform)) ? $platform : $options['platform'];
  $options['root'] = d($options['platform'])->root;
  $options['aliases'] = array();
  $options['redirection'] = 0;

  // Encryption auto-disable on the clone (SSL keys are source-name-bound).
  $options['ssl_enabled'] = 0;
  unset($options['ssl_key']);
  drush_log(dt('[clone] Encryption disabled for new site @name before final verify', array('@name' => $new_name)), 'info');

  // Do not copy CDN aliases to the clone.
  $options['cdn'] = 0;
  unset($options['cdn']);

  // Auto-disable any migration-source association: the clone gets a brand-new
  // database user with no grant on the referenced database, so a carried
  // association composes a connection it cannot open. (Latent while migrate is
  // fail-closed; it must be here BEFORE the real migrate engine lands.)
  unset($options['migrate_source_db']);
  unset($options['migrate_source_uri']);

  if ($profile = drush_get_option('profile', FALSE)) {
    $options['profile'] = $profile;
  }
  if ($db_server = drush_get_option('new_db_server', FALSE)) {
    $options['db_server'] = $db_server;
  }

  drush_invoke_process('@none', 'provision-save', array($new_name), $options);

  $deploy_options = array(
    'old_uri' => d()->uri,
    'strict' => 0,
    // Match the transport the safety copy above was written with. The deploy
    // runs as a SEPARATE backend process, so it resolves the backup mode from
    // scratch: without this it resolves to modeless, takes the MyQuick import
    // branch and looks for a tmp_expim dump directory that a mysqldump-mode
    // backup never wrote -- "Database import failed (dir: .../tmp_expim)".
    'selected_backup_mode' => 'site_files_with_mysqldump',
  );
  drush_command_invoke_all_ref('provision_deploy_options_alter', $deploy_options, 'clone');

  provision_backend_invoke($new_name, 'provision-deploy', array(drush_get_option('backup_file')), $deploy_options);

  if (!drush_get_error()) {
    provision_backend_invoke($new_name, 'provision-verify');

    $internal_backup_flag = d('@server_master')->backup_path . '/.internal_backup_flag.pid';
    provision_file()->unlink($internal_backup_flag)
      ->succeed('Removed Internal-DB-Backup Flag-File in drush_provision_txp_provision_clone')
      ->fail('Could not remove Internal-DB-Backup Flag-File in drush_provision_txp_provision_clone');
  }

  provision_reload_config('site', d()->site_path . '/drushrc.php');
}
