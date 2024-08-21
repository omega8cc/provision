<?php $this->root = provision_auto_fix_platform_root($this->root); ?>

<?php
$script_user = d('@server_master')->script_user;
if (!$script_user) {
  $script_user = drush_get_option('script_user');
}
if (!$script_user && $server->script_user) {
  $script_user = $server->script_user;
}

if ($this->redirection) {
  $aegir_root = d('@server_master')->aegir_root;
  $satellite_mode = d('@server_master')->satellite_mode;
  // Redirect all aliases to the main http url using separate vhosts blocks to avoid if{} in Nginx.
  foreach ($this->aliases as $alias_url) {
    if (!preg_match("/\.(?:nodns|dev|devel)\./", $alias_url)) {
      print "\n";
      print "# alias redirection virtual host\n";
      print "server {\n";
      print "  listen       *:{$http_port};\n";
      print "  #listen       [::]:{$http_port};\n";
      // if we use redirections, we need to change the redirection
      // target to be the original site URL ($this->uri instead of
      // $alias_url)
      if ($this->redirection && $alias_url == $this->redirection) {
        $this->uri = str_replace('/', '.', $this->uri);
        print "  server_name  {$this->uri};\n";
      } else {
        $alias_url = str_replace('/', '.', $alias_url);
        print "  server_name  {$alias_url};\n";
      }
      print "  access_log   off;\n";
      if ($satellite_mode == 'boa') {
        print "\n";
        print "  ###\n";
        print "  ### Allow access to letsencrypt.org ACME challenges directory.\n";
        print "  ###\n";
        print "  location ^~ /.well-known/acme-challenge {\n";
        print "    allow all;\n";
        print "    alias {$aegir_root}/tools/le/.acme-challenges;\n";
        print "    try_files \$uri 404;\n";
        print "  }\n";
        print "\n";
      }
      print "  return 301 \$scheme://{$this->redirection}\$request_uri;\n";
      print "}\n";
    }
  }
}

if ($this->redirection || !$this->redirection) {
  $aegir_root = d('@server_master')->aegir_root;
  $satellite_mode = d('@server_master')->satellite_mode;
  foreach ($this->aliases as $alias_url) {
    if (preg_match("/\.(?:nodns)\./", $alias_url)) {
      print "\n";
      print "# nodns alias exception virtual host\n";
      print "server {\n";
      print "  listen       *:{$http_port};\n";
      print "  #listen       [::]:{$http_port};\n";
      print "  include       fastcgi_params;\n";
      print "  fastcgi_param HTTP_PROXY \"\";\n";
      print "  fastcgi_param MAIN_SITE_NAME {$this->uri};\n";
      print "  set \$main_site_name {$this->uri};\n";
      print "  fastcgi_param SCRIPT_FILENAME \$document_root\$fastcgi_script_name;\n";

      if (!$db_type || !$db_name || !$db_user || !$db_passwd || !$db_host) {
        $db_type = 'mysqli';
        $db_name = 'none';
        $db_user = 'none';
        $db_passwd = 'none';
        $db_host = 'localhost';
      }

      $db_type = urlencode($db_type);
      $db_name = urlencode($db_name);
      $db_user = urlencode($db_user);
      $db_passwd = urlencode($db_passwd);
      $db_host = urlencode($db_host);

      print "  fastcgi_param db_type   {$db_type};\n";
      print "  fastcgi_param db_name   {$db_name};\n";
      print "  fastcgi_param db_user   {$db_user};\n";
      print "  fastcgi_param db_passwd {$db_passwd};\n";
      print "  fastcgi_param db_host   {$db_host};\n";

      if (!$db_port) {
        $ctrlf = '/data/conf/' . $script_user . '_use_proxysql.txt';
        if (provision_file()->exists($ctrlf)->status()) {
          $db_port = '6033';
        }
        else {
          $db_port = $this->server->db_port ? $this->server->db_port : '3306';
        }
      }
      $db_port = urlencode($db_port);
      print "  fastcgi_param db_port   {$db_port};\n";

      $alias_url = str_replace('/', '.', $alias_url);
      print "  server_name  {$alias_url};\n";
      print "  root  {$this->root};\n";
      print "  include       " . $server->include_path . "/ip_access/{$this->uri}.conf;\n";
      print "  include       " . $server->include_path . "/nginx_vhost_common.conf;\n";
      print "}\n";
    }
  }
}
?>

