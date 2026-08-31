<?php
/**
 * @file
 * The Grav cache-clear engine.
 *
 * `bin/grav clearcache` through the guarded CLI seam (enforced PHP, umask
 * 0002, vendor asserted, never root). NB it also touches
 * user/config/system.yaml (upstream behaviour) -- harmless under the group
 * ownership model.
 */

function _provision_grav_clear() {
  return _provision_grav_cli('grav', 'clearcache -n');
}

_provision_grav_clear();
