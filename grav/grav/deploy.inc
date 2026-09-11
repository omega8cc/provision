<?php
/**
 * @file
 * Post-deploy identity engine for a deployed Grav capsule.
 *
 * Runs after the archive is extracted and swapped in, in the TARGET site's
 * context, before the deploy hook removes the pre-deploy tree. Four classes
 * of deploy, taken from an explicit option the caller sets (never inferred
 * from which files happened to be readable at backup time):
 *
 *  - restore (identity-preserving, same uri): the archive is this site's own
 *    past. Its nonce key, api secret, account files and environment overlay
 *    are kept as they are; nothing here touches them.
 *  - clone (identity-CHANGING, new uri): the copy must not share the source's
 *    cryptographic identity, outstanding tokens, environment overlay name or
 *    contact addresses. Every step below is the flat-file twin of what the
 *    Textpattern deploy scrub does in SQL.
 *  - rename / upgrade (identity-PRESERVING, new uri): the same site under a
 *    new name, or the same site on a newer core. The keys, the accounts, the
 *    contact addresses and the licences are this site's own and stay exactly
 *    as they are; only the surfaces that are keyed by the uri move.
 *
 * The split that matters is identity, not uri: a clone is a second site and
 * must be able to stand next to the first, while a rename or an upgrade is
 * the SAME site answering somewhere else. Rotating a renamed site's keys
 * would log every one of its users out and invalidate every outstanding
 * link for no reason at all.
 *
 * Every write is a targeted line edit of the YAML files it touches, never a
 * parse-and-dump: a hand-edited config must stay byte-identical elsewhere.
 * The secret-surface modes are re-asserted last, after every write.
 */

$uri = d()->uri;
$site_path = d()->site_path;
$old_uri = drush_get_option('old_uri', '');

$deploy_class = drush_get_option('deploy_class', '');
if ($deploy_class === '') {
  // A caller that predates the class option: a same-uri deploy is a restore,
  // anything else is a copy.
  $is_restore_fallback = drush_get_option('is_restore', FALSE) || ($old_uri !== '' && $old_uri === $uri);
  $deploy_class = $is_restore_fallback ? 'restore' : 'clone';
}

if ($deploy_class === 'restore') {
  drush_log(dt('Restored Grav capsule at !uri (deploy class restore; identity preserved: keys, accounts and overlay as archived).', array('!uri' => $uri)), 'ok');
  return TRUE;
}
if ($deploy_class !== 'clone' && $deploy_class !== 'rename' && $deploy_class !== 'upgrade') {
  // Refusing here keeps the deploy from silently treating an unknown class as
  // one of the implemented shapes -- the difference between them is whether
  // the site keeps its cryptographic identity.
  return drush_set_error('PROVISION_GRAV_DEPLOY_CLASS',
    dt('Unknown deploy class "!class" for !uri; Grav implements restore, clone, rename and upgrade.',
    array('!class' => $deploy_class, '!uri' => $uri)));
}

$identity_changes = ($deploy_class === 'clone');

// ---- (c) the environment overlay follows the new uri ----
// Grav resolves environment:// to user/env/<GRAV_ENVIRONMENT> (or user/<..>
// in the 1.6 shape) and REFUSES to register a directory that does not exist:
// a capsule whose overlay still carries the old name silently loses every
// per-host config, blueprint and language override. .env and the vhost
// already name the new uri (the deploy pre hook wrote them). Shared with the
// rename command, which performs the same re-point without a deploy.
_provision_grav_repoint_uri($site_path, $old_uri, $uri);

// Resolved AFTER the overlay move: with an overlay present this IS the
// directory Grav reads config:// from.
$config_dirs = array_unique(array(
  $site_path . '/user/config',
  _provision_grav_config_dir($site_path, $uri),
));

if ($identity_changes) {
  // ---- (a) cryptographic identity: the copy signs its own nonces and tokens ----
  // The nonce key (CSRF signing, admin rate-limit hashing) and the api plugin's
  // JWT secret came along in the archive. Drop every copy the archive may have
  // carried, in both locations Grav could read them from, then seed fresh ones
  // in the location the copy actually resolves.
  foreach ($config_dirs as $config_dir) {
    foreach (array('/security-private.php', '/plugins/api-private.php') as $secret) {
      if (is_file($config_dir . $secret)) {
        @unlink($config_dir . $secret);
      }
    }
  }
  _provision_grav_seed_secrets($site_path, $uri, TRUE);
}

