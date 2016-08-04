<?php

require_once __DIR__ . '/src/DrushPatchFileGit.php';

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
    'no-cache' => 'Do not use the remote patch file caching (defaults to cache enabled).',
  );

  $items['patch-status'] = array(
    'description' => 'List the patches that should be applied to the site.',
    'options' => $options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
    'aliases' => array('ps'),
    'arguments' => array(
      'project-name' => 'The name of the module, theme, profile, or \'core\' to re-apply patches to.',
    ),
    'required-arguments' => FALSE,
  );
  $items['patch-apply-all'] = array(
    'description' => 'Apply all patches listed in the patch file.',
    'options' => $options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
    'aliases' => array('paa'),
  );
  $items['patch-project'] = array(
    'description' => 'Apply all the patches for a specific project as listed in the patch file.',
    'arguments' => array(
      'project-name' => 'The name of the module, theme, profile, or \'drupal\' to re-apply patches to.',
      'project-directory' => 'Optional, the directory of the project to use for the patches.',
    ),
    'required-arguments' => 1,
    'options' => array(
      'select' => 'If provided, will prompt for which patch to apply.',
    ) + $options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
    'aliases' => array('pp'),
  );
  $items['patch-add'] = array(
    'description' => 'Add a new patch to the patch file.',
    'arguments' => array(
      'project-name' => 'The name of the module, theme, profile, or \'drupal\' to re-apply patches to.',
      'patch-file' => 'The path to the patch to apply. Can be an external URL.',
    ),
    'required-arguments' => TRUE,
    'options' => array(
      'no-apply' => 'Do not apply the patch to the project.',
    ) + $options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
    'aliases' => array('pa'),
  );

  return $items;
}

function drush_patchfile_patch_status($project_name = NULL) {
  $projects = drush_patchfile_get_patched_projects();

  if (!empty($project_name)) {
    $projects = array_intersect_key($projects, array($project_name => NULL));
  }

  if (empty($projects)) {
    return drush_print(dt('No patches found.'));
  }

  $rows = array();
  $rows[] = array(dt('Project'), dt('Patch'), dt('Applied'));

  foreach ($projects as $project_name => $project_info) {
    $project_directory = drush_patchfile_project_get_directory($project_name);
    $project_extra = '';

    if (empty($project_directory)) {
      $project_extra .= ' ' . _drush_patchfile_color('red', dt('(missing)'));
    }

    foreach ($project_info['patch'] as $patch) {
      $rows[] = array(
        $project_name . $project_extra,
        _drush_patchfile_patch_short_name($patch),
        drush_patchfile_is_patch_applied($patch, $project_directory, TRUE),
      );
    }
  }

  drush_print_table($rows, TRUE);
}

/**
 * Command callback for drush patch-apply-all
 */
function drush_patchfile_patch_apply_all() {
  $projects = drush_patchfile_get_patched_projects();

  if (empty($projects)) {
    return drush_print(dt('No patches found.'));
  }

  foreach ($projects as $project_name => $project_info) {
    drush_patchfile_patch_project($project_name);
  }
}

/**
 * Command callback for drush patch-project.
 *
 * @param string $project_name
 *   The project name to apply patches to from the manifest.
 */
