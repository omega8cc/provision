<?php

class Provision_Service_db extends Provision_Service {
  protected $service = 'db';
  protected $creds;

  /**
   * Register the db handler for sites, based on the db_server option.
   */
  static function subscribe_site($context) {
    $context->setProperty('db_server', '@server_master');
    $context->is_oid('db_server');
    $context->service_subscribe('db', $context->db_server->name);
  }

  static function option_documentation() {
    return array(
      'master_db' => 'server with db: Master database connection info, {type}://{user}:{password}@{host}',
      'db_grant_all_hosts' => 'Grant access to site database users from any web host. If set to TRUE, any host will be allowed to connect to MySQL site databases on this server using the generated username and password. If set to FALSE, web hosts will be granted access by their detected IP address.',
    );
  }

  function init_server() {
    parent::init_server();
    $this->server->setProperty('master_db');
    $this->server->setProperty('db_grant_all_hosts', FALSE);
    $this->server->setProperty('utf8mb4_is_supported', FALSE);
    $this->creds = array_map('urldecode', parse_url($this->server->master_db));

    return TRUE;
  }

  function save_server() {
    // Check database 4 byte UTF-8 support and save it for later. A server
    // this save cannot connect to keeps what an earlier save recorded: a
    // failed connection says nothing about its support, and a FALSE recorded
    // for it drops the utf8mb4 charset from every settings.php written after
    // it, until a later save connects again.
    if (method_exists($this, 'connect') && !$this->connect()) {
      return;
    }
    $this->server->utf8mb4_is_supported = $this->utf8mb4_is_supported();
  }

  /**
   * Verifies database connection and commands
   */
  function verify_server_cmd() {
    if ($this->connect()) {
      if ($this->can_create_database()) {
        drush_log(dt('Provision can create new databases.'), 'success');
      }
      else {
        drush_set_error('PROVISION_CREATE_DB_FAILED');
      }
      if ($this->can_grant_privileges()) {
        drush_log(dt('Provision can grant privileges on database users.'), 'success');;
      }
      else {
        drush_set_error('PROVISION_GRANT_DB_USER_FAILED');
      }
      if ($this->server->utf8mb4_is_supported) {
        drush_log(dt('Provision can activate multi-byte UTF-8 support on Drupal 7 sites.'), 'success');
      }
      else {
        drush_log(dt('Multi-byte UTF-8 for Drupal 7 is not supported on your system. See the <a href="@url">documentation on adding 4 byte UTF-8 support</a> for more information.', array('@url' => 'https://www.drupal.org/node/2754539')), 'warning');
      }
    } else {
      drush_set_error('PROVISION_CONNECT_DB_FAILED');
    }
  }

  /**
   * Find a viable database name, based on the site's uri.
   */
  function suggest_db_name() {
    $uri = $this->context->uri;

    if (!$uri) {
      drush_log(dt("URI @uri is EMPTY...", array('@uri' => $uri)), 'info');
    }
    else {
      drush_log(dt("URI is OK @uri", array('@uri' => $uri)), 'info');
    }

    $suggest_base = substr(str_replace(array('.', '-'), '' , preg_replace('/^www\./', '', $uri)), 0, 16);
    drush_command_invoke_all_ref('provision_suggest_db_name_alter', $suggest_base);

    if (!$suggest_base) {
      drush_log(dt("SUGGEST_BASE @suggest_base is EMPTY...", array('@suggest_base' => $suggest_base)), 'info');
    }
    else {
      drush_log(dt("SUGGEST_BASE is OK @suggest_base", array('@suggest_base' => $suggest_base)), 'info');
    }

    // name_is_free() answers NULL when it cannot tell (the database broker
    // did not answer), which ends the search rather than trying every name.
    $free = $this->name_is_free($suggest_base);
    if ($free) {
      return $suggest_base;
    }

    for ($i = 0; !is_null($free) && $i < 100; $i++) {
      $option = sprintf("%s_%d", substr($suggest_base, 0, 15 - strlen( (string) $i) ), $i);
      $free = $this->name_is_free($option);
      if ($free) {
        return $option;
      }
    }

    if (is_null($free)) {
      drush_set_error('PROVISION_CREATE_DB_FAILED', dt("Could not reserve a database name through the database broker"));
      return false;
    }
    drush_set_error('PROVISION_CREATE_DB_FAILED', dt("Could not find a free database names after 100 attempts"));
    return false;
  }

  /**
   * Whether a database name is free for a new site.
   *
   * The name also becomes the site's database login, and the grant sets
   * that login's password, so a name is free only when no database AND no
   * login of it exists. A login alone takes it too: for an account whose
   * name is 16 characters the panel's name is the account's own instance
   * login, and reusing it reset that login's password mid-install. A
   * service that hands names out through a broker reserves the name here
   * instead, and answers NULL when it cannot tell.
   */
  function name_is_free($name) {
    return !$this->database_exists($name) && !$this->user_exists($name);
  }

