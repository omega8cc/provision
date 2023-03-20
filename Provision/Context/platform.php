<?php

use Eloquent\Composer\Configuration\ConfigurationReader;
use DevShop\Component\Common\ComposerRepositoryAwareTrait;

/**
 * @file Provision named context platform class.
 */

/**
 * Class for the platform context.
 */
class Provision_Context_platform extends Provision_Context {
  use ComposerRepositoryAwareTrait;
  
  public $type = 'platform';
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
      'git_docroot' => 'platform: The relative path within the git repository to expose to the web server.',
      'git_reset' => 'platform: If true, reset any changes to this platform when verifying.',
    );
  }

  function init_platform() {
    $this->setProperty('root');
    $this->setProperty('makefile', '');
    $this->setProperty('make_working_copy', FALSE);
    $this->setProperty('git_root');
    $this->setProperty('git_remote');
    $this->setProperty('git_reference');
    $this->setProperty('git_docroot');
    $this->setProperty('git_reset', TRUE);
  }

  /**
   * @TODO: Implement GitRepoAwareTrait.
   * Run a command and return true or false if it worked.
   */
  public function execSuccess($command, $pwd = NULL) {
    try {
      if (!$pwd) {
        $pwd = $this->git_root;
      }
      $process = new \Symfony\Component\Process\Process($command, $pwd);
      $process->mustRun();
      return FALSE;
    } catch (\Exception $e) {
      return TRUE;
    }
  }

  /**
   * Run a command and return output.
   */
  public function execOutput($command, $pwd = NULL) {
    if (!$pwd) {
      $pwd = $this->git_root;
    }
    $command = "cd $pwd && $command 2> /dev/null";
    return trim(shell_exec($command));
  }

  /**
   * Return TRUE if git_root is in DETACHED HEAD state.
   * https://stackoverflow.com/questions/52221558/programmatically-check-if-head-is-detached
   */
  public function isDetached() {
    $this->execSuccess('git symbolic-ref -q HEAD', $this->git_root);
  }
  /**
   * Get the current git SHA. 
   */
  public function getSha() {
    return trim(shell_exec("cd $this->git_root && git rev-parse @ 2> /dev/null"));
  }

  /**
   * Get the current git SHA. 
   */
  public function getLast() {
    return trim(shell_exec("cd {$this->git_root}; git log --pretty=format:'%ct' --max-count=1 2> /dev/null"));
  }

  /**
   * Get the current git SHA. 
   */
  public function getStatus() {
    chdir($this->git_root);
    return implode(PHP_EOL . PHP_EOL, [
      trim(shell_exec("git -c color.ui=always status 2> /dev/null")),
      trim(shell_exec("git -c color.ui=always show 2> /dev/null")),
      trim(shell_exec("git -c color.ui=always diff --minimal 2> /dev/null")),
    ]);
  }

  /**
   * Return the branch name.
   */
  public function getBranch()
  {
    if (!empty($this->git_root)) {
      return trim(shell_exec("cd $this->git_root && git symbolic-ref --quiet --short HEAD 2> /dev/null"));
    }
  }

  /**
   * Return the branch name.
   * @TODO: Implement branch-or-tag.
   */
  public function getTag()
  {
    if (!empty($this->git_root)) {
      return trim(shell_exec("cd $this->git_root && git describe --tags --exact-match 2> /dev/null"));
    }
  }

  /**
   * Return the currently checked out branch or tag.
   */
  public function getBranchOrTag()
  {
    $branch = $this->getBranch();
    $tag = $this->getTag();

    if ($branch) {
      return $branch;
    }
    elseif ($tag) {
      return $tag;
    }
    else {
      return FALSE;
    }
  }

  /**
   * Returns the remote name, usually "origin".
   * @return  array
   */
  public function getCurrentRemoteUrl()
  {
    // Only branch checkouts can have a remote.
    $remote = $this->getCurrentRemoteName();
    if (empty($remote)) {
      return;
    }
    else {
      $command = "git config remote.{$remote}.url";
      return $this->execOutput($command);
    }
  }

  /**
   * Returns the remote name, usually "origin".
   * @return  array
   */
  public function getCurrentRemoteName()
  {
    // Only branch checkouts can have a remote.
    $branch = $this->getBranch();
    if (empty($branch)) {
      return;
    }
    else {
      $command = "git config branch.{$branch}.remote";
      return $this->execOutput($command);
    }
  }

  /**
   * Print out current git status, git remotes, and logs.
   */
  public function displayGitStatus() {
    $provision_log_type = drush_get_option('runner') == 'hosting_task'? 'p_info': 'ok';
    provision_process("git -c color.ui=always log --oneline --max-count=1 && git -c color.ui=always status && git -c color.ui=always diff --minimal", $this->git_root, dt('Git Status'), [], TRUE, dt('Unable to run git status.'), FALSE, $provision_log_type);
  }

  /**
   * Use git-ls remote to read all available git references.
   */
  public function getRemoteReferences()
  {
    return $this->execOutput("git ls-remote $this->git_remote");
  }

  /**
   * Return true if there is a .gitmodules folder in the root.
   * @return bool
   */
  public function hasGitSubmodules() {
    return file_exists(d()->git_root . DIRECTORY_SEPARATOR . '.gitmodules');
  }

  /**
   * Returns TRUE if the working directory has commits that have not been pushed to the remote.
   *
   * @return  boolean
   */
  public function isAhead()
  {
    $output = $this->execOutput("git status -sb");
    if (strpos($output, 'ahead') !== FALSE) {
      return TRUE;
    }
  }

  /**
   * Helper for verifying the chosen path is a git repo.
   *
   * @param bool $set_error
   *   Use drush_set_error when it's not a repo, default to TRUE.
   *   Set to FALSE to silently check if a directory is a Git repo.
   *
   * @return bool
   *   TRUE if it's a functional Git repository, FALSE otherwise.
   *
   */
  public function isRepo($repo_path = null, $set_error = TRUE) {
    // If no path specified, use the site or platform path of the alias.
    if (!$repo_path) {
      $repo_path = d()->git_root;
    }
    if (!$repo_path) {
      if (d()->type === 'site') {
        $repo_path = d()->site_path ? d()->site_path : d()->root;
      } elseif (d()->type == 'platform') {
        $repo_path = d()->platform->root ? d()->platform->root : d()->root;
      }
    }

    // Do not allow /var/aegir to be repo_path.
    // See https://www.drupal.org/node/2893588
    if ($repo_path == '/var/aegir') {
      $repo_path = NULL;
      drush_set_option('repo_path', $repo_path);
      return drush_set_error('PROVISION_GIT_ERROR', dt('Warning: The "repo_path" for this !type is set to "/var/aegir". This is not allowed. Please manually edit the drush alias file in /var/aegir/.drush/!file.alias.drushrc.php.  Option repo_path has been unset.', array(
        '!file' => ltrim(d()->name, '@'),
        '!type' => d()->type,
      )));
    }

    // If repo_path does not exist, or git status returns False, this is not a git repo
    if (!file_exists($repo_path . '/.git/config') || !drush_shell_cd_and_exec($repo_path, 'git status')) {
      if ($set_error) {
        drush_set_error('DRUSH_PROVISION_GIT_PULL_FAILED', dt("Git repository not found in: !path", array('!path' => $repo_path)));
      }
      return FALSE;
    } else {
      // Find the toplevel directory for this git repository
      if (drush_shell_cd_and_exec($repo_path, 'git rev-parse --show-toplevel')) {
        $top_repo_path = implode("\n", drush_shell_exec_output());
      }
      if (drush_get_option('force_repo_path_as_toplevel', FALSE) && realpath($repo_path) != $top_repo_path) {
        if ($set_error) {
          drush_set_error('DRUSH_PROVISION_GIT_PULL_FAILED', dt("Path !path is not the toplevel dir of this Git repository.", array('!path' => $repo_path)));
        }
        return FALSE;
      }
      drush_set_option('repo_path', $top_repo_path);
      return TRUE;
    }
  }

  /**
   * Loads data from a platform's composer.json.
   * @return array
   */
  public function getComposer() {
    $composer_json_path = $this->git_root . DIRECTORY_SEPARATOR . 'composer.json';
    if (file_exists($composer_json_path)) {
      return json_decode(file_get_contents($composer_json_path), TRUE);
    }
    else {
      return [];
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
        $steps[$step]['overridden_by'] = t('Defined in %override: <code>deploy:@step</code>.', [
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
      'build' => [
        'title' => dt('Build'),
        'description' => dt('Prepare source code.'),
        'command' => 'composer install --no-dev --ansi',
      ],
      'update' => [
        'title' => dt('Update'),
        'description' => dt('Apply pending updates to the site.'),
        'command' => [
          "drush updatedb  --no-cache-clear",
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
   * @param $step
   *
   * @return bool
   */
  public function runDeployStep($step) {

    $log_output = drush_get_option('runner') == 'hosting_task';
    $provision_log_type = drush_get_option('runner') == 'hosting_task'? 'p_info': 'ok';

    $steps = $this->getDeploySteps();
    $commands = is_array($steps[$step]['command'])?
      $steps[$step]['command']:
      [$steps[$step]['command']];

    $cwd = $this->git_root;
    $env = [
      'DRUSH_OPTIONS_URI' => $this->uri,
      'XTERM' => 'TERM',
    ];

    $t = [
      '@step' => $step,
      '@root' => $cwd,
    ];
    foreach ($commands as $command) {
      provision_process($command, $cwd, dt('Deploy Step: @step in @root', $t), $env, TRUE, null, TRUE, $provision_log_type);
      $process = drush_get_context('provision_process_result');
      if (!$process->isSuccessful()) {
        return drush_set_error(DRUSH_APPLICATION_ERROR, dt('Deploy Step failed: @step', $t));
      }
    }
    return TRUE;
  }
}
