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
 */
?>
  ### GRAV location contract -- inline (no shared common include).

  # Billing suspend parity (the global.inc chain never runs for a capsule) --
  # the converged foreign-CMS shape.
  if (-f /data/conf/suspended/<?php print $script_user; ?>.pid) { return 503; }

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

  location = /favicon.ico { access_log off; try_files $uri /index.php?$args; }
  location = /robots.txt  { access_log off; try_files $uri /index.php?$args; }

  # The ONLY PHP that executes: the front controller (Admin2, the api plugin
  # and the plugin-asset-map fast path all route through it).
  location = /index.php {
    fastcgi_pass unix:<?php print $user_socket; ?>;
  }

  # Front controller (keeps the query string).
  location / {
    try_files $uri $uri/ /index.php?$args;
  }

  # Everything else that ends in .php 404s (writable dirs included).
  location ~* \.php$ { return 404; }
