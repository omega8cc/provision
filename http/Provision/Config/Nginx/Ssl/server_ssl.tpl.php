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

$nginx_has_ktls = drush_get_option('nginx_has_ktls');
if (!$nginx_has_ktls && $server->nginx_has_ktls) {
  $nginx_has_ktls = $server->nginx_has_ktls;
}

$ssl_args = "ssl";
$ssl_listen_ipv4 = "*";
?>

server {

  listen  <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} {$ssl_args}"; ?>;
<?php if ($nginx_has_http3): ?>
  listen  <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} quic"; ?>;
  http3 on;
  http3_hq on;
<?php endif; ?>
<?php if ($nginx_has_http2): ?>
  http2 on;
<?php endif; ?>
  server_name  _;
  ssl_dhparam          /etc/ssl/private/nginx-wild-ssl.dhp;
  ssl_certificate      /etc/ssl/private/nginx-wild-ssl.crt;
  ssl_certificate_key  /etc/ssl/private/nginx-wild-ssl.key;
<?php if ($nginx_has_ktls): ?>
  ssl_conf_command Options KTLS;
<?php endif; ?>
  location / {
    root  /var/www/nginx-default;
    index  index.html index.htm;
  }
}
