<?php
/**
 * @file
 * The Grav location contract, shared by vhost_grav.tpl.php and
 * vhost_ssl_grav.tpl.php (included inside their main server blocks; scope
 * carries $user_socket, $script_user and $this from the including template).
 *
 * Self-contained on purpose: the shared nginx_vhost_common.conf is
 * Drupal-shaped end to end and applied to a Grav docroot raw-serves system/**
 * source and user/** YAML (boa-grav R6). Contract per map par.3.4 +
 * docs/spikes/2026-08-31-round-1.md: docroot = the capsule; ONLY /index.php
 * executes; Grav internals + Aegir artefacts + root metadata denied; front
 * controller keeps the query string; no acme block here (BOA injects it via
 * $extra_config into every main server block).
 *
 * Self-contained does NOT mean unguarded: the CMS-agnostic guard chain from
 * the shared include (ban/probe/crawler/UA classification, the AI default-deny
 * consumers, TLS-on-plain, high-load shedding, the operator escape hatches)
 * is re-stated here -- every variable it reads is declared at http scope by
 * the master server.tpl.php render, so a capsule vhost may consume them
 * freely. Guard verdicts keep the fleet 444 (classified-abusive clients);
 * the CAPACITY cap below sheds 503 instead -- its victim can be a legitimate
 * anonymous visitor, a 444 reaches CDN-fronted visitors as a hard error, and
 * no Grav-side IDS detector counts these 444s (boa-grav Q9 ruling, D-007).
 */

// Concurrency guardrail (Q9/D-007): no fastcgi_cache in phase 1 (Grav
// re-emits its session Set-Cookie on every response, so the shared speed
// zone would store nothing) and no limit_req (a rate cap on the single
// front controller is the highest-false-positive control available);
// per-IP + per-host CONCURRENCY caps bound flood damage instead. The
// per-host zone is declared by BOA in the shared zones file; gate every
// reference on the zone actually being present in that file so no
// BOA/provision delivery order can produce an undeclared-zone reference
// (a box-wide nginx [emerg], on an upgrade path that restarts nginx
// without a configtest).
$grav_zones_file = '/etc/nginx/conf.d/limit-req-zones-boa.conf';
$grav_zones_body = @is_file($grav_zones_file)
  ? (string) @file_get_contents($grav_zones_file)
  : '';