function drush_patchfile_patch_project($project_name, $project_directory = NULL) {
  $patches = drush_patchfile_get_project_patches($project_name);
  if (empty($patches)) {
    return drush_set_error('DRUSH_PROJECT_NO_PATCHES', dt("There are no patches to apply to @project.", array('@project' => $project_name)));
  }

  if (!isset($project_directory)) {
    $project_directory = drush_patchfile_project_get_directory($project_name);
  }
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

function drush_patchfile_process_url(&$url) {
  $patches = _drush_patchfile_get_possible_patches_from_url($url);
  if ($patches !== FALSE) {
    if (empty($patches)) {
      throw new Exception("Unable to determine patch file for {$url}.");
    }
    elseif (count($patches) == 1) {
      $url = key($patches);
    }
    else {
      if ($patch = drush_choice($patches, dt("Which patch do you want to apply?"))) {
        $url = $patch;
      }
      else {
        return FALSE;
      }
    }
  }

  return TRUE;
}

/**
 * Command callback for drush patch-project.
 *
 * @param string $project_name
 *   The project name to apply patches to from the manifest.
 * @param string $patch_url
 *   The URL containing the patch file.
 */
function drush_patchfile_patch_add($project_name, $url) {
  $file = drush_get_option('patch-file');

  $args = array(
    '@project' => $project_name,
    '@url' => &$url,
  );

  $project_directory = drush_patchfile_project_get_directory($project_name);
  if (empty($project_directory)) {
    return drush_set_error('DRUSH_PROJECT_NOT_FOUND', dt("The project @project was not found.", $args));
  }

  // Attempt to convert generic URLs to patch-specific URLs.
  $original_url = $url;
  if (_drush_patchfile_is_url($url)) {
    if (!drush_patchfile_process_url($url)) {
      return FALSE;
    }
  }

  $patch = $url;
  if (!_drush_patchfile_preprocess_local_file_url($patch, $file)) {
    return FALSE;
  }

  // Check to see if this project is already patched with this patch.
  $projects = drush_patchfile_get_patched_projects();
  if (isset($projects[$project_name])) {
    if (in_array($patch, $projects[$project_name]['patch']) || in_array($url, $projects[$project_name]['patch'])) {
      return drush_set_error('DRUSH_PATCH_ALREADY_LISTED', dt("The patch @url is already listed as a patch for @project.", $args));
    }
  }

  if (!drush_get_option('no-apply')) {
    if (_drush_patchfile_project_apply_patches($project_name, array($patch), $project_directory) || drush_get_option('force')) {
      _drush_patchfile_add_patch($project_name, $patch['url'], $file);
    }
    else {
      if (drush_confirm("Would you still like to add this patch to the patch file?")) {
        _drush_patchfile_add_patch($project_name, $patch['url'], $file);
      }
      return FALSE;
    }
  }
  else {
    if (_make_download_file($patch['url']) || drush_get_option('force')) {
      _drush_patchfile_add_patch($project_name, $patch['url'], $file);
    }
    else {
      return drush_set_error('DOWNLOAD_ERROR', dt('Unable to download or fetch @url.', $args));
    }
  }
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
      // @todo Should this skip the patch choice? If not, we should use drush_invoke_process()
      //drush_invoke_process('@self', 'patch-project', array($request['name']));
      $dir = $request['full_project_path'];
      $root = drush_get_context('DRUSH_DRUPAL_ROOT');
      if (!empty($root) && strpos($dir, $root) === FALSE) {
        //$dir = $root . '/' . $dir;
      }
      drush_patchfile_patch_project($request['name'], $dir);
    //}
  }
}

function _drush_patchfile_patch_short_name($patch) {
  $return = basename($patch['url']);
  $return = preg_replace('/\.(patch|diff)$/', '', $return);
  return $return;
}

function drush_patchfile_get_patched_projects($patchfile = NULL, $use_cache = FALSE) {
  $projects = array();

  if (!isset($patchfile)) {
    $patchfile = drush_get_option('patch-file');
  }

  if (!empty($patchfile)) {
    // Cache not only by filename, but also by the time the file was modified
    // so that a drush cache clear is not necessary to pick up changes.
    $cid = _drush_patchfile_get_file_cid($patchfile);
    if ($cid && $use_cache && $cache = drush_cache_get($cid)) {
      $projects = $cache->data;
    }
    else {
      $projects = drush_patchfile_parse_info_file($patchfile);
      if ($cid && $use_cache) {
        drush_cache_set($cid, $projects);
      }
    }
  }
  else {
    // @todo Error here.
  }

  return $projects;
}

function _drush_patchfile_get_file_cid($file) {
  $params = array();
  $includes = _drush_patchfile_get_all_files($file);
  foreach ($includes as $include) {
    $params[] = $include . ':' . filemtime($include);
  }
  return drush_get_cid('patchfile', array(), $params);
}

/**
 * A copy of make_file_parse_info() but that only returns an array of files.
 *
 * @see make_file_parse_info()
 */
