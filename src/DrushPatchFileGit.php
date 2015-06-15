<?php

class DrushPatchFileGit {

  const PATCH_APPLIED = 'applied';
  const PATCH_UNAPPLIED = 'unapplied';
  const PATCH_UNDETERMINED = 'undetermined';

  public static function getPatch(array $patch) {
    static $cache = array();

    if (!isset($cache[$patch['url']])) {
      if (!empty($patch['local'])) {
        if (is_file($patch['url']) && filesize($patch['url'])) {
          $cache[$patch['url']] = $patch['url'];
        }
        else {
          throw new Exception("Unable to read patch from local path {$patch['url']}.");
        }
      }
      elseif (drush_get_option('no-cache')) {
        $temp_file = drush_tempnam('drush_patchfile_', NULL, '.patch');
        $cache[$patch['url']] = static::downloadPatch($patch['url'], $temp_file);
      }
      else {
        $cache_file = drush_directory_cache('patchfile') . '/' . md5($patch['url']) . '.patch';
        if (is_file($cache_file) && filectime($cache_file) > ($_SERVER['REQUEST_TIME'] - DRUSH_CACHE_LIFETIME_DEFAULT)) {
          drush_log(dt('Remote patch URL @url fetched from cache file @cache.', array('@url' => $patch['url'], '@cache' => $cache_file)));
          $cache[$patch['url']] = $cache_file;
        }
        else {
          $cache[$patch['url']] =static::downloadPatch($patch['url'], $cache_file);
        }
      }
    }

    return $cache[$patch['url']];
  }

  public static function downloadPatch($url, $destination) {
    if ($downloaded = _drush_download_file($url, $destination, TRUE)) {
      if (!filesize($downloaded)) {
        throw new Exception("Remote patch {$url} downloaded as empty file {$destination}.");
      }
      else {
        drush_log(dt('Remote patch @url downloaded to @file.', array('@url' => $url, '@file' => $downloaded)));
        return $downloaded;
      }
    }
    else {
      throw new Exception("Unable to download or fetch remote patch from {$url} and save to {$destination}.");
    }
  }

  public static function checkPatch($directory, array $patch) {
    $patch_filename = static::getPatch($patch);

    $patch['status'] = static::PATCH_UNDETERMINED;

    // Test each patch style; -p1 is the default with git. See
    // http://drupal.org/node/1054616
    $patch_levels = array('-p1', '-p0');
    foreach ($patch_levels as $patch_level) {

      // Test if the patch can be reverted, which would mean it is applied.
      if (static::execute($directory, 'git apply --git-dir=. --check -R %s %s', $patch_level, $patch_filename)) {
        $patch['status'] = static::PATCH_APPLIED;
        $patch['method'] = 'git apply';
        $patch['level'] = $patch_level;
        break;
      }

      // Test if the patch can be re-applied.
      if (static::execute($directory, 'git apply --git-dir=.--check %s %s', $patch_level, $patch_filename)) {
        $patch['status'] = static::PATCH_UNAPPLIED;
        $patch['method'] = 'git apply';
        $patch['level'] = $patch_level;
        break;
      }
    }

    // In some rare cases, git will fail to apply a patch, fallback to using
    // the 'patch' command.
    if ($patch['status'] === static::PATCH_UNDETERMINED) {
      foreach ($patch_levels as $patch_level) {

        // Test if the patch can be reverted, which would mean it is applied.
        if (static::execute($directory, "patch %s -R --dry-run < %s", $patch_level, $patch_filename)) {
          $patch['status'] = static::PATCH_APPLIED;
          $patch['method'] = 'patch';
          $patch['level'] = $patch_level;
          break;
        }

        // Test if the patch can be re-applied.
        if (static::execute($directory, "patch %s --dry-run < %s", $patch_level, $patch_filename)) {
          $patch['status'] = static::PATCH_UNAPPLIED;
          $patch['method'] = 'patch';
          $patch['level'] = $patch_level;
          break;
        }
      }
    }

    return $patch;
  }

  public static function applyPatch($directory, array $patch) {
    $patch = static::checkPatch($directory, $patch);

    switch ($patch['status']) {
      case static::PATCH_APPLIED:
        throw new Exception("Patch {$patch['url']} already applied to $directory.");

      case static::PATCH_UNDETERMINED;
        throw new Exception("Cannot apply patch {$patch['url']} to $directory.");

      case static::PATCH_UNAPPLIED:
        $result = FALSE;
        $patch_filename = static::getPatch($patch);

        switch ($patch['method']) {
          case 'git apply':
            $result = static::execute($directory, 'git apply --git-dir=. %s %s', $patch['level'], $patch_filename);
            break;

          case 'patch':
            $result = static::execute($directory,  "patch %s < %s", $patch['level'], $patch_filename);
            break;
        }

        return $result;
    }
  }

  public static function execute($directory, $command) {
    $args = func_get_args();
    $result = call_user_func_array('drush_shell_cd_and_exec', $args);
    if (drush_get_context('DRUSH_DEBUG') && $output = drush_shell_exec_output()) {
      // Log any command output, visible only in --debug mode.
      //drush_print(implode("\n", $output));
    }
    if (drush_get_context('DRUSH_VERBOSE') || drush_get_context('DRUSH_SIMULATE')) {
      drush_print('Result: ' . var_export($result, TRUE), 0, STDERR);
    }
    return $result;
  }

}
