<?php
/**
 * @file
 * nginx vhost template for a Textpattern multisite site.
 *
 * Suspend parity: BOA suspension = /data/conf/suspended/<oct>.pid
 * (managed by `boa suspend|unsuspend`; enforced for Drupal/Backdrop in the
 * global settings chain). Foreign-CMS sites never run that chain, so the
 * check lives in the PHP locations here — location-scoped on purpose: statics
 * and acme keep serving during suspension, exactly like the Drupal behaviour.
 * Shared shape with the grav track.
 *
 * Account write barrier parity: <aegir_root>/static/control/http-off.pid
 * holds every PHP request on 503 while a migration freezes the account
 * (the Drupal/Backdrop gate lives in the same global settings chain). Same
 * PHP locations, same reason. open_file_cache is off in those locations
 * because the presence test would otherwise ride the open-file cache: a
 * cached miss keeps PHP serving after the gate went up, a cached hit holds
 * the 503 after it was lifted. Nothing else in a PHP location opens files.
 *
 * (original @file docs continue below)
 * nginx vhost for a Textpattern multisite site (http).
 *
 * Selected via hook_provision_config_load_templates() when the platform is a
 * TXP root; the shared vhost.tpl.php + nginx_vhost_common.conf pair is
 * Drupal-shaped end to end (its own location /, the @legacy query-dropping
 * fallback, a PHP whitelist without css.php) and is NOT addable-to, so this
 * template is self-contained: per-site docroot + the full TXP location
 * contract inline.
 *
 * Contract: docroot = sites/<uri>/public;
 * public PHP at EXACTLY /index.php and /css.php, every other .php 404s
 * (upload-dir execution protection); front controller keeps the query string;
 * admin path-mapped via alias with the FULL fastcgi param set re-declared
 * in-location (nginx param inheritance is all-or-nothing); the four admin
 * symlinks need disable_symlinks left off (BOA default).
 */
$this->root = provision_auto_fix_platform_root($this->root);
$site_public = "{$this->root}/sites/{$this->uri}/public";
$site_admin = "{$this->root}/sites/{$this->uri}/admin";
$txp_admin_path = defined('PROVISION_TXP_ADMIN_PATH') ? PROVISION_TXP_ADMIN_PATH : 'txpadmin';

print "include  " . $server->include_path . "/user_admin_access_map/{$this->uri}.conf*;\n";
$script_user = d('@server_master')->script_user;
if (!$script_user) {
  $script_user = drush_get_option('script_user');
}
if (!$script_user && $server->script_user) {
  $script_user = $server->script_user;
}
// Enforced PHP: pin the enforced-version FPM socket
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
$aegir_root = d('@server_master')->aegir_root;
// A site named under the box hostname (the same "name carries the hostname"
// rule a hostname rename applies) also renders the per-host migration gate,
// static/control/http-off-host.pid: a whole-server move lifts the account
// gate as soon as the old box relays to the new one, but such a site is still
// being renamed there, so it stays held through that file. The vhost class is
// exactly what the file lists, so its presence is the whole test.
$txp_box_fqdn = (string) d('@server_master')->remote_host;
$txp_box_named = $txp_box_fqdn !== '' && strpos((string) $this->uri, $txp_box_fqdn) !== FALSE;
$satellite_mode = d('@server_master')->satellite_mode;

// Direct /files/ downloads are DENIED by default. Per-site opt-out via
// a control file: touch <aegir_root>/static/control/txp-files-open-<name>.info.
$txp_main_name = $this->redirection ? $this->redirection : $this->uri;
$txp_files_open = provision_file()
  ->exists($aegir_root . '/static/control/txp-files-open-' . $txp_main_name . '.info')
  ->status();

