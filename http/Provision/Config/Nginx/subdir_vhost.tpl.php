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

$nginx_has_http3 = d('@server_master')->nginx_has_http3;
if (!$nginx_has_http3) {
  $nginx_has_http3 = drush_get_option('nginx_has_http3');
}
if (!$nginx_has_http3 && $server->nginx_has_http3) {
  $nginx_has_http3 = $server->nginx_has_http3;
}

$boa_zones_file = '/etc/nginx/conf.d/limit-req-zones-boa.conf';
$boa_zones_body = @is_file($boa_zones_file)
  ? (string) @file_get_contents($boa_zones_file)
  : '';
?>
server {
  listen  *:<?php print $http_port; ?>;
  server_name  <?php print $uri; ?>;
  include  <?php print $server->include_path; ?>/ip_access/<?php print $uri; ?>.conf*;

  ###
  ### A domain that is not a site carries only its subdirectory sites here.
  ### nginx runs a location's set and if directives only for the requests
  ### that end in that location, never for its nested ones, so the guards and
  ### the PHP-FPM pool a site's vhost sets through the shared include are set
  ### at this level, where they run for every request, in the site vhost's
  ### order.
  ###
  set $main_site_name "<?php print $uri; ?>";
  set $nocache_details "Cache";

  ###
  ### AI training and evasive fetchers are refused unless the ai_policy
  ### fragment of this name opts in, as for a site.
  ###
  set $ai_train_allow 0;
  set $ai_evasive_allow 0;
  include  <?php print $server->include_path; ?>/ai_policy/<?php print $uri; ?>.conf*;

  ###
  ### The subdirectory sites go in where a site's vhost includes them, ahead
  ### of the guards and the PHP-FPM pins: each names itself in
  ### $main_site_name for its own paths, and the pins key on it.
  ###
  include  <?php print $subdirs_path; ?>/<?php print $uri; ?>/*.conf;

  ###
  ### Tested after the subdirectory confs, which give the static-chain flag
  ### its verdict for their own paths.
  ###
  if ($is_static_chain) {
    return 444;
  }

  if ($is_content_chain) {
    return 404;
  }

  if ($is_banned) {
    return 444;
  }

<?php if (strpos($boa_zones_body, 'map $boa_fleet_uaid $boa_fleet_block {') !== FALSE): ?>
  if ($boa_fleet_block) {
    return 429;
  }

<?php endif; ?>
  ###
  ### Return 404 on special PHP URLs to avoid revealing version used.
  ###
  if ( $args ~* "=PHP[A-Z0-9]{8}-" ) {
    return 404;
  }

  if ($is_secret_path) {
    return 444;
  }

  if ($is_cms_probe) {
    return 444;
  }

  if ($is_ai_forged) {
    return 444;
  }

  set $ai_train_block $is_ai_training;
  if ($ai_train_allow) {
    set $ai_train_block '';
  }
  if ($ai_train_block) {
    return 444;
  }

  set $ai_evasive_block $is_ai_evasive;
  if ($ai_evasive_allow) {
    set $ai_evasive_block '';
  }
  if ($ai_evasive_block) {
    return 444;
  }

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

  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
<?php if ($nginx_has_http3): ?>
  add_header Alt-Svc 'h3=":443"; ma=86400';
<?php endif; ?>

  include  <?php print $aegir_root; ?>/config/server_master/nginx/post.d/nginx_force_include*;
  include  <?php print $aegir_root; ?>/config/server_master/nginx/post.d/fpm_include*;

  if ($user_socket = '') {
    set $user_socket "<?php print $script_user; ?>";
  }
}
