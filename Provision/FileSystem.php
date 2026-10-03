<?php

class Provision_FileSystem extends Provision_ChainedState {
   /**
   * Copy file from $source to $destination.
   *
   * @param $source
   *   The path that you want copy.
   * @param $destination
   *   The destination path.
   */
  function copy($source, $destination) {
    $this->_clear_state();

    $this->tokens = array('@source' => $source, '@destination' => $destination);

    $this->last_status = FALSE;

    $this->last_status = copy($source, $destination);

    return $this;
  }


  /**
   * Determine if $path can be written to.
   *
   * Sets @path token for ->succeed and ->fail.
   *
   * @param $path
   *   The path you want to perform this operation on.
   */
  function writable($path) {
    $this->_clear_state();

    $this->last_status = is_writable($path);
    $this->tokens = array('@path' => $path);

    return $this;
  }

  /**
   * Determine if $path exists.
   *
   * Sets @path token for ->succeed and ->fail.
   *
   * @param $path
   *   The path you want to perform this operation on.
   */
  function exists($path) {
    $this->_clear_state();

    $this->last_status = file_exists($path) || is_link($path);
    $this->tokens = array('@path' => $path);

    return $this;
  }

  /**
   * Determine if $path is readable.
   *
   * Sets @path token for ->succeed and ->fail.
   *
   * @param $path
   *   The path you want to perform this operation on.
   */
  function readable($path) {
    $this->_clear_state();

    $this->last_status = is_readable($path);
    $this->tokens = array('@path' => $path);

    return $this;
  }

  /**
   * Create the $path directory.
   *
   * Sets @path token for ->succeed and ->fail.
   *
   * @param $path
   *   The path you want to perform this operation on.
   */
  function mkdir($path) {
    $this->_clear_state();

    $this->last_status = mkdir($path, 0775, TRUE);
    $this->tokens = array('@path' => $path);

    return $this;
  }

  /**
   * Delete the directory $path.
   *
   * Sets @path token for ->succeed and ->fail.
   *
   * @param $path
   *   The path you want to perform this operation on.
   */
  function rmdir($path) {
    $this->_clear_state();

    $this->last_status = rmdir($path);
    $this->tokens = array('@path' => $path);

    return $this;
  }

  /**
   * Delete the file $path.
   *
   * Sets @path token for ->succeed and ->fail.
   *
   * @param $path
   *   The path you want to perform this operation on.
   */
  function unlink($path) {
    $this->_clear_state();

    if (is_file($path) || is_link($path)) {
      $this->last_status = unlink($path);
    }
    else {
      $this->last_status = TRUE;
    }
    $this->tokens = array('@path' => $path);

    return $this;
  }

  /**
   * Change the file permissions of $path to the octal value in $perms.
   *
   * @param $perms
   *   An octal value denoting the desired file permissions.
   */
  function chmod($path, $perms, $recursive = FALSE) {
    $this->_clear_state();

    $this->tokens = array('@path' => $path, '@perm' => sprintf('%o', $perms));

    $func = ($recursive) ? array($this, '_chmod_recursive') : 'chmod';
    if (!@call_user_func($func, $path, $perms)) {
      $this->tokens['@reason'] = dt('chmod to @perm failed on @path', array('@perm' => sprintf('%o', $perms), '@path' => $path));
    }
    clearstatcache(); // this needs to be called, otherwise we get the old info 
    $this->last_status = substr(sprintf('%o', fileperms($path)), -4) == sprintf('%04o', $perms);

    return $this;
  }

