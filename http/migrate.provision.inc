<?php

function drush_http_pre_provision_migrate() {
  d()->service('http')->delete_config('site'); 
}

function drush_http_pre_provision_migrate_rollback() {
  // A failed migrate that kept the site on its target (see
  // provision_migrate_failure_outcome()) serves it from the vhost the target
  // verify wrote; the source's context in this process still names the old
  // platform, so writing it here would point the site back at a directory
  // that is gone.
  if (drush_get_option('migrate_kept_on_target', FALSE)) {
    return;
  }
  d()->service('http')->create_config('site');
}
