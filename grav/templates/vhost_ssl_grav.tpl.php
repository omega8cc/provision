<?php
/**
 * @file
 * nginx vhost template for a Grav 2 site capsule (https + the http vhost via
 * the chained include at the end, mirroring the shared vhost_ssl.tpl.php
 * structure and the TXP pair).
 *
 * See vhost_grav.tpl.php for the capsule-docroot rationale. The one
 * https-specific addition: fastcgi_param HTTPS on at server level (the single
 * PHP surface, location = /index.php, declares no params of its own, so
 * server-level params inherit intact).
 */
$this->root = provision_auto_fix_platform_root($this->root);
$grav_capsule = "{$this->root}/sites/{$this->uri}";
?>
<?php if ($this->ssl_enabled && $this->ssl_key) : ?>

<?php
$script_user = d('@server_master')->script_user;
if (!$script_user) {
  $script_user = drush_get_option('script_user');
}
if (!$script_user && $server->script_user) {
  $script_user = $server->script_user;
}
// Enforced PHP (D-005 addendum): pin the enforced-version FPM socket
// (BOA default 8.4; 8.5 then the 8.3 floor as fallbacks), never the
// instance's per-site-selectable pool. The bare socket is the last resort on
// topologies without versioned pools. Render-time file checks: this runs on
// the box.
$user_socket = '/run/' . $script_user . '.fpm.socket';
foreach (array('84', '85', '83') as $grav_php_ver) {
  if (file_exists('/run/' . $script_user . '.' . $grav_php_ver . '.fpm.socket')) {
    $user_socket = '/run/' . $script_user . '.' . $grav_php_ver . '.fpm.socket';
    break;
  }
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

$nginx_has_ktls = d('@server_master')->nginx_has_ktls;
if (!$nginx_has_ktls) {
  $nginx_has_ktls = drush_get_option('nginx_has_ktls');
}
if (!$nginx_has_ktls && $server->nginx_has_ktls) {
  $nginx_has_ktls = $server->nginx_has_ktls;
}

$aegir_root = d('@server_master')->aegir_root;
$ssl_args = "ssl";
$ssl_listen_ipv4 = "*";
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
  listen  <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} {$ssl_args}"; ?>;
<?php if ($nginx_has_http3): ?>
  listen  <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} quic"; ?>;
  http3 on;
  http3_hq on;
<?php endif; ?>
<?php if ($nginx_has_http2): ?>
  http2 on;
<?php endif; ?>
<?php
  if ($this->redirection && $alias_url == $this->redirection) {
    $this->uri = str_replace('/', '.', $this->uri);
    print "  server_name  {$this->uri};\n";
  }
  else {
    $alias_url = str_replace('/', '.', $alias_url);
    print "  server_name  {$alias_url};\n";
  }
?>
  ssl_dhparam /etc/ssl/private/nginx-wild-ssl.dhp;
<?php if ($legacy_tls_enable): ?>
  ssl_protocols TLSv1.1 TLSv1.2 TLSv1.3;
<?php endif; ?>
  ssl_certificate_key <?php print $ssl_cert_key; ?>;
<?php if (!empty($ssl_chain_cert)) : ?>
  ssl_certificate     <?php print $ssl_chain_cert; ?>;
<?php else: ?>
  ssl_certificate     <?php print $ssl_cert; ?>;
<?php endif; ?>
<?php if ($nginx_has_ktls): ?>
  ssl_conf_command Options KTLS;
<?php endif; ?>
  location ^~ /.well-known/acme-challenge {
    allow all;
    alias <?php print $aegir_root; ?>/tools/le/.acme-challenges;
    try_files $uri 404;
  }
  access_log off;
  log_not_found off;
  return 301 $scheme://<?php print $this->redirection; ?>$request_uri;
}
<?php endif; ?>
<?php endforeach; ?>
<?php endif; ?>

server {
  include fastcgi_params;
  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";
  fastcgi_param HTTP_HOST $host;
  fastcgi_param MAIN_SITE_NAME <?php print $this->uri; ?>;
  set $main_site_name "<?php print $this->uri; ?>";
  fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
  fastcgi_param HTTPS on;
  # Pin the environment:// stream to the site uri (never the request
  # hostname). Proven to reach getenv() under this FPM stack (spike Q7).
  fastcgi_param GRAV_ENVIRONMENT <?php print $this->uri; ?>;
  listen  <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} {$ssl_args}"; ?>;
<?php if ($nginx_has_http3): ?>
  listen  <?php print "{$ssl_listen_ipv4}:{$http_ssl_port} quic"; ?>;
  http3 on;
  http3_hq on;
<?php endif; ?>
<?php if ($nginx_has_http2): ?>
  http2 on;
<?php endif; ?>
  server_name  <?php
    if ($this->redirection) {
      print str_replace('/', '.', $this->redirection);
    }
    else {
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
  root  <?php print $grav_capsule; ?>;
  ssl_dhparam /etc/ssl/private/nginx-wild-ssl.dhp;
<?php if ($legacy_tls_enable): ?>
  ssl_protocols TLSv1.1 TLSv1.2 TLSv1.3;
<?php endif; ?>
  ssl_certificate_key <?php print $ssl_cert_key; ?>;
<?php if (!empty($ssl_chain_cert)) : ?>
  ssl_certificate     <?php print $ssl_chain_cert; ?>;
<?php else: ?>
  ssl_certificate     <?php print $ssl_cert; ?>;
<?php endif; ?>
<?php if ($nginx_has_ktls): ?>
  ssl_conf_command Options KTLS;
<?php endif; ?>
  <?php print $extra_config; ?>
  include  <?php print $server->include_path; ?>/ip_access/<?php print $this->uri; ?>.conf*;
  include  <?php print $server->include_path; ?>/user_admin_access/<?php print $this->uri; ?>.conf*;
  set $ai_train_allow 0;
  set $ai_evasive_allow 0;
  include  <?php print $server->include_path; ?>/ai_policy/<?php print $this->uri; ?>.conf*;
<?php include(PROVISION_GRAV_DIR . '/templates/grav_locations.tpl.php'); ?>
<?php
$if_subsite = $this->data['http_subdird_path'] . '/' . $this->uri;
if (provision_hosting_feature_enabled('subdirs') && provision_file()->exists($if_subsite)->status()) {
  print "  include  " . $if_subsite . "/*.conf;\n";
}
?>
}

<?php endif; ?>

<?php
  // Generate the standard (http) Grav virtual host too. This template body is
  // eval()'d, so __FILE__ is unusable here -- PROVISION_GRAV_DIR comes from
  // the commandfile (real include time).
  include(PROVISION_GRAV_DIR . '/templates/vhost_grav.tpl.php');
?>