  /**
   * Change the owner of $path to the user in $owner.
   *
   * Sets @path, @uid, and @reason tokens for ->succeed and ->fail.
   *
   * @param $path
   *   The path you want to perform this operation on.
   * @param $owner
   *   The name or user id you wish to change the file ownership to.
   * @param $recursive
   *   TRUE to descend into subdirectories.
   */
  function chown($path, $owner, $recursive = FALSE) {
    $this->_clear_state();
    $this->tokens = array('@path' => $path, '@uid' => $owner);

    // We do not attempt to chown symlinks.
    if (is_link($path)) {
      return $this;
    } 

    $func = ($recursive) ? array($this, '_chown_recursive') : 'chown';
    if ($owner = provision_posix_username($owner)) {
      if (!call_user_func($func, $path, $owner)) {
        $this->tokens['@reason'] = dt("chown to @owner failed on @path", array('@owner' => $owner, '@path' => $path)) ; 
      }
    }
    else {
      $this->tokens['@reason'] = dt("the user does not exist");
    }

    clearstatcache(); // this needs to be called, otherwise we get the old info 
    $this->last_status = $owner == provision_posix_username(fileowner($path));

    return $this;
  }

  /**
   * Change the group of $path to the group in $gid.
   *
   * Sets @path, @gid, and @reason tokens for ->succeed and ->fail.
   *
   * @param $path
   *   The path you want to perform this operation on.
   * @param $gid
   *   The name of group id you wish to change the file group ownership to.
   * @param $recursive
   *   TRUE to descend into subdirectories.
   */
  function chgrp($path, $gid, $recursive = FALSE) {
    $this->_clear_state();
    $this->tokens = array('@path' => $path, '@gid' => $gid);

    // We do not attempt to chown symlinks.
    if (is_link($path)) {
      return $this;
    } 

    $func = ($recursive) ? array($this, '_chgrp_recursive') : 'chgrp';
    if ($group = provision_posix_groupname($gid)) {
      if (provision_user_in_group(provision_current_user(), $gid)) {
        if (!call_user_func($func, $path, $group)) {
          $this->tokens['@reason'] = dt("chgrp to @group failed on @path", array('@group' => $group, '@path' => $path));
        }
      }
      else {
        $this->tokens['@reason'] = dt("@user is not in @group group", array("@user" => provision_current_user(), "@group" => $group));
      }
    }
    elseif (!@call_user_func($func, $path, $gid)) { # try to change the group anyways
      $this->tokens['@reason'] = dt("the group does not exist");
    }

    clearstatcache(); // this needs to be called, otherwise we get the old info 
    $this->last_status = $group == provision_posix_groupname(filegroup($path));

    return $this;
  }

  /**
   * Move $path1 to $path2, and vice versa.
   *
   * @param $path1
   *   The path that you want to replace the $path2 with.
   * @param $path2
   *   The path that you want to replace the $path1 with.
   */
  function switch_paths($path1, $path2) {
    $this->_clear_state();

    $this->tokens = array('@path1' => $path1, '@path2' => $path2);

    $this->last_status = FALSE;

    //TODO : Add error reasons.
    $temp = $path1 . '.tmp';
    if (!file_exists($path1)) {
      $this->last_status = rename($path2, $path1);
    }
    elseif (!file_exists($path2)) {
      $this->last_status = rename($path1, $path2);
    }
    elseif (rename($path1, $temp)) { 
      if (rename($path2, $path1)) {
        if (rename($temp, $path2)) {
          $this->last_status = TRUE; // path1 is now path2
        }
        else {
          // same .. just in reverse
          $this->last_status = rename($path1, $path2) && rename($temp, $path1);
        }
      }
      else {
        // same .. just in reverse
        $this->last_status = rename($temp, $path1);
      }   
    }

    return $this;
  }



