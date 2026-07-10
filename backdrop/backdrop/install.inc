<?php
/**
 *  @file
 *    Site installation for Backdrop CMS.
 *
 * Drives core/scripts/install.sh via an explicit modern PHP binary (PHP_BINARY,
 * with a hard >= 7.1 clamp -- Backdrop's floor). Modelled on the D10/D11
 * site-local shell-out MINUS the psr/log patch-revert dance: install.sh is plain
 * PHP that bootstraps Backdrop core directly, so there is no Aegir Drush and
 * nothing to patch/revert. The DB is pre-created by the db install pre-hook and
 * the settings.php is pre-written with pinned config dirs + a non-empty
 * hash_salt + backup_directory=FALSE, so install_backdrop() rewrites nothing
 * (settings_verified path). After a successful install the one-time login link
 * is captured via provision_generate_login_reset() (which spawns its own
 * `drush @site user-login` subprocess -> backdrop-user-login) and stored in the
 * login_link option the front end surfaces.
 *
 * This engine runs in the install hook's own process (no install-backend
 * sub-invoke): install.sh is already a separate PHP process, so there is no
 * multi-connection hazard to isolate.
 */

// These are provided by platform/drupal/install.inc in the Drupal flow, which
// never loads for a Backdrop site; define guarded copies for the Backdrop path.
if (!defined('PROVISION_DEFAULT_ADMIN_USER')) {
  define('PROVISION_DEFAULT_ADMIN_USER', 'admin');
}
if (!function_exists('install_validate_client_email')) {
  function install_validate_client_email($client_email) {
    if (!$client_email || !filter_var($client_email, FILTER_VALIDATE_EMAIL)) {
      $invalid_email = 'this.email@is.invalid';
      $user = function_exists('l') ? l(dt('user account'), 'user') : 'user account';
      drush_log(dt('Error validating client email `!email`. Please check your !user email. To allow the site installation to proceed, the email `!invalid` has been used instead. Please use the `login` link to change the site admin email to a proper valid address.',
        array(
          '!email' => $client_email,
          '!user' => $user,
          '!invalid' => $invalid_email,
        )),
        'warning'
      );
      return $invalid_email;
    }
    else {
      return $client_email;
    }
  }
}

$php_bin = PHP_BINARY;
if (version_compare(PHP_VERSION, '7.1.0', '<')) {
  return drush_set_error('PROVISION_BACKDROP_PHP_TOO_OLD',
    dt('Backdrop needs PHP 7.1+; refusing to install under @v.', array('@v' => PHP_VERSION)));
}

$root    = d()->root;
$uri     = d()->uri;
$profile = d()->profile ? d()->profile : 'standard';
$mail    = install_validate_client_email(drush_get_option('client_email', FALSE));
$db_url  = _provision_backdrop_db_url();
$langcode = d()->language ? d()->language : 'en';

$args = array(
  escapeshellarg($profile),
  '--root='         . escapeshellarg($root),
  '--url='          . escapeshellarg($uri),
  '--db-url='       . escapeshellarg($db_url),
  '--account-name=' . escapeshellarg(drush_get_option('admin_user', PROVISION_DEFAULT_ADMIN_USER)),
  '--account-pass=' . escapeshellarg(provision_password()),
  '--account-mail=' . escapeshellarg($mail),
  '--site-name='    . escapeshellarg($uri),
  '--site-mail='    . escapeshellarg($mail),
  '--langcode='     . escapeshellarg($langcode),
  '--clean-url='    . escapeshellarg(drush_get_option('clean_url', 1)),
);

// install.sh is not shipped +x, so run php as the program (matches bee); cwd
// must be the Backdrop root so its BACKDROP_ROOT = getcwd() resolves.
$cmd = escapeshellarg($php_bin) . ' ./core/scripts/install.sh ' . implode(' ', $args);
drush_log(dt('BACKDROP/INSTALL/CMD @cmd in @root', array('@cmd' => $cmd, '@root' => $root)), 'info');
$result = drush_shell_cd_and_exec($root, $cmd);
$output = drush_shell_exec_output();
foreach ($output as $index => $line) {
  $line_status = ($result && ($index + 1 == count($output))) ? 'success' : 'notice';
  drush_log($line, $line_status);
}
if (!$result) {
  return drush_set_error('PROVISION_BACKDROP_INSTALL_FAILED',
    dt('Backdrop install.sh failed for @uri', array('@uri' => $uri)));
}

// Capture the one-time admin login link (mirror install_7.inc / the D11 shape).
$onetime = provision_generate_login_reset();
if (!empty($onetime)) {
  drush_set_option('login_link', $onetime);
  drush_log(dt('Login url: !onetime', array('!onetime' => $onetime)), 'success');
}
else {
  drush_log(dt('Could not derive a one-time login link (user-login).'), 'warning');
}
