<?php
/**
 * @file
 * Provision API
 *
 * @see drush.api.php
 * @see drush_command_invoke_all()
 * @see http://docs.aegirproject.org/en/3.x/extend/altering-behaviours/
 */

/**
 * Possible variables to set in local.drushrc.php or another drushrc location Drush supports.
 *
 * usage:
 *   $options['provision_backup_suffix'] = '.tar.bz2';
 *
 * provision_verify_platforms_before_migrate
 *   When migrating many sites turning this off can save time, default TRUE.
 *
 * provision_backup_suffix
 *   Method to set the compression used for backups... e.g. '.tar.bz2' or '.tar.', defaults to '.tar.gz'.
 *
 * provision_apache_conf_suffix
 *   Set to TRUE to generate apache vhost files with a .conf suffix, default FALSE.
 *   This takes advantage of the IncludeOptional statment introduced in Apache 2.3.6.
 *   WARNING: After turning this on you need to re-verify all your sites, then servers,
 *   and then cleanup the old configfiles (those without the .conf suffix).
 *   Or run: `rename s/$/.conf/ /var/aegir/config/server_master/apache/vhost.d/*` for each server.
 *
 * provision_create_local_settings_file
 *   Create a site 'local.settings.php' file if one isn't found, default TRUE.
 *
 * provision_mysqldump_suppress_gtid_restore
 *   Don't restore GTIDs from a database export.  Set to TRUE for MySQL versions 5.6 and above to
 *   avoid having restores error out during operations such as cloning, migrating, and restoring from
 *   backup.  Default is FALSE.
 *
 * provision_composer_install_platforms
 *   Set to FALSE to prevent provision from ever running `composer install`.
 *   Default is TRUE.
 *
 * provision_composer_install_platforms_verify_always
 *   By default, provision will run `composer install` every time a platform
 *   is verified.
 *
 *   Set to FALSE to only run `composer install` once. If composer.json
 *   changes, you will have to run `composer install` manually.
 *
 *   Default is TRUE.
 *
 * provision_composer_install_command
 *
 *   The full command to run during platform verify.
 *   Default is 'composer install --no-interaction --no-progress --no-dev'
 *
 */

/**
 * Implements hook_drush_load(). Deprecated. Removed in Drush 7.x.
 *
 * In a drush contrib check if the frontend part (hosting_hook variant) is enabled.
 */
function hook_drush_load() {
  $features = drush_get_option('hosting_features', []);
  $hook_feature_name = 'something';

  return array_key_exists($hook_feature_name, $features) // Front-end module is installed...
    && $features[$hook_feature_name];                    // ... and enabled.
}

/**
 * Advertise what service types are available and their default
 * implementations. Services are class Provision_Service_{type}_{service} in
 * {type}/{service}/{service}_service.inc files.
 *
 * @return
 *   An associative array of type => default. Default may be NULL.
 *
 * @see provision.service.inc
 */
function hook_provision_services() {
  return array('db' => NULL);
}

/**
 * Alter a Context immediately after it is loaded and the 'init' methods are run.
 *
 * If replacing the context with a new object, be sure to implement the methods
 * $context->method_invoke('init) and $context->type_invoke('init');
 *
 * @param $context \Provision_Context|\Provision_Context_server|\Provision_Context_site|\Provision_Context_platform
 *
 * @see provision.context.inc#72
 */
function hook_provision_context_alter(&$context) {
  $context = new Provision_Context_Server_alternate($context->name);
  $context->method_invoke('init');
  $context->type_invoke('init');
}

/**
 * Append PHP code to Drupal's settings.php file.
 *
 * To use templating, return an include statement for the template.
 *
 * @param $uri
 *   URI for the site.
 * @param $data
 *   Associative array of data from Provision_Config_Drupal_Settings::data.
 *
 * @return
 *   Lines to add to the site's settings.php file.
 *
 * @see Provision_Config_Drupal_Settings
 */
function hook_provision_drupal_config($uri, $data) {
  return '$conf[\'reverse_proxy\'] = TRUE;';
}

/**
 * Append Apache configuration to server configuration.
 *
 * To use templating, return an include statement for the template.
 *
 * The d() function is available to retrieve more information from the aegir
 * context.
 *
 * @param $data
 *   Associative array of data from Provision_Config_Apache_Server::data.
 *
 * @return
 *   Lines to add to the configuration file.
 *
 * @see Provision_Config_Apache_Server
 */
function hook_provision_apache_server_config($data) {
}

