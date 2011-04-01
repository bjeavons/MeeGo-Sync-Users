<?php

/**
 * @file Drush commands for MeeGo sync migration.
 */

define('MEEGO_AUTH_URL', 'http://auth.meego.local/'); // Include ending slash.

/**
 * Implementation of hook_drush_command().
 */
function meego_sync_users_drush_command() {
  $items = array();

  $items['mmu-collect'] = array(
    'description' => "MeeGo.com user dump for importing users to auth.meego.com.",
    'arguments' => array(
      'file' => 'The full path of the file to dump to',
    ),
    'examples' => array(
      'drush mmuc --file=/home/meego/meego-users' => 'Writes the users to the file /tmp/meego-users.',
    ),
    'aliases' => array('mmuc'),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
  );
  $items['mmu-sync'] = array(
    'description' => "MeeGo.com user sync for synchronizing local meego.com accounts with their master account on auth.meego.com.",
    'arguments' => array(
      'file' => 'The full path of the file to load and sync users with',
    ),
    'examples' => array(
      'drush mmus --file=/home/meego/meego-users' => 'Loads the file and updates users table.',
    ),
    'aliases' => array('mmus'),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
  );

  return $items;
}

/**
 * mmu-collect drush command.
 */
function drush_meego_sync_users_mmu_collect() {
  $file = drush_get_option('file');
  if (empty($file)) {
    drush_die(dt('Provide a file name'));
  }
  if (!$handle = fopen($file, 'w')) {
    drush_die(dt('Unable to open file for writing'));
  }

  $users = array();

  // Gather profile field IDs.
  $result = db_query("SELECT name, fid FROM {profile_fields}");
  while ($row = db_fetch_array($result)) {
    if ($row['name'] == 'profile_firstname') {
      $firstname_fid = $row['fid'];
    }
    elseif ($row['name'] == 'profile_lastname') {
      $lastname_fid = $row['fid'];
    }
  }
  $fids = array($firstname_fid, $lastname_fid);
  // Select uid, name, email, status from users and profile_firstname and
  // profile_lastname from profile_values.
  $sql = "SELECT u.uid, u.name, u.mail, u.status, GROUP_CONCAT(v.value SEPARATOR \"','\") AS profile FROM {users} u LEFT OUTER JOIN {profile_values} v ON v.uid = u.uid AND v.fid IN (" . db_placeholders($fids) . ")  WHERE u.uid > 0 GROUP BY u.uid";
  $result = db_query($sql, $fids);
  while ($record = db_fetch_array($result)) {
    $users[] = $record;
  }
  if (!empty($users)) {
    drush_log(dt('Collected !count users', array('!count' => count($users))), 'success');
    $count = 0;
    // Write file identifier.
    $msg = "#'uid','name','mail','status','profile_firstname','profile_lastname' - " . time() . "\n";
    if (fwrite($handle, $msg) === FALSE) {
      drush_die(dt('Unable to write to file.'));
    }
    // Write comma-separated fields to the file.
    foreach ($users as $data) {
      // If profile was empty for this user set a special value.
      if (empty($data['profile'])) {
        $data['profile'] = "','";
      }
      // Quote elements of data.
      $data = array_map('_meego_sync_quote', $data);
      // Separate by comma.
      $out = implode(',', $data) . "\n";
      if (fwrite($handle, $out) === FALSE) {
        drush_log(dt('Unable to write to file. Stopping after writing !count', array('!count' => $count)), 'error');
        break;
      }
      $count++;
    }
    drush_log(dt('Wrote !count to file', array('!count' => $count)), 'success');
  }
  fclose($handle);
}

function _meego_sync_quote($element) {
  return "'" . $element . "'";
}

/**
 * mmu-sync drush command.
 */
function drush_meego_sync_users_mmu_sync() {
  $file = drush_get_option('file');
  if (empty($file)) {
    drush_die(dt('Provide a file name'));
  }
  if (!$handle = fopen($file, 'r')) {
    drush_die(dt('Unable to open file for reading'));
  }

  // Check file identifer.
  $msg = fgets($handle);
  if ($msg === FALSE || substr($msg, 0, 1) !== '#') {
    drush_die(dt('Invalid file or cannot be read.'));
  }
  if (!preg_match("/'(uid)','(name)','(mail)','(status)','(profile_firstname)','(profile_lastname)','(muid)'.*/", substr($msg, 1), $matches) && count($matches) != 7) {
    drush_die(dt('File does not contain auth.MeeGo UIDs, rerun auth.MeeGo migration.'));
  }

  $pattern = "/'([0-9]+)','(.*)','(.*)','([0|1])','(.*)','(.*)','([0-9])'/";
  // Sync users.
  $read = $updated = 0;
  while (($buffer = fgets($handle)) !== FALSE) {
    $read++;
    if (preg_match($pattern, $buffer, $matches)) {
      unset($matches[0]); // Remove first full string match.
      $data = array_map('trim', $matches, array("\n'")); // Remove whitespace and newlines.
      if (count($data) == 7) {
        list($uid, $name, $mail, $status, $profile_firstname, $profile_lastname, $muid) = $data;
        // Update init field.
        $init = MEEGO_AUTH_URL . 'user/' . $muid . '/edit';
        $sql = "UPDATE {users} SET init = '%s' WHERE uid = %d AND name = '%s' AND mail = '%s'";
        db_query($sql, $init, $uid, $name, $mail);
        if (db_affected_rows() > 0) {
          $updated++;
        }
      }
    }
  }
  drush_log(dt('Updated !count users of !read from file', array('!count' => $updated, '!read' => $read)), 'success');
  fclose($handle);
}