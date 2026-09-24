<?php
$script_user = d('@server_master')->script_user;
if (!$script_user) {
  $script_user = drush_get_option('script_user');
}
if (!$script_user && $server->script_user) {
  $script_user = $server->script_user;
}

$aegir_root = d('@server_master')->aegir_root;
if (!$aegir_root) {
  $aegir_root = drush_get_option('aegir_root');
}
if (!$aegir_root && $server->aegir_root) {
  $aegir_root = $server->aegir_root;
}

$boa_zones_file = '/etc/nginx/conf.d/limit-req-zones-boa.conf';
$boa_zones_body = @is_file($boa_zones_file)
  ? (string) @file_get_contents($boa_zones_file)
  : '';
?>
server {
  listen  *:<?php print $http_port; ?>;
  server_name  <?php print $uri; ?>;

  ###
  ### A domain that is not a site carries only its subdirectory sites here.
  ### nginx runs a location's set and if directives only for the requests
  ### that end in that location, never for its nested ones, so the guards and
  ### the PHP-FPM pool a site's vhost sets through the shared include are set
  ### at this level, where they run for every request.
  ###
  set $nocache_details "Cache";

  if ($is_banned) {
    return 444;
  }

<?php if (strpos($boa_zones_body, 'map $boa_fleet_uaid $boa_fleet_block {') !== FALSE): ?>
  if ($boa_fleet_block) {
    return 429;
  }

<?php endif; ?>
  if ($is_crawler) {
    return 444;
  }

  if ($is_botnet) {
    return 444;
  }

  include /data/conf/nginx_high_load.c*;

  if ( $request_method !~ ^(?:GET|HEAD|POST|PUT|PATCH|DELETE|OPTIONS)$ ) {
    return 444;
  }

  if ($is_denied) {
    return 444;
  }

  if ($ua_denied) {
    return 444;
  }

  if ($tls_on_plain) {
    return 444;
  }

  include  <?php print $aegir_root; ?>/config/server_master/nginx/post.d/nginx_force_include*;
  include  <?php print $aegir_root; ?>/config/server_master/nginx/post.d/fpm_include*;

  if ($user_socket = '') {
    set $user_socket "<?php print $script_user; ?>";
  }

  include  <?php print $subdirs_path; ?>/<?php print $uri; ?>/*.conf;
}
