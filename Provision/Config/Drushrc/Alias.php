<?php
/**
 * @file
 * Provides the Provision_Config_Drushrc_Alias class.
 */

/**
 * Class to write an alias records.
 */
class Provision_Config_Drushrc_Alias extends Provision_Config_Drushrc {
  public $template = 'provision_drushrc_alias.tpl.php';

  /**
   * @param $name
   *   String '\@name' for named context.
   * @param $options
   *   Array of string option names to save.
   */
  function __construct($context, $data = []) {
    parent::__construct($context, $data);

    if (isset($data['aliases']) && is_array($data['aliases'])) {
      $data['aliases'] = array_unique($data['aliases']);
    }
    if (isset($data['drush_aliases']) && is_array($data['drush_aliases'])) {
      $data['drush_aliases'] = array_unique($data['drush_aliases']);
    }

    $this->data = array(
      'aliasname' => ltrim($context, '@'),
      'options' => $data,
    );
  }

  function filename() {
    return drush_server_home() . '/.drush/' . $this->data['aliasname'] . '.alias.drushrc.php';
  }
}
