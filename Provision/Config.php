<?php
/**
 * @file
 * Provision configuration generation classes.
 */

class Provision_Config {
  /**
   * Template file, a PHP file which will have access to $this and variables
   * as defined in $data.
   */
  public $template = NULL;

  /**
   * Associate array of variables to make available to the template.
   */
  public $data = array();

  /**
   * A Provision_Context object thie configuration relates to.
   *
   * @var Provision_Context
   */
  public $context = NULL;

  /**
   * If set, replaces file name in log messages.
   */
  public $description = NULL;

  /**
   * Octal Unix mode for permissons of the created file.
   */
  protected $mode = NULL;

  /**
   * Unix group name for the created file.
   */
  protected $group = NULL;

  /**
   * An optional data store class to instantiate for this config.
   */
  protected $data_store_class = NULL;

  /**
   * The data store.
   */
  public $store = NULL;

  /**
   * Forward $this->... to $this->context->...
   * object.
   */
  function __get($name) {
    if (isset($this->context)) {
      return $this->context->$name;
    }
  }

  /**
   * Constructor, overriding not recommended.
   *
   * @param $context
   *   An alias name for d(), the Provision_Context that this configuration
   *   is relevant to.
   * @param $data
   *   An associative array to potentially manipulate in process() and make
   *   available as variables to the template.
   */
  function __construct($context, $data = array()) {
    if (is_null($this->template)) {
      throw new Exception(dt("No template specified for: %class", array('%class' => get_class($this))));
    }

    // Accept both a reference and an alias name for the context.
    $this->context = is_object($context) ? $context : d($context);

    if (!empty($data)) {
      $this->data = $data;
    }

    if (!is_null($this->data_store_class) && class_exists($this->data_store_class)) {
      $class = $this->data_store_class;
      $this->store = new $class($context, $data);
    }

  }

  /**
   * Process and add to $data before writing the configuration.
   *
   * This is a stub to be implemented by subclasses.
   */
  function process() {
    if (is_object($this->store)) {
      $this->data['records'] = array_filter(array_merge($this->store->loaded_records, $this->store->records));
    }
    return TRUE;
  }

  /**
   * The filename where the configuration is written.
   *
   * This is a stub to be implemented by subclasses.
   */
  function filename() {
    return FALSE;
  }

  /**
   * Load template from filename().
   *
   * @see hook_provision_config_load_templates()
   * @see hook_provision_config_load_templates_alter()
   */
  private function load_template() {

    // Allow other Drush commands to change the template used first.
    $templates = drush_command_invoke_all('provision_config_load_templates', $this);
    // Ensure that templates is at least an array.
    if (!is_array($templates)) {
      $templates = array();
    }
    // Allow other Drush commands to alter the templates from other commands.
    drush_command_invoke_all_ref('provision_config_load_templates_alter', $templates, $this);
    if (!empty($templates) && is_array($templates)) {
      foreach ($templates as $file) {
        if (is_readable($file)) {
          drush_log(dt('Template loaded from hook(s): :file', array(
            ':file' => $file,
          )));
          return file_get_contents($file);
        }
      }
    }

    // If we've got this far, then try to find a template from this class or
    // one of its parents.
    if (isset($this->template)) {
      $class_name = get_class($this);
      while ($class_name) {
        // Iterate through the config file's parent classes until we
        // find the template file to use.
        $base_dir = provision_class_directory($class_name);
        $file = $base_dir . '/' . $this->template;

        if (is_readable($file)) {
          drush_log(dt('Template loaded from Provision Config class :class_name: :file', array(
            ':class_name' => $class_name,
            ':file' => $file,
          )));
          return file_get_contents($file);
        }

        $class_name = get_parent_class($class_name);
      }
    }

    // We've failed to find a template if we've reached this far.
    drush_log(dt('No template found for Provision Config class: ', array(':class' => get_class($this))), 'warning');
    return FALSE;
  }

  /**
   * Render template, making variables available from $variables associative
   * array.
   */
  private function render_template($template, $variables) {

    // Allow modules to alter the variables before writing to the template.
    // @see hook_provision_config_variables_alter()
    drush_command_invoke_all_ref('provision_config_variables_alter', $variables, $template, $this);
    
    drush_errors_off();
    extract($variables, EXTR_SKIP);  // Extract the variables to a local namespace
    ob_start();                      // Start output buffering
    eval('?>' . $template);                 // Generate content
    $contents = ob_get_contents();   // Get the contents of the buffer
    ob_end_clean();                  // End buffering and discard
    drush_errors_on();
    return $contents;                // Return the contents
  }

