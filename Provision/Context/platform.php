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

    // Load properties from composer
    $this->setProperty('commands', $this->findCommands());
  }

  /**
   * @see hosting_find_deploy_commands()
   */
  public function findCommands() {
    $default_commands = $this->defaultCommands();

    $composer_json_path = $this->git_root . DIRECTORY_SEPARATOR . 'composer.json';
    if (file_exists($composer_json_path)) {
      $composer_data = json_decode(file_get_contents($composer_json_path), TRUE);
      $commands = isset($composer_data['extra']['devshop']['commands'])
        ? $composer_data['extra']['devshop']['commands']
        : array();
    }
    else {
      $commands = array();
    }
    return array_merge($default_commands, $commands);
  }

  /**
   * Define default commands for this platform type.
   *
   * @return array
   */
  public function defaultCommands() {
    return array(
      'git' => $this->isDetached()
        ? 'echo "HEAD is Detached:" && git status'
        : 'git fetch --all && git checkout $GIT_REFERENCE && git reset FETCH_HEAD',
      'build' => 'composer install --no-dev --no-progress --no-suggest --ansi',
      'install' => 'bin/drush site-install -y',
      'deploy' => 'bin/drush updb -y',
      'test' => 'bin/phpunit --help',
    );
  }

  /**
   * Return TRUE if git_root is in DETACHED HEAD state.
   * https://stackoverflow.com/questions/52221558/programmatically-check-if-head-is-detached
   */
  public function isDetached() {
    try {
      $process = new \Symfony\Component\Process\Process('git symbolic-ref -q HEAD', $this->git_root);
      $process->mustRun();
      return FALSE;
    } catch (\Exception $e) {
      return TRUE;
    }
  }
}
