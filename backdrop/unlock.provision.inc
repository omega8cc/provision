<?php

/**
 * Dummy drush hooks for the provision-unlock command (Backdrop layer).
 *
 * Fork of platform/unlock.provision.inc: nothing happens in the backend for
 * provision-unlock; the guard keeps parity with the mutually-exclusive layers.
 */

function drush_provision_backdrop_provision_unlock() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  // Nothing needs to happen in the backend for provision-unlock.
}