  /**
   * Write out this configuration.
   *
   * 1. Make sure parent directory exists and is writable.
   * 2. Load template with load_template().
   * 3. Process $data with process().
   * 4. Make existing file writable if necessary and possible.
   * 5. Render template with $this and $data and write out to filename().
   * 6. If $mode and/or $group are set, apply them for the new file.
   */
  function write() {
    $filename = $this->filename();
    // A file of a site's own directory is written where that directory
    // really is (_write_in_site_dir()).
    if ($filename && ($site_dir = $this->_site_dir_of($filename)) !== NULL) {
      return $this->_write_in_site_dir($filename, $site_dir);
    }
    // Make directory structure if it does not exist.
    if ($filename && !provision_file()->exists(dirname($filename))->status()) {
      provision_file()->mkdir(dirname($filename))
        ->succeed('Created directory @path.')
        ->fail('Could not create directory @path.');
    }

    $status = FALSE;
    if ($filename && is_writeable(dirname($filename))) {
      // manipulate data before passing to template.
      $this->process();

      if ($template = $this->load_template()) {
        // Make sure we can write to the file
        if (!is_null($this->mode) && !($this->mode & 0200) && provision_file()->exists($filename)->status()) {
          provision_file()->chmod($filename, $this->mode | 0200)
            ->succeed('Changed permissions of @path to @perm')
            ->fail('Could not change permissions of @path to @perm');
        }

        $status = provision_file()->file_put_contents($filename, $this->render_template($template, $this->data))
          ->succeed('Generated config in write(): ' . (empty($this->description) ? $filename : $this->description . ' (' . $filename. ')'), 'success')
          ->fail('Could not generate in write(): ' . (empty($this->description) ? $filename : $this->description . ' (' . $filename. ')'))->status();

        // Change the permissions of the file if needed
        if (!is_null($this->mode)) {
          provision_file()->chmod($filename, $this->mode)
            ->succeed('Changed permissions of @path to @perm')
            ->fail('Could not change permissions of @path to @perm');
        }
        // Where the file really lies outside the account's tree, the shared
        // web group rather than the account's own.
        if (!is_null($this->group)) {
          provision_file()->chgrp($filename, provision_path_web_group($filename, $this->group))
            ->succeed('Change group ownership of @path to @gid')
            ->fail('Could not change group ownership of @path to @gid');
        }
      }
    }
    return $status;
  }

  /**
   * The site directory $filename lies in, directly (settings.php,
   * drushrc.php), when this is a site's configuration and the site dir's
   * place rule is loaded; NULL otherwise.
   */
  function _site_dir_of($filename) {
    if (!function_exists('_provision_drupal_store_place') || !is_object($this->context) || $this->context->type !== 'site') {
      return NULL;
    }
    $site_dir = rtrim((string) $this->context->site_path, '/');
    if ($site_dir === '' || rtrim(dirname($filename), '/') !== $site_dir) {
      return NULL;
    }
    return $site_dir;
  }

