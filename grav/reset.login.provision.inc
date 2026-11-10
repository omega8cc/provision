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
  _provision_grav_login_unlock($username);
  drush_set_option('grav_admin_username', $username);
  drush_set_option('grav_admin_password', $password);
  drush_set_option('login_link', 'https://' . d()->uri . '/admin');
}

/**
 * Lift a login lockout standing against the account.
 *
 * After too many failed logins the Login plugin refuses the account, even
 * with the right password, until its interval ends; Admin2's password reset
 * (the API plugin's) lifts that, so a reset here does too, or its new password
 * is refused for the rest of the interval. The counters live under
 * cache/login, where the web pool writes them as the per-version web user
 * (FPM umask 022) and the instance user cannot delete them: the capsule is
 * handed back first, then Login's own unlock-user command (Login 3.9 and
 * later) clears the account's counters in every context, with the address
 * counters it tripped. The command says it cleared them even when a delete
 * failed, so it is only believed after a heal that ran clean. Not fatal: the
 * password is already rotated, and a lockout ends on its own.
 */
function _provision_grav_login_unlock($username) {
  $site_path = d()->site_path;
  $vars = array('!user' => $username);
  if (!is_dir($site_path . '/cache/login') || !_provision_grav_login_enabled($site_path, d()->uri)) {
    return TRUE;
  }
  if (!is_file($site_path . '/user/plugins/login/cli/UnlockUserCommand.php')) {
    drush_log(dt('This Login plugin has no unlock-user command; a login lockout of account "!user" ends on its own.', $vars), 'notice');
    return TRUE;
  }
  // The CLI seam fails the task when it cannot run at all; this step is not
  // worth that, so the same two conditions are only a warning here.
  if (_provision_grav_php_bin() === FALSE || !is_readable($site_path . '/vendor/autoload.php')) {
    drush_log(dt('Could not run the Grav CLI to lift a login lockout of account "!user"; it ends on its own after the Login plugin\'s interval.', $vars), 'warning');
    return FALSE;
  }
  $healed = _provision_grav_root_heal($site_path);
  if (!_provision_grav_cli('plugin', 'login unlock-user -u ' . escapeshellarg($username) . ' -n')) {
    drush_log(dt('Could not lift a login lockout of account "!user"; it ends on its own after the Login plugin\'s interval.', $vars), 'warning');
    return FALSE;
  }
  if ($healed !== TRUE) {
    drush_log(dt('Ran the Login plugin\'s unlock for account "!user", but the site could not be handed back first: a lockout the web server wrote may stand until the Login plugin\'s interval ends.', $vars), 'warning');
    return FALSE;
  }
  drush_log(dt('Lifted any login lockout of account "!user".', $vars), 'ok');
  return TRUE;
}

/**
 * Is the Login plugin switched on in this capsule? Its CLI refuses a plugin
 * that is off, and an off plugin keeps no lockout. Read as the KahunaCart
 * check reads its plugin: the host overlay first, then user/config; no
 * setting means on.
 */
function _provision_grav_login_enabled($site_path, $uri) {
  foreach (array($site_path . '/user/env/' . $uri . '/config/plugins/login.yaml',
    $site_path . '/user/config/plugins/login.yaml') as $yaml) {
    if (is_file($yaml) && is_readable($yaml)) {
      $body = file_get_contents($yaml);
      if ($body !== FALSE && preg_match('/^enabled:\s*(\S+)/m', $body, $m)) {
        return !in_array(strtolower(trim($m[1], '\'"')), array('false', '0', 'no', 'off'), TRUE);
      }
    }
  }
  return TRUE;
}
