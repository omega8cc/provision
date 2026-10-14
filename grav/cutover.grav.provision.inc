<?php

/**
 * @file
 * Same-URI cutover command (Grav layer).
 *
 * The upgrade-a-copy task lands an upgraded capsule at a NEW domain by design
 * (coexistence: the source keeps serving until the operator is satisfied).
 * Cutover is the deliberate final step: make the Grav copy answer at the
 * ORIGINAL domain. It chains two REAL hosting tasks synchronously, so every
 * side effect rides proven machinery and the frontend stays in sync:
 *
 *   1. delete on the site currently holding the target domain (resolved by
 *      the frontend at execution time and passed as the cutover_source
 *      option; the delete backs the source up first, removes its vhost and
 *      frees the domain), then
 *   2. grav_rename on THIS site — the same-platform rename leg (capsule move,
 *      uri re-point, new context and vhost, old context and vhost retired)
 *      plus the frontend post-task bookkeeping (node retitle, context
 *      re-registration, Encryption auto-disable, follow-up verify).
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
 * vhost removal (inside delete) and the final verify of the renamed copy. For
 * a capsule that window is a directory move and a vhost render rather than a
 * database import, so it is short — but the operator triggers cutover
 * deliberately all the same.
 *
 * There is deliberately NO rollback hook: each chained hosting task carries
 * its own rollback, and un-deleting the source would mean an implicit
 * restore. A failure between the legs leaves the source retired (preserved
 * in its pre-delete backup) and this site at its previous name; the error
 * message says exactly that and points at the re-run path.
 */

/**
 * Implements drush_hook_COMMAND_validate() for provision-grav-cutover.
 *
 * Fail-closed: everything checkable before the destructive delete leg runs
 * is checked here. The frontend form and pre-task hook validate too, but the
 * backend must not rely on that.
 */
function drush_provision_grav_cutover_validate($target_domain = NULL) {
  if (d()->type !== 'site') {
    return drush_set_error('PROVISION_GRAV_CUTOVER_NOT_SITE',
      dt('provision-grav-cutover must run on a site context.'));
  }
  if (!provision_platform_is_grav(d())) {
    return drush_set_error('PROVISION_GRAV_CUTOVER_NOT_GRAV',
      dt('provision-grav-cutover only runs on a Grav site; this context is not on a Grav platform.'));
  }
  if (!is_string($target_domain) || $target_domain === '') {
    return drush_set_error('PROVISION_GRAV_CUTOVER_NO_DOMAIN',
      dt('No target domain given.'));
  }
  $target_domain = strtolower(ltrim($target_domain, '@'));
  // Conservative hostname shape check; the frontend already ran its FQDN
  // validator, this catches a mangled hand-run invocation before anything
  // destructive is dispatched.
  if (!preg_match('/^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$/', $target_domain)
    || strpos($target_domain, '..') !== FALSE) {
    return drush_set_error('PROVISION_GRAV_CUTOVER_BAD_DOMAIN',
      dt('The target domain !domain is not a plausible domain name.',
      array('!domain' => $target_domain)));
  }
  if ($target_domain === strtolower(d()->uri)) {
    return drush_set_error('PROVISION_GRAV_CUTOVER_NOOP',
      dt('This site already answers at !domain; nothing to cut over.',
      array('!domain' => $target_domain)));
  }

  $aliases_dir = drush_server_home() . '/.drush';
  $source = drush_get_option('cutover_source', '');
  if (is_string($source) && $source !== '') {
    $source_uri = strtolower(ltrim($source, '@'));
    if ($source_uri === strtolower(ltrim(d()->name, '@'))
      || $source_uri === strtolower(d()->uri)) {
      return drush_set_error('PROVISION_GRAV_CUTOVER_SELF',
        dt('The cutover source resolves to this site itself.'));
    }
    if ($source_uri === 'hostmaster'
      || $source_uri === strtolower(d('@hostmaster')->uri)) {
      return drush_set_error('PROVISION_GRAV_CUTOVER_HOSTMASTER',
        dt('Refusing to retire the hosting front-end.'));
    }
    // The source must hold the target domain: taking over ITS domain is the
    // whole point. A mismatch means frontend and backend disagree — stop.
    if ($source_uri !== $target_domain) {
      return drush_set_error('PROVISION_GRAV_CUTOVER_MISMATCH',
        dt('The cutover source context (!source) does not hold the target domain (!domain).',
        array('!source' => $source_uri, '!domain' => $target_domain)));
    }
    if (!provision_file()->exists($aliases_dir . '/' . $source_uri . '.alias.drushrc.php')->status()) {
      return drush_set_error('PROVISION_GRAV_CUTOVER_NO_SOURCE',
        dt('The cutover source !source has no backend context; nothing holds !domain — re-run without a source to claim the free domain.',
        array('!source' => $source_uri, '!domain' => $target_domain)));
    }
  }
  else {
    // Claim mode: no site holds the domain per the frontend. A leftover
    // backend alias is tolerated ONLY when it demonstrably serves nothing —
    // its recorded site directory is gone. The Grav rename's own rollback
    // removes the context it saved, so this shape should not arise; it is
    // tolerated because a rollback that could not finish leaves exactly it,
    // and refusing on our own leftover would block the documented re-run
    // recovery. Anything with a real site directory keeps the hard refusal —
    // which is also what a rename stuck at the new path looks like.
    if (provision_file()->exists($aliases_dir . '/' . $target_domain . '.alias.drushrc.php')->status()) {
      $leftover = d('@' . $target_domain);
      $leftover_dir = (is_object($leftover) && isset($leftover->site_path) && is_string($leftover->site_path))
        ? $leftover->site_path : '';
      if ($leftover_dir === '' || provision_file()->exists($leftover_dir)->status()) {
        return drush_set_error('PROVISION_GRAV_CUTOVER_TAKEN',
          dt('A backend context already holds !domain but the frontend passed no cutover source; refusing to rename onto an occupied name.',
          array('!domain' => $target_domain)));
      }
      drush_log(dt('[cutover] A stale !domain alias with no site directory is left over (a rolled-back rename); the rename will overwrite it.',
        array('!domain' => $target_domain)), 'info');
    }
  }

  // Carried for parity with the other cutover commands and checked as a
  // frontend/backend agreement: the Grav rename leg needs neither id, but a
  // frontend that cannot resolve this site's platform and db server has not
  // resolved the holder either, and that is the value this command is being
  // handed.
  $platform_nid = drush_get_option('cutover_platform_nid', '');
  $db_nid = drush_get_option('cutover_db_server_nid', '');
  if (!ctype_digit((string) $platform_nid) || !ctype_digit((string) $db_nid)) {
    return drush_set_error('PROVISION_GRAV_CUTOVER_BAD_NIDS',
      dt('Missing or non-numeric platform/db-server node ids from the frontend.'));
  }
}

