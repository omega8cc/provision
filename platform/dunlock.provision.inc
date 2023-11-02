<?php

/**
 * Drush hooks for the provision-dunlock command
 */

function drush_provision_dunlock() {

  $drush_lock_ctrl = d()->root . '/local_drush_locked.pid';
  $drush_unlock_ctrl = d()->root . '/local_drush_unlocked.pid';

  if (d()->type === 'platform' && provision_file()->exists($drush_lock_ctrl)->status()) {

    $dru_vnd_drush_local = d()->root . '/vendor/drush';
    $dru_vnd_drush_above = d()->root . '/../vendor/drush';
    $dru_vnd_symf_con_loc = d()->root . '/vendor/symfony/console/Input';
    $dru_vnd_symf_con_abo = d()->root . '/../vendor/symfony/console/Input';

    if (provision_file()->exists($dru_vnd_drush_local)->status()) {
      provision_file()->chmod($dru_vnd_drush_local, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_drush_above)->status()) {
      provision_file()->chmod($dru_vnd_drush_above, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_loc)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_loc, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_abo)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_abo, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    // Delete drush-lock flag file.
    provision_file()->unlink($drush_lock_ctrl)
      ->succeed('Removed Drush-Lock Flag File')
      ->fail('Could not remove Drush-Lock Flag File');

    // Create drush-unlock flag file.
    if (!provision_file()->exists($drush_unlock_ctrl)->status()) {
      $drush_unlock_ctrl_info = "Drush-UnLock Flag File \n";
      $local_description = 'Drush-UnLock Flag File';
      provision_file()->file_put_contents($drush_unlock_ctrl, $drush_unlock_ctrl_info)
        ->succeed('Generated ' . $local_description)
      	->fail('Could not generate ' . $local_description);
    }

  }
  else {
    return TRUE;
  }
}