if ($this->redirection) {
  // Redirect all aliases to the main url in separate server blocks.
  foreach ($this->aliases as $alias_url) {
    if (!preg_match("/\.(?:nodns|dev|devel)\./", $alias_url)) {
      print "\n";
      print "# alias redirection virtual host\n";
      print "server {\n";
      print "  listen  *:{$http_port};\n";
      if ($this->redirection && $alias_url == $this->redirection) {
        $this->uri = str_replace('/', '.', $this->uri);
        print "  server_name  {$this->uri};\n";
      }
      else {
        $alias_url = str_replace('/', '.', $alias_url);
        print "  server_name  {$alias_url};\n";
      }
      print "  access_log off;\n";
      print "  log_not_found off;\n";
      if ($satellite_mode == 'boa') {
        print "  location ^~ /.well-known/acme-challenge {\n";
        print "    allow all;\n";
        print "    alias {$aegir_root}/tools/le/.acme-challenges;\n";
        print "    try_files \$uri 404;\n";
        print "  }\n";
      }
      // An HTTPS visitor who reached this alias through the wildcard SSL
      // front arrives here over plain HTTP, with X-Forwarded-Proto: https.
      print "  set \$boa_visitor_scheme \$scheme;\n";
      print "  if (\$http_x_forwarded_proto = \"https\") {\n";
      print "    set \$boa_visitor_scheme \"https\";\n";
      print "  }\n";
      print "  return 301 \$boa_visitor_scheme://{$this->redirection}\$request_uri;\n";
      print "}\n";
    }
  }
}
?>

server {
  include fastcgi_params;
  # Block https://httpoxy.org/ attacks.
  fastcgi_param HTTP_PROXY "";
  fastcgi_param HTTP_HOST $host;
  fastcgi_param REQUEST_SCHEME $scheme;
  fastcgi_param MAIN_SITE_NAME <?php print $this->uri; ?>;
  set $main_site_name "<?php print $this->uri; ?>";
  fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
<?php
  // If any of those parameters is empty for any reason, like after an attempt
  // to import complete platform with sites without importing their databases,
  // it will break Nginx reload and even shutdown all sites on the system on
  // Nginx restart, so we need to use dummy placeholders to avoid affecting
  // other sites on the system if this site is broken. Same guard as the
  // Drupal/Backdrop vhost templates.
  if (!$db_type || !$db_name || !$db_user || !$db_passwd || !$db_host) {
    $db_type = 'mysqli';
    $db_name = 'none';
    $db_user = 'none';
    $db_passwd = 'none';
    $db_host = 'localhost';
  }
?>
  fastcgi_param db_type   <?php print urlencode($db_type); ?>;
  fastcgi_param db_name   <?php print urlencode($db_name); ?>;
  fastcgi_param db_user   <?php print implode('@', array_map('urlencode', explode('@', $db_user))); ?>;
  fastcgi_param db_passwd <?php print urlencode($db_passwd); ?>;
  fastcgi_param db_host   <?php print urlencode($db_host); ?>;
<?php
  // Same ProxySQL port override as the Drupal/Backdrop vhost templates.
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
  fastcgi_param db_port   <?php print urlencode($db_port); ?>;
  # Marks the six credentials above as urlencode()d: the cloaked config.php
  # decodes exactly this source. Feeds /index.php and /css.php below by
  # inheritance; the admin location re-declares its own set (inheritance is
  # all-or-nothing there), so it reads these back through the $boa_db_*
  # variables the next line declares -- same contract as the shared
  # vhost_include.tpl.php locations.
  fastcgi_param db_creds_urlencoded 1;
<?php print provision_nginx_db_set_lines($db_type, $db_name, $db_user, $db_passwd, $db_host, $db_port); ?>
  listen  *:<?php print $http_port; ?>;
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
        if (trim($alias_url) && !preg_match("/\.(?:dev|devel)\./", $alias_url)) {
          print " " . str_replace('/', '.', $alias_url);
        }
      }
    } ?>;
  root  <?php print $site_public; ?>;
  <?php print $extra_config; ?>
<?php
if ($this->redirection || $ssl_redirection) {
  if ($ssl_redirection && !$this->redirection) {
    print "  access_log off;\n";
    print "  log_not_found off;\n";
    if ($satellite_mode == 'boa') {
      print "  location ^~ /.well-known/acme-challenge {\n";
      print "    allow all;\n";
      print "    alias {$aegir_root}/tools/le/.acme-challenges;\n";
      print "    try_files \$uri 404;\n";
      print "  }\n";
    }
    print "\n  return 301 https://\$host\$request_uri;\n";
    print "}\n";
    return;
  }
  elseif ($ssl_redirection && $this->redirection) {
    print "  access_log off;\n";
    print "  log_not_found off;\n";
    if ($satellite_mode == 'boa') {
      print "  location ^~ /.well-known/acme-challenge {\n";
      print "    allow all;\n";
      print "    alias {$aegir_root}/tools/le/.acme-challenges;\n";
      print "    try_files \$uri 404;\n";
      print "  }\n";
    }
    print "\n  return 301 https://{$this->redirection}\$request_uri;\n";
    print "}\n";
    return;
  }
}

