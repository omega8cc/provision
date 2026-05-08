# Aegir web server main configuration file

#######################################################
###  nginx.conf main
#######################################################

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

if ($nginx_is_modern) {
  print "  limit_conn_zone \$binary_remote_addr zone=limreq:10m;\n";
  print "  limit_req_zone  \$binary_remote_addr zone=search_limit:10m rate=3r/s;\n";
}
else {
  print "  limit_zone limreq \$binary_remote_addr 10m;\n";
}

if ($nginx_has_gzip) {
  print "  gzip_static       on;\n";
}
?>

 ## FastCGI params
  fastcgi_param  SCRIPT_FILENAME     $document_root$fastcgi_script_name;
  fastcgi_param  QUERY_STRING        $query_string;
  fastcgi_param  REQUEST_METHOD      $request_method;
  fastcgi_param  CONTENT_TYPE        $content_type;
  fastcgi_param  CONTENT_LENGTH      $content_length;
  fastcgi_param  SCRIPT_NAME         $fastcgi_script_name;
  fastcgi_param  REQUEST_URI         $request_uri;
  fastcgi_param  DOCUMENT_URI        $document_uri;
  fastcgi_param  DOCUMENT_ROOT       $document_root;
  fastcgi_param  SERVER_PROTOCOL     $server_protocol;
  fastcgi_param  GATEWAY_INTERFACE   CGI/1.1;
  fastcgi_param  SERVER_SOFTWARE     ApacheSolarisNginx/$nginx_version;
  fastcgi_param  REMOTE_ADDR         $remote_addr;
  fastcgi_param  REMOTE_PORT         $remote_port;
  fastcgi_param  SERVER_ADDR         $server_addr;
  fastcgi_param  SERVER_PORT         $server_port;
  fastcgi_param  SERVER_NAME         $server_name;
  fastcgi_param  USER_DEVICE         $device;
  fastcgi_param  GEOIP_COUNTRY_CODE  $geoip_country_code;
  fastcgi_param  GEOIP_COUNTRY_CODE3 $geoip_country_code3;
  fastcgi_param  GEOIP_COUNTRY_NAME  $geoip_country_name;
  fastcgi_param  REDIRECT_STATUS     200;
  fastcgi_index  index.php;
  # Block https://httpoxy.org/ attacks.
  fastcgi_param  HTTP_PROXY          "";

 ## Size Limits
  client_body_buffer_size        64k;
  client_header_buffer_size      32k;
  client_max_body_size          395m;
  connection_pool_size           256;
  fastcgi_buffer_size           512k;
  fastcgi_buffers             512 8k;
  fastcgi_temp_file_write_size  512k;
  large_client_header_buffers 32 64k;
  map_hash_bucket_size           192;
  request_pool_size               4k;
  server_names_hash_bucket_size  512;
  server_names_hash_max_size    8192;
  types_hash_bucket_size         512;
  variables_hash_max_size       2048;
  variables_hash_bucket_size     128;

 ## Timeouts
  client_body_timeout            180;
  client_header_timeout          180;
  send_timeout                   180;
  lingering_time                  30;
  lingering_timeout                5;
  fastcgi_connect_timeout        10s;
  fastcgi_send_timeout          180s;
  fastcgi_read_timeout          180s;

 ## Open File Performance
  open_file_cache max=8000 inactive=30s;
  open_file_cache_valid          99s;
  open_file_cache_min_uses         3;
  open_file_cache_errors          on;

 ## FastCGI Caching
  fastcgi_cache_path /var/lib/nginx/speed
                     levels=2:2
                     keys_zone=speed:10m
                     inactive=15m
                     max_size=3g;

 ## General Options
  ignore_invalid_headers          on;
  recursive_error_pages           on;
  reset_timedout_connection       on;
  fastcgi_intercept_errors        on;
  server_tokens                  off;
  fastcgi_hide_header           Link;
  fastcgi_hide_header    X-Generator;
  fastcgi_hide_header   X-Powered-By;

 ## SSL performance
  ssl_session_cache   shared:SSL:10m;

 ## SSL protocols, ciphers and settings
  ssl_protocols TLSv1.1 TLSv1.2 TLSv1.3;
  ssl_ciphers TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:DHE-RSA-AES256-CCM:DHE-RSA-AES256-CCM8:DHE-RSA-AES128-CCM:DHE-RSA-AES128-CCM8:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:AES128-GCM-SHA256:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-SHA:AES:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA:!ECDHE-ECDSA-AES256-SHA384:!ECDHE-ECDSA-AES128-SHA256;
  ssl_prefer_server_ciphers  on;

  ## GeoIP support
  geoip_country /usr/share/GeoIP/GeoIP.dat;

 ## Compression
  gzip_buffers      16 8k;
  gzip_comp_level   8;
  gzip_http_version 1.0;
  gzip_min_length   100;
  gzip_types
    application/atom+xml
    application/javascript
    application/json
    application/rss+xml
    application/vnd.ms-fontobject
    application/x-font-opentype
    application/x-font-ttf
    application/x-javascript
    application/xhtml+xml
    application/xml
    application/xml+rss
    font/opentype
    image/svg+xml
    image/x-icon
    text/css
    text/javascript
    text/plain
    text/xml;
  gzip_vary         on;
  gzip_proxied      any;

 ## Default index files
  index         index.php index.html;

 ## Log Format
  log_format        main '"$proxy_add_x_forwarded_for" $host [$time_local] '
                         '"$request" $status $body_bytes_sent '
                         '$request_length $bytes_sent "$http_referer" '
                         '"$http_user_agent" $request_time "$gzip_ratio" '
                         'proto="$server_protocol" '
                         'alpn="$ssl_alpn_protocol" '
                         'http2="$http2"';

  client_body_temp_path  /var/lib/nginx/body 1 2;
  access_log             /var/log/nginx/access.log main;

