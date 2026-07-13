<?php

/**
 * Dummy drush hooks for the provision-unlock command
 */

function drush_provision_drupal_provision_unlock() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  // Nothing needs to happen in the backend for provision-unlock
}