/**
 * Append Apache configuration to platform configuration.
 *
 * To use templating, return an include statement for the template.
 *
 * The d() function is available to retrieve more information from the aegir
 * context.
 *
 * @param $data
 *   Associative array of data from Provision_Config_Apache_Platform::data.
 *
 * @return
 *   Lines to add to the configuration file.
 *
 * @see Provision_Config_Apache_Platform
 */
function drush_hook_provision_apache_dir_config($data) {
}

/**
 * Append Apache configuration to site vhost configuration.
 *
 * To use templating, return an include statement for the template.
 *
 * The d() function is available to retrieve more information from the aegir
 * context.
 *
 * @param $uri
 *   URI for the site.
 * @param $data
 *   Associative array of data from Provision_Config_Apache_Site::data.
 *   For example:
 *   Array (
 *       [server] => Provision_Context_server Object()
 *       [application_name] => apache
 *       [http_pred_path] => /var/aegir/config/server_master/apache/pre.d
 *       [http_postd_path] => /var/aegir/config/server_master/apache/post.d
 *       [http_platformd_path] => /var/aegir/config/server_master/apache/platform.d
 *       [http_vhostd_path] => /var/aegir/config/server_master/apache/vhost.d
 *       [http_subdird_path] => /var/aegir/config/server_master/apache/subdir.d
 *       [http_port] => 80
 *       [redirect_url] => http://example.com
 *       [db_type] => mysql
 *       [db_host] => localhost
 *       [db_port] => 3306
 *       [db_passwd] => ***
 *       [db_name] => ***
 *       [db_user] => ***
 *       [packages] => Array of package information...
 *       [installed] => 1
 *       [config-file] => /var/aegir/platforms/drupal-7.58/sites/example.com/drushrc.php
 *       [context-path] => /var/aegir/platforms/drupal-7.58/sites/example.com/drushrc.php
 *       [https_port] => 443
 *       [extra_config] => # Extra configuration from modules:
 *   )
 *
 * @return
 *   Lines to add to the configuration file.
 *
 * @see Provision_Config_Apache_Site
 */
function drush_hook_provision_apache_vhost_config($uri, $data) {
}

/**
 * Append Nginx configuration to server configuration.
 *
 * To use templating, return an include statement for the template.
 *
 * The d() function is available to retrieve more information from the aegir
 * context.
 *
 * @param $data
 *   Associative array of data from Provision_Config_Nginx_Server::data.
 *
 * @return
 *   Lines to add to the configuration file.
 *
 * @see Provision_Config_Nginx_Server
 */
function hook_provision_nginx_server_config($data) {
}

/**
 * Append Nginx configuration to platform configuration.
 *
 * To use templating, return an include statement for the template.
 *
 * The d() function is available to retrieve more information from the aegir
 * context.
 *
 * @param $data
 *   Associative array of data from Provision_Config_Nginx_Platform::data.
 *
 * @return
 *   Lines to add to the configuration file.
 *
 * @see Provision_Config_Nginx_Platform
 */
function drush_hook_provision_nginx_dir_config($data) {
}

/**
 * Append Nginx configuration to site vhost configuration.
 *
 * To use templating, return an include statement for the template.
 *
 * The d() function is available to retrieve more information from the aegir
 * context.
 *
 * @param $uri
 *   URI for the site.
 * @param $data
 *   Associative array of data from Provision_Config_Nginx_Site::data.
 *
 * @return
 *   Lines to add to the configuration file.
 *
 * @see Provision_Config_Nginx_Site
 */
function drush_hook_provision_nginx_vhost_config($uri, $data) {
}

/**
 * Specify a different template for rendering a config file.
 *
 * @param $config
 *   The Provision_config object trying to find its template.
 *
 * @return
 *   A filename of a template to use for rendering.
 *
 * @see hook_provision_config_load_templates_alter()
 */
function hook_provision_config_load_templates($config) {
  if (is_a($config, 'Provision_Config_Drupal_Settings')) {
    $file = dirname(__FILE__) . '/custom-php-settings.tpl.php';
    return $file;
  }
}

/**
 * Alter the templates suggested for rendering a config file.
 *
 * @param $templates
 *   The array of templates suggested by other Drush commands.
 * @param $config
 *   The Provision_config object trying to find its template.
 *
 * @see hook_provision_config_load_templates()
 */
function hook_provision_config_load_templates_alter(&$templates, $config) {
  // Don't let any custom templates be used.
  $templates = [];
}

/**
 * Alter the variables used for rendering a config file.
 *
 * When implementing this hook, the function name should start with your file's name, not "drush_".
 *
 * @param $variables
 *   The variables that are about to be injected into the template.
 * @param $template
 *   The template file chosen for use.
 * @param $config
 *   The Provision_config object trying to find its template.
 *
 * @see hook_provision_config_load_templates()
 * @see hook_provision_config_load_templates_alter()
 */