// ---- (b) outstanding tokens: no source reset link may open the copy ----
// The login plugin stores a pending password-reset token in the account
// file as a top-level "reset: <token>::<expiry>" line.
$accounts_dirs = array($site_path . '/user/accounts');
$env_dir = _provision_grav_env_dir($site_path, $uri);
if ($env_dir !== '' && is_dir($env_dir . '/accounts')) {
  $accounts_dirs[] = $env_dir . '/accounts';
}
$healed_once = FALSE;
if ($identity_changes) {
  $stripped = 0;
  $unwritable = array();
  foreach ($accounts_dirs as $accounts_dir) {
    $yamls = glob($accounts_dir . '/*.yaml');
    if (!is_array($yamls)) {
      continue;
    }
    foreach ($yamls as $yaml) {
      if (is_link($yaml) || !is_readable($yaml)) {
        if (!$healed_once) {
          $healed_once = TRUE;
          _provision_grav_root_heal($site_path);
        }
        if (is_link($yaml) || !is_readable($yaml)) {
          $unwritable[] = $yaml;
          continue;
        }
      }
      $lines = file($yaml);
      if (!is_array($lines)) {
        continue;
      }
      $kept = array();
      $dropped = 0;
      foreach ($lines as $line) {
        if (preg_match('/^reset:\s/', $line)) {
          $dropped++;
          continue;
        }
        $kept[] = $line;
      }
      if ($dropped === 0) {
        continue;
      }
      if (!is_writable($yaml) && !$healed_once) {
        $healed_once = TRUE;
        _provision_grav_root_heal($site_path);
      }
      if (@file_put_contents($yaml, implode('', $kept), LOCK_EX) === FALSE) {
        $unwritable[] = $yaml;
        continue;
      }
      $stripped += $dropped;
    }
  }
  if ($stripped > 0) {
    drush_log(dt('Cleared !n pending password-reset token(s) from the copy\'s account files.', array('!n' => $stripped)), 'ok');
  }
  if (!empty($unwritable)) {
    drush_log(dt('Could not rewrite !n account file(s) on the copy (owned by the web process); run Verify on the copy, then Reset password for its accounts.',
      array('!n' => count($unwritable))), 'warning');
  }
}

// ---- (d) contact addresses: the copy is its own site ----
// Every account e-mail, the site author address and the e-mail plugin's
// sender/recipient now point at the copy's own admin address (the address
// the install engine would have used for it), so a password reset or a
// form notification on the copy can never reach the source's people. The
// relay host and credentials are box-level and stay. Counts only in the
// log: no address ever lands in a task log.
if ($identity_changes) {
  $client_email = drush_get_option('client_email', '');
  if (!is_string($client_email) || !filter_var($client_email, FILTER_VALIDATE_EMAIL)) {
    $client_email = 'admin@' . $uri;
  }
  $rewritten = 0;
  $rewrite_targets = array();
  foreach ($accounts_dirs as $accounts_dir) {
    $yamls = glob($accounts_dir . '/*.yaml');
    if (is_array($yamls)) {
      foreach ($yamls as $yaml) {
        // Top-level "email:" in an account file.
        $rewrite_targets[$yaml] = '/^(email:\s*)(\S.*)$/';
      }
    }
  }
  foreach ($config_dirs as $config_dir) {
    if (is_file($config_dir . '/site.yaml')) {
      // "author:\n  email: ..." -- the indented key under author.
      $rewrite_targets[$config_dir . '/site.yaml'] = '/^(\s+email:\s*)(\S.*)$/';
    }
    if (is_file($config_dir . '/plugins/email.yaml')) {
      // The plugin's default sender and every default recipient; the mailer
      // block (host, port, credentials) is the box relay and is left alone.
      $rewrite_targets[$config_dir . '/plugins/email.yaml'] = '/^((?:from|to|cc|bcc|reply_to):\s*)(\S.*)$/';
    }
  }
  foreach ($rewrite_targets as $file => $pattern) {
    if (is_link($file) || !is_readable($file)) {
      continue;
    }
    $lines = file($file);
    if (!is_array($lines)) {
      continue;
    }
    $changed = 0;
    foreach ($lines as $i => $line) {
      if (preg_match($pattern, rtrim($line, "\r\n"), $m)) {
        $current = trim($m[2], " \t'\"");
        if ($current === '' || $current === $client_email) {
          continue;
        }
        $eol = substr($line, strlen(rtrim($line, "\r\n")));
        $lines[$i] = $m[1] . $client_email . $eol;
        $changed++;
      }
    }
    if ($changed === 0) {
      continue;
    }
    if (!is_writable($file) && !$healed_once) {
      $healed_once = TRUE;
      _provision_grav_root_heal($site_path);
    }
    if (@file_put_contents($file, implode('', $lines), LOCK_EX) !== FALSE) {
      $rewritten += $changed;
    }
    else {
      drush_log(dt('Could not rewrite the contact address in @file on the copy; review it in the admin.',
        array('@file' => basename($file))), 'advisory');
    }
  }
  if ($rewritten > 0) {
    drush_log(dt('Pointed !n contact address(es) on the copy at its own admin address.', array('!n' => $rewritten)), 'ok');
  }
  else {
    drush_log(dt('No contact addresses to re-point on the copy.'), 'ok');
  }

  // ---- (e) source-bound state that must be a decision, not a silent copy ----
  if (is_file($site_path . '/user/data/licenses.yaml')) {
    drush_log(dt('The copy carries user/data/licenses.yaml (premium licence keys) from the source; keep or remove it deliberately.'), 'advisory');
  }
}
else {
  drush_log(dt('Identity preserved for !uri (deploy class !class): nonce key, api secret, accounts, outstanding tokens, contact addresses and licences are this site\'s own and were not touched.',
    array('!uri' => $uri, '!class' => $deploy_class)), 'ok');
}

