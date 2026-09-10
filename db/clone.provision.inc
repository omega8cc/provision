<?php

function drush_db_provision_clone_validate() {
  if (provision_platform_is_grav(d())) {
    // A Grav capsule is DB-less whichever db_server the site was attached
    // to: with a real MySQL node the deploy took the MyQuick import path
    // and failed on the dump a files-only backup never wrote.
    return;
  }
  d()->service('db')->connect();
}
