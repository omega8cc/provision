<?php $this->root = provision_auto_fix_platform_root($this->root); ?>

<?php if ($this->ssl_enabled && $this->ssl_key) : ?>

<?php
$script_user = d('@server_master')->script_user;
if (!$script_user) {
  $script_user = drush_get_option('script_user');
}
if (!$script_user && $server->script_user) {
  $script_user = $server->script_user;
}

$satellite_mode = d('@server_master')->satellite_mode;
if (!$satellite_mode) {
  $satellite_mode = drush_get_option('satellite_mode');
}
if (!$satellite_mode && $server->satellite_mode) {
  $satellite_mode = $server->satellite_mode;
}

$nginx_has_http2 = d('@server_master')->nginx_has_http2;
if (!$nginx_has_http2) {
  $nginx_has_http2 = drush_get_option('nginx_has_http2');
}
if (!$nginx_has_http2 && $server->nginx_has_http2) {
  $nginx_has_http2 = $server->nginx_has_http2;
}

$nginx_has_http3 = d('@server_master')->nginx_has_http3;
if (!$nginx_has_http3) {
  $nginx_has_http3 = drush_get_option('nginx_has_http3');
}
if (!$nginx_has_http3 && $server->nginx_has_http3) {
  $nginx_has_http3 = $server->nginx_has_http3;
}

$aegir_root = d('@server_master')->aegir_root;
$ssl_args = "ssl";
$ssl_listen_ipv4 = "*";
$ssl_listen_ipv6 = "[::]";
$main_name = $this->uri;
if ($this->redirection) {
  $main_name = $this->redirection;
}
$legacy_tls_ctrl = $aegir_root . "/static/control/tls-legacy-enable-" . $main_name . ".info";
$legacy_tls_enable = FALSE;
if (provision_file()->exists($legacy_tls_ctrl)->status()) {
  $legacy_tls_enable = TRUE;
}
?>

<?php if ($this->redirection): ?>
<?php foreach ($this->aliases as $alias_url): ?>
<?php if (!preg_match("/\.(?:nodns|dev|devel)\./", $alias_url)): ?>
server {
  listen       <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} {$ssl_args}"; ?>;
  #listen       <?php print "{$ssl_listen_ipv6}:{$http_ssl_port} {$ssl_args}"; ?>;
<?php if ($nginx_has_http3): ?>
  #listen       <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} quic"; ?>;
  #http3                      on;
  #http3_hq                   on;
<?php endif; ?>
<?php if ($nginx_has_http2): ?>
  http2                      on;
<?php endif; ?>
<?php
  // if we use redirections, we need to change the redirection
  // target to be the original site URL ($this->uri instead of
  // $alias_url)
  if ($this->redirection && $alias_url == $this->redirection) {
    $this->uri = str_replace('/', '.', $this->uri);
    print "  server_name  {$this->uri};\n";
  }
  else {
    $alias_url = str_replace('/', '.', $alias_url);
    print "  server_name  {$alias_url};\n";
  }
?>
  ssl_stapling               on;
  ssl_stapling_verify        on;
  resolver 1.1.1.1 1.0.0.1 valid=300s;
  resolver_timeout           5s;
  ssl_dhparam                /etc/ssl/private/nginx-wild-ssl.dhp;
<?php if ($legacy_tls_enable): ?>
  ssl_protocols              TLSv1.1 TLSv1.2 TLSv1.3;
<?php endif; ?>
  ssl_certificate_key        <?php print $ssl_cert_key; ?>;
<?php if (!empty($ssl_chain_cert)) : ?>
  ssl_certificate            <?php print $ssl_chain_cert; ?>;
<?php else: ?>
  ssl_certificate            <?php print $ssl_cert; ?>;
<?php endif; ?>

  ###
  ### Allow access to letsencrypt.org ACME challenges directory.
  ###
  location ^~ /.well-known/acme-challenge {
    allow all;
    alias <?php print $aegir_root; ?>/tools/le/.acme-challenges;
    try_files $uri 404;
  }

  ###
  ### Allow access to SQL Adminer.
  ###
  location ^~ /sqladmin/ {
    if ($is_crawler) {
      return 403;
    }
    include /var/aegir/config/includes/ip_access/sqladmin*;
    alias /var/www/adminer;
    index index.php index.html;
    try_files $uri 404;
  }

  return 301 $scheme://<?php print $this->redirection; ?>$request_uri;
}
<?php endif; ?>
<?php endforeach; ?>
<?php endif; ?>

server {
  include       fastcgi_params;
  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";
  fastcgi_param MAIN_SITE_NAME <?php print $this->uri; ?>;
  set $main_site_name "<?php print $this->uri; ?>";
  fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
  fastcgi_param HTTPS on;
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
  listen        <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} {$ssl_args}"; ?>;
  #listen        <?php print "{$ssl_listen_ipv6}:{$http_ssl_port} {$ssl_args}"; ?>;
<?php if ($nginx_has_http3): ?>
  #listen        <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} quic"; ?>;
  http2                      on;
  #http3                      on;
  #http3_hq                   on;
<?php endif; ?>
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
        if (trim($alias_url) && !preg_match("/\.(?:nodns|dev|devel)\./", $alias_url)) {
          print " " . str_replace('/', '.', $alias_url);
        }
      }
    } ?>;
  root          <?php print "{$this->root}"; ?>;
  ssl_stapling               on;
  ssl_stapling_verify        on;
  resolver 1.1.1.1 1.0.0.1 valid=300s;
  resolver_timeout           5s;
  ssl_dhparam                /etc/ssl/private/nginx-wild-ssl.dhp;
<?php if ($legacy_tls_enable): ?>
  ssl_protocols              TLSv1.1 TLSv1.2 TLSv1.3;
<?php endif; ?>
  ssl_certificate_key        <?php print $ssl_cert_key; ?>;
<?php if (!empty($ssl_chain_cert)) : ?>
  ssl_certificate            <?php print $ssl_chain_cert; ?>;
<?php else: ?>
  ssl_certificate            <?php print $ssl_cert; ?>;
<?php endif; ?>
  <?php print $extra_config; ?>
  include                    <?php print $server->include_path; ?>/ip_access/<?php print $this->uri; ?>*;
  include                    <?php print $server->include_path; ?>/nginx_vhost_common.conf;
}

<?php endif; ?>

<?php
  // Generate the standard virtual host too.
  include(provision_class_directory('Provision_Config_Nginx_Site') . '/vhost.tpl.php');
?>
