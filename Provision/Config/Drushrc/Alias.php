<?php
/**
 * @file
 * Provides the Provision_Config_Drushrc_Alias class.
 */
use Symfony\Component\Yaml\Yaml;

/**
 * Class to write an alias records.
 */
class Provision_Config_Drushrc_Alias extends Provision_Config_Drushrc {
  public $template = 'provision_drushrc_alias.tpl.php';

  /**
   * @param $name
   *   String name for named context.
   * @param $options
   *   Array of string option names to save.
   */
  function __construct($context, $options = array()) {
    parent::__construct($context, $options);

    if (isset($options['aliases']) && is_array($options['aliases'])) {
      $options['aliases'] = array_unique($options['aliases']);
    }
    if (isset($options['drush_aliases']) && is_array($options['drush_aliases'])) {
      $options['drush_aliases'] = array_unique($options['drush_aliases']);
    }

    // Force drush to use hostmaster drush
    $options['path-aliases']['%drush-script'] = d()->drush_script;

    $name = ltrim($context, '@');

    // Site, Server, & Platform alias info.
    $this->data = array(
      'aliasname' => $name,
      'options' => $options,
    );
  }

  function filename() {
    return drush_server_home() . '/.drush/' . $this->data['aliasname'] . '.alias.drushrc.php';
  }

  function filenameYaml() {
    return drush_server_home() . '/.drush/sites/' . $this->data['options']['hosting_group'] . '.site.yml';
  }

  function write()
  {
    $this->writeSiteAlias();
    return parent::write(); // TODO: Change the autogenerated stub
  }

  /**
   * Write a Drush 9+ YML drush alias.
   * @return void
   */
  function writeSiteAlias() {
    if ($this->context->type == 'site') {

      try {
        $alias = Yaml::parseFile($this->filenameYaml());
      }
      catch (\Exception $e) {
        $alias = [];
      }

      $alias[$this->data['options']['hosting_environment']] = [
        'root' => $this->data['options']['root'],
        'uri' => $this->data['options']['uri'],
        'options' => $this->data['options'],
      ];

      $yaml = Yaml::dump($alias, 10, 2);
      $filename = $this->filenameYaml();

      provision_file()->file_put_contents($filename, $yaml)
        ->succeed('Wrote Yaml Drush Site Alias file: ' . $filename, 'success')
        ->fail('Could not write Yaml Drush Site Alias file: ' . $filename)
        ->status();
    }
  }

  function delete() {
    if (file_exists($this->filenameYaml())){
      unlink($this->filenameYaml());
    }
    if (file_exists($this->filename())){
      unlink($this->filename());
    }
  }
}
