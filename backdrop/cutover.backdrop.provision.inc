<?php

/**
 * @file
 * Same-URI cutover command (Backdrop layer).
 *
 * The upgrade tasks land a converted copy at a NEW domain by design
 * (coexistence: the source keeps serving until the operator is satisfied).
 * Cutover is the deliberate final step: make the Backdrop copy answer at the
 * ORIGINAL domain. It chains two REAL hosting tasks synchronously, so every
 * side effect rides proven machinery and the frontend stays in sync:
 *
 *   1. delete on the site currently holding the target domain (resolved by
 *      the frontend at execution time and passed as the cutover_source
 *      option; the delete backs the source up first, removes its vhost and
 *      frees the domain), then
 *   2. migrate on THIS site to its own platform under the new name — the
 *      rename leg (alias/sites.php/files-store reconciliation, node retitle
 *      + context re-registration, Encryption auto-disable, follow-up verify)
 *      does all the work.
 *
 * When no site holds the target domain the delete leg is skipped and cutover
 * degrades to a plain claim-rename. That is also the recovery path: if a
 * previous cutover failed between the two legs, re-running it finds the
 * domain free and just renames.
 *
 * The rename disables Encryption for the new name (keys are name-bound);
 * re-enabling it is a deliberate operator step, called out in the task log.
 *
 * Downtime honesty: the original domain serves nothing between the source
 * vhost removal (inside delete) and the final verify of the renamed copy —
 * the window covers the copy's pre-migrate backup, deploy and verify, so it
 * scales with the copy's size. The operator triggers cutover deliberately.
 *
 * There is deliberately NO rollback hook: each chained hosting task carries
 * its own rollback, and un-deleting the source would mean an implicit
 * restore. A failure between the legs leaves the source retired (preserved
 * in its pre-delete backup) and this site at its previous name; the error
 * message says exactly that and points at the re-run path.
 */

/**
 * Implements drush_hook_COMMAND_validate() for provision-backdrop-cutover.
 *
 * Fail-closed: everything checkable before the destructive delete leg runs
 * is checked here. The frontend form and pre-task hook validate too, but the
 * backend must not rely on that.
 */