// ---- (h) never inherit a debug display setting ----
// The official release ships errors.display: true; a site deployed in that
// state would publish stack traces, whatever brought it here. The Grav twin
// of the Textpattern production_status normalisation: decide from the parsed
// YAML, write with a single-line replace.
foreach ($config_dirs as $config_dir) {
  $system_yaml = $config_dir . '/system.yaml';
  if (!is_file($system_yaml) || is_link($system_yaml) || !is_readable($system_yaml)) {
    continue;
  }
  $display_on = FALSE;
  if (class_exists('Symfony\Component\Yaml\Yaml')) {
    try {
      $parsed = Symfony\Component\Yaml\Yaml::parse(file_get_contents($system_yaml));
      $display_on = is_array($parsed) && isset($parsed['errors']['display']) && $parsed['errors']['display'] === TRUE;
    }
    catch (Exception $e) {
      $display_on = FALSE;
    }
  }
  else {
    $display_on = (bool) preg_match('/^errors:\s*\n(?:[ \t]+\S.*\n)*?[ \t]+display:\s*true\s*$/m', file_get_contents($system_yaml));
  }
  if (!$display_on) {
    continue;
  }
  $lines = file($system_yaml);
  $in_errors = FALSE;
  $done = FALSE;
  foreach ($lines as $i => $line) {
    if (preg_match('/^errors:\s*$/', rtrim($line))) {
      $in_errors = TRUE;
      continue;
    }
    if ($in_errors && preg_match('/^\S/', $line)) {
      $in_errors = FALSE;
    }
    if ($in_errors && preg_match('/^(\s+display:\s*)true(\s*)$/', rtrim($line, "\r\n"), $m)) {
      $eol = substr($line, strlen(rtrim($line, "\r\n")));
      $lines[$i] = $m[1] . 'false' . $eol;
      $done = TRUE;
      break;
    }
  }
  if ($done && @file_put_contents($system_yaml, implode('', $lines), LOCK_EX) !== FALSE) {
    drush_log(dt('Set errors.display to false (the archive had error display on).'), 'ok');
  }
}

// ---- (f) modes last, after every write above ----
_provision_grav_secure_secrets($site_path);

if ($identity_changes) {
  drush_log(dt('Deployed Grav copy at !uri (deploy class !class): own secrets, reset tokens cleared, contact addresses re-pointed, overlay renamed where present.',
    array('!uri' => $uri, '!class' => $deploy_class)), 'ok');
}
else {
  drush_log(dt('Deployed Grav site at !uri (deploy class !class): the uri-bound surfaces follow the new name, everything that makes it this site stays.',
    array('!uri' => $uri, '!class' => $deploy_class)), 'ok');
}
return TRUE;
