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
  # Per-vhost global search rate cap.
  # Keyed on $host so the ceiling applies across ALL source IPs collectively.
  # This is the primary defence against distributed one-request-per-IP search
  # floods that never exceed per-IP limits but saturate backend Solr/PHP-FPM.
  # 20 req/s with burst=40 allows normal interactive use while capping botnet
  # throughput. Tune upward for high-traffic public search pages.
  print "  limit_req_zone  \$host               zone=search_flood:1m  rate=20r/s;\n";
  # AI bot soft limits — allow but throttle these classes; tune after reviewing
  # logs.  The empty-key maps ($ai_*_limit_key) count only the matching AI
  # class, so no other traffic is ever rate-limited by these zones.
  print "  limit_req_zone  \$ai_search_limit_key  zone=ai_search:10m  rate=1r/s;\n";
  print "  limit_req_zone  \$ai_user_limit_key    zone=ai_user:10m    rate=2r/s;\n";
  print "  limit_req_zone  \$ai_utility_limit_key zone=ai_utility:5m  rate=1r/s;\n";
  # Tier-A distributed-i18n-flood guardrail.  Bounds the IN-FLIGHT count of
  # anonymous localized (i18n) requests per vhost (on by default; see the
  # $boa_i18n_anon_key maps below).  Keyed on a constant per vhost ($host), NOT
  # the client IP: a localized-page scraper spreads across thousands of IPs at
  # one or two requests each, so per-IP limits never trip; only an aggregate
  # per-vhost cap bounds the expensive translation class's share of the shared
  # per-account FPM pool.  Empty key (the common case) is not counted, so all
  # other traffic is unaffected.
  print "  limit_conn_zone \$boa_i18n_anon_key    zone=boa_i18n_anon:10m;\n";
}
else {
  print "  limit_zone limreq \$binary_remote_addr 10m;\n";
}

# Resolve the real client IP on Cloudflare-fronted vhosts so rate-limit keys,
# REMOTE_ADDR and access logs reflect the visitor and not the CF edge.  Trusted
# CF source ranges are supplied by a BOA-managed wildcard include, so a missing
# file never breaks `nginx -t`; with no trusted ranges the CF-Connecting-IP
# header is ignored and $remote_addr is left unchanged (no spoofing risk).
print "  real_ip_header    CF-Connecting-IP;\n";
print "  real_ip_recursive on;\n";
print "  include /data/conf/nginx_cloudflare_real_ip.c*;\n";

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
  # $realip_remote_addr = the original TCP peer (CF edge): keeps PHP global.inc's
  # own real-client resolution correct, while nginx $remote_addr is realip-
  # rewritten to the real client for rate-limit keys, logs and deny.
  fastcgi_param  REMOTE_ADDR         $realip_remote_addr;
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
  # Host field is the realip-resolved client ($remote_addr), so GoAccess's
  # "~h{, }" (which takes the FIRST token) and scan_nginx both report the real
  # visitor, not the spoofable X-Forwarded-For head. The full chain is preserved
  # as a trailing xff= field, ignored by GoAccess just like proto=/alpn=/http2=.
  log_format        main '"$remote_addr" $host [$time_local] '
                         '"$request" $status $body_bytes_sent '
                         '$request_length $bytes_sent "$http_referer" '
                         '"$http_user_agent" $request_time "$gzip_ratio" '
                         'proto="$server_protocol" '
                         'alpn="$ssl_alpn_protocol" '
                         'http2="$http2" '
                         'xff="$proxy_add_x_forwarded_for"';

  client_body_temp_path  /var/lib/nginx/body 1 2;
  access_log             /var/log/nginx/access.log main;

<?php print $extra_config; ?>

  error_log              /var/log/nginx/error.log crit;

#######################################################
###  nginx default maps
#######################################################

###
### AI traffic is split into mutually-exclusive classes instead of one broad
### "AI crawler" map.  The old single map collapsed training scrapers, AI search
### crawlers and user-triggered fetchers into one deny, which made hosted sites
### invisible to AI search and AI-assisted browsing.  Each class below matches
### FULL distinctive tokens only — never bare vendor names — so GPTBot is not
### ChatGPT-User, ClaudeBot is not Claude-User/Claude-SearchBot, Perplexitybot
### is not Perplexity-User, Googlebot is not Google-Extended, etc.
###
### Default policy (Stage 1, global):
###   training -> blocked      (per-site opt-in to allow,  BOA Stage 2)
###   search   -> allow + rl   (per-site opt-in to block,  BOA Stage 2)
###   user     -> allow + rl   (per-site opt-in to block,  BOA Stage 2)
###   utility  -> allow + rl   (per-site opt-in to block,  BOA Stage 2)
###   forged   -> blocked      (universal, zero false positive)
###

