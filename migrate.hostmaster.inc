<?php

/**
 * @file manual site migrate system
 */

/**
 * Convert a directory name to a drush aliasname, stripping unwanted chars.
 *
 * @param string $name The directory name.
 *
 * @return string
 *   A drush alias name for the platform.
 */
function _provision_path_to_platform_alias($name) {
  return '@platform_' . preg_replace("/[!\W]/", "", basename(rtrim($name, '/')));
}

function drush_provision_hostmaster_migrate_validate($site, $platform) {
  // XXX: this doesn't actually document the pre alpha9 upgrade path
  drush_print('
This command will operate the following changes in your system:

1. verify the server and the hostmaster site (provision-verify)
2. verify the new platform (which will download it if missing)
3. stop the queue (hosting-pause)
4. attempt a migration to the new platform (provision-migrate)
5. start the queue (hosting-resume)

We are making the following assumptions:
 * you have read UPGRADE.txt
 * you are executing this script as your "aegir" user
');

  if (!drush_confirm("Do you really want to proceed with the upgrade?")) {
    return drush_set_error('PROVISION_CANCEL_MIGRATE', 'Upgrade canceled by user');
  }

  $data = drush_get_merged_options();

  $alias_file = drush_server_home() . '/.drush/server_master.alias.drushrc.php';

  if (!file_exists($platform)) {
    $dir = realpath(dirname($platform));
    if (is_dir($dir)) {
      $platform = $dir . '/' . basename($platform);
      drush_log("changed relative platform path to absolute");
    }
  }

  // If the alias file exists, it means we are migrating from a post alpha9 release.
  if (is_readable($alias_file)) {
    provision_backend_invoke('@server_master', 'provision-verify');
    provision_backend_invoke('@hostmaster', 'provision-verify');
    // A panel that does not verify (its site cannot bootstrap, its server
    // context is broken) is not migrated: the new platform would be built
    // and verified, the site never moved, and the failure would surface
    // only as a later command that cannot find the site. Stop here, with
    // the verify's own error above this line.
    if (drush_get_error()) {
      return drush_set_error('PROVISION_HOSTMASTER_VERIFY_FAILED', dt('The server or the hostmaster site did not verify: nothing is migrated. Make @site bootstrap again (drush @site status) and run the upgrade again.', array('@site' => '@hostmaster')));
    }

    drush_set_option('site_name', '@hostmaster');
    drush_set_option('old_platform', d('@hostmaster')->platform->name);

    $new_platform = _provision_path_to_platform_alias($platform);
    drush_invoke_process('@none', "provision-save", array($new_platform), array(
      'context_type' => 'platform',
      'server' => '@server_master',
      'web_server' => '@server_master',
      'root' => $platform,
      'makefile' => drush_get_option('makefile', dirname(__FILE__) . '/aegir.make'),
    ));
    // propagate working-copy args downward
    $options = array();
    if (drush_get_option('working-copy')) {
      $options['working-copy'] = 1;
    }
    // The panel's own codebase: its room is the upgrade's to judge, not the
    // inode gate's (_provision_drush_check_inodes())
    $options['hostmaster_platform_build'] = 1;
    provision_backend_invoke($new_platform, 'provision-verify', array(), $options);
    drush_set_option('new_platform', $new_platform);
    // exit if an error has occured.
    if (drush_get_error()) {
      return false;
    }
  }
  else {

    // add a server record :
    $server = '@server_master';
    /**
     * @deprecated in drush3 it's 'options', in drush 4 it's 'cli', drop
     * 'options' when we drop drush3 support
     */
    $context = drush_get_context('cli') ? 'cli' : 'options';
    drush_invoke_process('@none', "provision-save", array($server), array(
      'context_type' => 'server',
      'aegir_root' => dirname(rtrim($data['config_path'], '/')),
      'remote_host' => php_uname('n'),
      'script_user' => $data['script_user'],
     // apache or nginx or..
      'http_service_type' => drush_get_option('http_service_type', 'apache', $context),
      'web_group' => $data['web_group'],
      'master_url' => $data['master_url'],
      'restart_cmd' => $data['restart_cmd'],
     // mysql
      'db_service_type' => 'mysql',
      'master_db' => $data['master_db'],
    ));

    provision_backend_invoke($server, 'provision-verify');
    drush_set_option('server_name', $server);
    // exit if an error has occured.
    if (drush_get_error()) {
      return false;
    }


    $old_platform = _provision_path_to_platform_alias($data['publish_path']);
    drush_invoke_process('@none', "provision-save", array($old_platform), array(
      'context_type' => 'platform',
      'server' => $server,
      'web_server' => $server,
      'root' => $data['publish_path'],
    ));
    provision_backend_invoke($old_platform, 'provision-verify');
    drush_set_option('old_platform', $old_platform);
    // exit if an error has occured.
    if (drush_get_error()) {
      return false;
    }

    $new_platform = _provision_path_to_platform_alias($platform);
    drush_invoke_process('@none', "provision-save", array($new_platform), array(
      'context_type' => 'platform',
      'server' => $server,
      'web_server' => $server,
      'root' => $platform,
      'makefile' => drush_get_option('makefile', dirname(__FILE__) . '/aegir.make'),
    ));
    provision_backend_invoke($new_platform, 'provision-verify');
    drush_set_option('new_platform', $new_platform);
    // exit if an error has occured.
    if (drush_get_error()) {
      return false;
    }
    _provision_hostmaster_copy_move_marker($data['publish_path'], $platform);


    $site_name = '@hostmaster';
    drush_invoke_process('@none', "provision-save", array($site_name), array(
      'context_type' => 'site',
      'platform' => $old_platform,
      'db_server' => $server,
      'uri' => $site,
      'profile' => 'hostmaster',
      'drush_aliases' => 'hm',
      'command-specific' => array (
        'pm-download' => array (
          'use-site-dir' => 1,
        ),
      ),
    ));
    provision_backend_invoke($site_name, 'provision-verify');
    drush_set_option('site_name', $site_name);
    // exit if an error has occured.
    if (drush_get_error()) {
      return false;
    }
  }
}

/**
 * Copy BOA's panel-move marker into the new root.
 *
 * Before it calls this command BOA writes sites/.panel-move.info in the root
 * the panel runs on: the time and the database this move drops. The new root
 * exists only from the platform verify above, so the marker is copied there
 * now, before the move saves the panel with the new root: until the deploy
 * rewrites the new copy's settings, that copy reads the old database and is
 * held too, while a copy on the new database is not. Only into a root whose
 * hosting reads the database name (the line panel-move-reads-database): an
 * older release would hold the move's own last steps on the new database. BOA
 * removes both markers once this command returns.
 */
function _provision_hostmaster_copy_move_marker($old_root, $new_root) {
  $from = rtrim((string) $old_root, '/') . '/sites/.panel-move.info';
  $to = rtrim((string) $new_root, '/') . '/sites/.panel-move.info';
  $hosting = rtrim((string) $new_root, '/') . '/profiles/hostmaster/modules/aegir/hosting/hosting.module';
  if (!is_file($from) || !is_dir(dirname($to)) || !is_readable($hosting)) {
    return;
  }
  $fields = preg_split('/\s+/', trim((string) @file_get_contents($from)));
  if (empty($fields[1]) || strpos((string) @file_get_contents($hosting), 'panel-move-reads-database') === FALSE) {
    return;
  }
  if (@copy($from, $to)) {
    @chmod($to, 0644);
    drush_log(dt('Copied the panel-move marker into @root.', array('@root' => $new_root)), 'notice');
  }
  else {
    drush_log(dt('Could not copy the panel-move marker into @root.', array('@root' => $new_root)), 'warning');
  }
}

/**
 * Prepare the hostmaster site for migration
 */
function drush_provision_pre_hostmaster_migrate($site, $platform) {
  // we ignore errors here through integrate = FALSE
  // provision_backend_invoke(drush_get_option('site_name'), 'hosting-pause', array(), array('#integrate' => FALSE));
}

function drush_provision_hostmaster_migrate($site, $platform) {
  // See: https://www.drupal.org/node/2762701.
  $platform_version = provision_backend_invoke(drush_get_option('site_name'), 'status', array(), array('fields' => 'drupal-version', 'field-labels' => 0));
  drush_log(dt("Debug trim platform_version @name", array('@name' => trim($platform_version['output']))), 'debug');
  drush_log(dt("Debug platform_version @path: @id", array('@path' => $platform, '@id' => $platform_version)), 'debug');
  $major_version_upgrade = version_compare('7.0', trim($platform_version['output'])) > 0 ? TRUE : FALSE;

  //
  // Disable PHP errors/warnings silencing temporarily
  //
  $major_version_upgrade = FALSE;

  if ($major_version_upgrade) {
    drush_log('Detected major version upgrade. Disabling error-reporting.', 'warning');
    $local_settings_file = d('@hostmaster')->site_path . '/local.settings.php';
    if (!file_exists($local_settings_file)) {
      file_put_contents($local_settings_file, "<?php # local settings.php \n");
    }
    $local_settings_backup = $local_settings_file . '.bak';
    provision_file()->copy($local_settings_file, $local_settings_backup)
      ->succeed('Backed up local.settings.php.')
      ->fail('Failed to backup of local.settings.php.');
    provision_file()->chmod($local_settings_file, 02640);
    $lines_to_append = "
    // These lines were added to overcome a false-positive error thrown in Drupal 7.50+
    error_reporting(0);
    ini_set('display_errors', FALSE);
    ini_set('display_startup_errors', FALSE);";
    file_put_contents($local_settings_file, $lines_to_append, FILE_APPEND);
  }

  $result = provision_backend_invoke(drush_get_option('site_name'), 'provision-migrate', array(drush_get_option('new_platform')));
  // A migrate that failed once the panel's old copy was being retired leaves
  // the panel on the new platform (provision_migrate_result_kept_on_target()).
  // Its record must follow it there as after a success, or the next Verify of
  // the panel writes its alias for the old root, where nothing is left; the
  // post hook does that only for a success. The command still fails.
  if (is_array($result) && !empty($result['error_status'])
    && provision_migrate_result_kept_on_target($result, drush_get_option('new_platform'))) {
    drush_log(dt('The migrate of @site failed after its old copy was retired: it stays on @platform, and its record follows it.', array('@site' => drush_get_option('site_name'), '@platform' => drush_get_option('new_platform'))), 'warning');
    drush_provision_post_hostmaster_migrate($site, $platform);
  }

  if ($major_version_upgrade) {
    drush_log('Re-enabling error-reporting.', 'notice');
    // The site path has changed, so update file paths.
    $local_settings_file = $platform . '/sites/' . d('@hostmaster')->uri . '/local.settings.php';
    $local_settings_backup = $local_settings_file . '.bak';
    provision_file()->chmod($local_settings_file, 02640);
    provision_file()->copy($local_settings_backup, $local_settings_file)
      ->succeed('Restored backup of local.settings.php.')
      ->fail('Failed to restore backup of local.settings.php.');
    provision_file()->unlink($local_settings_backup);
  }
}

function drush_provision_post_hostmaster_migrate($site, $platform) {
  provision_backend_invoke(drush_get_option('site_name'), 'cache-clear', array('drush'));
  // we pass the context names we generated to the task so we can enforce that the names
  // stay the same.
  provision_backend_invoke(drush_get_option('site_name'), 'hosting-resume', array(), array(
    'old_platform_name' => drush_get_option('old_platform'),
    'new_platform_name' => drush_get_option('new_platform'),
  ));
  // We need to re-verify @server_master via frontend to re-generate
  // its drush alias and to update Nginx configuration files.
  $server = '@server_master';
  provision_backend_invoke('@hostmaster', 'hosting-task', array("$server", 'verify'), array('force' => TRUE));
}

