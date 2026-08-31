<?php

/**
 * Dummy drush hooks for the provision-unlock command
 */

function drush_provision_drupal_provision_unlock() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  // Nothing needs to happen in the backend for provision-unlock
}
