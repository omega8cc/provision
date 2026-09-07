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
   * holds ALL PRIVILEGES; only the backend user (its owner) ever reads it, so
   * it takes no group read at all -- whether the group is still the box-wide
   * 'users' or the account's per-instance group. Tenant sites keep 0440: the
   * shell identity reads their credentials through the group (the CLI
   * pre-block in the settings templates), never through the web group.
   */
  function process() {
    if (provision_is_hostmaster_site()) {
      $this->mode = 0400;
    }
    return parent::process();
  }
}