  /**
   * Whether this server's databases go through the root database broker.
   * A service without one answers FALSE.
   */
  function broker_mode() {
    return FALSE;
  }

  /**
   * Generate a new mysql database and user account for the specified credentials
   */
  function create_site_database($creds = array()) {
    if (empty($creds)) {
      $creds = $this->generate_site_credentials();
    }
    extract($creds);

    if (drush_get_error() || !$this->can_create_database()) {
      drush_set_error('PROVISION_CREATE_DB_FAILED');
      drush_log("Database could not be created.", 'error');
      return FALSE;
    }

    // Through the database broker the database comes first: its create takes
    // every grant still left on the name before the database exists, and a
    // grant made ahead of it would be one of them. Its answer is the proof,
    // since the name's reservation alone already reads as existing there.
    $broker = $this->broker_mode();
    if ($broker && !$this->create_database($db_name)) {
      drush_set_error('PROVISION_CREATE_DB_FAILED', dt("Could not create @name database", array("@name" => $db_name)));
      return FALSE;
    }

    foreach ($this->grant_host_list() as $db_grant_host) {
      drush_log(dt("Granting privileges to %user@%client on %database", array('%user' => $db_user, '%client' => $db_grant_host, '%database' => $db_name)), 'info');
      if (!$this->grant($db_name, $db_user, $db_passwd, $db_grant_host)) {
        drush_set_error('PROVISION_CREATE_DB_FAILED', dt("Could not create database user @user", array('@user' => $db_user)));
      }
      drush_log(dt("Granted privileges to %user@%client on %database", array('%user' => $db_user, '%client' => $db_grant_host, '%database' => $db_name)), 'success');
    }

    if (!$broker) {
      $this->create_database($db_name);
    }
    $status = $this->database_exists($db_name);

    if ($status) {
      drush_log(dt('Created @name database', array("@name" => $db_name)), 'success');
    }
    else {
      drush_set_error('PROVISION_CREATE_DB_FAILED', dt("Could not create @name database", array("@name" => $db_name)));
    }
    return $status;
  }

  /**
   * Remove the database and user account for the supplied credentials
   */
  function destroy_site_database($creds = array()) {
    if (empty($creds)) {
      $creds = $this->fetch_site_credentials();
    }
    extract($creds);

    // Do not attempt to continue if there is no db name.
    if (empty($db_name)) {
      drush_log(dt("Unable to destroy the database because the database name is unknown.", array('@dbname' => $db_name)), 'warning');
      return;
    }

    if ( $this->database_exists($db_name) ) {
      drush_log(dt("Dropping database @dbname", array('@dbname' => $db_name)), 'info');
      if (!$this->drop_database($db_name)) {
        drush_log(dt("Failed to drop database @dbname", array('@dbname' => $db_name)), 'warning');
      }
    }

    if ( $this->database_exists($db_name) ) {
     drush_set_error('PROVISION_DROP_DB_FAILED');
     return FALSE;
    }

    foreach ($this->grant_host_list() as $db_grant_host) {
      drush_log(dt("Revoking privileges of %user@%client from %database", array('%user' => $db_user, '%client' => $db_grant_host, '%database' => $db_name)), 'info');
      if (!$this->revoke($db_name, $db_user, $db_grant_host)) {
        drush_log(dt("Failed to revoke user privileges"), 'warning');
      }
    }
  }


  /**
   * A secret's fingerprint for a debug log line: its length and eight hex
   * characters of its hash, enough to tell two credentials apart, never the
   * value itself. The MyQuick debug switch used to log the admin password
   * of whatever server the site uses into the task log, which the panel
   * shows to the site owner.
   *
   * PHP 5.6-safe.
   */
  function secret_hint($secret) {
    $secret = (string) $secret;
    if ($secret === '') {
      return '(empty)';
    }
    return strlen($secret) . ' chars, sha256 ' . substr(hash('sha256', $secret), 0, 8);
  }

  /**
   * A command line with one secret's shell-escaped form masked, for a debug
   * log line. Exact string replacement, so any character in the secret is
   * covered.
   */
  function masked_command($command, $secret) {
    $secret = (string) $secret;
    if ($secret === '') {
      return $command;
    }
    return str_replace(escapeshellarg($secret), "'***'", $command);
  }