print "  include  " . $server->include_path . "/ip_access/{$this->uri}.conf*;\n";
print "  include  " . $server->include_path . "/user_admin_access/{$this->uri}.conf*;\n";
print "  set \$ai_train_allow 0;\n";
print "  set \$ai_evasive_allow 0;\n";
print "  include  " . $server->include_path . "/ai_policy/{$this->uri}.conf*;\n";
?>

  ### TXP location contract -- inline (no shared common include).
  ### NB no acme-challenge block here: BOA injects it via $extra_config
  ### (provision_nginx_vhost_config) into every main server block.

  ###
  ### SHARED PROTECTIONS, RE-STATED. This vhost deliberately does not include
  ### nginx_vhost_common.conf (Drupal-shaped end to end), so every CMS-AGNOSTIC
  ### guard that include carries must be repeated here -- otherwise TXP sites
  ### are the only unprotected sites on the box. In particular the AI policy:
  ### the vhost above defaults $ai_train_allow/$ai_evasive_allow to 0 and
  ### includes the per-site ai_policy fragment, but the ENFORCEMENT lives in
  ### the include, so without these lines the policy renders and is inert.
  ###
  ### The IDS ban geo is declared by the master render, so it needs no gate.
  if ($is_banned) { return 444; }
<?php
// The $boa_fleet_* maps live in the BOA-written zones file, so the guard
// renders only when that file declares them.  The https template includes
// this one after its own server block: reuse the body it already read.
if (!isset($txp_zones_body)) {
  $txp_zones_file = '/etc/nginx/conf.d/limit-req-zones-boa.conf';
  $txp_zones_body = @is_file($txp_zones_file)
    ? (string) @file_get_contents($txp_zones_file)
    : '';
}
if (strpos($txp_zones_body, 'map $boa_fleet_uaid $boa_fleet_block {') !== FALSE):
?>
  ### Declared crawler-fleet fingerprint: 429, which no IDS scorer counts.
  if ($boa_fleet_block) { return 429; }
<?php endif; ?>
  ### Cache-poisoning probe signature in the query string; 404, not 444, so a
  ### scanner learns nothing from the difference with a genuine miss.
  if ( $args ~* "=PHP[A-Z0-9]{8}-" ) { return 404; }
  ### Credential and dotfile probes (.env/.git/.aws/.ssh, creds JSON,
  ### settings.py). Nothing a TXP site legitimately serves.
  if ($is_secret_path) { return 444; }
  ### Foreign-CMS admin probes (wp-*, administrator/, phpmyadmin). A TXP site
  ### serves none of these paths, so the whole-segment match is free of FPs.
  if ($is_cms_probe) { return 444; }
  if ($is_ai_forged) { return 444; }
  set $ai_train_block $is_ai_training;
  if ($ai_train_allow) { set $ai_train_block ''; }
  if ($ai_train_block) { return 444; }
  set $ai_evasive_block $is_ai_evasive;
  if ($ai_evasive_allow) { set $ai_evasive_block ''; }
  if ($ai_evasive_block) { return 444; }
  if ($is_crawler) { return 444; }
  ### Scanner payload maps: attack strings in the args, exploit-framework and
  ### vulnerability-scanner user agents. Both drop at the edge with a 444 an
  ### IDS scorer counts, instead of being answered by the tenant's PHP.
  if ($is_denied) { return 444; }
  if ($ua_denied) { return 444; }
  ### TLS ClientHello sent to the plain HTTP port.
  if ($tls_on_plain) { return 444; }
  ### Recommended headers (no location here defines its own add_header, so the
  ### server-level pair is inherited everywhere; if one ever does, re-state).
  add_header X-Content-Type-Options "nosniff";
  add_header X-Frame-Options "SAMEORIGIN" always;

  ### Operator escape hatch: the box-wide high-load protection drop-in that
  ### every other vhost includes. Without it a TXP site silently ignores
  ### operator load shedding. (Capacity-shed layer, part 1 of 2: the
  ### zone-dependent limits -- AI-class rate limits and the per-vhost
  ### anonymous-render cap -- are a designed round-3 item, presence-gated on
  ### the BOA-written zones file, NOT copied blind: their key semantics are
  ### CMS-specific.)
  include /data/conf/nginx_high_load.c*;

  ### Reject non-standard request methods without a 405 body (shared-include
  ### parity; CMS-agnostic).
  if ($request_method !~ ^(?:GET|HEAD|POST|PUT|PATCH|DELETE|OPTIONS)$) {
    return 444;
  }

  ### Deny listed requests for security reasons (verbatim from the shared
  ### include: backups, dumps, editor swap files, VCS metadata, composer files).
  location ~* (\.(?:git.*|htaccess|engine|config|inc|ini|info|install|make|module|profile|test|po|sh|.*sql|theme|twig|tpl(\.php)?|xtmpl|yml)(~|\.sw[op]|\.bak|\.orig|\.save)?$|^(\..*|Entries.*|Repository|Root|Tag|Template|composer\.(json|lock))$|^#.*#$|\.php(~|\.sw[op]|\.bak|\.orig\.save))$ {
    access_log off;
    log_not_found off;
    return 404;
  }

  location ~ /\.(?!well-known) { deny all; }
  location ~* \.txp$ { return 403; }
  location ~* ^/themes/.*/manifest\.json$ { deny all; }
