<?php
/**
 * @file
 * The provisioning framework API.
 *
 * API functions that are used by the provisioning framework to provide
 * structure to the provisioning modules.
 */

require_once __DIR__ . '/vendor/autoload.php';

drush_errors_on();

/**
 * Return an instance of the provision autoloader.
 *
 * This will instiatate an instance if it needs to.
 */
function provision_autoload() {
  static $instance = NULL;

  if (is_null($instance)) {
    $instance = new \Composer\Autoload\ClassLoader();
    // Activate the autoloader.
    $instance->register();
  }

  return $instance;
}

/**
 * Register a PECL style prefix with the provision autoloader.
 *
 * @param string $prefix
 *   The class prefix to register.
 * @param string $dir
 *   The directory to search for the classes in.
 * @param bool $prepend
 *   If the directory should be searched first for the classes in the given
 *   prefix, set this to TRUE, otherwise, the default, FALSE, is fine.
 */
function provision_autoload_register_prefix($prefix, $dir, $prepend = FALSE) {

  // Get any current directories set for this prefix.
  $current_prefixes = provision_autoload()->getPrefixes();
  if (isset($current_prefixes[$prefix])) {
    $dirs = $current_prefixes[$prefix];
  }
  else {
    $dirs = array();
  }

  // Now add the new one.
  if ($prepend) {
    array_unshift($dirs, $dir);
  }
  else {
    array_push($dirs, $dir);
  }

  // Set the prefixes.
  provision_autoload()->add($prefix, $dirs);
}

// Add our prefix to the autoloader.
provision_autoload_register_prefix('Provision_', dirname(__FILE__));

/**
 * Return the directory containing the file a class is defined in.
 *
 * @param string $class_name
 *   The class name to search for.
 *
 * @return string
 *   A directory if the class can be found or an empty string if not.
 */
function provision_class_directory($class_name) {
  return dirname(provision_class_file($class_name));
}

/**
 * Return the file a class is defined in.
 *
 * @param string $class_name
 *   The class name to search for.
 *
 * @return string
 *   A file if the class can be found or an empty string if not.
 */
function provision_class_file($class_name) {
  if (class_exists($class_name)) {
    $reflect = new reflectionClass($class_name);
    return $reflect->getFilename();
  }
  return '';
}

/**
 * Detect a Backdrop CMS platform/site root.
 *
 * Positive signal, matching BackdropBoot::valid_root() and bee's bee_find_root():
 * a root index.php + core/includes/bootstrap.inc + core/misc/backdrop.js, and NO
 * root autoload.php (which would mark a Composer-managed Drupal whose real docroot
 * is web/). Keyed on the codebase root, so it is FALSE for a server context (the
 * aegir root is not a Backdrop codebase) and FALSE for every Drupal tree -- which
 * makes every guard that calls it a no-op on non-Backdrop platforms (the drupal-
 * layer edits stay inert on the public merge path).
 *
 * @param $context
 *   A codebase root path (string), or a Provision context (site or platform)
 *   whose ->root is the platform docroot. Defaults to the active d(). A site
 *   context carries the platform root in ->root, so a single ->root test covers
 *   both platform and site contexts.
 * @return
 *   TRUE if the resolved root is a Backdrop root, FALSE otherwise (including for
 *   any non-string or empty root).
 */
function provision_platform_is_backdrop($context = NULL) {
  static $cache = array();
  if (is_null($context)) {
    $context = d();
  }
  $root = '';
  if (is_string($context)) {
    $root = $context;
  }
  elseif (is_object($context) && isset($context->root)) {
    $root = $context->root;
  }
  if (!is_string($root) || $root === '') {
    return FALSE;
  }
  if (!isset($cache[$root])) {
    $is_backdrop = FALSE;
    if (is_dir($root)
      && file_exists($root . '/index.php')
      && file_exists($root . '/core/includes/bootstrap.inc')
      && file_exists($root . '/core/misc/backdrop.js')
      && !file_exists($root . '/autoload.php')) {
      $is_backdrop = TRUE;
    }
    $cache[$root] = $is_backdrop;
  }
  return $cache[$root];
}

/**
 * Detect a Textpattern (TXP) platform/site root.
 *
 * Positive signal: root index.php + css.php + textpattern/lib/constants.php +
 * textpattern/index.php (the shared-core admin entry). Negative discriminators:
 * NO root core/ (Drupal 8+/Backdrop shape) and NO root autoload.php (Composer-
 * managed Drupal). Keyed on the codebase root exactly like
 * provision_platform_is_backdrop(): FALSE for server contexts and for every
 * Drupal/Backdrop tree, so every guard that calls it is a no-op on non-TXP
 * platforms and the shared-layer edits stay inert on the public merge path.
 *
 * @param $context
 *   A codebase root path (string), or a Provision context whose ->root is the
 *   platform docroot. Defaults to the active d().
 * @return
 *   TRUE if the resolved root is a Textpattern root, FALSE otherwise.
 */
function provision_platform_is_txp($context = NULL) {
  static $cache = array();
  if (is_null($context)) {
    $context = d();
  }
  $root = '';
  if (is_string($context)) {
    $root = $context;
  }
  elseif (is_object($context) && isset($context->root)) {
    $root = $context->root;
  }
  if (!is_string($root) || $root === '') {
    return FALSE;
  }
  if (!isset($cache[$root])) {
    $is_txp = FALSE;
    if (is_dir($root)
      && file_exists($root . '/index.php')
      && file_exists($root . '/css.php')
      && file_exists($root . '/textpattern/index.php')
      && file_exists($root . '/textpattern/lib/constants.php')
      && !is_dir($root . '/core')
      && !file_exists($root . '/autoload.php')) {
      $is_txp = TRUE;
    }
    $cache[$root] = $is_txp;
  }
  return $cache[$root];
}

/**
 * Detect a Grav 2 platform/site root (a capsule or the platform seed).
 *
 * Positive signal: root index.php + bin/grav + system/defines.php carrying
 * GRAV_VERSION (the file Grav documents as include-safe standalone; the version
 * is regex-readable with no CMS bootstrap). Negative discriminators: NO root
 * core/ (Drupal 8+/Backdrop shape). NB the Backdrop-style !autoload.php test
 * is deliberately absent -- a Grav root has no root autoload.php either, but
 * relying on that would couple the detectors; system/defines.php is already
 * distinctive (absent in every Drupal/Backdrop/TXP tree). Keyed on the
 * codebase root exactly like provision_platform_is_backdrop(): FALSE for
 * server contexts and every non-Grav tree, so every guard that calls it is a
 * no-op on non-Grav platforms and the shared-layer edits stay inert on the
 * public merge path. A site context's ->root is the PLATFORM root (the capsule
 * lives at sites/<uri>/ inside it), which is itself a Grav tree -- one ->root
 * test covers both context types.
 */
/**
 * The site's database credentials as nginx variables, for the shared
 * vhost include: locations there that declare their own fastcgi_param set
 * (which cancels the inheritance of the server level fastcgi_param lines)
 * read them back through these. urlencode()d exactly as the server level
 * lines are, so the cloaked settings.php decodes one source. The include
 * declares the variables empty and the vhost sets them after the include,
 * since set directives apply in their textual order.
 */
function provision_nginx_db_set_lines($db_type, $db_name, $db_user, $db_passwd, $db_host, $db_port) {
  $values = array(
    'db_type' => urlencode($db_type),
    'db_name' => urlencode($db_name),
    'db_user' => implode('@', array_map('urlencode', explode('@', $db_user))),
    'db_passwd' => urlencode($db_passwd),
    'db_host' => urlencode($db_host),
    'db_port' => urlencode($db_port),
  );
  $out = '';
  foreach ($values as $key => $value) {
    $out .= "  set \$boa_" . $key . " \"" . $value . "\";\n";
  }
  $out .= "  set \$boa_db_creds_urlencoded \"1\";\n";
  return $out;
}

function provision_platform_is_grav($context = NULL) {
  static $cache = array();
  if (is_null($context)) {
    $context = d();
  }
  $root = '';
  if (is_string($context)) {
    $root = $context;
  }
  elseif (is_object($context) && isset($context->root)) {
    $root = $context->root;
  }
  if (!is_string($root) || $root === '') {
    return FALSE;
  }
  if (!isset($cache[$root])) {
    $is_grav = FALSE;
    if (is_dir($root)
      && file_exists($root . '/index.php')
      && file_exists($root . '/bin/grav')
      && file_exists($root . '/system/defines.php')
      && !is_dir($root . '/core')) {
      $is_grav = TRUE;
    }
    $cache[$root] = $is_grav;
  }
  return $cache[$root];
}

/**
 * Union guard: is this a non-Drupal-lineage ("foreign") CMS root?
 *
 * The Drupal-layer hooks carry ONE added early-return on this union instead of
 * a per-CMS line at every seam, so each later CMS wires itself in HERE (a
 * single OR) with zero re-sweeps of the shared files. Backdrop is deliberately
 * NOT in the union: its per-seam guards shipped first and stay untouched (the
 * pure-addition rule for the public merge path).
 */
function provision_platform_is_foreign_cms($context = NULL) {
  return provision_platform_is_txp($context)
    || provision_platform_is_grav($context);
}

/**
 * @defgroup sitedata Site data management utility functions.
 * @{
 * The provision framework maintains a site.php file in the sites directory, to maintain additional
 * information from the front end, as well as providing a change history of setting changes.
 *
 * These functions load, save and manage changes made to the site data. This data has diagnostic and infrastructure
 * values, that allow sites to be more easily moved between different provisioned platforms.
 */

/**
 * Make a determination whether or not the given host is local or not.
 *
 * We needed to fork this from drush core to handle the case sensitivity in host names.
 *
 * @param host
 *   A hostname, 'localhost' or '127.0.0.1'.
 *
 * @return
 *   True if the host is local.
 */
function provision_is_local_host($host) {
  $host = strtolower($host);
  // In order for this to work right, you must use 'localhost' or '127.0.0.1'
  // or the machine returned by 'uname -n' for your 'remote-host' entry in
  // your site alias.  Note that sometimes 'uname -n' does not return the
  // correct value.  To fix it, put the correct hostname in /etc/hostname
  // and then run 'hostname -F /etc/hostname'.
  return ($host == 'localhost') ||
    ($host == '127.0.0.1') ||
    (gethostbyname($host) == '127.0.0.1') ||
    (gethostbyname($host) == '127.0.1.1') || // common setting on
                                             // ubuntu and friends
    ($host == strtolower(php_uname('n'))) ||
    ($host == provision_fqdn());
}

function genRandStrUnlock($length = 12) {
  return substr(str_shuffle(str_repeat($x='0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ', ceil($length/strlen($x)) )),1,$length);
}

/**
 * The codebase's psr/log package directory, resolved the way the lock and
 * unlock bodies resolve every vendor path: <root>/vendor first, then the
 * composer layout that keeps vendor/ above the docroot. '' when there is
 * none.
 */
function _provision_psr_log_dir($root) {
  return _provision_first_existing(array(
    $root . '/vendor/psr/log',
    $root . '/../vendor/psr/log',
  ));
}

/**
 * Whether a psr/log package directory carries the de-typed Aegir overlay.
 *
 * Read from the interface itself, never from a marker file: the overlay is
 * Drush 8's psr/log v1 copied over src/, whose log() takes an untyped
 * $message, while the psr/log v2/v3 a Drupal 10+ codebase ships declares
 * string|\Stringable and ': void'. The old test looked for the overlay's
 * Test/TestLogger.php instead, a file the copy may not deliver and any
 * tests-stripping pass removes, and both the lock and the unlock then read
 * the wrong state and skipped the package, silently.
 *
 * @return bool|null
 *   TRUE = de-typed (overlay in place), FALSE = typed (genuine package),
 *   NULL = no readable interface file.
 */
function _provision_psr_log_detyped($log_dir) {
  $file = $log_dir . '/src/LoggerInterface.php';
  if (!is_file($file) || !is_readable($file)) {
    return NULL;
  }
  $content = file_get_contents($file);
  if ($content === FALSE || strpos($content, 'function log(') === FALSE) {
    return NULL;
  }
  return (strpos($content, 'Stringable') === FALSE);
}

/**
 * Put the genuine psr/log back into a Drupal 10+ codebase, in place.
 *
 * Every step is judged by content and written to the task log. The pristine
 * copy the lock keeps at vendor/psr/._orig_log is copied file by file over
 * the overlaid src/, never renamed: the former restore moved the whole
 * package directory to a stash and moved the copy back, and when the first
 * move failed (a stash parent that was not there, a vendor/psr the backend
 * user cannot write) the second one buried the pristine copy INSIDE the
 * de-typed directory, every later unlock skipped the package, and the
 * site-local Drush died on Consolidation\Log\Logger::log() at every install
 * with nothing in the task log (omega8cc/boa#1943). A copy buried that way
 * is recognised and used. With no usable pristine copy composer reinstalls
 * the package. Whatever the route, the interface is read again afterwards:
 * a codebase still de-typed is reported and the caller must not run the
 * site-local Drush against it. The account's Drush 8 template plays no part
 * here, so the restore runs on every unlock.
 *
 * @return bool
 *   TRUE when the codebase carries the genuine psr/log now.
 */
