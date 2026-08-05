<?php

/**
 * Provision backup command
 *
 * Back up an existing site
 */

const PROVISION_BACKUP_EXCLUDED_DIRECTORIES = array(
  './files/advagg_css',
  './files/advagg_js',
  './files/civicrm/templates_c',
  './files/css',
  './files/js',
  './files/php',
  './files/tmp',
  './private/temp',
);

const PROVISION_BACKUP_EXCLUDED_DIRECTORIES_SQLDUMP = array(
  './.redisOff',
  './.redisOn',
  './files',
  './libraries',
  './modules',
  './private',
  './themes',
  './vendor',
);

/**
 * Make sure the site is installed and enabled, and that we have a valid target to back up to.
 */
function drush_provision_drupal_provision_backup_validate($backup_file = NULL) {
  if (provision_platform_is_backdrop(d())) {
    return;
  }

  if (drush_drupal_major_version() >= 8) {
    // Lock Site-Local Drush if Unlocked
    provision_check_codebase_status();
    if (!d()->isConsolePatched || !d()->isCorePatched) {
      drush_log(dt('BACKUP/LOCAL/DRUSH/LOCK with provision_lock_drush_local_vnd'), 'info');
      provision_lock_drush_local_vnd();
    }
  }

  if (!@drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_CONFIGURATION)) {
    if (drush_get_option('force', false)) {
      drush_log("clearing error");
      drush_set_context('DRUSH_ERROR_CODE', DRUSH_SUCCESS);
    }
  }
  if (!drush_get_option('installed') && !drush_get_option('force', false)) {
     drush_set_error('PROVISION_DRUPAL_SITE_NOT_FOUND');
  }

  // This is the actual drupal provisioning requirements.
  if (!is_dir(d()->platform->server->backup_path)) {
    drush_set_error('PROVISION_BACKUP_PATH_NOT_FOUND');
  }

  if ($backup_file) {
    if (provision_file()->exists($backup_file)->status()) {
      drush_set_error('PROVISION_BACKUP_ALREADY_EXISTS', dt('Back up file @path already exists.', array('@path' => $backup_file)));
    }
    else {
      drush_log(dt('Backing site up to @path.', array('@path' => $backup_file)), 'info');
      drush_set_option('backup_file', $backup_file);
    }
  }

  if (!$backup_file) {
    $suggested = drush_provision_drupal_provision_backup_suggest_filename();
    drush_set_option('backup_file', $suggested);
  }

  // One-shot consume: by now the mode (if any) is latched in
  // SELECTED_BACKUP_MODE for this whole process; removing the ctrl file
  // immediately shrinks the window in which a concurrent task's implicit
  // backup could inherit it. Only when a real mode was READ - the constant
  // latches FALSE on modeless backups, and consuming then could delete a
  // concurrent task's freshly written file this process never used.
  if (defined('SELECTED_BACKUP_MODE') && SELECTED_BACKUP_MODE && file_exists(AEGIR_BACKUP_MODE_CTRL)) {
    @unlink(AEGIR_BACKUP_MODE_CTRL);
  }

}

/**
 * Util function to suggest a filename for a new site backup.
 */
