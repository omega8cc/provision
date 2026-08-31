<?php
/**
 * @file
 * The Grav install engine: first admin account + cache warm.
 *
 * The capsule itself is materialised by the pre hook
 * (_provision_grav_create_directories()); by the time this engine runs, an
 * unpacked admin-bundle tree at sites/<uri>/ is already a working site (Grav
 * ships no installer wizard and needs no database). What is left is the first
 * admin account (a pre-hashed user/accounts/<user>.yaml -- never the
 * interactive, argv-leaking new-user CLI) and one cache warm through the Grav
 * CLI, which doubles as the proof that the enforced-PHP seam works on this
 * box.
 */

function _provision_grav_install() {
  $username = 'admin';
  $email = d()->client_email;
  if (!$email) {
    $email = 'admin@' . d()->uri;
  }

  $password = _provision_grav_create_admin_account($username, $email);
  if ($password === FALSE || drush_get_error()) {
    return drush_set_error('PROVISION_GRAV_INSTALL_FAILED',
      dt('Could not create the first admin account for !uri.', array('!uri' => d()->uri)));
  }

  // The one-time surfacing contract (parallel of the Drupal one-time login
  // link): the task log carries the credentials once; nothing here persists
  // them. The hosting_grav frontend decides the welcome-email plumbing.
  drush_log(dt('Created admin account "!user" (!email) for !uri.',
    array('!user' => $username, '!email' => $email, '!uri' => d()->uri)), 'ok');
  drush_set_option('grav_admin_username', $username);
  drush_set_option('grav_admin_password', $password);
  drush_set_option('login_link', 'https://' . d()->uri . '/admin');

  // Cache warm via the Grav CLI (umask 0002, enforced PHP, vendor asserted).
  // Failure here is a warning, not an install failure: the site serves without
  // a warmed cache; the CLI seam problems surface loudly in the task log.
  _provision_grav_cli('grav', 'clearcache -n');

  return TRUE;
}

_provision_grav_install();