function _provision_psr_log_restore($root) {
  $log = _provision_psr_log_dir($root);
  if ($log === '') {
    return TRUE;
  }
  $who = provision_current_user();
  $state = _provision_psr_log_detyped($log);
  if ($state === FALSE) {
    drush_log(dt('PSR/LOG genuine psr/log already in place in @dir', array('@dir' => $log)), 'info');
    return TRUE;
  }
  if ($state === NULL) {
    // No readable interface (an interrupted copy, a missing file): cannot
    // tell, so put the genuine package back; the read-back below decides.
    drush_log(dt('PSR/LOG cannot read @file to tell the Aegir overlay from the genuine psr/log; restoring the genuine package anyway', array('@file' => $log . '/src/LoggerInterface.php')), 'advisory');
  }
  $orig = dirname($log) . '/._orig_log';
  $nested = $log . '/._orig_log';
  $source = '';
  if (is_dir($orig) && _provision_psr_log_detyped($orig) === FALSE) {
    $source = $orig;
  }
  elseif (is_dir($nested) && _provision_psr_log_detyped($nested) === FALSE) {
    // Buried inside the package by an earlier unlock whose stash move failed.
    // Back beside the package when its parent lets the backend write (a
    // stale copy already there gives way); used where it is otherwise, and
    // left there as the only pristine copy.
    if (is_dir($orig)) {
      _provision_recursive_delete($orig);
    }
    if (!is_dir($orig) && drush_shell_exec('mv -f %s %s', $nested, $orig) && is_dir($orig)) {
      drush_log(dt('PSR/LOG the pristine copy was buried at @nested by an earlier unlock; moved back to @orig', array('@nested' => $nested, '@orig' => $orig)), 'advisory');
      $source = $orig;
    }
    else {
      drush_log(dt('PSR/LOG the pristine copy was buried at @nested by an earlier unlock and could not be moved back beside the package (is @parent writable by @user?); using it from there', array('@nested' => $nested, '@parent' => dirname($log), '@user' => $who)), 'advisory');
      $source = $nested;
    }
  }
  if ($source !== '') {
    drush_shell_exec('cp -af %s/. %s/', $source, $log);
    if (is_dir($log . '/src/Test')) {
      _provision_recursive_delete($log . '/src/Test');
    }
    if (_provision_psr_log_detyped($log) === FALSE) {
      drush_log(dt('PSR/LOG genuine psr/log restored in @dir from @src', array('@dir' => $log, '@src' => $source)), 'success');
      return TRUE;
    }
    drush_log(dt('PSR/LOG copying @src over @dir left it de-typed; reinstalling the package with composer', array('@src' => $source, '@dir' => $log)), 'warning');
  }
  else {
    drush_log(dt('PSR/LOG no pristine copy at @orig; reinstalling the package with composer', array('@orig' => $orig)), 'advisory');
  }
  $approot = findAppRoot($root);
  $output = '';
  if (!empty($approot) && is_file($approot . '/composer.json')) {
    drush_shell_cd_and_exec($approot, 'composer reinstall psr/log --no-interaction --no-progress');
    $output = trim(implode("\n", drush_shell_exec_output()));
  }
  drush_log(dt('PSR/LOG composer reinstall psr/log in @root: @out', array(
    '@root' => empty($approot) ? $root : $approot,
    '@out' => ($output === '') ? 'no output (composer not found for ' . $who . ', or no composer.json)' : $output,
  )), 'notice');
  if (_provision_psr_log_detyped($log) === FALSE) {
    drush_log(dt('PSR/LOG genuine psr/log reinstalled in @dir by composer', array('@dir' => $log)), 'success');
    return TRUE;
  }
  drush_log(dt('PSR/LOG @dir still carries the de-typed Aegir psr/log overlay (or no readable LoggerInterface.php), so no site-local Drush can start against it (Consolidation\Log\Logger::log() does not match the untyped Psr\Log\LoggerInterface::log()). Pristine copy at @orig: @orig_state. Make @parent writable by @user, or run "composer reinstall psr/log" in @root as @user, then run Verify + Lock Drush on the platform and retry.', array(
    '@dir' => $log,
    '@orig' => $orig,
    '@orig_state' => is_dir($orig) ? (_provision_psr_log_detyped($orig) === FALSE ? 'present' : 'present but de-typed') : 'absent',
    '@parent' => dirname($log),
    '@user' => $who,
    '@root' => empty($approot) ? $root : $approot,
  )), 'warning');
  return FALSE;
}

/**
 * Lay the Aegir psr/log overlay over a Drupal 10+ codebase, in place.
 *
 * The pristine package stays beside it at vendor/psr/._orig_log for the
 * unlock. It is taken from the codebase only while the codebase carries the
 * genuine (typed) package, and refreshed then whenever it is missing, not
 * itself genuine, or a different package version (a tenant's composer
 * update moves psr/log on between locks); a fresh copy is written beside
 * the old one and swapped in only once it reads genuine, so a parent the
 * backend cannot write never costs the copy it already has. The overlay is
 * judged by content, the same way the unlock judges it, and read back;
 * nothing here renames the package directory.
 *
 * @return bool
 *   TRUE when the codebase carries the overlay now.
 */
function _provision_psr_log_overlay($root, $psr_tpl) {
  $log = _provision_psr_log_dir($root);
  if ($log === '') {
    return TRUE;
  }
  $who = provision_current_user();
  $state = _provision_psr_log_detyped($log);
  $orig = dirname($log) . '/._orig_log';
  $nested = $log . '/._orig_log';
  // A pristine copy buried inside the package by an earlier unlock goes back
  // beside it when the parent allows; a second copy is removed.
  if (is_dir($nested)) {
    if (!is_dir($orig)) {
      drush_shell_exec('mv -f %s %s', $nested, $orig);
    }
    if (is_dir($nested) && is_dir($orig) && _provision_psr_log_detyped($orig) === FALSE) {
      _provision_recursive_delete($nested);
    }
  }
  if ($state === TRUE) {
    drush_log(dt('PSR/LOG Aegir overlay already in place in @dir', array('@dir' => $log)), 'info');
    return TRUE;
  }
  if ($state === FALSE) {
    $refresh = !is_dir($orig) || _provision_psr_log_detyped($orig) !== FALSE
      || @md5_file($orig . '/composer.json') !== @md5_file($log . '/composer.json');
    if ($refresh) {
      $fresh = $orig . '.new';
      if (is_dir($fresh)) {
        _provision_recursive_delete($fresh);
      }
      drush_shell_exec('cp -a %s %s', $log, $fresh);
      if (is_dir($fresh . '/._orig_log')) {
        _provision_recursive_delete($fresh . '/._orig_log');
      }
      $swapped = FALSE;
      if (is_dir($fresh) && _provision_psr_log_detyped($fresh) === FALSE) {
        if (is_dir($orig)) {
          _provision_recursive_delete($orig);
        }
        $swapped = !is_dir($orig) && drush_shell_exec('mv -f %s %s', $fresh, $orig) && is_dir($orig);
      }
      if (is_dir($fresh)) {
        _provision_recursive_delete($fresh);
      }
      if (!$swapped) {
        // A copy that still reads genuine, beside the package or buried
        // inside it, serves the next unlock; only its absence is a warning.
        if (is_dir($orig) && _provision_psr_log_detyped($orig) === FALSE) {
          $kept = 'keeping the copy already there';
        }
        elseif (is_dir($nested) && _provision_psr_log_detyped($nested) === FALSE) {
          $kept = 'keeping the copy inside the package';
        }
        else {
          $kept = '';
        }
        drush_log(dt('PSR/LOG could not keep a fresh pristine copy of @dir at @orig (is @parent writable by @user?); @kept', array('@dir' => $log, '@orig' => $orig, '@parent' => dirname($log), '@user' => $who, '@kept' => ($kept === '') ? 'the next unlock will reinstall the package with composer' : $kept)), ($kept === '') ? 'warning' : 'advisory');
      }
    }
  }
  else {
    // No readable interface: copying the overlay in is the repair; no copy
    // is taken from a tree that cannot be read.
    drush_log(dt('PSR/LOG cannot read @file to tell the Aegir overlay from the genuine psr/log; applying the overlay anyway', array('@file' => $log . '/src/LoggerInterface.php')), 'advisory');
  }
  drush_shell_exec('cp -af %s/. %s/src/', $psr_tpl, $log);
  if (_provision_psr_log_detyped($log) === TRUE) {
    drush_log(dt('PSR/LOG Aegir overlay applied in @dir (pristine copy at @orig)', array('@dir' => $log, '@orig' => $orig)), 'success');
    return TRUE;
  }
  drush_log(dt('PSR/LOG the overlay copy from @tpl into @dir did not take (is @src writable by @user?); Aegir Drush 8 cannot bootstrap this codebase', array('@tpl' => $psr_tpl, '@dir' => $log, '@src' => $log . '/src', '@user' => $who)), 'warning');
  return FALSE;
}

/**
 * Custom helper function findAppRoot
 */
function findAppRoot($startDir) {
  $dir = realpath($startDir);
  while ($dir !== '/' && !is_dir($dir . '/vendor')) {
    $dir = dirname($dir);
  }
  return is_dir($dir . '/vendor') ? $dir : null;
}

/**
 * Check codebase status for core/console patches
 */
function provision_check_codebase_status() {

  // Add platform root auto-discovery to avoid confusing
  // Composer based D8+ codebase root with Drupal real root.
  //d()->root = provision_auto_fix_platform_root(d()->root);
  d()->appRoot = findAppRoot(d()->root);

  if (is_dir(d()->appRoot)) {
    d()->symfonyDir = d()->appRoot . '/vendor/symfony/';
  }

  // Initialise patch-state flags before the symfonyDir gate so callers (the
  // idempotency predicate, the verify auto-lock guard) never read an unset
  // property on a not-yet-built or pre-D10 platform.
  d()->isCorePatched = FALSE;
  d()->isConsolePatched = FALSE;
  d()->isValidatorPatched = FALSE;

  if (is_dir(d()->symfonyDir)) {

    drush_log(dt('CHECK/CODE/APP =@appRoot= FOR @webRoot', array('@appRoot' => d()->appRoot, '@webRoot' => d()->root)), 'info');
    drush_log(dt('CHECK/CODE/VENDOR =@symfonyDir= FOR @webRoot', array('@symfonyDir' => d()->symfonyDir, '@webRoot' => d()->root)), 'info');
    drush_log(dt('CHECK/CODE/VALIDATOR =@appRoot= FOR @webRoot', array('@appRoot' => d()->appRoot, '@webRoot' => d()->root)), 'info');

    d()->isCorePatched = FALSE;
    $core_file_path = d()->root . '/core/lib/Drupal/Core/Logger/RfcLoggerTrait.php';
    $core_search_string = ': void';
    $core_file_content = file_get_contents($core_file_path);
    if (strpos($core_file_content, $core_search_string) == false) {
      d()->isCorePatched = TRUE;
    }
    drush_log(dt('CHECK/CODE/CORE =@is_core_patched= FOR @webRoot', array('@is_core_patched' => d()->isCorePatched, '@webRoot' => d()->root)), 'info');

    d()->isConsolePatched = FALSE;
    $console_file_path = d()->symfonyDir . 'console/Output/Output.php';
    if (drush_drupal_major_version() > 10) {
      $console_search_string = ': void;';
    }
    else {
      $console_search_string = 'doWrite(string';
    }
    $console_file_content = file_get_contents($console_file_path);
    if (strpos($console_file_content, $console_search_string) == false) {
      d()->isConsolePatched = TRUE;
    }
    drush_log(dt('CHECK/CODE/CONSOLE =@is_console_patched= FOR @webRoot', array('@is_console_patched' => d()->isConsolePatched, '@webRoot' => d()->root)), 'info');

    d()->isValidatorPatched = FALSE;
    $validator_file_path = d()->root . '/core/modules/package_manager/src/Validator/DiskSpaceValidator.php';
    $validator_search_string = 'assume plenty of space';
    $validator_file_content = file_get_contents($validator_file_path);
    if (strpos($validator_file_content, $validator_search_string) == true) {
      d()->isValidatorPatched = TRUE;
    }
    drush_log(dt('CHECK/CODE/VALIDATOR =@is_validator_patched= FOR @valFile', array('@is_validator_patched' => d()->isValidatorPatched, '@valFile' => $validator_file_path)), 'info');

    $psr_dir = _provision_psr_log_dir(d()->root);
    $psr_state = 'none';
    if ($psr_dir !== '') {
      $psr_detyped = _provision_psr_log_detyped($psr_dir);
      if ($psr_detyped === NULL) {
        $psr_state = 'unreadable';
      }
      elseif ($psr_detyped) {
        $psr_state = 'overlay';
      }
      else {
        $psr_state = 'typed';
      }
    }
    drush_log(dt('CHECK/CODE/PSRLOG =@state= FOR @dir', array('@state' => $psr_state, '@dir' => ($psr_dir === '') ? d()->root : $psr_dir)), 'info');
  }
  else {
    drush_log(dt('CHECK/CODE/VERSION =@coreVersion= FOR @webRoot', array('@coreVersion' => drush_drupal_major_version(), '@webRoot' => d()->root)), 'info');
  }
}

/**
 * Return the first existing path from a candidate list, or '' if none exist.
 *
 * Used to resolve the site-local-vs-above vendor layout the same way the
 * lock/unlock bodies do (d()->root/vendor/... vs d()->root/../vendor/...).
 */
function _provision_first_existing($paths) {
  foreach ($paths as $p) {
    if (file_exists($p)) {
      return $p;
    }
  }
  return '';
}