server {
  include       fastcgi_params;
  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";
  fastcgi_param MAIN_SITE_NAME <?php print $this->uri; ?>;
  set $main_site_name "<?php print $this->uri; ?>";
  fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
<?php
  // If any of those parameters is empty for any reason, like after an attempt
  // to import complete platform with sites without importing their databases,
  // it will break Nginx reload and even shutdown all sites on the system on
  // Nginx restart, so we need to use dummy placeholders to avoid affecting
  // other sites on the system if this site is broken.
  if (!$db_type || !$db_name || !$db_user || !$db_passwd || !$db_host) {
    $db_type = 'mysqli';
    $db_name = 'none';
    $db_user = 'none';
    $db_passwd = 'none';
    $db_host = 'localhost';
  }
?>
  fastcgi_param db_type   <?php print urlencode($db_type); ?>;
  fastcgi_param db_name   <?php print urlencode($db_name); ?>;
  fastcgi_param db_user   <?php print implode('@', array_map('urlencode', explode('@', $db_user))); ?>;
  fastcgi_param db_passwd <?php print urlencode($db_passwd); ?>;
  fastcgi_param db_host   <?php print urlencode($db_host); ?>;
<?php
  // Until the real source of this problem is fixed elsewhere, we have to
  // use this simple fallback to guarantee that empty db_port does not
  // break Nginx reload which results with downtime for the affected vhosts.
  if (!$db_port) {
    $ctrlf = '/data/conf/' . $script_user . '_use_proxysql.txt';
    if (provision_file()->exists($ctrlf)->status()) {
      $db_port = '6033';
    }
    else {
      $db_port = $this->server->db_port ? $this->server->db_port : '3306';
    }
  }
?>
  fastcgi_param db_port   <?php print urlencode($db_port); ?>;
  listen        *:<?php print $http_port; ?>;
  #listen        [::]:<?php print $http_port; ?>;
  server_name   <?php
    // this is the main vhost, so we need to put the redirection
    // target as the hostname (if it exists) and not the original URL
    // ($this->uri)
    if ($this->redirection) {
      print str_replace('/', '.', $this->redirection);
    } else {
      print $this->uri;
    }
    if (is_array($this->aliases)) {
      foreach ($this->aliases as $alias_url) {
        if (trim($alias_url) && preg_match("/\.(?:dev|devel)\./", $alias_url)) {
          print " " . str_replace('/', '.', $alias_url);
        }
      }
    }
    if (!$this->redirection && is_array($this->aliases)) {
      foreach ($this->aliases as $alias_url) {
        if (trim($alias_url) && !preg_match("/\.(?:dev|devel)\./", $alias_url)) {
          print " " . str_replace('/', '.', $alias_url);
        }
      }
    } ?>;
  root          <?php print "{$this->root}"; ?>;
  <?php print $extra_config; ?>
<?php
if ($this->redirection || $ssl_redirection) {
  if ($ssl_redirection && !$this->redirection) {
    // redirect aliases in non-ssl to the same alias on ssl.
    print "\n  return 301 https://\$host\$request_uri;\n";
  }
  elseif ($ssl_redirection && $this->redirection) {
    // redirect all aliases + main uri to the main https uri.
    print "\n  return 301 https://{$this->redirection}\$request_uri;\n";
  }
  elseif (!$ssl_redirection && $this->redirection) {
    print "  include       " . $server->include_path . "/ip_access/{$this->uri}.conf;\n";
    print "  include       " . $server->include_path . "/nginx_vhost_common.conf;\n";
  }
}
else {
  print "  include       " . $server->include_path . "/ip_access/{$this->uri}.conf;\n";
  print "  include       " . $server->include_path . "/nginx_vhost_common.conf;\n";
}
$if_subsite = $this->data['http_subdird_path'] . '/' . $this->uri;
if (provision_hosting_feature_enabled('subdirs') && provision_file()->exists($if_subsite)->status()) {
  print "  include       " . $if_subsite . "/*.conf;\n";
}
?>
}
