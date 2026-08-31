<?php

/**
 * Dummy drush hooks for the provision-unlock command (Grav layer).
 *
 * Parity twin of grav/lock.provision.inc.
 */

function drush_provision_grav_provision_unlock() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  // Nothing needs to happen in the backend for provision-unlock.
}
