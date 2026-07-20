<?php

/**
 * @file
 * provision-leftover-import command hooks (D-035).
 *
 * Inspect a revert-artifact leftover (sites/<site>.restore or .original)
 * stranded on a platform by an aborted deploy/restore/migrate swap and report
 * its facts back to the frontend, which registers a pseudo-site node for it
 * so it becomes visible (and purgeable) in the Aegir UI. Strictly read-only:
 * nothing on disk or in any database is changed here.
 *
 * The leftover syndrome is shared by Drupal and Backdrop alike, so this is
 * general-layer code: any platform context, both settings.php shapes, no CMS
 * bootstrap.
 *
 * PHP 5.6 floor applies (parse AND runtime).
 */

/**
 * Implementation of drush_hook_COMMAND_validate().
 */
function drush_provision_leftover_import_validate($leftover = NULL) {
  if (d()->type !== 'platform') {
    return drush_set_error('PROVISION_LEFTOVER_CONTEXT',
      dt('provision-leftover-import must run on a platform context.'));
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
function drush_provision_leftover_import($leftover = NULL) {
  $path = d()->root . '/sites/' . $leftover;

  if (is_link($path)) {
    return drush_set_error('PROVISION_LEFTOVER_SYMLINK',
      dt('@path is a symlink, not a leftover directory; refusing to adopt it.', array('@path' => $path)));
  }
  if (!is_dir($path)) {
    return drush_set_error('PROVISION_LEFTOVER_MISSING',
      dt('No leftover directory at @path -- already removed? Re-verify the platform to refresh the leftover list.',
      array('@path' => $path)));
  }

  $info = _provision_leftover_parse_settings($path);
  $info['leftover'] = $leftover;
  $info['mtime'] = filemtime($path);

  // Best-effort human-scale size; never fatal (du missing/unreadable entries).
  $info['size'] = '';
  if (drush_shell_exec('du -sh %s', $path)) {
    $du = drush_shell_exec_output();
    if (!empty($du[0])) {
      $parts = preg_split('/\s+/', trim($du[0]));
      if (!empty($parts[0])) {
        $info['size'] = $parts[0];
      }
    }
  }

  drush_set_option('leftover_info', $info, 'drupal');

  drush_log(dt('Leftover @name inspected: settings.php db @db on host @host, last modified @date, size @size.',
    array(
      '@name' => $leftover,
      '@db' => $info['db_name'] !== '' ? $info['db_name'] : dt('(none parsed)'),
      '@host' => $info['db_host'] !== '' ? $info['db_host'] : dt('(none parsed)'),
      '@date' => $info['mtime'] ? date('Y-m-d H:i:s', $info['mtime']) : dt('(unknown)'),
      '@size' => $info['size'] !== '' ? $info['size'] : dt('(unknown)'),
    )), 'ok');
  if ($info['db_name'] === '') {
    drush_log(dt('No literal database name could be parsed from the leftover settings.php; a later purge will remove the directory only.'), 'warning');
  }
}
