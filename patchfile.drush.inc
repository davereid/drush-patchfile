<?php

/**
 * @file
 * Drush integration for patch make files.
 */

/**
 * Implements hook_drush_command().
 */
function patchfile_drush_command() {
  $options = array(
    'patch-file' => 'Location of the patch make file.',
  );

  $items['patch-status'] = array(
    'description' => 'List the patches that should be applied to the site.',
    'options' => $options,
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
      'select' => 'If provided, will prompt for which patch to apply.',
    ) + $options,
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

  foreach ($projects as $project_name => $project_info) {
    $project_extra = '';
    if (!drush_patchfile_project_get_directory($project_name)) {
      $project_extra .= ' ' . dt('(missing)');
    }

    foreach ($project_info['patch'] as $patch) {
      $rows[] = array($project_name . $project_extra, _drush_patchfile_patch_short_name($patch), 'Unknown');
    }
  }

  drush_print_table($rows);
}

/**
 * Command callback for drush patch-project.
 *
 * @param string $project_name
 *   The project name to apply patches to from the manifest.
 */
function drush_patchfile_patch_project($project_name) {
  $patches = drush_patchfile_get_project_patches($project_name);
  if (empty($patches)) {
    return drush_set_error('DRUSH_PROJECT_NO_PATCHES', dt("There are no patches to apply to @project.", array('@project' => $project_name)));
  }

  $project_directory = drush_patchfile_project_get_directory($project_name);
  if (empty($project_directory)) {
    return drush_set_error('DRUSH_PROJECT_NOT_FOUND', dt("The project @project was not found.", array('@project' => $project_name)));
  }

  if (drush_get_option('select')) {
    $options = array('all' => dt('All')) + array_map('_drush_patchfile_patch_short_name', $patches);
    $result = drush_choice($options, dt("Which patch do you want to apply?"));
    if ($result === FALSE) {
      return FALSE;
    }
    elseif ($result != 'all') {
      $patches = array($patches[$result]);
    }
  }

  return _drush_patchfile_project_apply_patches($project_name, $patches, $project_directory);
}

/**
 * Implements hook_drush_pm_post_download().
 */
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

  if (drush_patchfile_get_project_patches($request['name'])) {
    //if (drush_confirm(dt("Patches detected for @project. Do you want to attempt to re-apply them?", array('@project' => $request['name'])))) {
    // For some reason, using drush_invoke_process causes drush_get_context('DRUSH_AFFIRMATIVE') to flip from false to true, which skips the patch choice screen. Directly invoke for now.
      //drush_invoke_process('@self', 'patch-project', array($request['name']));
      drush_patchfile_patch_project($request['name']);
    //}
  }
}

function _drush_patchfile_patch_short_name($patch) {
  $return = basename($patch['url']);
  $return = preg_replace('/\.(patch|diff)$/', '', $return);
  return $return;
}

function drush_patchfile_get_patched_projects($patchfile = NULL, $use_cache = FALSE) {
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
      drush_cache_set($cid, $projects);
    }
  }
  else {
    // @todo Error here.
  }

  return $projects;
}

function drush_patchfile_parse_info_file($file) {
  // Parse the patch make file.
  $info = make_parse_info_file($file);

  if ($info === FALSE) {
    // Throw fatal error here.
  }

  if (!empty($info['projects'])) {
    foreach ($info['projects'] as $project_name => &$project_info) {
      // Only return projects that have patches listed.
      if (empty($project_info['patch'])) {
        unset($info['projects'][$project_name]);
        continue;
      }

      // Normalize each patch record.
      foreach ($project_info['patch'] as &$patch_info) {
       _drush_patchfile_preprocess_local_file_url($patch_info);
      }
    }
  }

  return $info['projects'];
}

function drush_patchfile_get_project_patches($project_name) {
  $projects = drush_patchfile_get_patched_projects();
  return isset($projects[$project_name]) ? $projects[$project_name]['patch'] : FALSE;
}

/**
 * Return the directory of the project.
 *
 * @param string $project_name
 *   The name of the module or theme to lookup.
 *
 * @todo Switch this call to use drush_invoke_process('@self', 'drupal-directory', array($project_name))
 */
function drush_patchfile_project_get_directory($project_name) {
  $drupal_root = drush_get_context('DRUSH_DRUPAL_ROOT');

  if ($project_name == 'drupal') {
    return $drupal_root;
  }

  // Get all possible modules, themes, and profiles.
  drush_include_engine('drupal', 'environment');
  $projects = array_merge(drush_get_modules(), drush_get_themes());
  //$projects = array_merge($projects, drush_find_profiles($drupal_root));

  if (isset($projects[$project_name])) {
    return $drupal_root . '/' . dirname($projects[$project_name]->filename);
  }

  return FALSE;
}

/**
 * Copied from DrushMakeProject::preprocessLocalFileUrl().
 *
 * @see DrushMakeProject::preprocessLocalFileUrl()
 */
function _drush_patchfile_preprocess_local_file_url(&$info) {
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
}

/**
 * Copied from DrushMakeProject::applyPatches().
 *
 * Sections commented out are code responsible for checksum verification and
 * writing PATCHES.txt files.
 *
 * @see DrushMakeProject::applyPatches()
 */
function _drush_patchfile_project_apply_patches($project_name, array $patches, $project_directory) {
  if (empty($patches)) {
    return TRUE;
  }

  $patches_txt = '';
  $local_patches = array();
  $ignore_checksums = TRUE; // drush_get_option('ignore-checksums');
  foreach ($patches as $info) {
    // $this->preprocessLocalFileUrl($info);

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
        // if (!$ignore_checksums && !_make_verify_checksums($info, $filename)) {
        //    return FALSE;
        // }
        // $patch_url = $info['url'];

        // // If this is a local patch, copy that into place as well.
        // if ($info['local']) {
        //   $local_patches[] = $info['url'];
        //   // Use a local path for the PATCHES.txt file.
        //   $pathinfo = pathinfo($patch_url);
        //   $patch_url = $pathinfo['basename'];
        // }
        // $patches_txt .= '- ' . $patch_url . "\n";

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
