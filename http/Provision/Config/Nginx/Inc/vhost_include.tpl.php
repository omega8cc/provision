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
?>
#######################################################
###  nginx.conf site level extended vhost include start
#######################################################

###
### Use the main site name if available, instead of
### potentially virtual server_name when alias is set
### as redirection target. See #2358977 for details.
###
if ($main_site_name = '') {
  set $main_site_name "$server_name";
}

###
### Block “node-chain” URL mutation spam.
### Examples:
### /node/1771/pl/node/1771/es/node/1771/...
### /pl/node/1771/es/node/1771/...
###
if ($is_node_chain) {
  return 404;
}

###
### Block “language-prefix chain” URL mutation spam.
### Examples (4+ language-like prefixes in a row):
### /pl/en/fr/de/office/city-benelux
### /pt-br/es/it/nl/product/ai-driven-project-manager
### /zh-hans/ja/ko/en/node/1771
### /en/en/en/en/anything
###
if ($is_lang_chain) {
  return 404;
}

###
### Block static-asset “chain” URL mutation spam (see the $is_static_chain map
### in server.tpl.php).  444 here, before the /(?:external|system)/ asset router
### (~942) routes the absent file to @drupal -> /index.php -> php-fpm.  Uses 444
### (drop, no response) to match the hostile-traffic family and feed the per-IP
### 444 counter, vs the 404 used by the node/lang-chain siblings above.
###
if ($is_static_chain) {
  return 444;
}

###
### Block the content-path twin of the static-asset chain flood (see the
### $is_content_chain composed map in server.tpl.php).  404 (cheap, no php-fpm)
### matching the node/lang-chain content-shape siblings rather than 444 — these
### are content URLs, so a recoverable 404 keeps the false-positive blast radius
### small.  Converts the served-200 variant into a no-bootstrap 404.
###
if ($is_content_chain) {
  return 404;
}

###
### Block the Referer-less /print* flood (see $is_print_path /
### $block_print_no_referer in server.tpl.php).  404, not 444: search crawlers
### never send a Referer, so this class also contains Googlebot/Bingbot hits on
### linked print pages — a 444 reached them as Cloudflare 520 / proxy 502 and
### produced GSC "Server error (5xx)" churn plus wasted crawl-budget retries.
### A static 404 is equally php-fpm-free, still starves the botnet, and is the
### right crawl outcome anyway (print pages are duplicate content that should
### not be indexed); mirrors the $is_content_chain guard above.  A legitimate
### print or email-this-page click carries a Referer and passes straight
### through.  Works regardless of whether any print module is enabled, on D7
### and D10+.
###
if ($block_print_no_referer) {
  return 404;
}

###
### Block the Referer-less Flag-module toggle flood (see $is_flag_toggle /
### $block_flag_no_referer in server.tpl.php).  404, not 444, for the same
### crawler-safety reasons as the /print* guard above: the no-Referer class
### includes crawlers following flag action links, and a static 404 is
### php-fpm-free, starves the botnet, and tells crawlers to drop the URL.
### A real flag click carries a Referer (and a valid session token) and
### passes straight through; POST-based flagging is untouched (the composed
### map is GET-only).  Works regardless of whether the Flag module is
### enabled, on D7 and D8+.
###
if ($block_flag_no_referer) {
  return 404;
}

###
### Block the cold HybridAuth window flood (see $is_hybridauth_window /
### $block_hybridauth_no_referer in server.tpl.php).  404, not 444, for the
### same crawler-safety reasons as the guards above: a static 404 is
### php-fpm-free, starves the botnet, and tells crawlers to drop the URL.
### Fires only on the intersection of no Referer AND no session cookie, so
### BOTH hops of a real login pass: the initiating click carries a Referer,
### and the provider's return hop — which lands back on this same window path
### as hauth_return_to, often Referer-less — carries the session cookie the
### outbound leg created.  /hybridauth/endpoint is the provider's callback
### target and stays unguarded.  Works regardless of whether the HybridAuth
### module is enabled.
###
if ($block_hybridauth_no_referer) {
  return 404;
}

###
### Mitigation for https://www.drupal.org/SA-CORE-2018-002
###
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

set $nocache_details "Cache";

###
### Drop security-banned client IPs (populated by the BOA monitor/firewall
### layer).  Keyed on $remote_addr, which Cloudflare realip resolves to the
### real client, so the ban bites CF-proxied traffic, not only direct.
###
if ($is_banned) {
  return 444;
}

###
### Return 404 on special PHP URLs to avoid revealing version used,
### even indirectly. See also: https://drupal.org/node/2116387
###
if ( $args ~* "=PHP[A-Z0-9]{8}-" ) {
  return 404;
}

###
### Deny probes for secret/config paths (never valid on a hosted site, any UA).
###
if ($is_secret_path) {
  return 444;
}

###
### Deny foreign-CMS admin probes (WordPress/Joomla/phpMyAdmin path tokens that
### cannot exist on a Drupal/Backdrop/Hostmaster docroot — see $is_cms_probe in
### server.tpl.php).  444 drops them before the extensionless variant routes to
### @drupal -> /index.php -> php-fpm, removing the bootstrap-per-probe FPM sink
### and feeding the scan_nginx 444 counter.  Whole-segment match only; generic
### auth words are left to the IDS aggregate detector, not blocked here.
###
if ($is_cms_probe) {
  return 444;
}

###
### Deny forged AI user-agents.  Google-Extended / Applebot-Extended are
### robots.txt-only tokens a real client never sends — proof of forgery.
###
if ($is_ai_forged) {
  return 444;
}

###
### Deny AI training / bulk-collection crawlers by default.  A per-site BOA
### opt-in (the ai_policy fragment) sets $ai_train_allow 1 to exempt a site;
### $ai_train_allow is defaulted to 0 before the ai_policy include in the vhost
### template, so without a fragment training stays blocked.  ($is_ai_search /
### $is_ai_user / $is_ai_utility per-site BLOCK is carried directly by the
### fragment as `if ($is_ai_*) { return 444; }`, so no global guard for them.)
###
set $ai_train_block $is_ai_training;
if ($ai_train_allow) {
  set $ai_train_block '';
}
if ($ai_train_block) {
  return 444;
}