<?php print $extra_config; ?>

  error_log              /var/log/nginx/error.log crit;

#######################################################
###  nginx default maps
#######################################################

###
### Identify AI crawlers.
###
map $http_user_agent $is_ai_crawler {
  default  '';
  ~*Ai2Bot|Amazon|Anthropic|Applebot-Extended|Bytespider|Claude|Cohere-AI|Deepseek|Gemini  is_ai_crawler;
  ~*Google-Extended|GPT|HuggingFace|Meta-ExternalAgent|MistralAI|OAI|OpenAI|Perplexity|xAI is_ai_crawler;
}

###
### Identify crawlers.
###
map $http_user_agent $is_crawler {
  default  '';
  ~*Ahrefs|Amazon|Aspiegel|Automatic|Barkrowler|BrokenLinkCheck|BuzzTrack|bytedance|CCBot|DBot|externalagent|TikTok|openai is_crawler;
  ~*Go-http-client|GSLFbot|HiScan|HTMLParser|HTTrack|IbouBot|ImagesiftBot|Mireo|MJ12|Morfeus|Nutch|Offline|PChomebot|SWEB  is_crawler;
  ~*PECL|perplexity|PetalBot|Pinterest|Riddler|Scrap|Semrush|SEOkicks|serpstatbot|Sistrix|SiteBot|SleepBot|Sogou|Turnitin  is_crawler;
}

###
### DDoS protection: block full-text search without referrer
### Pattern: search_api_views_fulltext or search_api_fulltext param present + no referrer
### Action: 444 (connection close, no response)
###
### Map 1: Detect search_api_views_fulltext and search_api_fulltext in query string
###
map $query_string $has_fulltext_search {
  default                         0;
  "~*search_api_views_fulltext="  1;
  "~*search_api_fulltext="        1;
}
###
### Map 2: Detect empty/absent referrer
###
map $http_referer $has_no_referrer {
  default  0;
  ""       1;
  "-"      1;
}
###
### Map 3: Combine both signals → block decision
### Only blocks when BOTH conditions are true
###
map $has_fulltext_search$has_no_referrer $block_search_no_referrer {
  default 0;
  "11"    1;   # fulltext search=1 AND no referrer=1
}

###
### Block semalt botnet.
###
map $http_referer $is_botnet {
  default  '';
  ~*semalt\.com|kambasoft\.com|savetubevideo\.com|bottlenose\.com|yapoga\.com  is_botnet;
  ~*descargar-musica-gratis\.net|baixar-musicas-gratis\.com                    is_botnet;
}

###
### Deny all known bots/spiders on some URIs.
###
map $http_user_agent $is_bot {
  default  '';
  ~*crawl|bot|spider|tracker|click|parser|google|yahoo|yandex|baidu|bing  is_bot;
}

###
### Deny almost all crawlers under high load.
###
map $http_user_agent $deny_on_high_load {
  default  '';
  ~*crawl|spider|tracker|click|parser|google|yahoo|yandex|baidu|bing  deny_on_high_load;
}

