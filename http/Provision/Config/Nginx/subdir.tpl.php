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
$subdir_re = preg_quote($subdir);
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
### as relative-URL junk, and skips a two-letter site name as a language
### prefix. Under /<?php print $subdir; ?>/ the map's verdict is replaced by its three rules
### counted from this site's own root. The parent's vhost includes this file
### before its shared include, and a domain that is no site tests the flag
### after its subdirectory confs, so the guard there reads this verdict.
###
if ($uri ~* "^/<?php print $subdir_re; ?>/") {
  set $is_static_chain 0;
}
if ($uri ~* "^/<?php print $subdir_re; ?>/(?![a-z]{2}/)(?!(?:sites|modules|misc|themes|core|libraries|profiles|cdn|files|system|external|s3)/)[^?]+/(?:(?:sites/(?:all|default)/(?:modules|themes|libraries)|ui/external)/[^?]+\.(?:css|js|htc|png|gif|jpe?g|svg|ico|webp|avif|bmp|woff2?|ttf|otf|eot|less|map)|system\.(?:base|menus|messages|theme)\.css|(?:node|user|field|search|filter|comment|book|forum|poll|taxonomy|dblog)\.css|drupal\.js|jquery\.once\.js|ajax\.js|batch\.js|tabledrag\.js|tableselect\.js|states\.js|progress\.js|form\.js|collapse\.js|autocomplete\.js|machine-name\.js|textarea\.js|vertical-tabs\.js)$") {
  set $is_static_chain 1;
}
if ($uri ~* "^/<?php print $subdir_re; ?>/[^?]*/(?:sites/all/(?:modules|themes)|modules/(?:system|field|user|node|filter|search))/[^?]+/(?:sites/all/(?:modules|themes)|modules/(?:system|field|user|node|filter|search))/[^?]+\.(?:css|js|htc|png|gif|jpe?g|svg|ico|webp|avif|bmp|woff2?|ttf|otf|eot|less|map)$") {
  set $is_static_chain 1;
}

