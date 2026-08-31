<?php
/**
 * @file
 * Provision hooks for the provision backup command (TXP layer).
 *
 * Fork of backdrop/backup.provision.inc with the TXP exclusion constants and
 * ONE deliberate divergence: TXP archives are NEVER dereferenced (no tar -h in
 * ANY mode, and the /data/conf/dereference_backups.conf opt-in is ignored).
 * A TXP site's content dirs are real directories (no per-account store
 * symlinking), so every mode is self-contained without -h; the only symlinks
 * in scope are the four load-bearing admin core links, and dereferencing
 * those bakes the shared core into every archive and restores dir-over-symlink
 * states (map R-10). No cloaking dance either: TXP config.php always carries
 * literal credentials (the BOA golden rule), nothing to uncloak.
 */

// Volatile-only exclusions for files-carrying modes.
const PROVISION_TXP_BACKUP_EXCLUDED_DIRECTORIES = array(
  './tmp',
);

// mysqldump-only mode: exclude the bulky per-site content dirs; NEVER
// './private' (config.php lives there — the Drupal-layer SQLDUMP list would
// silently strip the site's credentials from every DB-mode backup, map R-3).
const PROVISION_TXP_BACKUP_EXCLUDED_DIRECTORIES_SQLDUMP = array(
  './tmp',
  './public/files',
  './public/images',
  './public/themes',
);

/**
 * Make sure the site is installed and enabled and the target is valid.
 */
function drush_provision_txp_provision_backup_validate($backup_file = NULL) {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  $backup_mode = provision_backup_mode_resolve();

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
    $suggested = drush_provision_txp_provision_backup_suggest_filename();
    drush_set_option('backup_file', $suggested);
  }
}

/**
 * Suggest the backup filename (mirrors the shared naming + suffix logic).
 */
function drush_provision_txp_provision_backup_suggest_filename() {
  $backup_mode = provision_backup_mode_resolve();
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
  elseif (!provision_is_hostmaster_site()) {
    $suffix = drush_get_option('provision_backup_suffix', '.tar.gz');
  }
  else {
    $suffix = drush_get_option('provision_backup_suffix', '.tar');
  }

  $backup_path = d()->platform->server->backup_path;
  return $backup_path . '/' . d()->uri . '-' . date("Ymd.His", time()) . $suffix;
}

/**
 * Back up the site directory (DB dump already inside via the db service).
 */
function drush_provision_txp_provision_backup() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  $backup_file = drush_get_option('backup_file');
  drush_log(dt("Adding sites directory to !backup_file", array('!backup_file' => $backup_file)), 'backup');

  // Sync all filesystem changes back from the remote server.
  provision_drupal_fetch_site();

  $olddir = getcwd();
  if (!chdir(d()->site_path)) {
    return drush_set_error('PROVISION_BACKUP_PATH_NOT_FOUND', dt('cannot change directory to %dir', array('%dir' => d()->site_path)));
  }

  $backup_mode = provision_backup_mode_resolve();

  // Tag excluded dirs per existing directory, clearing stale tags first (a
  // leftover tag silently shrinks every future backup).
  $exclusion_dirs = drush_provision_txp_provision_backup_get_exclusion_dirs(!empty($backup_mode) ? $backup_mode : NULL);
  $failed_tags = array();
  foreach ($exclusion_dirs as $exclusion_dir) {
    if (!is_dir($exclusion_dir)) {
      continue;
    }
    $exclusion_tag = $exclusion_dir . '/exclude.tag';
    if (is_file($exclusion_tag)) {
      @unlink($exclusion_tag);
    }
    if (!@touch($exclusion_tag)) {
      $failed_tags[] = $exclusion_tag;
    }
  }
  if (!empty($failed_tags)) {
    drush_log(dt('Could not write the backup exclusion tags (@tags); the archive may include directories that are normally excluded', array('@tags' => implode(', ', $failed_tags))), 'warning');
  }

  // Strip stray special files at the source (poisoned-archive protection,
  // same rationale as the Drupal layer). cwd is the site dir; -xdev + no -L
  // never follows the admin core symlinks.
  $strip_specials = 'find . -xdev \( -type b -o -type c -o -type p -o -type s \) -print -delete 2>/dev/null';
  if (drush_shell_exec($strip_specials)) {
    $stripped_specials = drush_shell_exec_output();
    if (!empty($stripped_specials)) {
      foreach ($stripped_specials as $stray_special) {
        drush_log(dt("BACKUP/STRIP removed stray special file from site dir before archiving: @f", array('@f' => $stray_special)), 'warning');
      }
    }
  }

  // ALWAYS preserve symlinks (see the file header): plain cpf in every mode.
  $command_base = "tar cpf";
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
    $command = "tar -c -p -I lz4 -f" . $command_arguments;
  }
  elseif (substr($backup_file, -3) == 'zst') {
    $command = "tar -c -p -I zstd -f" . $command_arguments;
  }
  else {
    $command = $command_base . $command_arguments;
  }
  $command .= ' .';
  $result = drush_shell_exec($command, $backup_file);

  // Remove the tags; a stuck tag silently shrinks later backups — say so.
  $stale_tags = array();
  foreach ($exclusion_dirs as $exclusion_dir) {
    $exclusion_tag = $exclusion_dir . '/exclude.tag';
    if (!is_file($exclusion_tag)) {
      continue;
    }
    @unlink($exclusion_tag);
    if (is_file($exclusion_tag)) {
      $stale_tags[] = $exclusion_tag;
    }
  }
  if (!empty($stale_tags)) {
    drush_log(dt('Could not remove the backup exclusion tags (@tags) - a stale tag would silently shrink later backups; remove exclude.tag by hand', array('@tags' => implode(', ', $stale_tags))), 'warning');
  }

  $size = filesize($backup_file);
  drush_set_option('backup_file_size', $size);

  chdir($olddir);

  if (!$result && !drush_get_option('force', FALSE)) {
    drush_set_error('PROVISION_BACKUP_FAILED', dt("Could not back up sites directory for Textpattern"));
  }
}

/**
 * The exclusion lists (TXP constants; alter hook honoured like the others).
 */
function drush_provision_txp_provision_backup_get_exclusion_dirs($backup_mode = NULL) {
  if ($backup_mode == 'backup_mysqldump_only') {
    return PROVISION_TXP_BACKUP_EXCLUDED_DIRECTORIES_SQLDUMP;
  }
  $directories = PROVISION_TXP_BACKUP_EXCLUDED_DIRECTORIES;
  drush_command_invoke_all_ref('provision_backup_exclusions_alter', $directories);
  return $directories;
}

function drush_provision_txp_post_provision_backup() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  drush_set_option('backup_file', drush_get_option('backup_file'), 'process');
  drush_set_option('backup_file_size', drush_get_option('backup_file_size'), 'process');
}

function drush_provision_txp_provision_backup_rollback() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  $backup_file = drush_get_option('backup_file');
  if ($backup_file && provision_file()->exists($backup_file)->status()) {
    provision_file()->unlink($backup_file)
      ->succeed('Removed stale backup file @path')
      ->fail('Could not remove stale backup file @path');
  }
}
