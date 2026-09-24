<?php

/**
 * Class for Nginx subdir support.
 */
class Provision_Config_Nginx_SubdirVhost extends Provision_Config_SubdirVhost {

  /**
   * Write the placeholder vhost of each parent domain that is not a site.
   *
   * None for a Grav or Textpattern site (its subdirectory alias is never
   * served, see Provision_Config_Nginx_Subdir::serves_subdirs()), none where
   * no conf was written to serve (a skipped alias), and none for a domain
   * another vhost of this server already answers for, typically as a site's
   * alias: a placeholder with the same server_name would take the name from
   * that site or lose it. A placeholder left from before such a site took the
   * name is removed then. A parent that is a site includes the confs in its
   * own vhost, and Provision_Config_Nginx_Subdir::report_parent() asks for its
   * verify when that vhost does not yet.
   */
  function write() {
    foreach (d()->aliases as $alias) {
      if (strpos($alias, '/') === FALSE) {
        continue;
      }
      if (provision_platform_is_foreign_cms(d()->root)) {
        break;
      }
      $this->current_alias = $alias;
      $parent = $this->uri();
      if ($this->parent_site()
        || !glob($this->data['http_subdird_path'] . '/' . $parent . '/*.conf')) {
        continue;
      }
      if ($vhost = $this->claimed_by($parent)) {
        drush_log(dt('Subdirectory alias @alias: @parent is served by @vhost, so it gets no vhost of its own; use that site\'s main name for the subdirectory.', array('@alias' => $alias, '@parent' => $parent, '@vhost' => $vhost)), 'warning');
        $subdir = new Provision_Config_Nginx_Subdir($this->context, $this->data);
        $subdir->unlink_placeholder($parent);
        continue;
      }
      drush_log(dt('Subdirectory alias %alias found. Generating default parent (%vhost) vhost configuration file.', array('%alias' => $alias, '%vhost' => $parent)), 'notice');
      Provision_Config_Http::write();
    }
    $this->current_alias = NULL;
  }

  /**
   * The vhost file of this server, other than the domain's own, that lists
   * the domain in a server_name line, or FALSE.
   */
  function claimed_by($domain) {
    $own = $this->data['http_vhostd_path'] . '/' . $domain;
    foreach ((array) glob($this->data['http_vhostd_path'] . '/*') as $vhost) {
      if ($vhost === $own || !is_file($vhost)) {
        continue;
      }
      $body = @file_get_contents($vhost);
      if (is_string($body) && preg_match_all('/^\s*server_name\s+([^;]+);/m', $body, $m)) {
        foreach ($m[1] as $names) {
          if (in_array($domain, preg_split('/\s+/', trim($names)))) {
            return basename($vhost);
          }
        }
      }
    }
    return FALSE;
  }

  /**
   * Nothing to remove for this site's aliases: their placeholder goes with
   * the domain's last conf, in Provision_Config_Nginx_Subdir::unlink_file().
   *
   * When this site is itself a parent whose vhost was just removed while
   * subdirectory sites remain under its name, they get the placeholder, so
   * they stay served until they go too.
   */
  function unlink() {
    $this->current_alias = d()->uri . '/';
    if (glob($this->data['http_subdird_path'] . '/' . d()->uri . '/*.conf')
      && !is_file($this->filename())) {
      drush_log(dt('Subdirectory sites remain under @uri: writing its subdirectory parent vhost.', array('@uri' => d()->uri)), 'notice');
      Provision_Config_Http::write();
    }
    $this->current_alias = NULL;
    return TRUE;
  }
}