function hook_provision_config_variables_alter(&$variables, $template, $config) {

  // If this is the vhost template and the http service is Docker...
  if (is_a($config, 'Provision_Config_Apache_Site') && is_a(d()->platform->service('http'), 'Provision_Service_http_apache_docker')) {

    // Force the listen port to be 80.
    $variables['http_port'] = '80';
  }
}

/**
 * Implements hook_provision_platform_sync_path_alter().
 *
 * Changes the sync_path to ensure that composer-built platforms get all of the
 * code moved to remote servers.
 *
 * @see provision_git_provision_platform_sync_path_alter()
 *`
 * @param $sync_path
 *   If the site is hosted on a remote server, this is the path that will be
 *   rsync'd over.
 */
function hook_provision_platform_sync_path_alter(&$sync_path) {
    $repo_path = d()->platform->repo_path;
    if ($repo_path != d()->root) {
        $sync_path = $repo_path;

        if (!file_exists($repo_path)) {
            return drush_set_error('PROVISION_ERROR',  dt("Platform !path does not exist.", array(
              '!path' => $repo_path,
            )));
        }
    }
}

/**
 * Alter the array of directories to create.
 *
 * @param $mkdir
 *    The array of directories to create.
 * @param string $url
 *    The url of the site being invoked.
 */
function hook_provision_drupal_create_directories_alter(&$mkdir, $url) {
  $mkdir["sites/$url/my_special_dir"] = 02770;
  $mkdir["sites/$url/my_other_dir"] = FALSE; // Skip the chmod on this directory.
}

/**
 * Alter the array of directories to change group ownership of.
 *
 * @param $chgrp
 *    The array of directories to change group ownership of.
 * @param string $url
 *    The url of the site being invoked.
 */
function hook_provision_drupal_chgrp_directories_alter(&$chgrp, $url) {
  $chgrp["sites/$url/my_special_dir"] = d('@server_master')->web_group;
  $chgrp["sites/$url/my_other_dir"] = FALSE; // Skip the chgrp on this directory.
}

/**
 * Alter the array of directories to not to recurse into in mkdir and chgrp
 * operations.
 *
 * @param $chgrp_not_recursive
 *    The array of directories not to recurse into.
 * @param string $url
 *    The url of the site being invoked.
 */
function hook_provision_drupal_chgrp_not_recursive_directories_alter($chgrp_not_recursive, $url) {
  $chgrp_not_recursive[] = "sites/$url/my_special_dir";
  unset($chgrp_not_recursive["sites/$url"]); // Allow recursion where we otherwise wouldn't.
}

/**
 * Alter the array of directories to not to recurse into in chmod operations.
 *
 * @param $chmod_not_recursive
 *    The array of directories not to recurse into.
 * @param string $url
 *    The url of the site being invoked.
 */
function hook_provision_drupal_chmod_not_recursive_directories_alter($chmod_not_recursive, $url) {
  $chmod_not_recursive[] = "sites/$url/my_special_dir";
  unset($chmod_not_recursive["sites/$url"]); // Allow recursion where we otherwise wouldn't.
}

/**
 * Alter the settings array just before starting the provision install.
 *
 * @param $settings
 *    The array with settings.
 * @param $url
 *    The site url.
 */
function hook_provision_drupal_install_settings_alter(&$settings, $url) {
  $settings['forms']['install_configure_form']['update_status_module'] = [];
}

/**
 * Alter the options passed to 'provision-deploy' when it is invoked in
 * restore, clone and migrate tasks.
 *
 * @param array $deploy_options
 *   Options passed to the invocation of provision-deploy.
 * @param string $context
 *   The type of task invoking the hook (e.g., 'clone').
 */
function hook_provision_deploy_options_alter(&$deploy_options, $context) {
  // From hosting_s3; see: https://www.drupal.org/node/2412563
  // Inject the backup bucket name during the 'clone' task, so that it is
  // available in deploy().
  if ($bucket = drush_get_option('s3_backup_name', FALSE)) {
    $deploy_options['s3_backup_name'] = $bucket;
  }
}

/**
 * Alter the array of regexes used to filter mysqldumps.
 *
 * @param $regexes
 *   An array of patterns to match (keys) and replacement patterns (values).
 *   Setting a value to FALSE will omit the line entirely from the database
 *   dump. Defaults are set in Provision_Service_db_mysql::get_regexes().
 */