  /**
   * The option that keeps a myloader import in the binary log, or ''.
   *
   * myloader opens every session with SET SQL_LOG_BIN=0 unless told
   * otherwise, and the packaged /etc/mydumper.cnf, which a call without an
   * option file of its own reads, sets it again. On a server running with
   * the binary log on (a replication source) the import then never reached
   * the replica, whose applier stopped at the first replicated write to the
   * tables it never got. --ignore-set=SQL_LOG_BIN drops that one session
   * variable wherever it comes from; --enable-binlog is no substitute: that
   * file sets it again, and the 0.19.3 line refuses the option. With the
   * binary log off it changes nothing. Asked through --help, so a build
   * without it gets its arguments as before.
   */
  function myloader_binlog_option($myloader_path) {
    if (drush_shell_exec($myloader_path . ' --help')) {
      if (preg_match('/^\s+--ignore-set(\s|=)/m', implode("\n", drush_shell_exec_output()))) {
        return ' --ignore-set=SQL_LOG_BIN';
      }
    }
    return '';
  }

  /**
   * The options that put triggers, stored routines and events into a dump.
   *
   * mydumper writes none of them unless asked, so a site that kept them (a
   * logging trigger, a function its queries call, a scheduled event) lost
   * them on a Migrate and on a Restore that carried its database over.
   * Asked through --help, so a build without one of them gets its arguments
   * as before.
   */
  function mydumper_objects_option($mydumper_path) {
    $options = '';
    if (drush_shell_exec($mydumper_path . ' --help')) {
      $help = implode("\n", drush_shell_exec_output());
      foreach (array('--triggers', '--routines', '--events') as $option) {
        if (preg_match('/^\s+(-[A-Za-z],\s+)?' . preg_quote($option, '/') . '(\s|=|$)/m', $help)) {
          $options .= ' ' . $option;
        }
      }
    }
    return $options;
  }

  /**
   * Gives every stored object of a fast dump its own sql_mode back.
   *
   * mydumper writes a database's triggers, routines and events under one
   * file-wide sql_mode, so an object made under another one (Drupal's own
   * connections use ANSI_QUOTES and PIPES_AS_CONCAT) failed a fast import,
   * or was imported and then behaved otherwise. Each object gets a plain SET
   * of the mode the server stored for it before its CREATE and the file's
   * mode after it (myloader stops on a versioned-comment SET in the middle
   * of a file); mode words 8.x refuses are left out, so a 5.7 dump still
   * loads there. The same rewrite as BOA's dump tools. Returns FALSE when
   * an object or a file kept the dump's own mode; the dump stands either way.
   */
  function mydumper_object_modes($db_name, $dump_dir) {
    return $this->mydumper_object_modes_apply($this->mydumper_object_modes_read($db_name), $dump_dir);
  }

  /**
   * The mode each stored object of $db_name was made under, as the server
   * stored it (mode words 8.x refuses left out), keyed by type and hex name;
   * FALSE when the name is not a plain one or the modes cannot be read.
   *
   * A fast dump reads them before mydumper runs: the service's connection
   * then sits idle through the whole dump, and one longer than the server's
   * wait_timeout finds it closed afterwards.
   */
  function mydumper_object_modes_read($db_name) {
    if (!preg_match('/^[A-Za-z0-9_]+$/', (string) $db_name)) {
      return FALSE;
    }
    $result = $this->query("SELECT 'TRIGGER', HEX(TRIGGER_NAME), SQL_MODE FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA = '%s'"
      . " UNION ALL SELECT ROUTINE_TYPE, HEX(ROUTINE_NAME), SQL_MODE FROM information_schema.ROUTINES WHERE ROUTINE_SCHEMA = '%s'"
      . " UNION ALL SELECT 'EVENT', HEX(EVENT_NAME), SQL_MODE FROM information_schema.EVENTS WHERE EVENT_SCHEMA = '%s'",
      $db_name, $db_name, $db_name);
    if (!$result) {
      return FALSE;
    }
    $gone = '/^(?:NO_AUTO_CREATE_USER|NO_FIELD_OPTIONS|NO_KEY_OPTIONS|NO_TABLE_OPTIONS|DB2|MAXDB|MSSQL|MYSQL323|MYSQL40|ORACLE|POSTGRESQL)$/';
    $modes = array();
    while ($row = $result->fetch(PDO::FETCH_NUM)) {
      if (!preg_match('/^(?:TRIGGER|FUNCTION|PROCEDURE|EVENT)$/', (string) $row[0])
        || !preg_match('/^[0-9A-Fa-f]+$/', (string) $row[1])
        || !preg_match('/^[A-Z0-9_,]*$/', (string) $row[2])) {
        continue;
      }
      $keep = array();
      foreach (explode(',', (string) $row[2]) as $word) {
        if ($word !== '' && !preg_match($gone, $word)) {
          $keep[] = $word;
        }
      }
      $modes[$row[0] . ' ' . strtolower($row[1])] = implode(',', $keep);
    }
    return $modes;
  }