/**
 * Derive the site-local Drush lock state purely from on-disk reality.
 *
 * No marker/pid files: state is read from the patch sentinels (set by
 * provision_check_codebase_status, which MUST have run first) and the actual
 * permission bits the lock (0400) / unlock (0775) apply to vendor/drush and the
 * Symfony console Input/Style dirs. Patch-state is authoritative where patch
 * templates exist (D10/D11); for pre-D10 and D12+ the lock is the chmod alone,
 * so perm-bits are the only signal. Any patch-vs-perm disagreement (the
 * interrupted-run signature) returns 'partial' so the caller runs the body,
 * whose per-step guards converge only the still-needed work.
 *
 * @return string  'locked' | 'unlocked' | 'partial' | 'unknown'
 */
function provision_drush_local_lock_state() {
  $drush = _provision_first_existing(array(
    d()->root . '/vendor/drush',
    d()->root . '/../vendor/drush',
  ));
  $inp = _provision_first_existing(array(
    d()->root . '/vendor/symfony/console/Input',
    d()->root . '/../vendor/symfony/console/Input',
  ));
  $sty = _provision_first_existing(array(
    d()->root . '/vendor/symfony/console/Style',
    d()->root . '/../vendor/symfony/console/Style',
  ));

  // Cannot resolve the lock targets -> unknown; the caller runs the body whose
  // exists() guards no-op safely.
  if (empty($drush) || empty($inp) || empty($sty)) {
    return 'unknown';
  }

  $bits_locked = ((fileperms($drush) & 0777) === 0400)
    && ((fileperms($inp) & 0777) === 0400)
    && ((fileperms($sty) & 0777) === 0400);
  $bits_unlocked = ((fileperms($drush) & 0777) === 0775)
    && ((fileperms($inp) & 0777) === 0775)
    && ((fileperms($sty) & 0777) === 0775);

  $version = drush_drupal_major_version();

  // The psr/log term (Drupal 10+): the de-typed Aegir overlay belongs to the
  // locked state, the genuine typed package to the unlocked one, read from
  // the interface file itself. A codebase without the package, or one whose
  // interface cannot be read, carries no signal; a disagreement with the
  // patch flags or the bits is the partial state whose body converges it.
  $psr_detyped = NULL;
  if ($version >= 10) {
    $psr_dir = _provision_psr_log_dir(d()->root);
    if ($psr_dir !== '') {
      $psr_detyped = _provision_psr_log_detyped($psr_dir);
    }
  }

  if ($version == 10 || $version == 11) {
    // Built-state gate: trust the patch flags only on a demonstrably built tree.
    if (!is_dir(d()->symfonyDir)) {
      return 'unknown';
    }
    if (d()->isCorePatched && d()->isConsolePatched && $bits_locked && $psr_detyped !== FALSE) {
      return 'locked';
    }
    if (!d()->isCorePatched && !d()->isConsolePatched && $bits_unlocked && $psr_detyped !== TRUE) {
      return 'unlocked';
    }
    return 'partial';
  }

  // Pre-D10 and D12+: no patch templates; the lock is the chmod alone.
  if ($bits_locked && $psr_detyped !== FALSE) {
    return 'locked';
  }
  if ($bits_unlocked && $psr_detyped !== TRUE) {
    return 'unlocked';
  }
  return 'partial';
}

/**
 * Bring the site-local Drush into a platform that ships without one.
 *
 * Runs composer require drush/drush in the codebase root, but only after
 * composer's own check of the lock's platform requirements against THIS PHP
 * passes. Measured 2026-09-17 on Opigno LMS 3.2.7 and Open Social 13.1.0
 * platforms (no vendor/drush in the tarball) verified on an instance whose
 * PHP CLI is 8.5 while the platforms' locks cap PHP at 8.4: composer-patches
 * first deleted every patched package "so that it can be re-installed and
 * re-patched", then the resolve failed on the PHP requirement and composer
 * reverted composer.json and composer.lock only -- calendar and h5p
 * (Opigno) and simple_oauth (Open Social) were gone from modules/contrib,
 * and every Opigno site install died on "Required modules not found". A
 * resolve the lock already rules out must therefore never start; that is
 * what check-platform-reqs decides, with no operation and nothing removed.
 * The plugins stay ON for the require itself: a require that succeeds with
 * them off makes composer forget where composer/installers placed every
 * drupal-* package, re-install those into vendor/ and point the autoloader
 * there while the docroot keeps the old trees -- worse than the failure.
 * A codebase that carries composer patches never gets the on-box require at
 * all: measured on the same Opigno platform under a PHP the lock accepts, a
 * SUCCESSFUL require re-installed the patched calendar and h5p packages and
 * composer-patches then logged "Could not apply patch! Skipping" for every
 * one of their patches -- the platform kept running on unpatched modules.
 * The build box applies those patches; a box never can. Both skips are
 * logged as advisories (the condition is permanent for that platform and
 * must not turn every later task into a WARNING); a require that still
 * fails is a warning carrying composer's own message.
 */
function _provision_require_site_local_drush($approot) {
  if (_provision_codebase_has_composer_patches($approot)) {
    drush_log(dt('DRUSH/DRUSH skipped composer require drush/drush in @root: the codebase carries composer patches, and a resolve on this box would re-install the patched packages without their patches; it keeps no site-local Drush until it is built with one', array(
      '@root' => $approot,
    )), 'advisory');
    return FALSE;
  }
  $check = drush_shell_cd_and_exec($approot, 'composer check-platform-reqs --no-plugins --no-interaction');
  $check_output = implode('', drush_shell_exec_output());
  if (!$check) {
    drush_log(dt('DRUSH/DRUSH skipped composer require drush/drush in @root: the codebase lock does not accept this PHP, so no resolve was started and the codebase is left as it was; it keeps no site-local Drush until it is built with one: @out', array(
      '@root' => $approot,
      '@out' => $check_output,
    )), 'advisory');
    return FALSE;
  }
  $result = drush_shell_cd_and_exec($approot, 'composer require drush/drush --no-interaction --no-progress');
  $output = implode('', drush_shell_exec_output());
  if ($result) {
    drush_log("DRUSH/DRUSH " . $output);
  }
  else {
    drush_log(dt('DRUSH/DRUSH composer require drush/drush failed in @root; it keeps no site-local Drush: @out', array(
      '@root' => $approot,
      '@out' => $output,
    )), 'warning');
  }
  return $result;
}

/**
 * Does this codebase carry composer patches?
 *
 * TRUE when composer.json declares extra.patches or extra.patches-file, or
 * the lock installs cweagans/composer-patches. Read from disk, no composer
 * run: this decides whether composer may run on the box at all.
 */
function _provision_codebase_has_composer_patches($approot) {
  $approot = rtrim($approot, '/');
  $json_path = $approot . '/composer.json';
  if (is_readable($json_path)) {
    $json = json_decode(file_get_contents($json_path), TRUE);
    if (is_array($json) && isset($json['extra']) && is_array($json['extra'])) {
      if (!empty($json['extra']['patches']) || !empty($json['extra']['patches-file'])) {
        return TRUE;
      }
    }
  }
  $lock_path = $approot . '/composer.lock';
  if (is_readable($lock_path)) {
    $lock = json_decode(file_get_contents($lock_path), TRUE);
    if (is_array($lock)) {
      foreach (array('packages', 'packages-dev') as $key) {
        if (!empty($lock[$key]) && is_array($lock[$key])) {
          foreach ($lock[$key] as $package) {
            if (isset($package['name']) && $package['name'] === 'cweagans/composer-patches') {
              return TRUE;
            }
          }
        }
      }
    }
  }
  return FALSE;
}

/**
 * Record, or ask, whether this backend process opened the lock on $root.
 *
 * A lock that closes a window the same task opened (a deploy's updatedb, a
 * Drupal 11 site install) leaves every other site's service container as it
 * was: only the task's own site ran site-local Drush in the window, and the
 * task rebuilds that one last. A lock that closes a window an EARLIER task
 * opened -- the Unlock Local Drush task, after which the account's own
 * site-local Drush may have compiled any site's container with its Drush-only
 * services (logger.drupaltodrush, whose class the lock makes unreadable) --
 * has no such guarantee, so provision_lock_drush_local_vnd() then rebuilds
 * every Drupal 10+ site of the platform with core's own rebuild.
 *
 * The unlock functions record a root only when they find the lock closed, so
 * an unlock that runs over an owner's open window leaves it foreign, and the
 * lock clears the record once it has read it, so a later window in the same
 * process is judged on its own. In-process only, by design: the window a task
 * opens ends with that task.
 *
 * @param string $root
 *   The platform root the lock works on.
 * @param bool $mark
 *   TRUE to record $root.
 * @param bool $clear
 *   TRUE to drop the record for $root (after it is read).
 *
 * @return bool
 *   Whether this process opened the lock on $root (before any clear).
 */
function _provision_drush_local_unlocked_here($root, $mark = FALSE, $clear = FALSE) {
  static $roots = array();
  $real = realpath($root);
  $key = ($real !== FALSE) ? $real : rtrim($root, '/');
  if ($mark) {
    $roots[$key] = TRUE;
  }
  $here = isset($roots[$key]);
  if ($clear) {
    unset($roots[$key]);
  }
  return $here;
}

/**
 * The last container write after a lock closed a foreign window.
 *
 * Runs core's own rebuild of every Drupal 10+ site of the platform once the
 * lock really holds: vendor/drush back at 0400, in the state the web now
 * serves. A lock whose chmod did not take leaves the local Drush readable, so
 * nothing it compiled can fail yet; the next lock that closes the window runs
 * the rebuild instead (and a Verify locks on that partial state every time, so
 * rebuilding here would repeat the platform-wide run on each of them).
 *
 * @param bool $closes_foreign_window
 *   What provision_lock_drush_local_vnd() read before changing any mode.
 */
function _provision_drush_local_relock_rebuild($closes_foreign_window) {
  if (!$closes_foreign_window || drush_drupal_major_version() < 10) {
    return;
  }
  if (_provision_drush_local_lock_is_open(d()->root)) {
    drush_log(dt('REBUILD/RELOCK/SKIP the lock left vendor/drush open in @root, so nothing site-local Drush compiled is unreadable yet; the lock that closes it rebuilds the sites', array('@root' => d()->root)), 'warning');
    return;
  }
  if (function_exists('_provision_drupal_rebuild_platform_sites')) {
    _provision_drupal_rebuild_platform_sites(d()->root);
  }
  else {
    drush_log(dt('REBUILD/RELOCK/UNAVAILABLE the lock closed an earlier Unlock window in @root, but the platform rebuild is not loaded; each site keeps its container until its next Verify', array('@root' => d()->root)), 'warning');
  }
}

/**
 * Whether the site-local Drush lock on $root is open right now.
 *
 * Read from the vendor/drush mode alone, before a lock changes it: anything
 * but the lock's 0400 means site-local Drush could run. No vendor/drush at all
 * means it could not, so nothing it compiles can be left behind.
 */
function _provision_drush_local_lock_is_open($root) {
  $drush = _provision_first_existing(array(
    $root . '/vendor/drush',
    $root . '/../vendor/drush',
  ));
  if ($drush === '') {
    return FALSE;
  }
  return ((fileperms($drush) & 0777) !== 0400);
}

/**
 * Unlock Site-Local Drush And Symfony/Console etc
 * in the Aegir tasks like updatedb.
 */