###
### Backward-compatibility shim — DO NOT REMOVE.  $is_ai_crawler was the old single
### broad AI map.  It is kept DEFINED (original broad match) because a barracuda
### upgrade re-renders this master http config BEFORE the Octopus instance vhosts:
### an instance vhost rendered by the previous Provision still references
### $is_ai_crawler, and an http config that no longer defines it makes `nginx -t`
### fail with `unknown "is_ai_crawler" variable`, taking EVERY instance down until
### each is individually re-rendered.  Shared http-block map variables must remain a
### superset across versions — never remove/rename one a deployed vhost may use.
### Retire only once no rendered vhost references it anywhere.
###
map $http_user_agent $is_ai_crawler {
  default  '';
  ~*Ai2Bot|Amazon|Anthropic|Applebot-Extended|Bytespider|Claude|Cohere-AI|Deepseek|Gemini  is_ai_crawler;
  ~*Google-Extended|GPT|HuggingFace|Meta-ExternalAgent|MistralAI|OAI|OpenAI|Perplexity|xAI is_ai_crawler;
}

###
### AI training / bulk-collection crawlers — blocked globally by default.
###
map $http_user_agent $is_ai_training {
  default  '';
  ~*GPTBot|ClaudeBot|Claude-Web|anthropic-ai|CCBot|Bytespider|Amazonbot|AI2Bot|Diffbot  is_ai_training;
  ~*Meta-ExternalAgent|cohere-ai|omgili                                                  is_ai_training;
}

###
### AI search / citation-index crawlers — allowed (rate-limited); these drive
### visibility in AI search answers.
###
map $http_user_agent $is_ai_search {
  default  '';
  ~*OAI-SearchBot|Claude-SearchBot|PerplexityBot|MistralAI-Index|YouBot|Google-CloudVertexBot  is_ai_search;
}

###
### User-triggered AI fetchers (a real user asked an assistant to read a page)
### — HONEST members only: allowed, under the per-vendor aggregate rate-limit
### below.  They identify truthfully and do not evade a block; some (ChatGPT-User,
### Meta-ExternalFetcher) fan a single user prompt out across many source IPs,
### which is exactly why the limit is a per-vendor AGGREGATE, not per-IP.
### Evasive user-fetchers (Perplexity-User) are split into $is_ai_evasive below.
###
map $http_user_agent $is_ai_user {
  default  '';
  ~*ChatGPT-User|Claude-User|MistralAI-User|Meta-ExternalFetcher|Google-?Agent  is_ai_user;
}

###
### Evasive AI user-fetchers.  User-triggered, but they IGNORE robots.txt by
### stated policy and, when blocked, DROP their declared UA and rotate IPs/ASNs
### (Cloudflare de-listed Perplexity as a verified bot for this).  Blocked by
### default; a per-site opt-in clears it (see $ai_evasive_allow in the vhost
### guard chain).  A UA block is best-effort here — the IDS/csf layer is the
### real backstop against the undeclared/rotating traffic.
###
map $http_user_agent $is_ai_evasive {
  default  '';
  ~*Perplexity-User  is_ai_evasive;
}

###
### AI utility bots (ads validation, text-to-speech, assistant retrieval)
### — allowed (rate-limited).
###
map $http_user_agent $is_ai_utility {
  default  '';
  ~*OAI-AdsBot|DuckAssistBot|Google-Read-Aloud|Google-NotebookLM  is_ai_utility;
}

###
### Forged AI user-agents.  Google-Extended and Applebot-Extended are
### robots.txt-only opt-out tokens that real crawlers NEVER send in a UA, so
### their presence proves forgery.  Hard-blocked globally (zero false positive).
###
map $http_user_agent $is_ai_forged {
  default  '';
  ~*Google-Extended|Applebot-Extended  is_ai_forged;
}

