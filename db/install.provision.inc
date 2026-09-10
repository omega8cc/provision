<?php

function drush_db_provision_install_validate() {
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
  d()->service('db')->connect();
}

function drush_db_pre_provision_install() {
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
  d()->service('db')->create_site_database();
}

function drush_db_pre_provision_install_rollback() {
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
  d()->service('db')->destroy_site_database();
}
