<?php

/**
 * @file Provision named context site class.
 */

class Provision_Context_site extends Provision_Context {
  public $parent_key = 'platform';

  static function option_documentation() {
    return array(
      'platform' => 'site: the platform the site is run on',
      'db_server' => 'site: the db server the site is run on',
      'uri' => 'site: example.com URI, no http:// or trailing /',
      'language' => 'site: site language; default en',
      'aliases' => 'site: comma-separated URIs',
      'redirection' => 'site: boolean for whether --aliases should redirect; default false',
      'client_name' => 'site: machine name of the client that owns this site',
      'install_method' => 'site: How to install the site; default profile. When set to "profile" the install profile will be run automatically. Otherwise, an empty database will be created. Additional modules may provide additional install_methods.',
      'profile' => 'site: Drupal profile to use; default standard',
      'drush_aliases' => 'site: Comma-separated list of additional Drush aliases through which this site can be accessed.',
    );
  }

  function init_site() {
    $this->setProperty('uri');

    // Add platform root auto-discovery to avoid confusing
    // Composer based D8 codebase root with Drupal real root.
    $this->platform->root = provision_auto_fix_platform_root($this->platform->root);

     // we need to set the alias root to the platform root, otherwise drush will cause problems.
    $this->root = $this->platform->root;

    // set this because this path is accessed a lot in the code, especially in config files.
    $this->site_path = $this->root . '/sites/' . $this->uri;

    $this->setProperty('site_enabled', true);
    $this->setProperty('language', 'en');
    $this->setProperty('client_name');
    $this->setProperty('aliases', [], TRUE);
    $this->setProperty('redirection', FALSE);
    $this->setProperty('cron_key', '');
    $this->setProperty('drush_aliases', [], TRUE);

    // this can potentially be handled by a Drupal sub class
    $this->setProperty('profile', 'standard');
    $this->setProperty('install_method', 'profile');
  }

  /**
   * Write out this named context to an alias file.
   */
  function write_alias() {
    $config = new Provision_Config_Drushrc_Alias($this->name, $this->properties);
    $config->write();
    foreach ($this->drush_aliases as $drush_alias) {
      $config = new Provision_Config_Drushrc_Alias($drush_alias, $this->properties);
      $config->write();
    }
  }
}