  /**
   * Extract gzip-compressed tar archive.
   *
   * Sets @path, @target, and @reason tokens for ->succeed and ->fail.
   *
   * @param $path
   *   The path you want to extract.
   * @param $target
   *   The destination path to extract to.
   */
  function extract($path, $target) {
    $this->_clear_state();

    $this->tokens = array('@path' => $path, '@target' => $target);

    if (is_readable($path)) {
      if (is_link($target)) {
        // A link at the target, a dangling one included, passes the tests
        // below; mkdir and chdir then fail on it and tar unpacks into the
        // working directory instead. Refused before anything is written.
        $this->tokens['@reason'] = dt('@target is a symbolic link', array('@target' => $target));
        $this->last_status = FALSE;
      }
      elseif (is_writeable(dirname($target)) && !file_exists($target) && !is_dir($target)) {
        // Refuse before the first member is written when the archive cannot
        // fit: the extracted tree is never smaller than the archive that holds
        // it, so free space below the archive's own size is a certain failure.
        // Left to tar, that failure arrives minutes later as ENOSPC, after the
        // partial extraction has filled the filesystem for every other service
        // on the box (nginx and mysqld logged "No space left on device" while
        // a 74 GB clone was still unpacking). A lower bound only: a poorly
        // compressible archive can still fail later, and that case is then
        // reported in tar's own words below.
        $archive_bytes = @filesize($path);
        $free_bytes = @disk_free_space(dirname($target));
        if ($archive_bytes !== FALSE && $free_bytes !== FALSE && $free_bytes < $archive_bytes) {
          $this->tokens['@reason'] = dt('only @free MB free on the filesystem of @where, less than the @size MB archive itself; the extracted site cannot fit, free space first', array(
            '@free' => $this->_extract_mb($free_bytes),
            '@size' => $this->_extract_mb($archive_bytes),
            '@where' => dirname($target),
          ));
          $this->last_status = FALSE;
          return $this;
        }
        $this->mkdir($target);
        $made = $this->last_status;
        // mkdir() sets its own tokens; the messages here name @path and @target
        $this->tokens = array('@path' => $path, '@target' => $target);
        $oldcwd = getcwd();
        // resolved before chdir() moves the directory a relative target is
        // resolved against
        $expected = realpath(dirname($target)) . '/' . basename($target);
        // we need to do this because some retarded implementations of tar (e.g. SunOS) don't support -C
        // tar unpacks into the working directory: only ever the directory just
        // made, never one a link put in its place after the mkdir
        if (!$made || is_link($target) || !@chdir($target)
          || getcwd() !== $expected) {
          if (getcwd() !== $oldcwd) {
            chdir($oldcwd);
          }
          $this->tokens['@reason'] = dt('The target directory could not be made and entered');
          $this->last_status = FALSE;
          return $this;
        }

        // Decompression is driven by tar, not through a pipe, and the
        // decompressor is chosen from the suffix exactly as the backup side
        // chooses it when creating the archive.
        //
        // `gunzip -c %s | tar pxf -` reported only TAR's status, so a
        // corrupt or truncated archive whose decompressor died still looked
        // like a clean extraction: tar unpacks the prefix it received and
        // exits 0. And the old suffix ladder never matched anything but gz
        // -- `substr($path, -2) == 'bz2'` compares two characters against
        // three -- while the backup side can emit .tar.zst and .tar.lz4,
        // neither of which plain `tar -pxf` nor `tar -paxf` can read (tar
        // auto-detects zstd but NOT lz4; verified against GNU tar 1.35).
        if (substr($path, -3) == '.gz' || substr($path, -4) == '.tgz') {
          $command = 'tar -pxzf %s';
        }
        elseif (substr($path, -4) == '.bz2') {
          $command = 'tar -pxjf %s';
        }
        elseif (substr($path, -3) == '.xz') {
          $command = 'tar -pxJf %s';
        }
        elseif (substr($path, -4) == '.zst') {
          $command = 'tar -p -I zstd -xf %s';
        }
        elseif (substr($path, -4) == '.lz4') {
          $command = 'tar -p -I lz4 -xf %s';
        }
        else {
          $command = 'tar -pxf %s';
        }

        drush_log(dt('Running: %command in %target', array('%command' => sprintf($command, $path), '%target' => $target)), 'info');
        $result = drush_shell_exec($command, $path);
        $extract_output = drush_shell_exec_output();
        chdir($oldcwd);

        if ($result && is_writeable(dirname($target)) && is_readable(dirname($target)) && is_dir($target)) {
          $this->last_status = TRUE;
        }
        elseif (!$result && is_writeable(dirname($target)) && is_readable(dirname($target)) && is_dir($target) && is_file($target . '/settings.php') && $this->_extract_only_mknod_errors($extract_output)) {
          // tar could not recreate a device/block special MEMBER because mknod()
          // needs CAP_MKNOD, which the unprivileged Octopus/Aegir user lacks.
          // Every regular member (including settings.php) extracted fine, and a
          // special file has no place in a Drupal site dir, so treat this exact,
          // fully-diagnosed case as non-fatal instead of rolling back the whole
          // deploy. Any OTHER tar error still fails hard in the else below. This
          // also stops the poison propagating: the special member is dropped, so
          // the freshly extracted platform copy is clean.
          foreach ($extract_output as $extract_line) {
            if (trim($extract_line) !== '') {
              drush_log(dt('Extraction skipped an un-creatable special file member (harmless; not valid Drupal content): @l', array('@l' => $extract_line)), 'warning');
            }
          }
          $this->last_status = TRUE;
        }
        else {
          // Say why in tar's own words. The output was captured all along but
          // only ever printed under --debug, so a failed deploy, clone, migrate
          // or restore reported nothing beyond "could not be extracted" -- a
          // 442-second extraction that died on a full disk read exactly like a
          // corrupt archive. The first line (usually the earliest error) rides
          // the failure message; the rest are logged, capped so an ENOSPC that
          // fails every remaining member cannot flood the task log.
          $first_line = provision_log_tar_output($extract_output, 'Extraction error');
          $reason = dt('The file could not be extracted');
          if ($first_line !== '') {
            $reason .= dt(': @first', array('@first' => $first_line));
          }
          $free_bytes = @disk_free_space(dirname($target));
          if ($free_bytes !== FALSE) {
            $reason .= dt('; @free MB free on the filesystem of @where now', array(
              '@free' => $this->_extract_mb($free_bytes),
              '@where' => dirname($target),
            ));
          }
          $this->tokens['@reason'] = $reason;
          $this->last_status = FALSE;
        }
      }
      else {
        $this->tokens['@reason'] = dt('The target directory could not be written to');
        $this->last_status = FALSE;
      }
    }
    else {
      $this->tokens['@reason'] = dt('Backup file could not be opened');
      $this->last_status = FALSE;
    }

    return $this;
  }

