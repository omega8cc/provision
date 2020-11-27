<?php

/**
 * @file Provision named context platform class.
 */


/**
 * Class for the platform context.
 */
class Provision_Context_platform extends Provision_Context {
  public $parent_key = 'server';

  static function option_documentation() {
    return array(
      'root' => 'platform: path to a Drupal installation',
      'server' => 'platform: drush backend server; default @server_master',
      'web_server' => 'platform: web server hosting the platform; default @server_master',
      'makefile' => 'platform: drush makefile to use for building the platform if it doesn\'t already exist',
      'make_working_copy' => 'platform: Specifiy TRUE to build the platform with the Drush make --working-copy option.',
        'git_root' => 'platform: The absolute path to clone the git repository to. May be left empty if same as document root. If the document root is inside a subfolder of the git repository, be sure that the "root" property points to the web document root.',
        'git_remote' => 'platform: The URL of the git repository to use when creating this platform.',
        'git_reference' => 'platform: The git reference to check out. Can be a branch, tag or SHA. Defaults to the git repository default branch.',
    );
  }

  function init_platform() {
    $this->setProperty('root');
    $this->setProperty('makefile', '');
    $this->setProperty('make_working_copy', FALSE);
    $this->setProperty('git_root');
    $this->setProperty('git_remote');
    $this->setProperty('git_reference');
  }
}