function drush_provision_backdrop_cutover_validate($target_domain = NULL) {
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_BACKDROP_CUTOVER_NOT_SITE',
      dt('provision-backdrop-cutover must run on a site context.'));
  }
  if (!provision_platform_is_backdrop(d())) {
    return drush_set_error('PROVISION_BACKDROP_CUTOVER_NOT_BACKDROP',
      dt('provision-backdrop-cutover only runs on a Backdrop site; this context is not on a Backdrop platform.'));
  }
  if (!is_string($target_domain) || $target_domain === '') {
    return drush_set_error('PROVISION_BACKDROP_CUTOVER_NO_DOMAIN',
      dt('No target domain given.'));
  }
  $target_domain = strtolower(ltrim($target_domain, '@'));
  // Conservative hostname shape check; the frontend already ran its FQDN
  // validator, this catches a mangled hand-run invocation before anything
  // destructive is dispatched.
  if (!preg_match('/^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/', $target_domain)
    || strpos($target_domain, '..') !== FALSE) {
    return drush_set_error('PROVISION_BACKDROP_CUTOVER_BAD_DOMAIN',
      dt('The target domain !domain is not a plausible domain name.',
      array('!domain' => $target_domain)));
  }
  if ($target_domain === strtolower(d()->uri)) {
    return drush_set_error('PROVISION_BACKDROP_CUTOVER_NOOP',
      dt('This site already answers at !domain; nothing to cut over.',
      array('!domain' => $target_domain)));
  }

  $aliases_dir = drush_server_home() . '/.drush';
  $source = drush_get_option('cutover_source', '');
  if (is_string($source) && $source !== '') {
    $source_uri = strtolower(ltrim($source, '@'));
    if ($source_uri === strtolower(ltrim(d()->name, '@'))
      || $source_uri === strtolower(d()->uri)) {
      return drush_set_error('PROVISION_BACKDROP_CUTOVER_SELF',
        dt('The cutover source resolves to this site itself.'));
    }
    if ($source_uri === 'hostmaster'
      || $source_uri === strtolower(d('@hostmaster')->uri)) {
      return drush_set_error('PROVISION_BACKDROP_CUTOVER_HOSTMASTER',
        dt('Refusing to retire the hosting front-end.'));
    }
    // The source must hold the target domain: taking over ITS domain is the
    // whole point. A mismatch means frontend and backend disagree — stop.
    if ($source_uri !== $target_domain) {
      return drush_set_error('PROVISION_BACKDROP_CUTOVER_MISMATCH',
        dt('The cutover source context (!source) does not hold the target domain (!domain).',
        array('!source' => $source_uri, '!domain' => $target_domain)));
    }
    if (!provision_file()->exists($aliases_dir . '/' . $source_uri . '.alias.drushrc.php')->status()) {
      return drush_set_error('PROVISION_BACKDROP_CUTOVER_NO_SOURCE',
        dt('The cutover source !source has no backend context; nothing holds !domain — re-run without a source to claim the free domain.',
        array('!source' => $source_uri, '!domain' => $target_domain)));
    }
  }
  else {
    // Claim mode: no site holds the domain per the frontend. A leftover
    // backend alias is tolerated ONLY when it demonstrably serves nothing —
    // its recorded site directory is gone — which is exactly the orphan a
    // rolled-back cutover rename leaves behind (the migrate's early
    // provision-save mints the new-name alias; its rollback re-saves only
    // the old name and never removes the new one). Without this tolerance
    // the documented re-run recovery would refuse on our own leftover.
    // Anything with a real site directory keeps the hard refusal.
    if (provision_file()->exists($aliases_dir . '/' . $target_domain . '.alias.drushrc.php')->status()) {
      $leftover = d('@' . $target_domain);
      $leftover_dir = (is_object($leftover) && isset($leftover->site_path) && is_string($leftover->site_path))
        ? $leftover->site_path : '';
      if ($leftover_dir === '' || provision_file()->exists($leftover_dir)->status()) {
        return drush_set_error('PROVISION_BACKDROP_CUTOVER_TAKEN',
          dt('A backend context already holds !domain but the frontend passed no cutover source; refusing to rename onto an occupied name.',
          array('!domain' => $target_domain)));
      }
      drush_log(dt('[cutover] A stale !domain alias with no site directory is left over (a rolled-back rename); the rename will overwrite it.',
        array('!domain' => $target_domain)), 'info');
    }
  }

  $platform_nid = drush_get_option('cutover_platform_nid', '');
  $db_nid = drush_get_option('cutover_db_server_nid', '');
  if (!ctype_digit((string) $platform_nid) || !ctype_digit((string) $db_nid)) {
    return drush_set_error('PROVISION_BACKDROP_CUTOVER_BAD_NIDS',
      dt('Missing or non-numeric platform/db-server node ids from the frontend.'));
  }
}

/**
 * Implements drush_hook_COMMAND() for provision-backdrop-cutover.
 *
 * Chain the two legs through REAL hosting tasks (the string+type form of the
 * hosting-task command creates the task with the passed task_args and runs
 * it in the same invocation), then verify the end state on disk (alias
 * files, plus the vhost when the layout is confirmed below) IN ADDITION to
 * the chained tasks' propagated error flags — either signal fails the leg.
 */