/**
 * Implements drush_hook_COMMAND() for provision-grav-cutover.
 *
 * Chain the two legs through REAL hosting tasks (the string+type form of the
 * hosting-task command creates the task with the passed task_args and runs
 * it in the same invocation), then verify the end state on disk (alias
 * files, plus the vhost when the layout is confirmed below) IN ADDITION to
 * the chained tasks' propagated error flags — either signal fails the leg.
 */
function drush_provision_grav_cutover($target_domain = NULL) {
  $target_domain = strtolower(ltrim($target_domain, '@'));
  $source = drush_get_option('cutover_source', '');
  $old_name = d()->name;
  $aliases_dir = drush_server_home() . '/.drush';

  // Vhost end-state probes, self-calibrating: armed only when the SOURCE
  // vhost is observed at the expected path beforehand, so a layout that
  // differs from the single-box nginx shape just disarms them (the alias
  // and error-flag checks still stand) instead of failing good runs.
  $vhostd_path = _provision_grav_cutover_vhostd_path();
  $vhost_scheme_ok = FALSE;

  // The site leg 1 retires, as it ran before it goes: a copy still held, or
  // a site that ran as a store of its own (a released copy among them), the
  // store its own marker names, if any, and whether it carries a store at
  // all (KahunaCart in its capsule, the test BOA's scheduler applies). A
  // marker BOA never writes (a link, a directory) is judged by the
  // scheduler's own test, which follows it: held while it exists and no
  // release file for the site is newer.
  $retired = (is_string($source) && $source !== '') ? strtolower(ltrim($source, '@')) : '';
  $retired_held = FALSE;
  $retired_names = array('', '');
  $retired_store = FALSE;
  if ($retired !== '') {
    $retired_record = drush_sitealias_get_record('@' . $retired);
    if (!empty($retired_record['site_path'])) {
      $retired_marker = _provision_grav_copy_marker($retired_record['site_path']);
      $retired_held = _provision_grav_copy_held($retired_record['site_path'], $retired);
      if (!$retired_held && (is_link($retired_marker) || (file_exists($retired_marker) && !is_file($retired_marker)))) {
        clearstatcache();
        $marked = @filemtime($retired_marker);
        $released = @filemtime(d('@server_master')->aegir_root . '/static/control/kahunacart-run.' . $retired . '.info');
        $retired_held = ($marked !== FALSE) && !($released !== FALSE && $released > $marked);
      }
      $retired_names = _provision_grav_copy_names($retired_record['site_path']);
      $retired_store = is_file($retired_record['site_path'] . '/user/plugins/kahunacart/kahunacart.php');
    }
  }

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
      return drush_set_error('PROVISION_GRAV_CUTOVER_RETIRE_FAILED',
        dt('Retiring !source failed; nothing was renamed and this site is untouched. Inspect the delete task log on !source, then re-run cutover.',
        array('!source' => $source)));
    }
    drush_log(dt('[cutover] Source retired; !domain is free', array('!domain' => $target_domain)), 'ok');
  }
  else {
    drush_log(dt('[cutover] No site holds !domain — claiming it with a plain rename',
      array('!domain' => $target_domain)), 'info');
  }

  // The copy takes over from the store it stands in for: the hold is lifted
  // when leg 1 retired that store at the marker's first name, a site that
  // carried a store (any site, for a copy that carries none) and was neither
  // a held copy nor a copy of another store, and the site the copy was made
  // from (its second name, when another one) does not run as a store now; or
  // when leg 1 retired the site the copy was made from while that site ran
  // as a released copy of the same store. A site that now holds either
  // domain but is no such site lifts nothing. A domain is all the marker
  // keeps, so a store that is no copy, put at the store's domain, still
  // passes that test (the source's test still applies). It is lifted before
  // the rename leg,
  // whose own safety backup would otherwise carry the hold onto the live
  // store's Backups tab, and it stays lifted when the rename does not
  // complete: that store is gone, this copy is the only one left. A copy the
  // account has released runs already, so its marker goes too: a release
  // names the domain, and the rename would otherwise hold the copy again
  // under its new name. Any other copy stays held until the account releases
  // it.
  $copy_marker = _provision_grav_copy_marker(d()->site_path);
  $copy_marked = is_file($copy_marker) && !is_link($copy_marker);
  $copy_held = $copy_marked && _provision_grav_copy_held(d()->site_path, d()->uri);
  $copy_names = $copy_marked ? _provision_grav_copy_names(d()->site_path) : array('', '');
  $copy_store = $copy_marked && is_file(d()->site_path . '/user/plugins/kahunacart/kahunacart.php');
  $copy_twin = '';
  $copy_lift = FALSE;
  if ($copy_marked && !$copy_held) {
    $copy_lift = TRUE;
    drush_log(dt('[cutover] This copy was released and runs as a store of its own: its hold marker is removed before the rename, so it keeps running at !domain.',
      array('!domain' => $target_domain)), 'ok');
  }
  elseif ($copy_held && $copy_names[0] !== '' && $retired !== '' && $copy_names[0] === $retired
    && ($retired_store || !$copy_store) && !$retired_held
    && ($retired_names[0] === '' || $retired_names[0] === $copy_names[0])) {
    if ($copy_names[1] !== '' && $copy_names[1] !== $copy_names[0] && _provision_grav_store_runs($copy_names[1])) {
      $copy_twin = $copy_names[1];
    }
    else {
      $copy_lift = TRUE;
    }
  }
  elseif ($copy_held && $copy_names[1] !== '' && $retired !== '' && $copy_names[1] === $retired
    && !$retired_held && $retired_names[0] !== '' && $retired_names[0] === $copy_names[0]) {
    $copy_lift = TRUE;
  }
  if ($copy_lift) {
    _provision_grav_unmark_copy(d()->site_path);
  }
  elseif ($copy_held) {
    $release = '~/static/control/kahunacart-run.' . $target_domain . '.info';
    if ($copy_twin !== '') {
      $note = dt('[cutover] The copy stays held: it was made from the site at !from, and a store runs at that domain now, possibly over the same customers. Release it with !release once !from no longer takes orders, or now if the store at !from is not the site this copy was made from.',
        array('!from' => $copy_twin, '!release' => $release));
    }
    elseif ($copy_names[0] === '') {
      $note = dt('[cutover] The copy stays held: its marker names no store. Release it with !release once its store and every other copy of that store no longer take orders.',
        array('!release' => $release));
    }
    elseif ($retired !== '' && $retired === $copy_names[0]
      && ($retired_held || ($retired_names[0] !== '' && $retired_names[0] !== $copy_names[0]))) {
      $note = dt('[cutover] The copy stays held: the site this cutover retires at !from was not the store it stands in for (it was a held copy or a copy of another store), so that store may run under another name now. Release the copy with !release once that store, under whatever name it runs now, and every other copy of it no longer take orders.',
        array('!from' => $retired, '!release' => $release));
    }
    elseif ($retired !== '' && $retired === $copy_names[0]) {
      $note = dt('[cutover] The copy stays held: the site this cutover retires at !from carried no store, so it was not the store this copy stands in for, which may run under another name now. If the shop was built on this copy and its source never had one, release it now with !release; otherwise release it once that store, under whatever name it runs now, and every other copy of it no longer take orders.',
        array('!from' => $retired, '!release' => $release));
    }
    elseif ($copy_names[1] !== '' && $copy_names[1] === $retired) {
      $note = dt('[cutover] The copy stays held: it stands in for !src, and the site this cutover retires at !from was not a released copy of that store. Release it with !release once that store, under whatever name it runs now, no longer takes orders.',
        array('!src' => $copy_names[0], '!from' => $retired, '!release' => $release));
    }
    elseif (is_file($aliases_dir . '/' . $copy_names[0] . '.alias.drushrc.php')) {
      $note = dt('[cutover] The copy stays held: it stands in for !src, which this cutover does not retire. Release it with !release once that store, under whatever name it runs now, no longer takes orders.',
        array('!src' => $copy_names[0], '!release' => $release));
    }
    else {
      $note = dt('[cutover] The copy stays held: it stands in for !src, which has no site on this server any more (deleted or renamed), so this cutover cannot tell whether that store still takes orders. Release the copy with !release once that store, under whatever name it runs now, and every other copy of it no longer take orders.',
        array('!src' => $copy_names[0], '!release' => $release));
    }
    drush_log($note, 'warning');
  }

  // Leg 2 — rename this site to the freed domain via a REAL grav_rename task,
  // so the rename leg plus the frontend post-task bookkeeping (node retitle,
  // context re-registration, Encryption auto-disable, follow-up verify) all
  // run.
  provision_backend_invoke('@hostmaster', 'hosting-task', array(
    $old_name,
    'grav_rename',
    'new_uri=' . $target_domain,
  ), array('force' => TRUE));

  $old_alias_file = $aliases_dir . '/' . ltrim($old_name, '@') . '.alias.drushrc.php';
  $new_alias_file = $aliases_dir . '/' . $target_domain . '.alias.drushrc.php';
  if (drush_get_error()
    || !provision_file()->exists($new_alias_file)->status()
    || provision_file()->exists($old_alias_file)->status()
    || ($vhost_scheme_ok && !provision_file()->exists($vhostd_path . '/' . $target_domain)->status())) {
    // The hold is not put back: the store it was held for is retired, so
    // this copy is the only one left, and a re-run (a plain rename by then,
    // with no source to match) could never lift it again.
    return drush_set_error('PROVISION_GRAV_CUTOVER_RENAME_FAILED',
      dt('The rename to !domain did not complete. The retired source is preserved in its pre-delete backup; if the rename rolled back, this site still answers at !old. Fix the cause from the rename task log, then re-run cutover — with !domain free it degrades to a plain rename (a stale leftover !domain alias from a rollback is detected and overwritten).',
      array('!domain' => $target_domain, '!old' => ltrim($old_name, '@'))));
  }

  drush_log(dt('[cutover] !old now answers at !domain',
    array('!old' => ltrim($old_name, '@'), '!domain' => $target_domain)), 'ok');
  // The copy took over from its source: its store worker runs from now on.
  if ($copy_lift) {
    _provision_grav_unmark_copy(d()->root . '/sites/' . $target_domain);
  }
  elseif ($copy_held) {
    // Still held under the new name: a release file left for this domain,
    // by the site leg 1 retired or earlier, is older than the marker from
    // now on and releases nothing.
    $moved_marker = _provision_grav_copy_marker(d()->root . '/sites/' . $target_domain);
    if (is_file($moved_marker) && !is_link($moved_marker)) {
      @touch($moved_marker);
    }
  }
  drush_log(dt('[cutover] Encryption is OFF for !domain (the rename disables it — keys are name-bound). Re-enable it deliberately on the site node to issue a certificate for the new name.',
    array('!domain' => $target_domain)), 'advisory');
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
function _provision_grav_cutover_vhostd_path() {
  $server = d('@server_master');
  if (is_object($server)
    && !empty($server->http_vhostd_path)
    && is_string($server->http_vhostd_path)) {
    return $server->http_vhostd_path;
  }
  return '';
}
