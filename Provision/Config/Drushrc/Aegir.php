<?php
/**
 * @file
 * Provides the Provision_Config_Drushrc_Aegir class.
 */

/**
 * Class for writing the /var/aegir/.drush/drushrc.php file.
 */
class Provision_Config_Drushrc_Aegir extends Provision_Config_Drushrc {
  protected $context_name = 'home.drush';
  public $template = 'provision_drushrc_aegir.tpl.php';
  public $description = 'Aegir Drush configuration file';

  function __construct($context = '@none', $data = array()) {
    parent::__construct($context, $data);
    $this->load_data();
  }

  function load_data() {
    // List Hosting Features and their enabled status.
    $features = hosting_get_features();
    foreach ($features as $name => $info) {
      $enabled_features[$name] = $info['enabled'];
    }

    $this->data['hosting_features'] = $enabled_features;

    $this->data['drush_exclude'] = array();
    $this->data['drush_include'] = array(
      'provision' => dirname(dirname(dirname(dirname(__FILE__)))),
    );
    foreach($enabled_features as $feature => $status) {
      if ($status === '0') {
        $this->data['drush_exclude'][] = $feature;
      }
      else {
        $feature_include_path = DRUPAL_ROOT . '/' . drupal_get_path('module', $features[$feature]['module']) . '/drush';
        if (file_exists($feature_include_path)) {
          $this->data['drush_include'][$feature] = $feature_include_path;
        }
      }
    }
  }

  function write()
  {
    $this->writeDrushYml();
    return parent::write();
  }

  function filenameYaml() {
    return drush_server_home() . '/.drush/drush.yml';
  }

  /**
   * Write a Drush 9+ YML config file.
   * @return void
   */
  function writeDrushYml() {
    $drush_config = [
      'drush' => [
        'paths' => [
          'alias-path' => [
            '${env.HOME}/.drush/sites'
          ],
          'config' => [
            '${env.HOME}/.drush/local.drush.yml'
          ]
        ],
      ],
    ];
    $yaml = \Symfony\Component\Yaml\Yaml::dump($drush_config, 10, 2);
    $filename = $this->filenameYaml();

    provision_file()->file_put_contents($filename, $yaml)
      ->succeed('Wrote Drush.yml config file: ' . $filename, 'success')
      ->fail('Could not write drush.yml config file: ' . $filename)
      ->status();
  }
}