###
### Deny listed requests for security reasons.
###
map $args $is_denied {
  default  '';
  ~*delete.+from|insert.+into|select.+from|union.+select    is_denied;
  ~*onload|\.php.+src|system\(.+|document\.cookie           is_denied;
  ~*\.\./                                                   is_denied;

  # SQL injection timing/blind attacks
  ~*waitfor(%2f|%2a|[\s%2b/\*])+delay                       is_denied;
  ~*declare(%2f|%2a|[\s%2b/\*])+@                           is_denied;

  # Comment-obfuscated injection (/**, used in UA and args)
  ~*/\*\*/                                                  is_denied;

  # Common blind SQLi patterns
  ~*(benchmark|sleep|pg_sleep)\s*\(                         is_denied;
  "~*0x[0-9a-fA-F]{4,}"                                     is_denied;

  # XSS
  ~*<script|javascript:|vbscript:|data:text/html            is_denied;

  # Path traversal (catches both raw and encoded)
  ~*(\.\./|%2[eE]%2[eE]%2[fF]|%252[eE])                     is_denied;
}

###
### Also check the UA string for injection patterns (attack may have
### embedded the WAITFOR payload inside the User-Agent header itself).
###
map $http_user_agent $ua_denied {
  default  '';
  ~*/\*\*/                                                  ua_denied;
  ~*waitfor([/\*]|\s\+)+delay                               ua_denied;
  ~*declare([/\*]|\s\+)+@                                   ua_denied;
  ~*(benchmark|sleep)\s*\(                                  ua_denied;
}

###
### Detect TLS ClientHello sent to a plain HTTP port.
###
map $request $tls_on_plain {
  default '';
  ~*^\x16\x03 tls_on_plain;
}

###
### Live switch of $key_uri for Speed Booster cache depending on $args.
###
map $request_uri $key_uri {
  default                                                                            $request_uri;
  ~(?<no_args_uri>[[:graph:]]+)\?(.*)(utm_|__utm|_campaign|gclid|source=|adv=|req=)  $no_args_uri;
}

###
### Detect “node-chain” URL mutation spam.
### Examples:
### /node/1771/pl/node/1771/es/node/1771/...
### /pl/node/1771/es/node/1771/...
###
map $uri $is_node_chain {
  default 0;

  # optional lang prefix, then node/<id>, then at least one more ".../node/<id>"
  ~*^/([a-z][a-z](-[a-z0-9]+)?/)?node/[0-9]+(/([a-z][a-z](-[a-z0-9]+)?/)?node/[0-9]+)+  1;

  # fallback: node/<id> appears 2+ times anywhere
  ~*/node/[0-9]+.*/node/[0-9]+  1;
}

###
### Detect “language-prefix chain” URL mutation spam.
### Examples (3+ language-like prefixes in a row):
### /pl/en/fr/de/office/city-benelux
### /pt-br/es/it/nl/product/ai-driven-project-manager
### /zh-hans/ja/ko/en/node/1771
### /en/en/en/en/anything
###
map $uri $is_lang_chain {
  default 0;

  # 3+ leading language-like segments: /xx/ or /xx-xxxx/
  ~*^/([a-z][a-z](-[a-z0-9]+)?/)([a-z][a-z](-[a-z0-9]+)?/)([a-z][a-z](-[a-z0-9]+)?/)([a-z][a-z](-[a-z0-9]+)?/)*  1;
}

###
### Support separate Speed Booster caches for various mobile devices.
###
map $http_user_agent $device {
  default                                                                normal;
  ~*Nokia|BlackBerry.+MIDP|240x|320x|Palm|NetFront|Symbian|SonyEricsson  mobile-other;
  ~*iPhone|iPod|Android|BlackBerry.+AppleWebKit                          mobile-smart;
  ~*iPad|Tablet                                                          mobile-tablet;
}

###
### Set a cache_uid variable for authenticated users (by @brianmercer and @perusio, fixed by @omega8cc).
###
map $http_cookie $cache_uid {
  default  '';
  ~SESS[[:alnum:]]+=(?<session_id>[[:graph:]]+)  $session_id;
}

#######################################################
###  nginx default server
#######################################################

server {
  listen  *:<?php print $http_port; ?>;
  server_name  _;
  location / {
    expires 99s;
    add_header Cache-Control "public, must-revalidate, proxy-revalidate";
    root  /var/www/nginx-default;
    index  index.html index.htm;
  }
}

server {
  listen  *:<?php print $http_port; ?>;
  server_name  127.0.0.1;
  location /nginx_status {
    stub_status on;
    access_log off;
    log_not_found off;
    allow 127.0.0.1;
    deny all;
  }
}

#######################################################
###  nginx virtual domains
#######################################################

# virtual hosts
include  <?php print $http_pred_path ?>/*;
include  <?php print $http_platformd_path ?>/*;
include  <?php print $http_vhostd_path ?>/*;
include  <?php print $http_postd_path ?>/*;
