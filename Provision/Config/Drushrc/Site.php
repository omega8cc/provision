<?php
/**
 * @file
 * Provides the Provision_Config_Drushrc_Site class.
 */

/**
 * Class for writing $platform/sites/$url/drushrc.php files.
 */
class Provision_Config_Drushrc_Site extends Provision_Config_Drushrc {
  protected $context_name = 'site';
  public $template = 'provision_drushrc_site.tpl.php';
  public $description = 'Site Drush configuration file';

  function filename() {
    return $this->site_path . '/drushrc.php';
  }

  /**
   * The hostmaster site's drushrc.php carries the instance DB user, which
   * holds ALL PRIVILEGES; only the backend user (its owner) ever reads it,
   * and group users is box-wide, so it takes no group read at all.
   */
  function process() {
    if (provision_is_hostmaster_site()) {
      $this->mode = 0400;
    }
    return parent::process();
  }
}
