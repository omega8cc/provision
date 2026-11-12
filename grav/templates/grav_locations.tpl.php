<?php
/**
 * @file
 * The Grav location contract, shared by vhost_grav.tpl.php and
 * vhost_ssl_grav.tpl.php (included inside their main server blocks; scope
 * carries $user_socket, $script_user and $this from the including template).
 *
 * Self-contained on purpose: the shared nginx_vhost_common.conf is
 * Drupal-shaped end to end and applied to a Grav docroot raw-serves system/**
 * source and user/** YAML. Contract (spike-proven):
 * docroot = the capsule; ONLY /index.php
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
 * no Grav-side IDS detector counts these 444s.
 */

// Concurrency guardrail: no fastcgi_cache in phase 1 (Grav
// re-emits its session Set-Cookie on every response, so the shared speed
// zone would store nothing) and no limit_req (a rate cap on the single
// front controller is the highest-false-positive control available);
// per-IP + per-host CONCURRENCY caps bound flood damage instead. The
// per-host zone is declared by BOA in the shared zones file; gate every
// reference on the zone actually being present in that file so no
// BOA/provision delivery order can produce an undeclared-zone reference
// (a box-wide nginx [emerg], on an upgrade path that restarts nginx
// without a configtest).  The https template includes this file and then the
// http template, which includes it again: read the zones file once per render.
if (!isset($grav_zones_body)) {
  $grav_zones_file = '/etc/nginx/conf.d/limit-req-zones-boa.conf';
  $grav_zones_body = @is_file($grav_zones_file)
    ? (string) @file_get_contents($grav_zones_file)
    : '';
}
$grav_zone_ok = strpos($grav_zones_body, 'zone=boa_grav_anon') !== FALSE;
// A site named under the box hostname (the same "name carries the hostname"
// rule a hostname rename applies) renders the per-host migration gate below.
$grav_box_fqdn = (string) d('@server_master')->remote_host;
$grav_box_named = $grav_box_fqdn !== '' && strpos((string) $this->uri, $grav_box_fqdn) !== FALSE;
// The migration gate: judged in PHP by BOA's own-folder rule once BOA's
// setup file includes it, else tested in nginx by path (the master's sites
// always). Read once per render, as the zones file is.
if (!isset($grav_hoff_php)) {
  $grav_hoff_php = provision_http_off_in_php(d('@server_master')->aegir_root,
    '/data/conf/global/global-grav-setup.php');
}
$grav_off_ctl = $grav_hoff_php ? '$boa_off_ctl'
  : d('@server_master')->aegir_root . '/static/control';
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

  # BOA's Grav setup file: a request creates folders and files with the group
  # write the hosting model gives them (umask 0002, as the shell's grav command
  # does), so the account can clear a cache folder the web server made; then
  # the site's own setup.php as Grav finds it. Named while nginx finds the
  # file: Grav stops on a GRAV_SETUP_PATH that names no file, and nginx's open
  # file cache can keep a removed file's answer until open_file_cache_valid
  # runs out (reload nginx after removing it by hand). Server level, as a
  # location-level fastcgi_param would cancel the inherited set.
  set $boa_grav_setup "";
  if (-f /data/conf/global/global-grav-setup.php) {
    set $boa_grav_setup /data/conf/global/global-grav-setup.php;
  }
  fastcgi_param GRAV_SETUP_PATH $boa_grav_setup if_not_empty;
<?php if ($grav_hoff_php): ?>

  # The migration's 503 gate is judged in PHP: the setup file includes BOA's
  # global-http-off.php first, the rule Drupal and Backdrop sites follow,
  # which honours a gate file only in a control folder that is the account's
  # own (nginx tests a path, never its owner). While either file is missing,
  # the index.php location below tests the gate files by path again; /dev/null
  # is never a folder, so otherwise its tests never match. Server level, so no
  # location switches its configuration for a set.
  set $boa_off_ctl /dev/null;
  if ($boa_grav_setup = "") {
    set $boa_off_ctl <?php print d('@server_master')->aegir_root; ?>/static/control;
  }
  if (!-f /data/conf/global/global-http-off.php) {
    set $boa_off_ctl <?php print d('@server_master')->aegir_root; ?>/static/control;
  }
<?php endif; ?>

  ###
  ### CMS-agnostic guard chain (fleet classification maps, http scope).
  ###
  if ($is_banned) {
    return 444;
  }