  /**
   * Rewrites the object files of the dump in $dump_dir with the modes
   * mydumper_object_modes_read() gave. Returns FALSE when those were not
   * read, or when an object or a file kept the dump's own mode.
   */
  function mydumper_object_modes_apply($modes, $dump_dir) {
    if (!is_array($modes)) {
      return FALSE;
    }
    if (empty($modes)) {
      return TRUE;
    }
    $ok = TRUE;
    $files = array_merge((array) glob($dump_dir . '/*-schema-post.sql'), (array) glob($dump_dir . '/*-schema-triggers.sql'));
    foreach (array_filter($files) as $file) {
      if (is_link($file) || (is_file($file) && $this->mydumper_object_modes_file($file, $modes) !== 0)) {
        $ok = FALSE;
      }
    }
    return $ok;
  }

  /**
   * The rewrite of mydumper_object_modes() on one object file.
   *
   * Returns 0 when written (or written before), 1 when the file is left as
   * it was (no line could be placed), 2 when written with an object that
   * had no mode to give.
   */
  function mydumper_object_modes_file($file, $modes) {
    $lines = @file($file);
    if ($lines === FALSE) {
      return 1;
    }
    $file_mode = NULL;
    foreach (array_slice($lines, 0, 10) as $line) {
      if ($file_mode === NULL && preg_match("/^\\/\\*!40101 SET SQL_MODE='([A-Z0-9_,]*)'\\*\\/;$/", $line, $m)) {
        $file_mode = $m[1];
      }
    }
    if ($file_mode === NULL) {
      return 1;
    }
    $out = array();
    $pending = FALSE;
    $missing = FALSE;
    $count = count($lines);
    for ($i = 0; $i < $count; $i++) {
      $line = $lines[$i];
      if ($pending && preg_match('/^SET character_set_client = @PREV_CHARACTER_SET_CLIENT;$/', $line)) {
        $out[] = "SET SQL_MODE='" . $file_mode . "';\n";
        $pending = FALSE;
      }
      $out[] = $line;
      if (!preg_match('/^DROP (TRIGGER|FUNCTION|PROCEDURE|EVENT) IF EXISTS `((?:[^`]|``)+)`;$/', $line, $m)) {
        continue;
      }
      if ($i < $count - 1 && strpos($lines[$i + 1], 'SET SQL_MODE=') === 0) {
        return 0;
      }
      $key = $m[1] . ' ' . bin2hex(str_replace('``', '`', $m[2]));
      if (isset($modes[$key])) {
        $out[] = "SET SQL_MODE='" . $modes[$key] . "';\n";
        $pending = TRUE;
      }
      else {
        $missing = TRUE;
      }
    }
    if ($pending) {
      return 1;
    }
    $tmp = $file . '.modes';
    $handle = @fopen($tmp, 'x');
    if ($handle === FALSE) {
      return 1;
    }
    $data = implode('', $out);
    $written = fwrite($handle, $data);
    fclose($handle);
    $perms = @fileperms($file);
    if ($written !== strlen($data)
      || ($perms !== FALSE && !@chmod($tmp, $perms & 07777))
      || !@rename($tmp, $file)) {
      @unlink($tmp);
      return 1;
    }
    return $missing ? 2 : 0;
  }

  /**
   * The option that keeps a fast dump of a Percona 5.7 server off the
   * backup lock, or ''.
   *
   * On 5.7 mydumper's default lock mode syncs its threads with FLUSH TABLES
   * WITH READ LOCK and also takes Percona's backup lock (LOCK TABLES FOR
   * BACKUP), keeping it until the read lock is released. A write to a MyISAM
   * table waits for that lock with its table open, and the event scheduler
   * writes one at every event run (mysql.event is MyISAM on 5.7): the flush
   * ahead of the read lock then waits for that table, or the read lock for
   * the writer, and neither side gives way. The server sees no deadlock, so
   * the dump stalls there and every write on the server queues behind it.
   * The read lock alone still gives the dump its consistent point. Given
   * only when the server reports 5.7 and the mydumper lists the option in
   * its --help, read whatever that exits with (the 0.19.3 line exits 1
   * there), so any other server, and a build without it, gets its arguments
   * as before. PHP 5.6-safe.
   */
  function mydumper_backup_locks_option($mydumper_path) {
    $version = $this->server_version();
    if (!is_string($version) || strpos($version, '5.7.') !== 0) {
      return '';
    }
    drush_shell_exec($mydumper_path . ' --help');
    if (preg_match('/^\s+(-[A-Za-z],\s+)?--no-backup-locks(\s|=|$)/m', implode("\n", drush_shell_exec_output()))) {
      return ' --no-backup-locks';
    }
    return '';
  }

