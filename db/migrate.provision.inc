<?php

function drush_db_provision_migrate_validate() {
  d()->service('db')->connect();
}

/**
 * Remember the database the site is migrated FROM.
 *
 * The post hook drops the source database once the deploy has finished, and
 * it finds that database by re-reading the site's drushrc.php. When the
 * platform and the domain stay the same and only the database server changes,
 * that file belongs to the migrated site itself and the deploy has already
 * rewritten it with the NEW credentials, so the drop looked for the new name
 * on the old server: the old database leaked whenever the target server had
 * to pick a suffixed name, and when both servers were the same one the drop
 * took the freshly imported database instead. The credentials read here,
 * before the deploy runs, are still the source's; the password is not needed
 * to drop a database and revoke its user, so it is not kept.
 */
function drush_db_pre_provision_migrate() {
  provision_reload_config('site', d()->site_path . '/drushrc.php');
  $source = d()->service('db')->fetch_site_credentials();
  if (!empty($source['db_name'])) {
    drush_set_option('provision_migrate_source_db', array(
      'db_name' => $source['db_name'],
      'db_user' => $source['db_user'],
    ));
  }
}

// Deletes the old database credentials
function drush_db_post_provision_migrate() {
  provision_reload_config('site', d()->site_path . '/drushrc.php');
  $source = drush_get_option('provision_migrate_source_db', array());
  if (is_array($source) && !empty($source['db_name'])) {
    d()->service('db')->destroy_site_database($source);
  }
  else {
    d()->service('db')->destroy_site_database();
  }
}
