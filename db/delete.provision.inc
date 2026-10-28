<?php

function drush_db_provision_delete_validate() {
  if (d()->type === 'site') {
    d()->service('db')->connect();
  }
}

/**
 * Implementation of hook_provision_delete()
 *
 * This will drop the database, revoke the privileges and flush the privileges.
 */
function drush_db_provision_delete() {
  if (d()->type === 'site') {
    // A site that was never installed (no settings.php, no database name in
    // its context) has nothing to drop: one line, not the drop's warning of
    // an unknown name, which turned such a Delete into a warning state.
    if (_provision_drupal_site_never_installed()) {
      drush_log(dt('No database was created for this site; nothing to drop.'), 'info');
      return;
    }
    d()->service('db')->destroy_site_database();
  }
}