  /**
   * The options that hand a fast import's views, triggers, routines and
   * events to the site's own database user.
   *
   * The import runs with the server's admin login, and each object kept the
   * DEFINER it was dumped with: the source database's user, which a Migrate
   * or a Restore drops once the import is done, so each object failed from
   * then on (ERROR 1449, the definer does not exist). The classic path
   * strips the definers and loads as the site's own user, so every object
   * belongs to that user; --replace-definer gives them the same owner here.
   * Stripping them would make the admin login their definer, and an object
   * runs with its definer's rights. A myloader without --replace-definer
   * (the 0.19.3 line) loads the tables and views as before and leaves the
   * triggers, routines and events out, as before.
   */
  function myloader_definer_option($myloader_path, $db_user, $dump_dir) {
    $help = '';
    if (drush_shell_exec($myloader_path . ' --help')) {
      $help = implode("\n", drush_shell_exec_output());
    }
    if (preg_match('/^\s+--replace-definer(\s|=)/m', $help)
      && preg_match('/^[A-Za-z0-9_]+$/', (string) $db_user)) {
      $host = $this->definer_host($db_user);
      if (is_string($host) && preg_match('/^[A-Za-z0-9_.:%-]+$/', $host)) {
        return ' --replace-definer=' . escapeshellarg('`' . $db_user . '`@`' . $host . '`');
      }
    }
    $options = '';
    foreach (array('--skip-triggers', '--skip-post') as $option) {
      if (preg_match('/^\s+' . preg_quote($option, '/') . '(\s|=)/m', $help)) {
        $options .= ' ' . $option;
      }
    }
    $carried = array_merge((array) glob($dump_dir . '/*-schema-triggers.sql*'), (array) glob($dump_dir . '/*-schema-post.sql*'));
    if ($options !== '' && count(array_filter($carried))) {
      drush_log(dt('The dump carries triggers, routines or events, and this myloader cannot hand them to @user: they are left out of the import.', array('@user' => $db_user)), 'warning');
    }
    return $options;
  }

