<?php

/**
 * @file
 * Drush integration for patch make files.
 */

/**
 * Implements hook_drush_command().
 */
function patchfile_drush_command() {
  $items['patch-status'] = array(
    'description' => 'List the patches that should be applied to the site.',
    'options' => array(
      'patch-file' => 'Filename of the patch makefile to use.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
    'aliases' => array('ps'),
  );
  $items['patch-project'] = array(
    'description' => 'List the patches that should be applied to the site.',
    'arguments' => array(
      'project-name' => 'The name of the module, theme, profile, or \'core\' to re-apply patches to.',
      'project-directory' => 'Optional, the directory of the project to use for the patches.',
    ),
    'required-arguments' => 1,
    'options' => array(
      'patch-file' => 'Filename of the patch makefile to use.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,

    'aliases' => array('pp'),
  );

  return $items;
}

function drush_patchfile_patch_status() {
  $projects = drush_patchfile_get_patched_projects();

  if (empty($projects)) {
    return drush_print(dt('No patches found.'));
  }

  $rows = array();
  $rows[] = array(dt('Project'), dt('Patch'), dt('Status'));

  foreach ($projects as $project => $project_info) {
    foreach ($project_info['patches'] as $patch) {
      $rows[] = array($project, basename($patch), 'Unknown');
    }
  }

  drush_print_table($rows);
}

function drush_patchfile_get_patched_projects($patchfile = NULL, $use_cache = TRUE) {
  if (!isset($patchfile)) {
    $patchfile = drush_get_option('patch-file');
  }

  $projects = array();

  if (!empty($patchfile)) {
    // Cache not only by filename, but also by the time the file was modified
    // so that a drush cache clear is not necessary to pick up changes.
    $cid = drush_get_cid('patchfile', array(), array($patchfile, filemtime($patchfile)));
    if ($use_cache && $cache = drush_cache_get($cid)) {
      $projects = $cache->data;
    }
    else {
      $projects = drush_patchfile_parse_info_file($patchfile);
      $projects = drush_patchfile_validate_projects($projects);
      drush_cache_set($cid, $projects);
    }
  }

  return $projects;
}

function drush_patchfile_get_patched_project($project) {
  $projects = drush_patchfile_get_patched_projects();
  return isset($projects[$project]) ? $projects[$project] : FALSE;
}

function drush_patchfile_parse_info_file($file) {
  $return = array();

  // Parse the patch make file.
  $info = make_parse_info_file($file);

  if (!empty($info['projects'])) {
    foreach ($info['projects'] as $project => $project_info) {
      // Only return projects that have patches listed.
      if (!empty($project_info['patch']) && is_array($project_info['patch'])) {
        $return[$project]['patches'] = $project_info['patch'];
      }
    }
  }

  return $return;
}

function drush_patchfile_validate_projects(array $projects) {
  $return = $projects;

  foreach ($projects as $project => $project_info) {
    if ($extra = drush_patchfile_add_project_info($project)) {
      $return[$project] += $extra;
    }
    else {
      drush_set_error('DRUSH_PROJECT_NOT_FOUND', dt("Patches provided for non-existant project !project.", array('!project' => $project)));
      unset($return[$project]);
    }
  }

  return $return;
}

/**
 * Return the directory of the project.
 */
function drush_patchfile_add_project_info($project) {
  $info = array();

  $drupal_root = drush_get_context('DRUSH_DRUPAL_ROOT');

  if ($project == 'drupal') {
    $info['type'] = 'core';
    $info['dir'] = $drupal_root;
    return $info;
  }

  // Get all possible modules, themes, and profiles.
  drush_include_engine('drupal', 'environment');

  $modules = drush_get_modules();
  if (isset($modules[$project])) {
    $info['type'] = 'module';
    $info['dir'] = $drupal_root . '/' . dirname($modules[$project]->filename);
    return $info;
  }

  $themes = drush_get_themes();
  if (isset($themes[$project])) {
    $info['type'] = 'theme';
    $info['dir'] = $drupal_root . '/' . dirname($themes[$project]->filename);
    return $info;
  }

  $profiles = drush_find_profiles($drupal_root);
  if (isset($profiles[$project])) {
    $info['type'] = 'profile';
    $info['dir'] = $drupal_root . '/' . dirname($profiles[$project]->filename);
    return $info;
  }

  return FALSE;
}

function patchfile_drush_pm_post_download($request, $release) {
/*
$request = array (
  'name' => 'search404',
  'version' => NULL,
  'drupal_version' => '7.x',
  'project_version' => NULL,
  'status url' => 'http://updates.drupal.org/release-history',
  'project_type' => 'module',
  'project_dir' => 'search404',
  'base_project_path' => 'search404',
  'full_project_path' => '/home/dave/CloudStation/projects/lullabot/lullabot.com/search404',
  'project_install_location' => '/home/dave/CloudStation/projects/lullabot/lullabot.com/search404',
)
 */
  if ($info = drush_patchfile_get_patched_project($request['name'])) {
    if (drush_confirm(dt("Patches detected for @project. Do you want to attempt to re-apply them?", array('@project' => $request['name'])))) {
    // @todo Convert to drush_invoke_process() once patch-project is a command.
      _drush_patchfile_project_apply_patches($request['name'], $info['patches'], $request['project_install_location']);
    }
  }
}

/**
 * Command callback for drush patch-project.
 *
 * @param string $project_name
 *   The project machine-name to apply patches to from the manifest.
 */
function drush_patchfile_patch_project($project_name) {
  if ($info = drush_patchfile_get_patched_project($project_name)) {
    return _drush_patchfile_project_apply_patches($project_name, $info['patches'], $info['dir']);
  }
  else {
    drush_print(dt("There are no patches to apply to !project.", array('!project' => $project_name)));
    return FALSE;
  }
}

function _drush_patchfile_project_apply_patches($project_name, array $patches, $project_directory) {
  if (!empty($patches)) {
    // Following is copied from DrushMakeProject.
    // Commented-out code is for checksum verification and writing PATCHES.txt.
    // @see DrushMakeProject::applyPatches
    $ignore_checksums = TRUE;
    $local_patches = array();
    foreach ($patches as $info) {
      // @see DrushMakeProject::preprocessLocalFileUrl()
      if (is_string($info)) {
        $info = array('url' => $info, 'local' => FALSE);
      }
      if (!_drush_is_url($info['url']) && !drush_is_absolute_path($info['url'])) {
        $info['url'] = $this->make_directory . '/' . $info['url'];
        $info['local'] = TRUE;
      } elseif (substr($info['url'], 0, 8) == 'file:///') {
        $info['url'] = substr($info['url'], 7);
        $info['local'] = TRUE;
      }

      // Download the patch.
      if ($filename = _make_download_file($info['url'])) {
        $patched = FALSE;
        $output = '';
        // Test each patch style; -p1 is the default with git. See
        // http://drupal.org/node/1054616
        $patch_levels = array('-p1', '-p0');
        foreach ($patch_levels as $patch_level) {
          $checked = drush_shell_exec('cd %s && GIT_DIR=. git apply --check %s %s --verbose', $project_directory, $patch_level, $filename);
          if ($checked) {
            // Apply the first successful style.
            $patched = drush_shell_exec('cd %s && GIT_DIR=. git apply %s %s --verbose', $project_directory, $patch_level, $filename);
            break;
          }
        }

        // In some rare cases, git will fail to apply a patch, fallback to using
        // the 'patch' command.
        if (!$patched) {
          foreach ($patch_levels as $patch_level) {
            // --no-backup-if-mismatch here is a hack that fixes some
            // differences between how patch works on windows and unix.
            if ($patched = drush_shell_exec("patch %s --no-backup-if-mismatch -d %s < %s", $patch_level, $project_directory, $filename)) {
              break;
            }
          }
        }

        if ($output = drush_shell_exec_output()) {
          // Log any command output, visible only in --verbose or --debug mode.
          drush_log(implode("\n", $output));
        }

        // Set up string placeholders to pass to dt().
        $dt_args = array(
          '@name' => $project_name,
          '@filename' => basename($filename),
        );

        if ($patched) {
          if (!$ignore_checksums && !_make_verify_checksums($info, $filename)) {
             return FALSE;
          }
          $patch_url = $info['url'];

          // If this is a local patch, copy that into place as well.
          if ($info['local']) {
            $local_patches[] = $info['url'];
            // Use a local path for the PATCHES.txt file.
            $pathinfo = pathinfo($patch_url);
            $patch_url = $pathinfo['basename'];
          }
          $patches_txt .= '- ' . $patch_url . "\n";

          drush_log(dt('@name patched with @filename.', $dt_args), 'ok');
        }
        else {
          make_error('PATCH_ERROR', dt("Unable to patch @name with @filename.", $dt_args));
        }
        drush_op('unlink', $filename);
      }
      else {
        make_error('DOWNLOAD_ERROR', 'Unable to download ' . $info['url'] . '.');
        return FALSE;
      }
    }

    // if (!empty($patches_txt) && !drush_get_option('no-patch-txt') && !file_exists($project_directory . '/PATCHES.txt')) {
    //   $patches_txt = "The following patches have been applied to this project:\n" .
    //     $patches_txt .
    //     "\nThis file was automatically generated by Drush Make (http://drupal.org/project/drush).";
    //   file_put_contents($project_directory . '/PATCHES.txt', $patches_txt);
    //   drush_log('Generated PATCHES.txt file for ' . $this->name, 'ok');

    //   // Copy local patches into place.
    //   foreach ($local_patches as $url) {
    //     $pathinfo = pathinfo($url);
    //     drush_copy_dir($url, $project_directory . '/' . $pathinfo['basename']);
    //   }
    // }

    return TRUE;
  }
  else {
    drush_print(dt("There are no patches to apply to !project.", array('!project' => $project_name)));
    return FALSE;
  }
}
