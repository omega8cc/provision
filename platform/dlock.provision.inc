<?php

/**
 * Drush hooks for the provision-dlock command
 */

function drush_provision_dlock() {

  if (d()->type === 'platform' &&
     provision_file()->exists(d()->root)->status() &&
     !preg_match("/\/aegir\/distro\//gi", d()->root) &&
     !preg_match("/\/var\/aegir\//gi", d()->root)) {

    drush_log(dt('Drupal root reported in provision_lock_drush_local_vnd: !vendor', array('!vendor' => d()->root)), 'message');

    // Fix ownership first
    $success = drush_shell_exec("sudo --non-interactive /usr/local/bin/fix-drupal-platform-ownership.sh --root=%s --script-user=%s --web-group=%s", d()->root, d()->server->script_user, d()->server->web_group);

    $dru_vnd_drush_local = d()->root . '/vendor/drush';
    $dru_vnd_drush_above = d()->root . '/../vendor/drush';
    $dru_vnd_symf_con_loc = d()->root . '/vendor/symfony/console/Input';
    $dru_vnd_symf_con_abo = d()->root . '/../vendor/symfony/console/Input';

    if (provision_file()->exists($dru_vnd_drush_local)->status()) {
      provision_file()->chmod($dru_vnd_drush_local, 0400)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_drush_above)->status()) {
      provision_file()->chmod($dru_vnd_drush_above, 0400)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_loc)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_loc, 0400)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_abo)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_abo, 0400)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    $drush_lock_ctrl = d()->root . '/local_drush_locked.pid';

    if (!provision_file()->exists($drush_lock_ctrl)->status()) {
      // Create drush-lock flag file.
      $drush_lock_ctrl_info = "Drush-Lock Flag File \n";
      $local_description = 'Drush-Lock Flag File';
      provision_file()->file_put_contents($drush_lock_ctrl, $drush_lock_ctrl_info)
        ->succeed('Generated ' . $local_description)
      	->fail('Could not generate ' . $local_description);
    }

    $drush_unlock_ctrl = d()->root . '/local_drush_unlocked.pid';

    if (provision_file()->exists($drush_unlock_ctrl)->status()) {
      // Delete drush-unlocked flag file.
      provision_file()->unlink($drush_unlock_ctrl)
        ->succeed('Removed Drush-UnLocked Flag File')
        ->fail('Could not remove Drush-UnLocked Flag File');
    }

    if (drush_drupal_major_version() >= 10) {

      $this_user = provision_current_user();

      if ($this_user == 'aegir' ) {
        $psr_tpl = '/var/aegir/drush/vendor/psr/log/Psr/Log';
      }
      else {
        $psr_tpl = '/data/disk/' . $this_user . '/tools/drush/vendor/psr/log/Psr/Log';
      }

      if (provision_file()->exists($psr_tpl)->status()) {

        $vnd_psr_log_orig_local = d()->root . '/vendor/psr/._orig_log';
        $vnd_psr_log_orig_above = d()->root . '/../vendor/psr/._orig_log';

        $dru_vnd_psr_local = d()->root . '/vendor/psr/log';
        $dru_vnd_psr_above = d()->root . '/../vendor/psr/log';

        $psr_good_local = $dru_vnd_psr_local . '/src/Test/TestLogger.php';
        $psr_good_above = $dru_vnd_psr_above . '/src/Test/TestLogger.php';

        if (provision_file()->exists($dru_vnd_psr_local)->status()) {
          if (!provision_file()->exists($psr_good_local)->status()) {
            if (!provision_file()->exists($vnd_psr_log_orig_local)->status()) {
              $command = "cp -a $dru_vnd_psr_local $vnd_psr_log_orig_local";
              $success = drush_shell_exec($command);
            }
            $target_dir = d()->root . '/vendor/psr/log/src/';
            $command = "cp -a $psr_tpl/* $target_dir";
            $success = drush_shell_exec($command);
          }
        }

        if (provision_file()->exists($dru_vnd_psr_above)->status()) {
          if (!provision_file()->exists($psr_good_above)->status()) {
            if (!provision_file()->exists($vnd_psr_log_orig_above)->status()) {
              $command = "cp -a $dru_vnd_psr_above $vnd_psr_log_orig_above";
              $success = drush_shell_exec($command);
            }
            $target_dir = d()->root . '/../vendor/psr/log/src/';
            $command = "cp -a $psr_tpl/* $target_dir";
            $success = drush_shell_exec($command);
          }
        }
      }

      $is_patched = "YES";
      $file_path = d()->root . '/core/lib/Drupal/Core/Logger/RfcLoggerTrait.php';
      $search_string = ': void';
      $file_content = file_get_contents($file_path);
      if (strpos($file_content, $search_string) !== false) {
        $is_patched = "NO";
      }

      if ($this_user == 'aegir' ) {
        $patchTpl = '/var/aegir/patches/drupal-ten-aegir-01.patch';
      }
      else {
        $patchTpl = '/data/conf/patches/drupal-ten-aegir-01.patch';
      }
      $patchFile = d()->root . '/drupal-ten-aegir-01.patch';
      $webRoot = d()->root;
      $command = sprintf('patch -d ' . $webRoot . ' -p1 < ' . $patchTpl);

      if (provision_file()->exists($patchTpl)->status() && $is_patched == 'NO') {
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
  }
  else {
    return TRUE;
  }
}