###
### AI bot rate-limit keys.  An empty key is not counted by its zone, so each
### zone counts only its own AI class and all other traffic is untouched.
### Keyed PER VENDOR (a constant per UA token), NOT per client IP: a single
### assistant prompt fans out across many source IPs, so a per-IP cap never
### bites the aggregate — each IP stays under the limit and nothing throttles.
### One shared key per vendor makes the zone cap that vendor's TOTAL rate across
### every IP and vhost, and gives each vendor its own bucket so no one vendor
### can starve another's allowance.  Each roster below MUST track the matching
### $is_ai_* class map above: a token in one but not the other is silently
### un-rate-limited or (for the class map) un-blockable.
###
map $http_user_agent $ai_search_limit_key {
  default                  '';
  ~*OAI-SearchBot          oai_searchbot;
  ~*Claude-SearchBot       claude_searchbot;
  ~*PerplexityBot          perplexitybot;
  ~*MistralAI-Index        mistralai_index;
  ~*YouBot                 youbot;
  ~*Google-CloudVertexBot  google_vertexbot;
}

map $http_user_agent $ai_user_limit_key {
  default                 '';
  ~*ChatGPT-User          chatgpt_user;
  ~*Claude-User           claude_user;
  ~*MistralAI-User        mistralai_user;
  ~*Meta-ExternalFetcher  meta_fetcher;
  ~*Google-?Agent         google_agent;
}

map $http_user_agent $ai_utility_limit_key {
  default              '';
  ~*OAI-AdsBot         oai_adsbot;
  ~*DuckAssistBot      duckassistbot;
  ~*Google-Read-Aloud  google_readaloud;
  ~*Google-NotebookLM  google_notebooklm;
}

###
### Probes for secret / config paths.  These never exist on a hosted
### Drupal/Aegir site, so they are hard-blocked globally regardless of UA.
### Matched on $uri (decoded, normalised) and anchored to path segments so a
### literal ".env" cannot match an arbitrary substring.  $uri normalisation
### also defends against percent-encoded evasion (e.g. /%2eenv -> /.env).
###
map $uri $is_secret_path {
  default  '';
  ~*(?:^|/)\.(?:env|git|aws|ssh)(?:[./]|$)                                       is_secret_path;
  ~*(?:^|/)(?:secrets|key|google-services|config|loadable-stats)\.json(?:$|/)    is_secret_path;
  ~*(?:^|/)application\.ya?ml(?:$|/)                                             is_secret_path;
  ~*(?:^|/)settings\.py(?:$|/)                                                   is_secret_path;
  ~*(?:^|/)__/firebase/init\.json(?:$|/)                                         is_secret_path;
  ~*(?:^|/)\.next/required-server-files\.json(?:$|/)                             is_secret_path;
}

###
### Deny probes for foreign-CMS admin paths (WordPress / Joomla / phpMyAdmin
### tokens that can never exist on a Drupal / Backdrop / Aegir-Hostmaster
### docroot, regardless of UA).  Without this the extensionless variant of such
### a probe (e.g. /cms/wp-admin, /ru/administrator) misses every static
### location, falls through try_files -> @drupal -> /index.php -> php-fpm, and
### costs a full Drupal bootstrap just to render a 404 — the exact sink that
### let a distributed auth-probe flood saturate a small VM's FPM pool.  444
### (drop, no bootstrap) instead, which also feeds the per-IP 444 counter in the
### scan_nginx IDS (status 444 is scored; 301/extensionless-404 routing is not).
###
### Tokens are matched only as whole path segments (followed by / . ? or end),
### so a legitimate alias such as /wp-content-strategy or /site-administrator
### does NOT match.  Generic auth words (login, signin, admin, user, account)
### are deliberately NOT matched here — they collide with real customer URL
### namespaces and with Drupal's own /admin, /user; the distributed tail that
### uses them is handled in aggregate by the scan_nginx UA-burst detector, not
### at this shared edge.  "adminer" is intentionally absent: BOA ships Adminer.
###
map $uri $is_cms_probe {
  default  '';
  ~*(?:^|/)wp-(?:admin|login|content|includes|json|config|cron|signup|mail|register|links-opml|trackback|comments-post)(?:[./?]|$)  is_cms_probe;
  ~*(?:^|/)administrator(?:/|$)                                                                                                     is_cms_probe;
  ~*(?:^|/)phpmyadmin(?:[./?]|$)                                                                                                    is_cms_probe;
}