  /**
   * Write a file of a site's own directory where that directory really is.
   *
   * The account owns the names in the site dir, and the site dir itself can
   * be a link it planted, so the directory is judged once by the place rule
   * (_provision_drupal_store_place(), which logs a STORE/SKIP warning for one
   * that leads out of the account's own places), entered for real, and the
   * contents go into a temporary file made there by
   * Provision_FileSystem::_create_here() with the file's mode, given its
   * group by name and renamed over the name. Neither a link planted at the
   * name (rename() replaces the link itself) nor one at the site dir is
   * followed, and a hard link at the name keeps its other file as it was.
   * With no mode set, an existing file's mode is kept, as writing it in
   * place kept it; where the group cannot be given, an existing file's
   * group is, or the existing file stays. A site dir that cannot be written
   * gets nothing, as write() by path gave it.
   */
  function _write_in_site_dir($filename, $site_dir) {
    $fs = provision_file();
    $name = basename($filename);
    $label = empty($this->description) ? $filename : $this->description . ' (' . $filename . ')';
    $place = _provision_drupal_store_place($site_dir, $site_dir);
    if ($place === FALSE || $place['dir'] === NULL) {
      drush_log(dt('Could not generate in write(): @label: @dir is not the site\'s own', array('@label' => $label, '@dir' => $site_dir)), 'warning');
      return FALSE;
    }
    $dir = $place['dir'];
    if ($place['rest']) {
      if (!$fs->mkdir_in($place['dir'], $place['rest'], $site_dir)->succeed('Created directory @path.')->fail('Could not create directory @path.')->status()) {
        return FALSE;
      }
      $dir .= '/' . implode('/', $place['rest']);
    }
    // As write() by path: nothing is processed for a directory that cannot
    // be written.
    if (!is_writable($dir)) {
      drush_log(dt('Could not generate in write(): @label', array('@label' => $label)), 'warning');
      return FALSE;
    }
    $this->process();
    $template = $this->load_template();
    if (!$template) {
      return FALSE;
    }
    $contents = $this->render_template($template, $this->data);
    $cwd = getcwd();
    $status = FALSE;
    if ($fs->_enter($dir)) {
      clearstatcache();
      if (!is_null($this->mode)) {
        $mode = $this->mode;
      }
      elseif (is_file($name) && !is_link($name)) {
        $mode = fileperms($name) & 07777;
      }
      else {
        $mode = 0666 & ~umask();
      }
      $tmp = '.' . $name . '.provision-' . getmypid() . '-' . mt_rand();
      $made = $fs->_create_here($tmp, $contents, $mode);
      $status = $made;
      if ($status && !is_null($this->group)) {
        // Where the file really lies outside the account's tree, the shared
        // web group rather than the account's own: judged on the new file,
        // where it lies, not through a link at the name.
        $group = provision_path_web_group($tmp, $this->group);
        if ($fs->_lchgrp_here($tmp, $group)) {
          drush_log(dt('Change group ownership of @path to @gid', array('@path' => $filename, '@gid' => $group)), 'success');
        }
        else {
          drush_log(dt('Could not change group ownership of @path to @gid', array('@path' => $filename, '@gid' => $group)), 'warning');
          // An existing file keeps its group, as writing it in place kept
          // it; where even that cannot be given, it stays as it is.
          clearstatcache();
          if (is_file($name) && !is_link($name) && !@lchgrp($tmp, filegroup($name))) {
            drush_log(dt('Left @path as it was: the new file cannot be given its group', array('@path' => $filename)), 'warning');
            $status = FALSE;
          }
        }
      }
      $status = $status && @rename($tmp, $name);
      // Only a file this write made: a name already there was refused.
      if (!$status && $made) {
        @unlink($tmp);
      }
    }
    else {
      drush_log(dt('@path is not where it was checked', array('@path' => $site_dir)), 'warning');
    }
    $fs->_leave($cwd);
    if ($status) {
      drush_log(dt('Generated config in write(): @label', array('@label' => $label)), 'success');
    }
    else {
      drush_log(dt('Could not generate in write(): @label', array('@label' => $label)), 'warning');
    }
    return $status;
  }

  /**
   * The site's local.settings.php, handled where the site dir really is (as
   * _write_in_site_dir() writes): a blank one created when no name is there
   * and the option asks for it, then the group (the shared web group where
   * the file really lies outside the account's tree) and the mode
   * $this->mode | 0440, by name in the directory entered; a link at the name
   * is left alone.
   *
   * @param $description
   *   The file's description, for the log.
   */
  function _local_settings_here($description) {
    $site_dir = rtrim((string) $this->site_path, '/');
    $local = $site_dir . '/local.settings.php';
    $place = _provision_drupal_store_place($site_dir, $site_dir);
    if ($place === FALSE || $place['dir'] === NULL || $place['rest']) {
      return;
    }
    $fs = provision_file();
    $cwd = getcwd();
    if ($fs->_enter($place['dir'])) {
      clearstatcache();
      if (!file_exists('local.settings.php') && !is_link('local.settings.php') && drush_get_option('provision_create_local_settings_file', TRUE)) {
        if ($fs->_create_here('local.settings.php', "<?php # local settings.php \n", 0666 & ~umask())) {
          drush_log(dt('Generated blank @desc', array('@desc' => $description)), 'success');
        }
        else {
          drush_log(dt('Could not generate @desc', array('@desc' => $description)), 'warning');
        }
      }
      clearstatcache();
      if (is_file('local.settings.php') && !is_link('local.settings.php')) {
        $group = provision_path_web_group($local, $this->group);
        if ($fs->_lchgrp_here('local.settings.php', $group)) {
          drush_log(dt('Changed group ownership of <code>@path</code> to @gid', array('@path' => $local, '@gid' => $group)), 'success');
        }
        else {
          drush_log(dt('Could not change group ownership <code>@path</code> to @gid', array('@path' => $local, '@gid' => $group)), 'warning');
        }
        $perm = $this->mode | 0440;
        if ($fs->_chmod_here_nofollow(array('local.settings.php'), $perm)) {
          drush_log(dt('Changed permissions of <code>@path</code> to @perm', array('@path' => $local, '@perm' => sprintf('%o', $perm))), 'success');
        }
      }
    }
    $fs->_leave($cwd);
  }

  // allow overriding w.r.t locking
  function file_put_contents($filename, $text) {
    provision_file()->file_put_contents($filename, $text)
      ->succeed('Generated config in file_put_contents()' . (empty($this->description) ? $filename : $this->description), 'success');
  }

  /**
   * Remove configuration file as specified by filename().
   */
  function unlink() {
    return provision_file()->unlink($this->filename())->status();
  }

}
