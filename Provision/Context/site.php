<?php

/**
 * @file Provision named context site class.
 */

class Provision_Context_site extends Provision_Context {
  public $type = 'site';
  public $parent_key = 'platform';

  static function option_documentation() {
    return array(
      'group' => 'site: the drush alias group to put this site into. If left blank, a sanitized URI will be used.',
      'environment' => 'site: the environment name for this site. For example, dev, test, live.',
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
    $this->setProperty('group');
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
    
    $this->setProperty('root', $this->platform->root);
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

  /**
   * Load the deploy steps for this site. 
   * @return array[]
   */
  public static function getDeploySteps() {
    $steps = self::defaultDeploySteps();
    return $steps;
  }

  /**
   * Default deploy steps for a site.
   * 
   * getDeploySteps() will load these or overrides.
   * @return array[]
   */
  private static function defaultDeploySteps() {
    return [
      'reset' => [
        'title' => t('Reset'),
        'description' => t('Discard uncommitted code changes.'),
        'command' => 'git reset --hard',
      ],
      'build' => [
        'title' => t('Build'),
        'description' => t('Prepare source code.'),
        'command' => 'composer install --no-dev --profile',
      ],
      'update' => [
        'title' => t('Update'),
        'description' => t('Apply changes to the site.'),
        'command' => [
          'drush pm:update --no-cache-clear',
          'drush cache:rebuild',
        ],
      ],
      'test' => [
        'title' => t('Test'),
        'description' => t('Run tests against the site.'),
        'command' => 'drush status',
      ],
    ];
  }
}
