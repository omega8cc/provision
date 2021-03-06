<?php $this->root = provision_auto_fix_platform_root($this->root); ?>

<?php
$aegir_root = drush_get_option('aegir_root');
if (!$aegir_root && $server->aegir_root) {
  $aegir_root = $server->aegir_root;
}
?>

Alias /<?php print $subdir; ?> <?php print $this->root; ?>

<Location /<?php print $subdir; ?>>

  SetEnv db_type  <?php print urlencode($db_type); ?>

  SetEnv db_name  <?php print urlencode($db_name); ?>

  SetEnv db_user  <?php print implode('@', array_map('urlencode', explode('@', $db_user))); ?>

  SetEnv db_passwd  <?php print urlencode($db_passwd); ?>

  SetEnv db_host  <?php print urlencode($db_host); ?>

  SetEnv db_port  <?php print urlencode($db_port); ?>

  <IfModule mod_rewrite.c>
  RewriteEngine on

  RewriteCond %{REQUEST_FILENAME} !-f
  RewriteCond %{REQUEST_FILENAME} !-d
  RewriteCond %{REQUEST_URI} !=/favicon.ico
  RewriteRule ^<?php print $this->root; ?>/(.*)$ /<?php print $subdir; ?>/index.php?q=$1 [L,QSA]
  </IfModule>
</Location>

# Error handler for Drupal > 4.6.7
<Directory ~ "sites/.*/files">
  SetHandler This_is_a_Drupal_security_line_do_not_remove
</Directory>

# Prevent direct reading of files in the private dir.
# This is for Drupal7 compatibility, which would normally drop
# a .htaccess in those directories, but we explicitly ignore those
<Directory ~ "sites/.*/private">
   SetHandler This_is_a_Drupal_security_line_do_not_remove
   Deny from all
   Options None
   Options +FollowSymLinks
</Directory>
