<?php

/**
 * Drush hooks for the provision-dunlock command
 */

function genRandStr($length = 12) {
  return substr(str_shuffle(str_repeat($x='0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ', ceil($length/strlen($x)) )),1,$length);
}

function drush_provision_dunlock() {

  if (provision_file()->exists(d()->root)->status() &&
    !preg_match("/\/aegir\/distro\//gi", d()->root) &&
    !preg_match("/\/var\/aegir\//gi", d()->root)) {

    $dru_vnd_drush_local = d()->root . '/vendor/drush';
    $dru_vnd_drush_above = d()->root . '/../vendor/drush';
    $dru_vnd_symf_con_inp_loc = d()->root . '/vendor/symfony/console/Input';
    $dru_vnd_symf_con_inp_abo = d()->root . '/../vendor/symfony/console/Input';
    $dru_vnd_symf_con_sty_loc = d()->root . '/vendor/symfony/console/Style';
    $dru_vnd_symf_con_sty_abo = d()->root . '/../vendor/symfony/console/Style';

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

    if (provision_file()->exists($dru_vnd_symf_con_inp_loc)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_inp_loc, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_inp_abo)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_inp_abo, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($dru_vnd_symf_con_sty_loc)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_sty_loc, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }
    if (provision_file()->exists($dru_vnd_symf_con_sty_abo)->status()) {
      provision_file()->chmod($dru_vnd_symf_con_sty_abo, 0775)
      ->succeed('Changed permissions of <code>@path</code> to @perm')
      ->fail('Could not change permissions of <code>@path</code> to @perm');
    }

    if (provision_file()->exists($bin_vnd_drush_local)->status()) {
      provision_file()->unlink($bin_vnd_drush_local)
      ->succeed('Removed Problematic vendor/bin/drush in provision_unlock_drush_local_vnd')
      ->fail('Could not remove problematic vendor/bin/drush in provision_unlock_drush_local_vnd');
    }
    if (provision_file()->exists($bin_vnd_drush_above)->status()) {
      provision_file()->unlink($bin_vnd_drush_above)
      ->succeed('Removed Problematic vendor/bin/drush in provision_unlock_drush_local_vnd')
      ->fail('Could not remove problematic vendor/bin/drush in provision_unlock_drush_local_vnd');
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

    if (provision_file()->exists($dru_vnd_symf_con_inp_loc)->status()) {
      if (!provision_file()->exists($dru_dru_dru_php_local)->status()) {
        $approot = d()->root;
        $result = drush_shell_cd_and_exec($approot, 'composer require drush/drush --no-interaction --no-progress');
        $output = implode('', drush_shell_exec_output());
        drush_log("DRUSH/DRUSH " . $output);
      }
    }
    elseif (provision_file()->exists($dru_vnd_symf_con_inp_abo)->status()) {
      if (!provision_file()->exists($dru_dru_dru_php_above)->status()) {
        $approot = d()->root . "/../";
        $result = drush_shell_cd_and_exec($approot, 'composer require drush/drush --no-interaction --no-progress');
        $output = implode('', drush_shell_exec_output());
        drush_log("DRUSH/DRUSH " . $output);
      }
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

      if (drush_drupal_major_version() == 10) {
        $core_patch_fname = "drupal-ten-aegir-core-01.patch";
        $console_patch_fname = "drupal-ten-aegir-console-02.patch";
      }
      elseif (drush_drupal_major_version() == 11) {
        $core_patch_fname = "drupal-eleven-aegir-core-01.patch";
        $console_patch_fname = "drupal-eleven-aegir-console-02.patch";
        $validator_patch_fname = "drupal-eleven-aegir-validator-03.patch";
      }

      if ($this_user == 'aegir' ) {
        $patchCoreTpl = '/var/aegir/patches/' . $core_patch_fname;
        $patchConsoleTpl = '/var/aegir/patches/' . $console_patch_fname;
        $patchValidatorTpl = '/var/aegir/patches/' . $validator_patch_fname;
      }
      else {
        $patchCoreTpl = '/data/conf/patches/' . $core_patch_fname;
        $patchConsoleTpl = '/data/conf/patches/' . $console_patch_fname;
        $patchValidatorTpl = '/data/conf/patches/' . $validator_patch_fname;
      }

      $patchCoreFile = d()->root . '/' . $core_patch_fname;
      $patchConsoleFile = d()->root . '/' . $console_patch_fname;
      $patchValidatorFile = d()->root . '/' . $validator_patch_fname;

      provision_check_codebase_status();

      $commandCore = sprintf('patch -d ' . d()->root . ' -R -p1 < ' . $patchCoreTpl);
      $commandConsole = sprintf('patch -d ' . d()->symfonyDir . ' -R -p1 < ' . $patchConsoleTpl);
      $commandValidator = sprintf('patch -d ' . d()->root . ' -R -p1 < ' . $patchValidatorTpl);

      drush_log(dt('CORE/PATCH/IF3 --@isCorePatched-- in @webRoot', array('@isCorePatched' => d()->isCorePatched, '@webRoot' => d()->root)), 'info');
      drush_log(dt('CONSOLE/PATCH/IF3 --@isConsolePatched-- in @symfonyDir', array('@isConsolePatched' => d()->isConsolePatched, '@symfonyDir' => d()->symfonyDir)), 'info');
      drush_log(dt('VALIDATOR/PATCH/IF3 --@isValidatorPatched-- in @webRoot', array('@isValidatorPatched' => d()->isValidatorPatched, '@webRoot' => d()->root)), 'info');

      if (provision_file()->exists($patchCoreTpl)->status() && d()->isCorePatched) {
        drush_log(dt('CORE/PATCH/REVERT @patchCoreTpl in @webRoot', array('@patchCoreTpl' => $patchCoreTpl, '@webRoot' => d()->root)), 'info');
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
      }
      if (provision_file()->exists($patchCoreFile)->status()) {
        provision_file()->unlink($patchCoreFile);
      }

      if (provision_file()->exists($patchConsoleTpl)->status() && d()->isConsolePatched) {
        drush_log(dt('CONSOLE/PATCH/REVERT @patchConsoleTpl in @symfonyDir', array('@patchConsoleTpl' => $patchConsoleTpl, '@symfonyDir' => d()->symfonyDir)), 'info');
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

      if (provision_file()->exists($patchValidatorTpl)->status() && d()->isValidatorPatched) {
        drush_log(dt('VALIDATOR/PATCH/REVERT @patchValidatorTpl in @webRoot', array('@patchValidatorTpl' => $patchValidatorTpl, '@webRoot' => d()->root)), 'info');
        $success = drush_shell_exec($commandValidator);
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
    }
  }
  else {
    return TRUE;
  }
}
