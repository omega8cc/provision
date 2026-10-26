<?php

/**
 * @file
 * Provision login-reset command (Grav layer).
 *
 * The Drupal flow mints a one-time login URL; Grav has no such mechanism, so
 * the honest equivalent is a password rotation on the first admin account:
 * regenerate the pre-hashed user/accounts/admin.yaml (the same writer the
 * install engine uses -- never the argv-leaking new-user CLI) and surface the
 * new credentials once via the task options, exactly like install does.
 */

function drush_provision_grav_provision_login_reset() {
  if (!provision_platform_is_grav(d())) {
    return;
  }
  if (d()->type !== 'site') {
    return;
  }
  $username = 'admin';
  $email = d()->client_email;
  if (!$email) {
    $email = 'admin@' . d()->uri;
  }
  $password = _provision_grav_create_admin_account($username, $email);
  if ($password === FALSE || drush_get_error()) {
    return drush_set_error('PROVISION_GRAV_INSTALL_FAILED',
      dt('Could not rotate the admin account password for !uri.', array('!uri' => d()->uri)));
  }
  drush_log(dt('Rotated the password of account "!user" for !uri.',
    array('!user' => $username, '!uri' => d()->uri)), 'ok');
  // The plugin's own password change also forgets the account's remembered
  // logins (the Login plugin's remember-me triplets): a remembered browser
  // would otherwise be signed straight back in. Not fatal: the password and
  // the token cutoff are already in place.
  $triplets = d()->site_path . '/user/data/rememberme/' . sha1($username) . '.yaml';
  if (file_exists($triplets) && !@unlink($triplets)) {
    _provision_grav_root_heal(d()->site_path);
    @unlink($triplets);
  }
  clearstatcache();
  if (file_exists($triplets)) {
    drush_log(dt('Could not forget the remembered logins of account "!user" (!file).',
      array('!user' => $username, '!file' => $triplets)), 'warning');
  }
  drush_set_option('grav_admin_username', $username);
  drush_set_option('grav_admin_password', $password);
  drush_set_option('login_link', 'https://' . d()->uri . '/admin');
}
