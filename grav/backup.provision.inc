<?php

/**
 * Provision backup command (Grav layer).
 *
 * Fork of backdrop/backup.provision.inc collapsed to the DB-less capsule
 * reality: no sqldump, no credential cloaking, no MyQuick tmp_expim machinery.
 * The archive is the whole capsule minus the volatile set; every
 * files-carrying mode dereferences for a self-contained archive (capsules
 * carry no store symlinks today, so this is future-proofing, not behaviour).
 * backup_mysqldump_only is REFUSED outright: meaningless for a DB-less site,
 * and its Drupal exclusion list tags ./vendor -- a leftover exclude.tag there
 * would silently strip the autoloader from every later archive.
 */

/**
 * Make sure the site is installed and enabled, and that we have a valid target to back up to.
 */
function drush_provision_grav_provision_backup_validate($backup_file = NULL) {
  if (!provision_platform_is_grav(d())) {
    return;
  }

  // No bootstrap and no CMS code: the tarball is filesystem work. The site
  // drushrc is not auto-loaded on a bare `drush @site provision-backup`
  // (proven on the TXP track), so restore the persisted 'installed' flag
  // before the guard below.
  _provision_grav_ensure_installed_loaded();
  if (!drush_get_option('installed') && !drush_get_option('force', false)) {
     drush_set_error('PROVISION_DRUPAL_SITE_NOT_FOUND');
  }

  if (!is_dir(d()->platform->server->backup_path)) {
    drush_set_error('PROVISION_BACKUP_PATH_NOT_FOUND');
  }

  // Latch the mode up front so both filename branches behave identically
  // (the once-only resolver; never re-read the ctrl file).
  $backup_mode = provision_backup_mode_resolve();

  // A DB-only backup of a DB-less site is an empty promise; refuse loudly
  // rather than register an archive that restores nothing.
  if ($backup_mode == 'backup_mysqldump_only') {
    return drush_set_error('PROVISION_GRAV_BACKUP_MODE_INVALID',
      dt('backup_mysqldump_only is not available for Grav sites (no database in phase 1).'));
  }
  // Truthful label note: the mysqldump-carrying mode cannot add a database
  // that does not exist. The archive is files-only either way.
  if ($backup_mode == 'site_files_with_mysqldump') {
    drush_log(dt('DB-less Grav site: the archive carries the full site tree and no database dump.'), 'info');
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
    $suggested = drush_provision_grav_provision_backup_suggest_filename();
    drush_set_option('backup_file', $suggested);
  }

  // One-shot consume, only when the mode was actually READ from the ctrl file
  // (same semantics and same rationale as the Backdrop fork).
  if (defined('AEGIR_BACKUP_MODE_CTRL_READ') && file_exists(AEGIR_BACKUP_MODE_CTRL)) {
    @unlink(AEGIR_BACKUP_MODE_CTRL);
  }
}

/**
 * Reload the persisted 'installed' flag from the site drushrc (regex, no
 * include). Namespaced copy of the TXP helper, minus the DB credentials a
 * phase-1 Grav site does not have.
 */
function _provision_grav_ensure_installed_loaded() {
  if (d()->type !== 'site') {
    return;
  }
  if (drush_get_option('installed')) {
    return;
  }
  $drushrc = d()->site_path . '/drushrc.php';
  if (!is_readable($drushrc)) {
    return;
  }
  $body = file_get_contents($drushrc);
  if ($body !== FALSE
    && preg_match('/\\$options\\s*\\[\\s*[\'"]installed[\'"]\\s*\\]\\s*=\\s*(TRUE|true|1)\\s*;/', $body)) {
    drush_set_option('installed', TRUE, 'site');
  }
}

/**
 * Util function to suggest a filename for a new site backup.
 */
function drush_provision_grav_provision_backup_suggest_filename() {
  $backup_mode = provision_backup_mode_resolve();

  // If mounted filesystem for files then use zst (optionally with tar pipelines)
  $mounted_arch = "/data/disk/arch";
  $mounted_bak = d()->platform->server->backup_path;

  if (is_link($mounted_arch) || is_link($mounted_bak)) {
    if ($backup_mode == 'site_files_without_db_backup' ||
      $backup_mode == 'site_files_with_mysqldump') {
      $suffix = drush_get_option('provision_backup_suffix', '.tar.gz');
    }
    else {
      $suffix = drush_get_option('provision_backup_suffix', '.tar.zst');
    }
  }
  else {
    $suffix = drush_get_option('provision_backup_suffix', '.tar.gz');
  }
  $suggested = d()->platform->server->backup_path . '/' . d()->uri . '-' . date("Ymd.His", time()) . $suffix;

  $count = 0;
  while (is_file($suggested)) {
    $count++;
    $suggested = d()->platform->server->backup_path . '/' . d()->uri . '-' .  date('Ymd.His', time()) . '_' . $count . $suffix;
  }
  return $suggested;
}

/**
 * Implementation of hook_provision_backup().
 */
