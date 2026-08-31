<?php

/**
 * Dummy drush hooks for the provision-lock command
 */

function drush_provision_drupal_provision_lock() {
  if (provision_platform_is_backdrop(d())) {
    return;
  }
  if (provision_platform_is_foreign_cms(d())) {
    return;
  }
  // Nothing needs to happen in the backend for provision-lock
}
