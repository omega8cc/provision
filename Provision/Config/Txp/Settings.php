<?php
/**
 * @file
 * Provides the Provision_Config_Txp_Settings class.
 */

class Provision_Config_Txp_Settings extends Provision_Config {
  public $template = 'provision_txp_settings.tpl.php';
  public $description = 'Textpattern private/config.php file';
  public $creds = array();
  protected $mode = 0440;

  function filename() {
    return $this->site_path . '/private/config.php';
  }

  function process() {
    // TXP is mysqli-only; the shared db normalisation never touches a TXP
    // context, and TXP wants the raw host anyway (it splits host:port itself).
    foreach (array('db_user', 'db_passwd', 'db_host', 'db_name', 'db_port') as $key) {
      $this->creds[$key] = isset($this->data[$key]) ? urldecode($this->data[$key]) : '';
    }

    $this->version = provision_version();
    $this->api_version = provision_api_version();

    // The multisite trio. admin_url is path-mapped on the site vhost (single
    // segment, on-disk dir stays 'admin'); cookie_domain is the EXPLICIT EMPTY
    // STRING: isset() skips TXP's broken derivation AND yields a host-only
    // login cookie (any Domain= attribute would widen it to sibling
    // subdomains).
    $this->data['txp_admin_url'] = $this->uri . '/' . PROVISION_TXP_ADMIN_PATH;
    $this->data['txp_multisite_root'] = $this->site_path;
    $this->data['txp_txpath'] = $this->root . '/textpattern';

    $this->group = $this->platform->server->web_group;
  }
}