function provision_unlock_drush_local_vnd() {

  if (provision_file()->exists(d()->root)->status() &&
    !preg_match("/\/aegir\/distro\//i", d()->root) &&
    !preg_match("/\/var\/aegir\//i", d()->root)) {

    // Only a window this task opens counts as its own; one an earlier task
    // left open stays foreign however many unlocks run over it.
    if (!_provision_drush_local_lock_is_open(d()->root)) {
      _provision_drush_local_unlocked_here(d()->root, TRUE);
    }

    $dru_vnd_drush_local = d()->root . '/vendor/drush';
    $dru_vnd_drush_above = d()->root . '/../vendor/drush';
    $dru_vnd_symf_con_inp_loc = d()->root . '/vendor/symfony/console/Input';
    $dru_vnd_symf_con_inp_abo = d()->root . '/../vendor/symfony/console/Input';
    $dru_vnd_symf_con_sty_loc = d()->root . '/vendor/symfony/console/Style';
    $dru_vnd_symf_con_sty_abo = d()->root . '/../vendor/symfony/console/Style';

    $bin_vnd_drush_local = d()->root . '/vendor/bin/drush';
    $bin_vnd_drush_above = d()->root . '/../vendor/bin/drush';
    $dru_dru_dru_php_local = d()->root . '/vendor/drush/drush/drush.php';
    $dru_dru_dru_php_above = d()->root . '/../vendor/drush/drush/drush.php';
    $dru_dru_drush_local = d()->root . '/vendor/drush/drush/drush';
    $dru_dru_drush_above = d()->root . '/../vendor/drush/drush/drush';

    if (provision_file()->exists($dru_vnd_drush_local)->status()) {
      provision_file()->chmod($dru_vnd_drush_local, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_drush_above)->status()) {
      provision_file()->chmod($dru_vnd_drush_above, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($dru_vnd_symf_con_inp_loc)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_inp_loc, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_inp_abo)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_inp_abo, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($dru_vnd_symf_con_sty_loc)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_sty_loc, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_sty_abo)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_sty_abo, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($bin_vnd_drush_local)->status()) {
      provision_file()->unlink($bin_vnd_drush_local)
      ->succeed('Removed Problematic vendor/bin/drush in provision_unlock_drush_local_vnd')
      ->fail('Could not remove problematic vendor/bin/drush in provision_unlock_drush_local_vnd');
    }
    if (provision_file()->exists($bin_vnd_drush_above)->status()) {
      provision_file()->unlink($bin_vnd_drush_above)
      ->succeed('Removed Problematic vendor/bin/drush in provision_unlock_drush_local_vnd')
      ->fail('Could not remove problematic vendor/bin/drush in provision_unlock_drush_local_vnd');
    }

    if (provision_file()->exists($dru_dru_dru_php_local)->status()) {
      provision_file()->chmod($dru_dru_dru_php_local, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_dru_dru_php_above)->status()) {
      provision_file()->chmod($dru_dru_dru_php_above, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($dru_dru_drush_local)->status()) {
      provision_file()->chmod($dru_dru_drush_local, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_dru_drush_above)->status()) {
      provision_file()->chmod($dru_dru_drush_above, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($dru_vnd_symf_con_inp_loc)->status()) {
      if (!provision_file()->exists($dru_dru_dru_php_local)->status()) {
        _provision_require_site_local_drush(d()->root);
      }
    }
    elseif (provision_file()->exists($dru_vnd_symf_con_inp_abo)->status()) {
      if (!provision_file()->exists($dru_dru_dru_php_above)->status()) {
        _provision_require_site_local_drush(d()->root . "/../");
      }
    }

    if (drush_drupal_major_version() >= 10) {

      $this_user = provision_current_user();

      // The genuine psr/log first, in place and judged by content: the
      // site-local Drush this window exists for cannot start against the
      // de-typed overlay. Unconditional: the restore reads the pristine copy
      // beside the package (or reinstalls it), never the account's Drush 8
      // template the lock copies from. When it fails the patches stay on,
      // the codebase is reported and the caller must not run that Drush
      // (see _provision_psr_log_restore()).
      if (!_provision_psr_log_restore(d()->root)) {
        return FALSE;
      }

      // D10 has no validator patch, but the name is still concatenated into the
      // template paths below; define it so a D10 pass does not log an
      // undefined-variable notice (the apply sites are gated on == 11 first).
      $validator_patch_fname = '';
      if (drush_drupal_major_version() == 10) {
        $core_patch_fname = "drupal-ten-aegir-core-01.patch";
        $console_patch_fname = "drupal-ten-aegir-console-02.patch";
        $core_patch_leftover = "drupal-ten-aegir-01.patch";
      }
      elseif (drush_drupal_major_version() == 11) {
        $core_patch_fname = "drupal-eleven-aegir-core-01.patch";
        $console_patch_fname = "drupal-eleven-aegir-console-02.patch";
        $validator_patch_fname = "drupal-eleven-aegir-validator-03.patch";
        $first_eleven_test = "core/modules/language/tests/language_test/language_test.module";
        $patchCoreTest = d()->root . '/' . $first_eleven_test;
        if (provision_file()->exists($patchCoreTest)->status()) {
          $core_patch_fname = "drupal-ten-aegir-core-01.patch";
        }
      }

      if ($this_user == 'aegir' ) {
        $patchCoreTpl = '/var/aegir/patches/' . $core_patch_fname;
        $patchConsoleTpl = '/var/aegir/patches/' . $console_patch_fname;
        $patchValidatorTpl = '/var/aegir/patches/' . $validator_patch_fname;
      }
      else {
        $patchCoreTpl = '/data/conf/patches/' . $core_patch_fname;
        $patchConsoleTpl = '/data/conf/patches/' . $console_patch_fname;
        $patchValidatorTpl = '/data/conf/patches/' . $validator_patch_fname;
      }

      $patchCoreFile = d()->root . '/' . $core_patch_fname;
      $patchConsoleFile = d()->root . '/' . $console_patch_fname;
      $patchValidatorFile = d()->root . '/' . $validator_patch_fname;
      $patchLeftoverFile = d()->root . '/' . $core_patch_leftover;

      if (provision_file()->exists($patchLeftoverFile)->status()) {
        provision_file()->unlink($patchLeftoverFile);
      }

      provision_check_codebase_status();

      $commandCore = sprintf('patch -d ' . d()->root . ' -R -p1 < ' . $patchCoreTpl);
      $commandConsole = sprintf('patch -d ' . d()->symfonyDir . ' -R -p1 < ' . $patchConsoleTpl);
      $commandValidator = sprintf('patch -d ' . d()->root . ' -R -p1 < ' . $patchValidatorTpl);
      $commandCoreDry = sprintf('patch --dry-run -d ' . d()->root . ' -R -p1 < ' . $patchCoreTpl);
      $commandConsoleDry = sprintf('patch --dry-run -d ' . d()->symfonyDir . ' -R -p1 < ' . $patchConsoleTpl);
      $commandValidatorDry = sprintf('patch --dry-run -d ' . d()->root . ' -R -p1 < ' . $patchValidatorTpl);

      drush_log(dt('CORE/PATCH/IF4 --@isCorePatched-- in @webRoot', array('@isCorePatched' => d()->isCorePatched, '@webRoot' => d()->root)), 'info');
      drush_log(dt('CONSOLE/PATCH/IF4 --@isConsolePatched-- in @symfonyDir', array('@isConsolePatched' => d()->isConsolePatched, '@symfonyDir' => d()->symfonyDir)), 'info');
      drush_log(dt('VALIDATOR/PATCH/IF4 --@isValidatorPatched-- in @webRoot', array('@isValidatorPatched' => d()->isValidatorPatched, '@webRoot' => d()->root)), 'info');

      if (isset($core_patch_fname) && provision_file()->exists($patchCoreTpl)->status() && d()->isCorePatched && !drush_shell_exec($commandCoreDry)) {
        drush_set_error('PROVISION_CORE_PATCH_REVERT_DRYRUN_FAILED', dt('Core patch would not revert cleanly in @webRoot; skipping to avoid a partial revert.', array('@webRoot' => d()->root)));
      }
      elseif (isset($core_patch_fname) && provision_file()->exists($patchCoreTpl)->status() && d()->isCorePatched) {
        drush_log(dt('CORE/PATCH/REVERT @patchCoreTpl in @webRoot', array('@patchCoreTpl' => $patchCoreTpl, '@webRoot' => d()->root)), 'info');
        $success = drush_shell_exec($commandCore);
        $result = drush_shell_exec_output();
        foreach ($result as $index => $line) {
          if (!$success) { // Log the entire error as a warning.
            $line_status = 'warning';
          }
          else { // Only log the last line as success.
            $line_status = $index+1 == count($result) ? 'success' : 'notice';
          }
          drush_log($line, $line_status);
        }
      }

      if (isset($console_patch_fname) && provision_file()->exists($patchConsoleTpl)->status() && d()->isConsolePatched && !drush_shell_exec($commandConsoleDry)) {
        drush_set_error('PROVISION_CONSOLE_PATCH_REVERT_DRYRUN_FAILED', dt('Console patch would not revert cleanly in @symfonyDir; skipping to avoid a partial revert.', array('@symfonyDir' => d()->symfonyDir)));
      }
      elseif (isset($console_patch_fname) && provision_file()->exists($patchConsoleTpl)->status() && d()->isConsolePatched) {
        drush_log(dt('CONSOLE/PATCH/REVERT @patchConsoleTpl in @symfonyDir', array('@patchConsoleTpl' => $patchConsoleTpl, '@symfonyDir' => d()->symfonyDir)), 'info');
        $success = drush_shell_exec($commandConsole);
        $result = drush_shell_exec_output();
        foreach ($result as $index => $line) {
          if (!$success) { // Log the entire error as a warning.
              $line_status = 'warning';
          }
          else { // Only log the last line as success.
              $line_status = $index+1 == count($result) ? 'success' : 'notice';
          }
          drush_log($line, $line_status);
        }
      }

      if (drush_drupal_major_version() == 11 && isset($validator_patch_fname) && provision_file()->exists($patchValidatorTpl)->status() && d()->isValidatorPatched && !drush_shell_exec($commandValidatorDry)) {
        drush_set_error('PROVISION_VALIDATOR_PATCH_REVERT_DRYRUN_FAILED', dt('Validator patch would not revert cleanly in @webRoot; skipping to avoid a partial revert.', array('@webRoot' => d()->root)));
      }
      elseif (drush_drupal_major_version() == 11 && isset($validator_patch_fname) && provision_file()->exists($patchValidatorTpl)->status() && d()->isValidatorPatched) {
        drush_log(dt('VALIDATOR/PATCH/REVERT @patchValidatorTpl in @webRoot', array('@patchValidatorTpl' => $patchValidatorTpl, '@webRoot' => d()->root)), 'info');
        $success = drush_shell_exec($commandValidator);
        $result = drush_shell_exec_output();
        foreach ($result as $index => $line) {
          if (!$success) { // Log the entire error as a warning.
              $line_status = 'warning';
          }
          else { // Only log the last line as success.
              $line_status = $index+1 == count($result) ? 'success' : 'notice';
          }
          drush_log($line, $line_status);
        }
      }
    }
  }
  else {
    drush_log(dt('Site-Local Drush Unlock skipped in provision_unlock_drush_local_vnd because unexpected type has been detected @type', array('@type' => d()->type)), 'info');
  }
  return TRUE;
}

/**
 * Unlock Site-Local Drush And Symfony/Console etc
 * in the Aegir post-task procedure.
 */
function provision_unlock_some_vnd() {

  if (provision_file()->exists(d()->root)->status() &&
    !preg_match("/\/aegir\/distro\//i", d()->root) &&
    !preg_match("/\/var\/aegir\//i", d()->root)) {

    drush_log(dt('Drupal root reported in provision_unlock_some_vnd: !vendor', array('!vendor' => d()->root)), 'message');
    // Only a window this task opens counts as its own; one an earlier task
    // left open stays foreign however many unlocks run over it.
    if (!_provision_drush_local_lock_is_open(d()->root)) {
      _provision_drush_local_unlocked_here(d()->root, TRUE);
    }

    // Fix ownership first
    $success = drush_shell_exec("sudo --non-interactive /usr/local/bin/fix-drupal-platform-ownership.sh --root=%s --script-user=%s --web-group=%s", d()->root, d()->server->script_user, d()->server->web_group);

    $dru_vnd_drush_local = d()->root . '/vendor/drush';
    $dru_vnd_drush_above = d()->root . '/../vendor/drush';
    $dru_vnd_symf_con_inp_loc = d()->root . '/vendor/symfony/console/Input';
    $dru_vnd_symf_con_inp_abo = d()->root . '/../vendor/symfony/console/Input';
    $dru_vnd_symf_con_sty_loc = d()->root . '/vendor/symfony/console/Style';
    $dru_vnd_symf_con_sty_abo = d()->root . '/../vendor/symfony/console/Style';

    if (provision_file()->exists($dru_vnd_drush_local)->status()) {
      provision_file()->chmod($dru_vnd_drush_local, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_drush_above)->status()) {
      provision_file()->chmod($dru_vnd_drush_above, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($dru_vnd_symf_con_inp_loc)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_inp_loc, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_inp_abo)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_inp_abo, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($dru_vnd_symf_con_sty_loc)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_sty_loc, 0700)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
      _provision_recursive_delete($dru_vnd_symf_con_sty_loc);
    }
    if (provision_file()->exists($dru_vnd_symf_con_sty_abo)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_sty_abo, 0700)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
      _provision_recursive_delete($dru_vnd_symf_con_sty_abo);
    }
  }
}

/**
 * Unlock Site-Local Drush Only
 * in the Aegir post-task procedure.
 */
