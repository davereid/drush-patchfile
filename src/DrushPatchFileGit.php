<?php

class DrushPatchFileGit {

  public static function checkPatch($directory, $patch_filename) {
    $status = array(
      'applied' => NULL,
    );

    // Test each patch style; -p1 is the default with git. See
    // http://drupal.org/node/1054616
    $patch_levels = array('-p1', '-p0');
    foreach ($patch_levels as $patch_level) {

      // Test if the patch can be reverted, which would mean it is applied.
      if (static::execute($directory, 'GIT_DIR=. git apply --check -R %s %s', $patch_level, $patch_filename)) {
        $status['applied'] = TRUE;
        $status['method'] = 'git apply';
        $status['level'] = $patch_level;
        break;
      }

      // Test if the patch can be re-applied.
      if (static::execute($directory, 'GIT_DIR=. git apply --check %s %s', $patch_level, $patch_filename)) {
        $status['applied'] = FALSE;
        $status['method'] = 'git apply';
        $status['level'] = $patch_level;
        break;
      }
    }

    // In some rare cases, git will fail to apply a patch, fallback to using
    // the 'patch' command.
    if (!isset($status['applied'])) {
      foreach ($patch_levels as $patch_level) {

        // Test if the patch can be reverted, which would mean it is applied.
        if (static::execute($directory, "patch %s -R --dry-run < %s", $patch_level, $patch_filename)) {
          $status['applied'] = TRUE;
          $status['method'] = 'patch';
          $status['level'] = $patch_level;
          break;
        }

        // Test if the patch can be re-applied.
        if (static::execute($directory, "patch %s --dry-run < %s", $patch_level, $patch_filename)) {
          $status['applied'] = FALSE;
          $status['method'] = 'patch';
          $status['level'] = $patch_level;
          break;
        }
      }
    }

    return $status;
  }

  public static function applyPatch($directory, $patch_filename) {
    $status = static::checkPatch($directory, $patch_filename);
    if ($status['applied'] !== FALSE) {
      throw new Exception("Cannot apply patch $patch_filename to $directory.");
    }

    switch ($status['method']) {
      case 'git apply':
        return static::execute($directory, 'GIT_DIR=. git apply %s %s', $status['level'], $patch_filename);

      case 'patch':
        return static::execute($directory,  "patch %s < %s", $status['level'], $patch_filename);
    }

    return FALSE;
  }

  public static function execute($directory, $command) {
    $args = func_get_args();
    $result = call_user_func_array('drush_shell_cd_and_exec', $args);
    if (drush_get_context('DRUSH_DEBUG') && $output = drush_shell_exec_output()) {
      // Log any command output, visible only in --debug mode.
      drush_print(implode("\n", $output));
    }
    if (drush_get_context('DRUSH_VERBOSE') || drush_get_context('DRUSH_SIMULATE')) {
      drush_print('Result: ' . var_export($result, TRUE), 0, STDERR);
    }
    return $result;
  }

}
