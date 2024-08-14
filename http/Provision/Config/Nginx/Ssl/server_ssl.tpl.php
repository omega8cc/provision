<?php include(provision_class_directory('Provision_Config_Nginx_Server') . '/server.tpl.php'); ?>

#######################################################
###  nginx default ssl server
#######################################################

<?php
$satellite_mode = drush_get_option('satellite_mode');
if (!$satellite_mode && $server->satellite_mode) {
  $satellite_mode = $server->satellite_mode;
}

$nginx_has_http2 = drush_get_option('nginx_has_http2');
if (!$nginx_has_http2 && $server->nginx_has_http2) {
  $nginx_has_http2 = $server->nginx_has_http2;
}

$nginx_has_http3 = drush_get_option('nginx_has_http3');
if (!$nginx_has_http3 && $server->nginx_has_http3) {
  $nginx_has_http3 = $server->nginx_has_http3;
}

$ssl_args = "ssl";
$ssl_listen_ipv4 = "*";
$ssl_listen_ipv6 = "[::]";
?>

server {

  listen       <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} {$ssl_args}"; ?>;
  #listen       <?php print "{$ssl_listen_ipv6}:{$http_ssl_port} {$ssl_args}"; ?>;

<?php if ($nginx_has_http3): ?>
  #listen       <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} quic"; ?>;
  #http3                      on;
  #http3_hq                   on;
<?php endif; ?>

  server_name  _;
<?php if ($nginx_has_http2): ?>
  http2                      on;
<?php endif; ?>
  ssl_stapling               on;
  ssl_stapling_verify        on;
  resolver 1.1.1.1 1.0.0.1 valid=300s;
  resolver_timeout           5s;
  ssl_dhparam          /etc/ssl/private/nginx-wild-ssl.dhp;
  ssl_certificate      /etc/ssl/private/nginx-wild-ssl.crt;
  ssl_certificate_key  /etc/ssl/private/nginx-wild-ssl.key;
  location / {
    root                 /var/www/nginx-default;
    index                index.html index.htm;
  }
}