function provision_unlock_drush_only() {

  if (provision_file()->exists(d()->root)->status() &&
    !preg_match("/\/aegir\/distro\//i", d()->root) &&
    !preg_match("/\/var\/aegir\//i", d()->root)) {

    drush_log(dt('Drupal root reported in provision_unlock_drush_only: !vendor', array('!vendor' => d()->root)), 'message');
    // Only a window this task opens counts as its own; one an earlier task
    // left open stays foreign however many unlocks run over it.
    if (!_provision_drush_local_lock_is_open(d()->root)) {
      _provision_drush_local_unlocked_here(d()->root, TRUE);
    }

    // Fix ownership first
    $success = drush_shell_exec("sudo --non-interactive /usr/local/bin/fix-drupal-platform-ownership.sh --root=%s --script-user=%s --web-group=%s", d()->root, d()->server->script_user, d()->server->web_group);

    $dru_vnd_drush_local = d()->root . '/vendor/drush';
    $dru_vnd_drush_above = d()->root . '/../vendor/drush';

    if (provision_file()->exists($dru_vnd_drush_local)->status()) {
      provision_file()->chmod($dru_vnd_drush_local, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_drush_above)->status()) {
      provision_file()->chmod($dru_vnd_drush_above, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
  }
}

/**
 * Lock Site-Local Drush And Symfony/Console etc
 * to avoid conflicts with Aegir own drush version.
 */
function provision_lock_drush_local_vnd() {

  if (provision_file()->exists(d()->root)->status() &&
    !preg_match("/\/aegir\/distro\//i", d()->root) &&
    !preg_match("/\/var\/aegir\//i", d()->root)) {

    drush_log(dt('Drupal root reported in provision_lock_drush_local_vnd: !vendor', array('!vendor' => d()->root)), 'message');

    // Read before anything below changes the modes: is this lock closing a
    // window an earlier task left open? See _provision_drush_local_unlocked_here().
    $closes_foreign_window = _provision_drush_local_lock_is_open(d()->root)
      && !_provision_drush_local_unlocked_here(d()->root, FALSE, TRUE);

    // Fix ownership first
    $success = drush_shell_exec("sudo --non-interactive /usr/local/bin/fix-drupal-platform-ownership.sh --root=%s --script-user=%s --web-group=%s", d()->root, d()->server->script_user, d()->server->web_group);

    $dru_vnd_drush_local = d()->root . '/vendor/drush';
    $dru_vnd_drush_above = d()->root . '/../vendor/drush';
    $dru_vnd_symf_con_inp_loc = d()->root . '/vendor/symfony/console/Input';
    $dru_vnd_symf_con_inp_abo = d()->root . '/../vendor/symfony/console/Input';
    $dru_vnd_symf_con_sty_loc = d()->root . '/vendor/symfony/console/Style';
    $dru_vnd_symf_con_sty_abo = d()->root . '/../vendor/symfony/console/Style';

    $dru_dru_dru_php_local = d()->root . '/vendor/drush/drush/drush.php';
    $dru_dru_dru_php_above = d()->root . '/../vendor/drush/drush/drush.php';

    if (provision_file()->exists($dru_vnd_symf_con_inp_loc)->status()) {
      if (!provision_file()->exists($dru_dru_dru_php_local)->status()) {
        _provision_require_site_local_drush(d()->root);
      }
    }
    elseif (provision_file()->exists($dru_vnd_symf_con_inp_abo)->status()) {
      if (!provision_file()->exists($dru_dru_dru_php_above)->status()) {
        _provision_require_site_local_drush(d()->root . "/../");
      }
    }

    if (provision_file()->exists($dru_vnd_drush_local)->status()) {
      provision_file()->chmod($dru_vnd_drush_local, 0400)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_drush_above)->status()) {
      provision_file()->chmod($dru_vnd_drush_above, 0400)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($dru_vnd_symf_con_inp_loc)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_inp_loc, 0400)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_inp_abo)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_inp_abo, 0400)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($dru_vnd_symf_con_sty_loc)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_sty_loc, 0400)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_sty_abo)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_sty_abo, 0400)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (drush_drupal_major_version() >= 10) {

      $this_user = provision_current_user();

      if ($this_user == 'aegir' ) {
        $psr_tpl = '/var/aegir/drush/vendor/psr/log/Psr/Log';
      }
      else {
        $psr_tpl = '/data/disk/' . $this_user . '/tools/drush/vendor/psr/log/Psr/Log';
      }

      // The overlay in place, judged by content, with the pristine copy kept
      // beside the package for the unlock (see _provision_psr_log_overlay()).
      // Without it the patches must not go on: de-typed core over a typed
      // psr/log is a codebase the web cannot serve, the stock one still is.
      if (provision_file()->exists($psr_tpl)->status()) {
        if (!_provision_psr_log_overlay(d()->root, $psr_tpl)) {
          // vendor/drush is already at 0400 here: the sites still need their
          // rebuild, in the stock state this aborted lock leaves.
          _provision_drush_local_relock_rebuild($closes_foreign_window);
          return drush_set_error('PROVISION_PSR_LOG_OVERLAY_FAILED', dt('Lock aborted before the Aegir patches: the psr/log overlay could not be applied in @root (see the PSR/LOG lines above); the codebase keeps its stock files', array('@root' => d()->root)));
        }
      }

      // D10 has no validator patch, but the name is still concatenated into the
      // template paths below; define it so a D10 pass does not log an
      // undefined-variable notice (the apply sites are gated on == 11 first).
      $validator_patch_fname = '';
      if (drush_drupal_major_version() == 10) {
        $core_patch_fname = "drupal-ten-aegir-core-01.patch";
        $console_patch_fname = "drupal-ten-aegir-console-02.patch";
        $core_patch_leftover = "drupal-ten-aegir-01.patch";
      }
      elseif (drush_drupal_major_version() == 11) {
        $core_patch_fname = "drupal-eleven-aegir-core-01.patch";
        $console_patch_fname = "drupal-eleven-aegir-console-02.patch";
        $validator_patch_fname = "drupal-eleven-aegir-validator-03.patch";
        $first_eleven_test = "core/modules/language/tests/language_test/language_test.module";
        $patchCoreTest = d()->root . '/' . $first_eleven_test;
        if (provision_file()->exists($patchCoreTest)->status()) {
          $core_patch_fname = "drupal-ten-aegir-core-01.patch";
        }
      }

      if ($this_user == 'aegir' ) {
        $patchCoreTpl = '/var/aegir/patches/' . $core_patch_fname;
        $patchConsoleTpl = '/var/aegir/patches/' . $console_patch_fname;
        $patchValidatorTpl = '/var/aegir/patches/' . $validator_patch_fname;
      }
      else {
        $patchCoreTpl = '/data/conf/patches/' . $core_patch_fname;
        $patchConsoleTpl = '/data/conf/patches/' . $console_patch_fname;
        $patchValidatorTpl = '/data/conf/patches/' . $validator_patch_fname;
      }

      $patchCoreFile = d()->root . '/' . $core_patch_fname;
      $patchConsoleFile = d()->root . '/' . $console_patch_fname;
      $patchValidatorFile = d()->root . '/' . $validator_patch_fname;
      $patchLeftoverFile = d()->root . '/' . $core_patch_leftover;

      if (provision_file()->exists($patchLeftoverFile)->status()) {
        provision_file()->unlink($patchLeftoverFile);
      }

      provision_check_codebase_status();

      $commandCore = sprintf('patch -d ' . d()->root . ' -p1 < ' . $patchCoreTpl);
      $commandConsole = sprintf('patch -d ' . d()->symfonyDir . ' -p1 < ' . $patchConsoleTpl);
      $commandValidator = sprintf('patch -d ' . d()->root . ' -p1 < ' . $patchValidatorTpl);
      $commandCoreDry = sprintf('patch --dry-run -d ' . d()->root . ' -p1 < ' . $patchCoreTpl);
      $commandConsoleDry = sprintf('patch --dry-run -d ' . d()->symfonyDir . ' -p1 < ' . $patchConsoleTpl);
      $commandValidatorDry = sprintf('patch --dry-run -d ' . d()->root . ' -p1 < ' . $patchValidatorTpl);

      // Contributed *.drush.inc files stay in place. The BOA Drush 8 extension
      // filter denies them for backend identities at discovery time, so a
      // command file the backend cannot load cannot break a task either.

      drush_log(dt('CORE/PATCH/IF1 --@isCorePatched-- in @webRoot', array('@isCorePatched' => d()->isCorePatched, '@webRoot' => d()->root)), 'info');
      drush_log(dt('CONSOLE/PATCH/IF1 --@isConsolePatched-- in @symfonyDir', array('@isConsolePatched' => d()->isConsolePatched, '@symfonyDir' => d()->symfonyDir)), 'info');
      drush_log(dt('VALIDATOR/PATCH/IF1 --@isValidatorPatched-- in @webRoot', array('@isValidatorPatched' => d()->isValidatorPatched, '@webRoot' => d()->root)), 'info');

      if (isset($core_patch_fname) && provision_file()->exists($patchCoreTpl)->status() && !d()->isCorePatched && !drush_shell_exec($commandCoreDry)) {
        drush_set_error('PROVISION_CORE_PATCH_DRYRUN_FAILED', dt('Core patch would not apply cleanly in @webRoot; skipping to avoid a partial patch.', array('@webRoot' => d()->root)));
      }
      elseif (isset($core_patch_fname) && provision_file()->exists($patchCoreTpl)->status() && !d()->isCorePatched) {
        drush_log(dt('CORE/PATCH/APPLY @patchCoreTpl in @webRoot', array('@patchCoreTpl' => $patchCoreTpl, '@webRoot' => d()->root)), 'info');
        $success = drush_shell_exec($commandCore);
        $result = drush_shell_exec_output();
        foreach ($result as $index => $line) {
          if (!$success) { // Log the entire error as a warning.
            $line_status = 'warning';
          }
          else { // Only log the last line as success.
            $line_status = $index+1 == count($result) ? 'success' : 'notice';
          }
          drush_log($line, $line_status);
        }
      }
      if (provision_file()->exists($patchCoreFile)->status()) {
        provision_file()->unlink($patchCoreFile);
      }

      if (isset($console_patch_fname) && provision_file()->exists($patchConsoleTpl)->status() && !d()->isConsolePatched && !drush_shell_exec($commandConsoleDry)) {
        drush_set_error('PROVISION_CONSOLE_PATCH_DRYRUN_FAILED', dt('Console patch would not apply cleanly in @symfonyDir; skipping to avoid a partial patch.', array('@symfonyDir' => d()->symfonyDir)));
      }
      elseif (isset($console_patch_fname) && provision_file()->exists($patchConsoleTpl)->status() && !d()->isConsolePatched) {
        drush_log(dt('CONSOLE/PATCH/APPLY @patchConsoleTpl in @symfonyDir', array('@patchConsoleTpl' => $patchConsoleTpl, '@symfonyDir' => d()->symfonyDir)), 'info');
        $success = drush_shell_exec($commandConsole);
        $result = drush_shell_exec_output();
        foreach ($result as $index => $line) {
          if (!$success) { // Log the entire error as a warning.
            $line_status = 'warning';
          }
          else { // Only log the last line as success.
            $line_status = $index+1 == count($result) ? 'success' : 'notice';
          }
          drush_log($line, $line_status);
        }
      }
      if (provision_file()->exists($patchConsoleFile)->status()) {
        provision_file()->unlink($patchConsoleFile);
      }

      if (drush_drupal_major_version() == 11 && isset($validator_patch_fname) && provision_file()->exists($patchValidatorTpl)->status() && !d()->isValidatorPatched && !drush_shell_exec($commandValidatorDry)) {
        drush_set_error('PROVISION_VALIDATOR_PATCH_DRYRUN_FAILED', dt('Validator patch would not apply cleanly in @webRoot; skipping to avoid a partial patch.', array('@webRoot' => d()->root)));
      }
      elseif (drush_drupal_major_version() == 11 && isset($validator_patch_fname) && provision_file()->exists($patchValidatorTpl)->status() && !d()->isValidatorPatched) {
        drush_log(dt('VALIDATOR/PATCH/APPLY @patchValidatorTpl in @webRoot', array('@patchValidatorTpl' => $patchValidatorTpl, '@webRoot' => d()->root)), 'info');
        $success = drush_shell_exec($commandValidator);
        $result = drush_shell_exec_output();
        foreach ($result as $index => $line) {
          if (!$success) { // Log the entire error as a warning.
              $line_status = 'warning';
          }
          else { // Only log the last line as success.
              $line_status = $index+1 == count($result) ? 'success' : 'notice';
          }
          drush_log($line, $line_status);
        }
      }
    }

    // The last container write after a window an earlier task opened: every
    // Drupal 10+ site of the platform gets core's own rebuild, in the patched,
    // locked state the web now serves.
    _provision_drush_local_relock_rebuild($closes_foreign_window);
  }
  return TRUE;
}

/**
 * Add platform root auto-discovery to avoid confusing
 * Composer based D8+ codebase root with Drupal real root.
 */
function provision_auto_fix_platform_root($isroot) {

  $test_root_index = $isroot . '/index.php';
  $test_root_docroot = $isroot . '/docroot/index.php';
  $test_root_html = $isroot . '/html/index.php';
  $test_root_web = $isroot . '/web/index.php';

  if (!provision_file()->exists($test_root_index)->status()) {
    drush_log(dt("Platform path needs correction, running auto-discovery.."), 'notice');
    if (provision_file()->exists($test_root_docroot)->status()) {
      drush_log(dt("Platform real root index: @realroot", array('@realroot' => $test_root_docroot)), 'debug');
      $isroot = $isroot . '/docroot';
    }
    elseif (provision_file()->exists($test_root_html)->status()) {
      drush_log(dt("Platform real root index: @realroot", array('@realroot' => $test_root_html)), 'debug');
      $isroot = $isroot . '/html';
    }
    elseif (provision_file()->exists($test_root_web)->status()) {
      drush_log(dt("Platform real root index: @realroot", array('@realroot' => $test_root_web)), 'debug');
      $isroot = $isroot . '/web';
    }
  }
  return $isroot;
}

/**
 * Determine if the currently acting context is the server's hostmaster site.
 *
 * @return
 *   Boolean TRUE if the current drush context is "@hostmaster".
 */
function provision_is_hostmaster_site() {
  // If the current root and URI matches "@hostmaster" root and URI, this is hostmaster.
  return d()->root == d('@hostmaster')->root && d()->uri == d('@hostmaster')->uri;
}

/**
 * return the FQDN of the machine or provided host
 *
 * this replicates hostname -f, which is not portable
 */
function provision_fqdn($host = NULL) {
  if (is_null($host)) {
    $host = php_uname('n');
  }
  return strtolower(gethostbyaddr(gethostbyname($host)));
}

/**
 * Retrieve a base_url for the currently active site.
 *
 * TODO: when we actually support HTTPS, do this correctly.
 */
function provision_get_base_url() {
  $base_url = 'http://' . d()->uri;

  $http_port = d()->server->http_port;
  if (!is_null($http_port) && ($http_port != 80)) {
    $base_url .= ':' . $http_port;
  }
  return $base_url;
}

