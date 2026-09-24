<?php $this->root = provision_auto_fix_platform_root($this->root); ?>

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

$nginx_config_mode = d('@server_master')->nginx_config_mode;
if (!$nginx_config_mode) {
  $nginx_config_mode = drush_get_option('nginx_config_mode');
}
if (!$nginx_config_mode && $server->nginx_config_mode) {
  $nginx_config_mode = $server->nginx_config_mode;
}

$phpfpm_mode = d('@server_master')->phpfpm_mode;
if (!$phpfpm_mode) {
  $phpfpm_mode = drush_get_option('phpfpm_mode');
}
if (!$phpfpm_mode && $server->phpfpm_mode) {
  $phpfpm_mode = $server->phpfpm_mode;
}

// We can use $server here once we have proper inheritance.
// See Provision_Service_http_nginx_ssl for details.
$phpfpm_socket_path = Provision_Service_http_nginx::getPhpFpmSocketPath();

$nginx_is_modern = d('@server_master')->nginx_is_modern;
if (!$nginx_is_modern) {
  $nginx_is_modern = drush_get_option('nginx_is_modern');
}
if (!$nginx_is_modern && $server->nginx_is_modern) {
  $nginx_is_modern = $server->nginx_is_modern;
}

$nginx_has_etag = d('@server_master')->nginx_has_etag;
if (!$nginx_has_etag) {
  $nginx_has_etag = drush_get_option('nginx_has_etag');
}
if (!$nginx_has_etag && $server->nginx_has_etag) {
  $nginx_has_etag = $server->nginx_has_etag;
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

$nginx_has_gzip = d('@server_master')->nginx_has_gzip;
if (!$nginx_has_gzip) {
  $nginx_has_gzip = drush_get_option('nginx_has_gzip');
}
if (!$nginx_has_gzip && $server->nginx_has_gzip) {
  $nginx_has_gzip = $server->nginx_has_gzip;
}

$satellite_mode = d('@server_master')->satellite_mode;
if (!$satellite_mode) {
  $satellite_mode = drush_get_option('satellite_mode');
}
if (!$satellite_mode && $server->satellite_mode) {
  $satellite_mode = $server->satellite_mode;
}

$subdir_loc = str_replace('/', '_', $subdir);
$subdir_dot = str_replace('/', '.', $subdir);
?>
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
#######################################################
###  nginx.conf site level extended vhost include start
#######################################################

###
### Subdirectory site <?php print $this->uri; ?>.
### Its own name is printed into every location below, never set as a
### variable: every subdirectory conf of one domain is included at the
### same server level, where the last set of a shared variable would win
### for all of them.
###

###
### This site's assets sit one path segment below the domain, where the
### static-chain map in server.tpl.php reads /<?php print $subdir; ?>/sites/all/...
### as relative-URL junk: the same root dirs the map lets through at a
### domain's root are let through here. The parent's vhost includes this file
### before its shared include, so this runs before that guard does; anything
### deeper under /<?php print $subdir; ?>/ stays guarded.
###
if ($uri ~* "^/<?php print $subdir; ?>/(?:sites|modules|misc|themes|core|libraries|profiles|cdn|files|system|external|s3)/") {
  set $is_static_chain 0;
}

###
### Drop repeated /node/<id>/.../node/<id>/ patterns flood (botnet typical abuse)
###
if ($is_node_chain) {
  return 404;
}

###
### Drop “too many language prefixes” (botnet typical abuse)
### The map keys on the full $uri, so a language-like subdir name (/pl,
### /pt-br) consumes one of the 4 chain slots — such subdir sites see an
### effective site-relative threshold of 3.
###
if ($is_lang_chain) {
  return 404;
}

<?php
// Gated on the BOA http-scope file declaring $is_amp_chain, exactly like the
// full-domain vhost include: no delivery order can reference an undefined
// variable (a whole-box nginx [emerg] on a restart without a configtest).
$boa_zones_file = '/etc/nginx/conf.d/limit-req-zones-boa.conf';
$boa_zones_body = @is_file($boa_zones_file)
  ? (string) @file_get_contents($boa_zones_file)
  : '';
if (strpos($boa_zones_body, 'map $args $is_amp_chain') !== FALSE):
?>
###
### Drop HTML-entity "amp chain" query mutation spam (botnet typical abuse).
### It keys on the query only, so a subdir site matches exactly like a full
### domain.
###
if ($is_amp_chain) {
  return 404;
}
<?php endif; ?>

###
### The scheme the redirects below keep, as in the shared vhost include:
### https for a visitor who came through the wildcard SSL front, which
### reaches this vhost over plain HTTP with X-Forwarded-Proto: https. Set
### here as well for a domain that is no site, whose server has no shared
### include; every subdirectory conf of a domain sets the same value.
###
set $boa_visitor_scheme $scheme;
if ($http_x_forwarded_proto = "https") {
  set $boa_visitor_scheme "https";
}

# $is_static_chain / $is_content_chain are not tested here. A site's vhost tests
# both for every path through the shared include, subdirectories included, which
# is why this site's own asset roots clear the static-chain flag above; the vhost
# of a domain that is no site tests neither.

# Mitigation for https://www.drupal.org/SA-CORE-2018-002
set $rce "ZZ";
if ( $query_string ~* (23value|23default_value|element_parents=%23) ) {
  set $rce "A";
}

if ( $request_method = POST ) {
  set $rce "${rce}B";
}

if ( $rce = "AB" ) {
  return 444;
}

###
### Helper locations to avoid 404 on legacy images paths
###
location ^~ /<?php print $subdir; ?>/sites/default/files {


  root  <?php print "{$this->root}"; ?>;

  location ~* ^/<?php print $subdir; ?>/sites/default/files/imagecache {
    access_log off;
    log_not_found off;
    expires 30d;
    set $nocache_details "Skip";
    rewrite ^/<?php print $subdir; ?>/sites/default/files/imagecache/(.*)$ /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/imagecache/$1 last;
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }
  location ~* ^/<?php print $subdir; ?>/sites/default/files/(css|js|styles) {
    access_log off;
    log_not_found off;
    expires 30d;
    set $nocache_details "Skip";
    rewrite ^/<?php print $subdir; ?>/sites/default/files/(css|js|styles)/(.*)$ /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1/$2 last;
    try_files /$2 $uri @drupal_<?php print $subdir_loc; ?>;
  }
  location ~* ^/<?php print $subdir; ?>/sites/default/files {
    access_log off;
    log_not_found off;
    expires 30d;
    rewrite ^/<?php print $subdir; ?>/sites/default/files/(.*)$ /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
    try_files /$1 $uri =404;
  }
}

###
### Redirect to working homepage. Kept outside the master location, which
### takes only paths under /<?php print $subdir; ?>/, so the parent site keeps
### its own /<?php print $subdir; ?>-anything paths. Relative, so a visitor who
### came in over HTTPS through the wildcard front stays on HTTPS.
###
location = /<?php print $subdir; ?> {
  access_log off;
  log_not_found off;
  absolute_redirect off;
  return 301 /<?php print $subdir; ?>/;
}

###
### Master location for subdir support (start)
###
location ^~ /<?php print $subdir; ?>/ {

  root  <?php print "{$this->root}"; ?>;

  set $nocache_details "Cache";

  ###
  ### Drop security-banned client IPs.  nginx runs these guards here only for
  ### requests that end in this location, never for the nested ones: for the
  ### whole domain they run at server level, from the shared include in a
  ### site's vhost or from subdir_vhost.tpl.php for a domain that is no site.
  ###
  if ($is_banned) {
    return 444;
  }

<?php
// Reuses the zones body read at the amp-chain gate above; the fallback read
// runs only if that gate ever moves below this one.
if (!isset($boa_zones_body)) {
  $boa_zones_file = '/etc/nginx/conf.d/limit-req-zones-boa.conf';
  $boa_zones_body = @is_file($boa_zones_file)
    ? (string) @file_get_contents($boa_zones_file)
    : '';
}
if (strpos($boa_zones_body, 'map $boa_fleet_uaid $boa_fleet_block {') !== FALSE):
?>
  ###
  ### Refuse a declared crawler-fleet fingerprint (see the $boa_fleet_* maps
  ### in the BOA http-scope zones file).  429 because no IDS scorer counts it.
  ###
  if ($boa_fleet_block) {
    return 429;
  }

<?php endif; ?>
  ###
  ### Deny crawlers.
  ###
  if ($is_crawler) {
    return 444;
  }

  ###
  ### Block semalt botnet.
  ###
  if ($is_botnet) {
    return 444;
  }

  ###
  ### Add recommended HTTP headers
  ### Note: any location with its own add_header directives cancels ALL
  ### inherited add_header lines, so this pair is re-stated verbatim in
  ### every such static-serving location below. Do not deduplicate.
  ###
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;

  ###
  ### Include high load protection config if exists.
  ###
  include /data/conf/nginx_high_load.c*;

  ###
  ### Include PHP-FPM version override logic if exists.
  ###
  include  <?php print $aegir_root; ?>/config/server_master/nginx/post.d/fpm_include*;

  ###
  ### Allow to use non-default PHP-FPM version for the site
  ### listed in the special include file.
  ###
  if ($user_socket = '') {
    set $user_socket "<?php print $script_user; ?>";
  }

  ###
  ### Deny not compatible request methods without 405 response.
  ###
  if ( $request_method !~ ^(?:GET|HEAD|POST|PUT|PATCH|DELETE|OPTIONS)$ ) {
    return 444;
  }

  ###
  ### Deny listed requests for security reasons.
  ###
  if ($is_denied) {
    return 444;
  }

  ###
  ### HTTPRL standard support.
  ###
  location ^~ /<?php print $subdir; ?>/httprl_async_function_callback {
    location ~* ^/<?php print $subdir; ?>/httprl_async_function_callback {
      access_log off;
      log_not_found off;
      set $nocache_details "Skip";
      try_files /httprl_async_function_callback $uri @drupal_<?php print $subdir_loc; ?>;
    }
  }

  ###
  ### HTTPRL test mode support.
  ###
  location ^~ /<?php print $subdir; ?>/admin/httprl-test {
    location ~* ^/<?php print $subdir; ?>/admin/httprl-test {
      access_log off;
      log_not_found off;
      set $nocache_details "Skip";
      try_files /admin/httprl-test $uri @drupal_<?php print $subdir_loc; ?>;
    }
  }

  ###
  ### CDN Far Future expiration support.
  ###
  location ^~ /<?php print $subdir; ?>/cdn/farfuture/ {
    access_log off;
    log_not_found off;
    etag off;
    gzip_http_version 1.1;
    if_modified_since exact;
    set $nocache_details "Skip";
    location ~* ^/<?php print $subdir; ?>/(cdn/farfuture/.+\.(?:css|js|jpe?g|gif|png|ico|webp|avif|bmp|svg|swf|pdf|docx?|xlsx?|pptx?|tiff?|txt|rtf|class|otf|ttf|woff2?|eot|less))$ {
      expires max;
      add_header X-Content-Type-Options "nosniff";
      add_header X-Frame-Options "SAMEORIGIN" always;
      add_header X-Header "CDN Far Future Generator 1.0";
      add_header Cache-Control "no-transform, public";
      add_header Last-Modified "Wed, 20 Jan 1988 04:20:42 GMT";
      rewrite ^/<?php print $subdir; ?>/cdn/farfuture/[^/]+/[^/]+/(.+)$ /$1 break;
      try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }
    location ~* ^/<?php print $subdir; ?>/(cdn/farfuture/) {
      expires epoch;
      add_header X-Content-Type-Options "nosniff";
      add_header X-Frame-Options "SAMEORIGIN" always;
      add_header X-Header "CDN Far Future Generator 1.1";
      add_header Cache-Control "private, must-revalidate, proxy-revalidate";
      rewrite ^/<?php print $subdir; ?>/cdn/farfuture/[^/]+/[^/]+/(.+)$ /$1 break;
      try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### If favicon else return error 204.
  ###
  location = /<?php print $subdir; ?>/favicon.ico {
    access_log off;
    log_not_found off;
    expires 30d;
    try_files /sites/<?php print $this->uri; ?>/files/favicon.ico /sites/$host/files/favicon.ico /favicon.ico $uri =204;
  }

  ###
  ### Support for http://drupal.org/project/llms_txt module
  ### and static file in the sites/domain/files directory.
  ###
  location = /<?php print $subdir; ?>/llms.txt {
    access_log off;
    log_not_found off;
    try_files /sites/<?php print $this->uri; ?>/files/$host.llms.txt /sites/<?php print $this->uri; ?>/files/llms.txt /sites/$host/files/llms.txt /llms.txt $uri @cache_<?php print $subdir_loc; ?>;
  }

  ###
  ### Support for http://drupal.org/project/robotstxt module
  ### and static file in the sites/domain/files directory.
  ###
  location = /<?php print $subdir; ?>/robots.txt {
    access_log off;
    log_not_found off;
    try_files /sites/<?php print $this->uri; ?>/files/$host.robots.txt /sites/<?php print $this->uri; ?>/files/robots.txt /sites/$host/files/robots.txt /robots.txt $uri @cache_<?php print $subdir_loc; ?>;
  }

  ###
  ### Allow local access to support wget method in Aegir settings
  ### for running sites cron.
  ###
  location = /<?php print $subdir; ?>/cron.php {

    include fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";

    # Marks the six credentials below as urlencode()d (the cloaked
    # settings.php decodes exactly this source; the CLI tier is raw).
    fastcgi_param db_creds_urlencoded 1;

    fastcgi_param db_type   <?php print urlencode($db_type); ?>;
    fastcgi_param db_name   <?php print urlencode($db_name); ?>;
    fastcgi_param db_user   <?php print implode('@', array_map('urlencode', explode('@', $db_user))); ?>;
    fastcgi_param db_passwd <?php print urlencode($db_passwd); ?>;
    fastcgi_param db_host   <?php print urlencode($db_host); ?>;
    fastcgi_param db_port   <?php print urlencode($db_port); ?>;

    fastcgi_param  HTTP_HOST           <?php print $this->uri; ?>;
    fastcgi_param  RAW_HOST            $host;
    fastcgi_param  SITE_SUBDIR         <?php print $subdir; ?>;
    fastcgi_param  MAIN_SITE_NAME      <?php print $this->uri; ?>;

    fastcgi_param  REDIRECT_STATUS     200;
    fastcgi_index  index.php;

    set $real_fastcgi_script_name cron.php;
    fastcgi_param SCRIPT_FILENAME <?php print "{$this->root}"; ?>/$real_fastcgi_script_name;

    allow 127.0.0.1;
    deny all;

    try_files /cron.php $uri =404;
    auth_basic off;
<?php if ($satellite_mode == 'boa'): ?>
    fastcgi_pass unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
    fastcgi_pass 127.0.0.1:9000;
<?php else: ?>
    fastcgi_pass unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>
  }

  ###
  ### Allow local access to support wget method in Aegir settings
  ### for running sites cron in Drupal 8+.
  ###
  location = /<?php print $subdir; ?>/cron/ {
    access_log off;
    log_not_found off;
    allow 127.0.0.1;
    deny all;
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
    auth_basic off;
  }

  ###
  ### Send search to php-fpm early so searching for node.js will work.
  ### Deny bots on search uri.
  ###
  location ^~ /<?php print $subdir; ?>/search {
    location ~* ^/<?php print $subdir; ?>/search {
      if ( $is_bot ) {
        return 444;
      }
      try_files /search $uri @drupal_<?php print $subdir_loc; ?>;
    }
  }

  ###
  ### Support for https://drupal.org/project/js module.
  ### The js.php handler exists only where the contrib js module
  ### ships it (D6/D7). Backdrop core admin_bar serves its menu at
  ### js/admin_bar/cache/* as a regular router path, so without
  ### js.php the request must go to the front controller instead.
  ###
  location ^~ /<?php print $subdir; ?>/js/ {
    location ~* ^/<?php print $subdir; ?>/js/ {
      if ( $is_bot ) {
        return 444;
      }
      error_page 418 = @drupal_<?php print $subdir_loc; ?>;
      if ( !-e $document_root/js.php ) {
        return 418;
      }
      rewrite ^/<?php print $subdir; ?>/(.*)$ /js.php?q=$1 last;
    }
  }

  ###
  ### Deny cache details display.
  ###
  location ^~ /<?php print $subdir; ?>/admin/settings/performance/cache-backend {
    access_log off;
    log_not_found off;
    return 301 $boa_visitor_scheme://$host/<?php print $subdir; ?>/admin/settings/performance;
  }

  ###
  ### Deny cache details display.
  ###
  location ^~ /<?php print $subdir; ?>/admin/config/development/performance/redis {
    access_log off;
    log_not_found off;
    return 301 $boa_visitor_scheme://$host/<?php print $subdir; ?>/admin/config/development/performance;
  }

  ###
  ### Deny cache details display.
  ###
  location ^~ /<?php print $subdir; ?>/admin/reports/redis {
    access_log off;
    log_not_found off;
    return 301 $boa_visitor_scheme://$host/<?php print $subdir; ?>/admin/reports;
  }

  ###
  ### Support for backup_migrate module download/restore/delete actions.
  ###
  location ^~ /<?php print $subdir; ?>/admin {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files /admin $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Avoid caching /civicrm*.
  ###
  location ^~ /<?php print $subdir; ?>/civicrm {
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Avoid caching /civicrm* requests
  ###
  location ^~ /<?php print $subdir; ?>/\w\w/civicrm {
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Support for audio module.
  ###
  location ^~ /<?php print $subdir; ?>/audio/download {
    location ~* ^/<?php print $subdir; ?>/(audio/download/.*/.*\.(?:mp3|mp4|m4a|ogg))$ {
      if ( $is_bot ) {
        return 444;
      }
      access_log off;
      log_not_found off;
      set $nocache_details "Skip";
      try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }
  }

  ###
  ### Deny listed requests for security reasons.
  ###
  location ~* (\.(?:git.*|htaccess|engine|config|inc|ini|info|install|make|module|profile|test|po|sh|.*sql|theme|twig|tpl(\.php)?|xtmpl|yml)(~|\.sw[op]|\.bak|\.orig|\.save)?$|^(\..*|Entries.*|Repository|Root|Tag|Template|composer\.(json|lock))$|^#.*#$|\.php(~|\.sw[op]|\.bak|\.orig\.save))$ {
    access_log off;
    log_not_found off;
    return 404;
  }

  ###
  ### Deny listed requests for security reasons.
  ###
  location ~* /(?:modules|themes|libraries)/.*\.(?:txt|md)$ {
    access_log off;
    log_not_found off;
    return 404;
  }

  ###
  ### Deny listed requests for security reasons.
  ###
  location ~* /files/civicrm/(?:ConfigAndLog|custom|upload|templates_c) {
    access_log off;
    log_not_found off;
    return 404;
  }

  ###
  ### Deny often flooded URI for performance reasons
  ###
  location = /<?php print $subdir; ?>/autodiscover/autodiscover.xml {
    access_log off;
    log_not_found off;
    return 404;
  }

  ###
  ### Responsive Images support.
  ### http://drupal.org/project/responsive_images
  ###
  location ~* ^/<?php print $subdir; ?>/.*\.r\.(?:jpe?g|png|gif) {
    if ( $http_cookie ~* "rwdimgsize=large" ) {
      rewrite ^/<?php print $subdir; ?>/(.*)/mobile/(.*)\.r(\.(?:jpe?g|png|gif))$ /<?php print $subdir; ?>/$1/desktop/$2$3 last;
    }
    rewrite ^/<?php print $subdir; ?>/(.*)\.r(\.(?:jpe?g|png|gif))$ /<?php print $subdir; ?>/$1$2 last;
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Adaptive Image Styles support.
  ### http://drupal.org/project/ais
  ###
  location ~* ^/<?php print $subdir; ?>/(?:.+)/files/(css|js|styles)/adaptive/(?:.+)$ {
    if ( $http_cookie ~* "ais=(?<ais_cookie>[a-z0-9-_]+)" ) {
      rewrite ^/<?php print $subdir; ?>/(.+)/files/(css|js|styles)/adaptive/(.+)$ /<?php print $subdir; ?>/$1/files/$2/$ais_cookie/$3 last;
    }
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files /$2 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Map /<?php print $subdir; ?>/files/ shortcut early to avoid overrides in other locations.
  ###
  location ^~ /<?php print $subdir; ?>/files/ {


    ###
    ### Sub-location to support files/styles with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/(css|js|styles)/(.*)$ {
      access_log off;
      log_not_found off;
      expires 30d;
      set $nocache_details "Skip";
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/(css|js|styles)/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    ###
    ### Sub-location to support css with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/css/(.*)$ {
      access_log off;
      log_not_found off;
      expires 30d;
      set $nocache_details "Skip";
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/css/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    ###
    ### Sub-location to support js with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/js/(.*)$ {
      access_log off;
      log_not_found off;
      expires 30d;
      set $nocache_details "Skip";
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/js/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    ###
    ### Sub-location to support files/imagecache with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/imagecache/(.*)$ {
      access_log off;
      log_not_found off;
      expires 30d;
      # fix common problems with old paths after import from standalone to Aegir multisite
      rewrite ^/<?php print $subdir; ?>/files/imagecache/(.*)/sites/default/files/(.*)$ /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/imagecache/$1/$2 last;
      rewrite ^/<?php print $subdir; ?>/files/imagecache/(.*)/files/(.*)$               /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/imagecache/$1/$2 last;
      set $nocache_details "Skip";
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/imagecache/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    location ~* ^.+\.(?:pdf|jpe?g|gif|png|ico|webp|avif|bmp|svg|swf|docx?|xlsx?|pptx?|tiff?|txt|rtf|vcard|vcf|bat|dll|class|otf|ttf|woff2?|eot|less|avi|mpe?g|mov|wmv|mp3|ogg|ogv|wav|midi|zip|tar|t?gz|rar|dmg|exe|apk|pxl|ipa|css|js|map)$ {
      expires 30d;
      access_log off;
      log_not_found off;
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files $uri =404;
    }
    try_files /$1 $uri @cache_<?php print $subdir_loc; ?>;
  }


  ###
  ### The s3/files/styles (s3fs) support.
  ###
  location ~* ^/<?php print $subdir; ?>/s3/files/(css|js|styles)/(.*)$ {
    access_log off;
    log_not_found off;
    expires 30d;
    set $nocache_details "Skip";
    try_files /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1/$2 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Imagecache and imagecache_external support.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:external|system|files/imagecache|files/(css|js|styles))/.*) {
    access_log off;
    log_not_found off;
    expires 30d;
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Deny direct access to backups.
  ###
  location ~* ^/<?php print $subdir; ?>/sites/.*/files/backup_migrate/ {
    access_log off;
    log_not_found off;
    deny all;
  }

  ###
  ### Deny direct access to config files in Drupal 8+.
  ###
  location ~* ^/<?php print $subdir; ?>/sites/.*/files/config_.* {
    access_log off;
    log_not_found off;
    deny all;
  }

  ###
  ### Private downloads are always sent to the drupal backend.
  ### Note: this location doesn't work with X-Accel-Redirect.
  ###
  location ~* ^/<?php print $subdir; ?>/(sites/.*/files/private/.*) {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    rewrite ^/<?php print $subdir; ?>/sites/.*/files/private/(.*)$ $boa_visitor_scheme://$host/<?php print $subdir; ?>/system/files/private/$1 permanent;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Header "Private Generator 1.0a";
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Deny direct access to private downloads in sites/domain/private.
  ### Note: this location works with X-Accel-Redirect.
  ###
  location ~* ^/<?php print $subdir; ?>/sites/.*/private/ {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    internal;
  }

  ###
  ### Deny direct access to private downloads also for short, rewritten URLs.
  ### Note: this location works with X-Accel-Redirect.
  ###
  location ~* /<?php print $subdir; ?>/files/private/ {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    internal;
  }

  ###
  ### Wysiwyg Fields support.
  ###
  location ~* ^/<?php print $subdir; ?>/(.*/wysiwyg_fields/(?:plugins|scripts)/.*\.(?:js|css)) {
    access_log off;
    log_not_found off;
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Advagg_css and Advagg_js support.
  ###
  location ~* ^/<?php print $subdir; ?>/(.*/files/advagg_(?:css|js).*) {
    expires max;
    access_log off;
    log_not_found off;
<?php if ($nginx_has_etag): ?>
    etag off;
<?php else: ?>
    add_header ETag "";
<?php endif; ?>
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Header "AdvAgg Generator 2.0";
    add_header Cache-Control "max-age=31449600, no-transform, public";
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Make css files compatible with boost caching.
  ###
  location ~* ^/<?php print $subdir; ?>/(.*\.css)$ {
    access_log off;
    log_not_found off;
    expires max; #if using aggregator
    try_files /cache/perm/$host${uri}_.css /$1 $uri =404;
  }

  ###
  ### Make js files compatible with boost caching.
  ###
  location ~* ^/<?php print $subdir; ?>/(.*\.(?:js|htc))$ {
    access_log off;
    log_not_found off;
    expires max; # if using aggregator
    try_files /cache/perm/$host${uri}_.js /$1 $uri =404;
  }

  ###
  ### Support for static .json files with fast 404 +Boost compatibility.
  ###
  location ~* ^/<?php print $subdir; ?>/sites/.*/files/(.*\.json)$ {
    access_log off;
    log_not_found off;
    expires max; ### if using aggregator
    try_files /cache/normal/$host${uri}_.json /$1 $uri =404;
  }

  ###
  ### Support for dynamic .json requests.
  ###
  location ~* (.*\.json)$ {
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Serve & no-log static files & images directly,
  ### without all standard drupal rewrites, php-fpm etc.
  ###
  location ~* ^/<?php print $subdir; ?>/(.+\.(?:jpe?g|gif|png|ico|webp|avif|bmp|svg|swf|pdf|docx?|xlsx?|pptx?|tiff?|txt|rtf|vcard|vcf|bat|dll|aspx?|class|otf|ttf|woff2?|eot|less))$ {
    expires 30d;
    access_log off;
    log_not_found off;
    try_files /$1 $uri =404;
  }

  ###
  ### Serve & log bigger media/static/archive files directly,
  ### without all standard drupal rewrites, php-fpm etc.
  ###
  location ~* ^/<?php print $subdir; ?>/(.+\.(?:avi|mpe?g|mov|wmv|mp3|mp4|m4a|ogg|ogv|flv|wav|midi|zip|tar|t?gz|rar|dmg|exe))$ {
    expires 30d;
    access_log off;
    log_not_found off;
    try_files /$1 $uri =404;
  }

  ###
  ### Serve & no-log some static files as is, without forcing default_type.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:cross-?domain)\.xml)$ {
    access_log off;
    log_not_found off;
    expires 30d;
    try_files /$1 $uri =404;
  }

  ###
  ### Allow some known php files (like serve.php in the ad module).
  ###
  location ~* ^/<?php print $subdir; ?>/(.*/(?:modules|libraries)/(?:contrib/)?(?:ad|tinybrowser|f?ckeditor|tinymce|wysiwyg_spellcheck|ecc|civicrm|fbconnect|radioactivity)/.*\.php)$ {

    limit_conn limreq 88;
    include fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";

    # Marks the six credentials below as urlencode()d (the cloaked
    # settings.php decodes exactly this source; the CLI tier is raw).
    fastcgi_param db_creds_urlencoded 1;

    fastcgi_param db_type   <?php print urlencode($db_type); ?>;
    fastcgi_param db_name   <?php print urlencode($db_name); ?>;
    fastcgi_param db_user   <?php print implode('@', array_map('urlencode', explode('@', $db_user))); ?>;
    fastcgi_param db_passwd <?php print urlencode($db_passwd); ?>;
    fastcgi_param db_host   <?php print urlencode($db_host); ?>;
    fastcgi_param db_port   <?php print urlencode($db_port); ?>;

    fastcgi_param  HTTP_HOST           <?php print $this->uri; ?>;
    fastcgi_param  RAW_HOST            $host;
    fastcgi_param  SITE_SUBDIR         <?php print $subdir; ?>;
    fastcgi_param  MAIN_SITE_NAME      <?php print $this->uri; ?>;

    fastcgi_param  REDIRECT_STATUS     200;
    fastcgi_index  index.php;

    set $real_fastcgi_script_name $1;
    fastcgi_param SCRIPT_FILENAME <?php print "{$this->root}"; ?>/$real_fastcgi_script_name;

    access_log off;
    log_not_found off;
    if ( $is_bot ) {
      return 444;
    }
    try_files /$1 $uri =404;
<?php if ($satellite_mode == 'boa'): ?>
    fastcgi_pass unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
    fastcgi_pass 127.0.0.1:9000;
<?php else: ?>
    fastcgi_pass unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>
  }

  ###
  ### Deny crawlers and never cache known AJAX requests.
  ###
  location ~* ^/<?php print $subdir; ?>/(.*(?:ahah|ajax|batch|autocomplete|progress/|x-progress-id|js/.*).*)$ {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Serve & no-log static helper files used in some wysiwyg editors.
  ###
  location ~* ^/<?php print $subdir; ?>/(sites/.*/(?:modules|libraries)/(?:contrib/)?(?:tinybrowser|f?ckeditor|tinymce|flowplayer|jwplayer|videomanager)/.*\.(?:html?|xml))$ {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    expires 30d;
    try_files /$1 $uri =404;
  }

  ###
  ### Serve & no-log any not specified above static files directly.
  ###
  location ~* ^/<?php print $subdir; ?>/(sites/.*/files/.*) {
    root  <?php print "{$this->root}"; ?>;
    rewrite ^/<?php print $subdir; ?>/sites/(.*)$ /sites/<?php print $this->uri; ?>/$1 last;
    access_log off;
    log_not_found off;
    expires 30d;
    try_files /$1 $uri =404;
  }

  ###
  ### Make feeds compatible with boost caching and set correct mime type.
  ###
  location ~* ^/<?php print $subdir; ?>/(.*\.xml)$ {
    if ( $request_method = POST ) {
      return 405;
    }
    if ( $cache_uid ) {
      return 405;
    }
    error_page 405 = @drupal_<?php print $subdir_loc; ?>;
    access_log off;
    log_not_found off;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Header "Boost Citrus 1.0";
    add_header Expires "Tue, 24 Jan 1984 08:00:00 GMT";
    add_header Cache-Control "must-revalidate, post-check=0, pre-check=0";
    charset utf-8;
    types { }
    default_type text/xml;
    try_files /cache/normal/$host${uri}_.xml /cache/normal/$host${uri}_.html /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Deny bots on never cached uri.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:.*/)?(?:admin|user|cart|checkout|logout|comment/reply)) {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Protect from DoS attempts on never cached uri.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:.*/)?(?:node/[0-9]+/edit|node/add)) {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Protect from DoS attempts on never cached uri.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:.*/)?(?:node/[0-9]+/delete|approve)) {
    if ($cache_uid = '') {
      return 444;
    }
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Workaround for https://www.drupal.org/node/2599326.
  ###
  if ( $args ~* "/autocomplete/" ) {
    return 405;
  }
  error_page 405 = @drupal_<?php print $subdir_loc; ?>;

  ###
  ### Catch all unspecified requests.
  ###
  location /<?php print $subdir; ?>/ {
    if ( $http_user_agent ~* wget ) {
      return 444;
    }
    try_files /$1 $uri @cache_<?php print $subdir_loc; ?>;
  }

  ###
  ### Send other known php requests/files to php-fpm without any caching.
  ###
  location ~* ^/<?php print $subdir; ?>/((core/)?(boost_stats|rtoc|js))\.php$ {

    if ( $is_bot ) {
      return 404;
    }

    limit_conn limreq 88;
    include fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";

    # Marks the six credentials below as urlencode()d (the cloaked
    # settings.php decodes exactly this source; the CLI tier is raw).
    fastcgi_param db_creds_urlencoded 1;

    fastcgi_param db_type   <?php print urlencode($db_type); ?>;
    fastcgi_param db_name   <?php print urlencode($db_name); ?>;
    fastcgi_param db_user   <?php print implode('@', array_map('urlencode', explode('@', $db_user))); ?>;
    fastcgi_param db_passwd <?php print urlencode($db_passwd); ?>;
    fastcgi_param db_host   <?php print urlencode($db_host); ?>;
    fastcgi_param db_port   <?php print urlencode($db_port); ?>;

    fastcgi_param  HTTP_HOST           <?php print $this->uri; ?>;
    fastcgi_param  RAW_HOST            $host;
    fastcgi_param  SITE_SUBDIR         <?php print $subdir; ?>;
    fastcgi_param  MAIN_SITE_NAME      <?php print $this->uri; ?>;

    fastcgi_param  REDIRECT_STATUS     200;
    fastcgi_index  index.php;

    set $real_fastcgi_script_name $1.php;
    fastcgi_param SCRIPT_FILENAME <?php print "{$this->root}"; ?>/$real_fastcgi_script_name;

    access_log off;
    log_not_found off;
    try_files /$1.php =404; ### check for existence of php file first
<?php if ($satellite_mode == 'boa'): ?>
    fastcgi_pass unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
    fastcgi_pass 127.0.0.1:9000;
<?php else: ?>
    fastcgi_pass unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>
  }

  ###
  ### Allow access to /update.php only for logged in admin user.
  ###
  location ~ ^/<?php print $subdir; ?>/(update)\.php$ {
    set $real_fastcgi_script_name $1.php;
    error_page 418 = @allowupdate_<?php print $subdir_loc; ?>;
    if ( $cache_uid ) {
      return 418;
    }
    return 404;
  }

  ###
  ### Allow access to /authorize.php only for logged in admin user.
  ###
  location ~ ^/<?php print $subdir; ?>/(authorize)\.php$ {
    set $real_fastcgi_script_name $1.php;
    error_page 418 = @allowauthorize_<?php print $subdir_loc; ?>;
    if ( $cache_uid ) {
      return 418;
    }
    return 404;
  }

  ###
  ### Send all non-static requests to php-fpm, restricted to known php file.
  ###
  location = /<?php print $subdir; ?>/index.php {

    limit_conn limreq 888;
    add_header X-Device "$device";
    add_header X-GeoIP-Country-Code "$geoip_country_code";
    add_header X-GeoIP-Country-Name "$geoip_country_name";
    add_header X-Speed-Cache "$upstream_cache_status";
    add_header X-Speed-Cache-UID "$debug_session_flag";
    add_header X-Speed-Cache-Key "$key_uri";
    add_header X-NoCache "$nocache_details";
    add_header X-This-Proto "$http_x_forwarded_proto";
    add_header X-Server-Sub-Name "<?php print $this->uri; ?>";
    add_header X-Response-Status "$status";

    root  <?php print "{$this->root}"; ?>;

    include fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";

    # Marks the six credentials below as urlencode()d (the cloaked
    # settings.php decodes exactly this source; the CLI tier is raw).
    fastcgi_param db_creds_urlencoded 1;

    fastcgi_param db_type   <?php print urlencode($db_type); ?>;
    fastcgi_param db_name   <?php print urlencode($db_name); ?>;
    fastcgi_param db_user   <?php print implode('@', array_map('urlencode', explode('@', $db_user))); ?>;
    fastcgi_param db_passwd <?php print urlencode($db_passwd); ?>;
    fastcgi_param db_host   <?php print urlencode($db_host); ?>;
    fastcgi_param db_port   <?php print urlencode($db_port); ?>;

    fastcgi_param  HTTP_HOST           $host;
    fastcgi_param  RAW_HOST            $host;
    fastcgi_param  SITE_SUBDIR         <?php print $subdir; ?>;
    fastcgi_param  SCRIPT_URL          /<?php print $subdir; ?>/;
    fastcgi_param  SCRIPT_URI          $scheme://$host/<?php print $subdir; ?>/;
    fastcgi_param  MAIN_SITE_NAME      <?php print $this->uri; ?>;

    fastcgi_param  REDIRECT_STATUS     200;
    fastcgi_index  index.php;

    set $real_fastcgi_script_name index.php;
    fastcgi_param  SCRIPT_FILENAME     <?php print "{$this->root}"; ?>/$real_fastcgi_script_name;
    fastcgi_param  SCRIPT_NAME         /<?php print $subdir; ?>/$real_fastcgi_script_name;
    fastcgi_param  PHP_SELF            /<?php print $subdir; ?>/$real_fastcgi_script_name;

    ###
    ### Detect supported no-cache exceptions
    ###
    if ( $request_method = POST ) {
      set $nocache_details "Method";
    }
    if ( $args ~* "nocache=1" ) {
      set $nocache_details "Args";
    }
    if ( $http_cookie ~* "NoCacheID" ) {
      set $nocache_details "AegirCookie";
    }
    ###
    ### The debug headers report THAT a session cookie or an Authorization
    ### header was sent, never its value: a response header is readable by
    ### same-origin script, the HttpOnly session cookie is not.
    ###
    set $debug_session_flag "";
    set $debug_auth_flag "";
    if ( $cache_uid ) {
      set $nocache_details "DrupalCookie";
      set $debug_session_flag "Session";
    }
    if ( $http_authorization ) {
      set $debug_auth_flag "Present";
    }
    ###
    ### Uptime monitors always reach the backend: never served from the cache
    ### nor stored in it, so a monitor never reads a crawler's cached copy and
    ### a backend that is down is never hidden behind a stale one.
    ###
    if ( $http_user_agent ~* (?:Pingdom|UptimeRobot) ) {
      set $nocache_details "Monitor";
    }
    ###
    ### Use Nginx cache for all visitors by default.
    ###
    set $nocache "";
    if ( $nocache_details ~ (?:AegirCookie|Args|Skip|Monitor) ) {
      set $nocache "NoCache";
    }

    ###
    ### Basic security/privacy headers.
    ###
    add_header Referrer-Policy "no-referrer-when-downgrade";

    ###
    ### Add headers for debugging
    ###
    add_header X-Debug-NoCache-Switch "$nocache";
    add_header X-Debug-NoCache-Auth "$debug_auth_flag";
    add_header X-Debug-NoCache-Cookie "$cookie_NoCacheID";

    add_header Cache-Control "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";

    try_files /index.php =404; ### check for existence of php file first

<?php if ($satellite_mode == 'boa'): ?>
    fastcgi_pass  unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
    fastcgi_pass  127.0.0.1:9000;
<?php else: ?>
    fastcgi_pass unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>

    fastcgi_cache speed;
    fastcgi_cache_methods GET HEAD; ### Nginx default, but added for clarity
    fastcgi_cache_min_uses 1;
    fastcgi_cache_key "$scheme$is_bot$device$host$request_method$key_uri$cache_uid$http_x_forwarded_proto$sent_http_x_local_proto$cookie_respimg";
    fastcgi_cache_valid 200 10s;
    fastcgi_cache_valid 301 302 403 404 1s;
    fastcgi_cache_valid any 1s;
    fastcgi_cache_lock on;
    fastcgi_ignore_headers Cache-Control Expires Vary;
    fastcgi_pass_header Set-Cookie;
    fastcgi_pass_header X-Accel-Expires;
    fastcgi_pass_header X-Accel-Redirect;
    ###
    ### A response that sends X-Force-Nocache (YES; any value but 0) is not
    ### stored. The test belongs here: fastcgi_no_cache is read once the
    ### response headers exist, while an if in the location runs before them.
    ###
    fastcgi_no_cache $cookie_NoCacheID $http_authorization $nocache $upstream_http_x_force_nocache;
    fastcgi_cache_bypass $cookie_NoCacheID $http_authorization $nocache;
    fastcgi_cache_use_stale error http_500 invalid_header timeout updating;
  }

  ###
  ### Deny access to any not listed above php files with 404 error.
  ###
  location ~* ^.+\.php$ {
    return 404;
  }

}
###
### Master location for subdir support (end)
###


###
### Boost compatible cache check.
###
location @cache_<?php print $subdir_loc; ?> {
  if ( $request_method = POST ) {
    set $nocache_details "Method";
    return 405;
  }
  if ( $args ~* "nocache=1" ) {
    set $nocache_details "Args";
    return 405;
  }
  if ( $http_cookie ~* "NoCacheID" ) {
    set $nocache_details "AegirCookie";
    return 405;
  }
  if ( $cache_uid ) {
    set $nocache_details "DrupalCookie";
    return 405;
  }
  if ( $http_user_agent ~* (?:Pingdom|UptimeRobot) ) {
    set $nocache_details "Monitor";
    return 405;
  }
  error_page 405 = @drupal_<?php print $subdir_loc; ?>;
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
  add_header X-Header "Boost Citrus 1.0";
  add_header Expires "Tue, 24 Jan 1984 08:00:00 GMT";
  add_header Cache-Control "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";
  charset utf-8;
  try_files /cache/normal/$host${uri}_$args.html @drupal_<?php print $subdir_loc; ?>;
}

###
### Send all not cached requests to drupal with clean URLs support.
###
location @drupal_<?php print $subdir_loc; ?> {
  set $core_detected "Legacy";
  ###
  ### For Drupal >= 7
  ###
  if ( -e $document_root/web.config ) {
    set $core_detected "Regular";
  }
  if ( -e $document_root/core ) {
    set $core_detected "Modern";
  }
  error_page 418 = @modern_<?php print $subdir_loc; ?>;
  if ( $core_detected ~ (?:NotForD7|Modern) ) {
    return 418;
  }
  ###
  ### For Drupal 6
  ###
  rewrite ^/<?php print $subdir; ?>/(.*)$  /<?php print $subdir; ?>/index.php?q=$1 last;
}

###
### Special location for Drupal 7+.
###
location @modern_<?php print $subdir_loc; ?> {
  try_files $uri /<?php print $subdir; ?>/index.php?$query_string;
}

###
### Internal location for /update.php restricted access.
###
location @allowupdate_<?php print $subdir_loc; ?> {

  limit_conn limreq 8;
  include fastcgi_params;

  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";

  # Marks the six credentials below as urlencode()d (the cloaked settings.php
  # decodes exactly this source; the CLI tier is raw).
  fastcgi_param db_creds_urlencoded 1;

  fastcgi_param db_type   <?php print urlencode($db_type); ?>;
  fastcgi_param db_name   <?php print urlencode($db_name); ?>;
  fastcgi_param db_user   <?php print implode('@', array_map('urlencode', explode('@', $db_user))); ?>;
  fastcgi_param db_passwd <?php print urlencode($db_passwd); ?>;
  fastcgi_param db_host   <?php print urlencode($db_host); ?>;
  fastcgi_param db_port   <?php print urlencode($db_port); ?>;

  fastcgi_param  HTTP_HOST           <?php print $this->uri; ?>;
  fastcgi_param  RAW_HOST            $host;
  fastcgi_param  SITE_SUBDIR         <?php print $subdir; ?>;
  fastcgi_param  MAIN_SITE_NAME      <?php print $this->uri; ?>;

  fastcgi_param  REDIRECT_STATUS     200;

  fastcgi_param SCRIPT_FILENAME <?php print "{$this->root}"; ?>/$real_fastcgi_script_name;

  fastcgi_split_path_info ^(.+\.php)(/.+)$;
  fastcgi_index update.php;
  fastcgi_intercept_errors on;

<?php if ($satellite_mode == 'boa'): ?>
  fastcgi_pass unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
  fastcgi_pass 127.0.0.1:9000;
<?php else: ?>
  fastcgi_pass unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>
}

###
### Internal location for /authorize.php restricted access.
###
location @allowauthorize_<?php print $subdir_loc; ?> {

  limit_conn limreq 8;
  include fastcgi_params;

  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";

  # Marks the six credentials below as urlencode()d (the cloaked settings.php
  # decodes exactly this source; the CLI tier is raw).
  fastcgi_param db_creds_urlencoded 1;

  fastcgi_param db_type   <?php print urlencode($db_type); ?>;
  fastcgi_param db_name   <?php print urlencode($db_name); ?>;
  fastcgi_param db_user   <?php print implode('@', array_map('urlencode', explode('@', $db_user))); ?>;
  fastcgi_param db_passwd <?php print urlencode($db_passwd); ?>;
  fastcgi_param db_host   <?php print urlencode($db_host); ?>;
  fastcgi_param db_port   <?php print urlencode($db_port); ?>;

  fastcgi_param  HTTP_HOST           <?php print $this->uri; ?>;
  fastcgi_param  RAW_HOST            $host;
  fastcgi_param  SITE_SUBDIR         <?php print $subdir; ?>;
  fastcgi_param  MAIN_SITE_NAME      <?php print $this->uri; ?>;

  fastcgi_param  REDIRECT_STATUS     200;

  fastcgi_param SCRIPT_FILENAME <?php print "{$this->root}"; ?>/$real_fastcgi_script_name;

  fastcgi_split_path_info ^(.+\.php)(/.+)$;
  fastcgi_index authorize.php;
  fastcgi_intercept_errors on;

<?php if ($satellite_mode == 'boa'): ?>
  fastcgi_pass unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
  fastcgi_pass 127.0.0.1:9000;
<?php else: ?>
  fastcgi_pass unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>
}


#######################################################
###  nginx.conf site level extended vhost include end
#######################################################
