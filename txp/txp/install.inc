<?php
/**
 * @file
 * Site installation for Textpattern.
 *
 * Runs the vendored headless shim (txp/txp/resources/txp-install-shim.php) in
 * its own PHP >= 8.4 process (D-008 enforced; _provision_txp_php_bin()). The
 * shim mirrors Textpattern's official CLI installer (setup/setup.php) plus the
 * two multisite constants it lacks, and consumes the private/config.php the
 * settings writer has already rendered -- ONE authority for the config from
 * birth. One shim process per site (setup_makeConfig() constants are
 * process-global in TXP's own code).
 *
 * After a successful shim run this engine, over mysqli with the site's own
 * credentials: ASSERTS the admin-user row exists (TXP's create_user() returns
 * FALSE silently and setup_db() ignores it -- the "install succeeded, no login
 * exists" trap), then seeds the prefs the multisite installer gets wrong or
 * leaves web-context-dependent: permlink_mode (the clean-URL self-probe
 * silently degrades to 'messy' when the vhost is not live yet -- and at
 * provision time it never is: verify renders the vhost after install),
 * siteurl (never self-heals in multisite), path_to_site (self-heal is
 * public-side only), file_base_path + tempdir (seeded SHARED by the installer;
 * the backup-scope law requires per-site paths), production_status
 * (seeds 'testing', which leaks trace output on every public page).
 */

$php_bin = _provision_txp_php_bin();
if ($php_bin === FALSE) {
  return drush_set_error('PROVISION_TXP_PHP_TOO_OLD',
    dt('No PHP >= 8.4 binary found; refusing to install (enforced floor, D-008).'));
}

$root = d()->root;
$uri = d()->uri;
$site_path = d()->site_path;

// Site DB credentials, set by the db service pre-hooks (CMS-agnostic).
$db_user = urldecode(drush_get_option('db_user', '', 'site'));
$db_passwd = urldecode(drush_get_option('db_passwd', '', 'site'));
$db_host = drush_get_option('db_host', 'localhost', 'site');
$db_port = drush_get_option('db_port', '', 'site');
$db_name = drush_get_option('db_name', '', 'site');
if ($db_name === '' || $db_user === '') {
  return drush_set_error('PROVISION_TXP_INSTALL_FAILED',
    dt('No site database credentials in the task context; cannot install.'));
}
$shim_db_host = $db_host . ($db_port ? ':' . $db_port : '');

// Admin account: name fixed, password minted here and surfaced via the
// login/welcome plumbing options the frontend consumes.
$admin_user = 'admin';
$admin_pass = provision_password(16);
$client_email = drush_get_option('client_email', FALSE);
if (!$client_email || !filter_var($client_email, FILTER_VALIDATE_EMAIL)) {
  $client_email = 'admin@' . $uri;
}

// Language: map the Aegir site language to a TXP pack code present on the
// platform; TXP codes are xx-yy shaped (en-gb), Aegir sends bare codes (en).
$language = d()->language ? d()->language : 'en';
if (!is_readable($root . '/textpattern/lang/' . $language . '.ini')) {
  $fallbacks = array(
    $language . '-' . $language,
    ($language === 'en') ? 'en-gb' : '',
    'en-gb',
  );
  foreach ($fallbacks as $candidate) {
    if ($candidate !== '' && is_readable($root . '/textpattern/lang/' . $candidate . '.ini')) {
      $language = $candidate;
      break;
    }
  }
}

$cfg = array(
  'site' => array(
    'language_code' => $language,
    'public_url' => 'http://' . $uri,
    'admin_url' => $uri . '/' . PROVISION_TXP_ADMIN_PATH,
    'cookie_domain' => '',
    'admin_theme' => 'hive',
    // The 4.9-bundled front-end theme; an empty value seeds bare placeholder
    // page templates (a site that serves comment markup).
    'public_theme' => 'four-point-nine',
    'content_directory' => '',
  ),
  'database' => array(
    'user' => $db_user,
    'password' => $db_passwd,
    'host' => $shim_db_host,
    'db_name' => $db_name,
    'table_prefix' => '',
  ),
  'user' => array(
    'full_name' => 'Site Administrator',
    'email' => $client_email,
    'login_name' => $admin_user,
    'password' => $admin_pass,
  ),
  'paths' => array(
    'txpath' => $root . '/textpattern',
    'multisite_root_path' => $site_path,
    'config_php' => $site_path . '/private/config.php',
  ),
);

