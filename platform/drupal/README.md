# provision/platform/drupal/*.inc

All files in this folder are included directly into the Drupal site codebase the task is being run on.

The code in these files is run fully bootstrapped to your Drupal sites.

This means the code must be compatible with the Drupal version of the site.

This came up as an issue when Drupal 8.4 moved to Symfony 3: The Yaml::parse() method changed, so we have to change our code to reflect that.

See https://www.drupal.org/node/2911855 for more details.

## The exception: rebuild_d8plus.php

`rebuild_d8plus.php` is not an engine. It is a standalone CLI script that
`_provision_drupal_rebuild_d8plus()` runs in a subprocess, as the site owner,
under the backend's own PHP, to rebuild a Drupal 10+ site's service container
and flush its caches with core's own `drupal_rebuild()` and nothing from
`vendor/drush` loaded (a locked D10/D11 platform runs no site-local Drush under
PHP 8). It is compatible with every Drupal 8+ core that ships
`core/includes/utility.inc`.
