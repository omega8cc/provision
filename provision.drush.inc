<?php
/**
 * @file
 * Provision Drush commands.
 *
 *
 * This module provides a framework for a Drupal site to manage and install new Drupal sites, using the command line
 * Drush utility.
 *
 * It allows for pluggable 'provisioning modules' that can extend and modify the tasks that are taken during installation.
 *
 * Each site has the following commands that can be run on it.
 *
 * Implemented :
 *   install - Install a new Drupal site. The install command uses 3 separate hooks to do its job,
 *             namely hook_pre_provision_install(), hook_provision_install() and hook_post_provision_install().
 *   verify  - Recreate all configuration files, to be in synch with changes in the front end. And test that they are correct.
 *   stats   - Return an associated array of site statistics. (implemented in provision_stats module, is thus optional)
 *   import  - Import the details of an already existing site into the provisioning framework.
 *             This command inspects the settings.php and generates the site.php file that the framework uses for configuration.
 *   backup  - Generates a tarball containing the sites directory, the site data configuration and the database dump.
 *             This allows the tarball to act as a 'site package', which can be redeployed on other installations,
 *             or used for an upgrade.
 *   disable - Disable an installed Drupal site. Changes the virtual host config file so that it redirects to provision_disabled_site_redirect_url
 *   enable  - Re-enable a site that has already been disabled. Recreates the virtual host file.
 *   delete  - In a site context: generates a back up of the site, and then removes all references to it.
 *             In a platform context: removes the platform and its vhost config from the server if no sites are currently running on it
 *   restore - Revert to a previous backup of the site.
 *
 *   deploy  - Accepts a site package (backup) as argument, and redeploys it, running the upgrade processes on it.
 *             Uses hook_provision_pre_upgrade(), hook_provision_upgrade() and hook_provision_post_upgrade() hooks,
 *             and allows clean roll back if any errors occur. Will include stringent checking of module versions,
 *             and allow unit tests to be run.
 *   lock    - Lock a platform so that sites cannot be provisioned on it. This does not disable or delete the platform
 *             nor any sites currently provisioned on it.
 *   unlock  - Unlock a platform so that sites can be provisioned on it.
 *
 *   login-reset - Generate a one-time login reset URL.
 */


/**
 * @defgroup provisiondrush Command line interface for Provision.
 * @{
 */
include_once('provision.inc');

/**
 * Implements hook_drush_init().
 */
function provision_drush_init() {
  // try to load the drush siterecoard (the "alias") into d
  // if a name is provided, it's because we're in provision-save so we
  // need to specify it because it's not loaded by drush
  $hash_name = drush_get_option('#name') ? '#name' : 'name';
  d(drush_get_option($hash_name, '@self', 'alias'), TRUE);
  // Make sure that the provision command is not being run as the root user.
  _provision_drush_check_user();
  // Abort the process if the load is too high.
  _provision_drush_check_load();
}

/**
 * This will abort any process running drush provision commands if the
 * user running the command is root.
 */
function _provision_drush_check_user() {
  $command = drush_get_command();
  $name = posix_getpwuid(posix_geteuid());
  if (preg_match("/^provision-\b/", $command['command']) && $name['name'] == 'root') {
    return drush_set_error('PROVISION_IS_ROOT', dt('You are running the provision script as the root user. Exiting'));
  }
}

/**
 * This will abort any process running drush if the load is critical.
 *
 * @see provision_load_critical()
 */
function _provision_drush_check_load() {
  $load = sys_getloadavg();
  if (provision_load_critical($load)) {
    drush_set_error('PROVISION_OVERLOAD', dt("load on system too heavy (@load), aborting", array('@load' => join(" ", $load))));
    exit(1);
  }
}

/**
 * Implementation of hook_drush_command().
 */
