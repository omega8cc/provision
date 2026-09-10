<?php

function drush_db_provision_delete_validate() {
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
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
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
  if (d()->type === 'site') {
    d()->service('db')->destroy_site_database();
  }
}
