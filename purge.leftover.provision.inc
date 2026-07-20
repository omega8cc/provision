<?php

/**
 * @file
 * provision-leftover-purge command hooks (D-035).
 *
 * Delete a revert-artifact leftover (sites/<site>.restore or .original)
 * stranded on a platform by an aborted deploy/restore/migrate swap, and drop
 * its orphan copy database when -- and only when -- it is provably unused:
 *
 *  - the name recorded by the frontend at expose time must match a fresh
 *    re-parse of the leftover's own settings.php (two-source agreement);
 *  - no other sites/<dir>/settings.php on the platform may reference it
 *    (covers the crashed-mid-swap case where the artifact still points at the
 *    site's CURRENT database);
 *  - system databases are refused outright.
 *
 * Anything short of full agreement degrades to a directory-only purge with a
 * logged reason -- never a guessed drop. The leftover syndrome is shared by
 * Drupal and Backdrop alike, so this is general-layer code: any platform
 * context, both settings.php shapes, no CMS bootstrap.
 *
 * PHP 5.6 floor applies (parse AND runtime).
 */

/**
 * Implementation of drush_hook_COMMAND_validate().
 */
function drush_provision_leftover_purge_validate($leftover = NULL) {
  if (d()->type !== 'platform') {
    return drush_set_error('PROVISION_LEFTOVER_CONTEXT',
      dt('provision-leftover-purge must run on a platform context.'));
  }
  if (!_provision_leftover_valid_name($leftover)) {
    return drush_set_error('PROVISION_LEFTOVER_NAME',
      dt('!name is not a revert-artifact leftover name (expected <site>.restore or <site>.original).',
      array('!name' => (string) $leftover)));
  }
}

/**
 * Implementation of drush_hook_COMMAND().
 */
function drush_provision_leftover_purge($leftover = NULL) {
  $root = d()->root;
  $path = $root . '/sites/' . $leftover;

  if (is_link($path)) {
    return drush_set_error('PROVISION_LEFTOVER_SYMLINK',
      dt('@path is a symlink, not a leftover directory; refusing to purge it.', array('@path' => $path)));
  }

  if (!is_dir($path)) {
    // Already removed out-of-band (manual cleanup, earlier purge): the goal
    // state is reached. Without the directory the recorded db name cannot be
    // re-confirmed, so no database is touched.
    drush_log(dt('No leftover directory at @path -- nothing to remove. The database (if any) was left untouched: without the directory the recorded name cannot be re-confirmed.',
      array('@path' => $path)), 'warning');
    return;
  }

  // Decide the database leg BEFORE deleting the directory (the parse needs it).
  $expected = drush_get_option('leftover_db_name', '');
  $expected_host = drush_get_option('leftover_db_host', '');
  $parsed = _provision_leftover_parse_settings($path);
  $drop = '';
  if (!is_string($expected) || $expected === '') {
    drush_log(dt('No recorded database name was passed; purging the directory only.'), 'info');
  }
  elseif ($parsed['db_name'] === '') {
    drush_log(dt('The leftover settings.php no longer yields a literal database name; purging the directory only.'), 'warning');
  }
  elseif ($parsed['db_name'] !== $expected) {
    drush_log(dt('Recorded database @expected does not match the leftover settings.php (@parsed); purging the directory only.',
      array('@expected' => $expected, '@parsed' => $parsed['db_name'])), 'warning');
  }
  elseif (is_string($expected_host) && $expected_host !== ''
    && $parsed['db_host'] !== '' && $parsed['db_host'] !== $expected_host) {
    drush_log(dt('Recorded database host @expected does not match the leftover settings.php (@parsed); purging the directory only.',
      array('@expected' => $expected_host, '@parsed' => $parsed['db_host'])), 'warning');
  }
  elseif (in_array(strtolower($expected), array('mysql', 'information_schema', 'performance_schema', 'sys', 'test'), TRUE)) {
    drush_log(dt('@db is a system database; refusing to drop it. Purging the directory only.',
      array('@db' => $expected)), 'warning');
  }
  elseif ($holder = _provision_leftover_db_in_use($root, $leftover, $expected)) {
    drush_log(dt('Database @db is still referenced by sites/@holder/settings.php on this platform -- NOT an orphan; purging the directory only.',
      array('@db' => $expected, '@holder' => $holder)), 'warning');
  }
  else {
    $drop = $expected;
  }

  if ($drop !== '') {
    // The drop is best-effort cleanup, never the task's success criterion --
    // but d() on an unloadable server context fires drush_set_error itself
    // (provision_context_factory), which would fail the whole task AFTER the
    // directory removal and leave the pseudo-site un-retired. Snapshot and
    // restore the error code around the drop (established pattern, see
    // platform/deploy.provision.inc update_fetch clear).
    $pre_error = drush_get_context('DRUSH_ERROR_CODE', DRUSH_SUCCESS);
    _provision_leftover_drop_db($drop, $parsed['db_host']);
    drush_set_context('DRUSH_ERROR_CODE', $pre_error);
  }

  _provision_recursive_delete($path);
  clearstatcache();
  if (is_dir($path)) {
    return drush_set_error('PROVISION_LEFTOVER_DELETE_FAILED',
      dt('Could not remove the leftover directory @path.', array('@path' => $path)));
  }
  drush_log(dt('Removed the leftover directory @path.', array('@path' => $path)), 'ok');
}

