<?php

/**
 * Implementation of hook_provision_pre_restore
 */
function drush_http_pre_provision_restore() {
  d()->service('http')->delete_config('site'); 
}

function drush_http_pre_provision_restore_rollback() {
  // A Restore that kept its restored copy (_provision_restore_keep_replaced())
  // serves it from the vhost its Verify wrote; rendering it again here, from
  // the pre-hook's state, would put the site behind the disabled vhost.
  if (drush_get_option('restore_kept_replaced', FALSE)) {
    return;
  }
  d()->service('http')->create_config('site');
  d()->service('http')->parse_configs();
}

