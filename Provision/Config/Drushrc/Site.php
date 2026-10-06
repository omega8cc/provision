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
    $this->fill_db_port();
    return parent::process();
  }

  /**
   * An imported site takes its credentials from its own settings, which name
   * no port, so its drushrc.php had none and the settings file's command-line
   * read of the credentials left the port unset: every task on the site ended
   * with a warning. Take the port of the site's database server, as Install
   * does when it makes the credentials; a site written without one gets it on
   * its next write.
   */
  function fill_db_port() {
    if (empty($this->data['db_name'])) {
      return;
    }
    if (isset($this->data['db_port']) && $this->data['db_port'] !== '') {
      return;
    }
    $server = isset($this->context->db_server) ? $this->context->db_server : NULL;
    if (!is_object($server) || empty($server->db_port)) {
      return;
    }
    $this->data['db_port'] = (string) $server->db_port;
    drush_set_option('db_port', $this->data['db_port'], 'site');
  }
}