function drush_provision_drupal_provision_backup_suggest_filename() {

  if (!provision_is_hostmaster_site()) {
    if (defined('SELECTED_BACKUP_MODE')) {
      $backup_mode = SELECTED_BACKUP_MODE;
      drush_set_option('backup_mode', $backup_mode);
    }
    else {
      $backup_mode = drush_get_option('selected_backup_mode', FALSE);
    }
    if (empty($backup_mode)) {
      if (file_exists(AEGIR_BACKUP_MODE_CTRL)) {
        $backup_mode = provision_backup_mode_sanitize(file_get_contents(AEGIR_BACKUP_MODE_CTRL));
        if ($backup_mode) {
          drush_set_option('backup_mode', $backup_mode);
          if (!defined('SELECTED_BACKUP_MODE')) {
            define('SELECTED_BACKUP_MODE', $backup_mode);
          }
          drush_log(dt("BACKUP/MODE/SET from control file: @var", array('@var' => $backup_mode)), 'success');
        }
      }
      else {
        drush_log("Backup mode control file not found.", 'info');
      }
    }
    if (isset($backup_mode)) {
      if (!defined('SELECTED_BACKUP_MODE')) {
        define('SELECTED_BACKUP_MODE', $backup_mode);
      }
      drush_log(dt("DRUSH/GET/OPTION selected_backup_mode in drush_provision_drupal_provision_backup_suggest_filename is: @var", array('@var' => $backup_mode)), 'info');
    }
  }

  // If mounted filesystem for files then use zst (optionally with tar pipelines)
  $mounted_arch = "/data/disk/arch";
  $mounted_bak = d()->platform->server->backup_path;

  if (is_link($mounted_arch) || is_link($mounted_bak)) {
    if ($backup_mode == 'backup_mysqldump_only' ||
      $backup_mode == 'site_files_without_db_backup' ||
      $backup_mode == 'site_files_with_mysqldump') {
      $suffix = drush_get_option('provision_backup_suffix', '.tar.gz');
    }
    else {
      $suffix = drush_get_option('provision_backup_suffix', '.tar.zst');
    }
  }
  elseif (isset($backup_mode)) {
    if ($backup_mode == 'backup_mysqldump_only' ||
      $backup_mode == 'site_files_without_db_backup' ||
      $backup_mode == 'site_files_with_mysqldump') {
      $suffix = drush_get_option('provision_backup_suffix', '.tar.gz');
    }
    else {
      $suffix = drush_get_option('provision_backup_suffix', '.tar.gz');
    }
  }
  else {
    $suffix = drush_get_option('provision_backup_suffix', '.tar');
  }
  $suggested = d()->platform->server->backup_path . '/' . d()->uri . '-' . date("Ymd.His", time()) . $suffix;

  // Use format of mysite.com-2008-01-02, if already existing, add number.
  $count = 0;
  while (is_file($suggested)) {
    $count++;
    $suggested = d()->platform->server->backup_path . '/' . d()->uri . '-' .  date('Ymd.His', time()) . '_' . $count . $suffix;
  }
  return $suggested;
}

/**
 * Implentation of hook_provision_backup()
 */
