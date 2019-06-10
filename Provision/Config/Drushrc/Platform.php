<?php
/**
 * @file
 * Provides the Provision_Config_Drushrc_Platform class.
 */

/**
 * Class for writing $platform/drushrc.php files.
 */
class Provision_Config_Drushrc_Platform extends Provision_Config_Drushrc {
  protected $context_name = 'drupal';
  public $description = 'Platform Drush configuration file';
  // platforms contain no confidential information
  protected $mode = 0444;

  // Add platform root auto-discovery to avoid confusing
  // Composer based D8 codebase root with Drupal real root.
  $this->root = provision_auto_fix_platform_root($this->root);

  function filename() {
    return $this->root . '/sites/all/drush/drushrc.php';
  }
}