  /**
   * Whole megabytes for a byte count, for the space messages above.
   */
  function _extract_mb($bytes) {
    return (int) floor($bytes / 1048576);
  }

  /**
   * TRUE only when every non-empty line of a FAILED tar extraction is a
   * 'Cannot mknod' notice (a device/block special member the unprivileged
   * Octopus/Aegir user cannot recreate). Lets extract() tolerate that one
   * harmless case while any genuine extraction error still fails hard.
   * Fail-closed: an empty/non-array output (e.g. a build that does not capture
   * stderr) returns FALSE, preserving the existing hard-fail behaviour.
   */
  function _extract_only_mknod_errors($output) {
    if (empty($output) || !is_array($output)) {
      return FALSE;
    }
    $saw_mknod = FALSE;
    foreach ($output as $line) {
      $line = trim($line);
      if ($line === '') {
        continue;
      }
      // tar's trailing summary line is not itself an error to weigh.
      if (strpos($line, 'Exiting with failure status') !== FALSE) {
        continue;
      }
      if (strpos($line, 'Cannot mknod') !== FALSE) {
        $saw_mknod = TRUE;
        continue;
      }
      // Any other non-empty line is an unexpected error -> not tolerable.
      return FALSE;
    }
    return $saw_mknod;
  }

  /**
   * Creates a symbolic link to the existing target with the specified name.
   *
   * Sets @path, @target, and @reason tokens for ->succeed and ->fail.
   *
   * @param $target
   *   The existing path you want the link to point to.
   * @param $path
   *   The path of the link to create.
   */
  function symlink($target, $path) {
    $this->_clear_state();

    $this->tokens = array('@target' => $target, '@path' => $path);

    if (file_exists($path) && !is_link($path)) {
      $this->tokens['@reason'] = dt("A file already exists at @path");
      $this->last_status = FALSE;
    }
    elseif (is_link($path) && (readlink($path) != $target)) {
      $this->tokens['@reason'] = dt("A symlink already exists at target, but it is pointing to @link", array("@link" => readlink($path)));
      $this->last_status = FALSE;
    }
    elseif (is_link($path) && (readlink($path) == $target)) {
      $this->last_status = TRUE;
    }
    elseif (symlink($target, $path)) {
      $this->last_status = TRUE;
    }
    else {
      $this->tokens['@reason'] = dt('The symlink could not be created, an error has occured');
      $this->last_status = FALSE;
    }

    return $this;
  }

