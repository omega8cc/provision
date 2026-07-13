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
$langcode = d()->language ? d()->language : 'en';

// Drive the install with bee, the native Backdrop CLI (mirrors the D10/D11
// site-local shell-out): install.sh run directly via drush's chdir + shell exec
// silently installs nothing, while bee -- which shells out to install.sh with an
// inline `cd $root && ...` -- installs correctly and writes config to the pinned
// dirs. bee bootstraps for command discovery off the pre-written settings.php.
$bee = '/opt/tools/bee/bee.php';
if (!file_exists($bee)) {
  return drush_set_error('PROVISION_BACKDROP_BEE_MISSING',
    dt('bee not found at @bee; Backdrop install needs it (enable _BACKDROP_SUPPORT).', array('@bee' => $bee)));
}

// bee resolves the install --url from sites/sites.php; make sure the site is
// mapped there before the install so the codebase resolves to sites/<uri>/ and
// its pinned config dirs, not the platform root.
$sites_php = $root . '/sites/sites.php';
$sites_line = '$sites[\'' . $uri . '\'] = \'' . $uri . '\';';
$sites_body = @file_get_contents($sites_php);
if ($sites_body === FALSE) {
  $sites_body = "<?php\n";
}
if (strpos($sites_body, $sites_line) === FALSE) {
  @file_put_contents($sites_php, rtrim($sites_body, "\n") . "\n" . $sites_line . "\n");
}

$db_user = urldecode(drush_get_option('db_user', '', 'site'));
$db_pass = urldecode(drush_get_option('db_passwd', '', 'site'));
$db_host = drush_get_option('db_host', 'localhost', 'site');
$db_name = drush_get_option('db_name', '', 'site');

$args = array(
  '--root='      . escapeshellarg($root),
  '--site='      . escapeshellarg($uri),
  'install',
  escapeshellarg($profile),
  '--db-name='   . escapeshellarg($db_name),
  '--db-user='   . escapeshellarg($db_user),
  '--db-pass='   . escapeshellarg($db_pass),
  '--db-host='   . escapeshellarg($db_host),
  '--username='  . escapeshellarg(drush_get_option('admin_user', PROVISION_DEFAULT_ADMIN_USER)),
  '--password='  . escapeshellarg(provision_password()),
  '--email='     . escapeshellarg($mail),
  '--site-name=' . escapeshellarg($uri),
  '--site-mail=' . escapeshellarg($mail),
  '--langcode='  . escapeshellarg($langcode),
  '--auto',
);

$cmd = escapeshellarg($php_bin) . ' ' . escapeshellarg($bee) . ' ' . implode(' ', $args) . ' 2>&1';
drush_log(dt('BACKDROP/INSTALL/CMD @cmd', array('@cmd' => $cmd)), 'info');
$result = drush_shell_cd_and_exec($root, $cmd);
$output = drush_shell_exec_output();
$installed = FALSE;
foreach ($output as $line) {
  drush_log(strip_tags($line), 'notice');
  if (strpos($line, 'successfully') !== FALSE) {
    $installed = TRUE;
  }
}
if (!$installed) {
  return drush_set_error('PROVISION_BACKDROP_INSTALL_FAILED',
    dt('Backdrop install via bee failed for @uri', array('@uri' => $uri)));
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
