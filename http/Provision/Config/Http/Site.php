<?php

/**
 * Base class for virtual host configuration files.
 */
class Provision_Config_Http_Site extends Provision_Config_Http {
  public $template = 'vhost.tpl.php';
  // The template file to use when the site has been disabled.
  public $disabled_template = 'vhost_disabled.tpl.php';
  public $description = 'virtual host configuration file';


  function filename() {
    if (drush_get_option('provision_apache_conf_suffix', FALSE)) {
      return $this->data['http_vhostd_path'] . '/' . $this->uri . '.conf';
    }
    else {
      return $this->data['http_vhostd_path'] . '/' . $this->uri;
    }
  }

  function process() {
    parent::process();

    if ($this->aliases && !is_array($this->aliases)) {
      $this->aliases = explode(",", $this->aliases);
    }

    $this->aliases = array_filter($this->aliases, 'trim');

    // A redirection target that is neither the site's own name, its www
    // companion nor one of its aliases (a name the site was renamed away
    // from) has no sites.php record or site directory behind it: served, it
    // answered with the installer, and the site's own name was left to
    // nginx's default server. The aliases redirect to the site's own name
    // instead, which is served. Names compare as the panel stores aliases
    // (case, spaces, a trailing slash), and a target takes the spelling of
    // the name it matches: nginx matches names without case, so an alias
    // spelled otherwise would get a redirect block of its own to the target
    // it is. The www companion stays a target even when its automatic alias
    // is missing for a save whose DNS lookup failed: Drupal finds the site's
    // own directory for it by host suffix.
    if ($this->redirection) {
      $target = strtolower(rtrim(trim($this->redirection), '/'));
      $known = array(strtolower(rtrim(trim($this->uri), '/')) => $this->uri);
      if (strpos(strtolower($this->uri), 'www.') !== 0) {
        $known['www.' . strtolower(rtrim(trim($this->uri), '/'))] = 'www.' . $this->uri;
      }
      foreach ($this->aliases as $alias) {
        $known[strtolower(rtrim(trim($alias), '/'))] = $alias;
      }
      $this->redirection = isset($known[$target]) ? $known[$target] : $this->uri;
    }

    if ($this->drush_aliases && !is_array($this->drush_aliases)) {
      $this->drush_aliases = explode(",", $this->drush_aliases);
    }

    $this->drush_aliases = array_filter($this->drush_aliases, 'trim');

    if (!$this->site_enabled) {
      $this->template = $this->disabled_template;
    }

  }
}
