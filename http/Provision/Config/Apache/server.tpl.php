# Aegir web server configuration file

NameVirtualHost *:<?php print $http_port; ?>


<VirtualHost *:<?php print $http_port; ?>>
  ServerName default

  <IfModule mod_rewrite.c>
    RewriteEngine on
    RewriteRule ^(?!(/\.well-known/acme-challenge/.+)) - [R=404,L,NC]
  </IfModule>
</VirtualHost>


<IfModule !env_module>
  LoadModule env_module modules/mod_env.so
</IfModule>

<IfModule !rewrite_module>
  LoadModule rewrite_module modules/mod_rewrite.so
</IfModule>

# Mitigation for https://www.drupal.org/SA-CORE-2018-002
<IfModule mod_rewrite.c>
    RewriteEngine On
    RewriteCond %{QUERY_STRING} (.*)(23value|23default_value|element_parents=%23)(.*) [NC]
    RewriteCond %{REQUEST_METHOD} POST [NC]
    RewriteRule ^.*$  - [R=403,L]
</IfModule>

<?php
if (drush_get_option('provision_apache_conf_suffix', FALSE)) {
  $include_statement = 'IncludeOptional ';
  $include_suffix = '/*.conf';
}
else {
  $include_statement = 'Include ';
  $include_suffix = '';
}

?>

# other configuration, not touched by aegir
# this allows you to override aegir configuration, as it is included before
<?php print $include_statement . $http_pred_path . $include_suffix ?>

# virtual hosts
<?php print $include_statement . $http_vhostd_path . $include_suffix ?>

# platforms
<?php print $include_statement . $http_platformd_path . $include_suffix ?>

# other configuration, not touched by aegir
# this allows to have default (for example during migrations) that are eventually overridden by aegir
<?php print $include_statement . $http_postd_path . $include_suffix ?>

<?php print $extra_config; ?>