function provision_drush_command() {
  $items['provision-save'] = array(
    'description' => dt('Save Drush alias'),
    'arguments' => array(
      '@context_name' => 'Context to save',
    ),
    'options' => array_merge(array(
      'context_type' => 'server, platform, or site; default server',
      'delete' => 'Remove the alias.'),
      Provision_Context_server::option_documentation(),
      Provision_Context_platform::option_documentation(),
      Provision_Context_site::option_documentation()),
    // we should populate from all known contexts, unfortunately we don't
    // enumerate them yet... see https://drupal.org/node/1972286
    'allow-additional-options' => TRUE,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );
  $items['provision-install'] = array(
    'description' => dt('Provision a new site using the provided data.'),
    'examples' => array(
      'drush @site provision-install' => 'Install the site as defined by the site Drush alias generated with provision-save.',
    ),
    'options' => array(
      'client_email' => dt('The email address of the client to use.'),
      'profile' => dt('The profile to use when installing the site.'),
      'force-reinstall' => dt('Delete the sites database and files, before attempting to install.'),
    ),
    'allow-additional-options' => TRUE,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-install-backend'] = array(
    'description' => dt('Provision a new site using the provided data.'),
    'options' => array(
      'client_email' => dt('The email address of the client to use.'),
    ),
    'allow-additional-options' => TRUE,
    'hidden' => TRUE,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_SITE
  );

  $items['provision-import'] = array(
    'description' => dt('Turn an already running site into a provisioned site.'),
    'options' => array(
      'client_email' => dt('The email address of the client to use.'),
    ),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @site provision-import' => 'Import the site as defined by the site Drush alias generated with provision-save.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-backup'] = array(
    'optional arguments' => array('backup-file' => dt('The file to save the backup to. This will be a gzipped tarball.')),
    'options' => array(
      'provision_backup_suffix' => dt('Set the extension for the backupfile, compression is derived from this (default: ".tar.gz"'),
    ),
    'description' => dt('Generate a back up for the site.'),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @site provision-backup' => 'Back up the site as defined by the site Drush alias generated with provision-save.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-enable'] = array(
    'description' => 'Enable a disabled site.',
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @site provision-enable' => 'Enable the site as defined by the site Drush alias generated with provision-save.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );
  $items['provision-disable'] = array(
    'arguments' => array('domain.com' => dt('The domain of the site to disable (only if disabled).')),
    'description' => 'Disable a site.',
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @site provision-disable' => 'Disable the site as defined by the site Drush alias generated with provision-save.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-lock'] = array(
    'description' => 'Lock a platform from having any other sites provisioned on it.',
    'examples' => array(
      'drush @platform provision-lock' => 'Lock the platform as defined by the platform Drush alias generated with provision-save.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-unlock'] = array(
    'description' => 'Unlock a platform so that sites can be provisioned on it.',
    'examples' => array(
      'drush @platform provision-unlock' => 'Unlock the platform as defined by the platform Drush alias generated with provision-save.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-dlock'] = array(
    'description' => 'Lock a platform local Drush.',
    'examples' => array(
      'drush @platform provision-dlock' => 'Lock the platform local Drush as defined by the platform Drush alias generated with provision-save.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH
  );

  $items['provision-dunlock'] = array(
    'description' => 'Unlock a platform local Drush.',
    'examples' => array(
      'drush @platform provision-dunlock' => 'Unlock the platform local Drush as defined by the platform Drush alias generated with provision-save.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH
  );

  // Revert-artifact leftover management (D-035): a deploy/restore/migrate that
  // aborts mid-swap strands sites/<name>.restore on the platform — a syndrome
  // shared by Drupal and Backdrop alike, so the pair lives here in the general
  // layer. provision-leftover-import inspects such a leftover (so the frontend
  // can register a purgeable pseudo-site record for it); provision-leftover-
  // purge removes it (directory + orphan copy database) on demand. Both run on
  // any platform context; neither declares or performs a CMS bootstrap (file
  // ops + regex parsing only). Hook functions in
  // import.leftover.provision.inc / purge.leftover.provision.inc.
  $items['provision-leftover-import'] = array(
    'description' => dt('Inspect a revert-artifact leftover (sites/<site>.restore or .original) on this platform and report its facts to the frontend. Read-only.'),
    'arguments' => array(
      'leftover' => dt('The leftover directory name under sites/ (must end in .restore or .original).'),
    ),
    'examples' => array(
      'drush @platform_example provision-leftover-import example.com.restore' => 'Report the facts of the stranded example.com.restore directory.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH
  );
  $items['provision-leftover-purge'] = array(
    'description' => dt('Delete a revert-artifact leftover (sites/<site>.restore or .original) from this platform, dropping its orphan copy database when it is provably unused.'),
    'arguments' => array(
      'leftover' => dt('The leftover directory name under sites/ (must end in .restore or .original).'),
    ),
    'options' => array(
      'leftover_db_name' => dt('The database name recorded at expose time; the drop only happens when it matches a fresh re-parse of the leftover settings.php.'),
      'leftover_db_host' => dt('The database host recorded at expose time; the drop only happens when it matches a fresh re-parse of the leftover settings.php.'),
      'leftover_db_server' => dt('Context name of the db server to drop the orphan database on (e.g. server_master). No database is touched when omitted.'),
    ),
    'examples' => array(
      'drush @platform_example provision-leftover-purge example.com.restore' => 'Remove the stranded example.com.restore directory (and its orphan copy database when safe).',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH
  );

  $items['provision-verify'] = array(
    'aliases' => array('v', 'pv', 'verify'),
    'arguments' => array('domain.com' => dt('The domain of the site to verify).')),
    'description' => 'Verify that the provisioning framework is correctly installed.',
    'examples' => array(
      'drush @site provision-verify' => 'Verify the site as defined by the site Drush alias generated with provision-save.',
      'drush @platform provision-verify' => 'Verify the platform as defined by the platform Drush alias generated with provision-save.',
    ),
    'options' => array(
      'working-copy' => array(
        'description' => dt('Keep VCS files when building the a platform using Drush make.'),
        'hidden' => TRUE,
      ),
      'override_slave_authority' => array(
        'description' => dt('Push the site specific files directory to a slave. This overrides the slaves authority.'),
      ),
    ),
    'allow-additional-options' => TRUE,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH
  );

  $items['provision-restore'] = array(
    'description' => 'Restore the site to a previous backup. This will also generate a backup of the site as it was.',
    'arguments' => array(
      'site_backup.tar.gz' => dt('The backup to restore the site to.')),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @site provision-restore ~/backups/some_site.tar.gz' => 'Restore the site to the backup in ~/backups/some_site.tar.gz.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-deploy'] = array(
    'description' => 'Deploy an existing backup to a new url.',
    'arguments' => array(
      'site_backup.tar.gz' => dt('The backup to deploy.')),
    'options' => array(
      'old_uri' => dt('Old site uri to replace in references to sites/example.com/files/ in the database content.)'),
    ),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @site provision-deploy ~/backups/some_site.tar.gz' => 'Deploy the site as defined by the site Drush alias, from the backup in ~/backups/some_site.tar.gz.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-migrate'] = array(
    'description' => 'Migrate a site between platforms.',
    'arguments' => array(
      '@platform_name' => dt('The Drush alias of the platform.')),
    'options' => array(
      'profile' => dt('The Drupal profile to use.')),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @site provision-migrate @platform_name' => 'Migrate the site as defined by the Drush alias, to the platform as defined by the platform\'s Drush alias',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-clone'] = array(
    'description' => 'Clone a site between platforms.',
    'arguments' => array(
      '@new_site' => dt('The Drush alias of the new site as generated by provision-save.'),
      '@platform_name' => dt('The Drush alias of the platform to clone the site onto.')),
    'options' => array(
      'profile' => dt('The Drupal profile to use.')),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @site provision-clone @new_site @platform_name' => 'Clone the original site to the new site on a platform',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-delete'] = array(
    'description' => 'Delete a site or platform.',
    'options' => array(
      'force' => dt('Force deletion.')),
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @site provision-delete' => 'Delete the site as defined by the site Drush alias generated with provision-save.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH
  );

  $items['provision-login-reset'] = array(
    'description' => 'Generate a one-time login reset URL.',
    'allow-additional-options' => TRUE,
    'examples' => array(
      'drush @site provision-login-reset' => 'Generate a one-time login reset URL for the site as defined by the site Drush alias generated with provision-save.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT
  );

  $items['provision-backup-delete'] = array(
    'description' => 'Delete a backup file.',
    'arguments' => array('backup-file' => dt('The backup file to delete. This will be a gzipped tarball.')),
    'required-arguments' => TRUE,
    'examples' => array(
      'drush @site provision-backup-delete /path/to/site_backup.tgz' => 'Delete a backup of this site as defined by the site Drush alias generated with provision-save.',
    ),
    'allow-additional-options' => TRUE,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH
  );

  $items['hostmaster-migrate'] = array(
    'description' => dt('Migrate an instance of the Hostmaster front end to a new platform'),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
    'arguments' => array(
      'example.com' => dt('The name of the site to migrate'),
      '/path/to/platform' => dt('The platform to migrate the site to.'),
    ),
    'options' => array(
      'http_service_type' => dt('Webserver type to configure (default: %webserver)', array('%webserver' => 'apache')),
      'makefile' => dt('The makefile used to create the hostmaster platform (default: %makefile)', array('%makefile' => dirname(__FILE__). '/aegir.make')),
      'working-copy' => dt('Keep VCS files when building the hostmaster platform using Drush make.')
    ),
  );

  $items['hostmaster-install'] = array(
    'description' => dt('Install and verify the Hostmaster frontend.'),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'arguments' => array(
      'example.com' => dt('The URL of the site to install, optional (default: %host).', array('%host' => provision_fqdn())),
    ),
    'options' => array
    (
     'http_service_type' => dt('Webserver type to configure (default: %webserver)', array('%webserver' => 'apache')),
     'aegir_db_host' => dt('Database host to connect to (default: %host)', array('%host' => 'localhost')),
     'aegir_db_user' => dt('Database user to connect as (default: %user)', array('%user' => 'root')),
     'aegir_db_pass' => dt('Database password to use'),
     'aegir_db_port' => dt('Database port to use (default: %port)', array('%port' => '3306')),
     'aegir_db_grant_all_hosts' => dt('Allow connection from arbitrary web hosts. (Default: FALSE)'),
     'client_email' => dt('Email of the first client to create in the frontend'),
     'client_name' => dt('Name of the first client to create in the frontend (default: %user)', array('%user' => 'admin')),
     'makefile' => dt('The makefile used to create the hostmaster platform (default: %makefile)', array('%makefile' => dirname(__FILE__). '/aegir.make')),
     'aegir_host' => dt('Fully qualified domain name of the local server (default: %fqdn)', array('%fqdn' => provision_fqdn())),
     'script_user' => dt('User to run the backend as (default: %user)', array('%user' => provision_current_user())),
     'web_group' => dt('Group the webserver is running as (default: %group)', array('%group' => _provision_default_web_group())),
     'http_port' => dt('Port the webserver is running on (default: %port)', array('%port' => '80')),
     'version' => dt('The version of this released. (default: %version)', array('%version' => provision_version())),
     'aegir_root' => dt('Install aegir in this home directory (default: %home). Do not change unless you know what you are doing.', array('%home' => drush_server_home())),
     'root' => dt('Install the frontend in this directory (default: %home/hostmaster-%version).', array('%home' => drush_server_home(), '%version' => provision_version())),
     'backend-only' => dt('Install just the backend, and not the frontend UI.'),
     'working-copy' => dt('Keep VCS files when building the hostmaster platform using Drush make.')
     ),
  );

  $items['hostmaster-uninstall'] = array(
    'description' => dt('Uninstall the Hostmaster frontend.'),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_SITE,
    'options' => array
    (
     'all' => dt('Destroy *ALL* sites managed by the Aegir frontend'),
     ),
  );

  $items['backend-parse'] = array(
    'description' => dt('Parse the output of --backend commands to a human readable form'),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  return $items;
}

function drush_provision_save($alias = NULL) {
  if (drush_get_option('delete', FALSE)) {
    // remove an existing alias
    $config = new Provision_Config_Drushrc_Alias($alias);
    $config->unlink();
  }
  else {
    // trigger additional logic that should happen only on save.
    d($alias)->type_invoke('save');
    // create or update the record
    d($alias)->write_alias();
  }
}

function drush_provision_verify() {
  provision_backend_invoke(d()->name, 'provision-save');
  d()->command_invoke('verify');
}

function _provision_default_web_group() {
  $info = posix_getgrgid(posix_getgid());
  $common_groups = array(
    'www-data',
    'apache',
    'nginx',
    'www',
    '_www',
    'webservd',
    'httpd',
    'nogroup',
    'nobody');
  // The caller's own primary group used to close the list. On BOA that group
  // is the account's per-instance group (named after the account itself), a
  // tenant-isolation group and never a web group: offering it would chgrp
  // settings.php away from the FPM pool's www-data on a box with no common
  // web group. Only a primary group that is not the caller's own name stays
  // a candidate.
  if (isset($info['name']) && $info['name'] != provision_current_user()) {
    $common_groups[] = $info['name'];
  }

  foreach ($common_groups as $group) {
    if (provision_posix_groupname($group)) {
      return $group;
      break;
    }
  }
  return NULL;
}

/**
 * determine the number of CPU on the machine
 *
 * This tries a best guess at the number of CPUs running on the system. This is
 * useful for calculating sane load threshold.
 *
 * On Linux, this parses /proc/cpuinfo and looks for lines like this:
 *
 * processor	: 0
 * ...
 * processor	: 1
 * processor	: n
 *
 * The number of CPUs on the system is n+1, we just count the number of lines.
 *
 * Other systems remain to be implemented, and would be best implemetend
 * through a PECL (or similar) extension that would use the POSIX sysconf
 * interface, as such:
 *
 * ncpus = sysconf(_SC_NPROCESSORS_ONLN);
 *
 * If no method can be found to figure out the number of CPUs, this will return
 * FALSE.
 *
 * People wishing to extend this to other platforms should look at
 * suggestions at:
 *
 * http://groups.google.com/group/sage-devel/browse_thread/thread/d65209f7ad6057fc
 *
 * @see provision_load_critical()
 * @todo implement for other systems than Linux
 */
function provision_count_cpus() {
  $ncpus = FALSE;
  if (is_readable("/data/all/cpuinfo")) {
    # this should work on BOA with a /data/all/cpuinfo generated daily
    $cpuinfo = (int) file_get_contents("/data/all/cpuinfo");
    if ($cpuinfo !== FALSE && is_numeric($cpuinfo)) {
      $ncpus = $cpuinfo;
    }
  }
  elseif (is_readable("/proc/cpuinfo")) {
    # this should work on Linux with a /proc filesystem
    $cpuinfo = file_get_contents("/proc/cpuinfo");
    if ($cpuinfo !== FALSE) {
      if (preg_match_all("/^processor.*:.*[0-9]+$/m", $cpuinfo, $matches)) {
        $ncpus = count(array_pop($matches));
      }
    }
  }
  return $ncpus;
}

define('CRITICAL_LOAD_MULTIPLIER', 5);
define('CRITICAL_LOAD_THRESHOLD', 10);
if (!defined('AEGIR_BACKUP_MODE_CTRL')) {
  // Scoped by uid: /tmp is shared and sticky, so an unscoped file written by
  // one Octopus account blocks (and silently mis-modes) every other account's
  // backups. Frontend and backend run as the same account user, so both
  // resolve the same path. posix_getuid() (real process uid), NOT getmyuid()
  // — the latter returns the entry-script file OWNER, which is root for the
  // shared drush8 launcher and would collapse every account onto one path.
  if (function_exists('posix_getuid')) {
    define('AEGIR_BACKUP_MODE_CTRL', '/tmp/aegir_backup_mode.' . (int) posix_getuid() . '.txt');
  }
  else {
    define('AEGIR_BACKUP_MODE_CTRL', '/tmp/aegir_backup_mode.' . (int) getmyuid() . '.txt');
  }
}

/**
 * Reduce a backup-mode ctrl-file payload to a known mode or FALSE.
 *
 * The ctrl file lives on world-writable /tmp, so its content is untrusted:
 * only the exact frontend-offered mode strings may steer tar/dump behaviour.
 */
function provision_backup_mode_sanitize($backup_mode) {
  $backup_mode = trim((string) $backup_mode);
  $known = array(
    'backup_mysqldump_only',
    'site_files_without_db_backup',
    'site_files_with_mysqldump',
  );
  return in_array($backup_mode, $known, TRUE) ? $backup_mode : FALSE;
}

/**
 * Resolve this process's backup mode exactly once.
 *
 * The first call latches the outcome in SELECTED_BACKUP_MODE - including the
 * FALSE "this process is modeless" verdict - and every later call returns the
 * latch. That is what makes an implicit (clone/migrate/delete/restore) backup
 * immune to a ctrl file appearing mid-flow: the per-hook copies of this block
 * used to gate the ctrl read on empty($backup_mode), and empty(FALSE) is TRUE,
 * so a modeless process re-read the ctrl file in every later hook and could
 * adopt a concurrent task's mode minutes into its own run.
 *
 * AEGIR_BACKUP_MODE_CTRL_READ is latched here iff the ctrl file content became
 * the operative mode; the backup validate consume keys on that provenance.
 *
 * Hostmaster flows never carry a mode and never touch the ctrl file: they get
 * FALSE without latching anything, so a concurrent task's live channel stays
 * intact and $backup_mode is a defined, falsy local in every caller.
 */
function provision_backup_mode_resolve() {
  if (defined('SELECTED_BACKUP_MODE')) {
    return SELECTED_BACKUP_MODE;
  }
  if (provision_is_hostmaster_site()) {
    return FALSE;
  }
  $backup_mode = provision_backup_mode_sanitize(drush_get_option('selected_backup_mode', FALSE));
  if (empty($backup_mode)) {
    if (file_exists(AEGIR_BACKUP_MODE_CTRL)) {
      $backup_mode = provision_backup_mode_sanitize(@file_get_contents(AEGIR_BACKUP_MODE_CTRL));
      if ($backup_mode) {
        define('AEGIR_BACKUP_MODE_CTRL_READ', TRUE);
        drush_log(dt("BACKUP/MODE/SET from control file: @var", array('@var' => $backup_mode)), 'success');
      }
    }
    else {
      drush_log("Backup mode control file not found.", 'info');
    }
  }
  define('SELECTED_BACKUP_MODE', $backup_mode);
  if ($backup_mode) {
    drush_set_option('backup_mode', $backup_mode);
  }
  return $backup_mode;
}

/**
 * determine if overall load of the machine is critical
 *
 * We use the "average system load" of the system as a metric, as available
 * through 'uptime' or in PHP sys_getloadavg() since 5.1. The load is usually
 * defined as "the number of processes in the system run queue"
 *
 * It's not a really reliable metric, but it's the best shot we've got without
 * getting into real specific details about I/O, CPU or memory load that are
 * going to be even tougher to evaluate.
 *
 * We base our evaluation on the number of CPUs on the servers. If there are
 * more than 5 processes waiting per CPU, we abort completely. If we ignore the
 * number of available CPUs, we assume a critical limit is a load of 10.
 *
 * @see sys_getloadavg()
 */
function provision_load_critical($load = NULL, $threshold = NULL) {
  if (is_null($load)) {
    $load = sys_getloadavg();
  }
  if (is_null($threshold)) {
    if ($ncpus = provision_count_cpus()) {
      $threshold = $ncpus * drush_get_option('critical_load_multiplier', CRITICAL_LOAD_MULTIPLIER);
    }
    else {
      // can't determine the number of CPU, we hardcode at load 10
      $threshold = drush_get_option('critical_load_threshold', CRITICAL_LOAD_THRESHOLD);
    }
  }
  return ($load[0] > $threshold);
}

/**
 * Check whether a Hosting feature is enabled.
 */
function provision_hosting_feature_enabled($feature) {
  $features = drush_get_option('hosting_features', array());
  return array_key_exists($feature, $features) && $features[$feature];
}

/**
 * Generate one-time login link
 */
function provision_generate_login_reset() {
  $uri = d()->redirection ?: d()->uri;
  $result = drush_invoke_process(d()->name, 'user-login', array(), array('uri' => $uri, 'no-browser' => TRUE));

  return $result['output'];
}