  function import_site_database($dump_file = null, $creds = array()) {
    if (empty($creds)) {
      $creds = $this->fetch_site_credentials();
    }
    extract($creds);

    $enable_myquick = FALSE;
    $myloader_path = FALSE;
    $myquick_creds_log = '/data/conf/_myquick_creds_log.txt';

    $backup_mode = provision_backup_mode_resolve();

    $restore_wants_classic = FALSE;
    if (drush_get_option('is_restore', FALSE)) {
      if (is_file(d()->site_path . '/database.sql')) {
        // Never take the MyQuick fast-import path on a restore that delivered
        // a dump: tmp_expim then holds the PRE-restore safety dump of the
        // CURRENT database, and importing it silently restores nothing. The
        // classic branch imports the archive's own database.sql instead.
        $restore_wants_classic = TRUE;
      }
      else {
        // A dump-less archive (a modeless MyQuick-era snapshot, a Migrate
        // or Delete safety copy, a "Site files without any DB" backup)
        // restores files only. The deploy has already created a fresh,
        // empty database for the site, so leaving the database as it was
        // means carrying the current one across (the restore passes its name
        // in as restore_source_db): dump the old database into
        // tmp_expim here and let the fast import below load it, which its
        // internal-flow rule accepts as a single fresh foreign dump. The
        // restore's own safety copy is classic and leaves no dump in
        // tmp_expim. When the current database cannot be carried across,
        // the classic branch fails the deploy ("No database dump was found"),
        // which rolls the site back: tmp_expim is never read here without a
        // dump this task made, since a stale internal flag could otherwise
        // let another site's leftover dump pass the acceptance rule.
        $old_db_name = drush_get_option('restore_source_db', drush_get_option('old_db_name', ''));
        $aegir_root = d('@server_master')->aegir_root;
        $fast_path = empty($backup_mode)
          && is_file($aegir_root . '/static/control/MyQuick.info')
          && is_executable('/usr/local/bin/mydumper')
          && is_executable('/usr/local/bin/myloader');
        if ($fast_path
          && $old_db_name !== ''
          && $old_db_name !== $db_name
          && $this->database_exists($old_db_name)) {
          $this->generate_dump($old_db_name);
          if (drush_get_error()) {
            return FALSE;
          }
          drush_log(dt('The restored archive carries no database dump; the current database is carried over unchanged (files-only restore).'), 'warning');
        }
        else {
          $restore_wants_classic = TRUE;
          if (!$fast_path) {
            drush_log(dt('The restored archive carries no database dump, and without fast DB backups the current database cannot be carried over.'), 'warning');
          }
          else {
            drush_log(dt('The restored archive carries no database dump, and the current database (@db) could not be found to carry over.', array('@db' => $old_db_name === '' ? 'unknown' : $old_db_name)), 'warning');
          }
        }
      }
    }
    if (empty($backup_mode) && !$restore_wants_classic) {
      drush_log(dt("MyQuick import_site_database db.php db_name @var", array('@var' => $db_name)), 'info');
      $mydumper_path = '/usr/local/bin/mydumper';
      $myloader_path = '/usr/local/bin/myloader';
      $script_user = d('@server_master')->script_user;
      $aegir_root = d('@server_master')->aegir_root;
      $backup_path = d('@server_master')->backup_path;
      $oct_db_dirx = $backup_path . '/tmp_expim';
      $enable_myquick = $aegir_root . '/static/control/MyQuick.info';
      drush_log(dt("MyQuick import_site_database db.php enable_myquick @var", array('@var' => $enable_myquick)), 'info');
    }

    if (is_file($enable_myquick) && is_executable($myloader_path)) {

      if ($db_name) {
        $mycnf = $this->generate_mycnf();

        // mydumper and myloader connect with the admin credentials of the db
        // server THIS site subscribes to, taken from that server's master_db
        // ($this->creds): the identity every other database operation on this
        // service object already uses. The instance's .oN.pass.php used to be
        // read here instead; it always describes the master box's own server,
        // so a site on an additional db server had its dump taken from, and
        // its import loaded into, the wrong server. User and password are
        // resolved as a pair: a master_db missing either half falls back to
        // the site's own credentials together, never to an admin user with a
        // site password. The port comes from the server property rather than
        // from master_db, which the master password rotation rewrites without
        // a port.
        if (!empty($this->creds['user']) && !empty($this->creds['pass'])) {
          $oct_db_user = $this->creds['user'];
          $oct_db_pass = $this->creds['pass'];
        }
        else {
          $oct_db_user = $db_user;
          $oct_db_pass = $db_passwd;
        }
        $oct_db_host = empty($this->creds['host']) ? $db_host : $this->creds['host'];
        $oct_db_port = empty($this->server->db_port) ? $db_port : $this->server->db_port;

        if ($this->server->db_port == '6033') {
          if (is_readable('/opt/tools/drush/proxysql_adm_pwd.inc')) {
            include('/opt/tools/drush/proxysql_adm_pwd.inc');
            if ($writer_node_ip) {
              drush_log('Skip ProxySQL in import_site_database', 'notice');
              $oct_db_host = $writer_node_ip;
              $oct_db_port = '3306';
            }
            else {
              drush_log('Using ProxySQL in import_site_database', 'notice');
            }
          }
        }
      }
      else {
        drush_log(dt("MyQuick import_site_database db.php FAIL no db_name @var", array('@var' => $db_name)), 'info');
      }

      if (!is_dir($oct_db_dirx)) {
        drush_log(dt("MyQuick import_site_database db.php fail oct_db_dirx @var", array('@var' => $oct_db_dirx)), 'info');
        drush_set_error('PROVISION_DB_IMPORT_FAILED', dt('Database import failed (dir: %dir)', array('%dir' => $oct_db_dirx)));
      }

      $threads = provision_count_cpus();
      $threads = max(2, intval($threads / 4) + 1);
      drush_log(dt("MyQuick import_site_database db.php db_name @var", array('@var' => $db_name)), 'info');
      if (provision_file()->exists($myquick_creds_log)->status()) {
        drush_log(dt("MyQuick import_site_database db.php oct_db_user @var", array('@var' => $oct_db_user)), 'info');
        drush_log(dt("MyQuick import_site_database db.php oct_db_pass @var", array('@var' => $this->secret_hint($oct_db_pass))), 'info');
        drush_log(dt("MyQuick import_site_database db.php oct_db_host @var", array('@var' => $oct_db_host)), 'info');
        drush_log(dt("MyQuick import_site_database db.php oct_db_port @var", array('@var' => $oct_db_port)), 'info');
      }

      // Create pre-db-import flag file.
      $pre_import_flag = $backup_path . '/.pre_import_flag.pid';
      $pre_import_flag_blank = "Starting Import \n";
      $local_description = 'Adding Pre-DB-Import Flag-File import_site_database db.php';
      if (!provision_file()->exists($pre_import_flag)->status()) {
        provision_file()->file_put_contents($pre_import_flag, $pre_import_flag_blank)
          ->succeed('Generated blank ' . $local_description)
          ->fail('Could not generate ' . $local_description);
      }

      // The dump side refuses to call an export successful without
      // mydumper's final `metadata` marker (see generate_dump), because a
      // run killed mid-flight leaves a directory full of partial table
      // files. The import side only checked that the DIRECTORY exists, so
      // exactly that half-written dump would be loaded over a live database
      // and reported as a clean import. Require the same marker here.
      if (is_dir($oct_db_dirx) && !is_file($oct_db_dirx . '/metadata')) {
        drush_set_error('PROVISION_DB_IMPORT_FAILED', dt('Refusing to import %dir: it carries no mydumper metadata marker, so the dump is incomplete', array('%dir' => $oct_db_dirx)));
      }
      elseif (is_dir($oct_db_dirx) &&
        $db_name &&
        $oct_db_user &&
        $oct_db_pass &&
        $oct_db_host &&
        $oct_db_port) {
        // The tmp_expim store is shared by every site of the account and is
        // deliberately NOT bound to a database name (Aegir renames databases
        // on clone/migrate), so the store must prove whose dump it holds
        // before myloader may run: importing a leftover export from an
        // earlier broken task has restored the WRONG database into a site
        // before. mydumper names its schema-create file after the source
        // database. Acceptance rules:
        // - this database's own dump: import;
        // - a single foreign dump inside an internal flow (clone/migrate
        //   import a source-named dump into the renamed target database),
        //   and only when the dump is NEWER than the internal flag - the
        //   flag is written before the flow's own backup dumps, so a
        //   legitimate flow always leaves metadata newer than the flag,
        //   while a broken task's leftover always predates it: import;
        // - anything else (mixed store, foreign dump with no flow or a
        //   stale one): refuse.
        $own_dump = glob($oct_db_dirx . '/' . $db_name . '-schema-create.sql*');
        $all_dumps = glob($oct_db_dirx . '/*-schema-create.sql*');
        $own_count = is_array($own_dump) ? count($own_dump) : 0;
        $all_count = is_array($all_dumps) ? count($all_dumps) : 0;
        $internal_flag_file = $backup_path . '/.internal_backup_flag.pid';
        $internal_flow = is_file($internal_flag_file);
        $import_allowed = FALSE;
        if ($all_count && $own_count == $all_count) {
          $import_allowed = TRUE;
        }
        elseif ($all_count && !$own_count && $internal_flow) {
          $foreign_names = array();
          foreach ($all_dumps as $dump_file) {
            $foreign_names[] = basename($dump_file);
          }
          $distinct = array();
          foreach ($foreign_names as $foreign_name) {
            $distinct[preg_replace('/-schema-create\.sql.*$/', '', $foreign_name)] = TRUE;
          }
          $metadata_file = $oct_db_dirx . '/metadata';
          $dump_fresh = is_file($metadata_file)
            && (@filemtime($metadata_file) >= @filemtime($internal_flag_file));
          if (count($distinct) == 1 && $dump_fresh) {
            $import_allowed = TRUE;
            drush_log(dt('Fast import of the internal-flow source dump @found into database @db.', array('@found' => implode(', ', $foreign_names), '@db' => $db_name)), 'info');
          }
        }
        if (!$import_allowed) {
          $found_dumps = array();
          if (is_array($all_dumps)) {
            foreach ($all_dumps as $dump_file) {
              $found_dumps[] = basename($dump_file);
            }
          }
          drush_set_error('PROVISION_DB_IMPORT_FAILED', dt('Refusing the fast database import: the shared dump store does not provably hold this site\'s database (expected @db, internal flow: @flow, found: @found). A leftover or concurrent export must never be imported - re-run the task.', array('@db' => $db_name, '@flow' => $internal_flow ? 'yes' : 'no', '@found' => count($found_dumps) ? implode(', ', $found_dumps) : 'no dump at all')));
        }
        elseif ($this->broker_mode()) {
          // The broker loads the store as the site's own database user,
          // proven first, never with an admin login: the site user, then
          // its password, one line each on stdin.
          if ($this->broker_call('load', array('--', $db_name), $db_user . "\n" . $db_passwd . "\n") === FALSE) {
            drush_set_error('PROVISION_DB_IMPORT_FAILED', dt('Database import failed: %output', array('%output' => $this->broker_failure())));
          }
        }
        else {
          // SECURITY: $db_name derives from alias context; $oct_db_* and
          // $oct_db_dirx originate in BOA root control files but may contain
          // shell-special characters in passwords. Escape every interpolated
          // value with escapeshellarg() before shell exec.
          $command = $myloader_path
            . ' --database=' . escapeshellarg($db_name)
            . ' --host=' . escapeshellarg($oct_db_host)
            . ' --user=' . escapeshellarg($oct_db_user)
            . ' --password=' . escapeshellarg($oct_db_pass)
            . ' --port=' . escapeshellarg($oct_db_port)
            . ' --directory=' . escapeshellarg($oct_db_dirx)
            . ' --threads=' . escapeshellarg($threads)
            . ' --drop-table=DROP' . $this->myloader_binlog_option($myloader_path)
            . $this->myloader_definer_option($myloader_path, $db_user, $oct_db_dirx)
            . ' --verbose=2';
          if (provision_file()->exists($myquick_creds_log)->status()) {
            drush_log(dt("MyQuick import_site_database db.php Cmd @var", array('@var' => $this->masked_command($command, $oct_db_pass))), 'info');
          }
          $success = provision_shell_exec_secret($command, array($oct_db_pass));

          if (!$success) {
            // Never interpolate $command into messages: it carries --password.
            drush_set_error('PROVISION_DB_IMPORT_FAILED', dt('Database import failed: %output', array('%output' => join("\n", drush_shell_exec_output()))));
          }
        }

        // Delete pre-db-import flag file.
        provision_file()->unlink($pre_import_flag)
          ->succeed('Remove Pre-DB-Import Flag-File import_site_database db.php')
          ->fail('Could not remove Pre-DB-Import Flag-File import_site_database db.php');

        // Create post-db-import flag file.
        $post_import_flag = $backup_path . '/.post_import_flag.pid';
        $post_import_flag_blank = "Post-DB-Import \n";
        $local_description = 'Adding Post-DB-Import Flag-File import_site_database db.php';
        if (!provision_file()->exists($post_import_flag)->status()) {
          provision_file()->file_put_contents($post_import_flag, $post_import_flag_blank)
            ->succeed('Generated blank ' . $local_description)
            ->fail('Could not generate ' . $local_description);
        }
      }
    }
    else {
      if (is_null($dump_file)) {
        $dump_file = d()->site_path . '/database.sql';
      }
      if (empty($creds)) {
        $creds = $this->fetch_site_credentials();
      }
      $exists = provision_file()->exists($dump_file)
        ->succeed('Found database dump at @path.')
        ->fail('No database dump was found at @path.', 'PROVISION_DB_DUMP_NOT_FOUND')
        ->status();
      if ($exists) {
        $readable = provision_file()->readable($dump_file)
          ->succeed('Database dump at @path is readable')
          ->fail('The database dump at @path could not be read.', 'PROVISION_DB_DUMP_NOT_READABLE')
          ->status();
        if ($readable) {
          $this->import_dump($dump_file, $creds);
        }
      }
    }
  }

