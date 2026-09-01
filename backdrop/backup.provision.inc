<?php

/**
 * Provision backup command (Backdrop layer).
 *
 * Fork of platform/backup.provision.inc: CMS-agnostic tarball + sqldump. The
 * D8+ site-local Drush lock block is dropped and the settings writer is the
 * Backdrop one. The PROVISION_BACKUP_EXCLUDED_DIRECTORIES* constants are defined
 * by the always-loaded Drupal layer and reused here.
 */

/**
 * Make sure the site is installed and enabled, and that we have a valid target to back up to.
 */
function drush_provision_backdrop_provision_backup_validate($backup_file = NULL) {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }

  // NO bootstrap here (the Drupal engine's configuration-phase attempt is
  // dropped): under BackdropBoot an in-process bootstrap can die with a hard
  // PHP fatal that no @/if() guard can catch -- a cloned site's pre-delete
  // backup died exactly there. Nothing in the backup needs it: the tarball is
  // filesystem work, the sqldump runs on the provision DB service, and the
  // installedness check below reads the persisted site drushrc.
  //
  // drush's bootstrap does not reload the Backdrop site drushrc into the context,
  // so the persisted 'installed' flag is absent on a bare `drush @site backup`
  // (as invoked by the clone pre-hook) -- restore it before the guard below, or
  // an installed site is wrongly rejected as PROVISION_DRUPAL_SITE_NOT_FOUND.
  _provision_backdrop_ensure_db_creds_loaded();
  if (!drush_get_option('installed') && !drush_get_option('force', false)) {
     drush_set_error('PROVISION_DRUPAL_SITE_NOT_FOUND');
  }

  // This is the actual drupal provisioning requirements.
  if (!is_dir(d()->platform->server->backup_path)) {
    drush_set_error('PROVISION_BACKUP_PATH_NOT_FOUND');
  }

  // Latch the mode HERE, before the filename branches: an explicit-$backup_file
  // invocation used to skip suggest_filename and reach the db pre hook with
  // nothing latched, so a ctrl-selected site_files_without_db_backup still
  // generated database.sql - straight into the "no DB" archive - and the ctrl
  // file was never consumed. Resolving up front makes both branches identical.
  provision_backup_mode_resolve();

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
    $suggested = drush_provision_backdrop_provision_backup_suggest_filename();
    drush_set_option('backup_file', $suggested);
  }

  // One-shot consume: by now the mode (if any) is latched in
  // SELECTED_BACKUP_MODE for this whole process; removing the ctrl file
  // immediately shrinks the window in which a concurrent task's implicit
  // backup could inherit it. Only when the mode was actually READ from
  // the ctrl file - the constant latches FALSE on modeless backups and
  // is force-defined without any read for the restore safety copy;
  // consuming in either case could delete a concurrent task's freshly
  // written file this process never used.
  if (defined('AEGIR_BACKUP_MODE_CTRL_READ') && file_exists(AEGIR_BACKUP_MODE_CTRL)) {
    @unlink(AEGIR_BACKUP_MODE_CTRL);
  }

}

/**
 * Util function to suggest a filename for a new site backup.
 */
