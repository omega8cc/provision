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

set $subdir_main_site_name "<?php print $this->uri; ?>";

###
### Use the main site name if available, instead of
### potentially virtual server_name when alias is set
### as redirection target. See #2358977 for details.
###
if ($subdir_main_site_name = '') {
  set $subdir_main_site_name "$server_name";
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
  return 403;
}

###
### Helper locations to avoid 404 on legacy images paths
###
location ^~ /<?php print $subdir; ?>/sites/default/files {


  root  <?php print "{$this->root}"; ?>;

  location ~* ^/<?php print $subdir; ?>/sites/default/files/imagecache {
    access_log off;
    log_not_found off;
    expires    30d;
    set $nocache_details "Skip";
    rewrite ^/<?php print $subdir; ?>/sites/default/files/imagecache/(.*)$ /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/imagecache/$1 last;
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }
  location ~* ^/<?php print $subdir; ?>/sites/default/files/(css|js|styles) {
    access_log off;
    log_not_found off;
    expires    30d;
    set $nocache_details "Skip";
    rewrite ^/<?php print $subdir; ?>/sites/default/files/(css|js|styles)/(.*)$ /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/$1/$2 last;
    try_files /$2 $uri @drupal_<?php print $subdir_loc; ?>;
  }
  location ~* ^/<?php print $subdir; ?>/sites/default/files {
    access_log off;
    log_not_found off;
    expires    30d;
    rewrite ^/<?php print $subdir; ?>/sites/default/files/(.*)$ /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/$1 last;
    try_files /$1 $uri =404;
  }
}

