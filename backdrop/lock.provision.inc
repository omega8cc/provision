<?php

/**
 * Dummy drush hooks for the provision-lock command (Backdrop layer).
 *
 * Fork of platform/lock.provision.inc: nothing happens in the backend for
 * provision-lock; the guard keeps parity with the mutually-exclusive layers.
 */

function drush_provision_backdrop_provision_lock() {
  if (!provision_platform_is_backdrop(d())) {
    return;
  }
  // Nothing needs to happen in the backend for provision-lock.
}
