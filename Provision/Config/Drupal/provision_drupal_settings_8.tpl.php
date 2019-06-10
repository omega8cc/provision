<?php
/**
 * @file
 * Template file for a settings.php.
 */
print '<?php' ?>

/**
 * @file Drupal's settings.php file
 *
 * This file was automatically generated by Aegir <?php print $this->version; ?>

 * on <?php print date('r'); ?>.
 *
 * If it is still managed by Aegir, changes to this file may be
 * lost. If it is not managed by aegir, you should remove this header
 * to avoid further confusion.
 */

<?php if ($subdirs_support_enabled): ?>
/**
 * Detecting subdirectory mode
 */
if (isset($_SERVER['SITE_SUBDIR']) && isset($_SERVER['RAW_HOST'])) {
  $base_url = 'http://' . $_SERVER['RAW_HOST'] . '/' . $_SERVER['SITE_SUBDIR'];
}
<?php endif; ?>

<?php if ($this->cloaked): ?>
if (isset($_SERVER['db_name'])) {
  /**
   * The database credentials are stored in the Apache or Nginx vhost config
   * of the associated site with SetEnv (fastcgi_param in Nginx) parameters.
   * They are called here with $_SERVER environment variables to
   * prevent sensitive data from leaking to site administrators
   * with PHP access, that potentially might be of other sites in
   * Drupal's multisite set-up.
   * This is a security measure implemented by the Aegir project.
   */
  $databases['default']['default'] = array(
    'driver' => $_SERVER['db_type'],
    'database' => $_SERVER['db_name'],
    'username' => $_SERVER['db_user'],
    'password' => $_SERVER['db_passwd'],
    'host' => $_SERVER['db_host'],
    /* Drupal interprets $databases['db_port'] as a string, whereas Drush sees
     * it as an integer. To maintain consistency, we cast it to a string. This
     * should probably be fixed in Drush.
     */
    'port' => (string) $_SERVER['db_port'],
<?php if ($utf8mb4_is_configurable && $utf8mb4_is_supported): ?>
    'charset' => 'utf8mb4',
    'collation' => 'utf8mb4_general_ci',
<?php endif; ?>
  );
  $db_url['default'] = $_SERVER['db_type'] . '://' . $_SERVER['db_user'] . ':' . $_SERVER['db_passwd'] . '@' . $_SERVER['db_host'] . ':' . $_SERVER['db_port'] . '/' . $_SERVER['db_name'];
}

  /**
   * Now that we used the credentials from the apache environment, we
   * don't need them anymore. Clear them from apache and the _SERVER
   * array, otherwise they show up in phpinfo() and other friendly
   * places.
   */
  if (function_exists('apache_setenv')) {
    apache_setenv('db_type', null);
    apache_setenv('db_user', null);
    apache_setenv('db_passwd', null);
    apache_setenv('db_host', null);
    apache_setenv('db_port', null);
    apache_setenv('db_name', null);
    // no idea why they are also in REDIRECT_foo, but they are
    apache_setenv('REDIRECT_db_type', null);
    apache_setenv('REDIRECT_db_user', null);
    apache_setenv('REDIRECT_db_passwd', null);
    apache_setenv('REDIRECT_db_host', null);
    apache_setenv('REDIRECT_db_port', null);
    apache_setenv('REDIRECT_db_name', null);
  }
  unset($_SERVER['db_type']);
  unset($_SERVER['db_user']);
  unset($_SERVER['db_passwd']);
  unset($_SERVER['db_host']);
  unset($_SERVER['db_port']);
  unset($_SERVER['db_name']);
  unset($_SERVER['REDIRECT_db_type']);
  unset($_SERVER['REDIRECT_db_user']);
  unset($_SERVER['REDIRECT_db_passwd']);
  unset($_SERVER['REDIRECT_db_host']);
  unset($_SERVER['REDIRECT_db_port']);
  unset($_SERVER['REDIRECT_db_name']);

