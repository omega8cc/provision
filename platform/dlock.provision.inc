<?php

/**
 * Dummy drush hooks for the provision-dlock command
 */

function drush_provision_drupal_provision_dlock() {
  if (d()->type === 'platform') {
    $success = drush_shell_exec("sudo --non-interactive /usr/local/bin/lock-local-drush-permissions.sh --root=%s --mode=%s", d()->root, "lock");
  }
  else {
    return TRUE;
  }

  $result = drush_shell_exec_output();
  foreach ($result as $index => $line) {
    if (!$success) { // Log the entire error as a warning.
      $line_status = 'warning';
    }
    else { // Only log the last line as success.
      $line_status = $index+1 == count($result) ? 'success' : 'notice';
    }
    drush_log($line, $line_status);
  }
}