function drush_provision_drupal_provision_backup() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  $backup_file = drush_get_option('backup_file');
  // Adds the site directory into the backup file
  drush_log(dt("Adding sites directory to !backup_file", array('!backup_file' => $backup_file)), 'backup');

  // sync all filesystem changes back from the remote server.
  provision_drupal_fetch_site();

  // Check if we are currently cloaking credentials
  $cloaked = d()->service('http')->cloaked_db_creds();
  $cloaked = drush_get_option('provision_db_cloaking', $cloaked);

  if ($cloaked) {
    drush_set_option('cloaking_off_temp', TRUE);
    // Disable the cloaking of credentials temporarily
    drush_log(dt("Temporarily uncloaking database credentials for backup"));
    drush_set_option('provision_db_cloaking', FALSE);

    // Write the uncloaked credentials to the settings.php
    _provision_drupal_create_settings_file();
    provision_drupal_push_site();

  }

  $olddir = getcwd();

  if (!chdir(d()->site_path)) {
    return drush_set_error('PROVISION_BACKUP_PATH_NOT_FOUND', dt('cannot change directory to %dir', array('%dir' => d()->site_path)));
  }

  if (!provision_is_hostmaster_site()) {
    if (defined('SELECTED_BACKUP_MODE')) {
      $backup_mode = SELECTED_BACKUP_MODE;
      drush_set_option('backup_mode', $backup_mode);
    }
    else {
      $backup_mode = drush_get_option('selected_backup_mode', FALSE);
    }
    if (empty($backup_mode)) {
      if (file_exists(AEGIR_BACKUP_MODE_CTRL)) {
        $backup_mode = provision_backup_mode_sanitize(file_get_contents(AEGIR_BACKUP_MODE_CTRL));
        if ($backup_mode) {
          drush_set_option('backup_mode', $backup_mode);
          if (!defined('SELECTED_BACKUP_MODE')) {
            define('SELECTED_BACKUP_MODE', $backup_mode);
          }
          drush_log(dt("BACKUP/MODE/SET from control file: @var", array('@var' => $backup_mode)), 'success');
        }
      }
      else {
        drush_log("Backup mode control file not found.", 'info');
      }
    }
    if (isset($backup_mode)) {
      if (!defined('SELECTED_BACKUP_MODE')) {
        define('SELECTED_BACKUP_MODE', $backup_mode);
      }
      drush_log(dt("DRUSH/GET/OPTION selected_backup_mode in drush_provision_drupal_provision_backup is: @var", array('@var' => $backup_mode)), 'info');
    }
  }

  if (isset($backup_mode) && $backup_mode == 'backup_mysqldump_only') {
    $exclude_tag = "touch" . drush_provision_drupal_provision_backup_get_exclusions_sqldump();
    $exclude_clean = "rm" . drush_provision_drupal_provision_backup_get_exclusions_sqldump();
  }
  else {
    $exclude_tag = "touch" . drush_provision_drupal_provision_backup_get_exclusions();
    $exclude_clean = "rm" . drush_provision_drupal_provision_backup_get_exclusions();
  }
  drush_shell_exec($exclude_tag);

  // A Drupal site dir must never contain device/block/fifo/socket special files.
  // A stray one (e.g. a /dev node copied into the site root by an out-of-band
  // operation) is archived by tar as a special member; a later non-root
  // `tar -pxf` on deploy/migrate/clone/restore then cannot recreate it (mknod
  // needs CAP_MKNOD) and aborts the whole extraction, rolling back the task.
  // Strip any such node here, at the source, so no poisoned archive is ever
  // produced. cwd is the site dir (chdir above); -xdev stays on the site's own
  // filesystem and, with no -L, never follows the files/private symlinks into
  // the per-account store.
  $strip_specials = 'find . -xdev \( -type b -o -type c -o -type p -o -type s \) -print -delete 2>/dev/null';
  if (drush_shell_exec($strip_specials)) {
    $stripped_specials = drush_shell_exec_output();
    if (!empty($stripped_specials)) {
      foreach ($stripped_specials as $stray_special) {
        drush_log(dt("BACKUP/STRIP removed stray special file from site dir before archiving: @f", array('@f' => $stray_special)), 'warning');
      }
    }
  }

  // If mounted filesystem for files then use tar pipelines
  $mounted_arch = "/data/disk/arch";
  $mounted_bak = d()->platform->server->backup_path;

  // Legacy hard-preserve override; preserving is the default now, so the
  // file is redundant but still honoured (it also beats the opt-out below).
  $force_symlinks = "/data/conf/force_symlinks.conf";
  // Opt-out restoring the old always-dereference behaviour for implicit
  // backups: self-contained archives at the cost of duplicating store data
  // into every safety copy.
  $deref_backups = "/data/conf/dereference_backups.conf";

  if (!empty($backup_mode) && ($backup_mode == 'site_files_with_mysqldump' ||
    $backup_mode == 'site_files_without_db_backup')) {
    // A files-carrying backup mode selected in the frontend must yield a
    // self-contained archive: with site files/private symlinked into the
    // per-account store by default, preserving symlinks would archive bare
    // links and silently drop the real content the user asked for. The
    // DB-only mode deliberately stays on the preserving default so its
    // archives remain files-safe if ever fed to the Restore task.
    $command_base = "tar cphf";
  }
  elseif (is_file($deref_backups) && !is_file($force_symlinks)) {
    // Operator opted back into dereferencing implicit backups.
    $command_base = "tar cphf";
  }
  else {
    // Default: do not follow symlinks. A symlinked site's content lives in
    // the per-account store and every lifecycle flow (clone, migrate,
    // restore) re-homes it; a plain-dirs site is archived in full either
    // way, since not FOLLOWING links never skips real directories.
    $command_base = "tar cpf";
  }

  $command_arguments = " %s --exclude-tag=exclude.tag";

  if (substr($backup_file, -2) == 'gz') {
    $command = $command_base . 'z' . $command_arguments;
  }
  elseif (substr($backup_file, -3) == 'bz2') {
    $command = $command_base . 'j' . $command_arguments;
  }
  elseif (substr($backup_file, -2) == 'xz') {
    $command = $command_base . 'J' . $command_arguments;
  }
  elseif (substr($backup_file, -3) == 'lz4') {
    if (!empty($backup_mode) && ($backup_mode == 'site_files_with_mysqldump' ||
      $backup_mode == 'site_files_without_db_backup')) {
      // Files-carrying mode: always follow symlinks for a self-contained archive
      $command_base = "tar -c -p -h -I lz4 -f";
    }
    elseif (is_file($deref_backups) && !is_file($force_symlinks)) {
      // Operator opted back into dereferencing implicit backups.
      $command_base = "tar -c -p -h -I lz4 -f";
    }
    else {
      // Default: do not follow symlinks (content lives in the store).
      $command_base = "tar -c -p -I lz4 -f";
    }
    $command = $command_base . $command_arguments;
  }
  elseif (substr($backup_file, -3) == 'zst') {
    if (!empty($backup_mode) && ($backup_mode == 'site_files_with_mysqldump' ||
      $backup_mode == 'site_files_without_db_backup')) {
      // Files-carrying mode: always follow symlinks for a self-contained archive
      $command_base = "tar -c -p -h -I zstd -f";
    }
    elseif (is_file($deref_backups) && !is_file($force_symlinks)) {
      // Operator opted back into dereferencing implicit backups.
      $command_base = "tar -c -p -h -I zstd -f";
    }
    else {
      // Default: do not follow symlinks (content lives in the store).
      $command_base = "tar -c -p -I zstd -f";
    }
    $command = $command_base . $command_arguments;
  }
  else {
    $command = $command_base . $command_arguments;
  }
  $command .= ' .';
  $result = drush_shell_exec($command,  $backup_file);
  drush_shell_exec($exclude_clean);

  // Get the size of the backup
  $size = filesize($backup_file);
  drush_set_option('backup_file_size', $size);

  chdir($olddir);

  if (drush_get_option('cloaking_off_temp', FALSE)) {
    drush_log(dt("Re-cloaking database credentials after backup"));
    drush_set_option('provision_db_cloaking', TRUE);
    _provision_drupal_create_settings_file();
    provision_drupal_push_site();

  }

  if (!$result && !drush_get_option('force', false)) {
    drush_set_error('PROVISION_BACKUP_FAILED', dt("Could not back up sites directory for drupal"));
  }
}