/**
 * Save modified options to the drushrc.php file
 */
function provision_save_server_data() {
  if (!drush_get_error()) {
    $config = new Provision_Config_Drushrc_Server(d()->name);
    $config->write();
  }
}


function provision_save_site_data() {
  if (!drush_get_error()) {
    $config = new Provision_Config_Drushrc_Site(d()->name);
    $config->write();
    provision_drupal_push_site();
  }
}


/**
 * Save modified options to the drushrc.php file
 */
function provision_save_platform_data() {
  if (!drush_get_error()) {
    $config = new Provision_Config_Drushrc_Platform(d()->name);
    $config->write();
    provision_drupal_push_site();
  }
}


/**
 * @} End of "defgroup sitedata".
 */

/**
 * Remove files or directories, recursively
 *
 * This was taken from Drupal 7's file.inc, with slight modifications:
 *  * carry error codes along the way (returns TRUE only if all operations return TRUE)
 *  * remove any type of files encountered (not files and directories)
 *  * do not follow symlink directories
 *
 * @see file_unmanaged_delete_recursive()
 */
function _provision_recursive_delete($path) {
  $ret = 1;
  // is_dir() follows symlinks, so it can return true on a symlink
  if (is_dir($path) && !is_link($path)) {
    $d = dir($path);
    if (!empty($d)) {
      while (($entry = $d->read()) !== FALSE) {
        if ($entry == '.' || $entry == '..') {
          continue;
        }
        $entry_path = $path . '/' . $entry;
        $ret &= _provision_recursive_delete($entry_path);
      }
      $d->close();
    }

    $rm = provision_file()->rmdir($path)
      ->fail('Deleting @path directory failed.')
      ->status();

    $ret = $ret && $rm;
  }
  else {
    $rm = provision_file()->unlink($path)
      ->fail('Deleting @path file failed.')
      ->status();
    $ret = $ret && $rm;
  }
  return $ret;
}

/**
 * Convenience copy of Drupal 6's file_check_location()
 *
 * Check if a file is really located inside $directory. Should be used to make
 * sure a file specified is really located within the directory to prevent
 * exploits.
 *
 * @code
 *   // Returns FALSE:
 *   file_check_location('/www/example.com/files/../../../etc/passwd', '/www/example.com/files');
 * @endcode
 *
 * @param $source A string set to the file to check.
 * @param $directory A string where the file should be located.
 * @return 0 for invalid path or the real path of the source.
 *
 * @see file_check_location()
 */
function _provision_file_check_location($source, $directory = '') {
  $check = realpath($source);
  if ($check) {
    $source = $check;
  }
  else {
    // This file does not yet exist
    $source = realpath(dirname($source)) . '/' . basename($source);
  }
  $directory = realpath($directory);
  if ($directory && strpos($source, $directory) !== 0) {
    return 0;
  }
  return $source;
}


/**
 * Find the username of the current running procses
 *
 * This will return the username of the current running user (as seen
 * from posix_geteuid()) and should be used instead of
 * get_current_user() (which looks at the file owner instead).
 *
 * @see get_current_user()
 * @see posix_geteuid()
 *
 * @return
 *   String. The username.
 */
function provision_current_user() {
  return provision_posix_username(posix_geteuid());
}

/**
 * Check whether a user is a member of a group.
 *
 * @param user
 *   username or user id of user.
 * @param group
 *   groupname or group id of group.
 *
 * @return
 *   Boolean. True if user does belong to group,
 *   and FALSE if the user does not belong to the group, or either the user or group do not exist.
 */
function provision_user_in_group($user, $group) {
  // TODO: make these singletons with static variables for caching.
  $user = provision_posix_username($user);
  $group = provision_posix_groupname($group);
  if ($user && $group) {
    $info = posix_getgrnam($group);
    if (in_array($user, $info['members'])) {
      return TRUE;
    }
    // posix_getgrnam() lists supplementary members only: a user whose PRIMARY
    // group this is never appears there. On BOA an Octopus account carrying
    // its per-instance group has that group as the primary group of oN and
    // oN.ftp, so a chgrp to it would be refused as "not in group" and
    // silently skipped. Compare the primary gid as well.
    $pw = posix_getpwnam($user);
    if (is_array($pw) && isset($pw['gid']) && isset($info['gid']) && $pw['gid'] == $info['gid']) {
      return TRUE;
    }
  }
  return FALSE;
}

/**
 * Return the valid system username for $user.
 *
 * @return
 *   Returns the username if found, otherwise returns FALSE
 */
function provision_posix_username($user) {
  // TODO: make these singletons with static variables for caching.
  // we do this both ways, so that the function returns NULL if no such user was found.
  if (is_numeric($user)) {
    $info = posix_getpwuid($user);
    $user = $info['name'];
  }
  else {
    $info = posix_getpwnam($user);
    $user = $info['name'];
  }
  return $user;
}

/**
 * Return the valid system groupname for $group.
 *
 * @return
 *   Returns the groupname if found, otherwise returns FALSE
 */
function provision_posix_groupname($group) {
  // TODO: make these singletons with static variables for caching.
  // we do this both ways, so that the function returns NULL if no such user was found.
  if (is_numeric($group)) {
    $info = posix_getgrgid($group);
    $group = isset($info['name']) ? $info['name'] : FALSE;
  }
  else {
    $info = posix_getgrnam($group);
    $group = isset($info['name']) ? $info['name'] : FALSE;
  }
  return $group;
}

/**
 * Generate a random alphanumeric password.
 *
 * This is a copy of Drupal core's user_password() function. We keep it
 * here in case we need this and don't have a bootstrapped Drupal
 * around.
 *
 * @see user_password()
 */
function provision_password($length = 10) {
  // This variable contains the list of allowable characters for the
  // password. Note that the number 0 and the letter 'O' have been
  // removed to avoid confusion between the two. The same is true
  // of 'I', 1, and 'l'.
  $allowable_characters = 'abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789';

  // Zero-based count of characters in the allowable list:
  $len = strlen($allowable_characters) - 1;

  // Declare the password as a blank string.
  $pass = '';

  // Loop the number of times specified by $length.
  for ($i = 0; $i < $length; $i++) {

    // Each iteration, pick a random character from the
    // allowable string and append it to the password:
    $pass .= $allowable_characters[mt_rand(0, $len)];
  }

  return $pass;
}

/**
 * This is a helper function which changes deeply nested objects into arrays
 *
 * This helps get past the face that objects are not simple to work with, or
 * save in context files.
 *
 * This function 'misuses' a side effect of the json_decode function's second
 * parameter. As this is done in C, and the structures we are manipulating
 * aren't that large, it should be performant enough.
 */
function _scrub_object($input) {
  return json_decode(json_encode($input), TRUE);
}

/**
 * Execute a command against a specific context object.
 *
 * @param $target
 *   the context to operate on, @ prefix is optional.
 * @param $command
 *   drush command passed to drush_invoke_process().
 * @param $arguments
 *   drush arguments passed to drush_invoke_process().
 * @param $data
 *   drush data passed to drush_invoke_process().
 * @param $mode
 *   drush IPC mode (GET/POST) passed to drush_invoke_process().
 *
 * @see drush_invoke_process()
 */
function provision_backend_invoke($target, $command, $arguments = array(), $data = array(), $mode = 'GET') {
  $context = '@' . ltrim($target, '@');
  return drush_invoke_process($context, $command, $arguments, $data, array('method' => $mode, 'integrate' => TRUE, 'dispatch-using-alias' => TRUE));
}

/**
 * Validate a revert-artifact leftover directory name.
 *
 * Strict allowlist: a lowercase domain-shaped basename carrying the .restore
 * or .original revert-artifact suffix. No slashes, no dot-dot, no leading dot
 * -- the name is later joined to the platform root by the leftover commands,
 * so this is the traversal guard as much as the artifact-pattern gate. The
 * frontend mirrors this pattern when recording manageable leftovers; keep the
 * two in sync.
 *
 * @param string $name
 *   The candidate directory basename.
 *
 * @return bool
 *   TRUE when the name is a well-formed revert-artifact leftover name.
 */
function _provision_leftover_valid_name($name) {
  if (!is_string($name) || $name === '' || strlen($name) > 200) {
    return FALSE;
  }
  if (strpos($name, '..') !== FALSE) {
    return FALSE;
  }
  return (bool) preg_match('/^[a-z0-9][a-z0-9.-]*\.(restore|original)$/', $name);
}

/**
 * Parse the db credentials out of a site-dir settings.php without including it.
 *
 * Regex-parse (never include -- the file runs code) targeting the literal-creds
 * branch every provision settings template emits when BOA uncloaks credentials:
 * the D7+ $databases array and the Backdrop scalar $database array both carry
 * 'database' => "<name>" / 'host' => "<host>" lines. The cloaked $_SERVER
 * variant carries no literal name at all, and cloaking is now the default, so
 * the sibling drushrc.php -- which holds the same values as plain $options
 * literals -- is parsed as a second source whenever settings.php yields
 * nothing. Parsed the same way, never included. Both callers (the expose-time
 * record and the purge-time re-parse) go through this one function, so the
 * two-source agreement the purge gate depends on is preserved.
 *
 * @param string $dir
 *   Absolute path of the site directory holding settings.php.
 *
 * @return array
 *   array('db_name' => string, 'db_host' => string) -- empty strings when the
 *   file is missing or the pattern is not found.
 */
function _provision_leftover_parse_settings($dir) {
  $result = array('db_name' => '', 'db_host' => '');
  $settings = $dir . '/settings.php';
  if (!is_file($settings) || !is_readable($settings)) {
    return $result;
  }
  $body = file_get_contents($settings);
  if ($body === FALSE) {
    return $result;
  }
  if (preg_match('/[\'"]database[\'"]\s*=>\s*["\']([A-Za-z0-9_]+)["\']/', $body, $m)) {
    $result['db_name'] = $m[1];
  }
  if (preg_match('/[\'"]host[\'"]\s*=>\s*["\']([A-Za-z0-9._-]+)["\']/', $body, $m)) {
    $result['db_host'] = $m[1];
  }
  if ($result['db_name'] !== '') {
    return $result;
  }
  // Cloaked settings.php holds no literal credentials -- they arrive as
  // fastcgi_param -- and cloaking is the default, so without this arm the
  // orphan copy database of every leftover would be left on the server
  // forever. The sibling drushrc.php carries the same values as plain
  // $options literals (var_export'd, always single-quoted). Parsed with the
  // same charsets as above and NEVER included: it is Drush configuration and
  // must not execute here.
  $drushrc = $dir . '/drushrc.php';
  if (!is_file($drushrc) || !is_readable($drushrc)) {
    return $result;
  }
  $rc_body = file_get_contents($drushrc);
  if ($rc_body === FALSE) {
    return $result;
  }
  if (preg_match('/\$options\s*\[\s*[\'"]db_name[\'"]\s*\]\s*=\s*[\'"]([A-Za-z0-9_]+)[\'"]\s*;/', $rc_body, $m)) {
    $result['db_name'] = $m[1];
  }
  if ($result['db_host'] === ''
    && preg_match('/\$options\s*\[\s*[\'"]db_host[\'"]\s*\]\s*=\s*[\'"]([A-Za-z0-9._-]+)[\'"]\s*;/', $rc_body, $m)) {
    $result['db_host'] = $m[1];
  }
  return $result;
}

/**
 * Per-CMS leftover credential resolvers (foreign-CMS leftovers carry no
 * settings.php, so the Drupal parse yields nothing and a later purge would
 * orphan the database). Each CMS adds one arm here; keep 5.6-parse-safe.
 */
function _provision_leftover_parse_foreign_cms($dir) {
  $result = array('db_name' => '', 'db_host' => '');
  // Textpattern: sites/<name>/private/config.php ($txpcfg scalar keys).
  $txp_config = $dir . '/private/config.php';
  if (is_file($txp_config) && is_readable($txp_config)) {
    $body = file_get_contents($txp_config);
    if ($body !== FALSE) {
      if (preg_match('/\$txpcfg\s*\[\s*[\'"]db[\'"]\s*\]\s*=\s*[\'"]([A-Za-z0-9_]+)[\'"]/', $body, $m)) {
        $result['db_name'] = $m[1];
      }
      if (preg_match('/\$txpcfg\s*\[\s*[\'"]host[\'"]\s*\]\s*=\s*[\'"]([A-Za-z0-9._:-]+)[\'"]/', $body, $m)) {
        // TXP stores host:port in one key; every consumer of db_host here
        // compares against bare hostnames (localhost / 127.0.0.1 / the
        // server's remote_host), so a ':port' suffix would fail every match
        // and the purge would leave the orphan database in place -- defeating
        // the whole point of this resolver on non-3306 (ProxySQL) topologies.
        $host = $m[1];
        $colon = strrpos($host, ':');
        if ($colon !== FALSE) {
          $host = substr($host, 0, $colon);
        }
        $result['db_host'] = $host;
      }
      return $result;
    }
  }
  // (grav arm lands here in the grav track's unit.)
  return $result;
}

