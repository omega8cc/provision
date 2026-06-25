<?php

/**
 * Drush hooks for the provision-dlock command ("Lock Local Drush" task).
 *
 * Thin wrapper: delegate to the canonical, battle-tested lock that the verify
 * hooks use (provision_lock_drush_local_vnd() in provision.inc), after the
 * platform-root auto-fix verify performs before locking. Detection-based
 * idempotency (no marker/pid files): a re-run on an already-locked platform is
 * a clean no-op rather than redundant work.
 */

function drush_provision_dlock() {
  d()->root = provision_auto_fix_platform_root(d()->root);
  provision_check_codebase_status();
  if (provision_drush_local_lock_state() == 'locked') {
    drush_log(dt('Local Drush already locked in @root; nothing to do.', array('@root' => d()->root)), 'success');
    return;
  }
  provision_lock_drush_local_vnd();
}