function drush_provision_backdrop_provision_backup_suggest_filename() {

  $backup_mode = provision_backup_mode_resolve();

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
  elseif (!provision_is_hostmaster_site()) {
    // Historically keyed on isset($backup_mode), which was assigned exactly
    // for non-hostmaster flows; spelled out now that the resolver defines
    // $backup_mode (as FALSE) for hostmaster too - the hostmaster suffix
    // must stay '.tar'.
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
function drush_provision_backdrop_provision_backup() {
  if (!provision_platform_is_backdrop(d())) {
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
    // Close the plaintext window even if this run aborts before the restore
    // below -- the chdir() failure a few lines down returns early.
    register_shutdown_function('_provision_backdrop_recloak_after_backup');
    // Disable the cloaking of credentials temporarily
    drush_log(dt("Temporarily uncloaking database credentials for backup"));
    drush_set_option('provision_db_cloaking', FALSE);

    // Write the uncloaked credentials to the settings.php
    _provision_backdrop_create_settings_file();
    provision_drupal_push_site();

  }

  $olddir = getcwd();

  if (!chdir(d()->site_path)) {
    return drush_set_error('PROVISION_BACKUP_PATH_NOT_FOUND', dt('cannot change directory to %dir', array('%dir' => d()->site_path)));
  }

  $backup_mode = provision_backup_mode_resolve();

  // Tag the excluded directories with exclude.tag markers for tar, per
  // existing directory only: a fresh site lacks most of the excluded
  // directories, and a wholesale `touch dir1/exclude.tag dir2/...` fails
  // the whole command on the first missing one. Stale tags are cleared
  // first (a leftover tag makes tar skip that directory in every future
  // backup). Mirrors the Drupal-layer backup.
  $exclusion_dirs = drush_provision_backdrop_provision_backup_get_exclusion_dirs(isset($backup_mode) ? $backup_mode : NULL);
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

  // A site dir must never contain device/block/fifo/socket special files. A
  // stray one is archived by tar as a special member; a later non-root tar -pxf
  // on deploy/migrate/clone/restore then cannot recreate it (mknod needs
  // CAP_MKNOD) and aborts the whole extraction. Strip any such node here, at the
  // source. cwd is the site dir; -xdev stays on the site's own filesystem and,
  // with no -L, never follows the files/private symlinks into the store.
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
  // Remove the tags per existing file; a tag that exists but cannot be
  // removed silently truncates every future backup, so say so loudly.
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

  // Get the size of the backup
  $size = filesize($backup_file);
  drush_set_option('backup_file_size', $size);

  chdir($olddir);

  _provision_backdrop_recloak_after_backup();

  if (!$result && !drush_get_option('force', false)) {
    drush_set_error('PROVISION_BACKUP_FAILED', dt("Could not back up sites directory for backdrop"));
  }
}

/**
 * Return the list of directories to exclude from a backup archive.
 *
 * Mirrors the Drupal-layer helper: mysqldump-only mode excludes all content
 * directories; every other mode excludes only the volatile cache
 * directories, with the alter hook applied.
 */
function drush_provision_backdrop_provision_backup_get_exclusion_dirs($backup_mode = NULL) {
  if ($backup_mode == 'backup_mysqldump_only') {
    return PROVISION_BACKUP_EXCLUDED_DIRECTORIES_SQLDUMP;
  }
  $directories = PROVISION_BACKUP_EXCLUDED_DIRECTORIES;
  drush_command_invoke_all_ref('provision_backup_exclusions_alter', $directories);
  return $directories;
}

function drush_provision_backdrop_provision_backup_get_exclusions() {
  $directories = PROVISION_BACKUP_EXCLUDED_DIRECTORIES;
  drush_command_invoke_all_ref('provision_backup_exclusions_alter', $directories);
  $exclusions = "";
  foreach ($directories as $directory) {
    $exclusions .= " '$directory'/exclude.tag";
  }
  drush_log(dt("In drush_provision_backdrop_provision_backup_get_exclusions: @var", array('@var' => $exclusions)), 'info');
  return $exclusions;
}

function drush_provision_backdrop_provision_backup_get_exclusions_sqldump() {
  $directories = PROVISION_BACKUP_EXCLUDED_DIRECTORIES_SQLDUMP;
  $exclusions = "";
  foreach ($directories as $directory) {
    $exclusions .= " '$directory'/exclude.tag";
  }
  drush_log(dt("In drush_provision_backdrop_provision_backup_get_exclusions_sqldump: @var", array('@var' => $exclusions)), 'info');
  return $exclusions;
}

function drush_provision_backdrop_post_provision_backup() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  drush_log(dt('Backed up site up to @path.', array('@path' => drush_get_option('backup_file'))), 'success');

  $backup_path = d('@server_master')->backup_path;
  $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
  if (!is_file($internal_backup_flag)) {

    $enable_myquick = FALSE;
    $oct_db_test = FALSE;
    $myquick_creds_log = '/data/conf/_myquick_creds_log.txt';

    $backup_mode = provision_backup_mode_resolve();

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
        drush_log(dt("MyQuick tmp_expim dir removed in drush_provision_backdrop_post_provision_backup @var", array('@var' => $old)), 'info');
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
function drush_provision_backdrop_provision_backup_rollback() {
  if (!provision_platform_is_backdrop(d())) {
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

    $backup_mode = provision_backup_mode_resolve();

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
        drush_log(dt("MyQuick tmp_expim dir removed in drush_provision_backdrop_provision_backup_rollback @var", array('@var' => $old)), 'info');
      }
    }
  }
}

/**
 * Restore credential cloaking after a backup.
 *
 * Backup temporarily writes the credentials into settings.php in the clear so
 * the archive is restorable on a host that has no vhost to supply them. That
 * window must close even when the run does not reach its end: an early return
 * or a fatal in between would otherwise leave a live site with a plaintext
 * password in a settings.php the web server user can read, which is exactly
 * what cloaking exists to prevent. Registered as a shutdown function and also
 * called on the normal path; the option is cleared first so it only ever runs
 * once.
 */
// Named per engine: sharing one name with the Drupal command file meant a
// redeclare guard picked whichever loaded first, and both bodies wrote the
// DRUPAL settings file -- so re-cloaking replaced a Backdrop site's
// settings.php with a Drupal-shaped one. The writer must match the engine;
// push_site is genuinely shared (see provision_backdrop.drush.inc).
function _provision_backdrop_recloak_after_backup() {
  if (!drush_get_option('cloaking_off_temp', FALSE)) {
    return;
  }
  drush_set_option('cloaking_off_temp', FALSE);
  drush_log(dt("Re-cloaking database credentials after backup"));
  drush_set_option('provision_db_cloaking', TRUE);
  _provision_backdrop_create_settings_file();
  provision_drupal_push_site();
}