function drush_provision_backdrop_cutover($target_domain = NULL) {
  $target_domain = strtolower(ltrim($target_domain, '@'));
  $source = drush_get_option('cutover_source', '');
  $platform_nid = (string) drush_get_option('cutover_platform_nid', '');
  $db_nid = (string) drush_get_option('cutover_db_server_nid', '');
  $old_name = d()->name;
  $aliases_dir = drush_server_home() . '/.drush';

  // Vhost end-state probes, self-calibrating: armed only when the SOURCE
  // vhost is observed at the expected path beforehand, so a layout that
  // differs from the single-box nginx shape just disarms them (the alias
  // and error-flag checks still stand) instead of failing good runs.
  $vhostd_path = _provision_backdrop_cutover_vhostd_path();
  $vhost_scheme_ok = FALSE;

  // Leg 1 — retire the holder of the target domain. The chained frontend
  // delete backs it up first, then removes its vhost/site dir/alias and
  // flips its node to DELETED, freeing the domain for the rename.
  if (is_string($source) && $source !== '') {
    $source_vhost_file = ($vhostd_path === '') ? '' : $vhostd_path . '/' . ltrim($source, '@');
    if ($source_vhost_file !== '' && provision_file()->exists($source_vhost_file)->status()) {
      $vhost_scheme_ok = TRUE;
    }
    drush_log(dt('[cutover] Retiring !source to free !domain (its full backup is taken first and stays restorable)',
      array('!source' => $source, '!domain' => $target_domain)), 'info');
    provision_backend_invoke('@hostmaster', 'hosting-task', array($source, 'delete'), array('force' => TRUE));
    $source_alias_file = $aliases_dir . '/' . ltrim($source, '@') . '.alias.drushrc.php';
    if (drush_get_error()
      || provision_file()->exists($source_alias_file)->status()
      || ($vhost_scheme_ok && provision_file()->exists($source_vhost_file)->status())) {
      return drush_set_error('PROVISION_BACKDROP_CUTOVER_RETIRE_FAILED',
        dt('Retiring !source failed; nothing was renamed and this site is untouched. Inspect the delete task log on !source, then re-run cutover.',
        array('!source' => $source)));
    }
    drush_log(dt('[cutover] Source retired; !domain is free', array('!domain' => $target_domain)), 'ok');
  }
  else {
    drush_log(dt('[cutover] No site holds !domain — claiming it with a plain rename',
      array('!domain' => $target_domain)), 'info');
  }

  // Leg 2 — rename this site to the freed domain via a REAL migrate task on
  // its own platform, so the rename leg plus the frontend post-task
  // bookkeeping (node retitle, context re-registration, Encryption
  // auto-disable, follow-up verify) all run.
  provision_backend_invoke('@hostmaster', 'hosting-task', array(
    $old_name,
    'migrate',
    'target_platform=' . $platform_nid,
    'new_uri=' . $target_domain,
    'new_db_server=' . $db_nid,
  ), array('force' => TRUE));

  $old_alias_file = $aliases_dir . '/' . ltrim($old_name, '@') . '.alias.drushrc.php';
  $new_alias_file = $aliases_dir . '/' . $target_domain . '.alias.drushrc.php';
  if (drush_get_error()
    || !provision_file()->exists($new_alias_file)->status()
    || provision_file()->exists($old_alias_file)->status()
    || ($vhost_scheme_ok && !provision_file()->exists($vhostd_path . '/' . $target_domain)->status())) {
    return drush_set_error('PROVISION_BACKDROP_CUTOVER_RENAME_FAILED',
      dt('The rename to !domain did not complete. The retired source is preserved in its pre-delete backup; if the migrate rolled back, this site still answers at !old. Fix the cause from the migrate task log, then re-run cutover — with !domain free it degrades to a plain rename (a stale leftover !domain alias from the rollback is detected and overwritten).',
      array('!domain' => $target_domain, '!old' => ltrim($old_name, '@'))));
  }

  drush_log(dt('[cutover] !old now answers at !domain',
    array('!old' => ltrim($old_name, '@'), '!domain' => $target_domain)), 'ok');
  drush_log(dt('[cutover] Encryption is OFF for !domain (the rename disables it — keys are name-bound). Re-enable it deliberately on the site node to issue a certificate for the new name.',
    array('!domain' => $target_domain)), 'warning');
}

/**
 * Resolve the vhost.d directory of the master http server, or '' when the
 * layout is not the expected one — callers treat '' as "probe disarmed".
 *
 * Read off the server CONTEXT (populated by the http service's init_server,
 * reachable through Provision_Context::__get like the shipped web_group
 * reads) — never via ->service('http')->server, whose $server property is
 * protected and fatals from a global function.
 */
function _provision_backdrop_cutover_vhostd_path() {
  $server = d('@server_master');
  if (is_object($server)
    && !empty($server->http_vhostd_path)
    && is_string($server->http_vhostd_path)) {
    return $server->http_vhostd_path;
  }
  return '';
}
