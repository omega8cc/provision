<?php
/**
 * @file
 * nginx vhost template for a Grav 2 site capsule (http).
 *
 * Selected via hook_provision_config_load_templates() when the platform is a
 * Grav root. The one structural difference from every Drupal vhost: the
 * docroot is the SITE CAPSULE (sites/<uri>/ -- a complete Grav install,
 * boa-grav D-003), not the platform root. The location contract lives in
 * grav_locations.tpl.php, shared with the https template.
 *
 * Enforced PHP (D-005 addendum): the enforced-version FPM socket is pinned at
 * render time (BOA default 8.4; 8.5 then the 8.3 floor as fallbacks; bare
 * socket last resort) -- never the instance's per-site-selectable pool, and
 * the per-site fpm.info/multi-fpm.info machinery is deliberately not
 * consulted.
 */
$this->root = provision_auto_fix_platform_root($this->root);
$grav_capsule = "{$this->root}/sites/{$this->uri}";

print "include  " . $server->include_path . "/user_admin_access_map/{$this->uri}.conf*;\n";
$script_user = d('@server_master')->script_user;
if (!$script_user) {
  $script_user = drush_get_option('script_user');
}
if (!$script_user && $server->script_user) {
  $script_user = $server->script_user;
}
$user_socket = '/run/' . $script_user . '.fpm.socket';
foreach (array('84', '85', '83') as $grav_php_ver) {
  if (file_exists('/run/' . $script_user . '.' . $grav_php_ver . '.fpm.socket')) {
    $user_socket = '/run/' . $script_user . '.' . $grav_php_ver . '.fpm.socket';
    break;
  }
}
$aegir_root = d('@server_master')->aegir_root;
$satellite_mode = d('@server_master')->satellite_mode;

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
      print "  return 301 \$scheme://{$this->redirection}\$request_uri;\n";
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
  fastcgi_param MAIN_SITE_NAME <?php print $this->uri; ?>;
  set $main_site_name "<?php print $this->uri; ?>";
  fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
  # Pin the environment:// stream to the site uri (never the request
  # hostname -- aliases must not fork per-host config). Proven to reach
  # getenv() under this FPM stack (spike Q7).
  fastcgi_param GRAV_ENVIRONMENT <?php print $this->uri; ?>;
  # Truthful scheme for Grav's Uri fallbacks (stock fastcgi_params does not
  # carry REQUEST_SCHEME under this stack), and -- presence-gated on the
  # shared map -- the local HTTPS front's X-Forwarded-Proto translated into
  # HTTPS for cert-less vhosts: Grav emits ABSOLUTE URLs from the request
  # scheme and its 2.0 default refuses the header app-side, so without this
  # a proxied https page carries http:// links and the session cookie loses
  # its Secure flag. MUST stay at server level: a location-level
  # fastcgi_param would cancel the whole inherited param set.
  fastcgi_param REQUEST_SCHEME $scheme;
<?php
$grav_fe_zones_body = @is_file('/etc/nginx/conf.d/limit-req-zones-boa.conf')
  ? (string) @file_get_contents('/etc/nginx/conf.d/limit-req-zones-boa.conf')
  : '';
if (strpos($grav_fe_zones_body, '$boa_grav_fe_https') !== FALSE) {
  print "  fastcgi_param HTTPS \$boa_grav_fe_https;\n";
}
?>
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
  root  <?php print $grav_capsule; ?>;
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

include(PROVISION_GRAV_DIR . '/templates/grav_locations.tpl.php');

$if_subsite = $this->data['http_subdird_path'] . '/' . $this->uri;
if (provision_hosting_feature_enabled('subdirs') && provision_file()->exists($if_subsite)->status()) {
  print "  include  " . $if_subsite . "/*.conf;\n";
}
?>
}
