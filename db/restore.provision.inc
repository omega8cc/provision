<?php

function drush_db_provision_restore_validate() {
  d()->service('db')->connect();
}

function drush_db_post_provision_restore() {
  d()->service('db')->destroy_site_database();
  // The restored site runs on the database its deploy made. Read the site's
  // drushrc.php back, as a migrate does, so the task's result names that
  // database and the panel records it. Only after the drop: it takes the old
  // database from this context.
  provision_reload_config('site', d()->site_path . '/drushrc.php');
}