<?php else: ?>

  $databases['default']['default'] = array(
    'driver' => "<?php print $this->creds['db_type']; ?>",
    'database' => "<?php print $this->creds['db_name']; ?>",
    'username' => "<?php print $this->creds['db_user']; ?>",
    'password' => "<?php print $this->creds['db_passwd']; ?>",
    'host' => "<?php print $this->creds['db_host']; ?>",
    'port' => "<?php print $this->creds['db_port']; ?>",
   );
  $db_url['default'] = "<?php print strtr("%db_type://%db_user:%db_passwd@%db_host:%db_port/%db_name", array(
    '%db_type' => $this->creds['db_type'],
    '%db_user' => $this->creds['db_user'],
    '%db_passwd' => $this->creds['db_passwd'],
    '%db_host' => $this->creds['db_host'],
    '%db_port' => $this->creds['db_port'],
    '%db_name' => $this->creds['db_name'])); ?>";

<?php endif; ?>

  $profile = "<?php print $this->profile ?>";
  $install_profile = "<?php print $this->profile ?>";

  /**
  * PHP settings: (managed in BOA via site and platform level INI files)
  *
  * To see what PHP settings are possible, including whether they can
  * be set at runtime (ie., when ini_set() occurs), read the PHP
  * documentation at http://www.php.net/manual/en/ini.php#ini.list
  * and take a look at the .htaccess file to see which non-runtime
  * settings are used there. Settings defined here should not be
  * duplicated there so as to avoid conflict issues.
  */
  //ini_set('session.gc_probability', 1);
  //ini_set('session.gc_divisor', 100);
  //ini_set('session.gc_maxlifetime', 200000);
  //ini_set('session.cookie_lifetime', 2000000);

  /**
  * Set the umask so that new directories created by Drupal have the correct permissions
  */
  umask(0002);

  $settings['install_profile'] = '<?php print $this->profile ?>';
  $settings['file_public_path'] = 'sites/<?php print $this->uri ?>/files';
  $settings['file_private_path'] = 'sites/<?php print $this->uri ?>/private/files';
  $config['system.file']['path']['temporary'] = 'sites/<?php print $this->uri ?>/private/temp';
  $config_directories[CONFIG_SYNC_DIRECTORY] = 'sites/<?php print $this->uri ?>/private/config/sync';
  $settings['hash_salt'] = '<?php print $drupal_hash_salt_var ?>';
  $settings['aegir_api'] = <?php print $this->api_version ? $this->api_version : 0 ?>;
  $settings['allow_authorize_operations'] = FALSE;

  /**
   * Useless currently, because it is not used in Drupal 8 anyway.
   * Instead, Drupal 8 is trying to set the clean URLs mode on the fly,
   * depending on the request, so we should force this by redirecting
   * non-clean to clean URLs on the web server level - Nginx example:
   *
   *   rewrite ^/index.php/(.*)$ $scheme://$host/$1 permanent;
   *
   */
  $settings['clean_url'] = 1;

<?php if (!$this->site_enabled) : ?>
  /**
   * Useless currently, because it is ignored in Drupal 8 anyway.
   */
  $settings['maintenance_mode'] = 1;
<?php endif; ?>

  /**
   * Load services definition file.
   */
  $settings['container_yamls'][] = __DIR__ . '/services.yml';

  /**
   * Trusted Host Settings support.
   */
  $settings['trusted_host_patterns'] = array(
<?php
  $esc_uri = str_replace('.', '\.', $this->uri);
  print "    '^{$esc_uri}\$',\n";
  foreach ($this->aliases as $alias_url) {
    $esc_alias = preg_replace(['/\./', '/\/.+/'], ['\.', ''], $alias_url);
    print "    '^{$esc_alias}\$',\n";
  }
?>
    '^localhost$',
    '^localhost\.*',
    '\.local$',
  );

<?php print $extra_config; ?>

  # Additional host wide configuration settings. Useful for safely specifying configuration settings.
  if (is_readable('<?php print $this->platform->server->include_path  ?>/global.inc')) {
    include('<?php print $this->platform->server->include_path  ?>/global.inc');
  }

  # Additional platform wide configuration settings.
  <?php $this->platform->root = provision_auto_fix_platform_root($this->platform->root); ?>
  if (is_readable('<?php print $this->platform->root  ?>/sites/all/platform.settings.php')) {
    include('<?php print $this->platform->root ?>/sites/all/platform.settings.php');
  }

  # Additional site configuration settings.
  if (is_readable('<?php print $this->site_path  ?>/local.settings.php')) {
    include('<?php print $this->site_path  ?>/local.settings.php');
  }
