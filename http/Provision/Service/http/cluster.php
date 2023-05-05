<?php

class Provision_Service_http_cluster extends Provision_Service_http {
  static function option_documentation() {
    return array(
      'cluster_web_servers' => 'server with cluster: comma-separated list of web servers.'
    );
  }

  function init_server() {
    $this->server->setProperty('cluster_web_servers', [], TRUE);
  }

  function init_site() {
    $this->_each_server(__FUNCTION__);
  }

  /**
   * Run a method on each server in the cluster.
   *
   * This function does a logical AND on the return status of each of the
   * methods, and returns TRUE only if they all returned something that
   * can be interpreted as TRUE.
   */
  function _each_server($method, $args = []) {
    // Return True by default.
    $ret = TRUE;
    foreach ($this->server->cluster_web_servers as $server) {
      // If any methods return false, return false for the whole operation.
      $result = call_user_func_array(array(d($server)->service('http', $this->context), $method), $args);
      $ret = $ret && $result;
    }
    return $ret;
  }

  function parse_configs() {
    $this->_each_server(__FUNCTION__);
  }

  function create_config($config, $data = []) {
    $this->_each_server(__FUNCTION__, array($config));
  }

  function delete_config($config, $data = []) {
    $this->_each_server(__FUNCTION__, array($config));

    return $this;
  }

  function restart() {
    $this->_each_server(__FUNCTION__);
  }

  /**
   * Support the ability to cloak database credentials using environment variables.
   *
   * The cluster supports this functionality only if ALL the servers it maintains 
   * supports this functionality.
   */
  function cloaked_db_creds() {
    return $this->_each_server(__FUNCTION__);
  }

  function sync($path = NULL, $additional_options = []) {
    $args = func_get_args();
    $this->_each_server(__FUNCTION__, $args);
  }

  function fetch($path = NULL) {
    drush_log('Skipping fetch from remote server(s), since master is authoritative in cluster service.');
  }

  function grant_server_list() {
    return array_merge(
      array_map('d', $this->server->cluster_web_servers),
      array($this->context->platform->server)
    );
  }
}
