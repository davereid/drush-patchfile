<?php

class DrushPatchFileGit {

  const PATCH_APPLIED = 'applied';
  const PATCH_UNAPPLIED = 'unapplied';
  const PATCH_UNDETERMINED = 'undetermined';

  public static function getPatch(array $patch) {
    $filename = _make_download_file($patch['url']);
    if (!$filename) {
      throw new Exception("Unable to download or fetch patch from {$patch['url']}.");
    }
    return $filename;
  }

  public static function checkPatch($directory, array $patch) {
    $patch_filename = static::getPatch($patch);

    $patch['status'] = static::PATCH_UNDETERMINED;

    // Test each patch style; -p1 is the default with git. See
    // http://drupal.org/node/1054616
    $patch_levels = array('-p1', '-p0');
    foreach ($patch_levels as $patch_level) {

      // Test if the patch can be reverted, which would mean it is applied.
      if (static::execute($directory, 'GIT_DIR=. git apply --check -R %s %s', $patch_level, $patch_filename)) {
        $patch['status'] = static::PATCH_APPLIED;
        $patch['method'] = 'git apply';
        $patch['level'] = $patch_level;
        break;
      }

      // Test if the patch can be re-applied.
      if (static::execute($directory, 'GIT_DIR=. git apply --check %s %s', $patch_level, $patch_filename)) {
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
            $result = static::execute($directory, 'GIT_DIR=. git apply %s %s', $patch['level'], $patch_filename);
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
