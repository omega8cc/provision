<?php
$satellite_mode = drush_get_option('satellite_mode');
if (!$satellite_mode && $server->satellite_mode) {
  $satellite_mode = $server->satellite_mode;
}
?>

location ^~ /<?php print $subdir; ?>/ {
  root   /var/www/nginx-default;
  index  index.html index.htm;
  ### Do not reveal Aegir front-end URL here.
}
