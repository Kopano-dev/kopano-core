<?php
/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

?>
<?

/**
 * Secondary state handling
 *
 * This class works exactly the same as standard PHP sessions files. We implement it here
 * so we can have improved granularity and don't have to put everything into the session
 * variables. Basically we have multiple session objects corresponding to multiple webclient
 * objects on the client. This class also locks the session files (in the same way as standard
 * PHP sessions), in order to serialize requests from the same client object.
 *
 * The main reason for creating this is to improve performance; normally PHP will lock the session
 * file for as long as your PHP request is running. However, we want to do multiple PHP requests at
 * once. For this reason, we only use the PHP session system for login information, and use this
 * class for other state information. This means that we can set a message as 'read' at the same time
 * as opening a dialog to view a message. 
 *
 * Currently, there is one 'state' for each 'subsystem'. The 'subsystem' is simply a tag which is appended
 * to the request URL when the client does an XML request. Each 'subsystem' has its own state file.
 *
 * Currently the subsystem is equal to the module ID. This means that if you have two requests from the same
 * module, they will have to wait for eachother. In practice this should hardly ever happen.
 *
 * @package core
 */

class State {

	/**
	 * The file pointer of the state file
	 */
	var $fp;

	/**
	 * The basedir in which the statefiles are found
	 */
	var $basedir;

	/**
	 * The filename which is opened by this state file
	 */
	var $filename;

	/**
	 * The directory in which the session files are created
	 */
	var $sessiondir = "session";

	/**
	 * The unserialized data as it has been read from the file
	 */
	var $sessioncache;

	/**
	 * The raw data as it has been read from the file
	 */
	var $contents;

	/**
	 * @param string $subsystem Name of the subsystem
	 */
	function State($subsystem) {
		$this->basedir = TMP_PATH . DIRECTORY_SEPARATOR . $this->sessiondir;
		$this->filename = $this->basedir . DIRECTORY_SEPARATOR . session_id() . "." . $subsystem;
	}

	/**
	 * Open the session file
	 *
	 * The session file is opened and locked so that other processes can not access the state information
	 */
	function open() {
		if (!is_dir($this->basedir)) {
			mkdir($this->basedir, 0755, true /* recursive */);
		}
		$this->fp = fopen($this->filename, "a+");
		$this->sessioncache = false;
		flock($this->fp, LOCK_EX);
	}

	/**
	 * Read a setting from the state file
	 *
	 * @param string $name Name of the setting to retrieve
	 * @return string Value of the state value, or null if not found
	 */
	function read($name) {
		// If the file has already been read, we only have to access
		// our cache to obtain the requeste data.
		if ($this->sessioncache === false) {
			$this->contents = file_get_contents($this->filename);
			$this->sessioncache = unserialize($this->contents);
		}

		if (isset($this->sessioncache[$name])) {
			return $this->sessioncache[$name];
		} else {
			return false;
		}
	}

	/**
	 * Write a setting to the state file
	 *
	 * @param string $name Name of the setting to write
	 * @param mixed $object Value of the object to be written to the setting
	 * @param bool $flush False to prevent the changes written to disk
	 * This requires a call to $flush() to write the changes to disk.
	 */
	function write($name, $object, $flush = true)
	{
		$contents = "";

		// If the file has already been read, then we don't
		// need to read the entire file again.
		if ($this->sessioncache === false) {
			$this->read($name);
		}

		$this->sessioncache[$name] = $object;

		if ($flush === true) {
			$this->flush();
		}
	}

	/**
	 * Flushes all changes to disk
	 *
	 * This flushes all changed made to the $this->sessioncache to disk
	 */
	function flush()
	{
		if ($this->sessioncache) {
			$contents = serialize($this->sessioncache);
			if ($contents !== $this->contents) {
				ftruncate($this->fp, 0);
				fseek($this->fp, 0);
				fwrite($this->fp, $contents);
				$this->contents = $contents;
			}
		}
	}

	/**
	 * Close the state file
	 *
	 * This closes and unlocks the state file so that other processes can access the state
	 */
	function close() {
		if (isset($this->fp)) {
			fclose($this->fp);
		}
	}

	/**
	 * Cleans all old state information in the session directory
	 */
	function clean() {
		cleanTemp($this->basedir, STATE_FILE_MAX_LIFETIME);
	}
}