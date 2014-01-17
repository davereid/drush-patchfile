<?php

/**
 * @file
 * Drush integration for patch make files.
 *
 * @todo Fix assumption that all patches are remote URLs.
 * @todo Make a patch-project commmand
 * @todo Make the patch-status command show if the patch is applied or not.
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
    $cid = drush_get_cid('patchfile', array(), array($patchfile));
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
    if ($dir = drush_patchfile_project_directory($project)) {
      $return[$project]['dir'] = $dir;
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
function drush_patchfile_project_directory($project) {
  $drupal_root = drush_get_context('DRUSH_DRUPAL_ROOT');

  if ($project == 'drupal') {
    return $drupal_root;
  }

  // Get all possible modules, themes, and profiles.
  drush_include_engine('drupal', 'environment');
  $projects = array_merge(drush_get_modules(), drush_get_themes(), drush_find_profiles($drupal_root));

  if (isset($projects[$project])) {
    return $drupal_root . '/' . dirname($projects[$project]->filename);
  }
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
    if (drush_confirm(dt("Patches detected for !project. Do you want to attempt to re-apply them?", array('!project' => $request['name'])))) {
    // @todo Convert to drush_invoke_process() once patch-project is a command.
      drush_patchfile_patch_project($request['project_type'], $request['name'], $request['project_install_location']);
    }
  }
}

function drush_patchfile_patch_project($project_type, $project_name, $project_location = NULL) {
  if ($info = drush_patchfile_get_patched_project($project_name)) {
    if (!isset($project_location)) {
      $project_location = $info['dir'];
    }

    $make_data = array();
    $make_data['name'] = $project_name;
    $make_data['patch'] = $info['patches'];
    $make = DrushMakeProject::getInstance($project_type, $make_data);
    drush_set_option('no-patch-txt', TRUE);
    $make->applyPatches($project_location);
  }
  else {
    drush_print(dt("There are no patches to apply to !project.", array('!project' => $project_name)));
  }
}