###
### Master location for subdir support (start)
###
location ^~ /<?php print $subdir; ?> {

  root  <?php print "{$this->root}"; ?>;

  set $nocache_details "Cache";

  ###
  ### Deny crawlers.
  ###
  if ($is_crawler) {
    return 403;
  }

  ###
  ### Block semalt botnet.
  ###
  if ($is_botnet) {
    return 403;
  }

  ###
  ### Include high load protection config if exists.
  ###
  include /data/conf/nginx_high_load.c*;

  ###
  ### Include PHP-FPM version override logic if exists.
  ###
  include <?php print $aegir_root; ?>/config/server_master/nginx/post.d/fpm_include*;

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
    return 403;
  }

  ###
  ### Deny listed requests for security reasons.
  ###
  if ($is_denied) {
    return 403;
  }

  ###
  ### HTTPRL standard support.
  ###
  location ^~ /<?php print $subdir; ?>/httprl_async_function_callback {
    location ~* ^/<?php print $subdir; ?>/httprl_async_function_callback {
      access_log off;
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
      set $nocache_details "Skip";
      try_files /admin/httprl-test $uri @drupal_<?php print $subdir_loc; ?>;
    }
  }

  ###
  ### CDN Far Future expiration support.
  ###
  location ^~ /<?php print $subdir; ?>/cdn/farfuture/ {
    access_log    off;
    log_not_found off;
    etag          off;
    gzip_http_version 1.1;
    if_modified_since exact;
    set $nocache_details "Skip";
    location ~* ^/<?php print $subdir; ?>/(cdn/farfuture/.+\.(?:css|js|jpe?g|gif|png|ico|webp|bmp|svg|swf|pdf|docx?|xlsx?|pptx?|tiff?|txt|rtf|class|otf|ttf|woff2?|eot|less))$ {
      expires max;
      add_header X-Header "CDN Far Future Generator 1.0";
      add_header Cache-Control "no-transform, public";
      add_header Last-Modified "Wed, 20 Jan 1988 04:20:42 GMT";
      rewrite ^/<?php print $subdir; ?>/cdn/farfuture/[^/]+/[^/]+/(.+)$ /$1 break;
      try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }
    location ~* ^/<?php print $subdir; ?>/(cdn/farfuture/) {
      expires epoch;
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
    access_log    off;
    log_not_found off;
    expires       30d;
    try_files     /sites/$subdir_main_site_name/files/favicon.ico /sites/$host/files/favicon.ico /favicon.ico $uri =204;
  }

  ###
  ### Support for http://drupal.org/project/robotstxt module
  ### and static file in the sites/domain/files directory.
  ###
  location = /<?php print $subdir; ?>/robots.txt {
    access_log    off;
    log_not_found off;
    try_files /sites/$subdir_main_site_name/files/$host.robots.txt /sites/$subdir_main_site_name/files/robots.txt /sites/$host/files/robots.txt /robots.txt $uri @cache_<?php print $subdir_loc; ?>;
  }

  ###
  ### Allow local access to support wget method in Aegir settings
  ### for running sites cron.
  ###
  location = /<?php print $subdir; ?>/cron.php {

    include       fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";

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

    allow        127.0.0.1;
    deny         all;

    try_files    /cron.php $uri =404;
<?php if ($satellite_mode == 'boa'): ?>
    fastcgi_pass unix:/var/run/$user_socket.fpm.socket;
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
    access_log   off;
    allow        127.0.0.1;
    deny         all;
    try_files    $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Send search to php-fpm early so searching for node.js will work.
  ### Deny bots on search uri.
  ###
  location ^~ /<?php print $subdir; ?>/search {
    location ~* ^/<?php print $subdir; ?>/search {
      if ( $is_bot ) {
        return 403;
      }
      try_files /search $uri @drupal_<?php print $subdir_loc; ?>;
    }
  }

  ###
  ### Support for https://drupal.org/project/js module.
  ###
  location ^~ /<?php print $subdir; ?>/js/ {
    location ~* ^/<?php print $subdir; ?>/js/ {
      if ( $is_bot ) {
        return 403;
      }
      rewrite ^/<?php print $subdir; ?>/(.*)$ /js.php?q=$1 last;
    }
  }

  ###
  ### Deny cache details display.
  ###
  location ^~ /<?php print $subdir; ?>/admin/settings/performance/cache-backend {
    access_log off;
    return 301 $scheme://$host/<?php print $subdir; ?>/admin/settings/performance;
  }

  ###
  ### Deny cache details display.
  ###
  location ^~ /<?php print $subdir; ?>/admin/config/development/performance/redis {
    access_log off;
    return 301 $scheme://$host/<?php print $subdir; ?>/admin/config/development/performance;
  }

  ###
  ### Deny cache details display.
  ###
  location ^~ /<?php print $subdir; ?>/admin/reports/redis {
    access_log off;
    return 301 $scheme://$host/<?php print $subdir; ?>/admin/reports;
  }

  ###
  ### Support for backup_migrate module download/restore/delete actions.
  ###
  location ^~ /<?php print $subdir; ?>/admin {
    if ( $is_bot ) {
      return 403;
    }
    access_log off;
    set $nocache_details "Skip";
    try_files /admin $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Avoid caching /civicrm* and protect it from bots.
  ###
  location ^~ /<?php print $subdir; ?>/civicrm {
    if ( $is_bot ) {
      return 403;
    }
    access_log off;
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Avoid caching /civicrm* requests, but protect from bots on a multi-lingual site
  ###
  location ^~ /<?php print $subdir; ?>/\w\w/civicrm {
    if ( $is_bot ) {
      return 403;
    }
    access_log off;
    set $nocache_details "Skip";
    try_files $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Support for audio module.
  ###
  location ^~ /<?php print $subdir; ?>/audio/download {
    location ~* ^/<?php print $subdir; ?>/(audio/download/.*/.*\.(?:mp3|mp4|m4a|ogg))$ {
      if ( $is_bot ) {
        return 403;
      }
      access_log    off;
      log_not_found off;
      set $nocache_details "Skip";
      try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }
  }

  ###
  ### Deny listed requests for security reasons.
  ###
  location ~* (\.(?:git.*|htaccess|engine|config|inc|ini|info|install|make|module|profile|test|pl|po|sh|.*sql|theme|twig|tpl(\.php)?|xtmpl|yml)(~|\.sw[op]|\.bak|\.orig|\.save)?$|^(\..*|Entries.*|Repository|Root|Tag|Template|composer\.(json|lock))$|^#.*#$|\.php(~|\.sw[op]|\.bak|\.orig\.save))$ {
    access_log off;
    return 404;
  }

  ###
  ### Deny listed requests for security reasons.
  ###
  location ~* /(?:modules|themes|libraries)/.*\.(?:txt|md)$ {
    access_log off;
    return 404;
  }

  ###
  ### Deny listed requests for security reasons.
  ###
  location ~* /files/civicrm/(?:ConfigAndLog|custom|upload|templates_c) {
    access_log off;
    return 404;
  }

  ###
  ### Deny often flooded URI for performance reasons
  ###
  location = /<?php print $subdir; ?>/autodiscover/autodiscover.xml {
    access_log off;
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
      expires    30d;
      set $nocache_details "Skip";
      rewrite  ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/$1 last;
      try_files  /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/(css|js|styles)/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    ###
    ### Sub-location to support css with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/css/(.*)$ {
      access_log off;
      log_not_found off;
      expires    30d;
      set $nocache_details "Skip";
      rewrite  ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/$1 last;
      try_files  /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/css/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    ###
    ### Sub-location to support js with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/js/(.*)$ {
      access_log off;
      log_not_found off;
      expires    30d;
      set $nocache_details "Skip";
      rewrite  ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/$1 last;
      try_files  /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/js/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    ###
    ### Sub-location to support files/imagecache with short URIs.
    ###
    location ~* /<?php print $subdir; ?>/files/imagecache/(.*)$ {
      access_log off;
      log_not_found off;
      expires    30d;
      # fix common problems with old paths after import from standalone to Aegir multisite
      rewrite ^/<?php print $subdir; ?>/files/imagecache/(.*)/sites/default/files/(.*)$ /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/imagecache/$1/$2 last;
      rewrite ^/<?php print $subdir; ?>/files/imagecache/(.*)/files/(.*)$               /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/imagecache/$1/$2 last;
      set $nocache_details "Skip";
      rewrite  ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/$1 last;
      try_files  /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/imagecache/$1 $uri @drupal_<?php print $subdir_loc; ?>;
    }

    location ~* ^.+\.(?:pdf|jpe?g|gif|png|ico|webp|bmp|svg|swf|docx?|xlsx?|pptx?|tiff?|txt|rtf|vcard|vcf|cgi|bat|pl|dll|class|otf|ttf|woff2?|eot|less|avi|mpe?g|mov|wmv|mp3|ogg|ogv|wav|midi|zip|tar|t?gz|rar|dmg|exe|apk|pxl|ipa|css|js|map)$ {
      expires       30d;
      access_log    off;
      log_not_found off;
      rewrite  ^/<?php print $subdir; ?>/files/(.*)$  /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/$1 last;
      try_files   $uri =404;
    }
    try_files /$1 $uri @cache_<?php print $subdir_loc; ?>;
  }


  ###
  ### The s3/files/styles (s3fs) support.
  ###
  location ~* ^/<?php print $subdir; ?>/s3/files/(css|js|styles)/(.*)$ {
    access_log off;
    log_not_found off;
    expires    30d;
    set $nocache_details "Skip";
    try_files  /<?php print $subdir; ?>/sites/$subdir_main_site_name/files/$1/$2 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Imagecache and imagecache_external support.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:external|system|files/imagecache|files/(css|js|styles))/.*) {
    access_log off;
    log_not_found off;
    expires    30d;
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Deny direct access to backups.
  ###
  location ~* ^/<?php print $subdir; ?>/sites/.*/files/backup_migrate/ {
    access_log off;
    deny all;
  }

  ###
  ### Deny direct access to config files in Drupal 8+.
  ###
  location ~* ^/<?php print $subdir; ?>/sites/.*/files/config_.* {
    access_log off;
    deny all;
  }

  ###
  ### Private downloads are always sent to the drupal backend.
  ### Note: this location doesn't work with X-Accel-Redirect.
  ###
  location ~* ^/<?php print $subdir; ?>/(sites/.*/files/private/.*) {
    if ( $is_bot ) {
      return 403;
    }
    access_log off;
    rewrite    ^/<?php print $subdir; ?>/sites/.*/files/private/(.*)$ $scheme://$host/<?php print $subdir; ?>/system/files/private/$1 permanent;
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
      return 403;
    }
    access_log off;
    internal;
  }

  ###
  ### Deny direct access to private downloads also for short, rewritten URLs.
  ### Note: this location works with X-Accel-Redirect.
  ###
  location ~* /<?php print $subdir; ?>/files/private/ {
    if ( $is_bot ) {
      return 403;
    }
    access_log off;
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
    expires    max;
    access_log off;
<?php if ($nginx_has_etag): ?>
    etag       off;
<?php else: ?>
    add_header ETag "";
<?php endif; ?>
    add_header X-Header "AdvAgg Generator 2.0";
    add_header Cache-Control "max-age=31449600, no-transform, public";
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Make css files compatible with boost caching.
  ###
  location ~* ^/<?php print $subdir; ?>/(.*\.css)$ {
    access_log  off;
    expires     max; #if using aggregator
    try_files   /cache/perm/$host${uri}_.css /$1 $uri =404;
  }

  ###
  ### Make js files compatible with boost caching.
  ###
  location ~* ^/<?php print $subdir; ?>/(.*\.(?:js|htc))$ {
    access_log  off;
    expires     max; # if using aggregator
    try_files   /cache/perm/$host${uri}_.js /$1 $uri =404;
  }

  ###
  ### Support for static .json files with fast 404 +Boost compatibility.
  ###
  location ~* ^/<?php print $subdir; ?>/sites/.*/files/(.*\.json)$ {
    access_log  off;
    expires     max; ### if using aggregator
    try_files   /cache/normal/$host${uri}_.json /$1 $uri =404;
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
  location ~* ^/<?php print $subdir; ?>/(.+\.(?:jpe?g|gif|png|ico|webp|bmp|svg|swf|pdf|docx?|xlsx?|pptx?|tiff?|txt|rtf|vcard|vcf|cgi|bat|pl|dll|aspx?|class|otf|ttf|woff2?|eot|less))$ {
    expires       30d;
    access_log    off;
    log_not_found off;
    try_files   /$1 $uri =404;
  }

  ###
  ### Serve & log bigger media/static/archive files directly,
  ### without all standard drupal rewrites, php-fpm etc.
  ###
  location ~* ^/<?php print $subdir; ?>/(.+\.(?:avi|mpe?g|mov|wmv|mp3|mp4|m4a|ogg|ogv|flv|wav|midi|zip|tar|t?gz|rar|dmg|exe))$ {
    expires     30d;
    access_log    off;
    log_not_found off;
    try_files   /$1 $uri =404;
  }

  ###
  ### Serve & no-log some static files as is, without forcing default_type.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:cross-?domain)\.xml)$ {
    access_log  off;
    expires     30d;
    try_files   /$1 $uri =404;
  }

  ###
  ### Allow some known php files (like serve.php in the ad module).
  ###
  location ~* ^/<?php print $subdir; ?>/(.*/(?:modules|libraries)/(?:contrib/)?(?:ad|tinybrowser|f?ckeditor|tinymce|wysiwyg_spellcheck|ecc|civicrm|fbconnect|radioactivity)/.*\.php)$ {

    limit_conn   limreq 88;

    include       fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";

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

    access_log   off;
    if ( $is_bot ) {
      return 403;
    }
    try_files    /$1 $uri =404;
<?php if ($satellite_mode == 'boa'): ?>
    fastcgi_pass unix:/var/run/$user_socket.fpm.socket;
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
      return 403;
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
      return 403;
    }
    access_log      off;
    expires         30d;
    try_files /$1 $uri =404;
  }

  ###
  ### Serve & no-log any not specified above static files directly.
  ###
  location ~* ^/<?php print $subdir; ?>/(sites/.*/files/.*) {
    root  <?php print "{$this->root}"; ?>;
    rewrite     ^/<?php print $subdir; ?>/sites/(.*)$ /sites/$subdir_main_site_name/$1 last;
    access_log      off;
    expires         30d;
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
    add_header X-Header "Boost Citrus 1.0";
    add_header Expires "Tue, 24 Jan 1984 08:00:00 GMT";
    add_header Cache-Control "must-revalidate, post-check=0, pre-check=0";
    charset    utf-8;
    types { }
    default_type text/xml;
    try_files /cache/normal/$host${uri}_.xml /cache/normal/$host${uri}_.html /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Deny bots on never cached uri.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:.*/)?(?:admin|user|cart|checkout|logout|comment/reply)) {
    if ( $is_bot ) {
      return 403;
    }
    access_log off;
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Protect from DoS attempts on never cached uri.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:.*/)?(?:node/[0-9]+/edit|node/add)) {
    if ( $is_bot ) {
      return 403;
    }
    access_log off;
    set $nocache_details "Skip";
    try_files /$1 $uri @drupal_<?php print $subdir_loc; ?>;
  }

  ###
  ### Protect from DoS attempts on never cached uri.
  ###
  location ~* ^/<?php print $subdir; ?>/((?:.*/)?(?:node/[0-9]+/delete|approve)) {
    if ($cache_uid = '') {
      return 403;
    }
    if ( $is_bot ) {
      return 403;
    }
    access_log off;
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
  ### Redirect to working homepage.
  ###
  location = /<?php print $subdir; ?> {
    return 301 $scheme://$host/<?php print $subdir; ?>/;
  }

  ###
  ### Catch all unspecified requests.
  ###
  location /<?php print $subdir; ?>/ {
    if ( $http_user_agent ~* wget ) {
      return 403;
    }
    try_files /$1 $uri @cache_<?php print $subdir_loc; ?>;
  }

  ###
  ### Send other known php requests/files to php-fpm without any caching.
  ###
  location ~* ^/<?php print $subdir; ?>/((core/)?(boost_stats|rtoc|js))\.php$ {

    limit_conn   limreq 88;

    if ( $is_bot ) {
      return 404;
    }

    include       fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";

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

    access_log   off;
    try_files    /$1.php =404; ### check for existence of php file first
<?php if ($satellite_mode == 'boa'): ?>
    fastcgi_pass unix:/var/run/$user_socket.fpm.socket;
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
  ### Rewrite legacy requests with /<?php print $subdir; ?>/index.php to extension-free URL.
  ###
  if ( $args ~* "^q=(?<query_value>.*)" ) {
    rewrite ^/<?php print $subdir; ?>/index.php$ $scheme://$host/<?php print $subdir; ?>/?q=$query_value? permanent;
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
    add_header X-Speed-Cache-UID "$cache_uid";
    add_header X-Speed-Cache-Key "$key_uri";
    add_header X-NoCache "$nocache_details";
    add_header X-This-Proto "$http_x_forwarded_proto";
    add_header X-Server-Sub-Name "$subdir_main_site_name";
    add_header X-Response-Status "$status";

    root          <?php print "{$this->root}"; ?>;

    include       fastcgi_params;

    # Block https://httpoxy.org/ attacks.
    fastcgi_param HTTP_PROXY "";

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
    if ( $sent_http_x_force_nocache = "YES" ) {
      set $nocache_details "Skip";
    }
    if ( $http_cookie ~* "NoCacheID" ) {
      set $nocache_details "AegirCookie";
    }
    if ( $cache_uid ) {
      set $nocache_details "DrupalCookie";
    }
    ###
    ### Use Nginx cache for all visitors by default.
    ###
    set $nocache "";
    if ( $nocache_details ~ (?:AegirCookie|Args|Skip) ) {
      set $nocache "NoCache";
    }

    ###
    ### Ensure security and privacy headers are added only if not set by Drupal.
    ###
    if ($sent_http_strict_transport_security = '') {
      add_header Strict-Transport-Security "max-age=86400";    # 1 day for now
    }
    if ($sent_http_x_content_type_options = '') {
      add_header X-Content-Type-Options "nosniff";
    }
    if ($sent_http_content_security_policy = '') {
      add_header Content-Security-Policy "default-src 'self' https: data:; script-src 'self' 'unsafe-inline' 'unsafe-eval' https:; style-src 'self' 'unsafe-inline' https:; img-src 'self' data: https:; font-src 'self' https:; object-src 'none'; frame-ancestors 'self'; base-uri 'self'; form-action 'self'";
    }
    if ($sent_http_referrer_policy = '') {
      add_header Referrer-Policy "no-referrer-when-downgrade";
    }
    if ($sent_http_permissions_policy = '') {
      add_header Permissions-Policy "geolocation=(), microphone=(), camera=(), fullscreen=(self), autoplay=()";
    }

    ###
    ### Add headers for debugging
    ###
    add_header X-Debug-NoCache-Switch "$nocache";
    add_header X-Debug-NoCache-Auth "$http_authorization";
    add_header X-Debug-NoCache-Cookie "$cookie_NoCacheID";
    add_header Cache-Control "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";

    try_files     /index.php =404; ### check for existence of php file first
<?php if ($satellite_mode == 'boa'): ?>
    fastcgi_pass  unix:/var/run/$user_socket.fpm.socket;
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
    fastcgi_no_cache $cookie_NoCacheID $http_authorization $nocache;
    fastcgi_cache_bypass $cookie_NoCacheID $http_authorization $nocache;
    fastcgi_cache_use_stale error http_500 http_503 invalid_header timeout updating;
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
  if ( $sent_http_x_force_nocache = "YES" ) {
    set $nocache_details "Skip";
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
  error_page 405 = @drupal_<?php print $subdir_loc; ?>;
  add_header X-Header "Boost Citrus 1.0";
  add_header Expires "Tue, 24 Jan 1984 08:00:00 GMT";
  add_header Cache-Control "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";
  charset    utf-8;
  try_files  /cache/normal/$host${uri}_$args.html @drupal_<?php print $subdir_loc; ?>;
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

  limit_conn   limreq 8;

  include       fastcgi_params;

  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";

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

  fastcgi_param SCRIPT_FILENAME <?php print "{$this->root}"; ?>/$real_fastcgi_script_name;

  fastcgi_split_path_info ^(.+\.php)(/.+)$;
  fastcgi_index update.php;
  fastcgi_intercept_errors on;

<?php if ($satellite_mode == 'boa'): ?>
  fastcgi_pass unix:/var/run/$user_socket.fpm.socket;
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

  limit_conn   limreq 8;
  include       fastcgi_params;

  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";

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
  fastcgi_pass unix:/var/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
  fastcgi_pass 127.0.0.1:9000;
<?php else: ?>
  fastcgi_pass unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>
}


#######################################################
###  nginx.conf site level extended vhost include end
#######################################################
