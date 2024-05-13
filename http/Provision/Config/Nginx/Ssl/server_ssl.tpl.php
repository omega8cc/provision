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

if ($nginx_has_http2) {
  $ssl_args = "ssl http2";
}
else {
  $ssl_args = "ssl";
}
if ($nginx_has_http3) {
  $ssl_args = "ssl";
}

if ($satellite_mode == 'boa') {
  $ssl_listen_ipv4 = "*";
  $ssl_listen_ipv6 = "[::]";
}
?>

server {
<?php if ($satellite_mode == 'boa'): ?>
  listen       <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} {$ssl_args}"; ?>;
  #listen       <?php print "{$ssl_listen_ipv6}:{$http_ssl_port} {$ssl_args}"; ?>;
<?php if ($nginx_has_http3): ?>
  listen       <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} quic reuseport"; ?>;
<?php endif; ?>
<?php else: ?>
<?php foreach ($server->ip_addresses as $ip) :?>
  listen       <?php print "{$ip}:{$http_ssl_port} {$ssl_args}"; ?>;
<?php if ($nginx_has_http3): ?>
  listen       <?php print "{$ip}:{$http_ssl_port} quic reuseport"; ?>;
<?php endif; ?>
<?php endforeach; ?>
  #listen       <?php print "[::]:{$http_ssl_port} {$ssl_args}"; ?>;
<?php endif; ?>
<?php if ($nginx_has_http3): ?>
  http2 on;
  add_header Alt-Svc 'h3=":<?php print "{$http_ssl_port}"; ?>"; ma=86400';
<?php endif; ?>
  server_name  _;
  ssl_stapling               on;
  ssl_stapling_verify        on;
  resolver 1.1.1.1 1.0.0.1 valid=300s;
  resolver_timeout           5s;
  ssl_dhparam          /etc/ssl/private/nginx-wild-ssl.dhp;
  ssl_certificate      /etc/ssl/private/nginx-wild-ssl.crt;
  ssl_certificate_key  /etc/ssl/private/nginx-wild-ssl.key;
  location / {
<?php if ($satellite_mode == 'boa'): ?>
    root                 /var/www/nginx-default;
    index                index.html index.htm;
<?php else: ?>
    return 404;
<?php endif; ?>
  }
}