###
### Security-banned client IPs, populated by the BOA monitor/firewall layer
### (scan_nginx/fire write offending real-client IPs here).  Keyed on
### $remote_addr, which Cloudflare realip resolves to the real client, so the
### deny bites CF-proxied attackers at the origin's nginx — where an origin
### CSF/iptables ban on a CF-fronted IP would not.  Wildcard include: an
### absent or empty file is safe (no entries → $is_banned stays 0).
###
geo $remote_addr $is_banned {
  default 0;
  include /data/conf/nginx_banned_ips.c*;
}

###
### Identify bad crawlers, SEO scrapers and abusive bots — hard-blocked.
### AI vendor traffic is classified separately by the $is_ai_* maps above; do
### NOT add AI tokens here or they would bypass the per-class AI policy.  The
### former AI substrings (Amazon, bytedance, CCBot, externalagent, openai,
### perplexity) were moved out: bare "openai"/"perplexity" matched the
### openai.com / perplexity.ai vendor URLs in OAI-SearchBot / ChatGPT-User /
### PerplexityBot / Perplexity-User UAs and wrongly blocked allowed AI traffic.
###
map $http_user_agent $is_crawler {
  default  '';
  ~*Ahrefs|Aspiegel|Automatic|Barkrowler|BrokenLinkCheck|BuzzTrack|DBot|TikTok  is_crawler;
  ~*Go-http-client|GSLFbot|HiScan|HTMLParser|HTTrack|IbouBot|ImagesiftBot|Mireo|MJ12|Morfeus|Nutch|Offline|PChomebot|SWEB  is_crawler;
  ~*PECL|PetalBot|Pinterest|Riddler|Scrap|Semrush|SEOkicks|serpstatbot|Sistrix|SiteBot|SleepBot|Sogou|Turnitin  is_crawler;
}

###
### DDoS protection: block full-text search without referrer.
###
### Two-tier approach:
###   Tier 1 (Maps 1-3): blocks bots that send no referrer at all.
###   Tier 2 (Map 4):    blocks bots that add a fake self-referrer to bypass
###                      Tier 1 but still carry bot-characteristic facet payloads.
###
### Tier 1 — Map 1: Detect search_api / apachesolr facet params in query string
###
map $query_string $has_fulltext_search {
  default                         0;
  "~*search_api_views_fulltext="  1;
  "~*search_api_fulltext="        1;
  "~*im_taxonomy_vid"             1;
}
###
### Tier 1 — Map 2: Detect empty/absent referrer
###
map $http_referer $has_no_referrer {
  default  0;
  ""       1;
  "-"      1;
}
###
### Tier 1 — Map 3: Combine both signals → block when fulltext params AND no referrer
###
map $has_fulltext_search$has_no_referrer $block_search_no_referrer {
  default 0;
  "11"    1;   # fulltext search=1 AND no referrer=1
}

###
### Block the /print* email/printer-friendly flood without assuming module state.
### A printer-friendly or email-this-page request is always a click FROM a page,
### so it carries a Referer; a Referer-less hit to a /print*/ path is the
### distributed botnet (100% of the observed flood had no Referer).  Covers D7
### print/print_mail/print_pdf/printer_and_pdf, D10+ entity_print + printable,
### and Backdrop — anchored on a numeric node id or an export-format segment so
### content slugs (/printing-services, /print/about-us, /printable-maps) never
### match.  No module/version detection; mirrors $block_search_no_referrer.
###
map $uri $is_print_path {
  default 0;
  "~*^/(?:[a-z]{2}/)?(?:printmail/[0-9]|printpdf/[0-9]|printer/[0-9]|print/(?:[0-9]|pdf/|epub/|png/|html/|word_docx/)|printable/(?:print|pdf)/)"  1;
}
map $is_print_path$has_no_referrer $block_print_no_referer {
  default 0;
  "11"  1;   # print-module path AND no referrer → botnet
}

