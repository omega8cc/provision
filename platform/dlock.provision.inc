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

    $dru_dru_dru_php_local = d()->root . '/vendor/drush/drush/drush.php';
    $dru_dru_dru_php_above = d()->root . '/../vendor/drush/drush/drush.php';

    if (provision_file()->exists($dru_vnd_symf_con_loc)->status()) {
      if (!provision_file()->exists($dru_dru_dru_php_local)->status()) {
        $approot = d()->root;
        $result = drush_shell_cd_and_exec($approot, 'composer require drush/drush --no-interaction --no-progress');
        $output = implode('', drush_shell_exec_output());
        drush_log("DRUSH/DRUSH " . $output);
      }
    }
    elseif (provision_file()->exists($dru_vnd_symf_con_abo)->status()) {
      if (!provision_file()->exists($dru_dru_dru_php_above)->status()) {
        $approot = d()->root . "/../";
        $result = drush_shell_cd_and_exec($approot, 'composer require drush/drush --no-interaction --no-progress');
        $output = implode('', drush_shell_exec_output());
        drush_log("DRUSH/DRUSH " . $output);
      }
    }

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
          $symfonyDir = d()->root . '/vendor/symfony/';
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
        $symfonyDir = d()->root . '/../vendor/symfony/';
      }

      $is_core_patched = "YES";
      $core_file_path = d()->root . '/core/lib/Drupal/Core/Logger/RfcLoggerTrait.php';
      $core_search_string = ': void';
      $core_file_content = file_get_contents($core_file_path);
      if (strpos($core_file_content, $core_search_string) !== false) {
        $is_core_patched = "NO";
      }

      $is_console_patched = "YES";
      $console_file_path = $symfonyDir . 'console/Output/Output.php';
      $console_search_string = ': void;';
      $console_file_content = file_get_contents($console_file_path);
      if (strpos($console_file_content, $console_search_string) !== false) {
        $is_console_patched = "NO";
      }

      if (drush_drupal_major_version() == 10) {
        $core_patch_fname = "drupal-ten-aegir-core-01.patch";
        $console_patch_fname = "drupal-ten-aegir-console-02.patch";
      }
      elseif (drush_drupal_major_version() == 11) {
        $core_patch_fname = "drupal-eleven-aegir-core-01.patch";
        $console_patch_fname = "drupal-eleven-aegir-console-02.patch";
      }

      if ($this_user == 'aegir' ) {
        $patchCoreTpl = '/var/aegir/patches/' . $core_patch_fname;
        $patchConsoleTpl = '/var/aegir/patches/' . $console_patch_fname;
      }
      else {
        $patchCoreTpl = '/data/conf/patches/' . $core_patch_fname;
        $patchConsoleTpl = '/data/conf/patches/' . $console_patch_fname;
      }

      $patchCoreFile = d()->root . '/' . $core_patch_fname;
      $patchConsoleFile = d()->root . '/' . $console_patch_fname;

      $webRoot = d()->root;
      $commandCore = sprintf('patch -d ' . $webRoot . ' -p1 < ' . $patchCoreTpl);
      $commandConsole = sprintf('patch -d ' . $symfonyDir . ' -p1 < ' . $patchConsoleTpl);

      if (provision_file()->exists($patchCoreTpl)->status() && $is_core_patched == 'NO') {
        drush_log(dt('CORE/PATCH/APPLY @patchCoreTpl in @webRoot', array('@patchCoreTpl' => $patchCoreTpl, '@webRoot' => $webRoot)), 'info');
        $success = drush_shell_exec($commandCore);
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
      if (provision_file()->exists($patchCoreFile)->status()) {
        provision_file()->unlink($patchCoreFile);
      }

      if (provision_file()->exists($patchConsoleTpl)->status() && $is_console_patched == 'NO') {
        drush_log(dt('CONSOLE/PATCH/APPLY @patchConsoleTpl in @symfonyDir', array('@patchConsoleTpl' => $patchConsoleTpl, '@symfonyDir' => $symfonyDir)), 'info');
        $success = drush_shell_exec($commandConsole);
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
      }
      if (provision_file()->exists($patchConsoleFile)->status()) {
        provision_file()->unlink($patchConsoleFile);
      }
    }
  }
  else {
    return TRUE;
  }
}
