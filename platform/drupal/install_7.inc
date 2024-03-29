<?php
/**
 *  @file
 *    Rebuild all the caches
 */

$GLOBALS['url'] = d()->uri;
$GLOBALS['profile'] = d()->profile;
$GLOBALS['install_locale'] = d()->language;
$GLOBALS['base_url'] = provision_get_base_url();

require_once 'install.inc';  // From Provision, not the Drupal platform.
define('MAINTENANCE_MODE', 'install');

function install_send_welcome_mail($url, $account, $language, $client_email, $onetime) {
  global $base_url;
  if ($client_email) {
    // Mail one time login URL and instructions.
    $from = variable_get('site_mail', ini_get('sendmail_from'));
    $mail_params['variables'] = array(
      '!username' => $account->name, '!site' => variable_get('site_name', 'Drupal'), '!login_url' => $onetime,
      '!uri' => $base_url, '!uri_brief' => preg_replace('!^https?://!', '', $base_url), '!mailto' => $account->mail,
      '!date' => format_date(time()), '!login_uri' => url('user', array('absolute' => TRUE)),
      '!edit_uri' => url('user/' . $account->uid . '/edit', array('absolute' => TRUE)));

    $mail_success = drupal_mail('install', 'welcome-admin', $account->mail, user_preferred_language($account), $mail_params, $from, TRUE);

    if ($mail_success) {
      drush_log(dt('Sent welcome mail to @client', array('@client' => $client_email)), 'success');
    }
    else {
      drush_log(dt('Could not send welcome mail to @client', array('@client' => $client_email)), 'warning');
    }
  }
}


function install_mail($key, &$message, $params) {
  global $profile;
  switch ($key) {
    case 'welcome-admin':
      // allow the profile to override welcome email text
      if (file_exists("./profiles/$profile/provision_welcome_mail.inc")) {
        require_once "./profiles/$profile/provision_welcome_mail.inc";
        $custom = TRUE;
      }
      elseif (file_exists(dirname(__FILE__) . '/../provision_welcome_mail.inc')) {
        /** use the module provided welcome email
         * We can not use drupal_get_path here,
         * as we are connected to the provisioned site's database
         */
        require_once dirname(__FILE__) . '/../provision_welcome_mail.inc';
        $custom = TRUE;
      }
      else {
        // last resort use the user-pass mail text
        $custom = FALSE;
      }

      if ($custom) {
        $message['subject'] = dt($mail['subject'], $params['variables']);
        $message['body'][] = dt($mail['body'], $params['variables']);
      }
      else {
        $message['subject'] = _user_mail_text('pass_subject', $params['variables']);
        $message['body'][] = _user_mail_text('pass_body', $params['variables']);
      }

      break;
    }
}


function install_main() {
  global $profile, $install_locale, $conf, $url, $base_url;

  require_once DRUPAL_ROOT . '/includes/install.core.inc';
  require_once DRUPAL_ROOT . '/includes/entity.inc';
  require_once DRUPAL_ROOT . '/includes/unicode.inc';
  require_once DRUPAL_ROOT . '/modules/user/user.module';

  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_CONFIGURATION);

  // We have an existing settings.php.

  switch (substr(drush_core_version(), 0, 1)) {
    case '10':
    case '9':
    case '8':
    case '7':
      $sql = drush_get_class('Drush\Sql\Sql', array(), array(drush_drupal_major_version()));
      $db_spec = $sql->get_db_spec();
      break;
    case '6':
    case '5':
      $db_spec = _drush_sql_get_db_spec();
      break;
    default:
      drush_set_error('DRUSH_UNSUPPORTED_VERSION', dt('Drush !version is not supported'));
  }

  $db_spec['db_prefix'] = $GLOBALS['db_prefix'];

  if ($db_spec['driver'] == 'mysqli') {
    $db_spec['driver'] = 'mysql';
  }


  unset($conf['site_name']);
  unset($conf['site_mail']);
  unset($GLOBALS['db_url']);

  $account_pass = provision_password();
  $name = drush_get_option('admin_user', PROVISION_DEFAULT_ADMIN_USER);
  if ($error = user_validate_name($name)) {
    drush_log(dt('Error setting UID username to `!user`: ', array('!user' => $name)) . $error . dt(' Using default (!default) instead.', array('!default' => PROVISION_DEFAULT_ADMIN_USER)) , 'warning');
    $name = PROVISION_DEFAULT_ADMIN_USER;
  }
  $client_email = install_validate_client_email(drush_get_option('client_email', FALSE));
  $settings = array(
    'parameters' => array(
      'profile' => $profile,
      'locale' => $install_locale,
    ),
    'settings_verified' => TRUE,
    'forms' => array(
      'install_settings_form' => $db_spec,
      'install_configure_form' => array(
        'site_name' => $url,
        'site_mail' => $client_email,
        'account' => array(
          'name' => $name,
          'mail' => $client_email,
          'pass' => array(
            'pass1' => $account_pass,
            'pass2' => $account_pass,
          ),
        ),
        'update_status_module' => array(
          1 => TRUE,
          2 => TRUE,
        ),
        'clean_url' => drush_get_option('clean_url', TRUE),
      ),
    ),
  );

  // Allow other commands to alter the installation settings.
  drush_command_invoke_all_ref('provision_drupal_install_settings_alter', $settings, $url);

  try {
    install_drupal($settings);
  }
  catch (Exception $e) {
    drush_set_error('DRUPAL_INSTALL_FAILED');
    drush_log('Site installation caused an exception: ' . $e->getMessage(), 'error');
    drush_log("Trace: \n" . $e->getTraceAsString(), 'debug');
    dlm($e);

    # Site install failed. Do no proceed further through install_main();
    return;
  }
  _provision_drupal_create_directories();

  $account = user_load(1);

  // If a redirect is defined, the symlink to the alias needs to exist before
  // we generate the login link, below.
  _provision_drupal_maintain_aliases();

  // Store the one time login link in an option so the front end can direct the
  // user to their new site.
  $onetime = provision_generate_login_reset();
  drush_set_option('login_link', $onetime);
  drush_log(dt('Login url: !onetime', array('!onetime' => $onetime)), 'success');

  if (drush_get_option('client_email', FALSE)) {
    install_send_welcome_mail($url, $account, $install_locale, $client_email, $onetime);
  }
}
install_main();

function install_civicrm_dashboard() {
  // Create a CiviCRM dashboard link in the navigation menu.
  $options = array(
    'link_title' => 'CiviCRM',
    'link_path' => 'civicrm/dashboard',
  );
  menu_link_save($options);

  // Update the menu router and cache information.
  menu_cache_clear_all();

  drush_log(dt("CiviCRM: dashboard link in the navigation menu added in install_civicrm_dashboard."), 'ok');
}

if (provision_file()->exists(DRUPAL_ROOT . '/sites/all/modules/civicrm/drupal/civicrm.install')->status()) {
  install_civicrm_dashboard();
}

function install_exception_handler() {
  dlm(func_get_args());
}