  function generate_site_credentials() {
    $creds = array();
    // replace with service type
    $db_type = drush_get_option('db_type', function_exists('mysqli_connect') ? 'mysqli' : 'mysql');
    // As of Drupal 7 there is no more mysqli type
    if (drush_drupal_major_version() >= 7) {
      $db_type = ($db_type == 'mysqli') ? 'mysql' : $db_type;
    }

    //TODO - this should not be here at all
    $creds['db_type'] = drush_set_option('db_type', $db_type, 'site');
    $creds['db_host'] = drush_set_option('db_host', $this->server->remote_host, 'site');
    $creds['db_port'] = drush_set_option('db_port', $this->server->db_port, 'site');
    $creds['db_passwd'] = drush_set_option('db_passwd', provision_password(), 'site');
    $creds['db_name'] = drush_set_option('db_name', $this->suggest_db_name(), 'site');
    $user = $creds['db_name'];
    drush_command_invoke_all_ref('provision_db_username_alter', $user, $creds['db_host']);
    $creds['db_user'] = drush_set_option('db_user', $user, 'site');

    return $creds;
  }

  function fetch_site_credentials() {
    $creds = array();

    $keys = array('db_type', 'db_port', 'db_user', 'db_name', 'db_host', 'db_passwd');
    foreach ($keys as $key) {
      $creds[$key] = drush_get_option($key, '', 'site');
    }

    return $creds;
  }