  /**
   * Small helper function for creation of configuration directories.
   */
  function create_dir($path, $name, $perms) {
    $exists = $this->exists($path)
      ->succeed($name . ' path @path exists.')
      ->status();

    if (!$exists) {
      $exists = $this->mkdir($path)
        ->succeed($name . ' path @path has been created.')
        ->fail($name . ' path @path could not be created.', 'DRUSH_PERM_ERROR')
        ->status();
    }

    if ($exists) {
      $this->chown($path, provision_current_user())
        ->succeed($name . ' ownership of @path has been changed to @uid.')
        ->fail($name . ' ownership of @path could not be changed to @uid.', 'DRUSH_PERM_ERROR');

      $this->chmod($path, $perms)
        ->succeed($name . ' permissions of @path have been changed to @perm.')
        ->fail($name . ' permissions of @path could not be changed to @perm.', 'DRUSH_PERM_ERROR');

      $this->writable($path)
        ->succeed($name . ' path @path is writable.')
        ->fail($name . ' path @path is not writable.', 'DRUSH_PERM_ERROR');
    }

    return $exists;
  }

  /**
   * Write $data to $path.
   *
   * Sets @path token for ->succeed and ->fail.
   *
   * @param $path
   *   The path you want to perform this operation on.
   * @param $data
   *   The data to write.
   * @param $flags
   *   The file_put_contents() flags to use.
   *
   * @see file_put_contents()
   */
  function file_put_contents($path, $data, $flags = 0) {
    $this->_clear_state();

    $this->tokens = array('@path' => $path);
    $this->last_status = file_put_contents($path, $data, $flags) !== FALSE;

    return $this;
  }

  /**
   * Create the directories $names, one below the other, from the physical
   * directory $dir, where a caller judged the path to be.
   *
   * $dir is entered for real first, and each name is made there by name
   * (mkdir() never follows a link) and entered for real before the next (see
   * _enter()), so a link put on the way since the path was judged is never
   * followed. A name that exists already is entered the same way.
   *
   * Sets @path and @reason tokens for ->succeed and ->fail.
   *
   * @param $dir
   *   The physical path (realpath()) of the deepest directory that exists,
   *   or NULL when the path resolves nowhere: then nothing is made, and the
   *   operation fails as mkdir() fails through a dangling link.
   * @param $names
   *   The names to make below it, in order.
   * @param $path
   *   The path this stands for, for the log.
   */
  function mkdir_in($dir, $names, $path) {
    $this->_clear_state();
    $this->tokens = array('@path' => $path);
    if ($dir === NULL) {
      $this->last_status = FALSE;
      return $this;
    }

    $cwd = getcwd();
    $status = $this->_enter($dir);
    $moved = !$status;
    foreach ($names as $name) {
      if (!$status) {
        break;
      }
      if (!@mkdir($name, 0775) && !file_exists($name) && !is_link($name)) {
        $status = FALSE;
        break;
      }
      $dir .= '/' . $name;
      $status = $this->_enter($dir, $name);
      $moved = !$status;
    }
    $this->_leave($cwd);
    if ($moved) {
      $this->tokens['@reason'] = dt('@path is not where it was checked', array('@path' => $path));
    }
    $this->last_status = $status;

    return $this;
  }

