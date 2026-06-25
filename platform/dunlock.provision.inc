<?php

/**
 * Drush hooks for the provision-dunlock command ("Unlock Local Drush" task).
 *
 * Thin wrapper: delegate to the canonical provision_unlock_drush_local_vnd() in
 * provision.inc, after the platform-root auto-fix. Detection-based idempotency
 * (no marker/pid files): a re-run on an already-unlocked platform is a clean
 * no-op rather than re-running patch reverts / composer reinstall noise.
 */

function drush_provision_dunlock() {
  d()->root = provision_auto_fix_platform_root(d()->root);
  provision_check_codebase_status();
  if (provision_drush_local_lock_state() == 'unlocked') {
    drush_log(dt('Local Drush already unlocked in @root; nothing to do.', array('@root' => d()->root)), 'success');
    return;
  }
  provision_unlock_drush_local_vnd();
}
