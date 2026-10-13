<?php

/**
 * Provision backup command (Grav layer).
 *
 * Fork of backdrop/backup.provision.inc for the capsule: the archive is the
 * whole capsule minus the volatile set; every files-carrying mode
 * dereferences for a self-contained archive (capsules carry no store
 * symlinks today, so this is future-proofing, not behaviour). A site with a
 * database gets its dump from the db layer's pre hook (database.sql at the
 * capsule root, inside the archive, removed by the db post hook) or, modeless
 * with MyQuick, in tmp_expim, rotated to a dated directory as the Textpattern
 * layer does. A site on the 'none' server has no dump at all.
 * backup_mysqldump_only is REFUSED outright: the Drupal exclusion list it
 * would need tags ./vendor -- a leftover exclude.tag there would silently
 * strip the autoloader from every later archive.
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
  // before the guard below, and the db credentials NOW: the db service's pre
  // hook dumps through fetch_site_credentials() before any other hook here
  // runs, and an empty context means a silently dump-less backup.
  _provision_grav_ensure_db_creds_loaded();
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
  // rather than register an archive that restores nothing. Consume the ctrl
  // FIRST (when it was actually read): an early return would otherwise leave
  // the dead file mis-moding the next modeless backup on the account.
  if ($backup_mode == 'backup_mysqldump_only') {
    if (defined('AEGIR_BACKUP_MODE_CTRL_READ') && file_exists(AEGIR_BACKUP_MODE_CTRL)) {
      @unlink(AEGIR_BACKUP_MODE_CTRL);
    }
    return drush_set_error('PROVISION_GRAV_BACKUP_MODE_INVALID',
      dt('backup_mysqldump_only is not available for Grav sites; use a mode that carries the site files.'));
  }
  // Truthful label note: the mysqldump-carrying mode cannot add a database
  // that does not exist. On the 'none' server the archive is files-only.
  if ($backup_mode == 'site_files_with_mysqldump' && !_provision_grav_has_db()) {
    drush_log(dt('Grav site without a database: the archive carries the full site tree and no database dump.'), 'info');
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
  // precondition; a remote-web Grav topology needs a grav-aware
  // fetch first.

  $olddir = getcwd();

  if (!chdir(d()->site_path)) {
    return drush_set_error('PROVISION_BACKUP_PATH_NOT_FOUND', dt('cannot change directory to %dir', array('%dir' => d()->site_path)));
  }

  $backup_mode = provision_backup_mode_resolve();

  // Anything the instance user cannot read is a file the web tier created
  // (a key minted before the seeder ran, an account file rewritten by the
  // login plugin). Hand the tree back through the root heal before tar
  // walks it, so the archive is complete and the SECRETS row below is left
  // for a genuine fault.
  if (drush_shell_exec('find . -type f ! -readable 2>/dev/null')) {
    if (count(array_filter((array) drush_shell_exec_output()))) {
      _provision_grav_root_heal(d()->site_path);
    }
  }

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
    // An archive that silently carries the volatile trees is not the one
    // the operator asked for; a tag state this user cannot write is a real
    // fault on the capsule, so fail the task instead of colouring it. The
    // tags already written come out first: a refused backup leaves the
    // capsule as it found it.
    foreach ($exclusion_dirs as $exclusion_dir) {
      if (is_file($exclusion_dir . '/exclude.tag')) {
        @unlink($exclusion_dir . '/exclude.tag');
      }
    }
    chdir($olddir);
    return drush_set_error('PROVISION_BACKUP_FAILED',
      dt('Could not write the backup exclusion tags (@tags); the capsule needs a Verify before it can be backed up.', array('@tags' => implode(', ', $failed_tags))));
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
  // Captured now: the readability probe below overwrites Drush's last-output
  // buffer, and a failed retry replaces it with its own words.
  $tar_output = drush_shell_exec_output();

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
      $tar_output = drush_shell_exec_output();
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
  $size = filesize($backup_file);
  drush_set_option('backup_file_size', $size);

  chdir($olddir);

  if (!empty($stale_tags)) {
    // A leftover tag makes tar skip that directory in every later archive
    // without a word; for a capsule that can be vendor/ -- a backup that
    // restores a site that does not boot. This one goes UP, never down.
    return drush_set_error('PROVISION_BACKUP_FAILED',
      dt('Could not remove the backup exclusion tags (@tags); a stale tag would silently shrink every later backup -- remove exclude.tag by hand.', array('@tags' => implode(', ', $stale_tags))));
  }

  if (!$result) {
    // tar's own words, the same way the deploy-side extraction reports them;
    // a forced run tolerates the failure, so its lines are warnings.
    $forced = drush_get_option('force', FALSE);
    $first_line = provision_log_tar_output($tar_output, 'Backup archive error', $forced ? 'warning' : 'error');
    if (!$forced) {
      $reason = ($first_line === '') ? '' : dt(' (@first)', array('@first' => $first_line));
      drush_set_error('PROVISION_BACKUP_FAILED', dt("Could not back up the Grav site capsule") . $reason);
    }
  }
}

/**
 * The Grav backup exclusion set: the regenerable volatile dirs, relative to
 * the capsule root (spike-proven). The alter hook is applied for future
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

  // A modeless (MyQuick) backup of a site with a database: rotate tmp_expim
  // into the dated per-site dump dir, the dated DB retention every other CMS
  // gets; a stale tmp_expim left behind would feed a LATER clone or migrate
  // fast-import with the wrong database. Never for an internal backup, whose
  // deploy reads it.
  if (_provision_grav_has_db()) {
    $backup_path = d('@server_master')->backup_path;
    $internal_backup_flag = $backup_path . '/.internal_backup_flag.pid';
    $backup_mode = provision_backup_mode_resolve();
    if (!is_file($internal_backup_flag) && empty($backup_mode)) {
      $oct_db_dirx = $backup_path . '/tmp_expim';
      $uri_db_dirz = $backup_path . '/' . d()->uri . '_sql_dump_backup_' . date("Ymd.His", time());
      $enable_myquick = d('@server_master')->aegir_root . '/static/control/MyQuick.info';
      if (is_file($enable_myquick) && is_file($oct_db_dirx . '/metadata')) {
        provision_file()->switch_paths($oct_db_dirx, $uri_db_dirz)
          ->succeed('Moving the Post-DB-Backup @path1 to @path2 was successful.')
          ->fail('Moving the Post-DB-Backup @path1 to @path2 has failed.');
        if (is_dir($uri_db_dirz)) {
          _provision_recursive_delete($oct_db_dirx);
          drush_log(dt("MyQuick tmp_expim dir removed in drush_provision_grav_post_provision_backup @var", array('@var' => $oct_db_dirx)), 'info');
        }
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
 * Remove the backup file if something went wrong.
 */