function drush_provision_drupal_provision_backup_get_exclusions() {
  $directories = PROVISION_BACKUP_EXCLUDED_DIRECTORIES;
  drush_command_invoke_all_ref('provision_backup_exclusions_alter', $directories);
  $exclusions = "";
  foreach ($directories as $directory) {
    $exclusions .= " '$directory'/exclude.tag";
  }
  drush_log(dt("In drush_provision_drupal_provision_backup_get_exclusions: @var", array('@var' => $exclusions)), 'info');
  return $exclusions;
}

function drush_provision_drupal_provision_backup_get_exclusions_sqldump() {
  $directories = PROVISION_BACKUP_EXCLUDED_DIRECTORIES_SQLDUMP;
  $exclusions = "";
  foreach ($directories as $directory) {
    $exclusions .= " '$directory'/exclude.tag";
  }
  drush_log(dt("In drush_provision_drupal_provision_backup_get_exclusions_sqldump: @var", array('@var' => $exclusions)), 'info');
  return $exclusions;
}

function drush_provision_drupal_post_provision_backup() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  drush_log(dt('Backed up site up to @path.', array('@path' => drush_get_option('backup_file'))), 'success');

  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  if (!is_file($internal_backup_flag)) {

    $enable_myquick = FALSE;
    $oct_db_test = FALSE;
    $myquick_creds_log = '/data/conf/_myquick_creds_log.txt';

    if (!provision_is_hostmaster_site()) {
      if (defined('SELECTED_BACKUP_MODE')) {
        $backup_mode = SELECTED_BACKUP_MODE;
        drush_set_option('backup_mode', $backup_mode);
      }
      else {
        $backup_mode = drush_get_option('selected_backup_mode', FALSE);
      }
      if (empty($backup_mode)) {
        if (file_exists(AEGIR_BACKUP_MODE_CTRL)) {
          $backup_mode = provision_backup_mode_sanitize(file_get_contents(AEGIR_BACKUP_MODE_CTRL));
          if ($backup_mode) {
            drush_set_option('backup_mode', $backup_mode);
            if (!defined('SELECTED_BACKUP_MODE')) {
              define('SELECTED_BACKUP_MODE', $backup_mode);
            }
            drush_log(dt("BACKUP/MODE/SET from control file: @var", array('@var' => $backup_mode)), 'success');
          }
        }
        else {
          drush_log("Backup mode control file not found.", 'info');
        }
      }
      if (isset($backup_mode)) {
        if (!defined('SELECTED_BACKUP_MODE')) {
          define('SELECTED_BACKUP_MODE', $backup_mode);
        }
        drush_log(dt("DRUSH/GET/OPTION selected_backup_mode in drush_provision_drupal_post_provision_backup is: @var", array('@var' => $backup_mode)), 'info');
      }
    }

    if (empty($backup_mode)) {
      $aegir_root = d('@server_master')->aegir_root;
      $backup_path = d('@server_master')->backup_path;
      $oct_db_dirx = $backup_path . '/tmp_expim';
      $oct_db_test = $oct_db_dirx . '/metadata';
      $uri_db_dirz = $backup_path . '/' . d()->uri . '_sql_dump_backup_' . date("Ymd.His", time());
      $enable_myquick = $aegir_root . '/static/control/MyQuick.info';
    }

    if (is_file($enable_myquick) && is_file($oct_db_test)) {
      $old = $oct_db_dirx;
      $new = $uri_db_dirz;
      provision_file()->switch_paths($old, $new)
        ->succeed('Moving the Post-DB-Backup @path1 to @path2 was successful.')
        ->fail('Moving the Post-DB-Backup @path1 to @path2 has failed.');
      if (is_dir($new)) {
        _provision_recursive_delete($old);
        drush_log(dt("MyQuick tmp_expim dir removed in drush_provision_drupal_post_provision_backup @var", array('@var' => $old)), 'info');
      }
    }
  }

  if (d()->client_name) {
    $backup_dir = d()->server->clients_path . '/' . d()->client_name . '/backups';
    provision_file()->create_dir($backup_dir, dt('Client backup directory for @client', array('@client' => d()->client_name)), 0750);
    provision_file()->symlink(drush_get_option('backup_file'), $backup_dir . '/' . basename(drush_get_option('backup_file')))
      ->succeed('Created symlink @path to @target')
      ->fail('Could not create symlink @path to @target: @reason');
  }
}