/**
 * Run a command, sending output to drush logs in real time.
 *
 * The Symfony\Component\Process\Process Object is used to run this command.
 * After implementing provision_process(), you can get the Process result object
 * via drush context:
 *
 *   $process = drush_get_context('provision_process_result');
 *   print $process->getExitCode();
 *
 * @param string|array $command
 *   The command to run
 * @param null $cwd
 *   The directory to run the command in.
 * @param string $label
 *   A string to display above the command block in the front-end.
 * @param array $env
 *   A list of environment variables to set for the process.
 * @param bool $log_output
 *   Whether or not to send output to drush_log in real time.
 * @param null $error_message
 *   The error message to show after a failure. Defaults to NULL because the UI turning red and the error output is usually enough.
 * @param bool $throw_drush_error
 *   Whether or not to throw a drush error if the process fails. Defaults to TRUE.
 *
 * @return string|void
 *   The output or error output of the command.
 */
function provision_process($command, $cwd = null, $label = 'Process', $env = array(), $log_output = TRUE, $error_message = NULL, $throw_drush_error = TRUE, $log_type = 'p_info') {
  if (empty($command)) {
    return;
  }

  // Merge in env vars, inheriting the CLI's
  if (is_array($env)) {
    $env = array_merge($_SERVER, $env);
  }
  else {
    $env = $_SERVER;
  }

  // Make sure colors always come through
  $env['TERM'] = 'xterm';

  $process = new \Symfony\Component\Process\Process($command, $cwd, $env);
  $process->setTimeout(NULL);
  if ($log_output) {
    drush_log("[$label] $command", 'p_command');

    $exit_code = $process->run(function ($type, $buffer) use ($log_type) {
      drush_log($buffer, $log_type);
    });
  }
  else {
    $exit_code = $process->run();
  }

  // Save the Provision Process object to Drush Context so that implementors can access the full object.
  drush_set_context('provision_process_result', $process);
  drush_log("Provision process command exited with $exit_code", 'debug');

  // check exit code
  if ($exit_code === 0) {

    if ($log_output) {
      drush_log('', 'p_ok');
    }
    return $process->getOutput();
  }
  else {

    if ($log_output) {
      drush_log('', 'p_error');
    }

    if ($throw_drush_error) {
      drush_set_error('PROVISION_PROCESS_ERROR', !empty($error_message)? $error_message: $process->getErrorOutput());
    }
    return $process->getErrorOutput();
  }
}

/**
 * the aegir version of the backend
 *
 * @return string
 *  the aegir version as stored in the .info file, potentially
 *  including the 6.x- prefix. to get a cleaned up version, use
 *  provision_version_parts()
 *
 * @see provision_version_parts()
 */
function provision_version() {
  $ini = parse_ini_file(dirname(__FILE__) . '/provision.info');
  return $ini['version'];
}

/**
 * Aegir API implemented by this backend
 *
 * This is the major release number, the first part of the version
 * stored in the info file
 *
 * @return int
 *   a number greater than zero, 1 for 1.0 or 1.0-rc2, 2 for 2.0, etc.
 *
 * @see provision_version_parts()
 */
function provision_api_version() {
  $parts = provision_version_parts();
  return $parts[0];
}

/**
 * The different parts of the version number
 *
 * This cleans up the version number by removing the Drupal version
 * (6.x-...) and splits the remaining version on dots.
 *
 * @return array
 *   the major and minor version numbers, e.g. array(1, 0-rc3) for
 *   1.0-rc3 or array(1, 2) for 1.2
 */
function provision_version_parts() {
  $version = preg_replace('/^[^-]*-/', '', provision_version()); // remove "6.x-"
  return explode('.', $version);
}

/**
 * Normalise a context name, ensuring that it starts with one '@'.
 *
 * @param $name
 *   The context name to normalise.
 *
 * @return
 *   The normalised context name.
 */
function provision_normalise_context_name($name) {
  return '@' . ltrim($name, '@');
}

// Base class for provision exceptions.
class provisionException extends Exception {

}

/**
 * Signal for parent to continue processing.
 *
 * The primary use for this class is for the config
 * classes to be able to signal to it's caller, that
 * the configuration file was not needed, and to
 * continue on.
 */
class provisionException_continue extends provisionException {

}

/**
 * Provision class.
 *
 * This is just a container for some useful static methods.
 */
class provision {
  /**
   * The actual body of the method_invoke function.
   *
   * This is a static method so it can be re-used by some other classes
   * that aren't contexts. (notably services and configs).
   */
  static function method_invoke($object, $func, $args = array()) {
    if (method_exists($object, $func)) {
      return call_user_func_array(array($object, $func), $args);
    }
  }
}

include_once('provision.context.inc');
include_once('provision.service.inc');
include_once('provision.file.inc');

/**
 * The drushrc alias file a context's provision-save writes and --delete unlinks
 * (Provision_Config_Drushrc_Alias::filename()).
 *
 * Tested directly rather than through provision_sitealias_get_record(): that
 * wrapper caches a miss for the life of the process, and the callers below ask
 * about a name BEFORE a subprocess writes it and again after.
 */
function provision_context_alias_file($name) {
  return drush_server_home() . '/.drush/' . ltrim((string) $name, '@') . '.alias.drushrc.php';
}

/**
 * Remove the context a failed clone or rename-migrate left behind.
 *
 * Both write the new site's context (provision-save) before they invoke the
 * deploy on it; the deploy's own rollback drops the half-built site directory
 * and database, but the context stayed behind, so a site that never came to
 * be kept an alias in ~/.drush and, once the deploy had run site:alias-convert,
 * a yml alias in ~/.drush/sites, until something else removed them. Called from
 * the command rollbacks, which Drush runs on ANY error raised after the command
 * hook completed; so this removes nothing unless the caller recorded, in the
 * option named by $preexisted_option, that it probed the name (0: free, 1: a
 * context of that name existed before the task, which is not ours to remove --
 * the front end never lets a clone or rename target an existing site, a
 * command-line run might), and nothing when the caller recorded in
 * $deployed_option that its deploy came through (a later step failed; the site
 * exists and keeps its context). Never the source site's own context, never one
 * of the source's own alias names (their pointer files are the source's).
 *
 * The yml alias is the converted copy of the drushrc alias; the limited-shell
 * worker regenerates the whole store from the drushrc aliases, but only on its
 * own triggers, so the stale copy is removed here with its checksum.
 */
function provision_remove_unfinished_context($new_name, $preexisted_option, $deployed_option = '') {
  $new_uri = ltrim((string) $new_name, '@');
  // A context name is a hostname-like token; anything else never reaches the
  // filesystem from here.
  if ($new_uri === '' || !preg_match('/^[A-Za-z0-9][A-Za-z0-9._-]*$/', $new_uri)) {
    return;
  }
  if ($new_uri === d()->uri || '@' . $new_uri === d()->name) {
    return;
  }
  // Unwritten: the task aborted before it probed the name and before its own
  // provision-save, so whatever exists under that name is not its leftover.
  $preexisted = drush_get_option($preexisted_option, NULL);
  if ($preexisted === NULL) {
    return;
  }
  if ($preexisted) {
    drush_log(dt('The context @name existed before this task and is left in place.', array('@name' => '@' . $new_uri)), 'notice');
    return;
  }
  if ($deployed_option !== '' && drush_get_option($deployed_option, FALSE)) {
    drush_log(dt('The deploy of @name completed; its context is kept although a later step failed.', array('@name' => '@' . $new_uri)), 'notice');
    return;
  }
  if (isset(d()->drush_aliases)) {
    $own = d()->drush_aliases;
    if (!is_array($own)) {
      $own = preg_split('/[\s,]+/', (string) $own, -1, PREG_SPLIT_NO_EMPTY);
    }
    if (in_array($new_uri, $own, TRUE)) {
      return;
    }
  }
  $new_name = '@' . $new_uri;
  if (is_file(provision_context_alias_file($new_name))) {
    drush_invoke_process('@none', 'provision-save', array($new_name), array('delete' => TRUE));
    drush_log(dt('Removed the context @name of the site this task left unfinished.', array('@name' => $new_name)), 'ok');
  }
  // The same home the deploy's alias conversion writes into (site:alias-convert
  // over $aegir_root/.drush/sites).
  $sites = d('@server_master')->aegir_root . '/.drush/sites';
  foreach (array($sites . '/' . $new_uri . '.site.yml', $sites . '/.checksums/' . $new_uri . '.site.md5') as $path) {
    if (is_file($path)) {
      provision_file()->unlink($path)
        ->succeed('Removed @path of the unfinished site')
        ->fail('Could not remove @path of the unfinished site');
    }
  }
}

/**
 * Name a leftover DisAutoUpDb control file, which no deploy honours any more.
 *
 * ~/static/control/DisAutoUpDb.info (every site) and <uri>_DisAutoUpDb.info
 * (one site) once made the deploy skip its updatedb, for accounts that ran the
 * updates by hand while they were unreliable in Aegir. Retired: the deploy
 * always applies the database updates. A skipped update cannot survive a move
 * onto newer core anyway -- the deploy's own cache rebuild meets the old
 * schema (a Drupal 10 site moved onto 11.3 fails on router.alias) and the task
 * rolls back. The file changes nothing; it is only named so it can go.
 */
function provision_report_retired_updatedb_controls($uri) {
  $control = d('@server_master')->aegir_root . '/static/control/';
  foreach (array($control . 'DisAutoUpDb.info', $control . $uri . '_DisAutoUpDb.info') as $path) {
    if (is_file($path)) {
      drush_log(dt('DRUSH/CTRL/UPDATEDB the retired control file @path is ignored: the deploy always runs the database updates, and the file can be deleted', array('@path' => $path)), 'notice');
    }
  }
}

/**
 * Report a failed tar run in tar's own words.
 *
 * Drush captures a command's output all along but prints it only under
 * --debug, so every archive failure -- a backup's `tar c`, a deploy's `tar x`
 * -- reached the task log as a bare "could not" line. "file changed as we
 * read it", "Cannot open: Permission denied" and "No space left on device"
 * each ask for a different remedy; a 442-second extraction that died on a
 * full disk read exactly like a corrupt archive. Trimmed, empty lines and the
 * closing "Exiting with failure status" summary dropped (the exit code said
 * that already), capped so a disk-full run that fails every remaining member
 * cannot flood the log.
 *
 * @param $output
 *   The captured lines, drush_shell_exec_output() taken right after the run
 *   (any later shell call overwrites that buffer).
 * @param $prefix
 *   Names the run in each logged line, e.g. 'Extraction error'.
 * @param $type
 *   The drush_log type, 'error' unless the caller tolerates the failure.
 * @param $cap
 *   Lines logged before the rest is summarised as a count.
 *
 * @return
 *   The first reported line, for the failure message; '' when tar said nothing.
 */
function provision_log_tar_output($output, $prefix, $type = 'error', $cap = 20) {
  $lines = array();
  if (is_array($output)) {
    foreach ($output as $line) {
      $line = trim($line);
      if ($line === '' || strpos($line, 'Exiting with failure status') !== FALSE) {
        continue;
      }
      $lines[] = $line;
    }
  }
  $shown = 0;
  foreach ($lines as $line) {
    if ($shown >= $cap) {
      drush_log(dt('... and @n more lines from tar not shown', array('@n' => count($lines) - $shown)), $type);
      break;
    }
    drush_log(dt('@prefix: @l', array('@prefix' => $prefix, '@l' => $line)), $type);
    $shown++;
  }
  return empty($lines) ? '' : $lines[0];
}

/**
 * Measure a site directory in bytes, following links when asked.
 *
 * With $follow_links the figure is what an archive made with tar's -h sees:
 * the per-account store behind the files/private links, an attached mount,
 * everything. Without it, what a link-preserving archive packs. Both come
 * from one `du -s` (stat only, seconds even for a store of a few hundred
 * thousand files), bounded by a timeout so a pathological tree cannot stall
 * the task. Unreadable corners make du exit non-zero while it still prints
 * its total, so the total is taken whatever the exit code.
 *
 * @return
 *   Bytes, or FALSE when the directory could not be measured: the caller then
 *   skips its check rather than refuse on a guess.
 */
function provision_site_bytes($site_path, $follow_links = TRUE) {
  if (empty($site_path) || !is_dir($site_path)) {
    return FALSE;
  }
  $command = $follow_links ? 'timeout 900 du -s -L --block-size=1 -- %s' : 'timeout 900 du -s --block-size=1 -- %s';
  drush_shell_exec($command, $site_path);
  $output = drush_shell_exec_output();
  if (!is_array($output)) {
    return FALSE;
  }
  $bytes = FALSE;
  foreach ($output as $line) {
    if (preg_match('/^(\d+)\s/', trim($line), $matches)) {
      $bytes = (float) $matches[1];
    }
  }
  return $bytes;
}

/**
 * Refuse a copying task before it starts when its copies cannot fit.
 *
 * A clone, a migrate and a restore each make a safety copy of the site and
 * unpack another copy, and a site whose files live in the per-account store
 * is copied in full: a 74 GB site cloned onto a disk with less room than
 * that filled it 44 minutes into the task, took nginx and mysqld down with
 * ENOSPC for the whole box, and rolled back. Measured before a byte is
 * written, refused with the numbers when it cannot fit. Estimates: the
 * safety copy is counted at the site's full size (compression only helps),
 * the unpacked copy at the same, and a renamed site's store copy at the
 * store's size; a very compressible site may be refused with a little room
 * to spare, which is the cheap side of that error.
 *
 * @param $task
 *   'clone', 'migrate' or 'restore', the flow whose copies are counted.
 * @param $platform
 *   The target platform context name for a clone or migrate (NULL = same).
 * @param $new_name
 *   The target site context name of a clone or a migrate under a new name.
 * @param $restore_file
 *   The archive a restore unpacks.
 *
 * @return
 *   TRUE when the copies fit or the question could not be judged; FALSE,
 *   after drush_set_error(), when they cannot.
 */