  /**
   * Change the group of the physical directory $dir, and with $recursive of
   * everything below it, as chgrp() does for a path a caller judged: $dir is
   * entered for real (see _enter()) and the change is made there, on '.' or
   * by the walk of _here_recursive().
   *
   * Sets @path, @gid, and @reason tokens for ->succeed and ->fail.
   *
   * @param $dir
   *   The physical path (realpath()) of the directory, or NULL when the path
   *   resolves nowhere: then nothing is changed, and the operation fails as
   *   chgrp() fails on a missing path.
   * @param $gid
   *   The name of group id you wish to change the file group ownership to.
   * @param $recursive
   *   TRUE to descend into subdirectories.
   * @param $path
   *   The path this stands for, for the log.
   */
  function chgrp_in($dir, $gid, $recursive, $path) {
    $this->_clear_state();
    $this->tokens = array('@path' => $path, '@gid' => $gid);
    if ($dir === NULL) {
      $group = provision_posix_groupname($gid);
      $this->tokens['@reason'] = dt("chgrp to @group failed on @path", array('@group' => $group ? $group : $gid, '@path' => $path));
      $this->last_status = FALSE;
      return $this;
    }

    $cwd = getcwd();
    if (!$this->_enter($dir)) {
      $this->_leave($cwd);
      $this->tokens['@reason'] = dt('@path is not where it was checked', array('@path' => $path));
      $this->last_status = FALSE;
      return $this;
    }
    if ($group = provision_posix_groupname($gid)) {
      if (provision_user_in_group(provision_current_user(), $gid)) {
        $done = $recursive ? $this->_here_recursive('chgrp', $group) : chgrp('.', $group);
        if (!$done) {
          $this->tokens['@reason'] = dt("chgrp to @group failed on @path", array('@group' => $group, '@path' => $path));
        }
      }
      else {
        $this->tokens['@reason'] = dt("@user is not in @group group", array("@user" => provision_current_user(), "@group" => $group));
      }
    }
    else {
      $done = $recursive ? $this->_here_recursive('chgrp', $gid) : @chgrp('.', $gid);
      if (!$done) {
        $this->tokens['@reason'] = dt("the group does not exist");
      }
    }

    clearstatcache();
    $this->last_status = $this->_enter($dir) && $group == provision_posix_groupname(filegroup('.'));
    $this->_leave($cwd);

    return $this;
  }

  /**
   * Change the mode of the physical directory $dir to $perms, and with
   * $recursive of everything below it, as chmod() does for a path a caller
   * judged: $dir is entered for real (see _enter()) and the change is made
   * there, on '.' or by the walk of _here_recursive().
   *
   * Sets @path, @perm, and @reason tokens for ->succeed and ->fail.
   *
   * @param $dir
   *   The physical path (realpath()) of the directory, or NULL when the path
   *   resolves nowhere: then nothing is changed, and the operation fails as
   *   chmod() fails on a missing path.
   * @param $perms
   *   An octal value denoting the desired file permissions.
   * @param $recursive
   *   TRUE to descend into subdirectories.
   * @param $path
   *   The path this stands for, for the log.
   */
  function chmod_in($dir, $perms, $recursive, $path) {
    $this->_clear_state();
    $this->tokens = array('@path' => $path, '@perm' => sprintf('%o', $perms));
    if ($dir === NULL) {
      $this->tokens['@reason'] = dt('chmod to @perm failed on @path', array('@perm' => sprintf('%o', $perms), '@path' => $path));
      $this->last_status = FALSE;
      return $this;
    }

    $cwd = getcwd();
    if (!$this->_enter($dir)) {
      $this->_leave($cwd);
      $this->tokens['@reason'] = dt('@path is not where it was checked', array('@path' => $path));
      $this->last_status = FALSE;
      return $this;
    }
    if ($recursive) {
      $done = $this->_here_recursive('chmod', $perms);
    }
    else {
      $done = @chmod('.', $perms);
    }
    if (!$done) {
      $this->tokens['@reason'] = dt('chmod to @perm failed on @path', array('@perm' => sprintf('%o', $perms), '@path' => $path));
    }
    clearstatcache();
    $this->last_status = $this->_enter($dir) && substr(sprintf('%o', fileperms('.')), -4) == sprintf('%04o', $perms);
    $this->_leave($cwd);

    return $this;
  }

