<?php

// Fetch the cron key from the Backdrop site and set it in a drush context.
//
// Backdrop stores the cron key in state (state_get('cron_key')), NOT in the
// {variable} table the D7 engine reads -- the endpoint is
// core/cron.php?cron_key=<key>. Runs in-process under a FULL bootstrap. Guard
// on state_get() so a partial bootstrap never fatals; fall back to variable_get.

if (function_exists('state_get')) {
  drush_set_option('cron_key', state_get('cron_key', 'backdrop'));
}
elseif (function_exists('variable_get')) {
  drush_set_option('cron_key', variable_get('cron_key', 'backdrop'));
}