function provision_copy_task_space_preflight($task, $platform = NULL, $new_name = NULL, $restore_file = NULL) {
  // The operator's valve for an estimate that gets a box wrong.
  if (is_file('/data/conf/disable_space_preflight.cnf')) {
    return TRUE;
  }
  if (function_exists('provision_is_hostmaster_site') && provision_is_hostmaster_site()) {
    return TRUE;
  }
  $site_path = d()->site_path;
  $backup_path = d('@server_master')->backup_path;
  $aegir_root = d('@server_master')->aegir_root;
  $store_root = $aegir_root . '/static/files';
  $source_root = d()->platform->root;
  $target_root = $source_root;
  if (!empty($platform)) {
    $target_platform = d($platform);
    if (is_object($target_platform) && !empty($target_platform->root)) {
      $target_root = $target_platform->root;
    }
  }
  $what = ucfirst($task) . ' of ' . d()->uri;
  $gb = 1073741824;
  $full_bytes = provision_site_bytes($site_path, TRUE);
  if ($full_bytes === FALSE) {
    // Fail open, loudly: a refusal on a guess would be the worse error, and
    // the extraction's own floor still stands.
    drush_log(dt('SPACE/PREFLIGHT @what: @path could not be measured within the bound, so the space check is SKIPPED; make sure the copies fit (about twice the site\'s size following links)', array('@what' => $what, '@path' => $site_path)), 'warning');
    return TRUE;
  }
  // The volatile cache directories are tagged out of every archive and so
  // never unpacked either; for a site with gigabytes of image styles they
  // are the difference between a fit and a false refusal.
  $excluded_bytes = provision_site_excluded_bytes($site_path);
  if ($excluded_bytes > 0 && $excluded_bytes < $full_bytes) {
    $full_bytes -= $excluded_bytes;
  }
  // The database dump a restorable safety copy writes: uncompressed into the
  // site directory first, then inside the archive, then unpacked again. For
  // a database-heavy site it is the largest single new file of the task.
  $db_bytes = provision_site_database_bytes();
  $needs = array();
  $detail = array();
  $unpack_bytes = 0;
  switch ($task) {
    case 'clone':
      // The safety copy is forced restorable (self-contained, links followed)
      // and the deploy unpacks all of it beside the source; when the store
      // sits on another filesystem than the platform, the unshare that moves
      // the unpacked files into the store is a copy there as well.
      $needs[] = array($backup_path, $full_bytes + $db_bytes);
      $needs[] = array($target_root, $full_bytes + $db_bytes);
      $needs[] = array($source_root, $db_bytes);
      $unpack_bytes = $full_bytes + $db_bytes;
      $detail[] = dt('a self-contained safety copy (@gb GB) and the unpacked clone (@gb GB)', array('@gb' => round(($full_bytes + $db_bytes) / $gb, 1)));
      if (provision_path_device($store_root) !== provision_path_device($target_root)) {
        $needs[] = array($store_root, $full_bytes);
        $detail[] = dt('the clone\'s own store on its separate filesystem (@gb GB)', array('@gb' => round($full_bytes / $gb, 1)));
      }
      break;

    case 'restore':
      // Its safety copy is forced restorable too; the archive it unpacks
      // holds a site about the size of the live one, and never less than
      // the archive itself.
      $archive_bytes = (!empty($restore_file) && is_file($restore_file)) ? (float) filesize($restore_file) : 0;
      $unpack_bytes = max($archive_bytes, $full_bytes + $db_bytes);
      $needs[] = array($backup_path, $full_bytes + $db_bytes);
      $needs[] = array($target_root, $unpack_bytes);
      $needs[] = array($source_root, $db_bytes);
      $detail[] = dt('a self-contained safety copy (@gb GB) and the unpacked archive (about @ugb GB, a lower bound)', array('@gb' => round(($full_bytes + $db_bytes) / $gb, 1), '@ugb' => round($unpack_bytes / $gb, 1)));
      break;

    case 'migrate':
      // The safety copy keeps links unless a files-carrying mode was asked
      // for or the operator opted into dereferencing; a site renamed under a
      // new name then gets its own copy of the store (the unshare), a
      // same-name migrate keeps it.
      $local_bytes = provision_site_bytes($site_path, FALSE);
      if ($local_bytes === FALSE) {
        $local_bytes = $full_bytes;
      }
      $asked_mode = function_exists('provision_backup_mode_sanitize') ? provision_backup_mode_sanitize(drush_get_option('selected_backup_mode', FALSE)) : FALSE;
      $dereference = ($asked_mode == 'site_files_with_mysqldump' || $asked_mode == 'site_files_without_db_backup')
        || (is_file('/data/conf/dereference_backups.conf') && !is_file('/data/conf/force_symlinks.conf'));
      $archive_bytes = $dereference ? $full_bytes : $local_bytes;
      $unpack_bytes = $archive_bytes;
      // The fast path dumps the database uncompressed into the backups
      // directory's transient store, once.
      $needs[] = array($backup_path, $archive_bytes + $db_bytes);
      $needs[] = array($target_root, $archive_bytes);
      $detail[] = dt('a safety copy (@gb GB, plus a @dgb GB database dump) and the unpacked site (@gb GB)', array('@gb' => round($archive_bytes / $gb, 1), '@dgb' => round($db_bytes / $gb, 1)));
      $renamed = !empty($new_name) && ltrim($new_name, '@') != ltrim(d()->name, '@');
      $store_bytes = $full_bytes - $local_bytes;
      if ($renamed && $store_bytes > 0 && (is_link($site_path . '/files') || is_link($site_path . '/private'))) {
        $needs[] = array($store_root, $store_bytes);
        $detail[] = dt('the renamed site\'s own copy of the store (@gb GB)', array('@gb' => round($store_bytes / $gb, 1)));
      }
      break;

    default:
      return TRUE;
  }
  // Sum per filesystem: two copies on one device need room for both.
  $by_device = array();
  foreach ($needs as $need) {
    list($path, $bytes) = $need;
    if ($bytes <= 0 || empty($path)) {
      continue;
    }
    $probe = provision_path_probe($path);
    $device = provision_path_device($path);
    if ($probe === FALSE || $device === FALSE) {
      drush_log(dt('SPACE/PREFLIGHT @what: @path could not be examined, @gb GB of the estimate went unchecked', array('@what' => $what, '@path' => $path, '@gb' => round($bytes / $gb, 1))), 'warning');
      continue;
    }
    if (!isset($by_device[$device])) {
      $by_device[$device] = array('path' => $probe, 'need' => 0);
    }
    $by_device[$device]['need'] += $bytes;
  }
  drush_log(dt('SPACE/PREFLIGHT @what: the site measures @gb GB following links (cache directories left out); the task needs @detail', array(
    '@what' => $what,
    '@gb' => round($full_bytes / $gb, 1),
    '@detail' => implode(dt(' and '), $detail),
  )), 'info');
  foreach ($by_device as $device) {
    $free = @disk_free_space($device['path']);
    if ($free === FALSE) {
      drush_log(dt('SPACE/PREFLIGHT @what: the free space at @path could not be read, that filesystem went unchecked', array('@what' => $what, '@path' => $device['path'])), 'warning');
      continue;
    }
    $margin = provision_space_margin($device['path']);
    if ($free < $device['need'] + $margin) {
      drush_set_error('PROVISION_INSUFFICIENT_SPACE', dt('@what needs about @need GB free on the filesystem holding @path (@margin GB of it headroom) but only @free GB is free: free space first, nothing was written (every copy is counted at the site\'s full size following links, or at the archive\'s size when that is larger; /data/conf/disable_space_preflight.cnf turns this check off)', array(
        '@what' => $what,
        '@need' => round(($device['need'] + $margin) / $gb, 1),
        '@margin' => round($margin / $gb, 1),
        '@path' => $device['path'],
        '@free' => round($free / $gb, 1),
      )));
      return FALSE;
    }
  }
  // For the re-check at the deploy seam, once the safety copy exists.
  drush_set_option('space_preflight_unpack_bytes', $unpack_bytes);
  drush_set_option('space_preflight_unpack_path', $target_root);
  return TRUE;
}

/**
 * The bytes of a site's tagged-out cache directories, following links.
 *
 * PROVISION_BACKUP_EXCLUDED_DIRECTORIES (the Drupal/Backdrop set) is what the
 * archive's --exclude-tag skips; a lineage without the constant measures 0.
 */
function provision_site_excluded_bytes($site_path) {
  if (!defined('PROVISION_BACKUP_EXCLUDED_DIRECTORIES') || empty($site_path)) {
    return 0;
  }
  $dirs = array();
  foreach (PROVISION_BACKUP_EXCLUDED_DIRECTORIES as $excluded) {
    $dir = $site_path . '/' . ltrim($excluded, './');
    if (is_dir($dir)) {
      $dirs[] = drush_escapeshellarg($dir);
    }
  }
  if (empty($dirs)) {
    return 0;
  }
  drush_shell_exec('timeout 300 du -s -L --block-size=1 -- ' . implode(' ', $dirs));
  $output = drush_shell_exec_output();
  $bytes = 0;
  if (is_array($output)) {
    foreach ($output as $line) {
      if (preg_match('/^(\d+)\s/', trim($line), $matches)) {
        $bytes += (float) $matches[1];
      }
    }
  }
  return $bytes;
}

/**
 * The nearest existing directory at or above a path, FALSE when none.
 */
function provision_path_probe($path) {
  $probe = (string) $path;
  while ($probe !== '' && $probe !== '/' && $probe !== '.' && !is_dir($probe)) {
    $probe = dirname($probe);
  }
  return is_dir($probe) ? $probe : FALSE;
}

/**
 * The device a path lives on (as a string key), FALSE when unknown.
 */
function provision_path_device($path) {
  $probe = provision_path_probe($path);
  if ($probe === FALSE) {
    return FALSE;
  }
  $stat = @stat($probe);
  return ($stat === FALSE) ? FALSE : (string) $stat['dev'];
}

/**
 * The headroom a copying task must leave on a filesystem: 2 GB, or 2% of
 * the filesystem when that is more. A copy that lands the box at zero is
 * the failure this exists to prevent, and BOA's own scripts stop at 90%.
 */
function provision_space_margin($path) {
  $margin = 2.0 * 1073741824;
  $total = @disk_total_space($path);
  if ($total !== FALSE && $total * 0.02 > $margin) {
    $margin = $total * 0.02;
  }
  return $margin;
}

/**
 * Re-check the unpacked copy's room at the deploy seam.
 *
 * The pre-flight ran before the safety copy, which takes as long as gzipping
 * the site takes -- 43 minutes for the incident's 74 GB -- and the room can
 * go elsewhere meanwhile. The archive exists now, so the question is only
 * whether the unpacked copy still fits with the same headroom; a refusal
 * here rolls the task back like any other failure before the deploy, the
 * archive included.
 */
function provision_copy_task_space_recheck($what) {
  if (is_file('/data/conf/disable_space_preflight.cnf')) {
    return TRUE;
  }
  $bytes = (float) drush_get_option('space_preflight_unpack_bytes', 0);
  $path = drush_get_option('space_preflight_unpack_path', '');
  if ($bytes <= 0 || empty($path)) {
    return TRUE;
  }
  $probe = provision_path_probe($path);
  if ($probe === FALSE) {
    return TRUE;
  }
  $free = @disk_free_space($probe);
  if ($free === FALSE) {
    return TRUE;
  }
  $gb = 1073741824;
  $margin = provision_space_margin($probe);
  if ($free < $bytes + $margin) {
    drush_set_error('PROVISION_INSUFFICIENT_SPACE', dt('@what: the safety copy is done, but only @free GB is free on the filesystem holding @path for the @need GB unpacked copy (@margin GB of it headroom); the room went elsewhere since the pre-flight, free space and retry', array(
      '@what' => $what,
      '@free' => round($free / $gb, 1),
      '@path' => $probe,
      '@need' => round(($bytes + $margin) / $gb, 1),
      '@margin' => round($margin / $gb, 1),
    )));
    return FALSE;
  }
  return TRUE;
}

/**
 * The site's database on disk, in bytes, as the safety copy's dump will
 * roughly weigh: the data and index lengths of its tables, read through the
 * database server's own connection. Zero when there is no database, the
 * server cannot be asked, or the site carries no database name (a Grav
 * capsule): the estimate then simply lacks the dump term.
 */
function provision_site_database_bytes() {
  $db_name = isset(d()->db_name) ? d()->db_name : '';
  if ($db_name === '' || !preg_match('/^[A-Za-z0-9_]+$/', $db_name)) {
    return 0;
  }
  try {
    $db = d()->service('db');
    if (!is_object($db) || !method_exists($db, 'query') || $db instanceof Provision_Service_null) {
      return 0;
    }
    $result = $db->query("SELECT COALESCE(SUM(data_length + index_length), 0) FROM information_schema.tables WHERE table_schema = '%s'", $db_name);
    if (!is_object($result) || !method_exists($result, 'fetch')) {
      return 0;
    }
    $row = $result->fetch(PDO::FETCH_NUM);
    return (is_array($row) && isset($row[0])) ? (float) $row[0] : 0;
  }
  catch (Exception $e) {
    return 0;
  }
}