function hook_provision_mysql_regex_alter(&$regexes) {
  $regexes = [
    // remove these lines entirely.
    '#/\*!50013 DEFINER=.*/#' => FALSE,
    // just remove the matched content.
    '#/\*!50017 DEFINER=`[^`]*`@`[^`]*`\s*\*/#' => '',
    // replace matched content as needed
    '#/\*!50001 CREATE ALGORITHM=UNDEFINED \*/#' => "/*!50001 CREATE */",
  ];
}

/**
 * Implements hook_provision_prepare_environment()
 *
 * React to the setting up of $_SERVER variables such as db_name and db_passwd.
 *
 * Runs right after writing sites/$URI/drushrc.php.
 * Database credentials are available in the $_SERVER variables.
 *
 * @see provision_prepare_environment()
 */
function hook_provision_prepare_environment() {

  // Write a .env file in the root of the project with the Drupal DB credentials.
  // This file could be used by other tools to access the site's database.
  $file_name = d()->root . '/.env';
  $file_contents = <<<ENV
MYSQL_DATABASE={$_SERVER['db_name']}
MYSQL_USER={$_SERVER['db_name']}
MYSQL_PASSWORD={$_SERVER['db_name']}
ENV;

  // Make writable, then write the file.
  if (file_exists($file_name) && !is_writable($file_name)) {
    provision_file()->chmod($file_name, 0660);
  }
  file_put_contents($file_name, $file_contents);

  // Hide sensitive information from any other users.
  provision_file()->chmod($file_name, 0400);
}

/**
 * Alter the list of directories excluded from site backups.
 *
 * @param $directories
 *   An array of strings representing directories, which are relative to a
 *   site directory.
 *
 * @see drush_provision_drupal_provision_backup_get_exclusions()
 */
function hook_provision_backup_exclusions_alter(&$directories) {
  // Prevent backing up the CiviCRM Smarty cache.
  $directories[] = './files/civicrm/templates_c';
}

/**
 * Alter the db options.
 *
 * @param $options
 *   The options array to alter. This is empty by default.
 * @param $dsn
 *   The db data source name. For more info see
 *   https://www.php.net/manual/en/pdo.construct.php.
 */
function hook_provision_db_options_alter(&$options, $dsn) {
  // Azure requires specifying a SSL cert
  // see https://docs.microsoft.com/en-us/azure/mysql/howto-configure-ssl

  // List any servers that need the certificate here using their FQDN and/or
  // their private/internal endpoints.
  $servers = [
    '10.0.0.1',
    'my-prod-db-server.mysql.database.azure.com'
  ];

  // If the $dsn references one of the above servers, add the certificate.
  // Sometimes things don't work as they should, hence the second line,
  // via https://owendavies.net/articles/azure-php-mysql-ssl/.
  foreach ($servers as $server) {
    if (strpos($dsn, $server)) {
      $options = [
        PDO::MYSQL_ATTR_SSL_CA => '/var/aegir/config/ssl.d/Combined.crt.pem',
        PDO::MYSQL_ATTR_SSL_VERIFY_SERVER_CERT => false,
      ];
    }
  }
}

/**
 * Alter the suggested database name for new databases.
 *
 * @param $database
 *   The default suggested database name to alter. The modified name
 *   should be 16 characters or fewer.
 */
function hook_provision_suggest_db_name_alter(&$database) {
    if (d()->db_server->name == '@server_myremoteserver') {
        $database = substr('r_' . $database, 0, 15);
    }
}

/**
 * Alter the db username.
 *
 * @param string $user
 *   The user string to alter.
 * @param string $host
 *   The remote host, for reference.
 * @param string $op
 *   Optionally, the operation being performed.
 */
function hook_provision_db_username_alter(&$user, $host, $op = '') {
  // Azure requires username@server
  // see http://bit.ly/azure-username-servername

  // Figure out what IPs correspond to which servers, as they may be mapped as
  // IP or may be FQDN. As host_in_aegir => server_short_name.
  $servers = [
    '10.0.0.1' => 'my-prod-db-server',
    'my-prod-db-server.mysql.database.azure.com' => 'my-prod-db-server',
    '10.0.1.1' => 'my-stage-db-server',
    'my-stage-db-server.mysql.database.azure.com' => 'my-stage-db-server',
  ];
  // On grant and revoke we need to make sure we're NOT sending the username
  // in the username@host format.
  if ($op == 'grant' || $op == 'revoke') {
    $user = explode('@', $user)[0];
  }
  else {
    if (isset($servers[$host])) {
      // Only alter if it hasn't been altered before.
      if (strpos($user, $servers[$host]) === FALSE) {
        $user = $user . '@' . $servers[$host];
      }
    }
  }
}
