<?php
/**
 * @file
 * nginx vhost template for a Textpattern multisite site.
 *
 * Suspend parity (map R-18): BOA suspension = /data/conf/suspended/<oct>.pid
 * (managed by `boa suspend|unsuspend`; enforced for Drupal/Backdrop in the
 * global settings chain). Foreign-CMS sites never run that chain, so the
 * check lives in the PHP locations here — location-scoped on purpose: statics
 * and acme keep serving during suspension, exactly like the Drupal behaviour.
 * Shared shape with the grav track.
 *
 * (original @file docs continue below)
 * nginx vhost for a Textpattern multisite site (https + the http
 * vhost via the chained include at the end, mirroring the shared
 * vhost_ssl.tpl.php structure).
 *
 * See vhost_txp.tpl.php for the TXP location contract rationale. The one
 * https-specific addition: fastcgi_param HTTPS on at BOTH php surfaces (the
 * admin location re-declares the full param set -- inheritance is
 * all-or-nothing -- so it carries its own HTTPS on here).
 */
$this->root = provision_auto_fix_platform_root($this->root);
$site_public = "{$this->root}/sites/{$this->uri}/public";
$site_admin = "{$this->root}/sites/{$this->uri}/admin";
$txp_admin_path = defined('PROVISION_TXP_ADMIN_PATH') ? PROVISION_TXP_ADMIN_PATH : 'txpadmin';
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
// Enforced PHP (D-008 addendum): pin the enforced-version FPM socket
// (BOA default 8.4; 8.5 fallback), never the instance's per-site-selectable
// pool. The bare socket is the last resort on topologies without versioned
// pools. Render-time file checks: this runs on the box.
$user_socket = '/run/' . $script_user . '.fpm.socket';
foreach (array('84', '85') as $txp_php_ver) {
  if (file_exists('/run/' . $script_user . '.' . $txp_php_ver . '.fpm.socket')) {
    $user_socket = '/run/' . $script_user . '.' . $txp_php_ver . '.fpm.socket';
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

// Direct /files/ downloads are DENIED by default (D-010); per-site opt-out via
// the same control-file idiom as the legacy-TLS gate above.
$txp_files_open = provision_file()
  ->exists($aegir_root . '/static/control/txp-files-open-' . $main_name . '.info')
  ->status();
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
  fastcgi_param REQUEST_SCHEME $scheme;
  fastcgi_param MAIN_SITE_NAME <?php print $this->uri; ?>;
  set $main_site_name "<?php print $this->uri; ?>";
  fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
  fastcgi_param HTTPS on;
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
  root  <?php print $site_public; ?>;
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

  ### TXP location contract -- inline (no shared common include).
  ### NB no acme-challenge block here: BOA injects it via $extra_config.

  ###
  ### SHARED PROTECTIONS, RE-STATED -- see vhost_txp.tpl.php for the full
  ### rationale (this vhost does not include nginx_vhost_common.conf, so the
  ### CMS-agnostic guards, including the AI-policy ENFORCEMENT the fragment
  ### above only configures, must be repeated here).
  ###
  if ($is_ai_forged) { return 444; }
  set $ai_train_block $is_ai_training;
  if ($ai_train_allow) { set $ai_train_block ''; }
  if ($ai_train_block) { return 444; }
  set $ai_evasive_block $is_ai_evasive;
  if ($ai_evasive_allow) { set $ai_evasive_block ''; }
  if ($ai_evasive_block) { return 444; }
  if ($is_crawler) { return 444; }
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;

  ### Operator escape hatch: the box-wide high-load protection drop-in that
  ### every other vhost includes. Without it a TXP site silently ignores
  ### operator load shedding. (Capacity-shed layer, part 1 of 2: the
  ### zone-dependent limits -- AI-class rate limits and the per-vhost
  ### anonymous-render cap -- are a designed round-3 item, presence-gated on
  ### the BOA-written zones file, NOT copied blind: their key semantics are
  ### CMS-specific. See boa-txp docs/integration-map.md R-22.)
  include /data/conf/nginx_high_load.c*;

  ### Reject non-standard request methods without a 405 body (shared-include
  ### parity; CMS-agnostic).
  if ($request_method !~ ^(?:GET|HEAD|POST|PUT|PATCH|DELETE|OPTIONS)$) {
    return 444;
  }

  ### Deny listed requests for security reasons (verbatim from the shared include).
  location ~* (\.(?:git.*|htaccess|engine|config|inc|ini|info|install|make|module|profile|test|po|sh|.*sql|theme|twig|tpl(\.php)?|xtmpl|yml)(~|\.sw[op]|\.bak|\.orig|\.save)?$|^(\..*|Entries.*|Repository|Root|Tag|Template|composer\.(json|lock))$|^#.*#$|\.php(~|\.sw[op]|\.bak|\.orig\.save))$ {
    access_log off;
    log_not_found off;
    return 404;
  }

  location ~ /\.(?!well-known) { deny all; }
  location ~* \.txp$ { return 403; }
  location ~* ^/themes/.*/manifest\.json$ { deny all; }
<?php if (!$txp_files_open): ?>
  ### Direct file downloads DENIED by default (D-010) -- see the http twin.
  location ^~ /files/ { return 403; }
<?php endif; ?>

  location = /favicon.ico { access_log off; try_files $uri =204; }
  location = /robots.txt  { access_log off; try_files $uri =404; }

  # Public PHP whitelist: EXACTLY index.php and css.php.
  location = /index.php {
    if (-f /data/conf/suspended/<?php print $script_user; ?>.pid) { return 503; }
    fastcgi_pass unix:<?php print $user_socket; ?>;
  }
  location = /css.php {
    if (-f /data/conf/suspended/<?php print $script_user; ?>.pid) { return 503; }
    fastcgi_pass unix:<?php print $user_socket; ?>;
  }

  # Path-mapped multisite admin (single segment; on-disk dir is 'admin').
  location ^~ /<?php print $txp_admin_path; ?> {
    alias <?php print $site_admin; ?>;
    index index.php;
    ### A `^~` prefix location suppresses evaluation of every server-level
    ### REGEX location, so the dotfile and sensitive-file denies above do NOT
    ### reach requests under the admin path. Re-state them here: the plugins
    ### dir is a per-site upload target and plugin archives routinely carry
    ### .git/, .env, dumps and editor leftovers.
    location ~ /\. { deny all; }
    location ~* \.(?:git.*|htaccess|ini|sh|.*sql|yml|bak|orig|save|swp)$ {
      access_log off;
      log_not_found off;
      return 404;
    }
    location ~ ^/<?php print $txp_admin_path; ?>/index\.php$ {
      if (-f /data/conf/suspended/<?php print $script_user; ?>.pid) { return 503; }
      include fastcgi_params;
      fastcgi_param HTTP_PROXY "";
      fastcgi_param HTTP_HOST $host;
      fastcgi_param REQUEST_SCHEME $scheme;
  fastcgi_param REQUEST_SCHEME $scheme;
      fastcgi_param MAIN_SITE_NAME <?php print $this->uri; ?>;
      fastcgi_param HTTPS on;
      fastcgi_param SCRIPT_FILENAME $request_filename;
      fastcgi_pass unix:<?php print $user_socket; ?>;
    }
    location ~* \.php$ { return 404; }
  }

  # Front controller (keeps the query string).
  location / {
    try_files $uri $uri/ /index.php?$args;
  }

  # Everything else that ends in .php 404s (upload dirs included).
  location ~* \.php$ { return 404; }
<?php
$if_subsite = $this->data['http_subdird_path'] . '/' . $this->uri;
if (provision_hosting_feature_enabled('subdirs') && provision_file()->exists($if_subsite)->status()) {
  print "  include  " . $if_subsite . "/*.conf;\n";
}
?>
}

<?php endif; ?>

<?php
  // Generate the standard (http) TXP virtual host too. This template body is
  // eval()'d, so __FILE__ is unusable here -- PROVISION_TXP_DIR comes from the
  // commandfile (real include time).
  include(PROVISION_TXP_DIR . '/templates/vhost_txp.tpl.php');
?>