/**
 * Check whether any OTHER site dir on the platform references a database.
 *
 * Sweeps every sites/<dir>/settings.php except the leftover being purged;
 * a match means the database is NOT an orphan (typically the crashed-mid-swap
 * shape, where the artifact and the live site dir still share credentials).
 *
 * @param string $root
 *   The platform root.
 * @param string $leftover
 *   The leftover directory name being purged (excluded from the sweep).
 * @param string $db_name
 *   The database name to look for.
 *
 * @return string
 *   The first referencing directory name, or '' when none reference it.
 */
function _provision_leftover_db_in_use($root, $leftover, $db_name) {
  $handle = opendir($root . '/sites');
  if (!$handle) {
    // Cannot prove the negative -- report "in use" so the drop is skipped.
    drush_log(dt('Cannot open @path to prove the database is orphaned.', array('@path' => $root . '/sites')), 'warning');
    return dt('(unreadable sites directory)');
  }
  $found = '';
  while (FALSE !== ($subdir = readdir($handle))) {
    if ($subdir === '.' || $subdir === '..' || $subdir === $leftover) {
      continue;
    }
    $dir = $root . '/sites/' . $subdir;
    if (!is_dir($dir) || is_link($dir)) {
      continue;
    }
    $other = _provision_leftover_parse_settings($dir);
    if ($other['db_name'] !== '' && $other['db_name'] === $db_name) {
      $found = $subdir;
      break;
    }
  }
  closedir($handle);
  return $found;
}

/**
 * Drop the orphan copy database on the recorded db server context.
 *
 * Best-effort by design: a missing server context or an absent database logs
 * a warning and leaves the directory purge to proceed -- the drop is cleanup,
 * not the task's success criterion.
 *
 * @param string $db_name
 *   The database to drop (already through every safety gate).
 * @param string $db_host
 *   The db host the artifact's settings.php names ('' when unparsed). A
 *   non-local host must match the drop server's remote_host -- server-binding
 *   guard against dropping a same-named database on the wrong server.
 */
function _provision_leftover_drop_db($db_name, $db_host = '') {
  $server_name = drush_get_option('leftover_db_server', '');
  if (!is_string($server_name) || $server_name === '') {
    drush_log(dt('No db server context was passed; leaving database @db in place.', array('@db' => $db_name)), 'warning');
    return;
  }
  $server = d('@' . ltrim($server_name, '@'), FALSE, FALSE);
  if (is_null($server) || !is_object($server) || $server->type !== 'server') {
    drush_log(dt('Db server context @name could not be loaded; leaving database @db in place.',
      array('@name' => $server_name, '@db' => $db_name)), 'warning');
    return;
  }
  // Server-binding guard: when the artifact names a remote db host, it must
  // be the host this server context talks to. Local spellings are trusted to
  // the frontend's server choice (single-box BOA norm).
  if (is_string($db_host) && $db_host !== ''
    && !in_array(strtolower($db_host), array('localhost', '127.0.0.1'), TRUE)) {
    $remote = isset($server->remote_host) && is_string($server->remote_host) ? $server->remote_host : '';
    if ($remote !== '' && strtolower($db_host) !== strtolower($remote)) {
      drush_log(dt('The leftover names db host @host but @name talks to @remote; leaving database @db in place.',
        array('@host' => $db_host, '@name' => $server_name, '@remote' => $remote, '@db' => $db_name)), 'warning');
      return;
    }
  }
  // service() indexes into ->services without an isset guard; probe first.
  if (!array_key_exists('db', $server->get_services())) {
    drush_log(dt('Db server context @name carries no db service; leaving database @db in place.',
      array('@name' => $server_name, '@db' => $db_name)), 'warning');
    return;
  }
  $service = $server->service('db');
  if (!$service->database_exists($db_name)) {
    drush_log(dt('Database @db does not exist on @name -- nothing to drop.',
      array('@db' => $db_name, '@name' => $server_name)), 'info');
    return;
  }
  if ($service->drop_database($db_name)) {
    drush_log(dt('Dropped the orphan copy database @db on @name.',
      array('@db' => $db_name, '@name' => $server_name)), 'ok');
  }
  else {
    drush_log(dt('Failed to drop database @db on @name; drop it manually if it is confirmed orphaned.',
      array('@db' => $db_name, '@name' => $server_name)), 'warning');
  }
}
