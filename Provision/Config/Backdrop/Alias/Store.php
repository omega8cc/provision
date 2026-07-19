<?php
/**
 * @file
 * Provides the Provision_Config_Backdrop_Alias_Store class.
 */

class Provision_Config_Backdrop_Alias_Store extends Provision_Config_Drupal_Alias_Store {

  /**
   * Write the alias records PLUS a primary-URI self-map.
   *
   * Backdrop's find_conf_path() gates all site-dir probing on a truthy $sites
   * array, and the first conf_path() result is cached for the whole process:
   * a site with zero host aliases deployed first onto a fresh platform leaves
   * sites.php empty, so every conf_path() in that process resolves to '.' and
   * the deploy post-hook FULL escalation dies on the platform-root stub
   * settings.php (RCA 2026-07-19, the B->B migrate fatal). The self-map keeps
   * $sites truthy for every deployed site; for lookups the record is a no-op
   * (uri => uri names the directory Backdrop resolves anyway). Backdrop
   * platforms only: the shared Drupal store keeps its record shape.
   */
  function maintain() {
    parent::maintain();
    $this->records[$this->uri] = $this->uri;
  }
}