/**
 * Remove the backup file if something went wrong
 */
function drush_provision_drupal_provision_backup_rollback() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  $backup_file = drush_get_option('backup_file');
  if (is_readable($backup_file) && is_file($backup_file)) {
    provision_file()->unlink($backup_file)
      ->succeed('Removed stale backup file @path')
      ->fail('Failed deleting backup file @path');

    $enable_myquick = FALSE;
    $oct_db_test = FALSE;
    $myquick_creds_log = '/data/conf/_myquick_creds_log.txt';

    if (!provision_is_hostmaster_site()) {
      if (defined('SELECTED_BACKUP_MODE')) {
        $backup_mode = SELECTED_BACKUP_MODE;
        drush_set_option('backup_mode', $backup_mode);
      }
      else {
        $backup_mode = drush_get_option('selected_backup_mode', FALSE);
      }
      if (empty($backup_mode)) {
        if (file_exists(AEGIR_BACKUP_MODE_CTRL)) {
          $backup_mode = provision_backup_mode_sanitize(file_get_contents(AEGIR_BACKUP_MODE_CTRL));
          if ($backup_mode) {
            drush_set_option('backup_mode', $backup_mode);
            if (!defined('SELECTED_BACKUP_MODE')) {
              define('SELECTED_BACKUP_MODE', $backup_mode);
            }
            drush_log(dt("BACKUP/MODE/SET from control file: @var", array('@var' => $backup_mode)), 'success');
          }
        }
        else {
          drush_log("Backup mode control file not found.", 'info');
        }
      }
      if (isset($backup_mode)) {
        if (!defined('SELECTED_BACKUP_MODE')) {
          define('SELECTED_BACKUP_MODE', $backup_mode);
        }
        drush_log(dt("DRUSH/GET/OPTION selected_backup_mode in drush_provision_drupal_provision_backup_rollback is: @var", array('@var' => $backup_mode)), 'info');
      }
    }

    if (empty($backup_mode)) {
      $aegir_root = d('@server_master')->aegir_root;
      $backup_path = d('@server_master')->backup_path;
      $oct_db_dirx = $backup_path . '/tmp_expim';
      $oct_db_test = $oct_db_dirx . '/metadata';
      $uri_db_dirz = $backup_path . '/' . d()->uri . '_sql_dump_rollback_' . date("Ymd.His", time());
      $enable_myquick = $aegir_root . '/static/control/MyQuick.info';
    }

    if (is_file($enable_myquick) && is_file($oct_db_test)) {
      $old = $oct_db_dirx;
      $new = $uri_db_dirz;
      provision_file()->switch_paths($old, $new)
        ->succeed('Moving the Post-DB-Cleanup @path1 to @path2 was successful.')
        ->fail('Moving the Post-DB-Cleanup @path1 to @path2 has failed.');
      if (is_dir($new)) {
        _provision_recursive_delete($old);
        drush_log(dt("MyQuick tmp_expim dir removed in drush_provision_drupal_provision_backup_rollback @var", array('@var' => $old)), 'info');
      }
    }
  }
}