<?php if (strpos($grav_zones_body, 'map $boa_fleet_uaid $boa_fleet_block {') !== FALSE): ?>
  ###
  ### Refuse a declared crawler-fleet fingerprint (the $boa_fleet_* maps in
  ### the BOA zones file, gated like boa_grav_anon above).  429, not 444:
  ### no IDS scorer counts it, and a real visitor sharing the fingerprint
  ### passes with a Referer or a Grav admin session.
  ###
  if ($boa_fleet_block) {
    return 429;
  }
<?php endif; ?>
  if ( $args ~* "=PHP[A-Z0-9]{8}-" ) {
    return 404;
  }
  if ($is_secret_path) {
    return 444;
  }
  if ($is_cms_probe) {
    return 444;
  }
  ###
  ### Deny forged AI user-agents. Google-Extended / Applebot-Extended are
  ### robots.txt-only opt-out tokens a real client never sends, so their
  ### presence proves forgery (zero false positive). Present in the shared
  ### include and in both Textpattern twins; the converged foreign-CMS shape
  ### carries it. $is_botnet is deliberately NOT re-stated here: it is
  ### referer-based, and the referer is attacker-controlled.
  ###
  if ($is_ai_forged) {
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
  include /data/conf/nginx_high_load.con[f];

  ###
  ### Deny not compatible request methods without 405 response. The allowed
  ### set is the fleet-standard one and already covers the api plugin's full
  ### REST verb surface (PUT/PATCH/DELETE) and CORS preflight (OPTIONS).
  ###
  if ( $request_method !~ ^(?:GET|HEAD|POST|PUT|PATCH|DELETE|OPTIONS)$ ) {
    return 444;
  }

  ###
  ### The scheme a redirect in the operator includes below keeps, as in the
  ### shared include: https for a visitor who came through the wildcard SSL
  ### front, which reaches this vhost over plain HTTP with
  ### X-Forwarded-Proto: https.
  ###
  set $boa_visitor_scheme $scheme;
  if ($http_x_forwarded_proto = "https") {
    set $boa_visitor_scheme "https";
  }

  ###
  ### Include high level local configuration override if exists.
  ###
  include  <?php print d('@server_master')->aegir_root; ?>/config/server_master/nginx/post.d/nginx_force_include*;

  # Dotfiles (covers /.env and /.env.*) except ACME and the rest of
  # /.well-known/: a segment that merely starts with ".well-known" is a dotfile.
  location ~ /\.(?!well-known(?:/|$)) { return 404; }

  # Aegir artefacts in the served docroot (capsule model): the option carrier,
  # any settings file, any SQL dump (phase 2 writes database.sql here for the
  # duration of every backup).
  location ~* ^/(drushrc\.php|settings\.php|local\.settings\.php)$ { return 404; }
  location ~* \.sql$ { return 404; }
  # SQLite databases and their write-ahead and journal siblings, wherever a
  # site keeps them (a KahunaCart store, its search index, the API audit log;
  # user/data is denied below, but a store's path is the owner's setting).
  # Only a file that exists is refused: an API route that merely ends in .db
  # (an Admin2 media delete) goes to the front controller, as location / does.
  location ~* \.(?:sqlite3?|db)(?:-(?:wal|shm|journal))?$ {
    if (-f $request_filename) { return 404; }
    try_files $uri /index.php?$args;
  }

  # Root metadata (dependency inventory -- /composer.lock served 271KB under
  # upstream's own config; caught live in a spike) and the sample server
  # configs, matched as prefixes like upstream's own rule, so a copy such as
  # /composer.json.bak is refused too. The root's Markdown files (CHANGELOG.md,
  # README.md and the rest) are refused by the Markdown rule below only when
  # they exist, so a page whose address takes one of those names keeps its
  # Markdown output.
  location ~* ^/(composer\.(json|lock)|now\.json|LICENSE\.txt|nginx\.conf|web\.config|htaccess\.txt) { return 404; }

  # Grav internal trees. cache/logs/tmp/backup/bin/tests never serve;
  # webserver-configs is upstream's own gap; modules/ is where an older
  # release left a BOA control INI (no Grav route). system/ and vendor/ serve
  # real assets, so those two deny by extension (upstream's own contract), as
  # does user/ (page media must serve; sources must not).
  # The extension lists are upstream's own (only /index.php ever executes here,
  # so these files would only be served raw), plus BOA's .md and .txt rules.
  location ~* ^/(cache|bin|logs|backup|tmp|tests|modules|webserver-configs)/ { return 404; }
  location ~* ^/(system|vendor)/.*\.(txt|xml|md|html|htm|shtml|shtm|json|yaml|yml|php|php2|php3|php4|php5|phar|phtml|pl|py|cgi|twig|sh|bat)$ { return 404; }
  # Scripts in the public cache folders (image derivatives, combined assets).
  location ~* ^/(images|assets)/.*\.(php|php2|php3|php4|php5|php7|php8|phar|phtml|pht|phtm|phps|pl|py|cgi|sh|bat)$ { return 404; }
  location ~* ^/user/(config|env|accounts|data)/ { return 404; }
  location ~* ^/user/.*\.(txt|md|yaml|yml|php|php2|php3|php4|php5|phar|phtml|pl|py|cgi|twig|sh|bat)$ { return 404; }
  # Dependency manifests under user/ (plugins ship composer.json, composer.lock
  # and package.json): the same version inventory refused at the root above.
  # Page attachments keep serving; Admin2 reads its version.json through PHP.
  location ~* ^/user/(?!pages/).*\.(json|lock)$ { return 404; }
  # Markdown files kept on disk anywhere in the docroot outside the trees above
  # (the core's CHANGELOG.md and README.md at the root, a README beside images,
  # notes dropped there): upstream's own rule since 2.2.19. Only a
  # file that exists is refused: a page route ending in .md is not a file, so
  # Markdown output and the llms-txt views still answer through the front
  # controller.
  location ~* \.md$ {
    if (-f $request_filename) { return 404; }
    try_files $uri /index.php?$args;
  }

  # Content-hashed SvelteKit bundles of plugins and themes: Admin2 names its
  # chunks by their real path under user/plugins/admin2/app/_app/immutable/,
  # and a name changes with its content, so they cache for a year and a
  # missing one is a plain 404 instead of a Grav boot. The year-long header
  # goes on found files only (no "always"): a chunk missing for a moment
  # during an update must not be cached as a 404. Above the theme fast path
  # below, which would otherwise take a theme's bundles first. This location
  # sets its own headers, so the security pair is re-stated (add_header does
  # not inherit into a location that has its own).
  location ~* ^/user/(?:plugins|themes)/.+/_app/immutable/.+\.(?:js|css|woff2?)$ {
    access_log off;
    add_header Cache-Control "public, max-age=31536000, immutable";
    add_header X-Content-Type-Options "nosniff";
    add_header X-Frame-Options "SAMEORIGIN" always;
    try_files $uri =404;
  }

  # Real-file fast path for the trees that are static by construction (theme
  # and system assets, the compiled asset dir) -- takes them off the PHP path.
  # Deliberately NOT applied to user/pages media (Grav routes those for media
  # processing/derivatives). Placed BELOW the denies: regex locations match in
  # order of appearance. expires is not add_header, so the security header
  # pair above still inherits here.
  #
  # Code assets take a SHORT window, media and fonts the fleet-standard 30d.
  # The Drupal include can afford a long expiry on css/js because the
  # aggregator renames the file on every change; Grav's shipped defaults have
  # the css/js pipelines and the asset timestamp all off, so a theme's
  # stylesheet keeps ONE stable URL for its whole life and a long expiry hides
  # an edit from returning visitors until it lapses.
  location ~* ^/(system/assets|assets|user/themes)/.*\.(css|js|map)$ {
    access_log off;
    expires 10m;
    try_files $uri =404;
  }
  location ~* ^/(system/assets|assets|user/themes)/.*\.(jpe?g|png|gif|svg|webp|avif|ico|woff2?|ttf|eot)$ {
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
    # Account write barrier parity (the global.inc chain never runs for a
    # capsule): the account's http-off.pid holds every PHP request on 503
    # while a migration freezes the account, in PHP where the server level
    # above leaves it there, else by this path test. Location-scoped like the
    # Drupal gate, so statics and acme keep serving. The presence test must
    # not ride the open-file cache: a cached miss would keep PHP serving after
    # the gate went up, and a cached hit would hold the 503 after it was lifted.
    open_file_cache off;
    if (-f <?php print $grav_off_ctl; ?>/http-off.pid) {
      add_header Retry-After 300 always;
      add_header Cache-Control "no-store, must-revalidate" always;
      return 503;
    }
<?php if ($grav_box_named): ?>
    # The per-host twin: a whole-server move lifts the account gate as soon
    # as the old box relays to the new one, but a site named under the box
    # hostname is still being renamed there, so it stays held through this
    # file. Rendered only into a vhost named under the box hostname, which
    # is the class the file lists, so its presence is the whole test.
    if (-f <?php print $grav_off_ctl; ?>/http-off-host.pid) {
      add_header Retry-After 300 always;
      add_header Cache-Control "no-store, must-revalidate" always;
      return 503;
    }
<?php endif; ?>
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
