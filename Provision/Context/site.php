<?php

use Eloquent\Composer\Configuration\ConfigurationReader;

/**
 * @file Provision named context site class.
 */

class Provision_Context_site extends Provision_Context {

  use \DevShop\Component\Common\ComposerRepositoryAwareTrait;

  public $type = 'site';
  public $parent_key = 'platform';

  public function __construct($name, $node = null) {
    parent::__construct($name);
    if ($node && $node->type != $this->type) {
      throw new \Exception('Node passed to __construct() is not a site.');
    }
    elseif ($node) {

      //
      $this->setProperty('git_root', $node->git_root);
    }
  }

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
  public function getDeploySteps() {
    $steps = self::defaultDeploySteps();
    $composer_path = $this->git_root . '/composer.json';

    // Don't try to load if there's no file.
    if (empty($this->git_root) || !file_exists($composer_path)) {
      return $steps;
    }

    $reader = new ConfigurationReader;
    $this->composerConfig =  $reader->read($composer_path);
    $scripts = (array) $this->composerConfig->scripts()->rawData();

    foreach ($steps as $step => $info) {
      $command = "deploy:$step";
      if (!empty($scripts[$command])) {
        $steps[$step]['command'] = $scripts[$command];
        $steps[$step]['source'] = 'composer';
        $steps[$step]['note'] = t('Defined by <code>composer.json:deploy:build</code> script.', [
          '%override' => 'composer.json',
          '@step' => $step,
        ]);
      }
    }

    // Allow modules to alter the steps.
    if (function_exists('drupal_alter')) {
      drupal_alter('hosting_site_deploy_steps', $steps, $this);
    }

    return $steps;
  }

  /**
   * Default deploy steps for a site.
   *
   * getDeploySteps() will load these or overrides.
   * @return array[]
   */
  protected static function defaultDeploySteps() {
    return [
      // @TODO: Add as hosting task option. Not a real deploy step.
      //      'reset' => [
      //        'title' => t('Reset'),
      //        'description' => t('Discard uncommitted code changes.'),
      //        'command' => 'git reset --hard',
      //      ],
      'install' => [
        'title' => dt('Re-install'),
        'description' => dt('Destroy & reinstall the site.'),
        'note' => dt('WARNING: If checked, this site will be destroyed and recreated on every deployment.'),
        'command' => 'drush @hostmaster hosting-task --force @alias install force-reinstall=1 parent_task=@task_nid'
      ],
      'build' => [
        'title' => dt('Build'),
        'description' => dt('Prepare source code.'),
        // Preferred default production composer install command.
        'command' => 'composer --no-interaction install --no-progress --prefer-dist --optimize-autoloader',
      ],
      'update' => [
        'title' => dt('Update'),
        'description' => dt('Apply pending updates to the site.'),
        'command' => [
          "drush updatedb --yes --no-cache-clear",
          'drush cache:rebuild',
        ],
      ],
      'test' => [
        'title' => dt('Test'),
        'description' => dt('Run tests against the site.'),
        'command' => 'drush status',
      ],
    ];
  }

  /**
   * Default deploy steps for a site.
   *
   * getDeploySteps() will load these or overrides.
   * @return array[]
   */
  public static function generateComposerJsonDeployScripts($steps = null) {
    if (empty($steps)) {
      $steps = self::defaultDeploySteps();
    }
    $composer = [];
    $composer['scripts'] = [];

    foreach ($steps as $name => $info) {
      $composer['scripts']["deploy:$name"] = is_array($info['command'])?
       $info['command']:
       [$info['command']];
    }
    return $composer;
  }

  /**
   * @param $step
   *
   * @return bool
   */
  public function runDeployStep($step) {

    $task_nid = drush_get_option('task_nid');
    $log_output = drush_get_option('runner') == 'hosting_task';
    $provision_log_type = drush_get_option('runner') == 'hosting_task'? 'p_info': 'ok';

    $steps = $this->getDeploySteps();
    if (empty($steps[$step]['command'])) {
      return TRUE;
    }
    $commands = is_array($steps[$step]['command'])?
      $steps[$step]['command']:
      [$steps[$step]['command']];

    $cwd = $this->platform->git_root;
    $env = [
      'DRUSH_OPTIONS_URI' => $this->uri,
      'XTERM' => 'TERM',
    ];

    $t = [
      '@step' => $step,
      '@root' => $cwd,
      '@alias' => d()->name,
      '@task_nid' => $task_nid,
    ];
    foreach ($commands as $command) {
      provision_process(strtr($command, $t), $cwd, dt('Deploy Step: @step in @root', $t), $env, TRUE, null, TRUE, $provision_log_type);
      $process = drush_get_context('provision_process_result');
      if (!$process->isSuccessful()) {
        return drush_set_error(DRUSH_APPLICATION_ERROR, dt('Deploy Step failed: @step', $t));
      }
    }
    return TRUE;
  }
}
