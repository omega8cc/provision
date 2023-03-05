<?php

/**
 * @file Provision named context site class.
 */

class Provision_Context_site extends Provision_Context {
  public $parent_key = 'platform';

  static function option_documentation() {
    return array(
      'environment' => 'site: the environment name for this site. For example, dev, test, live.',
      'project' => 'site: a codename for this site. If left empty, site URI will be used.',
      'platform' => 'site: the platform the site is run on',
      'db_server' => 'site: the db server the site is run on',
      'uri' => 'site: example.com URI, no http:// or trailing /',
      'file_public_path' => 'site: path to public files folder. Defaults to sites/example.com/files',
      'file_private_path' => 'site: path to private files folder. Defaults to sites/example.com/private/files',
      'file_temporary_path' => 'site: path to temporary files folder. Defaults to sites/example.com/private/temp',
      'language' => 'site: site language; default en',
      'aliases' => 'site: comma-separated URIs',
      'redirection' => 'site: boolean for whether --aliases should redirect; default false',
      'client_name' => 'site: machine name of the client that owns this site',
      'install_method' => 'site: How to install the site; default profile. When set to "profile" the install profile will be run automatically. Otherwise, an empty database will be created. Additional modules may provide additional install_methods.',
      'profile' => 'site: Drupal profile to use; default standard',
      'drush_aliases' => 'site: Comma-separated list of additional Drush aliases through which this site can be accessed.',
      'site_install_command' => 'site: The drush command to run when installing the site.',
    );
  }

  function init_site() {
    $this->setProperty('uri');
    $this->setProperty('project');
    $this->setProperty('environment');

    // set this because this path is accessed a lot in the code, especially in config files.
    $this->site_path = $this->root . '/sites/' . $this->uri;

    $this->setProperty('site_enabled', true);
    $this->setProperty('language', 'en');
    $this->setProperty('client_name');
    $this->setProperty('aliases', array(), TRUE);
    $this->setProperty('redirection', FALSE);
    $this->setProperty('cron_key', '');
    $this->setProperty('drush_aliases', array(), TRUE);
    $this->setProperty('site_install_command', 'si');

    // this can potentially be handled by a Drupal sub class
    $this->setProperty('profile', 'standard');
    $this->setProperty('install_method', 'profile');
    $this->setProperty('file_public_path', 'sites/' . $this->uri . '/files');
    $this->setProperty('file_private_path', 'sites/' . $this->uri . '/private/files');
    $this->setProperty('file_temporary_path', 'sites/' . $this->uri . '/private/temp');

    // Load commands from platform, but allow site to retain it's own.
    if (!empty($this->platform)) {
      // we need to set the alias root to the platform root, otherwise drush will cause problems.
      $this->root = $this->platform->root;

      $this->setProperty('commands', $this->platform->findCommands());

      // Load git properties from platform.
      $this->setProperty('git_root', $this->platform->git_root);
      $this->setProperty('git_remote', $this->platform->git_remote);
      $this->setProperty('git_reference', $this->platform->git_reference);
      $this->setProperty('git_docroot', $this->platform->git_docroot);
    }
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