<?php if (!$txp_files_open): ?>
  ### Direct file downloads DENIED by default. Downloads belong on the
  ### front controller (/index.php?s=file_download&id=N), which enforces the
  ### per-file status/privs, the download counter and file_download_header;
  ### serving public/files/ directly bypasses all three. Per-site opt-out:
  ### touch <aegir_root>/static/control/txp-files-open-<uri>.info
  location ^~ /files/ { return 403; }
<?php endif; ?>

  location = /favicon.ico { access_log off; try_files $uri =204; }
  location = /robots.txt  { access_log off; try_files $uri =404; }

  # Public PHP whitelist: EXACTLY index.php and css.php.
  location = /index.php {
    if (-f /data/conf/suspended/<?php print $script_user; ?>.pid) { return 503; }
    open_file_cache off;
    if (-f <?php print $aegir_root; ?>/static/control/http-off.pid) {
      add_header Retry-After 300 always;
      add_header Cache-Control "no-store, must-revalidate" always;
      return 503;
    }
<?php if ($txp_box_named): ?>
    if (-f <?php print $aegir_root; ?>/static/control/http-off-host.pid) {
      add_header Retry-After 300 always;
      add_header Cache-Control "no-store, must-revalidate" always;
      return 503;
    }
<?php endif; ?>
    fastcgi_pass unix:<?php print $user_socket; ?>;
  }
  location = /css.php {
    if (-f /data/conf/suspended/<?php print $script_user; ?>.pid) { return 503; }
    open_file_cache off;
    if (-f <?php print $aegir_root; ?>/static/control/http-off.pid) {
      add_header Retry-After 300 always;
      add_header Cache-Control "no-store, must-revalidate" always;
      return 503;
    }
<?php if ($txp_box_named): ?>
    if (-f <?php print $aegir_root; ?>/static/control/http-off-host.pid) {
      add_header Retry-After 300 always;
      add_header Cache-Control "no-store, must-revalidate" always;
      return 503;
    }
<?php endif; ?>
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
      open_file_cache off;
      if (-f <?php print $aegir_root; ?>/static/control/http-off.pid) {
        add_header Retry-After 300 always;
        add_header Cache-Control "no-store, must-revalidate" always;
        return 503;
      }
<?php if ($txp_box_named): ?>
      if (-f <?php print $aegir_root; ?>/static/control/http-off-host.pid) {
        add_header Retry-After 300 always;
        add_header Cache-Control "no-store, must-revalidate" always;
        return 503;
      }
<?php endif; ?>
      # Full param set re-declared: nginx fastcgi_param inheritance is
      # all-or-nothing, and alias needs $request_filename.
      include fastcgi_params;
      fastcgi_param HTTP_PROXY "";
      fastcgi_param HTTP_HOST $host;
      fastcgi_param REQUEST_SCHEME $scheme;
      fastcgi_param MAIN_SITE_NAME <?php print $this->uri; ?>;
      fastcgi_param SCRIPT_FILENAME $request_filename;
      fastcgi_param db_type   $boa_db_type;
      fastcgi_param db_name   $boa_db_name;
      fastcgi_param db_user   $boa_db_user;
      fastcgi_param db_passwd $boa_db_passwd;
      fastcgi_param db_host   $boa_db_host;
      fastcgi_param db_port   $boa_db_port;
      fastcgi_param db_creds_urlencoded $boa_db_creds_urlencoded;
      fastcgi_pass unix:<?php print $user_socket; ?>;
    }
    # No other PHP executes under the admin path (vendors/, admin-themes/,
    # plugins/ ship .php that must only ever be read as assets, never run).
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