###
### Tier 2 — Map 4: Detect excessive facet count in the query string.
###
### Bots sending a fake self-referrer (e.g. Referer: https://www.example.com)
### bypass the no-referrer check above, but still generate bot-characteristic
### URL payloads with many applied facets.
###
### Two encoding forms must both be matched:
###   Encoded:   f%5BN%5D=value  (URL-encoded brackets, produced by most bots)
###   Unencoded: f[N]=value      (literal brackets, valid per RFC 3986 §3.4,
###                               produced by a distinct bot subgroup that
###                               deliberately avoids the encoded-form regex)
###
### Threshold: f[5]+ = 6th facet = highly likely automated. Real browser users
### navigating a faceted search UI rarely exceed 4-5 concurrent facets.
###
map $query_string $has_excessive_facets {
  default  0;
  ~*f%5[bB][5-9]%5[dD]       1;   # encoded   f[5]–f[9]   (6–10 facets)
  ~*f%5[bB][1-9][0-9]%5[dD]  1;   # encoded   f[10]–f[99] (11+ facets)
  ~*f\[[5-9]\]               1;   # unencoded f[5]–f[9]   (6–10 facets)
  ~*f\[[1-9][0-9]\]          1;   # unencoded f[10]–f[99] (11+ facets)
}

###
### Tier 2 — Map 7: Detect a bare-root Referer (scheme + host, no path).
###
### A genuine user browsing a faceted search page arrives from a specific prior
### page on the site, so their Referer carries a full path.  A bare root Referer
### (https://www.example.com with no /path) combined with facet search params is
### a bot fingerprint: the bot hardcodes the site root as fake Referer but does
### not simulate the actual navigation that would have produced it.
###
map $http_referer $has_root_only_referer {
  default  0;
  ~^https?://[^/]+/?$  1;
}

###
### Tier 2 — Map 7b: Detect presence of any facet param (f[0]+).
###
### Required to distinguish bot payloads (facets + root Referer) from legitimate
### homepage search form submissions (no facets, bare-root Referer).  Without
### this guard, a user submitting the plain search form from the homepage sends
### Referer: https://example.com/ which is indistinguishable from a bot Referer
### at the Map 7 level alone.
###
map $query_string $has_any_facet {
  default  0;
  ~*f%5[bB][0-9]%5[dD]       1;   # encoded   f[0]–f[9]   (1–10 facets)
  ~*f%5[bB][1-9][0-9]%5[dD]  1;   # encoded   f[10]–f[99] (11+ facets)
  ~*f\[[0-9]\]               1;   # unencoded f[0]–f[9]
  ~*f\[[1-9][0-9]\]          1;   # unencoded f[10]–f[99]
}

###
### Tier 2 — Map 8: Block 5-facet threshold-calibration bots.
###
### A second bot sub-group deliberately sends exactly 5 facets (f[0]–f[4],
### max index 4) to stay just under the f[5]+ threshold in Map 4, combined
### with a bare-root Referer to pass the no-referrer gate in Map 3.
###
### Requires all three signals: fulltext search params + bare-root Referer +
### at least one facet param (Map 7b).  Without the facet requirement, legitimate
### users submitting the plain search form from the homepage are falsely blocked
### because their browser sends Referer: https://example.com/ (bare root), which
### is valid same-site navigation but indistinguishable from a bot Referer at the
### Map 7 level alone.
###
map $has_fulltext_search$has_root_only_referer$has_any_facet $block_search_root_referer {
  default  0;
  "111"    1;   # fulltext + root Referer + facet present → bot
}

###
### Tier 2 — Map 5: Detect search payload embedded in a /user/login destination.
###
### Bots use /user/login?destination=search%2F... to route around the location-
### level /search protection — Nginx never evaluates the search guards because
### the request path is /user/login, not /search.  The destination query param
### contains a URL-encoded search URL whose components (apachesolr_search,
### search_api, im_taxonomy_vid) appear verbatim in $query_string.
###
map $query_string $has_search_in_destination {
  default  0;
  ~*destination=.*apachesolr_search  1;
  ~*destination=.*search_api         1;
  ~*destination=.*im_taxonomy_vid    1;
}

