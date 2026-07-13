<?php

/**
 * Drush hooks for the provision-dlock command ("Lock Local Drush" task).
 *
 * Thin wrapper: delegate to the canonical, battle-tested lock that the verify
 * hooks use (provision_lock_drush_local_vnd() in provision.inc), after the
 * platform-root auto-fix verify performs before locking. Detection-based
 * idempotency (no marker/pid files): a re-run on an already-locked platform is
 * a clean no-op rather than redundant work.
 *
 * The command bootstraps at DRUSH_BOOTSTRAP_DRUSH (see provision.drush.inc), so
 * Drush 8's DrushInputAdapter resolves against its own bundled symfony/console
 * at dispatch, before the platform's modern (typed) symfony/console is loaded.
 * We then bootstrap DRUPAL_ROOT here, inside the callback, exactly as
 * provision-verify does in its pre-hook. Bootstrapping at command-dispatch
 * instead would load the platform's typed InputInterface first and fatal on an
 * unlocked platform (DrushInputAdapter::getFirstArgument() signature mismatch).
 */

function drush_provision_dlock() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  // Auto-fix the platform root BEFORE setting the selected-root context and
  // bootstrapping, so the corrected (D8+ web/) docroot is the one bootstrapped.
  d()->root = provision_auto_fix_platform_root(d()->root);

  // Mirror provision-verify's in-hook bootstrap dance (verify.provision.inc).
  drush_set_context('DRUSH_SELECTED_DRUPAL_ROOT', d()->root);
  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_ROOT);
  if (drush_get_error()) {
    $errors = drush_get_error_log();
    foreach ($errors as $type => &$error) {
      $error = "$type: {$error[0]}";
    }
    return drush_set_error("DRUSH_INVALID_DRUPAL_ROOT",
      dt("A Drupal codebase was not found in @path. The errors were: @errors", array(
        '@path' => d()->root,
        '@errors' => implode('|', $errors),
      )));
  }

  // Fail closed if the Drupal major version did not resolve: the D10/D11 patch
  // logic in provision_lock_drush_local_vnd() and provision_check_codebase_status()
  // is version-gated, while the chmod-to-0400 lock runs unconditionally. A bogus
  // version 0 would chmod the vendor dirs to locked perms WITHOUT applying the
  // console patch -> a silent half-lock that fatals every subsequent task.
  if (drush_drupal_major_version() < 1) {
    return drush_set_error("PROVISION_DRUPAL_VERSION_UNRESOLVED",
      dt("Drupal major version did not resolve in @path; refusing to lock to avoid a partial patch state.", array(
        '@path' => d()->root,
      )));
  }

  provision_check_codebase_status();
  if (provision_drush_local_lock_state() == 'locked') {
    drush_log(dt('Local Drush already locked in @root; nothing to do.', array('@root' => d()->root)), 'success');
    return;
  }
  provision_lock_drush_local_vnd();
}
