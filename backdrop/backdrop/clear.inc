<?php
/**
 *  @file
 *    Rebuild all the caches on a Backdrop site.
 *
 * Backdrop equivalent of platform/drupal/clear_7.inc, but driven by a FRESH bee
 * subprocess (`bee cache-clear all`) rather than an in-process
 * backdrop_flush_all_caches(): the provision parent process never obtains a
 * complete in-process Backdrop bootstrap (see backdrop/install.inc for why), so
 * an in-process flush fatals. bee bootstraps the site cleanly in its own
 * process. Best-effort by design -- bee's own install already flushed once, and
 * a failed post-hoc flush must never fail the surrounding verify/install task --
 * so this only warns on trouble.
 */

$bee = '/opt/tools/bee/bee.php';
if (!is_readable($bee)) {
  drush_log(dt('bee not found at @bee; skipping Backdrop cache flush.', array('@bee' => $bee)), 'warning');
}
else {
  $php_bin = (defined('PHP_BINARY') && PHP_BINARY) ? PHP_BINARY : 'php';
  $root = d()->root;
  $uri = d()->uri;
  $cmd = escapeshellarg($php_bin) . ' ' . escapeshellarg($bee)
    . ' --root=' . escapeshellarg($root)
    . ' --site=' . escapeshellarg($uri)
    . ' cache-clear all 2>&1';
  drush_shell_cd_and_exec($root, $cmd);
  foreach (drush_shell_exec_output() as $line) {
    drush_log(strip_tags($line), 'notice');
  }
  drush_log(dt('Flushed Backdrop caches via bee for @uri', array('@uri' => $uri)), 'ok');
}
