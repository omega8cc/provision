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

    // The site's own name is never its alias: as one, with a redirection
    // set, the vhost would give that name a redirect block to itself beside
    // the main block, and every request would answer with a redirect. Names
    // compare as the panel stores aliases.
    $own = strtolower(rtrim(trim($this->uri), '/'));
    foreach ($this->aliases as $key => $alias) {
      if (strtolower(rtrim(trim($alias), '/')) === $own) {
        unset($this->aliases[$key]);
      }
    }

    // Nor is the domain a subdirectory alias of the site lives in: that
    // domain's own vhost serves it and includes the subdirectory. Claimed
    // here, it would redirect to the subdirectory address, which then
    // redirects into itself.
    $parents = array();
    foreach ($this->aliases as $alias) {
      $parts = explode('/', strtolower(rtrim(trim($alias), '/')), 2);
      if (count($parts) == 2 && $parts[1] !== '') {
        $parents[$parts[0]] = TRUE;
      }
    }
    foreach ($this->aliases as $key => $alias) {
      if (isset($parents[strtolower(rtrim(trim($alias), '/'))])) {
        unset($this->aliases[$key]);
      }
    }

    // A redirection target that is neither the site's own name nor one of
    // its aliases is never served: a name the site was renamed away from
    // has no sites.php record or site directory behind it (it answered with
    // the installer, and the site's own name was left to nginx's default
    // server), and the settings.php trusted host patterns list only the
    // site's own name and its aliases, so the site refuses any other name,
    // its www companion without an alias row included. The aliases redirect
    // to the site's own name instead, which is served. Names compare as the
    // panel stores aliases (case, spaces, a trailing slash), and a target
    // takes the spelling of the alias it matches: nginx matches names
    // without case, so an alias spelled otherwise would get a redirect block
    // of its own to the target it is.
    if ($this->redirection) {
      $target = strtolower(rtrim(trim($this->redirection), '/'));
      $known = array(strtolower(rtrim(trim($this->uri), '/')) => $this->uri);
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
