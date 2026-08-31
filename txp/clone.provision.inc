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
}

function drush_provision_txp_pre_provision_clone($new_name, $platform = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
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