// The JSON carries credentials: 0600, in the per-site tmp dir, removed in
// every exit path below.
// The name is unpredictable and the file is created 0600 by UMASK, not by a
// post-write chmod: the containing tmp dir is group-writable (web group), so a
// create-then-chmod leaves a window in which the db password and the minted
// admin password are group-readable.
$json_file = $site_path . '/tmp/txp-install-' . substr(md5($uri . microtime() . mt_rand()), 0, 16) . '.json';
$old_umask = umask(0177);
$json_written = @file_put_contents($json_file, json_encode($cfg));
umask($old_umask);
if ($json_written === FALSE) {
  return drush_set_error('PROVISION_TXP_INSTALL_FAILED',
    dt('Could not write the shim config to @file.', array('@file' => $json_file)));
}

$shim = dirname(__FILE__) . '/resources/txp-install-shim.php';
$result = drush_shell_exec('%s %s --config=%s', $php_bin, $shim, $json_file);
$output = implode("\n", (array) drush_shell_exec_output());
@unlink($json_file);

drush_log(dt("TXP install shim output:\n!output", array('!output' => $output)), $result ? 'info' : 'error');
if (!$result) {
  return drush_set_error('PROVISION_TXP_INSTALL_FAILED',
    dt('The TXP install shim failed for !uri.', array('!uri' => $uri)));
}

// Post-install assertions + pref seeding, over mysqli with the site's creds.
mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);
try {
  $mysqli = mysqli_init();
  $connect_host = $db_host;
  $connect_port = $db_port ? (int) $db_port : 3306;
  $mysqli->real_connect($connect_host, $db_user, $db_passwd, $db_name, $connect_port);
  $mysqli->set_charset('utf8mb4');

  // THE assertion: no admin row means no login exists -- fail the install.
  $stmt = $mysqli->prepare("SELECT COUNT(*) FROM txp_users WHERE name = ?");
  $stmt->bind_param('s', $admin_user);
  $stmt->execute();
  $stmt->bind_result($count);
  $stmt->fetch();
  $stmt->close();
  if ((int) $count !== 1) {
    $mysqli->close();
    return drush_set_error('PROVISION_TXP_INSTALL_FAILED',
      dt('The admin user row is missing after install (TXP create_user() failed silently); rolling back.'));
  }

  $prefs = array(
    'permlink_mode' => 'section_title',
    'siteurl' => $uri,
    'path_to_site' => $site_path . '/public',
    'file_base_path' => $site_path . '/public/files',
    'tempdir' => $site_path . '/tmp',
    'production_status' => 'live',
  );
  $stmt = $mysqli->prepare("UPDATE txp_prefs SET val = ? WHERE name = ?");
  foreach ($prefs as $name => $val) {
    $stmt->bind_param('ss', $val, $name);
    $stmt->execute();
  }
  $stmt->close();
  $mysqli->close();
}
catch (Exception $e) {
  return drush_set_error('PROVISION_TXP_INSTALL_FAILED',
    dt('Post-install DB assertions failed for !uri: !msg', array('!uri' => $uri, '!msg' => $e->getMessage())));
}
drush_log(dt('Admin user asserted and per-site prefs seeded for !uri.', array('!uri' => $uri)), 'ok');

// The frontend's "Log in" affordance: a real TXP password-reset link when
// the token shim delivers, the admin login URL otherwise. The welcome mail
// carries the same link (never a plaintext password).
$login_link = _provision_txp_mint_login_link();
if ($login_link === FALSE) {
  $login_link = 'https://' . $uri . '/' . PROVISION_TXP_ADMIN_PATH . '/';
}
drush_set_option('login_link', $login_link);
// DELIBERATELY not persisted: the 'site' option context is var_export()ed
// wholesale into sites/<uri>/drushrc.php by the shared Drushrc writer, which
// lives at the site-dir root and therefore rides EVERY backup archive (and the
// client-readable backup symlink). The minted password has no consumer beyond
// this process -- the welcome mail carries a reset link, never a password.
_provision_txp_send_welcome_mail($client_email, $login_link);

// The shared textpattern/setup dir stays (other sites + future installs need
// it); the per-site admin/setup dir is spent -- remove it like the multisite
// updater would.
$site_setup = $site_path . '/admin/setup';
if (is_dir($site_setup)) {
  _provision_recursive_delete($site_setup);
  drush_log(dt('Removed the spent per-site admin/setup directory.'), 'ok');
}