###
### Deny EVASIVE AI user-fetchers (Perplexity-User) by default.  Same shape as
### training: they identify as user-triggered but ignore robots.txt and, when
### blocked, drop their UA and rotate IPs/ASNs, so this UA block is best-effort
### and the IDS/csf layer is the real backstop.  A per-site opt-in sets
### $ai_evasive_allow 1 (defaulted to 0 before the ai_policy include in the
### vhost template) to exempt a site.
###
set $ai_evasive_block $is_ai_evasive;
if ($ai_evasive_allow) {
  set $ai_evasive_block '';
}
if ($ai_evasive_block) {
  return 444;
}

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
### Include high load protection config if exists.
###
include /data/conf/nginx_high_load.c*;

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
### Deny false UA/bots trying known security patterns.
###
if ($ua_denied) {
  return 444;
}

###
### Detect TLS ClientHello sent to a plain HTTP port.
###
if ($tls_on_plain) {
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

<?php if ($nginx_has_http3): ?>
###
### Add recommended HTTP/3 headers
### https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Alt-Svc
###
add_header Alt-Svc 'h3=":443"; ma=86400';
<?php endif; ?>

###
### Force clean URLs for Drupal 8+.
###
rewrite ^/index.php/(.*)$ $scheme://$host/$1 permanent;

###
### Include high level local configuration override if exists.
###
include  <?php print $aegir_root; ?>/config/server_master/nginx/post.d/nginx_force_include*;

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
### Allow access to site-specific well-known/mta-sts.txt file.
### Example:
###   static/control/mta-sts-sitename.com.txt
###   static/control/mta-sts-otherone.com.txt
###
location ^~ /.well-known/mta-sts.txt {
  allow all;
  alias <?php print $aegir_root; ?>/static/control/mta-sts-$main_site_name.txt;
  try_files $uri 404;
}

###
### HTTPRL standard support.
###
location ^~ /httprl_async_function_callback {
  if ( $is_bot ) {
    return 444;
  }
  location ~* ^/httprl_async_function_callback {
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files $uri @drupal;
  }
}

###
### HTTPRL test mode support.
###
location ^~ /admin/httprl-test {
  if ($cache_uid = '') {
    return 403;
  }
  if ( $is_bot ) {
    return 444;
  }
  location ~* ^/admin/httprl-test {
    set $nocache_details "Skip";
    try_files $uri @drupal;
  }
}

<?php
// D7 background_process/background_batch drive batches via self-HTTP POSTs
// to /bgp-start/<handle>/<token>; under FPM saturation the re-dispatch loop
// feeds itself into a request wall the edge cannot ban (the source is the
// box's own address). The per-vhost bgp_flood cap bounds that wall while
// clearing worst-case legitimate cadence with headroom.
// The zone is declared in a BOA-written http-scope file, NOT in the master
// render, and these consumers appear only when that file is present with
// the expected zone: no delivery order can produce an undeclared-zone
// reference, which matters because a missing zone is a whole-box nginx
// [emerg] and the upgrade path restarts nginx without a configtest.
// Read once here and reuse: more than one guardrail in this template gates on
// this file, and a vhost render should not stat and slurp it per consumer.
// An absent or unreadable file yields '', so every gate below is simply FALSE.
$boa_zones_file = '/etc/nginx/conf.d/limit-req-zones-boa.conf';
$boa_zones_body = @is_file($boa_zones_file)
  ? (string) @file_get_contents($boa_zones_file)
  : '';
$bgp_zone_ok = strpos($boa_zones_body, 'zone=bgp_flood') !== FALSE;
if ($bgp_zone_ok):
?>
###
### Background process/batch self-request storm guard. Every legitimate
### request here is POST /bgp-start/<handle>/<token> from the site itself
### (fire-and-forget, HTTP/1.0, response never read), so the two-segment
### shape is exact and anything else under the prefix is junk we can shed
### for less than today's bootstrap-per-404. Access logging must stay ON:
### the batch_guard monitor reads these lines. The bot shed sits inside
### the limit-carrying location (an outer-block `if` never runs for
### requests matched by a nested location) and runs in the rewrite phase,
### before limit_req accounting, so crawler noise cannot spend the budget.
### 444 is deliberate: the dispatcher only detects TCP-connect failures,
### so no status is visible to it and the cheapest close wins.
###
location ^~ /bgp-start/ {
  location ~* ^/bgp-start/[^/]+/[^/]+$ {
    if ( $is_bot ) {
      return 444;
    }
    limit_req zone=bgp_flood burst=50 nodelay;
    limit_req_status 444;
    set $nocache_details "Skip";
    try_files $uri @drupal;
  }
  return 444;
}

###
### Language-prefix sibling (D7 url() prepends the prefix on multilingual
### sites), mirroring the /\w\w/search and /\w\w/civicrm convention; longer
### prefixes stay fail-open exactly like those precedents.
###
location ~* ^/\w\w/bgp-start/[^/]+/[^/]+$ {
  if ( $is_bot ) {
    return 444;
  }
  limit_req zone=bgp_flood burst=50 nodelay;
  limit_req_status 444;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}
<?php endif; ?>

###
### CDN Far Future expiration support.
###
location ^~ /cdn/farfuture/ {
  access_log off;
  log_not_found off;
<?php if ($nginx_has_etag): ?>
  etag off;
<?php else: ?>
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
  add_header ETag "";
<?php endif; ?>
  gzip_http_version 1.1;
  if_modified_since exact;
  set $nocache_details "Skip";
  location ~* ^/cdn/farfuture/.+\.(?:css|js|jpe?g|gif|png|ico|webp|bmp|svg|swf|pdf|docx?|xlsx?|pptx?|tiff?|txt|rtf|class|otf|ttf|woff2?|eot|less)$ {
    expires max;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Header "CDN Far Future Generator 1.0";
    add_header Cache-Control "no-transform, public";
    add_header Last-Modified "Wed, 20 Jan 1988 04:20:42 GMT";
    rewrite ^/cdn/farfuture/[^/]+/[^/]+/(.+)$ /$1 break;
    try_files $uri @drupal;
  }
  location ~* ^/cdn/farfuture/ {
    expires epoch;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Header "CDN Far Future Generator 1.1";
    add_header Cache-Control "private, must-revalidate, proxy-revalidate";
    rewrite ^/cdn/farfuture/[^/]+/[^/]+/(.+)$ /$1 break;
    try_files $uri @drupal;
  }
  try_files $uri @drupal;
}

###
### If favicon else return error 204.
###
location = /favicon.ico {
  access_log off;
  log_not_found off;
  expires 30d;
  try_files /sites/$main_site_name/files/favicon.ico $uri =204;
}

###
### Support for https://drupal.org/project/llms_txt module
### and static file in the sites/domain/files directory.
###
location = /llms.txt {
  access_log off;
  log_not_found off;
  try_files /sites/$main_site_name/files/$host.llms.txt /sites/$main_site_name/files/llms.txt $uri @cache;
}

###
### Support for https://drupal.org/project/robotstxt module
### and static file in the sites/domain/files directory.
###
location = /robots.txt {
  access_log off;
  log_not_found off;
  try_files /sites/$main_site_name/files/$host.robots.txt /sites/$main_site_name/files/robots.txt $uri @cache;
}

###
### Support for static ads.txt file in the sites/domain/files directory.
###
location = /ads.txt {
  access_log off;
  log_not_found off;
  try_files /sites/$main_site_name/files/$host.ads.txt /sites/$main_site_name/files/ads.txt $uri =404;
}

###
### Allow local access to the FPM status page.
###
location = /fpm-status {
  access_log off;
  log_not_found off;
  allow 127.0.0.1;
  deny all;
<?php if ($satellite_mode == 'boa'): ?>
  fastcgi_pass unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
  fastcgi_pass 127.0.0.1:9000;
<?php else: ?>
  fastcgi_pass unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>
}

###
### Allow local access to the FPM ping URI.
###
location = /fpm-ping {
  access_log off;
  log_not_found off;
  allow 127.0.0.1;
  deny all;
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
### for running sites cron.
###
location = /cron.php {
  allow 127.0.0.1;
  deny all;
  auth_basic off;
  try_files $uri =404;
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
location ^~ /cron/ {
  allow 127.0.0.1;
  deny all;
  auth_basic off;
  try_files "" @modern_cron;
}

###
### Cron-only PHP entrypoint for Drupal 8+ w/ auth_basic turned off
###
location @modern_cron {
  auth_basic off;
  include fastcgi_params;
  fastcgi_index index.php;
  fastcgi_param SCRIPT_FILENAME $document_root/index.php;
  fastcgi_param SCRIPT_NAME  /index.php;
  fastcgi_param DOCUMENT_URI /index.php;
  fastcgi_param QUERY_STRING $args;
  limit_conn limreq 8;
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
### for running sites cron on Backdrop (served by core/cron.php,
### with the key validated in the query string).
###
location = /core/cron.php {
  allow 127.0.0.1;
  deny all;
  auth_basic off;
  try_files $uri =404;
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
### Deny bots on search uri.
###
### Three guards (in order):
###   1. $block_search_no_referrer — Tier 1: blocks bots with no referrer
###      sending known fulltext/facet search params (see server_tpl maps 1-3).
###   2. $has_excessive_facets    — Tier 2: blocks bots that fake a self-referrer
###      but still carry 6+ URL-encoded facet params (f[5]+).
###   3. limit_req (two zones)    — Tier 3: per-IP burst cap (search_limit) and
###      per-vhost global cap (search_flood) for distributed one-IP-per-request
###      floods that slip through Tiers 1 and 2.
###
location ^~ /search {
  location ~* ^/search {
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
    try_files $uri @drupal;
  }
}

###
### Same three-tier search protection for language-prefixed paths (/xx/search).
###
location ~* ^/[a-z][a-z]/search {
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
  try_files $uri @drupal;
}

###
### Block search-destination abuse via Drupal's login redirect mechanism.
###
### Bots send /user/login?destination=search%2F...%3Ff%5BN%5D=im_taxonomy_vid...
### to bypass all /search location guards (those guards are never evaluated when
### the request path is /user/login).  The three tiers mirror /search exactly:
###
###   1. $block_login_search_destination — Map 5+2: search payload in destination
###      param AND no referrer → definite bot (no-referer tier, Maps 5+2+6).
###   2. $has_excessive_facets           — Map 4: 6+ facets encoded inside the
###      destination value → self-referer bot tier.
###   3. limit_req search_flood          — per-vhost global cap; catches the
###      remaining self-referer / few-facet distributed bots by aggregate rate.
###
### Note: $is_bot check is intentionally omitted — bots probing /user/login
### exclusively use modern, realistic UA strings.  The rate-limit zone provides
### the equivalent protection for that tier.
###
### set $nocache_details "Skip" bypasses Speed Booster so the login form is
### always rendered fresh (consistent with how /admin is handled).
###
location ^~ /user/login {
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
  try_files $uri @drupal;
}

###
### Support for https://drupal.org/project/js module.
### The js.php handler exists only where the contrib js module
### ships it (D6/D7). Backdrop core admin_bar serves its menu at
### js/admin_bar/cache/* as a regular router path, so without
### js.php the request must go to the front controller instead.
###
location ^~ /js/ {
  if ( $is_bot ) {
    return 444;
  }
  location ~* ^/js/ {
    if ( $is_bot ) {
      return 444;
    }
    error_page 418 = @drupal;
    if ( !-e $document_root/js.php ) {
      return 418;
    }
    rewrite ^/(.*)$ /js.php?q=$1 last;
  }
}

###
### Deny access to Hostmaster web/db server node.
### It is still possible to edit or break web/db server
### node at /node/2/edit, if you know what are you doing.
###
location ^~ /hosting/c/server_master {
  if ($cache_uid = '') {
    return 403;
  }
  if ( $is_bot ) {
    return 444;
  }
  return 301 $scheme://$host/hosting/sites;
}

###
### Deny access to Hostmaster db server node.
### It is still possible to edit or break db server
### node at /node/4/edit, if you know what are you doing.
###
location ^~ /hosting/c/server_localhost {
  if ($cache_uid = '') {
    return 403;
  }
  if ( $is_bot ) {
    return 444;
  }
  return 301 $scheme://$host/hosting/sites;
}

###
### Fix for #2005116
###
location ^~ /hosting/sites {
  if ($cache_uid = '') {
    return 403;
  }
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Fix for Aegir & .info .pl domain extensions.
###
location ^~ /hosting {
  if ($cache_uid = '') {
    return 403;
  }
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Deny cache details display.
###
location ^~ /admin/settings/performance/cache-backend {
  if ($cache_uid = '') {
    return 403;
  }
  if ( $is_bot ) {
    return 444;
  }
  return 301 $scheme://$host/admin/settings/performance;
}

###
### Deny cache details display.
###
location ^~ /admin/config/development/performance/redis {
  if ($cache_uid = '') {
    return 403;
  }
  if ( $is_bot ) {
    return 444;
  }
  return 301 $scheme://$host/admin/config/development/performance;
}

###
### Deny cache details display.
###
location ^~ /admin/reports/redis {
  if ($cache_uid = '') {
    return 403;
  }
  if ( $is_bot ) {
    return 444;
  }
  return 301 $scheme://$host/admin/reports;
}

###
### Support for backup_migrate module download/restore/delete actions.
###
location ^~ /admin {
  if ($cache_uid = '') {
    return 403;
  }
  if ( $is_bot ) {
    return 444;
  }
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Don't log and avoid caching /civicrm* requests.
###
location ^~ /civicrm {
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Avoid caching /civicrm* requests
###
location ~* ^/\w\w/civicrm {
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Support for audio module.
###
location ^~ /audio/download {
  if ( $is_bot ) {
    return 444;
  }
  location ~* ^/audio/download/.*/.*\.(?:mp3|mp4|m4a|ogg)$ {
    access_log off;
    log_not_found off;
    set $nocache_details "Skip";
    try_files $uri @drupal;
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
location ~* ^/sites/.*/files/civicrm/(?:ConfigAndLog|custom|upload|templates_c) {
  access_log off;
  log_not_found off;
  return 404;
}

###
### [Option] Deny public access to webform uploaded files
### for privacy reasons and to prevent phishing attacks.
### The files uploaded should be available only via SFTP.
###
location ~* ^/sites/.*/files/webform/ {
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  expires 99s;
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
  add_header Cache-Control "public, must-revalidate, proxy-revalidate";
  try_files $uri =404;
  ### to deny the access replace the last line with:
  ### return 404;
}
location ~* ^/files/webform/ {
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  expires 99s;
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
  add_header Cache-Control "public, must-revalidate, proxy-revalidate";
  try_files $uri =404;
  ### to deny the access replace the last line with:
  ### return 404;
}

###
### Deny often flooded URI for performance reasons
###
location = /autodiscover/autodiscover.xml {
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
### Deny bots on some weak modules uri.
###
location ~* (?:validation|aggregator|vote_up_down|captcha|vbulletin|glossary/|flag\/flag) {
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  try_files $uri @drupal;
}

###
### Responsive Images support.
### https://drupal.org/project/responsive_images
###
location ~* \.r\.(?:jpe?g|png|gif) {
  if ( $http_cookie ~* "rwdimgsize=large" ) {
    rewrite ^/(.*)/mobile/(.*)\.r(\.(?:jpe?g|png|gif))$ /$1/desktop/$2$3 last;
  }
  rewrite ^/(.*)\.r(\.(?:jpe?g|png|gif))$ /$1$2 last;
  access_log off;
  log_not_found off;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Adaptive Image Styles support.
### https://drupal.org/project/ais
###
location ~* /(?:.+)/files/(css|js|styles)/adaptive/(?:.+)$ {
  if ( $http_cookie ~* "ais=(?<ais_cookie>[a-z0-9-_]+)" ) {
    rewrite ^/(.+)/files/(css|js|styles)/adaptive/(.+)$ /$1/files/$2/$ais_cookie/$3 last;
  }
  access_log off;
  log_not_found off;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### The files/styles support.
###
location ~* /sites/.*/files/(css|js|styles)/(.*)$ {
  access_log off;
  log_not_found off;
  expires max;
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
  add_header Cache-Control "public";
  try_files /sites/$main_site_name/files/$1/$2 $uri @drupal;
}

###
### The s3/files/styles (s3fs) support.
###
location ~* /s3/files/(css|js|styles)/(.*)$ {
  access_log off;
  log_not_found off;
  expires max;
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
  add_header Cache-Control "public";
  try_files /sites/$main_site_name/files/$1/$2 $uri @drupal;
}

###
### The files/imagecache support.
###
location ~* /sites/.*/files/imagecache/(.*)$ {
  access_log off;
  log_not_found off;
  expires max;
  # fix common problems with old paths after import from standalone to Aegir multisite
  rewrite ^/sites/(.*)/files/imagecache/(.*)/sites/default/files/(.*)$ /sites/$main_site_name/files/imagecache/$2/$3 last;
  rewrite ^/sites/(.*)/files/imagecache/(.*)/files/(.*)$               /sites/$main_site_name/files/imagecache/$2/$3 last;
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
  add_header Cache-Control "public";
  try_files /sites/$main_site_name/files/imagecache/$1 $uri @drupal;
}

###
### Send requests with /external/ and /system/ URI keywords to @drupal.
###
location ~* /(?:external|system)/ {
  access_log off;
  log_not_found off;
  expires 30d;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Deny direct access to backups.
###
location ~* ^/sites/.*/files/backup_migrate/ {
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
location ~* ^/sites/.*/files/config_.* {
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  deny all;
}

###
### Include local configuration override if exists.
###
include  <?php print $aegir_root; ?>/config/server_master/nginx/post.d/nginx_vhost_include*;

###
### Private downloads are always sent to the drupal backend.
### Note: this location doesn't work with X-Accel-Redirect.
###
location ~* ^/sites/.*/files/private/ {
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  rewrite ^/sites/.*/files/private/(.*)$ $scheme://$host/system/files/private/$1 permanent;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Deny direct access to private downloads in sites/domain/private.
### Note: this location works with X-Accel-Redirect.
###
location ~* ^/sites/.*/private/ {
  internal;
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
}

###
### Deny direct access to private downloads also for short, rewritten URLs.
### Note: this location works with X-Accel-Redirect.
###
location ~* /files/private/ {
  internal;
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
}

###
### Wysiwyg Fields support.
###
location ~* wysiwyg_fields/(?:plugins|scripts)/.*\.(?:js|css) {
  access_log off;
  log_not_found off;
  try_files $uri @drupal;
}

###
### Advagg_css and Advagg_js support.
###
location ~* files/advagg_(?:css|js)/ {
  expires max;
  access_log off;
  log_not_found off;
<?php if ($nginx_has_etag): ?>
  etag off;
<?php else: ?>
  add_header ETag "";
<?php endif; ?>
  rewrite ^/files/advagg_(.*)/(.*)$ /sites/$main_site_name/files/advagg_$1/$2 last;
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
  add_header X-Header "AdvAgg Generator 2.0";
  add_header Cache-Control "max-age=31449600, no-transform, public";
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Make css files compatible with boost caching.
###
location ~* \.css$ {
  if ( $request_method = POST ) {
    return 405;
  }
  if ( $cache_uid ) {
    return 405;
  }
  error_page  405 = @uncached;
  access_log off;
  log_not_found off;
  expires max; #if using aggregator
  try_files /cache/perm/$host${uri}_.css $uri =404;
}

###
### Support for dynamic /sw.js requests. See #2982073 on drupal.org
###
location = /sw.js {
  try_files $uri @drupal;
}

###
### Make js files compatible with boost caching.
###
location ~* \.(?:js|htc)$ {
  if ( $request_method = POST ) {
    return 405;
  }
  if ( $cache_uid ) {
    return 405;
  }
  error_page  405 = @uncached;
  access_log off;
  log_not_found off;
  expires max; # if using aggregator
  try_files /cache/perm/$host${uri}_.js $uri =404;
}

###
### Deny listed requests for security reasons.
###
location ~* /.*composer\.(json|lock)$ {
  access_log off;
  log_not_found off;
  return 404;
}
location ^~ /vendor/composer/ {
  access_log off;
  log_not_found off;
  return 404;
}
location = /CHANGELOG.txt {
  access_log off;
  log_not_found off;
  return 404;
}

###
### Support for dynamic .json requests.
###
location ~* \.json$ {
  try_files $uri @drupal;
}

###
### Support for static .json files with fast 404 +Boost compatibility.
###
location ~* ^/sites/.*/files/.*\.json$ {
  if ( $cache_uid ) {
    return 405;
  }
  error_page  405 = @uncached;
  access_log off;
  log_not_found off;
  expires max; ### if using aggregator
  try_files /cache/normal/$host${uri}_.json $uri =404;
}

###
### Helper location to bypass boost static files cache for logged in users.
###
location @uncached {
  access_log off;
  log_not_found off;
  expires max; # max if using aggregator, otherwise sane expire time
}

###
### Map /files/ shortcut early to avoid overrides in other locations.
###
location ^~ /files/ {

  ###
  ### Sub-location to support Flash Video (FLV) files with short URIs.
  ###
  location ~* /files/.+\.flv$ {
    flv;
    expires 30d;
    access_log off;
    log_not_found off;
    rewrite ^/files/(.*)$  /sites/$main_site_name/files/$1 last;
    try_files $uri =404;
  }

  ###
  ### Sub-location to support H.264/AAC files with short URIs.
  ###
  location ~* /files/.+\.(?:mp4|m4a)$ {
    mp4;
    mp4_buffer_size 1m;
    mp4_max_buffer_size 5m;
    expires 30d;
    access_log off;
    log_not_found off;
    rewrite ^/files/(.*)$  /sites/$main_site_name/files/$1 last;
    try_files $uri =404;
  }

  ###
  ### Sub-location to support files/css with short URIs.
  ###
  location ~* /files/css/(.*)$ {
    access_log off;
    log_not_found off;
    expires max;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header Cache-Control "public";
    rewrite ^/files/(.*)$  /sites/$main_site_name/files/$1 last;
    try_files /sites/$main_site_name/files/css/$1 $uri @drupal;
  }

  ###
  ### Sub-location to support files/js with short URIs.
  ###
  location ~* /files/js/(.*)$ {
    access_log off;
    log_not_found off;
    expires max;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header Cache-Control "public";
    rewrite ^/files/(.*)$  /sites/$main_site_name/files/$1 last;
    try_files /sites/$main_site_name/files/js/$1 $uri @drupal;
  }

  ###
  ### Sub-location to support files/styles with short URIs.
  ###
  location ~* /files/(css|js|styles)/(.*)$ {
    access_log off;
    log_not_found off;
    expires max;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header Cache-Control "public";
    rewrite ^/files/(.*)$  /sites/$main_site_name/files/$1 last;
    try_files /sites/$main_site_name/files/$1/$2 $uri @drupal;
  }

  ###
  ### Sub-location to support files/imagecache with short URIs.
  ###
  location ~* /files/imagecache/(.*)$ {
    access_log off;
    log_not_found off;
    expires max;
    # fix common problems with old paths after import from standalone to Aegir multisite
    rewrite ^/files/imagecache/(.*)/sites/default/files/(.*)$ /sites/$main_site_name/files/imagecache/$1/$2 last;
    rewrite ^/files/imagecache/(.*)/files/(.*)$               /sites/$main_site_name/files/imagecache/$1/$2 last;
    rewrite ^/sites/(.*)/files/imagecache/(.*)/sites/(.*)/files/(.*)$ /sites/$main_site_name/files/imagecache/$2/$4 last;
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header Cache-Control "public";
    rewrite ^/files/(.*)$  /sites/$main_site_name/files/$1 last;
    try_files /sites/$main_site_name/files/imagecache/$1 $uri @drupal;
  }

  location ~* ^.+\.(?:pdf|jpe?g|gif|png|ico|webp|bmp|svg|swf|docx?|xlsx?|pptx?|tiff?|txt|rtf|vcard|vcf|bat|dll|class|otf|ttf|woff2?|eot|less|avi|mpe?g|mov|wmv|mp3|ogg|ogv|wav|midi|zip|tar|t?gz|rar|dmg|exe|apk|pxl|ipa|css|js|map)$ {
    expires 30d;
    access_log off;
    log_not_found off;
    rewrite ^/files/(.*)$  /sites/$main_site_name/files/$1 last;
    try_files $uri =404;
  }
  try_files $uri @cache;
}

###
### Map /downloads/ shortcut early to avoid overrides in other locations.
###
location ^~ /downloads/ {
  location ~* ^.+\.(?:pdf|jpe?g|gif|png|ico|webp|bmp|svg|swf|docx?|xlsx?|pptx?|tiff?|txt|rtf|vcard|vcf|bat|dll|class|otf|ttf|woff2?|eot|less|avi|mpe?g|mov|wmv|mp3|ogg|ogv|wav|midi|zip|tar|t?gz|rar|dmg|exe|apk|pxl|ipa|map)$ {
    expires 30d;
    access_log off;
    log_not_found off;
    rewrite ^/downloads/(.*)$  /sites/$main_site_name/files/downloads/$1 last;
    try_files $uri =404;
  }
  try_files $uri @cache;
}

###
### Serve & no-log static files & images directly,
### without all standard drupal rewrites, php-fpm etc.
###
location ~* ^.+\.(?:pdf|jpe?g|gif|png|ico|webp|bmp|svg|swf|docx?|xlsx?|pptx?|tiff?|txt|rtf|vcard|vcf|bat|dll|class|otf|ttf|woff2?|eot|less|avi|mpe?g|mov|wmv|mp3|ogg|ogv|wav|midi|zip|tar|t?gz|rar|dmg|exe|apk|pxl|ipa|map)$ {
  expires 30d;
  access_log off;
  log_not_found off;
  rewrite ^/images/(.*)$  /sites/$main_site_name/files/images/$1 last;
  rewrite ^/.+/sites/.+/files/(.*)$  /sites/$main_site_name/files/$1 last;
  try_files $uri =404;
}

###
### Serve bigger media/static/archive files directly,
### without all standard drupal rewrites, php-fpm etc.
###
location ~* ^.+\.(?:avi|mpe?g|mov|wmv|ogg|ogv|webm|zip|tar|t?gz|rar|dmg|exe|apk|pxl|ipa)$ {
  expires 30d;
  access_log off;
  log_not_found off;
  rewrite ^/.+/sites/.+/files/(.*)$  /sites/$main_site_name/files/$1 last;
  try_files $uri =404;
}

###
### Serve & no-log some static files directly,
### but only from the files directory to not break
### dynamically created pdf files or redirects for
### legacy URLs with asp/aspx extension.
###
location ~* ^/sites/.+/files/.+\.(?:pdf|aspx?)$ {
  expires 30d;
  access_log off;
  log_not_found off;
  try_files $uri =404;
}

###
### Pseudo-streaming server-side support for Flash Video (FLV) files.
###
location ~* ^.+\.flv$ {
  flv;
  expires 30d;
  access_log off;
  log_not_found off;
  try_files $uri =404;
}

###
### Pseudo-streaming server-side support for H.264/AAC files.
###
location ~* ^.+\.(?:mp4|m4a)$ {
  mp4;
  mp4_buffer_size 1m;
  mp4_max_buffer_size 5m;
  expires 30d;
  access_log off;
  log_not_found off;
  try_files $uri =404;
}

###
### Serve & no-log some static files as is, without forcing default_type.
###
location ~* /(?:cross-?domain)\.xml$ {
  access_log off;
  log_not_found off;
  expires 30d;
  try_files $uri =404;
}

###
### Allow some known php files (like serve.php in the ad module).
###
location ~* /(?:modules|libraries)/(?:contrib/)?(?:ad|tinybrowser|f?ckeditor|tinymce|wysiwyg_spellcheck|ecc|civicrm|fbconnect|radioactivity|statistics)/.*\.php$ {
  limit_conn limreq 88;
  access_log off;
  log_not_found off;
  if ( $is_bot ) {
    return 444;
  }
  try_files $uri =404;
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
location ~* /(?:ahah|ajax|batch|autocomplete|progress/|x-progress-id|js/.*) {
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Serve & no-log static helper files used in some wysiwyg editors.
###
location ~* ^/sites/.*/(?:modules|libraries)/(?:contrib/)?(?:tinybrowser|f?ckeditor|tinymce|flowplayer|jwplayer|videomanager)/.*\.(?:html?|xml)$ {
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  expires 30d;
  try_files $uri =404;
}

###
### Serve & no-log any not specified above static files directly.
###
location ~* ^/sites/.*/files/ {
  access_log off;
  log_not_found off;
  expires 30d;
  try_files $uri =404;
}

###
### Make feeds compatible with boost caching and set correct mime type.
###
location ~* \.xml$ {
  location ~* ^/autodiscover/autodiscover\.xml {
    access_log off;
    log_not_found off;
    return 400;
  }
  if ( $request_method = POST ) {
    return 405;
  }
  if ( $cache_uid ) {
    return 405;
  }
  error_page 405 = @drupal;
  access_log off;
  log_not_found off;
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
  add_header X-Header "Boost Citrus 1.0";
  add_header Expires "Tue, 24 Jan 1984 08:00:00 GMT";
  add_header Cache-Control "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";
  charset utf-8;
  types { }
  default_type text/xml;
  try_files /cache/normal/$host${uri}_.xml /cache/normal/$host${uri}_.html $uri @drupal;
}

###
### Deny bots on never cached uri.
###
location ~* ^/(?:admin|user|cart|checkout|logout) {
  if ( $is_bot ) {
    return 444;
  }
  set $nocache_details "Skip";
  try_files $uri @drupal;
}
location ~* ^/\w\w/(?:admin|user|cart|checkout|logout) {
  if ( $is_bot ) {
    return 444;
  }
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Protect from DoS attempts on never cached uri.
###
location ~* ^/(?:.*/)?(?:node/[0-9]+/edit|node/add|comment/reply) {
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Protect from DoS attempts on never cached uri.
###
location ~* ^/(?:.*/)?(?:node/[0-9]+/delete|approve) {
  if ($cache_uid = '') {
    return 403;
  }
  if ( $is_bot ) {
    return 444;
  }
  access_log off;
  log_not_found off;
  set $nocache_details "Skip";
  try_files $uri @drupal;
}

###
### Support for ESI microcaching: http://groups.drupal.org/node/197478.
###
### This may enhance not only anonymous visitors, but also
### logged in users experience, as it allows you to separate
### microcache for ESI/SSI includes (valid for just 5 seconds)
### from both default Speed Booster cache for anonymous visitors
### (valid by default for 10s or 1h, unless purged on demand via
### recently introduced Purge/Expire modules) and also from
### Speed Booster cache per logged in user (valid for 10 seconds).
###
### Now you have three different levels of Speed Booster cache
### to leverage and deliver the 'live content' experience for
### all visitors, and still protect your server from DoS or
### simply high load caused by unexpected high traffic etc.
###
location ~ ^/(?<esi>esi/.*)"$ {
  ssi on;
  ssi_silent_errors on;
  internal;
  limit_conn limreq 888;
  add_header Cache-Control "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";
  ###
  ### Set correct, local $uri.
  ###
  fastcgi_param QUERY_STRING q=$esi;
  fastcgi_param SCRIPT_FILENAME $document_root/index.php;
  fastcgi_param HTTP_HOST $host;
  ### This location declares its own minimal param set (no fastcgi_params
  ### include, no inheritance), so the scheme must be passed explicitly.
  fastcgi_param REQUEST_SCHEME $scheme;
<?php if ($satellite_mode == 'boa'): ?>
  fastcgi_pass  unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
  fastcgi_pass  127.0.0.1:9000;
<?php else: ?>
  fastcgi_pass  unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>
  ###
  ### Use Nginx cache for all visitors.
  ###
  set $nocache "";
  if ( $http_cookie ~* "NoCacheID" ) {
    set $nocache "NoCache";
  }
  fastcgi_cache speed;
  fastcgi_cache_methods GET HEAD;
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
  fastcgi_cache_use_stale error http_500 invalid_header timeout updating;
  expires epoch;
}

###
### Workaround for https://www.drupal.org/node/2599326.
###
if ( $args ~* "/autocomplete/" ) {
  return 405;
}
error_page 405 = @drupal;

###
### Catch all unspecified requests.
###
location / {
  if ( $http_user_agent ~* wget ) {
    return 444;
  }
  ###
  ### Allow but rate-limit AI search/index, user-triggered and utility bots on
  ### the main content surface.  Empty-key maps mean only these AI classes are
  ### counted; all other traffic is unaffected.  Keyed per vendor (UA-derived
  ### $ai_*_limit_key), NOT per client IP: one assistant job fans out across
  ### many IPs, so only a shared per-vendor key caps the aggregate.  Training
  ### and forged AI are already 444'd above.
  ###
  limit_req zone=ai_search  burst=20 nodelay;
  limit_req zone=ai_user    burst=20 nodelay;
  limit_req zone=ai_utility burst=10 nodelay;
  limit_req_status 444;
  try_files $uri @cache;
}

###
### Boost compatible cache check.
###
location @cache {
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
  error_page 405 = @drupal;
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;
  add_header X-Header "Boost Citrus 1.0";
  add_header Expires "Tue, 24 Jan 1984 08:00:00 GMT";
  add_header Cache-Control "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";
  charset utf-8;
  try_files /cache/normal/$host${uri}_$args.html @drupal;
}

###
### Send all not cached requests to drupal with clean URLs support.
###
location @drupal {

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
  error_page 402 = @legacy;
  if ( $core_detected = Legacy ) {
    return 402;
  }
  error_page 406 = @regular;
  if ( $core_detected = Regular ) {
    return 406;
  }
  error_page 418 = @modern;
  if ( $core_detected = Modern ) {
    return 418;
  }

  ###
  ### Fallback to regular / D7 style rewrite
  ###
  set $location_detected "Fallback";
  rewrite ^ /index.php?$query_string? last;
}

###
### Special location for Drupal 6.
###
location @legacy {
  set $location_detected "Legacy";
  rewrite ^/(.*)$ /index.php?q=$1 last;
}

###
### Special location for Drupal 7.
###
location @regular {
  set $location_detected "Regular";
  rewrite ^ /index.php?$query_string? last;
}

###
### Special location for Drupal 8+.
###
location @modern {
  set $location_detected "Modern";
  try_files $uri /index.php?$query_string;
}

###
### Send all non-static requests to php-fpm, restricted to known php file.
###
location = /index.php {

  limit_conn limreq 88;

  ###
  ### Tier-A distributed-i18n-flood guardrail.  Every dynamic request — including
  ### localized clean URLs after the internal rewrite to /index.php — funnels
  ### through this location, so it is the single chokepoint where the anonymous
  ### localized request class can be capped before it reaches php-fpm.  The
  ### $boa_i18n_anon_key (http{} maps in server.tpl.php) is non-empty only for an
  ### anonymous request to a localized path on a guarded vhost (guarding is on by
  ### default; a host can be opted out), so English, authenticated and opted-out
  ### traffic is never counted.  Bounding the
  ### IN-FLIGHT count of this class per vhost bounds the share of the shared
  ### per-account FPM pool a distributed translation-path flood can ever hold.
  ### Default 24 (~1/8 of a 192-worker pool); tune via the nginx_i18n_anon_conn
  ### option.  444 (set below) sheds excess instantly, at ~no cost.  Static files
  ### under /xx/ never reach here (served by their own locations), so they are
  ### correctly excluded.
  ###
<?php
  $i18n_anon_conn = (int) drush_get_option('nginx_i18n_anon_conn', 24);
  if ($i18n_anon_conn < 1) {
    $i18n_anon_conn = 24;
  }
?>
  limit_conn boa_i18n_anon <?php print $i18n_anon_conn; ?>;
  limit_conn_status 444;

<?php
  $perhost_zone_ok = isset($boa_zones_body)
    && strpos($boa_zones_body, 'zone=boa_perhost_anon') !== FALSE;
  $perhost_anon_conn = (int) drush_get_option('nginx_perhost_anon_conn', 100);
  if ($perhost_anon_conn < 1 || $perhost_anon_conn > 65535) {
    $perhost_anon_conn = 100;
  }
  if ($perhost_zone_ok):
?>
  ###
  ### General anonymous-render guardrail.  The i18n cap above bounds one
  ### expensive request class; this one bounds the page-render total.  (Scope,
  ### stated precisely: every PAGE request lands here, including clean URLs
  ### after the internal rewrite through @drupal/@legacy/@regular/@modern.
  ### Other fastcgi entry points in this file -- cron, xmlrpc, the ESI
  ### microcache and friends -- have their own locations and are NOT counted.)  An observed
  ### distributed scraper swarm presented a single spoofed browser user-agent
  ### across ~270 client IPs at ~1.4 requests each against one vhost, every
  ### response a 200 from ordinary content routes: not an AI vendor, not a
  ### declared bot (so it received the short human cache window rather than the
  ### long crawler one), not bad-status, and far too thin per IP for any per-IP
  ### control.  The renders piled up until each took longer than the front
  ### cache's own TTL and lock timeout, at which point waiting requests stopped
  ### waiting and rendered too — a feedback loop the cache cannot break out of
  ### by itself.  Capping the IN-FLIGHT anonymous render count per vhost keeps
  ### render time inside that horizon, which is what keeps the front cache
  ### effective; it is deliberately the only control here that does not
  ### classify the client, because that class cannot be classified.
  ###
  ### $boa_perhost_anon_key and the zone are declared in the BOA-written
  ### http-scope file, NOT in the master render, and this consumer appears only
  ### when that file is present with the expected zone -- so no delivery order
  ### can produce an undeclared-zone reference, which matters because a missing
  ### zone is a whole-box nginx [emerg] and the upgrade path restarts nginx
  ### without a configtest.  (Same contract as the bgp_flood zone above.)
  ### The key is $host for anonymous requests and EMPTY for authenticated ones,
  ### so an editor or admin is never shed while a flood is being trimmed.
  ###
  ### The shed status is the location-wide 444 set above, NOT 503:
  ### limit_conn_status is one-per-context, and the scan_nginx i18n detector
  ### counts those 444s (_NGINX_I18N_FLOOD_C444_THRESHOLD) as its Tier-A
  ### shedding signal — forcing 503 here would silently blind that detector.
  ### Shedding at 444 also keeps this guardrail visible to the IDS.
  ###
  ### Default 100: above the busiest legitimate per-vhost in-flight peak
  ### measured on a production box (13-57 across every tenant over a full day)
  ### with headroom, and far below an observed flood (417).  Deliberately loose
  ### so it only ever bounds a genuine flood; tune toward ~1.5x the instance's
  ### FPM pool pm.max_children via the nginx_perhost_anon_conn option (the
  ### render cannot read the pool size, so this cannot be derived here).
  ###
  limit_conn boa_perhost_anon <?php print $perhost_anon_conn; ?>;
<?php endif; ?>

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
  ### Add headers for debugging
  ###
  add_header X-Debug-NoCache-Switch "$nocache";
  add_header X-Debug-NoCache-Auth "$http_authorization";
  add_header X-Debug-NoCache-Cookie "$cookie_NoCacheID";
  add_header X-Device "$device";
  add_header X-GeoIP-Country-Code "$geoip_country_code";
  add_header X-GeoIP-Country-Name "$geoip_country_name";
  add_header X-Core-Variant "$core_detected";
  add_header X-Loc-Where "$location_detected";
  add_header X-Http-Pragma "$http_pragma";
  add_header X-Arg-Nocache "$arg_nocache";
  add_header X-Arg-Comment "$arg_comment";
  add_header X-Speed-Cache "$upstream_cache_status";
  add_header X-Speed-Cache-UID "$cache_uid";
  add_header X-Speed-Cache-Key "$key_uri";
  add_header X-NoCache "$nocache_details";
  add_header X-This-Proto "$http_x_forwarded_proto";
  add_header X-Server-Name "$main_site_name";

<?php if ($nginx_has_http3): ?>
  add_header Alt-Svc 'h3=":443"; ma=86400';
<?php endif; ?>

  add_header Cache-Control "no-store, no-cache, must-revalidate, post-check=0, pre-check=0";

  ###
  ### Basic security/privacy headers.
  ###
  add_header Referrer-Policy "no-referrer-when-downgrade";

  try_files $uri =404; ### check for existence of php file first

  ###
  ### FastCGI
  ###
<?php if ($satellite_mode == 'boa'): ?>
  fastcgi_pass  unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
  fastcgi_pass  127.0.0.1:9000;
<?php else: ?>
  fastcgi_pass  unix:<?php print $phpfpm_socket_path; ?>;
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
  fastcgi_cache_use_stale error http_500 invalid_header timeout updating;
}

###
### Send other known php requests/files to php-fpm without any caching.
###
location ~* ^/(?:core/)?(?:boost_stats|rtoc|js)\.php$ {
  limit_conn limreq 88;
  if ( $is_bot ) {
    return 404;
  }
  access_log off;
  log_not_found off;
  try_files $uri =404; ### check for existence of php file first
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
location ~ ^/update.php {
  error_page 418 = @allowupdate;
  if ( $cache_uid ) {
    return 418;
  }
  return 404;
}

###
### Allow access to Backdrop /core/update.php only for logged in admin user.
###
location ~ ^/core/update.php {
  error_page 418 = @allowupdate;
  if ( $cache_uid ) {
    return 418;
  }
  return 404;
}

###
### Allow access to /authorize.php only for logged in admin user.
###
location ~ ^/authorize.php {
  error_page 418 = @allowauthorize;
  if ( $cache_uid ) {
    return 418;
  }
  return 404;
}

###
### Internal location for /update.php restricted access.
###
location @allowupdate {
  fastcgi_split_path_info ^(.+\.php)(/.+)$;
  fastcgi_index update.php;
  fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
  fastcgi_intercept_errors on;
  include fastcgi_params;
  fastcgi_param HTTP_HOST $host;
  fastcgi_param REQUEST_SCHEME $scheme;
  limit_conn limreq 8;
<?php if ($satellite_mode == 'boa'): ?>
  fastcgi_pass unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
  fastcgi_pass 127.0.0.1:9000;
<?php else: ?>
  fastcgi_pass unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>
}

###
### Internal location for /authorize.php and restricted access.
###
location @allowauthorize {
  fastcgi_split_path_info ^(.+\.php)(/.+)$;
  fastcgi_index authorize.php;
  fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
  fastcgi_intercept_errors on;
  include fastcgi_params;
  fastcgi_param HTTP_HOST $host;
  fastcgi_param REQUEST_SCHEME $scheme;
  limit_conn limreq 8;
<?php if ($satellite_mode == 'boa'): ?>
  fastcgi_pass unix:/run/$user_socket.fpm.socket;
<?php elseif ($phpfpm_mode == 'port'): ?>
  fastcgi_pass 127.0.0.1:9000;
<?php else: ?>
  fastcgi_pass unix:<?php print $phpfpm_socket_path; ?>;
<?php endif; ?>
}

###
### Deny access to any not listed above php files with 404 error.
###
location ~* ^.+\.php$ {
  return 404;
}

#######################################################
###  nginx.conf site level extended vhost include end
#######################################################