function drush_provision_grav_provision_backup() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  $backup_file = drush_get_option('backup_file');
  drush_log(dt("Adding the site capsule to !backup_file", array('!backup_file' => $backup_file)), 'backup');

  // NB no provision_drupal_fetch_site() here: its sync set is Drupal-shaped
  // (files/, private/, modules/, ...) and phase 1 is single-server by stated
  // precondition (map par.4); a remote-web Grav topology needs a grav-aware
  // fetch first.

  $olddir = getcwd();

  if (!chdir(d()->site_path)) {
    return drush_set_error('PROVISION_BACKUP_PATH_NOT_FOUND', dt('cannot change directory to %dir', array('%dir' => d()->site_path)));
  }

  $backup_mode = provision_backup_mode_resolve();

  // Tag the excluded directories with exclude.tag markers for tar, per
  // existing directory only; stale tags are cleared first (a leftover tag
  // makes tar skip that directory in every future backup).
  $exclusion_dirs = drush_provision_grav_provision_backup_get_exclusion_dirs();
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

  // Strip stray special files at the source (a non-root tar -pxf on
  // deploy/restore cannot mknod and would abort the whole extraction).
  $strip_specials = 'find . -xdev \( -type b -o -type c -o -type p -o -type s \) -print -delete 2>/dev/null';
  if (drush_shell_exec($strip_specials)) {
    $stripped_specials = drush_shell_exec_output();
    if (!empty($stripped_specials)) {
      foreach ($stripped_specials as $stray_special) {
        drush_log(dt("BACKUP/STRIP removed stray special file from site dir before archiving: @f", array('@f' => $stray_special)), 'warning');
      }
    }
  }

  $force_symlinks = "/data/conf/force_symlinks.conf";
  $deref_backups = "/data/conf/dereference_backups.conf";

  if (!empty($backup_mode) && ($backup_mode == 'site_files_with_mysqldump' ||
    $backup_mode == 'site_files_without_db_backup')) {
    // Files-carrying mode: self-contained archive.
    $command_base = "tar cphf";
  }
  elseif (is_file($deref_backups) && !is_file($force_symlinks)) {
    $command_base = "tar cphf";
  }
  else {
    // Default: do not follow symlinks (capsules ship none today; parity with
    // the fleet ladder).
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
      $command_base = "tar -c -p -h -I lz4 -f";
    }
    elseif (is_file($deref_backups) && !is_file($force_symlinks)) {
      $command_base = "tar -c -p -h -I lz4 -f";
    }
    else {
      $command_base = "tar -c -p -I lz4 -f";
    }
    $command = $command_base . $command_arguments;
  }
  elseif (substr($backup_file, -3) == 'zst') {
    if (!empty($backup_mode) && ($backup_mode == 'site_files_with_mysqldump' ||
      $backup_mode == 'site_files_without_db_backup')) {
      $command_base = "tar -c -p -h -I zstd -f";
    }
    elseif (is_file($deref_backups) && !is_file($force_symlinks)) {
      $command_base = "tar -c -p -h -I zstd -f";
    }
    else {
      $command_base = "tar -c -p -I zstd -f";
    }
    $command = $command_base . $command_arguments;
  }
  else {
    $command = $command_base . $command_arguments;
  }
  $command .= ' .';
  $result = drush_shell_exec($command,  $backup_file);

  // Web-only secrets: Grav's web process writes user/config/*-private.php
  // (security nonce salt, api JWT signing key) owner-web at an explicit 0600,
  // unreadable by the CLI owner BY DESIGN -- strict tar exits 2 on them. When
  // that is the only failure class, degrade honestly: name every unreadable
  // file, warn that a restore regenerates the nonce salt (sessions reset) and
  // invalidates issued API tokens, and re-run with --ignore-failed-read. Any
  // other tar failure still fails the backup.
  if (!$result) {
    $unreadable = array();
    if (drush_shell_exec('find . -type f ! -readable 2>/dev/null')) {
      $unreadable = array_filter(drush_shell_exec_output());
    }
    if (!empty($unreadable)) {
      foreach ($unreadable as $secret) {
        drush_log(dt('BACKUP/SECRETS unreadable by the CLI owner, NOT in the archive: @f (web-only secret; a restore regenerates it -- sessions reset, issued API tokens invalidated)',
          array('@f' => $secret)), 'warning');
      }
      if (is_file($backup_file)) {
        @unlink($backup_file);
      }
      $retry = str_replace(' --exclude-tag=', ' --ignore-failed-read --exclude-tag=', $command);
      $result = drush_shell_exec($retry, $backup_file);
    }
  }

  // Remove the tags per existing file; a stale tag silently shrinks every
  // later backup, so say so loudly.
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

  if (!$result && !drush_get_option('force', false)) {
    drush_set_error('PROVISION_BACKUP_FAILED', dt("Could not back up the Grav site capsule"));
  }
}

/**
 * The Grav backup exclusion set: the regenerable volatile dirs, relative to
 * the capsule root (spike-4-proven). The alter hook is applied for future
 * extension; there is no sqldump list (that mode is refused in validate).
 */
function drush_provision_grav_provision_backup_get_exclusion_dirs($backup_mode = NULL) {
  $directories = array(
    './cache',
    './logs',
    './tmp',
    './backup',
    './assets',
    './images',
  );
  drush_command_invoke_all_ref('provision_backup_exclusions_alter', $directories);
  return $directories;
}

function drush_provision_grav_post_provision_backup() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  drush_log(dt('Backed up site up to @path.', array('@path' => drush_get_option('backup_file'))), 'success');

  if (d()->client_name) {
    $backup_dir = d()->server->clients_path . '/' . d()->client_name . '/backups';
    provision_file()->create_dir($backup_dir, dt('Client backup directory for @client', array('@client' => d()->client_name)), 0750);
    provision_file()->symlink(drush_get_option('backup_file'), $backup_dir . '/' . basename(drush_get_option('backup_file')))
      ->succeed('Created symlink @path to @target')
      ->fail('Could not create symlink @path to @target: @reason');
  }
}

/**
 * Remove the backup file if something went wrong.
 */
function drush_provision_grav_provision_backup_rollback() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  $backup_file = drush_get_option('backup_file');
  if (is_readable($backup_file) && is_file($backup_file)) {
    provision_file()->unlink($backup_file)
      ->succeed('Removed stale backup file @path')
      ->fail('Failed deleting backup file @path');
  }
}