  /**
   * Enter the directory $dir for real: chdir() there, or to $name from the
   * current directory when given, then TRUE only while getcwd() names $dir,
   * the physical path expected. A link put on the way is followed by
   * chdir() but never passes the test, and what is then done on '.' or on a
   * name there is done in that very directory, wherever it is renamed to.
   */
  function _enter($dir, $name = NULL) {
    return @chdir($name === NULL ? $dir : $name) && getcwd() === $dir;
  }

  /**
   * Go back to the working directory $cwd, as getcwd() returned it.
   */
  function _leave($cwd) {
    if ($cwd !== FALSE) {
      @chdir($cwd);
    }
  }

  /**
   * Walk the current directory depth first, calling $func (chgrp or chmod)
   * with $arg on everything below it and then on '.', as _call_recursive()
   * walks a path: links are neither followed nor changed.
   *
   * Each directory below is entered for real by name (_enter()) and the walk
   * comes back to the physical path it left, and every other entry is
   * changed by name in the directory it was read from, so a link put on the
   * way during the walk is never followed. The group goes through lchgrp(),
   * which does not follow a link put in an entry's place since it was read;
   * PHP has no lchmod(), so a file swapped for a link between its test and
   * chmod() is followed.
   *
   * @return
   *   TRUE if every call returned TRUE.
   */
  function _here_recursive($func, $arg) {
    $here = getcwd();
    if ($here === FALSE) {
      return FALSE;
    }
    $status = TRUE;
    if ($dh = @opendir('.')) {
      $names = array();
      while (($name = readdir($dh)) !== FALSE) {
        if ($name !== '.' && $name !== '..') {
          $names[] = $name;
        }
      }
      closedir($dh);
      foreach ($names as $name) {
        if (is_link($name)) {
          continue;
        }
        if (is_dir($name)) {
          $status = $this->_enter($here . '/' . $name, $name) && $this->_here_recursive($func, $arg) && $status;
          if (!$this->_enter($here)) {
            return FALSE;
          }
        }
        elseif ($func == 'chgrp') {
          $status = lchgrp($name, $arg) && $status;
        }
        else {
          $status = call_user_func($func, $name, $arg) && $status;
        }
      }
    }
    $status = call_user_func($func, '.', $arg) && $status;
    if (!$status) {
      drush_log(dt('Failed calling :func on :path.', array(':func' => $func . '()', ':path' => $here)), 'debug');
    }
    return $status;
  }

  /**
   * Walk the given tree recursively (depth first), calling a function on each file
   *
   * $func is not checked for existence and called directly with $path and $arg
   * for every file encountered.
   *
   * @param string $func a valid callback, usually chmod, chown or chgrp
   * @param string $path a path in the filesystem
   * @param string $arg the second argument to $func
   * @return boolean returns TRUE if every $func call returns true
   */
  function _call_recursive($func, $path, $arg) {
    $status = 1;
    // do not follow symlinks as it could lead to a DOS attack
    // consider someone creating a symlink from files/foo to ..: it would create an infinite loop
    if (!is_link($path)) {
      if ($dh = @opendir($path)) {
        while (($file = readdir($dh)) !== false) {
          if ($file != '.' && $file != '..') {
            $status = $this->_call_recursive($func, $path . "/" . $file, $arg) && $status;
          }
        }
        closedir($dh);
      }
      $status = call_user_func($func, $path, $arg) && $status;
    }
    if (!$status) {
      drush_log(dt('Failed calling :func on :path.', array(':func' => $func . '()', ':path' => $path)), 'debug');
    }
    return $status;
  }

  /**
   * Chmod a directory recursively
   *
   */
  function _chmod_recursive($path, $filemode) {
    return $this->_call_recursive('chmod', $path, $filemode);
  }

  /**
   * Chown a directory recursively
   */
  function _chown_recursive($path, $owner) {
    return $this->_call_recursive('chown', $path, $owner);
  }

  /**
   * Chgrp a directory recursively
   */
  function _chgrp_recursive($path, $group) {
    return $this->_call_recursive('chgrp', $path, $group);
  }


}