###
### The per-site PHP-FPM pins (fpm_include_site_*) key on $main_site_name,
### which the parent's vhost sets to its own name. Under /<?php print $subdir; ?>/ it names this
### site, before the pins are read, so this site runs on its own pinned PHP
### version, or on the default one, never on the parent's.
###
if ($uri ~ "^/<?php print $subdir_re; ?>/") {
  set $main_site_name "<?php print $this->uri; ?>";
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
### effective site-relative threshold of 3. The same 4 prefixes counted from
### this site's own root are dropped by the second test.
###
if ($is_lang_chain) {
  return 404;
}
if ($uri ~* "^/<?php print $subdir_re; ?>/(?:[a-z][a-z](?:-[a-z0-9]+)?/){4}") {
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
# of a domain that is no site tests both after its subdirectory confs.

###
### The Referer-less /print*, Flag toggle and cold HybridAuth window floods
### (see the $block_*_no_referer maps in server.tpl.php), counted from this
### site's own root: the maps anchor their paths at the domain's root. 404,
### as the shared include answers them. Every subdirectory conf of a domain
### sets this variable and tests it at once, so none reads another's value.
###
set $boa_subdir_flood "$request_method:$has_no_referrer$has_no_session:$uri";
if ($boa_subdir_flood ~* "^[A-Z]+:1.:/<?php print $subdir_re; ?>/(?:[a-z]{2}/)?(?:printmail/[0-9]|printpdf/[0-9]|printer/[0-9]|print/(?:[0-9]|pdf/|epub/|png/|html/|word_docx/)|printable/(?:print|pdf)/)") {
  return 404;
}
if ($boa_subdir_flood ~* "^(?:GET|HEAD):1.:/<?php print $subdir_re; ?>/(?:[a-z]{2}(?:-[a-z]+)?/)?flag/(?:flag|unflag)/[a-z0-9_]+/[0-9]") {
  return 404;
}
if ($boa_subdir_flood ~* "^(?:GET|HEAD):11:/<?php print $subdir_re; ?>/(?:[a-z]{2}(?:-[a-z]+)?/)?hybridauth/window/[a-z0-9_.-]+/?$") {
  return 404;
}

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
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }
  location ~* ^/<?php print $subdir; ?>/sites/default/files/(css|js|styles) {
    access_log off;
    log_not_found off;
    expires 30d;
    set $nocache_details "Skip";
    rewrite ^/<?php print $subdir; ?>/sites/default/files/(css|js|styles)/(.*)$ /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1/$2 last;
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }
  location ~* ^/<?php print $subdir; ?>/sites/default/files {
    access_log off;
    log_not_found off;
    expires 30d;
    rewrite ^/<?php print $subdir; ?>/sites/default/files/(.*)$ /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
    try_files $uri =404;
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
  return 301 /<?php print $subdir; ?>/$is_args$args;
}

###
### Master location for subdir support (start)
###
location ^~ /<?php print $subdir; ?>/ {

  root  <?php print "{$this->root}"; ?>;

  ###
  ### Every request under /<?php print $subdir; ?>/ ends in one of the nested locations below
  ### (the catch-all takes this location's own prefix), and nginx runs the
  ### set, if and return lines of the location a request ends in only. The
  ### guards run at server level instead: from the shared include in a site's
  ### vhost, or from subdir_vhost.tpl.php for a domain that is no site. What
  ### the nested locations inherit from here is root, add_header and
  ### error_page.
  ###

  ###
  ### Add recommended HTTP headers
  ### Note: any location with its own add_header directives cancels ALL
  ### inherited add_header lines, so this pair is re-stated verbatim in
  ### every such static-serving location below. Do not deduplicate.
  ###
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
<?php if ($nginx_has_http3): ?>
  add_header Alt-Svc 'h3=":443"; ma=86400';
<?php endif; ?>

  ###
  ### A 405 returned by a nested location goes to this site's Drupal.
  ###
  error_page 405 = @drupal_<?php print $subdir_loc; ?>;

  ###
  ### HTTPRL standard support.
  ###
  location ^~ /<?php print $subdir; ?>/httprl_async_function_callback {
    location ~* ^/<?php print $subdir; ?>/httprl_async_function_callback {
      access_log off;
      log_not_found off;
      set $nocache_details "Skip";
      try_files $uri @drupal_<?php print $subdir_loc; ?>;
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
      try_files $uri @drupal_<?php print $subdir_loc; ?>;
    }
  }

<?php
// The bgp_flood zone is declared in the BOA http-scope zones file read at the
// amp-chain gate above; its consumers render only when that file declares it.
if (!isset($boa_zones_body)) {
  $boa_zones_file = '/etc/nginx/conf.d/limit-req-zones-boa.conf';
  $boa_zones_body = @is_file($boa_zones_file)
    ? (string) @file_get_contents($boa_zones_file)
    : '';
}
if (strpos($boa_zones_body, 'zone=bgp_flood') !== FALSE):
?>
  ###
  ### Background process/batch self-request storm guard, as in the shared
  ### include: every legitimate request here is a POST from the site itself
  ### to bgp-start/<handle>/<token>, anything else under the prefix is shed.
  ### Access logging stays on, as there. The batch_guard monitor counts
  ### domain-root /bgp-start/ lines only, so a storm here is capped, not healed.
  ###
  location ^~ /<?php print $subdir; ?>/bgp-start/ {
    location ~* ^/<?php print $subdir; ?>/bgp-start/[^/]+/[^/]+$ {
      if ( $is_bot ) {
        return 444;
      }
      limit_req zone=bgp_flood burst=50 nodelay;
      limit_req_status 444;
      set $nocache_details "Skip";
      try_files $uri @drupal_<?php print $subdir_loc; ?>;
    }
    return 444;
  }

  ###
  ### Language-prefix sibling.
  ###
  location ~* ^/<?php print $subdir; ?>/\w\w/bgp-start/[^/]+/[^/]+$ {
    if ( $is_bot ) {
      return 444;
    }
    limit_req zone=bgp_flood burst=50 nodelay;
    limit_req_status 444;
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }
<?php endif; ?>

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
    location ~* ^/<?php print $subdir; ?>/cdn/farfuture/[^/]+/[^/]+/(?:CHANGELOG\.txt$|(?:.*/)?\.|sites/[^/]+/(?:files/)?private/|sites/.*/files/(?:backup_migrate/|config_|civicrm/(?:ConfigAndLog|custom|upload|templates_c))|(?:.*/)?vendor/composer/|(?:.*/)?composer\.(?:json|lock)$|(?:.*/)?(?:modules|themes|libraries)/.*\.(?:txt|md)$|.*\.(?:php|engine|config|inc|ini|info|install|make|module|profile|test|po|sh|[a-z]*sql|theme|twig|tpl|xtmpl|yml)(?:~|\.sw[op]|\.bak|\.orig|\.save)?$) {
      return 404;
    }
    location ~* ^/<?php print $subdir; ?>/cdn/farfuture/.+\.(?:css|js|jpe?g|gif|png|ico|webp|avif|bmp|svg|swf|pdf|docx?|xlsx?|pptx?|tiff?|txt|rtf|class|otf|ttf|woff2?|eot|less)$ {
      expires max;
      add_header X-Content-Type-Options "nosniff";
      add_header X-Frame-Options "SAMEORIGIN" always;
      add_header X-Header "CDN Far Future Generator 1.0";
      add_header Cache-Control "no-transform, public";
      add_header Last-Modified "Wed, 20 Jan 1988 04:20:42 GMT";
      rewrite ^/<?php print $subdir; ?>/cdn/farfuture/[^/]+/[^/]+/(.+)$ /$1 break;
      try_files $uri @drupal_<?php print $subdir_loc; ?>;
    }
    location ~* ^/<?php print $subdir; ?>/cdn/farfuture/ {
      expires epoch;
      add_header X-Content-Type-Options "nosniff";
      add_header X-Frame-Options "SAMEORIGIN" always;
      add_header X-Header "CDN Far Future Generator 1.1";
      add_header Cache-Control "private, must-revalidate, proxy-revalidate";
      rewrite ^/<?php print $subdir; ?>/cdn/farfuture/[^/]+/[^/]+/(.+)$ /$1 break;
      try_files $uri @drupal_<?php print $subdir_loc; ?>;
    }
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
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
    # Read by the proxied-https shim in settings.php; an older
    # fastcgi_params lacks it.
    fastcgi_param REQUEST_SCHEME $scheme;

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
  ### for running sites cron in Drupal 8+ with auth_basic disabled on the fly.
  ### Note that this works only for auth_basic enabled in Aegir
  ### on the Nginx level, not for modules on the PHP level.
  ###
  location ^~ /<?php print $subdir; ?>/cron/ {
    allow 127.0.0.1;
    deny all;
    auth_basic off;
    try_files "" @cron_modern_<?php print $subdir_loc; ?>;
  }

  ###
  ### Allow local access to support wget method in Aegir settings
  ### for running sites cron on Backdrop (served by core/cron.php,
  ### with the key validated in the query string).
  ###
  location = /<?php print $subdir; ?>/core/cron.php {

    include fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";
    # Read by the proxied-https shim in settings.php; an older
    # fastcgi_params lacks it.
    fastcgi_param REQUEST_SCHEME $scheme;

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

    set $real_fastcgi_script_name core/cron.php;
    fastcgi_param SCRIPT_FILENAME <?php print "{$this->root}"; ?>/$real_fastcgi_script_name;

    allow 127.0.0.1;
    deny all;

    try_files /core/cron.php =404;
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
  ### Send search to php-fpm early so searching for node.js will work.
  ### Deny bots on search uri. The three guards of the shared include, in
  ### order: search params with no referrer, 6+ facets behind a faked
  ### referrer, then the per-IP (search_limit) and per-vhost (search_flood)
  ### rate caps.
  ###
  location ^~ /<?php print $subdir; ?>/search {
    location ~* ^/<?php print $subdir; ?>/search {
      if ( $block_search_no_referrer ) {
        return 444;
      }
      if ( $block_search_root_referer ) {
        return 444;
      }
      if ( $has_excessive_facets ) {
        return 444;
      }
      if ( $block_stale_chrome_search ) {
        return 444;
      }
      if ( $is_catalina_stale_chrome ) {
        return 444;
      }
      if ( $is_bot ) {
        return 444;
      }
      limit_req zone=search_limit burst=5  nodelay;
      limit_req zone=search_flood burst=40 nodelay;
      limit_req_status 444;
      try_files $uri @drupal_<?php print $subdir_loc; ?>;
    }
  }

  ###
  ### Same three-tier search protection for language-prefixed paths (/xx/search).
  ###
  location ~* ^/<?php print $subdir; ?>/[a-z][a-z]/search {
    if ( $block_search_no_referrer ) {
      return 444;
    }
    if ( $block_search_root_referer ) {
      return 444;
    }
    if ( $has_excessive_facets ) {
      return 444;
    }
    if ( $block_stale_chrome_search ) {
      return 444;
    }
    if ( $is_catalina_stale_chrome ) {
      return 444;
    }
    if ( $is_bot ) {
      return 444;
    }
    limit_req zone=search_limit burst=5  nodelay;
    limit_req zone=search_flood burst=40 nodelay;
    limit_req_status 444;
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Block search-destination abuse via Drupal's login redirect mechanism,
  ### as the shared include does: the /search guards never run for a
  ### /user/login?destination=search... request. "Skip" keeps the login form
  ### out of Speed Booster.
  ###
  location ^~ /<?php print $subdir; ?>/user/login {
    if ( $is_bot ) {
      return 444;
    }
    if ( $block_login_search_destination ) {
      return 444;
    }
    if ( $block_search_root_referer ) {
      return 444;
    }
    if ( $has_excessive_facets ) {
      return 444;
    }
    set $nocache_details "Skip";
    limit_req zone=search_flood burst=40 nodelay;
    limit_req_status 444;
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
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
      rewrite ^/<?php print $subdir; ?>/(.*)$ /<?php print $subdir; ?>/js.php?q=$1 last;
    }
  }

  ###
  ### Deny cache details display.
  ###
  location ^~ /<?php print $subdir; ?>/admin/settings/performance/cache-backend {
    if ($cache_uid = '') {
      return 403;
    }
    if ( $is_bot ) {
      return 444;
    }
    return 301 $boa_visitor_scheme://$host/<?php print $subdir; ?>/admin/settings/performance;
  }

  ###
  ### Deny cache details display.
  ###
  location ^~ /<?php print $subdir; ?>/admin/config/development/performance/redis {
    if ($cache_uid = '') {
      return 403;
    }
    if ( $is_bot ) {
      return 444;
    }
    return 301 $boa_visitor_scheme://$host/<?php print $subdir; ?>/admin/config/development/performance;
  }

  ###
  ### Deny cache details display.
  ###
  location ^~ /<?php print $subdir; ?>/admin/reports/redis {
    if ($cache_uid = '') {
      return 403;
    }
    if ( $is_bot ) {
      return 444;
    }
    return 301 $boa_visitor_scheme://$host/<?php print $subdir; ?>/admin/reports;
  }

  ###
  ### Support for backup_migrate module download/restore/delete actions.
  ###
  location ^~ /<?php print $subdir; ?>/admin {
    if ($cache_uid = '') {
      return 403;
    }
    if ( $is_bot ) {
      return 444;
    }
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Don't log and avoid caching /civicrm* requests.
  ###
  location ^~ /<?php print $subdir; ?>/civicrm {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Avoid caching /civicrm* requests
  ###
  location ~* ^/<?php print $subdir; ?>/\w\w/civicrm {
    if ( $is_bot ) {
      return 444;
    }
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
  location ~* (\.(?:git.*|htaccess|engine|config|inc|ini|info|install|make|module|profile|test|po|sh|.*sql|theme|twig|tpl(\.php)?|xtmpl|yml)(~|\.sw[op]|\.bak|\.orig|\.save)?$|^(\..*|Entries.*|Repository|Root|Tag|Template|composer\.(json|lock))$|^#.*#$|\.php(~|\.sw[op]|\.bak|\.orig|\.save))$ {
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
  ### Deny direct access to backups.
  ###
  location ~* ^/<?php print $subdir; ?>/sites/.*/files/backup_migrate/ {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    deny all;
  }

  ###
  ### Deny direct access to config files in Drupal 8+.
  ###
  location ~* ^/<?php print $subdir; ?>/sites/.*/files/config_.* {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    deny all;
  }

  ###
  ### Private downloads are always sent to the drupal backend.
  ### Note: this location doesn't work with X-Accel-Redirect.
  ###
  location ~* ^/<?php print $subdir; ?>/(sites/[^/]+/files/private/.*) {
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
  location ~* ^/<?php print $subdir; ?>/sites/[^/]+/private/ {
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
  ### No PHP source from a files directory, whatever location would take it.
  ###
  location ~* ^/<?php print $subdir; ?>/sites/[^/]+/files/.+\.php$ {
    access_log off;
    log_not_found off;
    return 404;
  }

  ###
  ### [Option] Deny public access to webform uploaded files
  ### for privacy reasons and to prevent phishing attacks.
  ### The files uploaded should be available only via SFTP.
  ###
  location ~* ^/<?php print $subdir; ?>/(sites/[^/]+/files/webform/.*)$ {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    expires 99s;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header Cache-Control "public, must-revalidate, proxy-revalidate";
    try_files /$1 =404;
    ### to deny the access replace the last line with:
    ### return 404;
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
  ### Deny some not supported URI like cgi-bin on the Nginx level.
  ###
  location ~* (?:cgi-bin|vti-bin) {
    access_log off;
    log_not_found off;
    return 404;
  }

  ###
  ### Deny bots on some weak modules uri. An existing file is served, as
  ### the shared include serves it.
  ###
  location ~* ^/<?php print $subdir; ?>/(.*(?:validation|aggregator|vote_up_down|captcha|vbulletin|glossary/|flag/flag).*)$ {
    location ~* \.php$ {
      return 404;
    }
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    try_files /$1 @drupal_<?php print $subdir_loc; ?>;
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
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
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
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### The files/styles support: an image style derivative or a CSS/JS
  ### aggregate that is not there yet is generated by Drupal.
  ###
  location ~* ^/<?php print $subdir; ?>/(?:.+/)?sites/.*/files/(css|js|styles)/(.*)$ {
    location ~* \.php$ {
      return 404;
    }
    access_log off;
    log_not_found off;
    expires max;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header Cache-Control "public";
    try_files /sites/<?php print $this->uri; ?>/files/$1/$2 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### The files/imagecache support. A named capture, since a rewrite runs
  ### before the try_files.
  ###
  location ~* ^/<?php print $subdir; ?>/(?:.+/)?sites/.*/files/imagecache/(?<sd_ic>.*)$ {
    location ~* \.php$ {
      return 404;
    }
    access_log off;
    log_not_found off;
    expires max;
    # fix common problems with old paths after import from standalone to Aegir multisite
    rewrite ^/<?php print $subdir; ?>/sites/(.*)/files/imagecache/(.*)/sites/default/files/(.*)$ /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/imagecache/$2/$3 last;
    rewrite ^/<?php print $subdir; ?>/sites/(.*)/files/imagecache/(.*)/files/(.*)$               /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/imagecache/$2/$3 last;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header Cache-Control "public";
    try_files /sites/<?php print $this->uri; ?>/files/imagecache/$sd_ic $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Map /<?php print $subdir; ?>/files/ shortcut early to avoid overrides in other locations.
  ###
  location ^~ /<?php print $subdir; ?>/files/ {

    ###
    ### Sub-location to support Flash Video (FLV) files with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/.+\.flv$ {
      flv;
      expires 30d;
      access_log off;
      log_not_found off;
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files $uri =404;
    }

    ###
    ### Sub-location to support H.264/AAC files with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/.+\.(?:mp4|m4a)$ {
      mp4;
      mp4_buffer_size 1m;
      mp4_max_buffer_size 5m;
      expires 30d;
      access_log off;
      log_not_found off;
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files $uri =404;
    }

    ###
    ### Sub-location to support files/css with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/css/(.*)$ {
      access_log off;
      log_not_found off;
      expires max;
      add_header X-Content-Type-Options "nosniff";
      add_header X-Frame-Options "SAMEORIGIN" always;
      add_header Cache-Control "public";
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files /sites/<?php print $this->uri; ?>/files/css/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    ###
    ### Sub-location to support files/js with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/js/(.*)$ {
      access_log off;
      log_not_found off;
      expires max;
      add_header X-Content-Type-Options "nosniff";
      add_header X-Frame-Options "SAMEORIGIN" always;
      add_header Cache-Control "public";
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files /sites/<?php print $this->uri; ?>/files/js/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    ###
    ### Sub-location to support files/styles with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/(css|js|styles)/(.*)$ {
      access_log off;
      log_not_found off;
      expires max;
      add_header X-Content-Type-Options "nosniff";
      add_header X-Frame-Options "SAMEORIGIN" always;
      add_header Cache-Control "public";
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files /sites/<?php print $this->uri; ?>/files/$1/$2 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    ###
    ### Sub-location to support files/imagecache with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/imagecache/(.*)$ {
      access_log off;
      log_not_found off;
      expires max;
      # fix common problems with old paths after import from standalone to Aegir multisite
      rewrite ^/<?php print $subdir; ?>/files/imagecache/(.*)/sites/default/files/(.*)$ /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/imagecache/$1/$2 last;
      rewrite ^/<?php print $subdir; ?>/files/imagecache/(.*)/files/(.*)$               /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/imagecache/$1/$2 last;
      add_header X-Content-Type-Options "nosniff";
      add_header X-Frame-Options "SAMEORIGIN" always;
      add_header Cache-Control "public";
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files /sites/<?php print $this->uri; ?>/files/imagecache/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    location ~* ^.+\.(?:pdf|jpe?g|gif|png|ico|webp|avif|bmp|svg|swf|docx?|xlsx?|pptx?|tiff?|txt|rtf|vcard|vcf|bat|dll|class|otf|ttf|woff2?|eot|less|avi|mpe?g|mov|wmv|mp3|ogg|ogv|wav|midi|zip|tar|t?gz|rar|dmg|exe|apk|pxl|ipa|css|js|map)$ {
      expires 30d;
      access_log off;
      log_not_found off;
      rewrite ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
      try_files $uri =404;
    }
    try_files $uri @cache_<?php print $subdir_loc; ?>;
  }

  ###
  ### Map /<?php print $subdir; ?>/downloads/ shortcut early to avoid overrides in other locations.
  ###
  location ^~ /<?php print $subdir; ?>/downloads/ {
    location ~* ^.+\.(?:pdf|jpe?g|gif|png|ico|webp|avif|bmp|svg|swf|docx?|xlsx?|pptx?|tiff?|txt|rtf|vcard|vcf|bat|dll|class|otf|ttf|woff2?|eot|less|avi|mpe?g|mov|wmv|mp3|ogg|ogv|wav|midi|zip|tar|t?gz|rar|dmg|exe|apk|pxl|ipa|map)$ {
      expires 30d;
      access_log off;
      log_not_found off;
      rewrite ^/<?php print $subdir; ?>/downloads/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/downloads/$1 last;
      try_files $uri =404;
    }
    try_files $uri @cache_<?php print $subdir_loc; ?>;
  }


  ###
  ### The s3/files/styles (s3fs) support.
  ###
  location ~* ^/<?php print $subdir; ?>/(?:.+/)?s3/files/(css|js|styles)/(.*)$ {
    location ~* \.php$ {
      return 404;
    }
    access_log off;
    log_not_found off;
    expires max;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header Cache-Control "public";
    try_files /sites/<?php print $this->uri; ?>/files/$1/$2 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Send requests with /external/ and /system/ URI keywords to @drupal,
  ### at any depth, as the shared include does. An existing file there, core
  ### CSS under modules/system/ for one, is served.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:.*/)?(?:external|system)/.*)$ {
    location ~* \.php$ {
      set $nocache_details "Skip";
      try_files $uri @drupal_<?php print $subdir_loc; ?>;
    }
    access_log off;
    log_not_found off;
    expires 30d;
    set $nocache_details "Skip";
    try_files /$1 @drupal_<?php print $subdir_loc; ?>;
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
  location ~* ^/<?php print $subdir; ?>/(.*/files/advagg_(?:css|js).*)$ {
    location ~* \.php$ {
      return 404;
    }
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
  ### Support for dynamic /sw.js requests. See #2982073 on drupal.org
  ###
  location = /<?php print $subdir; ?>/sw.js {
    try_files /sw.js @drupal_<?php print $subdir_loc; ?>;
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
  ### Deny listed requests for security reasons.
  ###
  location ~* /.*composer\.(json|lock)$ {
    access_log off;
    log_not_found off;
    return 404;
  }
  location ^~ /<?php print $subdir; ?>/vendor/composer/ {
    access_log off;
    log_not_found off;
    return 404;
  }
  location = /<?php print $subdir; ?>/CHANGELOG.txt {
    access_log off;
    log_not_found off;
    return 404;
  }

  ###
  ### Support for static .json files with fast 404 +Boost compatibility.
  ###
  location ~* ^/<?php print $subdir; ?>/(sites/.*/files/.*\.json)$ {
    access_log off;
    log_not_found off;
    expires max; ### if using aggregator
    try_files /cache/normal/$host${uri}_.json /$1 =404;
  }

  ###
  ### Support for dynamic .json requests.
  ###
  location ~* ^/<?php print $subdir; ?>/(.*\.json)$ {
    try_files /$1 @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Serve audio and video files directly, with a long send timeout. A player
  ### or a CDN edge reads far ahead of playback and then reads nothing at all
  ### until the listener catches up, which takes many minutes on an audio file.
  ### The http-level timeout between two writes would close the response in the
  ### meantime, and behind a CDN the listener then gets a file cut short long
  ### after the fact. Short /files/ and /downloads/ URIs arrive here through
  ### their own rewrite. Keep this location ahead of the static one below.
  ### The media locations use a named capture: a rewrite runs before the
  ### try_files.
  ###
  location ~* ^/<?php print $subdir; ?>/(?<sd_file>.+\.(?:mp3|ogg|oga|ogv|opus|wav|flac|aac|weba|webm|avi|mpe?g|mov|wmv|mkv|m4v))$ {
    send_timeout 3600s;
    expires 30d;
    access_log off;
    log_not_found off;
    rewrite ^/<?php print $subdir; ?>/images/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/images/$1 last;
    rewrite ^/<?php print $subdir; ?>/.+/sites/.+/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
    try_files /$sd_file =404;
  }

  ###
  ### Serve & no-log static files & images directly,
  ### without all standard drupal rewrites, php-fpm etc.
  ###
  location ~* ^/<?php print $subdir; ?>/(?<sd_file>.+\.(?:pdf|jpe?g|gif|png|ico|webp|avif|bmp|svg|swf|docx?|xlsx?|pptx?|tiff?|txt|rtf|vcard|vcf|bat|dll|class|otf|ttf|woff2?|eot|less|avi|mpe?g|mov|wmv|mp3|ogg|ogv|wav|midi|zip|tar|t?gz|rar|dmg|exe|apk|pxl|ipa|map))$ {
    expires 30d;
    access_log off;
    log_not_found off;
    rewrite ^/<?php print $subdir; ?>/images/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/images/$1 last;
    rewrite ^/<?php print $subdir; ?>/.+/sites/.+/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
    try_files /$sd_file =404;
  }

  ###
  ### Serve bigger media/static/archive files directly,
  ### without all standard drupal rewrites, php-fpm etc.
  ###
  location ~* ^/<?php print $subdir; ?>/(?<sd_file>.+\.(?:avi|mpe?g|mov|wmv|ogg|ogv|webm|zip|tar|t?gz|rar|dmg|exe|apk|pxl|ipa))$ {
    expires 30d;
    access_log off;
    log_not_found off;
    rewrite ^/<?php print $subdir; ?>/.+/sites/.+/files/(.*)$  /<?php print $subdir; ?>/sites/<?php print $this->uri; ?>/files/$1 last;
    try_files /$sd_file =404;
  }

  ###
  ### Serve & no-log some static files directly,
  ### but only from the files directory to not break
  ### dynamically created pdf files or redirects for
  ### legacy URLs with asp/aspx extension.
  ###
  location ~* ^/<?php print $subdir; ?>/(sites/.+/files/.+\.(?:pdf|aspx?))$ {
    expires 30d;
    access_log off;
    log_not_found off;
    try_files /$1 =404;
  }

  ###
  ### Pseudo-streaming server-side support for Flash Video (FLV) files.
  ###
  location ~* ^/<?php print $subdir; ?>/(.+\.flv)$ {
    flv;
    send_timeout 3600s;
    expires 30d;
    access_log off;
    log_not_found off;
    try_files /$1 =404;
  }

  ###
  ### Pseudo-streaming server-side support for H.264/AAC files.
  ###
  location ~* ^/<?php print $subdir; ?>/(.+\.(?:mp4|m4a))$ {
    mp4;
    mp4_buffer_size 1m;
    mp4_max_buffer_size 5m;
    send_timeout 3600s;
    expires 30d;
    access_log off;
    log_not_found off;
    try_files /$1 =404;
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
  location ~* ^/<?php print $subdir; ?>/((?:.*/)?(?:modules|libraries)/(?:contrib/)?(?:ad|tinybrowser|f?ckeditor|tinymce|wysiwyg_spellcheck|ecc|civicrm|fbconnect|radioactivity|statistics)/.*\.php)$ {

    limit_conn limreq 88;
    include fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";
    # Read by the proxied-https shim in settings.php; an older
    # fastcgi_params lacks it.
    fastcgi_param REQUEST_SCHEME $scheme;

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
    try_files /$real_fastcgi_script_name =404;
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
  location ~* ^/<?php print $subdir; ?>/((?:.*/)?(?:ahah|ajax|batch|autocomplete|progress/|x-progress-id|js/).*)$ {
    location ~* \.php$ {
      return 404;
    }
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files /$1 @drupal_<?php print $subdir_loc; ?>;
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
  location ~* ^/<?php print $subdir; ?>/(sites/.*/files/.*)$ {
    location ~* \.php$ {
      return 404;
    }
    access_log off;
    log_not_found off;
    expires 30d;
    try_files /$1 =404;
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
  location ~* ^/<?php print $subdir; ?>/(?:admin|user|cart|checkout|logout) {
    if ( $is_bot ) {
      return 444;
    }
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }
  location ~* ^/<?php print $subdir; ?>/\w\w/(?:admin|user|cart|checkout|logout) {
    if ( $is_bot ) {
      return 444;
    }
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Protect from DoS attempts on never cached uri.
  ###
  location ~* ^/<?php print $subdir; ?>/(?:.*/)?(?:node/[0-9]+/edit|node/add|comment/reply) {
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Protect from DoS attempts on never cached uri.
  ###
  location ~* ^/<?php print $subdir; ?>/(?:.*/)?(?:node/[0-9]+/delete|approve) {
    if ($cache_uid = '') {
      return 403;
    }
    if ( $is_bot ) {
      return 444;
    }
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Catch all unspecified requests.
  ###
  location /<?php print $subdir; ?>/ {
    if ( $http_user_agent ~* wget ) {
      return 444;
    }
    ###
    ### Workaround for https://www.drupal.org/node/2599326.
    ###
    if ( $args ~* "/autocomplete/" ) {
      return 405;
    }
    ###
    ### Allow but rate-limit AI search/index, user-triggered and utility bots,
    ### as the shared include does on the main content surface: keyed per
    ### vendor, so only those AI classes are counted.
    ###
    limit_req zone=ai_search  burst=20 nodelay;
    limit_req zone=ai_user    burst=20 nodelay;
    limit_req zone=ai_utility burst=10 nodelay;
    limit_req_status 444;
    try_files $uri @cache_<?php print $subdir_loc; ?>;
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
    # Read by the proxied-https shim in settings.php; an older
    # fastcgi_params lacks it.
    fastcgi_param REQUEST_SCHEME $scheme;

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
  location ~ ^/<?php print $subdir; ?>/((?:core/)?update\.php)(?:/|$) {
    set $real_fastcgi_script_name $1;
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
  ### Force clean URLs for Drupal 8+.
  ###
  location ^~ /<?php print $subdir; ?>/index.php/ {
    rewrite ^/<?php print $subdir; ?>/index\.php/(.*)$ $boa_visitor_scheme://$host/<?php print $subdir; ?>/$1 permanent;
  }

  ###
  ### Send all non-static requests to php-fpm, restricted to known php file.
  ###
  location = /<?php print $subdir; ?>/index.php {

    limit_conn limreq 88;
    limit_conn_status 444;

<?php
  // The anonymous per-host render cap of the shared include, under the same
  // render-gate contract: its zone is declared in the BOA http-scope zones
  // file, and it is keyed on the host, so this site shares its parent's cap.
  $perhost_zone_ok = isset($boa_zones_body)
    && strpos($boa_zones_body, 'zone=boa_perhost_anon') !== FALSE;
  $perhost_anon_conn = (int) drush_get_option('nginx_perhost_anon_conn', 100);
  if ($perhost_anon_conn < 1 || $perhost_anon_conn > 65535) {
    $perhost_anon_conn = 100;
  }
  if ($perhost_zone_ok):
?>
    limit_conn boa_perhost_anon <?php print $perhost_anon_conn; ?>;

<?php endif; ?>
    add_header X-Device "$device";
    add_header X-GeoIP-Country-Code "$geoip_country_code";
    add_header X-GeoIP-Country-Name "$geoip_country_name";
    add_header X-Speed-Cache "$upstream_cache_status";
    add_header X-Speed-Cache-UID "$debug_session_flag";
    add_header X-Speed-Cache-Key "$key_uri";
    add_header X-NoCache "$nocache_details";
    add_header X-This-Proto "$http_x_forwarded_proto";
    add_header X-Core-Variant "$core_detected";
    add_header X-Loc-Where "$location_detected";
    add_header X-Http-Pragma "$http_pragma";
    add_header X-Arg-Nocache "$arg_nocache";
    add_header X-Arg-Comment "$arg_comment";
    add_header X-Server-Name "$main_site_name";
    add_header X-Server-Sub-Name "<?php print $this->uri; ?>";
    add_header X-Response-Status "$status";
<?php if ($nginx_has_http3): ?>
    add_header Alt-Svc 'h3=":443"; ma=86400';
<?php endif; ?>

    root  <?php print "{$this->root}"; ?>;

    include fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";
    # Read by the proxied-https shim in settings.php; an older
    # fastcgi_params lacks it.
    fastcgi_param REQUEST_SCHEME $scheme;

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
    fastcgi_param  SCRIPT_URI          $boa_visitor_scheme://$host/<?php print $subdir; ?>/;
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
    ### No http_500: a 5xx is cached for 1s and refreshed every window (the error
    ### microcache); served stale it replayed the first 500 for as long as the
    ### upstream kept failing. PHP down or slow still gets the last good copy.
    fastcgi_cache_use_stale error invalid_header timeout updating;
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
  root  <?php print "{$this->root}"; ?>;
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
### A named location sits at server level, so it names this site's root:
### the core variant is detected in this site's platform, not the parent's.
###
location @drupal_<?php print $subdir_loc; ?> {

  root  <?php print "{$this->root}"; ?>;

  ###
  ### Detect Drupal core variant
  ###
  set $core_detected "Legacy";
  set $location_detected "Nowhere";

  if ( -e $document_root/web.config ) {
    set $core_detected "Regular";
  }
  if ( -e $document_root/core ) {
    set $core_detected "Modern";
  }

  ###
  ### Drupal core specific location switch
  ###
  error_page 402 = @legacy_<?php print $subdir_loc; ?>;
  if ( $core_detected = Legacy ) {
    return 402;
  }
  error_page 406 = @regular_<?php print $subdir_loc; ?>;
  if ( $core_detected = Regular ) {
    return 406;
  }
  error_page 418 = @modern_<?php print $subdir_loc; ?>;
  if ( $core_detected = Modern ) {
    return 418;
  }

  ###
  ### Fallback to regular / D7 style rewrite
  ###
  set $location_detected "Fallback";
  rewrite ^ /<?php print $subdir; ?>/index.php?$query_string? last;
}

###
### Special location for Drupal 6.
###
location @legacy_<?php print $subdir_loc; ?> {
  root  <?php print "{$this->root}"; ?>;
  set $location_detected "Legacy";
  rewrite ^/<?php print $subdir; ?>/(.*)$ /<?php print $subdir; ?>/index.php?q=$1 last;
}

###
### Special location for Drupal 7.
###
location @regular_<?php print $subdir_loc; ?> {
  root  <?php print "{$this->root}"; ?>;
  set $location_detected "Regular";
  rewrite ^ /<?php print $subdir; ?>/index.php?$query_string? last;
}

###
### Special location for Drupal 8+.
###
location @modern_<?php print $subdir_loc; ?> {
  root  <?php print "{$this->root}"; ?>;
  set $location_detected "Modern";
  try_files $uri @index_modern_<?php print $subdir_loc; ?>;
}

###
### Reach index.php by rewrite, not by the internal redirect a try_files URI
### fallback performs: that restarts at the server level, where
### set $nocache_details "Cache" runs again and erases a location's "Skip".
###
location @index_modern_<?php print $subdir_loc; ?> {
  rewrite ^ /<?php print $subdir; ?>/index.php?$query_string? last;
}

###
### Internal location for /update.php restricted access.
###
location @allowupdate_<?php print $subdir_loc; ?> {

  root  <?php print "{$this->root}"; ?>;
  limit_conn limreq 8;
  include fastcgi_params;

  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";
  # Read by the proxied-https shim in settings.php; an older
  # fastcgi_params lacks it.
  fastcgi_param REQUEST_SCHEME $scheme;

  # Marks the six credentials below as urlencode()d (the cloaked settings.php
  # decodes exactly this source; the CLI tier is raw).
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

  root  <?php print "{$this->root}"; ?>;
  limit_conn limreq 8;
  include fastcgi_params;

  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";
  # Read by the proxied-https shim in settings.php; an older
  # fastcgi_params lacks it.
  fastcgi_param REQUEST_SCHEME $scheme;

  # Marks the six credentials below as urlencode()d (the cloaked settings.php
  # decodes exactly this source; the CLI tier is raw).
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

###
### Cron-only PHP entrypoint for Drupal 8+ w/ auth_basic turned off.
### The front controller's parameters, as the index.php location passes them.
###
location @cron_modern_<?php print $subdir_loc; ?> {

  root  <?php print "{$this->root}"; ?>;
  auth_basic off;
  limit_conn limreq 8;
  include fastcgi_params;

  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";
  # Read by the proxied-https shim in settings.php; an older
  # fastcgi_params lacks it.
  fastcgi_param REQUEST_SCHEME $scheme;

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
  fastcgi_param  SCRIPT_URI          $boa_visitor_scheme://$host/<?php print $subdir; ?>/;
  fastcgi_param  MAIN_SITE_NAME      <?php print $this->uri; ?>;

  fastcgi_param  REDIRECT_STATUS     200;
  fastcgi_index  index.php;

  fastcgi_param  SCRIPT_FILENAME     <?php print "{$this->root}"; ?>/index.php;
  fastcgi_param  SCRIPT_NAME         /<?php print $subdir; ?>/index.php;
  fastcgi_param  DOCUMENT_URI        /<?php print $subdir; ?>/index.php;
  fastcgi_param  PHP_SELF            /<?php print $subdir; ?>/index.php;
  fastcgi_param  QUERY_STRING        $args;

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
