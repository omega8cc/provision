<?php

function drush_db_provision_restore_validate() {
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
  d()->service('db')->connect();
}

function drush_db_post_provision_restore() {
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
  d()->service('db')->destroy_site_database();
}