function _drush_patchfile_get_all_files($makefile) {
  $data = make_get_data($makefile);
  if (empty($data)) {
    drush_set_error('MAKE_INVALID_MAKE_FILE', dt('Invalid or empty make file: !makefile', array('!makefile' => $makefile)));
    return array();
  }

  $info = _drush_drupal_parse_info_file($data);
  if (empty($info)) {
    return array();
  }

  $includes = array();

  if (!empty($info['includes']) && is_array($info['includes'])) {
    $include_path = dirname($makefile);
    foreach ($info['includes'] as $include) {
      if (is_string($include)) {
        if (make_valid_url($include, TRUE) && ($file = _drush_patchfile_get_all_files($include))) {
          $includes = array_merge($includes, $file);
        }
        elseif (file_exists($include_path . '/' . $include) && ($file = _drush_patchfile_get_all_files($include_path . '/' . $include))) {
          $includes = array_merge($includes, $file);
        }
        elseif (file_exists($include) && ($file = _drush_patchfile_get_all_files($include))) {
          $includes = array_merge($includes, $file);
        }
        else {
          make_error('BUILD_ERROR', dt("Include file missing: !include", array('!include' => $include)));
        }
      }
    }
  }

  return array_merge(array($makefile), $includes);
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
        _drush_patchfile_preprocess_local_file_url($patch_info, $file);
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
 * Test if a patch is currently applied to a directory.
 *
 * @param array $patch
 *   A patch array, must have 'url' value defined at minimum for
 *   _make_download_file().
 * @param string $directory
 *   The directory the patch should be applied to.
 * @param bool $nice_format
 *   Boolean if TRUE will return the status in a human-readable format.
 *
 * @return bool|null
 *   TRUE if the patch is currently applied. FALSE if the patch is not
 *   currently applied, but can be. NULL if we could not determine if
 *   the patch is applied or not (likely re-roll needed).
 */
function drush_patchfile_is_patch_applied(array $patch, $directory, $nice_format = FALSE) {
  $result = DrushPatchFileGit::checkPatch($directory, $patch);

  if (!$nice_format) {
    return $result['status'];
  }

  if ($result['status'] === DrushPatchFileGit::PATCH_APPLIED) {
    return _drush_patchfile_color('green', dt('Yes'));
  }
  elseif ($result['status'] === DrushPatchFileGit::PATCH_UNAPPLIED) {
    return _drush_patchfile_color('red', dt('No'));
  }
  else {
    return _drush_patchfile_color('yellow', dt('Unsure'));
  }
}

function _drush_patchfile_color($color, $string) {
  $colors = array(
    'red' => 31,
    'green' => 32,
    'yellow' => 33,
    'blue' => 34,
  );

  if (drush_get_context('DRUSH_NOCOLOR') || !isset($colors[$color])) {
    return $string;
  }

  return sprintf("\033[1;%sm%s\033[0m", $colors[$color], $string);
}

/**
 * Copied from DrushMakeProject::preprocessLocalFileUrl().
 *
 * @see DrushMakeProject::preprocessLocalFileUrl()
 */
function _drush_patchfile_preprocess_local_file_url(&$info, $makefile) {
  if (is_string($info)) {
    $info = array('url' => $info, 'local' => FALSE);
  }

  if (!_drush_patchfile_is_url($info['url']) && !drush_is_absolute_path($info['url'])) {
    $makefiles = _drush_patchfile_get_all_files($makefile);
    $makefiles = array_map('realpath', $makefiles);
    $possible_dirs = array_unique(array_map('dirname', $makefiles));
    foreach ($possible_dirs as $possible_dir) {
      if (is_file($possible_dir . '/' . $info['url'])) {
        $info['url'] = $possible_dir . '/' . $info['url'];
        $info['local'] = TRUE;
        break;
      }
    }
    if (!$info['local']) {
      throw new Exception("Unable to determine patch file location for {$info['url']}.");
    }
  }
  elseif (substr($info['url'], 0, 8) == 'file:///') {
    $info['url'] = substr($info['url'], 7);
    $info['local'] = TRUE;
  }

  return TRUE;
}

/**
 * A duplicate of _drush_is_url() so this will work on 5.x
 */
function _drush_patchfile_is_url($url) {
  return parse_url($url, PHP_URL_SCHEME) !== NULL;
}

/**
 * Add a patch record to a make file.
 *
 * @param string $project_name
 *   The project being patched.
 * @param string $patch_url
 *   The URL of the patch (remote or local).
 * @param string $file
 *   The make file to add the patch record to.
 *
 * @return bool
 *   TRUE if the file was appended successfully, or FALSE otherwise.
 */
function _drush_patchfile_add_patch($project_name, $patch_url, $file) {
  $output = "\nprojects[{$project_name}][patch][] = \"{$patch_url}\"\n";
  $result = file_put_contents($file, $output, FILE_APPEND);
  if ($result !== FALSE) {
    drush_log(dt('Added patch @patch to @project in @file.', array('@patch' => $patch_url, '@project' => $project_name, '@file' => $file)), 'success');
    return TRUE;
  }
  else {
    return drush_set_error('DRUSH_MAKE_FILE_NO_WRITE', dt('Unable to append to file @file.', array('@file' => $file)));
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
  foreach ($patches as $patch) {
    try {
      $patched = DrushPatchFileGit::applyPatch($project_directory, $patch);

      // Set up string placeholders to pass to dt().
      $dt_args = array(
        '@name' => $project_name,
        '@patch' => basename($patch['url']),
      );

      if ($patched) {
        // $patch_url = $info['url'];

        // // If this is a local patch, copy that into place as well.
        // if ($info['local']) {
        //   $local_patches[] = $info['url'];
        //   // Use a local path for the PATCHES.txt file.
        //   $pathinfo = pathinfo($patch_url);
        //   $patch_url = $pathinfo['basename'];
        // }
        // $patches_txt .= '- ' . $patch_url . "\n";

        drush_log(dt('@name patched with @patch.', $dt_args), 'success');
      }
      else {
        make_error('PATCH_ERROR', dt("Unable to patch @name with @patch.", $dt_args));
        return FALSE;
      }
    }
    catch (Exception $e) {
      drush_set_error('DRUSH_PATCH_ERROR', $e->getMessage());
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

function _drush_patchfile_get_possible_patches_from_url($url) {
  if (preg_match('~^https?://(?:www\.)?drupal\.org(/node/\d+(#comment-\d+)?)~i', $url, $matches)) {
    $patches = array();
    if ($request = @file_get_contents($url)) {
      $dom = @new DOMDocument();
      @$dom->loadHTML($request);
      $xpath = new DOMXpath($dom);

      foreach ($xpath->query("//table[@id='extended-file-field-table-field-issue-files']/tbody/tr[not(contains(concat(' ',normalize-space(@class),' '),' element-hidden '))]") as $element) {
        $file_xpath = $xpath->query("td[@class='extended-file-field-table-filename']//a", $element);
        if (!$file_xpath->length) {
          continue;
        }

        // If a comment number was provided, then filter the results to only
        // patches that match the comment.
        $cid_query = "td[@class='extended-file-field-table-cid']//a";
        if (!empty($matches[2])) {
          $cid_query .= "[@href='{$matches[1]}']";
        }
        $cid_xpath = $xpath->query($cid_query, $element);
        if (!$cid_xpath->length) {
          continue;
        }

        // Validate that this is actually a .patch or .diff file.
        $patch_url = $file_xpath->item(0)->getAttribute('href');
        if (!in_array(pathinfo($patch_url, PATHINFO_EXTENSION), array('patch', 'diff'))) {
          continue;
        }

        $size = $xpath->query("td[@class='extended-file-field-table-filesize']", $element)->item(0)->textContent;
        $author = $xpath->query("td[@class='extended-file-field-table-uid']//a", $element)->item(0)->textContent;
        $cid = ltrim($cid_xpath->item(0)->textContent, '#');
        $patches[$patch_url] = array(
          'patch' => $file_xpath->item(0)->textContent,
          'details' =>  sprintf("%10s by %s on comment %d", $size, $author, $cid),
        );
      }
    }
    return $patches;
  }

  if (preg_match('~^https://github\.com/[\w-]+/[\w-]+/pull/\d+$~i', $url, $matches)) {
    $patch_url = $url . '.patch';
    if (@file_get_contents($patch_url)) {
      return array($patch_url => $patch_url);
    }
  }

  if (preg_match('~^https://bitbucket\.org/([\w-]+)/([\w-]+)/pull-request/(\d+)~i', $url, $matches)) {
    $patch_url = "https://bitbucket.org/api/2.0/repositories/{$matches[1]}/{$matches[2]}/pullrequests/{$matches[3]}/diff";
    if (@file_get_contents($patch_url)) {
      return array($patch_url => $patch_url);
    }
  }

  return FALSE;
}
