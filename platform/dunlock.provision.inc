<?php

/**
 * Drush hooks for the provision-dunlock command
 */

function genRandStr($length = 12) {
  return substr(str_shuffle(str_repeat($x='0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ', ceil($length/strlen($x)) )),1,$length);
}

function drush_provision_dunlock() {

  $drush_lock_ctrl = d()->root . '/local_drush_locked.pid';
  $drush_unlock_ctrl = d()->root . '/local_drush_unlocked.pid';

  if (d()->type === 'platform' && provision_file()->exists($drush_lock_ctrl)->status()) {

    $dru_vnd_drush_local = d()->root . '/vendor/drush';
    $dru_vnd_drush_above = d()->root . '/../vendor/drush';
    $dru_vnd_symf_con_loc = d()->root . '/vendor/symfony/console/Input';
    $dru_vnd_symf_con_abo = d()->root . '/../vendor/symfony/console/Input';

    $bin_vnd_drush_local = d()->root . '/vendor/bin/drush';
    $bin_vnd_drush_above = d()->root . '/../vendor/bin/drush';
    $dru_dru_dru_php_local = d()->root . '/vendor/drush/drush/drush.php';
    $dru_dru_dru_php_above = d()->root . '/../vendor/drush/drush/drush.php';
    $dru_dru_drush_local = d()->root . '/vendor/drush/drush/drush';
    $dru_dru_drush_above = d()->root . '/../vendor/drush/drush/drush';

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
    if (provision_file()->exists($bin_vnd_drush_local)->status()) {
      provision_file()->chmod($bin_vnd_drush_local, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($bin_vnd_drush_above)->status()) {
      provision_file()->chmod($bin_vnd_drush_above, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_dru_dru_php_local)->status()) {
      provision_file()->chmod($dru_dru_dru_php_local, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_dru_dru_php_above)->status()) {
      provision_file()->chmod($dru_dru_dru_php_above, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_dru_drush_local)->status()) {
      provision_file()->chmod($dru_dru_drush_local, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_dru_drush_above)->status()) {
      provision_file()->chmod($dru_dru_drush_above, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (drush_drupal_major_version() >= 10) {

      $this_user = provision_current_user();

      if ($this_user == 'aegir' ) {
        $psr_tpl = '/var/aegir/drush/vendor/psr/log/Psr/Log';
        $psr_bak = '/var/aegir/backups';
      }
      else {
        $psr_tpl = '/data/disk/' . $this_user . '/tools/drush/vendor/psr/log/Psr/Log';
        $psr_bak = '/data/disk/' . $this_user . '/backups';
      }

      if (provision_file()->exists($psr_tpl)->status()) {

        $vnd_psr_log_orig_local = d()->root . '/vendor/psr/._orig_log';
        $vnd_psr_log_orig_above = d()->root . '/../vendor/psr/._orig_log';

        $dru_vnd_psr_local = d()->root . '/vendor/psr/log';
        $dru_vnd_psr_above = d()->root . '/../vendor/psr/log';

        $psr_good_local = $dru_vnd_psr_local . '/src/Test/TestLogger.php';
        $psr_good_above = $dru_vnd_psr_above . '/src/Test/TestLogger.php';

        if (provision_file()->exists($dru_vnd_psr_local)->status()) {
          if (provision_file()->exists($psr_good_local)->status()) {
            if (provision_file()->exists($vnd_psr_log_orig_local)->status()) {
              $randstr = genRandStr(24);
              $psrback = $psr_bak . "/psr-log-" . $randstr;
              $command = "mv -f $dru_vnd_psr_local $psrback";
              $success = drush_shell_exec($command);
              $command = "mv -f $vnd_psr_log_orig_local $dru_vnd_psr_local";
              $success = drush_shell_exec($command);
            }
            else {
              $randstr = genRandStr(24);
              $psrback = $psr_bak . "/psr-log-" . $randstr;
              $command = "cp -a $dru_vnd_psr_local $psrback";
              $success = drush_shell_exec($command);
              $approot = d()->root;
              $result = drush_shell_cd_and_exec($approot, 'composer reinstall psr/log --no-interaction --no-progress');
              $output = implode('', drush_shell_exec_output());
              drush_log("PSR/LOG " . $output);
            }
          }
        }

        if (provision_file()->exists($dru_vnd_psr_above)->status()) {
          if (provision_file()->exists($psr_good_above)->status()) {
            if (provision_file()->exists($vnd_psr_log_orig_above)->status()) {
              $randstr = genRandStr(24);
              $psrback = $psr_bak . "/psr-log-" . $randstr;
              $command = "mv -f $dru_vnd_psr_above $psrback";
              $success = drush_shell_exec($command);
              $command = "mv -f $vnd_psr_log_orig_above $dru_vnd_psr_above";
              $success = drush_shell_exec($command);
            }
            else {
              $randstr = genRandStr(24);
              $psrback = $psr_bak . "/psr-log-" . $randstr;
              $command = "cp -a $dru_vnd_psr_above $psrback";
              $success = drush_shell_exec($command);
              $approot = d()->root . "/../";
              $result = drush_shell_cd_and_exec($approot, 'composer reinstall psr/log --no-interaction --no-progress');
              $output = implode('', drush_shell_exec_output());
              drush_log("PSR/LOG " . $output);
            }
          }
        }
      }

      if ($this_user == 'aegir' ) {
        $patchTpl = '/var/aegir/patches/drupal-ten-aegir-01.patch';
      }
      else {
        $patchTpl = '/data/conf/patches/drupal-ten-aegir-01.patch';
      }
      $patchFile = d()->root . '/drupal-ten-aegir-01.patch';
      $webRoot = d()->root;
      $command = sprintf('patch -d ' . $webRoot . ' -R -p1 < ' . $patchTpl);

      if (provision_file()->exists($patchTpl)->status()) {
        if (!provision_file()->exists($patchFile)->status()) {
          $success = drush_shell_exec($command);
          $result = drush_shell_exec_output();
          foreach ($result as $index => $line) {
            if (!$success) { // Log the entire error as a warning.
              $line_status = 'warning';
            }
            else { // Only log the last line as success.
              $line_status = $index+1 == count($result) ? 'success' : 'notice';
            }
            drush_log($line, $line_status);
          }
          provision_file()->copy($patchTpl, $patchFile);
        }
      }
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