$grav_zone_ok = strpos($grav_zones_body, 'zone=boa_grav_anon') !== FALSE;
$grav_anon_conn = (int) drush_get_option('nginx_grav_anon_conn', 100);
if ($grav_anon_conn < 1 || $grav_anon_conn > 65535) {
  // An out-of-range tenant value falls back to the default -- it must never
  // disable or zero the guardrail.
  $grav_anon_conn = 100;
}
?>
  ### GRAV location contract -- inline (no shared common include).

  # Billing suspend parity (the global.inc chain never runs for a capsule) --
  # the converged foreign-CMS shape.
  if (-f /data/conf/suspended/<?php print $script_user; ?>.pid) { return 503; }

  ###
  ### CMS-agnostic guard chain (fleet classification maps, http scope).
  ###
  if ($is_banned) {
    return 444;
  }
  if ( $args ~* "=PHP[A-Z0-9]{8}-" ) {
    return 404;
  }
  if ($is_secret_path) {
    return 444;
  }
  if ($is_cms_probe) {
    return 444;
  }
  if ($is_crawler) {
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

  ###
  ### AI training/evasive default-deny consumers. The vhost templates set
  ### $ai_train_allow / $ai_evasive_allow 0 before the per-site ai_policy
  ### fragment; without these consumers those sets are inert (the shared
  ### include that normally consumes them is not pulled here).
  ###
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

  # Any location carrying its own add_header cancels ALL inherited add_header
  # lines -- locations below must not add headers without re-stating this pair.
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;

  ###
  ### Include high load protection config if exists.
  ###
  include /data/conf/nginx_high_load.c*;

  ###
  ### Deny not compatible request methods without 405 response. The allowed
  ### set is the fleet-standard one and already covers the api plugin's full
  ### REST verb surface (PUT/PATCH/DELETE) and CORS preflight (OPTIONS).
  ###
  if ( $request_method !~ ^(?:GET|HEAD|POST|PUT|PATCH|DELETE|OPTIONS)$ ) {
    return 444;
  }

  ###
  ### Include high level local configuration override if exists.
  ###
  include  <?php print d('@server_master')->aegir_root; ?>/config/server_master/nginx/post.d/nginx_force_include*;

  # Dotfiles (covers /.env and /.env.*) except ACME.
  location ~ /\.(?!well-known) { return 404; }

  # Aegir artefacts in the served docroot (capsule model): the option carrier,
  # any settings file, any SQL dump (phase 2 writes database.sql here for the
  # duration of every backup).
  location ~* ^/(drushrc\.php|settings\.php|local\.settings\.php)$ { return 404; }
  location ~* \.sql$ { return 404; }

  # Root metadata (dependency inventory + docs -- /composer.lock served 271KB
  # under upstream's own config; spike-1 live catch).
  location ~* ^/(composer\.(json|lock)|now\.json|CHANGELOG\.md|README\.md|SECURITY\.md|LICENSE\.txt|CODE_OF_CONDUCT\.md|CONTRIBUTING\.md)$ { return 404; }

  # Grav internal trees. cache/logs/tmp/backup/bin/tests never serve;
  # webserver-configs is upstream's own gap; modules/ is the BOA control-INI
  # dir (no Grav route). system/ and vendor/ serve real assets, so those two
  # deny by extension (upstream's own contract), as does user/ (page media
  # must serve; sources must not).
  location ~* ^/(cache|bin|logs|backup|tmp|tests|modules|webserver-configs)/ { return 404; }
  location ~* ^/(system|vendor)/.*\.(txt|xml|md|html|json|yaml|yml|php|pl|py|cgi|twig|sh|bat)$ { return 404; }
  location ~* ^/user/(config|env|accounts|data)/ { return 404; }
  location ~* ^/user/.*\.(txt|md|yaml|yml|php|pl|py|cgi|twig|sh|bat)$ { return 404; }

  # Real-file fast path for the trees that are static by construction (theme
  # and system assets, the compiled asset dir) -- takes them off the PHP path.
  # Deliberately NOT applied to user/pages media (Grav routes those for media
  # processing/derivatives) or to plugin _app bundles (served through the
  # front controller's asset map). Placed BELOW the denies: regex locations
  # match in order of appearance. expires is not add_header, so the security
  # header pair above still inherits here.
  location ~* ^/(system/assets|assets|user/themes)/.*\.(css|js|jpe?g|png|gif|svg|webp|avif|ico|woff2?|ttf|eot|map)$ {
    access_log off;
    expires 30d;
    try_files $uri =404;
  }

  location = /favicon.ico { access_log off; try_files $uri /index.php?$args; }
  location = /robots.txt  { access_log off; try_files $uri /index.php?$args; }

  # The ONLY PHP that executes: the front controller (Admin2, the api plugin
  # and the plugin-asset-map fast path all route through it). Concurrency
  # caps live here so static files are never capped: the per-IP ceiling is
  # fleet-standard; the per-host anonymous ceiling keys off the shared-file
  # map that exempts Grav admin-cookie holders and Authorization-bearing API
  # clients (an editor is never shed while a flood is trimmed). Shed = 503:
  # retryable, renders correctly through a fronting CDN, still IDS-visible.
  location = /index.php {
    limit_conn limreq 88;
<?php if ($grav_zone_ok): ?>
    limit_conn boa_grav_anon <?php print $grav_anon_conn; ?>;
<?php endif; ?>
    limit_conn_status 503;
    fastcgi_pass unix:<?php print $user_socket; ?>;
  }

  # Front controller (keeps the query string).
  location / {
    try_files $uri $uri/ /index.php?$args;
  }

  # Everything else that ends in .php 404s (writable dirs included).
  location ~* \.php$ { return 404; }

  ###
  ### Include local configuration override if exists.
  ###
  include  <?php print d('@server_master')->aegir_root; ?>/config/server_master/nginx/post.d/nginx_vhost_include*;
