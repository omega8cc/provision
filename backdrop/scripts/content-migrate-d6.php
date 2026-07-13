<?php

/**
 * CCK field-data migration driver for provision-backdrop-d6-upgrade, run as
 * `drush @copy php-script` against the D7 intermediate AFTER the kit enable
 * and the second updatedb. Drives the content_migrate API directly
 * (module_load_include), never its drush commands — module-shipped drush
 * commandfiles are not reliably discovered for site aliases.
 *
 * HARD GATE (D-026, no force flag): any field content_migrate classes
 * 'missing' (its provider module is absent/disabled) aborts the hop via
 * drush_set_error — the error rides the integrated backend invoke into the
 * parent command, which then discards the copy. Skipping a field would
 * strand its data silently: the later Backdrop upgrade completes green with
 * the content simply absent.
 *
 * API realities this script is written against (CCK 7.x-2.x
 * includes/content_migrate.admin.inc):
 * - content_migrate_get_options() returns FALSE when the source has no CCK
 *   tables, else array('available'=>[], 'converted'=>[], 'missing'=>[]).
 * - The structure op reports errors ONLY via drupal_set_message('error');
 *   $context['results']['errors'] is never populated.
 * - The data op NEVER sets $context['finished'] >= 1 when driven raw; the
 *   authoritative completion signal is an empty $context['sandbox']['nodes']
 *   work queue. Row counts can plateau legitimately (empty values write no
 *   row), so they corroborate the log only, never terminate the loop.
 * - A per-record exception makes the data op self-roll-back the whole field
 *   (field_delete_field + drop) — the field then reappears in 'available',
 *   which the final re-check treats as failure.
 */

// One field's whole node queue is held in memory by the batch op; the
// process is disposable and runs under the instance CLI (PHP 7.4).
ini_set('memory_limit', '-1');

module_load_include('inc', 'content_migrate', 'includes/content_migrate.admin');

if (!function_exists('content_migrate_get_options')) {
  drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_CONTENT_MIGRATE',
    dt('content_migrate is not enabled on the copy; the kit enable step did not take.'));
  return;
}

// Drop any messages queued during bootstrap so the error capture below only
// sees what the migration itself emits.
drupal_get_messages(NULL, TRUE);

$info = content_migrate_get_options();
if ($info === FALSE || !is_array($info)) {
  drush_log(dt('CONTENT-MIGRATE no CCK field tables on the source; nothing to migrate.'), 'ok');
  return;
}

foreach (array('available', 'converted', 'missing') as $bucket) {
  $names = !empty($info[$bucket]) ? array_keys($info[$bucket]) : array();
  drush_log(dt('CONTENT-MIGRATE @bucket: @names',
    array('@bucket' => $bucket, '@names' => empty($names) ? dt('(none)') : implode(', ', $names))), 'ok');
}

// THE HARD GATE: a 'missing' field has no provider module on the copy.
if (!empty($info['missing'])) {
  drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_CONTENT_MIGRATE',
    dt('Fields with no provider module on the copy remain: @fields. Aborting; the copy must be discarded. Extend the D6->D7 kit (or enable the missing module on the target platform) and retry.',
    array('@fields' => implode(', ', array_keys($info['missing'])))));
  return;
}

if (empty($info['available'])) {
  drush_log(dt('CONTENT-MIGRATE all CCK fields already converted; nothing to do.'), 'ok');
  return;
}

// Structure pass: create the D7 field + instances per field.
foreach (array_keys($info['available']) as $field_name) {
  drush_log(dt('CONTENT-MIGRATE structure: @field', array('@field' => $field_name)), 'ok');
  $context = array('sandbox' => array(), 'results' => array(), 'finished' => 0);
  _content_migrate_batch_process_create_fields($field_name, $context);
  $messages = drupal_get_messages('error', TRUE);
  if (!empty($messages['error'])) {
    drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_CONTENT_MIGRATE',
      dt('Field structure creation failed for @field: @errors',
      array('@field' => $field_name, '@errors' => implode(' | ', $messages['error']))));
    return;
  }
}

// Data pass: drain each field's node queue; empty sandbox[nodes] is the
// authoritative completion signal (the finished flag never reaches 1 raw).
foreach (array_keys($info['available']) as $field_name) {
  drush_log(dt('CONTENT-MIGRATE data: @field', array('@field' => $field_name)), 'ok');
  $context = array('sandbox' => array(), 'results' => array(), 'finished' => 0);
  $iterations = 0;
  $iteration_cap = 0;
  do {
    _content_migrate_batch_process_migrate_data($field_name, $context);
    $iterations++;
    if ($iteration_cap === 0 && isset($context['sandbox']['max'])) {
      // Each call consumes a bounded batch of queued nodes; size the cap
      // from the queue with generous headroom.
      $iteration_cap = (int) ceil(intval($context['sandbox']['max']) / 10) + 10;
    }
    $messages = drupal_get_messages('error', TRUE);
    if (!empty($messages['error'])) {
      drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_CONTENT_MIGRATE',
        dt('Data migration failed for @field (the op rolls the field back): @errors',
        array('@field' => $field_name, '@errors' => implode(' | ', $messages['error']))));
      return;
    }
  } while (!empty($context['sandbox']['nodes']) && $iterations < ($iteration_cap > 0 ? $iteration_cap : 10000));
  if (!empty($context['sandbox']['nodes'])) {
    drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_CONTENT_MIGRATE',
      dt('Data migration for @field did not drain its work queue within @cap iterations; aborting.',
      array('@field' => $field_name, '@cap' => $iterations)));
    return;
  }
  drush_log(dt('CONTENT-MIGRATE data: @field drained in @n iterations.',
    array('@field' => $field_name, '@n' => $iterations)), 'ok');
}

// Final re-check: every field must have landed in 'converted'. A field back
// in 'available' means the data op silently rolled it back; 'missing' here
// would mean a module got disabled mid-run. Both are failures.
$post = content_migrate_get_options();
if (!is_array($post)) {
  drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_CONTENT_MIGRATE',
    dt('content_migrate lost sight of the CCK tables after migration; aborting.'));
  return;
}
if (!empty($post['available']) || !empty($post['missing'])) {
  $stuck = array_merge(
    !empty($post['available']) ? array_keys($post['available']) : array(),
    !empty($post['missing']) ? array_keys($post['missing']) : array()
  );
  drush_set_error('PROVISION_BACKDROP_D6_UPGRADE_CONTENT_MIGRATE',
    dt('Fields failed to convert (rolled back or lost their module mid-run): @fields. Aborting; the copy must be discarded.',
    array('@fields' => implode(', ', $stuck))));
  return;
}

// Corroborating record: converted field row counts (log only, not a gate —
// fields whose values were all empty legitimately carry zero rows).
$converted = !empty($post['converted']) ? array_keys($post['converted']) : array();
foreach ($converted as $field_name) {
  $table = 'field_data_' . $field_name;
  if (db_table_exists($table)) {
    $count = db_query('SELECT COUNT(*) FROM {' . $table . '}')->fetchField();
    drush_log(dt('CONTENT-MIGRATE @table rows: @count', array('@table' => $table, '@count' => $count)), 'ok');
  }
  else {
    drush_log(dt('CONTENT-MIGRATE @table missing after conversion.', array('@table' => $table)), 'warning');
  }
}
drush_log(dt('CONTENT-MIGRATE complete: @n fields converted.', array('@n' => count($converted))), 'ok');
