<?php
/**
 * @file
 * Provides the Provision_Config_Backdrop_Settings class.
 */

class Provision_Config_Backdrop_Settings extends Provision_Config {
  public $template = 'provision_backdrop_settings.tpl.php';
  public $description = 'Backdrop settings.php file';
  public $creds = array();
  protected $mode = 0440;

  function filename() {
    return $this->site_path . '/settings.php';
  }

  function process() {
    // Backdrop's numeric major version is 1, so the Drupal Settings ladder
    // would fall through to settings_6; there is no clean version seam for a
    // subclass. Select the single Backdrop template unconditionally.
    $this->template = 'provision_backdrop_settings.tpl.php';

    // Backdrop only supports the mysql driver, and the shared db normalisation
    // (db.php) skips the mysqli->mysql fix for major < 7 (Backdrop == 1), so
    // the alias can still carry 'mysqli'. Force it here, unconditionally.
    $this->data['db_type'] = ($this->data['db_type'] == 'mysqli') ? 'mysql' : $this->data['db_type'];

    // A non-empty hash_salt keeps install_backdrop() from trying to rewrite the
    // 0440 settings.php (install.core.inc install_hash_salt() only rewrites when
    // the salt is empty). Like the D8+ templates but without Drupal's Crypt
    // class (unavailable in a Backdrop tree). Unlike them we reuse an existing
    // salt so re-rendering on verify stays idempotent -- a changed salt would
    // invalidate every one-time login link and active session.
    $this->data['backdrop_hash_salt'] = $this->hash_salt();

    $this->version = provision_version();
    $this->api_version = provision_api_version();
    $this->cloaked = drush_get_option('provision_db_cloaking', $this->context->service('http')->cloaked_db_creds());

    if (provision_hosting_feature_enabled('subdirs')) {
      $this->data['subdirs_support_enabled'] = TRUE;
    }
    else {
      $this->data['subdirs_support_enabled'] = drush_get_option('subdirs_support');
    }

    foreach (array('db_type', 'db_user', 'db_passwd', 'db_host', 'db_name', 'db_port') as $key) {
      $this->creds[$key] = urldecode($this->data[$key]);
    }

    $this->data['extra_config'] = "# Extra configuration from modules:\n";
    $this->data['extra_config'] .= join("\n", drush_command_invoke_all('provision_drupal_config', d()->uri, $this->data));

    $this->group = $this->platform->server->web_group;

    // Add a handy variable indicating if the site is being backed up, we can
    // then react to this and change any settings we don't want backed up.
    $backup_file = drush_get_option('backup_file');
    $this->backup_in_progress = !empty($backup_file);

    // Create a blank local.settings.php file if not exists and option calls for this.
    $local_settings = $this->site_path . '/local.settings.php';
    $local_settings_blank = "<?php # local settings.php \n";
    $local_description = 'Backdrop local.settings.php file';
    if (!provision_file()->exists($local_settings)->status() &&
        drush_get_option('provision_create_local_settings_file', TRUE)) {
      provision_file()->file_put_contents($local_settings, $local_settings_blank)
        ->succeed('Generated blank ' . $local_description)
        ->fail('Could not generate ' . $local_description);
    }

    // Set permissions on local.settings.php, if it exists.
    if (provision_file()->exists($local_settings)->status()) {
      provision_file()->chgrp($local_settings, $this->group)
        ->succeed('Changed group ownership of <code>@path</code> to @gid')
        ->fail('Could not change group ownership of <code>@path</code> to @gid');
      provision_file()->chmod($local_settings, $this->mode | 0440)
        ->succeed('Changed permissions of <code>@path</code> to @perm')
        ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
  }

  /**
   * Resolve the hash salt: reuse the one already written, else a fresh one.
   *
   * Reuse keeps a re-render (e.g. every verify) from rotating the salt and
   * logging every user out / breaking outstanding one-time login links. Reading
   * the current file is fail-safe: any miss falls back to a fresh salt.
   */
  function hash_salt() {
    $existing = $this->existing_hash_salt();
    if (!empty($existing)) {
      return $existing;
    }
    return $this->random_hash_salt();
  }

  /**
   * Read a non-empty $settings['hash_salt'] out of the current settings.php.
   *
   * Returns '' when the file is absent, unreadable, or carries no salt.
   */
  function existing_hash_salt() {
    $file = $this->filename();
    if (!provision_file()->exists($file)->status()) {
      return '';
    }
    $contents = @file_get_contents($file);
    if ($contents === FALSE) {
      return '';
    }
    if (preg_match("/\\\$settings\\['hash_salt'\\]\\s*=\\s*'([^']*)'/", $contents, $matches)) {
      return $matches[1];
    }
    return '';
  }

  /**
   * Generate a non-empty, URL-safe hash salt.
   *
   * Kept 5.6-safe (this runs in the Provision backend): openssl if available,
   * otherwise an mt_rand byte string. The value only needs to be non-empty and
   * unpredictable; Backdrop uses it verbatim as $settings['hash_salt'].
   */
  function random_hash_salt() {
    $raw = FALSE;
    if (function_exists('openssl_random_pseudo_bytes')) {
      $raw = openssl_random_pseudo_bytes(55);
    }
    if ($raw === FALSE || $raw === '') {
      $raw = '';
      for ($i = 0; $i < 55; $i++) {
        $raw .= chr(mt_rand(0, 255));
      }
    }
    return rtrim(strtr(base64_encode($raw), '+/', '-_'), '=');
  }
}
