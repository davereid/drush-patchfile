# Drush Patch File

This is a set of Drush commands to help you manage patches on your local Drupal
installation. Most developers (should) know about Drush Make files as a way to
store data about a site, installation profile, etc. When using Drush Make files,
typically you add information about the patches applied to a project in the
following way:

```make
projects[nodequeue][subdir] = contrib
projects[nodequeue][version] = 2.0-alpha1
projects[nodequeue][patch][] = "http://drupal.org/files/issues/1023606-qid-to-name-6.patch"
projects[nodequeue][patch][] = "http://drupal.org/files/issues/nodequeue_d7_autocomplete-872444-6.patch"
```

These Drush scripts can read a normal Drush make file just fine, and it will
only care about projects which have 'patch' defined. You should make sure that
the file referenced contains all the patches you need. If you have more than
one Drush make file (and most projects do), you can define a patches.make that
"includes" the other files.

```make
# Our project contains patches for both Drupal core and contrib modules/themes.
includes[] = "drupal-org.make"
includes[] = "drupal-org-core.make"
```

If you are not using Drush make you can just add one Drush make file just for
the purpose of documenting your patches. I typically like to add extra
comments to give me even more info about each patch, like the issue to the
Drupal.org comment where I found the patch, or if the patch has been committed
to the module, and noting that I should remove it when I upgrade.

```make
; @see https://drupal.org/comment/5460918#comment-5460918
; @todo Remove when updating to 7.x-2.0-beta2 or higher
projects[nodequeue][patch][] = "https://drupal.org/files/node-mark-deprecated-1402634-1.patch"

; @see https://drupal.org/node/763454#comment-6070450
projects[noderefcreate][patch][] = "https://drupal.org/files/763454-9.patch"
```

## Requirements

- Drush 5.x or higher
- A valid Drush make file for patches (or normal Drush make files) as described
  above.

## Installation

Download or git clone into your ~/.drush directory, or wherever your local Drush
commands are stored.

You should also add the canonical location of the patch make file to your
project's drushrc.php:

```ini
# Patch file is relevant to the Drupal root directory. This example would refer
# a patch file that is located one directory up from the Drupal root. Define
# this option to save typing when running Drush commands for your project.
$options['patch-file'] = '../patches.make';
```

## Usage

### drush patch-status (ps)

TODO

### drush patch-project (pp)

TODO

### drush pm-download (dl)

If you are running a drush dl on a module or theme that has a related patch,
after the download has been completed, the patch utility will attempt to apply
the patches again to the project. Use the patch application messages to see if
you will need to reroll the patch, or if it has been fixed.

```
drush dl noderefcreate
Install location sites/all/modules/contrib/noderefcreate already exists. Do you want to overwrite it? (y/n): y
Project noderefcreate (7.x-1.0) downloaded to sites/all/modules/contrib/noderefcreate. [success]
noderefcreate patched with 763454-9.patch. [ok]
```

The above indicates that the patch was successfully re-applied.

```
drush dl noderefcreate
Install location sites/all/modules/contrib/noderefcreate already exists. Do you want to overwrite it? (y/n): y
Project noderefcreate (7.x-1.0) downloaded to sites/all/modules/contrib/noderefcreate. [success]
Unable to patch noderefcreate with 763454-9.patch. [error]
```

The above means either the patch does not apply and needs to be re-rolled, or
that the patch has possibly been included in the release that you downloaded,
and is no longer necessary.
