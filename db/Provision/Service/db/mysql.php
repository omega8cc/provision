<?php
/**
 * @file
 * Provides the MySQL service driver.
 */

/**
 * The MySQL provision service.
 */
class Provision_Service_db_mysql extends Provision_Service_db_pdo {
  public $PDO_type = 'mysql';
  protected $safe_shell_exec_output = '';
  protected $has_port = TRUE;

  function default_port() {
    $script_user = d('@server_master')->script_user;
    if (!$script_user) {
      $script_user = drush_get_option('script_user');
    }
    if (!$script_user && $server->script_user) {
      $script_user = $server->script_user;
    }
    $ctrlf = '/data/conf/' . $script_user . '_use_proxysql.txt';
    if (provision_file()->exists($ctrlf)->status()) {
      return 6033;
    }
    else {
      return 3306;
    }
  }

  function drop_database($name) {
    return $this->query("DROP DATABASE `%s`", $name);
  }

  function create_database($name) {
    return $this->query("CREATE DATABASE `%s`", $name);
  }

  function can_create_database() {
    $test = drush_get_option('aegir_db_prefix', 'site_') . 'tmp_test';
    $this->create_database($test);

    if ($this->database_exists($test)) {
      if (!$this->drop_database($test)) {
        drush_log(dt("Failed to drop database @dbname", array('@dbname' => $test)), 'warning');
      }
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Verifies that provision can grant privileges to a user on a database.
   *
   * @return
   *   TRUE if the check was successful.
   */
  function can_grant_privileges() {
    $dbname   = drush_get_option('aegir_db_prefix', 'site_') . 'tmp_test';
    $this->create_database($dbname);
    $user     = $dbname . '_user';
    $password = $dbname . '_password';
    $host     = 'localhost';
    $status = $this->grant($dbname, $user, $password, $host);
    $this->revoke($dbname, $user, $host);
    $this->drop_database($dbname);
    return $status;
  }

  function grant($name, $username, $password, $host = '') {
    if (provision_file()->exists('/data/conf/clstr.cnf')->status()) {
      $host = '%';
    }
    $host = ($host) ? $host : '%';

    if ($host != "127.0.0.1") {
      $extra_host = "127.0.0.1";
      $this->grant_privileges($name, $username, $password, $extra_host);
    }

    // Support for ProxySQL integration
    if ($name && $this->server->db_port == '6033') {
      if (is_readable('/opt/tools/drush/proxysql_adm_pwd.inc')) {
        include('/opt/tools/drush/proxysql_adm_pwd.inc');
        $proxysqlc = "SELECT hostgroup_id,hostname,port,status FROM mysql_servers;";
        $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
        drush_shell_exec($command);
        if (preg_match("/Access denied for user 'admin'@'([^']*)'/", implode('', drush_shell_exec_output()), $match)) {
          drush_log(dt("Failed to add @name to ProxySQL", array('@name' => $name)), 'warning');
        }
        elseif (preg_match("/Host '([^']*)' is not allowed to connect to/", implode('', drush_shell_exec_output()), $match)) {
          drush_log(dt("Failed to add @name to ProxySQL", array('@name' => $name)), 'warning');
        }
        else {
          $proxysqlc = "DELETE FROM mysql_users where username='" . $name . "';";
          $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
          drush_shell_exec($command);

          $proxysqlc = "INSERT INTO mysql_users (username,password,default_hostgroup) VALUES ('" . $name . "','" . $password . "',10);";
          $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
          drush_shell_exec($command);

          $proxysqlc = "LOAD MYSQL USERS TO RUNTIME;";
          $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
          drush_shell_exec($command);

          $proxysqlc = "SAVE MYSQL USERS FROM RUNTIME;";
          $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
          drush_shell_exec($command);

          $proxysqlc = "SAVE MYSQL USERS TO DISK;";
          $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
          drush_shell_exec($command);

          $proxysqlc = "DELETE FROM mysql_query_rules where username='" . $name . "';";
          $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
          drush_shell_exec($command);

          $proxysqlc = "INSERT INTO mysql_query_rules (username,destination_hostgroup,active) values ('" . $name . "',10,1);";
          $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
          drush_shell_exec($command);

          $proxysqlc = "INSERT INTO mysql_query_rules (username,destination_hostgroup,active) values ('" . $name . "',11,1);";
          $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
          drush_shell_exec($command);

          $proxysqlc = "LOAD MYSQL QUERY RULES TO RUNTIME;";
          $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
          drush_shell_exec($command);

          $proxysqlc = "SAVE MYSQL QUERY RULES TO DISK;";
          $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
          drush_shell_exec($command);
        }
      }
    }

    return $this->grant_privileges($name, $username, $password, $host);
  }

  function create_user($username, $host) {
    $statement = "CREATE USER IF NOT EXISTS `%s`@`%s`";
    return $this->query($statement, $username, $host);
  }

  function alter_user($username, $host, $password) {
    $statement = "ALTER USER `%s`@`%s` IDENTIFIED BY '%s'";
    return $this->query($statement, $username, $host, $password);
  }

  function grant_privileges($name, $username, $password, $host) {
    $user_created = $this->create_user($username, $host);
    $user_altered = $this->alter_user($username, $host, $password);
    if (!$user_created) {
      drush_log(dt("Failed to create db_user @name", array('@name' => $username)), 'error');
      return $user_created;
    }
    if (!$user_altered) {
      drush_log(dt("Failed to alter db_user @name", array('@name' => $username)), 'error');
      return $user_altered;
    }

    // MySQL did this to us. https://github.com/drush-ops/drush/issues/5368#issuecomment-1405209770
    //
    // MySQL reads _ and % in the ON clause as LIKE wildcards, so an unescaped
    // name grants on a PATTERN of databases rather than on the one named: a
    // grant on `site_0` also covers `siteX0` for any X. suggest_db_name() mints
    // exactly those names, appending _<n> whenever a 16-character truncation
    // collides, so the wildcard lands on real BOA databases -- and here it is
    // ALL PRIVILEGES, not just SELECT. Verified on Percona 5.7.44 and 8.4.10.
    //
    // Interpolated rather than passed as %s: query() renders %s through
    // PDO::quote(), whose MySQL escaping doubles the backslash, and a grant on
    // `site\\_0` names a database with a literal backslash and confers nothing
    // at all (measured). Only names that are already plain identifiers take this
    // path; anything else keeps the previous behaviour untouched.
    if (preg_match('/^[A-Za-z0-9_]+$/', $name) !== 1) {
      $statement = "GRANT ALL PRIVILEGES ON `%s`.* TO `%s`@`%s`";
      return $this->query($statement, $name, $username, $host);
    }

    $escaped = str_replace(array('_', '%'), array('\_', '\%'), $name);
    $statement = "GRANT ALL PRIVILEGES ON `" . $escaped . "`.* TO `%s`@`%s`";
    $granted = $this->query($statement, $username, $host);

    // Converge: a site created before this escaping existed still holds the old
    // pattern grant, and adding the literal one does not take it away. Drop it
    // once its replacement is in place, so existing sites heal on their next
    // verify instead of keeping the wildcard for the life of the database.
    if ($granted && $escaped !== $name) {
      $this->revoke_wildcard_db_grant($name, $username, $host);
    }

    return $granted;
  }

  /**
   * Drop a pre-escaping wildcard grant, once its literal replacement exists.
   *
   * Matches the UNESCAPED spelling only: the escaped grant reads back with a
   * backslash, so it can never match this pattern and is never revoked here.
   */
  function revoke_wildcard_db_grant($name, $username, $host) {
    if (preg_match('/^[A-Za-z0-9_]+$/', $name) !== 1) {
      return;
    }
    $grants = $this->query("SHOW GRANTS FOR `%s`@`%s`", $username, $host);
    if (!$grants) {
      return;
    }
    $found = FALSE;
    while ($row = $grants->fetch()) {
      $statement = array_pop($row);
      if (preg_match('/^GRANT .+ ON `' . preg_quote($name, '/') . '`\.\* /', $statement) === 1) {
        $found = TRUE;
        break;
      }
    }
    if (!$found) {
      return;
    }
    $revoked = $this->query("REVOKE ALL PRIVILEGES ON `" . $name . "`.* FROM `%s`@`%s`", $username, $host);
    if ($revoked) {
      drush_log(dt("Replaced the wildcard grant on @name with an exact one for @user", array('@name' => $name, '@user' => $username)), 'notice');
    }
    else {
      drush_log(dt("Could not drop the wildcard grant on @name for @user; the exact grant is in place alongside it", array('@name' => $name, '@user' => $username)), 'warning');
    }
  }

  /**
   * Remove a dropped account's ProxySQL registration.
   *
   * Accounts register in ProxySQL under the DATABASE name at grant time, so a
   * dropped account leaves under the same key: $name here is the database, not
   * the sql username, matching the grant-time INSERT in grant(). Runs before
   * each DROP USER site in revoke(); the DELETE-flavoured statements are
   * idempotent, so a shared registration cleaned once per dropping host costs
   * nothing. A no-op off ProxySQL boxes.
   */
  function proxysql_cleanup($name) {
    if (!$name || $this->server->db_port != '6033') {
      return;
    }
    if (!is_readable('/opt/tools/drush/proxysql_adm_pwd.inc')) {
      return;
    }
    include('/opt/tools/drush/proxysql_adm_pwd.inc');
    $proxysqlc = "SELECT hostgroup_id,hostname,port,status FROM mysql_servers;";
    $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
    drush_shell_exec($command);
    if (preg_match("/Access denied for user 'admin'@'([^']*)'/", implode('', drush_shell_exec_output()), $match)) {
      drush_log(dt("REVOKE/PXY: Failed to delete @name in ProxySQL", array('@name' => $name)), 'warning');
    }
    elseif (preg_match("/Host '([^']*)' is not allowed to connect to/", implode('', drush_shell_exec_output()), $match)) {
      drush_log(dt("REVOKE/PXY: Failed to delete @name in ProxySQL", array('@name' => $name)), 'warning');
    }
    else {
      $proxysqlc = "DELETE FROM mysql_users where username='" . $name . "';";
      $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
      drush_shell_exec($command);

      $proxysqlc = "LOAD MYSQL USERS TO RUNTIME;";
      $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
      drush_shell_exec($command);

      $proxysqlc = "SAVE MYSQL USERS FROM RUNTIME;";
      $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
      drush_shell_exec($command);

      $proxysqlc = "SAVE MYSQL USERS TO DISK;";
      $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
      drush_shell_exec($command);

      $proxysqlc = "DELETE FROM mysql_query_rules where username='" . $name . "';";
      $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
      drush_shell_exec($command);

      $proxysqlc = "LOAD MYSQL QUERY RULES TO RUNTIME;";
      $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
      drush_shell_exec($command);

      $proxysqlc = "SAVE MYSQL QUERY RULES TO DISK;";
      $command = sprintf('mysql -u admin -h %s -P %s -p%s -e %s', '127.0.0.1', '6032', $prxy_adm_paswd, escapeshellarg($proxysqlc));
      drush_shell_exec($command);
    }
  }

  function revoke($name, $username, $host = '') {
    // Define the desired hosts
    if (provision_file()->exists('/data/conf/clstr.cnf')->status()) {
      $desired_hosts = ['%', '127.0.0.1', 'localhost'];
    }
    else {
      $desired_hosts = ['127.0.0.1', 'localhost'];
    }

    // Fetch all host entries for the user
    $hosts_result = $this->query("SELECT host FROM mysql.user WHERE user = '%s'", $username);

    if (!$hosts_result) {
      // The host lookup itself failed, so the state is unknown: fail closed.
      // $success was previously returned here before it was ever assigned.
      drush_log(dt("REVOKE/0: Could not read host entries for sql user: @var", array('@var' => $username)), 'notice');
      return FALSE;
    }

    $success = true;

    // Since grant_privileges() escapes _ and % in the ON clause, the stored
    // spelling of a site's own grant is the escaped one (`site\_0`.*); grants
    // issued before that escaping hold the unescaped spelling. MySQL matches a
    // REVOKE against the stored spelling only, so both spellings must be
    // recognised below and each one revoked exactly as stored -- revoking the
    // wrong spelling raises 1141 and a superseded user keeps ALL PRIVILEGES on
    // a freed name a future site can take. Only plain identifiers ever carry
    // the escaped spelling, mirroring the grant side.
    $name_spellings = array($name);
    if (preg_match('/^[A-Za-z0-9_]+$/', $name) === 1) {
      $escaped = str_replace(array('_', '%'), array('\_', '\%'), $name);
      if ($escaped !== $name) {
        $name_spellings[] = $escaped;
      }
    }

    while ($row = $hosts_result->fetch()) {
      $host = $row['host'];

      // Skip desired hosts; handle them separately if needed
      if (in_array($host, $desired_hosts)) {
        continue;
      }

      // A stray host entry gets the same per-database semantics as the
      // desired hosts below: destroying one site must never touch grants on
      // databases it is not destroying, whatever host row the account holds.
      // Import takes db_user verbatim from the site's settings, so a shared
      // user can sit at `%` on a box without clstr.cnf -- a stray host here
      // -- and the former global REVOKE ALL PRIVILEGES, GRANT OPTION plus
      // unconditional DROP stripped its every grant on every database the
      // moment any one of its sites was destroyed. An entry whose grants
      // were confined to the destroyed database still converges to bare
      // USAGE below and is dropped, so genuinely stray rows keep going away.
      $grants_result = $this->query("SHOW GRANTS FOR `%s`@`%s`", $username, $host);
      $stored_spellings = array();
      $grant_found = false;

      if ($grants_result) {
        while ($grant = $grants_result->fetch()) {
          $grant_statement = array_pop($grant);
          // Check for a grant on this specific database, in whichever spelling
          // MySQL stored. SHOW GRANTS carries the single backslash verbatim
          // over PDO; the doubled form is mysql CLI display escaping only.
          foreach ($name_spellings as $spelling) {
            if (preg_match("/^GRANT .+ ON `" . preg_quote($spelling, '/') . "`\.\*/", $grant_statement)) {
              $stored_spellings[$spelling] = $spelling;
            }
          }
          // Check for any real grant beyond GRANT USAGE (used for DROP decision).
          if (!preg_match("/^GRANT USAGE ON /", $grant_statement)) {
            $grant_found = true;
          }
        }
      }

      // Only REVOKE spellings confirmed above, each exactly as stored; the
      // spelling is interpolated, never passed as %s, for the PDO::quote
      // backslash-doubling reason documented on the desired-hosts loop below.
      $revokes_clean = !empty($stored_spellings);
      foreach ($stored_spellings as $spelling) {
        $revoke_query = sprintf(
          "REVOKE ALL PRIVILEGES ON `%s`.* FROM `%s`@`%s`",
          $spelling,
          $username,
          $host
        );
        $revoke_success = $this->query($revoke_query);
        if (!$revoke_success) {
          drush_log(dt("REVOKE/1: Failed to revoke privileges for sql user: @var", array('@var' => $username)), 'warning');
          $revokes_clean = FALSE;
        }
        $success = $success && $revoke_success;
      }

      // The DROP gates mirror the desired-hosts loop: an unreadable grant
      // list keeps the account -- dropping on unknown state is never safe,
      // and this branch used to drop exactly then, with $grant_found false
      // only because the scan never ran; an account holding no real grants
      // goes; otherwise only a clean revoke pass followed by a re-read
      // showing the bare global USAGE row alone lets it go.
      if (!$grants_result) {
        drush_log(dt("REVOKE/1: Could not read grants for sql user: @var, leaving it as-is", array('@var' => $username)), 'notice');
      }
      elseif (!$grant_found) {
        // Support for ProxySQL integration
        $this->proxysql_cleanup($name);
        $drop_query = sprintf(
          "DROP USER `%s`@`%s`",
          $username,
          $host
        );
        $drop_success = $this->query($drop_query);
        if (!$drop_success) {
          drush_log(dt("DROP/1: Failed to drop db user: @var", array('@var' => $username)), 'warning');
        }
        $success = $success && $drop_success;
      }
      elseif ($revokes_clean) {
        // Same bare-USAGE whitelist as DROP/3 below, for the same reasons;
        // like there, it is deliberately stricter than the pre-revoke scan
        // above -- do not unify the two.
        $post_grants = $this->query("SHOW GRANTS FOR `%s`@`%s`", $username, $host);
        if (!$post_grants) {
          drush_log(dt("DROP/4: Could not re-read grants for sql user: @var, keeping it", array('@var' => $username)), 'notice');
        }
        else {
          // Seeded FALSE so a result carrying no rows at all keeps the
          // account too; only rows actually seen can qualify it for the drop.
          $usage_only = FALSE;
          while ($grant = $post_grants->fetch()) {
            $grant_statement = array_pop($grant);
            if (!preg_match("/^GRANT USAGE ON \*\.\* TO /", $grant_statement)
              || strpos($grant_statement, ' WITH GRANT OPTION') !== FALSE) {
              $usage_only = FALSE;
              break;
            }
            $usage_only = TRUE;
          }
          if ($usage_only) {
            // Support for ProxySQL integration
            $this->proxysql_cleanup($name);
            $drop_query = sprintf(
              "DROP USER `%s`@`%s`",
              $username,
              $host
            );
            $drop_success = $this->query($drop_query);
            if (!$drop_success) {
              drush_log(dt("DROP/4: Failed to drop db user: @var", array('@var' => $username)), 'warning');
            }
            $success = $success && $drop_success;
          }
        }
      }
    }

    // Handle desired hosts separately if necessary
    foreach ($desired_hosts as $desired_host) {
      // Check if user actually exists for this host before attempting REVOKE/DROP.
      // If it doesn't exist (e.g. interrupted previous cleanup), skip silently to
      // avoid spurious warnings that would mark the task as HOSTING_TASK_WARNING.
      $user_exists_result = $this->query(
        "SELECT 1 FROM mysql.user WHERE User = '%s' AND Host = '%s'",
        $username,
        $desired_host
      );
      if (!$user_exists_result || !$user_exists_result->fetch()) {
        drush_log(dt("REVOKE/2: User @var not found for host @host, skipping.", array('@var' => $username, '@host' => $desired_host)), 'notice');
        continue;
      }

      // Use SHOW GRANTS as the single source of truth for both the REVOKE guard
      // and the DROP decision. mysql.db does hold schema-level grant rows on all
      // supported versions (Percona/MySQL 5.7 and 8.x alike), but only that one
      // scope: global grants live in mysql.user, table/column/routine grants in
      // their own tables, and 8.0+ partial revokes in mysql.user restrictions,
      // none of which a mysql.db read can see. A user holding only a global
      // grant has no mysql.db row yet must not be dropped. SHOW GRANTS reports
      // the full effective picture across all scopes in one statement.
      $grants_result = $this->query("SHOW GRANTS FOR `%s`@`%s`", $username, $desired_host);
      $stored_spellings = array();
      $grant_found = false;

      if ($grants_result) {
        while ($grant = $grants_result->fetch()) {
          $grant_statement = array_pop($grant);
          // Check for a grant on this specific database, in whichever spelling
          // MySQL stored. SHOW GRANTS carries the single backslash verbatim
          // over PDO; the doubled form is mysql CLI display escaping only.
          foreach ($name_spellings as $spelling) {
            if (preg_match("/^GRANT .+ ON `" . preg_quote($spelling, '/') . "`\.\*/", $grant_statement)) {
              $stored_spellings[$spelling] = $spelling;
            }
          }
          // Check for any real grant beyond GRANT USAGE (used for DROP decision).
          if (!preg_match("/^GRANT USAGE ON /", $grant_statement)) {
            $grant_found = true;
          }
        }
      }

      // Only REVOKE spellings confirmed above. Skipping silently avoids MySQL
      // 1141 when the user exists globally but holds no grant on this database
      // (e.g. during migrate cleanup). The spelling is interpolated, never
      // passed as %s: query() renders %s through PDO::quote(), which doubles
      // the backslash into a spelling MySQL never stored.
      $revokes_clean = !empty($stored_spellings);
      foreach ($stored_spellings as $spelling) {
        $revoke_desired_query = sprintf(
          "REVOKE ALL PRIVILEGES ON `%s`.* FROM `%s`@`%s`",
          $spelling,
          $username,
          $desired_host
        );
        $revoke_desired = $this->query($revoke_desired_query);
        if (!$revoke_desired) {
          drush_log(dt("REVOKE/2: Failed to revoke privileges for db user: @var", array('@var' => $username)), 'warning');
          $revokes_clean = FALSE;
        }
        $success = $success && $revoke_desired;
      }

      // Drop the user@desired_host if no real grants remain. An unreadable
      // grant list leaves the account as it stands: with the scan above
      // skipped, $grant_found reports nothing about the account's real state,
      // and dropping on unknown state is never safe.
      if (!$grants_result) {
        drush_log(dt("REVOKE/2: Could not read grants for sql user: @var, leaving it as-is", array('@var' => $username)), 'notice');
      }
      elseif (!$grant_found) {
        $drop_desired_query = sprintf(
          "DROP USER `%s`@`%s`",
          $username,
          $desired_host
        );
        $drop_desired = $this->query($drop_desired_query);
        if (!$drop_desired) {
          drush_log(dt("DROP/2: Failed to drop db user: @var", array('@var' => $username)), 'warning');
        }
        $success = $success && $drop_desired;
      }
      elseif ($revokes_clean) {
        // A revoke above can leave the account holding nothing: MySQL keeps a
        // privilege-less user as a bare `GRANT USAGE ON *.*` row, so the user
        // a credential rotation (restore, deploy, migrate) supersedes would
        // otherwise linger, one per rotation. Re-read the grants and drop the
        // account only when bare USAGE is all that remains: a db_user imported
        // verbatim from a site's settings may legitimately hold grants on
        // other databases, and any line beyond bare USAGE -- a real privilege,
        // `WITH GRANT OPTION`, or a grant list that cannot be read -- keeps
        // the account. The re-check runs only after every revoke on this host
        // succeeded: dropping on partial or unknown state is never safe, and
        // a kept account holds zero privileges either way. This test is
        // deliberately stricter than the pre-revoke scan above: only the
        // global bare-USAGE row qualifies here, while the scan treats any
        // USAGE-prefixed line as empty -- do not unify the two.
        $post_grants = $this->query("SHOW GRANTS FOR `%s`@`%s`", $username, $desired_host);
        if (!$post_grants) {
          drush_log(dt("DROP/3: Could not re-read grants for sql user: @var, keeping it", array('@var' => $username)), 'notice');
        }
        else {
          // Seeded FALSE so a result carrying no rows at all keeps the
          // account too; only rows actually seen can qualify it for the drop.
          $usage_only = FALSE;
          while ($grant = $post_grants->fetch()) {
            $grant_statement = array_pop($grant);
            if (!preg_match("/^GRANT USAGE ON \*\.\* TO /", $grant_statement)
              || strpos($grant_statement, ' WITH GRANT OPTION') !== FALSE) {
              $usage_only = FALSE;
              break;
            }
            $usage_only = TRUE;
          }
          if ($usage_only) {
            // Support for ProxySQL integration
            $this->proxysql_cleanup($name);
            $drop_desired_query = sprintf(
              "DROP USER `%s`@`%s`",
              $username,
              $desired_host
            );
            $drop_desired = $this->query($drop_desired_query);
            if (!$drop_desired) {
              drush_log(dt("DROP/3: Failed to drop db user: @var", array('@var' => $username)), 'warning');
            }
            $success = $success && $drop_desired;
          }
        }
      }
    }

    // FLUSH PRIVILEGES is a no-op in MySQL/Percona 8.0+ because DCL statements
    // (GRANT, REVOKE, CREATE USER, DROP USER) automatically update the privilege
    // cache. Retained for MySQL 5.7 compatibility. Not coupled to $success since
    // a flush failure does not indicate a real privilege management problem.
    $this->query("FLUSH PRIVILEGES");

    return $success;
  }

  function import_dump($dump_file, $creds) {
    if (empty($creds)) {
      $creds = $this->generate_site_credentials();
    }
    extract($creds);

    $enable_myquick = FALSE;
    $myloader_path = FALSE;
    $myquick_creds_log = '/data/conf/_myquick_creds_log.txt';

    $backup_mode = provision_backup_mode_resolve();

    if (empty($backup_mode) && !drush_get_option('is_restore', FALSE)) {
      // Never take the MyQuick fast-import path on a restore: tmp_expim then
      // holds the PRE-restore safety dump of the CURRENT database, and
      // importing it silently restores nothing. The classic path imports the
      // archive's own database.sql instead.
      drush_log(dt("MyQuick import_dump mysql.php db_name first @var", array('@var' => $db_name)), 'info');
      $mydumper_path = '/usr/local/bin/mydumper';
      $myloader_path = '/usr/local/bin/myloader';
      $script_user = d('@server_master')->script_user;
      $aegir_root = d('@server_master')->aegir_root;
      $backup_path = d('@server_master')->backup_path;
      $oct_db_dirx = $backup_path . '/tmp_expim';
      $pass_php_inc = $aegir_root . '/.' . $script_user . '.pass.php';
      drush_log(dt("MyQuick import_dump mysql.php pass_php_inc @var", array('@var' => $pass_php_inc)), 'info');
      $enable_myquick = $aegir_root . '/static/control/MyQuick.info';
      drush_log(dt("MyQuick import_dump mysql.php enable_myquick @var", array('@var' => $enable_myquick)), 'info');
    }

    if (is_file($enable_myquick) && is_executable($myloader_path)) {

      if (provision_file()->exists($pass_php_inc)->status()) {
        include_once($pass_php_inc);
      }

      if ($db_name) {
        $mycnf = $this->generate_mycnf();

        $oct_db_user = empty($oct_db_user) ? $db_user : $oct_db_user;
        $oct_db_pass = empty($oct_db_pass) ? $db_passwd : $oct_db_pass;
        $oct_db_host = empty($oct_db_host) ? $db_host : $oct_db_host;
        $oct_db_port = empty($oct_db_port) ? $db_port : $oct_db_port;

        if ($this->server->db_port == '6033') {
          if (is_readable('/opt/tools/drush/proxysql_adm_pwd.inc')) {
            include('/opt/tools/drush/proxysql_adm_pwd.inc');
            if ($writer_node_ip) {
              drush_log('Skip ProxySQL in import_dump', 'notice');
              $oct_db_host = $writer_node_ip;
              $oct_db_port = '3306';
            }
            else {
              drush_log('Using ProxySQL in import_dump', 'notice');
            }
          }
        }
      }
      else {
        drush_log(dt("MyQuick import_dump mysql.php FAIL no db_name @var", array('@var' => $db_name)), 'info');
      }

      if (!is_dir($oct_db_dirx)) {
        drush_log(dt("MyQuick import_dump mysql.php fail oct_db_dirx @var", array('@var' => $oct_db_dirx)), 'info');
        drush_set_error('PROVISION_DB_IMPORT_FAILED', dt('Database import failed (dir: %dir)', array('%dir' => $oct_db_dirx)));
      }

      $threads = provision_count_cpus();
      $threads = max(2, intval($threads / 4) + 1);
      drush_log(dt("MyQuick import_dump mysql.php db_name second @var", array('@var' => $db_name)), 'info');
      if (provision_file()->exists($myquick_creds_log)->status()) {
        drush_log(dt("MyQuick import_dump mysql.php oct_db_user @var", array('@var' => $oct_db_user)), 'info');
        drush_log(dt("MyQuick import_dump mysql.php oct_db_pass @var", array('@var' => $oct_db_pass)), 'info');
        drush_log(dt("MyQuick import_dump mysql.php oct_db_host @var", array('@var' => $oct_db_host)), 'info');
        drush_log(dt("MyQuick import_dump mysql.php oct_db_port @var", array('@var' => $oct_db_port)), 'info');
      }

      // Create pre-db-import flag file.
      $pre_import_flag = $backup_path . '/.pre_import_flag.pid';
      $pre_import_flag_blank = "Starting Import \n";
      $local_description = 'Adding Pre-DB-Import Flag-File import_dump mysql.php';
      if (!provision_file()->exists($pre_import_flag)->status()) {
        provision_file()->file_put_contents($pre_import_flag, $pre_import_flag_blank)
      	->succeed('Generated blank ' . $local_description)
      	->fail('Could not generate ' . $local_description);
      }

      if (is_dir($oct_db_dirx) &&
        $db_name &&
        $oct_db_user &&
        $oct_db_pass &&
        $oct_db_host &&
        $oct_db_port) {
        // The tmp_expim store is shared by every site of the account, and a
        // concurrent export rotates it wholesale; importing whatever sits
        // there has restored the WRONG database into a site before.
        // mydumper names its schema-create file after the source database,
        // so the store itself proves whose dump it holds - even for dumps
        // made by older code. Acceptance rules:
        // - a mixed store (two databases' dumps) or an empty one: refuse;
        // - this database's own dump: import;
        // - a single foreign dump: import ONLY inside an internal flow
        //   (clone/migrate import a source-named dump into the target
        //   database, marked by the internal-backup flag); outside one it
        //   is a stale leftover of another site - the exact shape that
        //   imported the wrong database in the past - so refuse.
        $own_dump = glob($oct_db_dirx . '/' . $db_name . '-schema-create.sql*');
        $all_dumps = glob($oct_db_dirx . '/*-schema-create.sql*');
        $own_count = is_array($own_dump) ? count($own_dump) : 0;
        $all_count = is_array($all_dumps) ? count($all_dumps) : 0;
        $internal_flow = is_file($backup_path . '/.internal_backup_flag.pid');
        $import_allowed = FALSE;
        if ($all_count && $own_count == $all_count) {
          $import_allowed = TRUE;
        }
        elseif ($all_count && !$own_count && $internal_flow) {
          $foreign_names = array();
          foreach ($all_dumps as $dump_file) {
            $foreign_names[] = basename($dump_file);
          }
          // A clone/migrate imports its own source dump under the target
          // database name (Aegir renames databases mid-flow, which is why
          // the store cannot be bound to a database name). Verify the store
          // holds exactly ONE database's dump, and that the dump was
          // written AFTER the internal flag: the flag is created before the
          // flow's own backup dumps, so a legitimate flow always leaves
          // metadata newer than the flag, while a dump left behind by an
          // earlier broken task always predates it.
          $distinct = array();
          foreach ($foreign_names as $foreign_name) {
            $distinct[preg_replace('/-schema-create\.sql.*$/', '', $foreign_name)] = TRUE;
          }
          $metadata_file = $oct_db_dirx . '/metadata';
          $dump_fresh = is_file($metadata_file)
            && (@filemtime($metadata_file) >= @filemtime($backup_path . '/.internal_backup_flag.pid'));
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
          drush_set_error('PROVISION_DB_IMPORT_FAILED', dt('Refusing the fast database import: the shared dump store does not provably hold this site\'s database (expected @db, internal flow: @flow, found: @found). A concurrent backup may have rotated the store - re-run the task.', array('@db' => $db_name, '@flow' => $internal_flow ? 'yes' : 'no', '@found' => count($found_dumps) ? implode(', ', $found_dumps) : 'no dump at all')));
        }
        else {
          // SECURITY: $db_name derives from alias context; $oct_db_* originate
          // in BOA root control files but may contain shell-special characters.
          // Escape every interpolated value. See DECISIONS.md Decision 002.
          $command = $myloader_path
            . ' --database=' . escapeshellarg($db_name)
            . ' --host=' . escapeshellarg($oct_db_host)
            . ' --user=' . escapeshellarg($oct_db_user)
            . ' --password=' . escapeshellarg($oct_db_pass)
            . ' --port=' . escapeshellarg($oct_db_port)
            . ' --directory=' . escapeshellarg($oct_db_dirx)
            . ' --threads=' . escapeshellarg($threads)
            . ' --drop-table=DROP --verbose=2';
          if (provision_file()->exists($myquick_creds_log)->status()) {
            drush_log(dt("MyQuick import_dump mysql.php Cmd @var", array('@var' => $command)), 'info');
          }
          $success = drush_shell_exec($command);

          if (!$success) {
            // Never interpolate $command into messages: it carries --password.
            drush_set_error('PROVISION_DB_IMPORT_FAILED', dt('Database import failed: %output', array('%output' => join("\n", drush_shell_exec_output()))));
          }
        }

        // Delete pre-db-import flag file.
        provision_file()->unlink($pre_import_flag)
          ->succeed('Remove Pre-DB-Import Flag-File')
          ->fail('Could not remove Pre-DB-Import Flag-File');

		// Create post-db-import flag file.
		$post_import_flag = $backup_path . '/.post_import_flag.pid';
		$post_import_flag_blank = "Post-DB-Import \n";
		$local_description = 'Adding Post-DB-Import Flag-File import_dump mysql.php';
		if (!provision_file()->exists($post_import_flag)->status()) {
		  provision_file()->file_put_contents($post_import_flag, $post_import_flag_blank)
			->succeed('Generated blank ' . $local_description)
			->fail('Could not generate ' . $local_description);
		}
      }
    }
    else {
      $cmd = sprintf("mysql --defaults-file=/dev/fd/3 --force %s", escapeshellcmd($db_name));

      $success = $this->safe_shell_exec($cmd, $db_host, $db_user, $db_passwd, $dump_file);

      drush_log(sprintf("Importing database using command: %s", $cmd));

      // --force makes the client CONTINUE past every failing statement and
      // still exit 0, so the exit status alone cannot tell a clean restore
      // from one where every CREATE TABLE failed. Measured on Percona 8.4: a
      // dump whose statements all fail imports "successfully" and leaves the
      // database empty; a partially failing dump silently loses whole tables.
      // That matters most on Restore, which ALWAYS takes this branch and
      // whose post-hook drops the ORIGINAL database once no error is set.
      // --force is kept so a salvageable dump still loads as much as it can,
      // but the errors it prints are now read and reported.
      $import_errors = '';
      if (preg_match_all('/^ERROR .*/m', $this->safe_shell_exec_output, $matches)) {
        $import_errors = implode("\n", array_slice($matches[0], 0, 10));
      }
      if (!$success) {
        drush_set_error('PROVISION_DB_IMPORT_FAILED', dt("Database import failed: %output", array('%output' => $this->safe_shell_exec_output)));
      }
      elseif ($import_errors && !drush_get_option('force', FALSE)) {
        drush_set_error('PROVISION_DB_IMPORT_FAILED', dt("Database import reported errors and is INCOMPLETE - refusing to treat it as restored: %errors", array('%errors' => $import_errors)));
      }
    }
  }

  function grant_host(Provision_Context_server $server) {
    $user = 'intntnllyInvalid';
    drush_command_invoke_all_ref('provision_db_username_alter', $user, $this->server->remote_host);

    $command = sprintf('mysql -u %s -h %s -P %s -e "SELECT VERSION()"',
      escapeshellarg($user),
      escapeshellarg($this->server->remote_host),
      escapeshellarg($this->server->db_port));

    $server->shell_exec($command);
    $output = implode('', drush_shell_exec_output());
    if (preg_match("/Access denied for user 'intntnllyInvalid'@'([^']*)'/", $output, $match)) {
      return $match[1];
    }
    elseif (preg_match("/Host '([^']*)' is not allowed to connect to/", $output, $match)) {
      return $match[1];
    }
    elseif (preg_match("/ERROR 2002 \(HY000\): Can't connect to local MySQL server through socket '([^']*)'/", $output, $match)) {
      return drush_set_error('PROVISION_DB_CONNECT_FAIL', dt('Local database server not running, or not accessible via socket (%socket): %msg', array('%socket' => $match[1], '%msg' => join("\n", drush_shell_exec_output()))));
    }
    elseif (preg_match("/ERROR 2003 \(HY000\): Can't connect to MySQL server on/", $output, $match)) {
      return drush_set_error('PROVISION_DB_CONNECT_FAIL', dt('Connection to database server failed: %msg', array('%msg' => join("\n", drush_shell_exec_output()))));
    }
    elseif (preg_match("/ERROR 2005 \(HY000\): Unknown MySQL server host '([^']*)'/", $output, $match)) {
      return drush_set_error('PROVISION_DB_CONNECT_FAIL', dt('Cannot resolve database server hostname (%host): %msg', array('%host' => $match[1], '%msg' => join("\n", drush_shell_exec_output()))));
    }
    else {
      return drush_set_error('PROVISION_DB_CONNECT_FAIL', dt('Dummy connection failed to fail. Either your MySQL permissions are too lax, or the response was not understood. See http://is.gd/Y6i4FO for more information. %msg', array('%msg' => join("\n", drush_shell_exec_output()))));
    }
  }

  /**
   * Generate the contents of a mysql config file containing database
   * credentials.
   */
  function generate_mycnf($db_host = NULL, $db_user = NULL, $db_passwd = NULL, $db_port = NULL) {
    // Look up defaults, if no credentials are provided.
    if (is_null($db_host)) {
      $db_host = drush_get_option('db_host');
    }
    if (is_null($db_user)) {
      $db_user = urldecode(drush_get_option('db_user'));
    }
    drush_command_invoke_all_ref('provision_db_username_alter', $db_user, $db_host);
    if (is_null($db_passwd)) {
      $db_passwd = urldecode(drush_get_option('db_passwd'));
    }
    if (is_null($db_port)) {
      $db_port = $this->server->db_port;
    }

    $mycnf = sprintf('[client]
host=%s
user=%s
password="%s"
port=%s
', $db_host, $db_user, $db_passwd, $db_port);

    if ($this->server->utf8mb4_is_supported) {
      $mycnf .= "default-character-set=utf8mb4" . PHP_EOL;
    }

    return $mycnf;
  }

  /**
   * Generate the descriptors necessary to open a process with readable and
   * writeable pipes.
   */
  function generate_descriptorspec($stdin_file = NULL) {
    $stdin_spec = is_null($stdin_file) ? array("pipe", "r") : array("file", $stdin_file, "r");
    $descriptorspec = array(
      0 => $stdin_spec,         // stdin is a pipe that the child will read from
      1 => array("pipe", "w"),  // stdout is a pipe that the child will write to
      2 => array("pipe", "w"),  // stderr is a file to write to
      3 => array("pipe", "r"),  // fd3 is our special file descriptor where we pass credentials
    );
    return $descriptorspec;
  }

  /**
   * Return an array of regexes to filter lines of mysqldumps.
   */
  function get_regexes() {
    static $regexes = NULL;
    if (is_null($regexes)) {
      $regexes = array(
        // remove DEFINER entries
        '#/\*!50013 DEFINER=.*/#' => FALSE,
        // remove another kind of DEFINER line
        '#/\*!50017 DEFINER=`[^`]*`@`[^`]*`\s*\*/#' => '',
        // remove broken CREATE ALGORITHM entries
        '#/\*!50001 CREATE ALGORITHM=UNDEFINED \*/#' => "/*!50001 CREATE */",
      );

      // Allow regexes to be altered or appended to.
      drush_command_invoke_all_ref('provision_mysql_regex_alter', $regexes);
    }
    return $regexes;
  }

  function filter_line(&$line) {
    $regexes = $this->get_regexes();
    foreach ($regexes as $find => $replace) {
      if ($replace === FALSE) {
        if (preg_match($find, $line)) {
          // Remove this line entirely.
          $line = FALSE;
        }
      }
      else {
        $line = preg_replace($find, $replace, $line);
        if (is_null($line)) {
          // preg exploded in our face, oops.
          drush_set_error('PROVISION_BACKUP_FAILED', dt(
            "Error while running regular expression:\n Pattern: !find\n Replacement: !replace",
            array(
              '!find' => $find,
              '!replace' => $replace,
          )));
        }
      }
    }
  }

  /**
   * Generate a mysqldump for use in backups.
   */
  function generate_dump() {
    // Set the umask to 077 so that the dump itself is non-readable by the
    // webserver.
    umask(0077);

    // Determine whether to suppress GTID restore information in the dump.
    // MySQL/Percona 8.0+ commonly runs with GTIDs enabled; importing a dump
    // that contains GTID information will fail unless --set-gtid-purged=OFF
    // is passed. Auto-detect from the server rather than relying on a manual
    // drush option, while still allowing explicit override via that option.
    if (drush_get_option('provision_mysqldump_suppress_gtid_restore', FALSE)) {
      // Explicit override set: always suppress GTID restore information.
      $gtid_option = '--set-gtid-purged=OFF';
    }
    else {
      // Auto-detect: query the server's gtid_mode and suppress if GTIDs are on.
      $gtid_option = '';
      $gtid_result = $this->query("SHOW VARIABLES LIKE 'gtid_mode'");
      if ($gtid_result) {
        $gtid_row = $gtid_result->fetch();
        if ($gtid_row && isset($gtid_row['Value']) && strtoupper($gtid_row['Value']) !== 'OFF') {
          $gtid_option = '--set-gtid-purged=OFF';
          drush_log(dt('GTID mode is @mode: adding --set-gtid-purged=OFF to mysqldump.', array('@mode' => $gtid_row['Value'])), 'info');
        }
      }
    }

    if (empty($creds)) {
      $creds = $this->fetch_site_credentials();
    }
    extract($creds);

    $enable_myquick = FALSE;
    $mydumper_path = FALSE;
    $myquick_creds_log = '/data/conf/_myquick_creds_log.txt';

    $backup_mode = provision_backup_mode_resolve();

    if (empty($backup_mode)) {
      drush_log(dt("MyQuick generate_dump mysql.php db_name @var", array('@var' => $db_name)), 'info');
      $mydumper_path = '/usr/local/bin/mydumper';
      $myloader_path = '/usr/local/bin/myloader';
      $script_user = d('@server_master')->script_user;
      $aegir_root = d('@server_master')->aegir_root;
      $backup_path = d('@server_master')->backup_path;
      $oct_db_dirx = $backup_path . '/tmp_expim';
      $pass_php_inc = $aegir_root . '/.' . $script_user . '.pass.php';
      if (provision_file()->exists($myquick_creds_log)->status()) {
        drush_log(dt("MyQuick generate_dump mysql.php pass_php_inc @var", array('@var' => $pass_php_inc)), 'info');
      }
      $enable_myquick = $aegir_root . '/static/control/MyQuick.info';
      drush_log(dt("MyQuick generate_dump mysql.php enable_myquick @var", array('@var' => $enable_myquick)), 'info');
    }

    if (is_file($enable_myquick) && is_executable($mydumper_path)) {

      $oct_db_test = $oct_db_dirx . '/metadata';
      $oct_db_test_p = $oct_db_dirx . '/metadata.partial';
      while ((is_file($oct_db_test) || is_file($oct_db_test_p)) && $count <= 6) {
        $count++;
        sleep(10);
        drush_log(dt("MyQuick wait 10s for prev db-dump cleanup x @var times (max 6) in generate_dump", array('@var' => $count)), 'info');
      }

      if (provision_file()->exists($pass_php_inc)->status()) {
        include_once($pass_php_inc);
      }

      if ($db_name) {
        $mycnf = $this->generate_mycnf();

        $oct_db_user = empty($oct_db_user) ? $db_user : $oct_db_user;
        $oct_db_pass = empty($oct_db_pass) ? $db_passwd : $oct_db_pass;
        $oct_db_host = empty($oct_db_host) ? $db_host : $oct_db_host;
        $oct_db_port = empty($oct_db_port) ? $db_port : $oct_db_port;

        if ($this->server->db_port == '6033') {
          if (is_readable('/opt/tools/drush/proxysql_adm_pwd.inc')) {
            include('/opt/tools/drush/proxysql_adm_pwd.inc');
            if ($writer_node_ip) {
              drush_log('Skip ProxySQL in generate_dump', 'notice');
              $oct_db_host = $writer_node_ip;
              $oct_db_port = '3306';
            }
            else {
              drush_log('Using ProxySQL in generate_dump', 'notice');
            }
          }
        }
      }
      else {
        drush_log(dt("MyQuick generate_dump mysql.php FAIL no db_name @var", array('@var' => $db_name)), 'info');
      }

      if (is_dir($oct_db_dirx)) {
        drush_log(dt("MyQuick generate_dump mysql.php delete @var", array('@var' => $oct_db_dirx)), 'info');
        _provision_recursive_delete($oct_db_dirx);
        drush_log(dt("MyQuick tmp_expim dir removed @var", array('@var' => $oct_db_dirx)), 'info');
      }

      if (!is_dir($oct_db_dirx)) {
        drush_log(dt("MyQuick generate_dump mysql.php create @var", array('@var' => $oct_db_dirx)), 'info');
        provision_file()->mkdir($oct_db_dirx)
          ->succeed('Created <code>@path</code>')
          ->fail('Could not create <code>@path</code>', 'DRUSH_PERM_ERROR');
      }

      $threads = provision_count_cpus();
      $threads = max(2, intval($threads / 4) + 1);
      drush_log(dt("MyQuick generate_dump mysql.php db_name @var", array('@var' => $db_name)), 'info');
      if (provision_file()->exists($myquick_creds_log)->status()) {
        drush_log(dt("MyQuick generate_dump mysql.php oct_db_user @var", array('@var' => $oct_db_user)), 'info');
        drush_log(dt("MyQuick generate_dump mysql.php oct_db_pass @var", array('@var' => $oct_db_pass)), 'info');
        drush_log(dt("MyQuick generate_dump mysql.php oct_db_host @var", array('@var' => $oct_db_host)), 'info');
        drush_log(dt("MyQuick generate_dump mysql.php oct_db_port @var", array('@var' => $oct_db_port)), 'info');
      }

      if (is_dir($oct_db_dirx) &&
        $db_name &&
        $oct_db_user &&
        $oct_db_pass &&
        $oct_db_host &&
        $oct_db_port) {
        // Any non-transactional table makes mydumper abort the whole
        // database unless --trx-tables=0 is passed; InnoDB-only keeps the
        // fast consistent-snapshot path. Mirrors mysql_backup.sh; a failed
        // count degrades to the InnoDB-only behaviour.
        $trx_opt = '';
        $non_trx_result = $this->query("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA = '%s' AND TABLE_TYPE = 'BASE TABLE' AND ENGINE IS NOT NULL AND ENGINE NOT IN ('InnoDB')", $db_name);
        if ($non_trx_result && ($non_trx_row = $non_trx_result->fetch()) && intval($non_trx_row[0]) > 0) {
          $trx_opt = ' --trx-tables=0';
        }
        // SECURITY: $db_name derives from alias context; $oct_db_* originate
        // in BOA root control files but may contain shell-special characters.
        // Escape every interpolated value. See DECISIONS.md Decision 002.
        $command = $mydumper_path
          . ' --database=' . escapeshellarg($db_name)
          . ' --host=' . escapeshellarg($oct_db_host)
          . ' --user=' . escapeshellarg($oct_db_user)
          . ' --password=' . escapeshellarg($oct_db_pass)
          . ' --port=' . escapeshellarg($oct_db_port)
          . ' --outputdir=' . escapeshellarg($oct_db_dirx)
          . ' --rows=50000 --build-empty-files --threads=' . escapeshellarg($threads)
          . ' --long-query-guard=900 --clear' . $trx_opt . ' --verbose=2';
        if (provision_file()->exists($myquick_creds_log)->status()) {
          drush_log(dt("MyQuick generate_dump mysql.php Cmd @var", array('@var' => $command)), 'info');
        }
        $success = drush_shell_exec($command);

        if ((!$success || !is_file($oct_db_dirx . '/metadata')) && !drush_get_option('force', FALSE)) {
          // Never interpolate $command into messages: it carries --password.
          // A run that exits 0 without the final metadata marker still left
          // no restorable dump (killed mid-flight), so treat it as failed.
          drush_set_error('PROVISION_BACKUP_FAILED', dt('Database dump failed: %output', array('%output' => join("\n", drush_shell_exec_output()))));
        }
      }
    }
    else {
      // Mixed copy-paste of drush_shell_exec and provision_shell_exec.
      $cmd = sprintf("mysqldump --defaults-file=/dev/fd/3 %s --no-tablespaces --no-autocommit --skip-add-locks --single-transaction --quick --hex-blob %s", $gtid_option, escapeshellcmd(drush_get_option('db_name')));

      // Fail if db file already exists.
      $dump_file = fopen(d()->site_path . '/database.sql', 'x');
      if ($dump_file === FALSE) {
        drush_set_error('PROVISION_BACKUP_FAILED', dt('Could not write database backup file mysqldump'));
      }
      else {
        $pipes = array();
        $descriptorspec = $this->generate_descriptorspec();
        $process = proc_open($cmd, $descriptorspec, $pipes);
        if (is_resource($process)) {
          fwrite($pipes[3], $this->generate_mycnf());
          fclose($pipes[3]);

          // At this point we have opened a pipe to that mysqldump command. Now
          // we want to read it one line at a time and do our replacements.
          while (($buffer = fgets($pipes[1], 4096)) !== FALSE) {
            $this->filter_line($buffer);
            // Write the resulting line in the backup file.
            if ($buffer) {
              // fwrite returns the byte count, and a SHORT write (disk full,
              // quota) returns a number smaller than the buffer rather than
              // FALSE -- which the old === FALSE test read as success and
              // then kept appending to, producing a silently truncated dump.
              $written = fwrite($dump_file, $buffer);
              if ($written === FALSE || $written < strlen($buffer)) {
                drush_set_error('PROVISION_BACKUP_FAILED', dt('Short or failed write to the database backup file (wrote %w of %n bytes) - the dump is truncated', array('%w' => ($written === FALSE ? 0 : $written), '%n' => strlen($buffer))));
                break;
              }
            }
          }
          // Close stdout.
          fclose($pipes[1]);
          // Catch errors returned by mysqldump.
          $err = fread($pipes[2], 4096);
          // Close stderr as well.
          fclose($pipes[2]);
          if (proc_close($process) != 0) {
            drush_set_error('PROVISION_BACKUP_FAILED', dt('Could not write database backup file mysqldump (command: %command) (error: %msg)', array('%msg' => $err, '%command' => $cmd)));
          }
        }
        else {
          drush_set_error('PROVISION_BACKUP_FAILED', dt('Could not run mysqldump for backups'));
        }
      }

      $dump_size_too_small = filesize(d()->site_path . '/database.sql') < 1024;
      if (($dump_size_too_small) && !drush_get_option('force', FALSE)) {
        drush_set_error('PROVISION_BACKUP_FAILED', dt('Could not generate database backup from mysqldump. (error: %msg)', array('%msg' => $err)));
      }
    }

    // Reset the umask to normal permissions.
    umask(0022);
  }

  /**
   * We go through all this trouble to hide the password from the commandline,
   * it's the most secure way (apart from writing a temporary file, which would
   * create conflicts in parallel runs)
   *
   * XXX: this needs to be refactored so it:
   *  - works even if /dev/fd/3 doesn't exist
   *  - has a meaningful name (we're talking about reading and writing
   * dumps here, really, or at least call mysql and mysqldump, not
   * just any command)
   *  - can be pushed upstream to drush (http://drupal.org/node/671906)
   */
  function safe_shell_exec($cmd, $db_host, $db_user, $db_passwd, $dump_file = NULL) {
    $mycnf = $this->generate_mycnf($db_host, $db_user, $db_passwd);
    $descriptorspec = $this->generate_descriptorspec($dump_file);
    $pipes = array();
    $process = proc_open($cmd, $descriptorspec, $pipes);
    $this->safe_shell_exec_output = '';
    if (is_resource($process)) {
      fwrite($pipes[3], $mycnf);
      fclose($pipes[3]);

      // Drain BOTH pipes concurrently. Reading stdout to EOF first deadlocks
      // as soon as the child writes more than one pipe buffer (64K on Linux)
      // to stderr: the child blocks writing stderr, we block reading stdout,
      // and neither side moves again. mysql --force on a bad dump emits one
      // ERROR line per statement, so filling stderr is the NORMAL failure
      // case here, not an exotic one.
      $this->safe_shell_exec_output = '';
      stream_set_blocking($pipes[1], 0);
      stream_set_blocking($pipes[2], 0);
      $open = array(1 => TRUE, 2 => TRUE);
      while ($open[1] || $open[2]) {
        $read = array();
        if ($open[1]) { $read[1] = $pipes[1]; }
        if ($open[2]) { $read[2] = $pipes[2]; }
        $write = NULL;
        $except = NULL;
        if (stream_select($read, $write, $except, 5) === FALSE) {
          break;
        }
        foreach ($read as $idx => $stream) {
          $chunk = fread($stream, 8192);
          if ($chunk === FALSE || $chunk === '') {
            if (feof($stream)) {
              $open[$idx] = FALSE;
            }
          }
          else {
            $this->safe_shell_exec_output .= $chunk;
          }
        }
      }
      // "It is important that you close any pipes before calling
      // proc_close in order to avoid a deadlock"
      fclose($pipes[1]);
      fclose($pipes[2]);
      $return_value = proc_close($process);
    }
    else {
      // XXX: failed to execute? unsure when this happens
      $return_value = -1;
    }
  return ($return_value == 0);
  }

  function utf8mb4_is_supported() {
    // Ensure that provision can connect to the database.
    if (!$this->connect()) {
      return FALSE;
    }

    // Ensure that the MySQL driver supports utf8mb4 encoding.
    $version = $this->conn->getAttribute(PDO::ATTR_CLIENT_VERSION);
    if (strpos($version, 'mysqlnd') !== FALSE) {
      // The mysqlnd driver supports utf8mb4 starting at version 5.0.9.
      $version = preg_replace('/^\D+([\d.]+).*/', '$1', $version);
      if (version_compare($version, '5.0.9', '<')) {
        return FALSE;
      }
    }
    else {
      // The libmysqlclient driver supports utf8mb4 starting at version 5.5.3.
      if (version_compare($version, '5.5.3', '<')) {
        return FALSE;
      }
    }

    // Ensure that the MySQL server supports large prefixes and utf8mb4.
    $dbname = uniqid(drush_get_option('aegir_db_prefix', 'site_'));
    $this->create_database($dbname);
    $success = $this->query("CREATE TABLE `%s`.`drupal_utf8mb4_test` (id VARCHAR(255), PRIMARY KEY(id(255))) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC", $dbname);
    if (!$this->drop_database($dbname)) {
      drush_log(dt("Failed to drop database @dbname", array('@dbname' => $dbname)), 'warning');
    }

    return ($success !== FALSE);
  }
}