function drush_provision_grav_provision_backup_rollback() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  $backup_file = drush_get_option('backup_file');
  // tmp_expim is ACCOUNT-SHARED: act only on a modeless run of a site with a
  // database that produced an archive, and keep the dump by rotating it to a
  // dated dir rather than deleting it (a concurrent clone's in-flight dump
  // lives there too).
  $backup_mode = provision_backup_mode_resolve();
  if (_provision_grav_has_db() && empty($backup_mode) && $backup_file && is_file($backup_file)) {
    $backup_path = d('@server_master')->backup_path;
    $oct_db_dirx = $backup_path . '/tmp_expim';
    $enable_myquick = d('@server_master')->aegir_root . '/static/control/MyQuick.info';
    if (is_file($enable_myquick) && is_file($oct_db_dirx . '/metadata')) {
      $uri_db_dirz = $backup_path . '/' . d()->uri . '_sql_dump_rollback_' . date("Ymd.His", time());
      provision_file()->switch_paths($oct_db_dirx, $uri_db_dirz)
        ->succeed('Moving the rollback DB dump @path1 to @path2 was successful.')
        ->fail('Moving the rollback DB dump @path1 to @path2 has failed.');
      if (is_dir($uri_db_dirz)) {
        _provision_recursive_delete($oct_db_dirx);
        drush_log(dt("MyQuick tmp_expim dir rotated in drush_provision_grav_provision_backup_rollback @var", array('@var' => $uri_db_dirz)), 'info');
      }
    }
  }
  if (is_readable($backup_file) && is_file($backup_file)) {
    provision_file()->unlink($backup_file)
      ->succeed('Removed stale backup file @path')
      ->fail('Failed deleting backup file @path');
  }
}