  function database_exists($name) {
    return FALSE;
  }

  /**
   * Whether a database login of this name exists on this server, on any
   * host. A service that cannot tell answers FALSE.
   */
  function user_exists($name) {
    return FALSE;
  }

  /**
   * The host part of an existing login named $name, for a DEFINER, or
   * FALSE. A service that cannot tell answers FALSE.
   */
  function definer_host($name) {
    return FALSE;
  }

  /**
   * The version this server reports (SELECT VERSION()), or FALSE. A service
   * that cannot tell answers FALSE.
   */
  function server_version() {
    return FALSE;
  }

  function drop_database($name) {
    return FALSE;
  }

  function create_database($name) {
    return FALSE;
  }

  function can_create_database() {
    return FALSE;
  }

  function can_grant_privileges() {
    return FALSE;
  }

  function grant($name, $username, $password, $host = '') {
    return FALSE;
  }

  function revoke($name, $username, $host = '') {
    return FALSE;
  }

  function import_dump($dump_file, $creds) {
    return FALSE;
  }

  function generate_dump($source_db = NULL) {
    return FALSE;
  }

  /**
   * Return a list of hosts, as seen by the db server, which should be granted
   * access to the site database. If server property 'db_grant_all_hosts' is
   * TRUE, use the MySQL wildcard '%' instead of
   */
  function grant_host_list() {
    if ($this->server->db_grant_all_hosts) {
      return array('%');
    }
    else {
      return array_unique(array_map(array($this, 'grant_host'), $this->context->service('http')->grant_server_list()));
    }
  }

  /**
   * Return a hostname suitable for database grants from a server object.
   */
  function grant_host(Provision_Context_server $server) {
    return $server->remote_host;
  }

  /**
   * Checks whether utf8mb4 support is available on the current database system.
   *
   * @return bool
   */
  function utf8mb4_is_supported() {
    // By default we assume that the database backend may not support 4 byte
    // UTF-8.
    return FALSE;
  }
}