###
### Tier 2 — Map 6: Block login-destination abuse when no referrer is present.
### Combines Map 5 with $has_no_referrer (Map 2).
### Self-referer bots that clear this gate are caught downstream by
### $has_excessive_facets and the search_flood rate-limit zone applied
### in the /user/login location block.
###
map $has_search_in_destination$has_no_referrer $block_login_search_destination {
  default  0;
  "11"     1;   # search in destination + no referrer → automated bot
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
### Detect stale Chrome UA in a search context.
###
### Chrome releases every ~4 weeks and auto-updates aggressively on all
### platforms.  By May 2026, Chrome versions below ~124 are more than 12
### months old; genuine consumer installations at those versions are extremely
### rare.  Bots that fake a "moderately outdated but not obviously fake" Chrome
### UA rely on this blind spot — they avoid the obviously-bogus Opera/8 or
### MSIE/6 that trigger $is_bot, but their UA is still detectably stale.
###
### Regex covers Chrome/100–123 (major version 100 through 123):
###   1([01][0-9]|2[0-3])  →  100–119 | 120–123
###
### Maintenance: review this threshold when the current Chrome major version
### advances past ~148 (i.e. when Chrome/124 itself becomes > 12 months old).
### Update the upper bound of the character class accordingly.
###
map $http_user_agent $is_stale_chrome {
  default  0;
  ~*Chrome/1([0-2][0-9]|3[01])\.  1;   # Chrome/100–131: > 12 months stale
  # [0-2][0-9] → 100–129 | 3[01] → 130–131
  # Chrome/131 released Nov 2024; by May 2026 it is ~18 months old.
  # Maintenance: when Chrome/132 exceeds 12 months (≈ Feb 2027),
  # widen to 3[0-2] and update this comment.
}

###
### Combined: stale Chrome UA + fulltext/facet search params = bot on search path.
### Fires only in search location blocks where $has_fulltext_search is also 1,
### avoiding any impact on non-search requests from the same UA class.
###
map $is_stale_chrome$has_fulltext_search $block_stale_chrome_search {
  default  0;
  "11"     1;
}

###
### Standalone: macOS Catalina (10.15.7, EOL Nov 2022) + stale Chrome.
### All confirmed Solr search-amplification bots observed May 2026 use this exact
### combination — no legitimate user in 2026 runs Catalina + Chrome ≤ 131.
### Applied directly in the /search location blocks so no dependency on
### $has_fulltext_search; the search location scope limits false-positive risk.
###
map $http_user_agent $is_catalina_stale_chrome {
  default  0;
  "~*Mac OS X 10_15_7.*Chrome/1([0-2][0-9]|3[01])\."  1;
  # Regex: macOS 10.15.7 (Catalina) + Chrome/100–131
  # Maintenance: widen Chrome range alongside $is_stale_chrome as versions age.
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
  default '';

  # SQL injection / value-scoped, whitespace variants without lazy quantifiers
  ~*(?:^|&)[^=&]+=[^&]*(?:union(?:\s|%20|%2[bB]|/\*\*/)+select)                    is_denied;
  ~*(?:^|&)[^=&]+=[^&]*(?:select(?:\s|%20|%2[bB]|/\*\*/)+[^&]+(?:from|where))      is_denied;
  ~*(?:^|&)[^=&]+=[^&]*(?:insert(?:\s|%20|%2[bB]|/\*\*/)+into)                     is_denied;
  ~*(?:^|&)[^=&]+=[^&]*(?:delete(?:\s|%20|%2[bB]|/\*\*/)+from)                     is_denied;

  # SQL timing/blind attacks
  ~*waitfor(?:\s|%20|%2[bB]|/\*\*/)+delay                                          is_denied;
  ~*declare(?:\s|%20|%2[bB]|/\*\*/)+@                                              is_denied;
  ~*(?:benchmark|sleep|pg_sleep)\s*\(                                              is_denied;

  # Hex literals / require = or SQL keyword immediately before 0x within same value
  "~*(?:^|&)[^=&]+=[^&]*(?:=|%3[dD]|char|cast|convert)(?:\\s|%20|%2[bB])*0x[0-9a-fA-F]{4,}" is_denied;

  # Comment-obfuscated SQLi / keyword must appear before /**/ within same value
  "~*(?:^|&)[^=&]+=[^&]*(?:union|select|where|from|and|or)[^&]{0,40}/\\*\\*/"      is_denied;

  # XSS / raw and percent-encoded
  ~*<script                                                                        is_denied;
  ~*%3[cC]script                                                                   is_denied;
  ~*javascript(?::|%3[aA])                                                         is_denied;
  ~*vbscript(?::|%3[aA])                                                           is_denied;
  ~*data:text/html                                                                 is_denied;
  ~*onload(?:\s|%20)*(?:=|%3[dD])                                                  is_denied;
  ~*document\.cookie                                                               is_denied;

  # PHP source probes / scoped within single param value
  ~*\.php[^&]*(?:src|source|highlight)                                             is_denied;

  # Shell injection
  ~*system(?:\s|%20|%2[bB])*(?:\(|%28)                                             is_denied;

  # Path traversal / raw and encoded, anchored to avoid base64 false positives
  "~*(?:^|[^A-Za-z0-9])\\.\\.\/"                                                   is_denied;
  ~*%2[eE]%2[eE](?:%2[fF]|%5[cC]|/)                                                is_denied;
  ~*%252[eE]%252[eE](?:%252[fF]|%255[cC]|%2[fF]|/)                                 is_denied;
}

###
### Also check the UA string for injection patterns (attack may have
### embedded the WAITFOR payload inside the User-Agent header itself).
###
map $http_user_agent $ua_denied {
  default  '';
  ~*/\*\*/                           ua_denied;
  ~*waitfor([/\*]|\s\+)+delay        ua_denied;
  ~*declare([/\*]|\s\+)+@            ua_denied;
  ~*(benchmark|sleep|pg_sleep)\s*\(  ua_denied;
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
### Detect static-asset “chain” URL mutation spam (broken relative-URL
### resolution: a distributed botnet appends root-relative-without-leading-
### slash Drupal asset refs onto a deep content URL).  Legitimate Drupal asset
### URLs are root-anchored; these bury an asset-dir marker (or a canonical core
### asset file) under a content path, or repeat the marker.  Matched here so the
### vhost can 444 them before the /(?:external|system)/ asset router reaches
### @drupal -> /index.php -> php-fpm.  Validated against 44k real flood requests:
### 0 false positives on root-anchored assets, aggregated files, image styles
### and /system/files private files.  Examples:
### /about-council/meetings/day/2017-12-11/system/sites/all/modules/colorbox/js/modules/node/node.css
### /a-z-services/modules/system/about-council/.../sites/all/modules/menu_minipanels/js/x.callbacks.js
### /about-council/meetings/day/2021-02-24/system/system.base.css
###
map $uri $is_static_chain {
  default 0;

  # buried sites/all|default asset-dir (or ui/external) marker, ending in an asset
  "~*^/(?![a-z]{2}/)(?!(?:sites|modules|misc|themes|core|libraries|profiles|cdn|files|system|external|s3)/)[^?]+/(?:sites/(?:all|default)/(?:modules|themes|libraries)|ui/external)/[^?]+\.(?:css|js|htc|png|gif|jpe?g|svg|ico|webp|bmp|woff2?|ttf|otf|eot|less|map)$"  1;

  # same Drupal asset-dir token repeated (relative-URL concatenation); asset-
  # anchored so it cannot match a legitimate content path with no static file
  "~*^/[^?]*/(?:sites/all/(?:modules|themes)|modules/(?:system|field|user|node|filter|search))/[^?]+/(?:sites/all/(?:modules|themes)|modules/(?:system|field|user|node|filter|search))/[^?]+\.(?:css|js|htc|png|gif|jpe?g|svg|ico|webp|bmp|woff2?|ttf|otf|eot|less|map)$"  1;

  # a canonical Drupal core asset file buried under a content path (these core
  # filenames only ever occur legitimately at a root-anchored path)
  "~*^/(?![a-z]{2}/)(?!(?:sites|modules|misc|themes|core|libraries|profiles|cdn|files|system|external|s3)/)[^?]+/(?:system\.(?:base|menus|messages|theme)\.css|(?:node|user|field|search|filter|comment|book|forum|poll|taxonomy|dblog)\.css|drupal\.js|jquery\.once\.js|ajax\.js|batch\.js|tabledrag\.js|tableselect\.js|states\.js|progress\.js|form\.js|collapse\.js|autocomplete\.js|machine-name\.js|textarea\.js|vertical-tabs\.js)$"  1;
}

###
### Detect the content-path twin of the static-asset chain flood: the same
### broken relative-URL resolution, but the mutated URL ends in a content
### segment (no static asset), so it falls through to Drupal and renders a full
### themed page (200) instead of being 444’d by $is_static_chain.  Matched only
### when BOTH signals hold: a Drupal code-dir marker appears as a path segment
### (sites/all/modules, modules/system, ui/external … never occur in a legitimate
### content alias) AND some path segment repeats 3+ times (the relative-URL
### accumulation signature).  The marker is a leading lookahead so normal,
### markerless traffic fails fast and never touches the back-reference.  This is
### deliberately conservative (covers the clear majority of the variant, not the
### 2x-repeat tail) — the complete cure is a source-side <base href>/theme fix
### that stops the site emitting root-relative-without-leading-slash links.
### Examples:
### /a-z-services/modules/system/about-council/modules/node/about-council/modules/node/about-council/meetings
### /a-z-services/about-council/sites/all/modules/jquery_update/replace/ui/external/about-council/sites/all/modules/x/about-council/meetings
###
map $uri $is_content_chain {
  default 0;
  "~*^(?=[^?]*/(?:sites/(?:all|default)/(?:modules|themes|libraries)|modules/(?:system|field|user|node|filter|search)|ui/external)/)[^?]*/([a-z0-9][a-z0-9_-]{2,})/(?:[^/]+/)*?\1/(?:[^/]+/)*?\1(?:/|$)"  1;
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

###
### Tier-A distributed-i18n-flood guardrail (BOA abuse-guard).
###
### A distributed scraper crawling localized pages drives each uncached page
### through an expensive synchronous backend (on-the-fly translation), holding
### a PHP-FPM worker for tens of seconds.  FPM pools are shared per account, so
### enough concurrent localized requests saturate the whole pool and collapse
### every site on it; the source is spread over thousands of IPs (one or two
### requests each), so per-IP limits never trip.
###
### These maps build a CONSTANT per-vhost key ($host) that is non-empty only
### for an anonymous request to a localized path on a guarded vhost.  The key
### feeds limit_conn zone=boa_i18n_anon (declared in the http{} block above),
### which bounds the IN-FLIGHT count of that request class per vhost regardless
### of source IP.  An empty key is not counted, so English, static, authenticated
### and opted-out traffic is never touched; excess returns 444 before it reaches
### php-fpm.  The guardrail is ON by default for every vhost: a leading two-letter
### path prefix is Drupal's URL language-negotiation convention, never a content
### subdirectory, so the class match is safe across Drupal/Backdrop sites (the
### existing /[a-z][a-z]/search and /[a-z][a-z]/civicrm locations rely on the same
### convention).  Opt a vhost OUT by adding a   "host" 0;   line to
### /data/conf/boa_i18n_guard.map (wildcard include below; an absent file leaves
### the guardrail ON fleet-wide).
###

### Per-vhost on/off switch.  Default 1 (ON) — see the note above on why a
### two-letter prefix is a safe fleet-wide language signal.  A BOA-managed
### wildcard include can set specific hosts to 0 to opt them out (e.g. a
### non-Drupal app, or a single-language site that uses a two-letter path for a
### region rather than a language); an absent/empty file leaves every host ON.
map $host $boa_i18n_guard {
  default 1;
  include /data/conf/boa_i18n_guard.map*;
}

### Localized request class: 1 when the ORIGINAL request URI targets a path
### under a two-letter language prefix, optionally with a script/region suffix
### (e.g. /pt-br/ or /zh-hans/).  Keyed on $request_uri, NOT $uri, because BOA
### internally rewrites clean URLs to /index.php before this is evaluated.  The
### second pattern closes the D7 ?q=<lang>/... bypass (an explicit /index.php?q=
### request keeps the language in the query, not the path); it cannot match
### ordinary q=node/ , q=user/ or q=desktop values, which are never exactly two
### letters followed by '/'.
map $request_uri $boa_i18n_path {
  default 0;
  ~*^/[a-z][a-z](-[a-z]+)?/        1;
  ~*[?&]q=/?[a-z][a-z](-[a-z]+)?/  1;
}

### Anonymous predicate: 1 when there is no Drupal session cookie (reuses the
### authoritative $cache_uid map above; empty $cache_uid = anonymous = limited).
map $cache_uid $boa_is_anon {
  default 0;
  ""      1;
}

### Final key: $host only when the vhost is opted in AND the request is
### localized AND anonymous; otherwise empty, so limit_conn ignores it.
map "$boa_i18n_guard$boa_i18n_path$boa_is_anon" $boa_i18n_anon_key {
  default "";
  "111"   $host;
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
