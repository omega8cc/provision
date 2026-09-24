<?php

/**
 * Base class for subdir support.
 *
 * This class will publish the config files to remote
 * servers automatically.
 */
class Provision_Config_Nginx_Subdir extends Provision_Config_Http {
  public $template = 'subdir.tpl.php';
  public $disabled_template = 'subdir_disabled.tpl.php';
  public $description = 'subdirectory support';

  // hack: because the parent class doesn't support multiple config
  // files, we need to keep track of the alias we're working on.
  protected $current_alias;

  function write() {
    $written = array();
    foreach (d()->aliases as $alias) {
      if (strpos($alias, '/')) {
        if (!$this->serves_subdirs($alias)) {
          continue;
        }
        $this->current_alias = $alias;
        if ($this->collides()) {
          continue;
        }
        drush_log("Subdirectory alias `$alias` found. Creating configuration files.", 'notice');
        $uri_path = $this->data['http_subdird_path'] . '/' . $this->uri();
        provision_file()->create_dir($uri_path, dt("Webserver subdir configuration for domain"), 0700);
        $this->context->platform->server->sync($uri_path, array(
          'exclude' => $uri_path . '/*',  // Make sure remote directory is created
        ));
        parent::write();
        $written[$this->filename()] = TRUE;
        $this->report_parent();
      }
    }
    $this->current_alias = NULL;
    $this->unlink_stale($written);
  }

  /**
   * Remove the subdirectory configs this site wrote.
   *
   * A conf is removed only when this site rendered it, so a conf another
   * site now writes for the same alias stays. The domain's directory goes
   * with its last conf: the parent vhost includes it by a glob, which a
   * missing directory satisfies, and the parent's next verify drops the
   * include.
   */
  function unlink() {
    foreach (d()->aliases as $alias) {
      if (!strpos($alias, '/')) {
        continue;
      }
      $this->current_alias = $alias;
      $this->unlink_file($this->filename());
    }
    $this->current_alias = NULL;
    return TRUE;
  }

  /**
   * Remove one of this site's confs, and its domain's directory once empty,
   * with the placeholder vhost a domain that is no site got for it.
   */
  function unlink_file($filename) {
    if (is_file($filename) && $this->rendered_here($filename)) {
      provision_file()->unlink($filename)
        ->succeed('Removed subdirectory configuration @path')
        ->fail('Could not remove subdirectory configuration @path');
      $this->data['server']->sync($filename);
    }
    $uri_path = dirname($filename);
    $entries = is_dir($uri_path) ? @scandir($uri_path) : FALSE;
    if (is_array($entries) && count($entries) == 2) {
      provision_file()->rmdir($uri_path)
        ->succeed('Removed @path')
        ->fail('Could not remove @path');
      $this->data['server']->sync($uri_path);
      $this->unlink_placeholder(basename($uri_path));
    }
  }

  /**
   * Remove the placeholder vhost of a domain whose last subdirectory site
   * went. Only that template's shape is touched: a site's own vhost carries
   * fastcgi_param lines and stays.
   */
  function unlink_placeholder($domain) {
    $vhost = $this->data['http_vhostd_path'] . '/' . $domain;
    $body = @file_get_contents($vhost);
    if (is_string($body)
      && strpos($body, 'include  ' . $this->data['http_subdird_path'] . '/' . $domain . '/*.conf;') !== FALSE
      && strpos($body, 'fastcgi_param') === FALSE) {
      provision_file()->unlink($vhost)
        ->succeed('Removed the subdirectory parent vhost @path')
        ->fail('Could not remove the subdirectory parent vhost @path');
      $this->data['server']->sync($vhost);
    }
  }

  /**
   * Remove this site's confs for subdirectories it no longer has.
   *
   * A renamed site or a removed alias leaves the old conf behind, still
   * serving the old path; only a conf carrying this site's marker is touched.
   */
  function unlink_stale($written) {
    $confs = glob($this->data['http_subdird_path'] . '/*/*.conf');
    if (empty($confs)) {
      return;
    }
    foreach ($confs as $filename) {
      if (!isset($written[$filename]) && $this->rendered_here($filename)) {
        $this->unlink_file($filename);
      }
    }
  }

  /**
   * Whether a subdirectory conf at $filename was rendered for this site.
   *
   * Both templates print the same marker line with the site's own name.
   */
  function rendered_here($filename) {
    $body = @file_get_contents($filename);
    if (!is_string($body)) {
      return FALSE;
    }
    return strpos($body, '### Subdirectory site ' . d()->uri . ".\n") !== FALSE;
  }

  /**
   * Grav and Textpattern sites carry their own vhost contract, and this
   * Drupal-shaped location set would serve their trees raw.
   */
  function serves_subdirs($alias) {
    if (provision_platform_is_foreign_cms(d()->root)) {
      drush_log(dt('Subdirectory alias @alias skipped: only Drupal and Backdrop sites can be served from a subdirectory.', array('@alias' => $alias)), 'warning');
      return FALSE;
    }
    return TRUE;
  }

  /**
   * Whether this subdirectory's locations would duplicate a location of the
   * shared vhost include, which a site's vhost carries at the same level.
   *
   * nginx refuses a duplicate location, so one such conf would fail the
   * configtest of the whole box. The rendered include is read, not a list,
   * so a location added there later is covered too.
   */
  function collides() {
    $include = $this->data['server']->include_path . '/nginx_vhost_common.conf';
    $body = @file_get_contents($include);
    if (!is_string($body)) {
      return FALSE;
    }
    $subdir = '/' . $this->subdir();
    $mine = array(
      '= ' . $subdir,
      '^~ ' . $subdir . '/',
      '^~ ' . $subdir . '/sites/default/files',
    );
    preg_match_all('/^location\s+(=|\^~)?\s*(\/[^\s{]*)\s*\{/m', $body, $m, PREG_SET_ORDER);
    foreach ($m as $location) {
      $modifier = ($location[1] === '=') ? '=' : '^~';
      if (in_array($modifier . ' ' . $location[2], $mine)) {
        drush_log(dt('Subdirectory alias @alias skipped: the location @location is taken by the web server configuration every site shares.', array('@alias' => $this->current_alias, '@location' => $location[2])), 'warning');
        return TRUE;
      }
    }
    return FALSE;
  }

  /**
   * Tell the front end when the parent site's vhost does not include this
   * domain's subdirectory configs yet, which only its own verify can change.
   */
  function report_parent() {
    $parent = $this->uri();
    $vhost = $this->data['http_vhostd_path'] . '/' . $parent;
    $body = @file_get_contents($vhost);
    if (!is_string($body) || strpos($body, 'fastcgi_param') === FALSE) {
      // No vhost, or the placeholder a domain that is not a site gets.
      return;
    }
    if (strpos($body, 'include  ' . $this->data['http_subdird_path'] . '/' . $parent . '/*.conf;') === FALSE) {
      $parents = drush_get_option('subdirs_parents_to_verify', array());
      $parents[$parent] = $parent;
      drush_set_option('subdirs_parents_to_verify', $parents);
      drush_log(dt('Parent site @parent is verified once more to include the subdirectory configs.', array('@parent' => $parent)), 'notice');
    }
  }

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

  function process() {
    parent::process();
    $this->data['uri'] = $this->uri();
    $this->data['subdir'] = $this->subdir();
    if (!$this->site_enabled) {
      $this->template = $this->disabled_template;
    }
  }

  function filename() {
    $filename = str_replace('/', '_', $this->subdir());
    return $this->data['http_subdird_path'] . '/' . $this->uri() . '/' . $filename . '.conf';
  }
}
