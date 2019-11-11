<?php

/**
 * Base class for subdir support.
 *
 * This class will publish the config files to remote
 * servers automatically.
 */
class Provision_Config_SubdirVhost extends Provision_Config_Http {
  public $template = 'subdir_vhost.tpl.php';
  public $description = 'subdirectory vhost support';

  // hack: because the parent class doesn't support multiple config
  // files, we need to keep track of the alias we're working on.
  protected $current_alias;

  /**
   * Guess the URI this subdir alias is related too.
   */
  function uri() {
    $e = explode('/', $this->current_alias, 2);
    return $e[0];
  }

  /**
   * Guess the subdir part of the subdir alias.
   */
  function subdir() {
    $e = explode('/', $this->current_alias, 2);
    return $e[1];
  }

  /**
   * Check if the (real) parent site (drushrc) exists.
   */
  function parent_site() {
    $drush_home_dir = drush_server_home() . '/.drush/';
    $parent_site_drushrc = $drush_home_dir . $this->uri() . '.alias.drushrc.php';
    drush_log(dt('Checking for parent site %vhost', ['%vhost' => $this->uri()]), 'notice');
    if (provision_file()->exists($parent_site_drushrc)->status()
      || $this->uri() == d('@hostmaster')->uri) { // The Hostmaster site's alias is always `@hostmaster`
      drush_log(dt('Parent site (%vhost) was found.', ['%vhost' => $this->uri()]), 'notice');
      return TRUE;
    }
    drush_log(dt('Parent site (%vhost) was not found.', ['%vhost' => $this->uri()]), 'notice');
    return FALSE;
  }

  function write() {
    foreach (d()->aliases as $alias) {
      // We only care about subdir URLs.
      if (strpos($alias, '/') === FALSE) continue;

      $this->current_alias = $alias;
      $log_vars = [
        '%vhost' => $this->uri(),
        '%alias' => $alias,
      ];
      if ($this->parent_site()) {
        drush_log(dt('Parent site (%vhost) exists for alias %alias, skipping generation of default parent vhost.', $log_vars), 'notice');
        if (drush_parse_command()['command'] == 'provision-install') {
          drush_log(dt('Parent site (%vhost) re-verify required to include subdir config for %alias', $log_vars), 'warning');
        }
      }
      else {
        drush_log(dt('Subdirectory alias %alias found. Generating default parent (%vhost) vhost configuration file.', $log_vars), 'notice');
        parent::write();
      }
    }
  }

  function process() {
    parent::process();
    $this->data['uri'] = $this->uri();
    $this->data['subdir'] = $this->subdir();
    $this->data['subdirs_path'] = $this->data['http_subdird_path'];
  }

  function filename() {
    return $this->data['http_vhostd_path'] . '/' . $this->uri();
  }
}
