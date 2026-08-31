<?php
/**
 * @file
 * Post-deploy SQL rewrite + security scrub for a deployed TXP site.
 *
 * Runs after clone/migrate/rename/restore extraction + DB import, against the
 * TARGET site's DB with the target creds. The definitive rewrite list
 * (docs/analysis/txp-clone-surface.md §2 + the map §2.1 item 7):
 *  - prefs re-point: siteurl (never self-heals), path_to_site (heal is
 *    public-side only — an admin-first boot would write into the SOURCE),
 *    file_base_path, tempdir, plugin_cache_dir cleared (source-bound; PHP is
 *    include()d from it on every public request).
 *  - security state: blog_uid REGENERATED (feed GUID seed AND the CSRF salt —
 *    copies must not share it), blog_mail_uid reset, txp_token TRUNCATED
 *    (the source's outstanding password-reset/activation tokens stay valid
 *    otherwise), txp_log + txp_discuss_nonce truncated.
 * Same-URI restores get the same scrub: tokens/logs from the archive's epoch
 * are stale anyway, and a restore must not resurrect old reset links.
 * txp_image rows are never touched (ids key the files on disk).
 */

$uri = d()->uri;
$site_path = d()->site_path;

$db_user = urldecode(drush_get_option('db_user', '', 'site'));
$db_passwd = urldecode(drush_get_option('db_passwd', '', 'site'));
$db_host = drush_get_option('db_host', 'localhost', 'site');
$db_port = drush_get_option('db_port', '', 'site');
$db_name = drush_get_option('db_name', '', 'site');

if ($db_name === '' || $db_user === '') {
  return drush_set_error('PROVISION_TXP_DEPLOY_FAILED',
    dt('No site database credentials in the deploy context; cannot rewrite the deployed site.'));
}

mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);
try {
  $mysqli = mysqli_init();
  $mysqli->real_connect($db_host, $db_user, $db_passwd, $db_name, $db_port ? (int) $db_port : 3306);
  $mysqli->set_charset('utf8mb4');

  $result = $mysqli->query("SHOW TABLES LIKE 'textpattern'");
  if ($result === FALSE || $result->num_rows !== 1) {
    $mysqli->close();
    return drush_set_error('PROVISION_TXP_DEPLOY_FAILED',
      dt('The deployed database carries no TXP schema (anchor table missing).'));
  }

  // Prefs re-point.
  $prefs = array(
    'siteurl' => $uri,
    'path_to_site' => $site_path . '/public',
    'file_base_path' => $site_path . '/public/files',
    'tempdir' => $site_path . '/tmp',
    'plugin_cache_dir' => '',
  );
  $stmt = $mysqli->prepare("UPDATE txp_prefs SET val = ? WHERE name = ?");
  foreach ($prefs as $name => $val) {
    $stmt->bind_param('ss', $val, $name);
    $stmt->execute();
  }

  // Security scrub. blog_uid: 32 hex chars like the installer mints; the
  // CSRF salt must be unique per site copy.
  $new_uid = md5(uniqid(rand(), TRUE));
  $val = $new_uid;
  $name = 'blog_uid';
  $stmt->bind_param('ss', $val, $name);
  $stmt->execute();
  $val = md5(uniqid(rand(), TRUE)) . 'blog@example.com';
  $name = 'blog_mail_uid';
  $stmt->bind_param('ss', $val, $name);
  $stmt->execute();
  $stmt->close();

  foreach (array('txp_token', 'txp_log', 'txp_discuss_nonce') as $table) {
    $result = $mysqli->query("SHOW TABLES LIKE '" . $table . "'");
    if ($result !== FALSE && $result->num_rows === 1) {
      $mysqli->query("TRUNCATE TABLE " . $table);
    }
  }
  $mysqli->close();
}
catch (Exception $e) {
  return drush_set_error('PROVISION_TXP_DEPLOY_FAILED',
    dt('Deploy rewrite/scrub failed for !uri: !msg', array('!uri' => $uri, '!msg' => $e->getMessage())));
}
drush_log(dt('Deployed TXP site re-pointed at !uri; blog_uid regenerated, tokens/logs/nonces truncated.', array('!uri' => $uri)), 'ok');
