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
<?php

/**
 * AttachmentState
 *
 * A wrapper around the State class for handling the Attachment State file.
 * This class supplies operators for correctly managing the State contents.
 */
class AttachmentState {

	/**
	 * The basedir in which the attachments for all sessions are found
	 */
	private $basedir;

	/**
	 * The directpry in which the attachments for the current session are found
	 */
	private $sessiondir;

	/**
	 * The directory in which the session files are created
	 */
	private $attachmentdir = "attachments";

	/**
	 * The State object which refers to the Attachments state
	 */
	private $state;

	/**
	 * List of attachment files from the $state
	 */
	private $files;

	/**
	 * List of deleted attachment files from the $state
	 */
	private $deleteattachment;

	/**
	 * Constructor
	 */
	public function AttachmentState()
	{
		$this->basedir = TMP_PATH . DIRECTORY_SEPARATOR . $this->attachmentdir;
		$this->sessiondir = $this->basedir . DIRECTORY_SEPARATOR . session_id();

		$this->state = new State('attachments');
	}

	/**
	 * Open the session file
	 *
	 * The session file is opened and locked so that other processes can not access the state information
	 */
	public function open()
	{
		if (!is_dir($this->sessiondir)) {
			mkdir($this->sessiondir, 0755, true /* recursive */);
		}

		$this->state->open();
		$this->files = $this->state->read("files");
		$this->deleteattachment = $this->state->read("deleteattachment");
	}

	/**
	 * Obtain the folder in which the attachments for the current session
	 * can be found. All handling of attachments will be isolated to this
	 * folder to prevent any other user to be able to access the attachment
	 * files of another user.
	 *
	 * @return String The foldername in which the attachments can be found
	 */
	private function getAttachmentFolder()
	{
		return $this->sessiondir;
	}

	/**
	 * Obtain the full path for a new filename on the harddisk
	 * This uses tempnam to generate a new filename in the default
	 * attachments folder
	 *
	 * @param String $filename The file for which the temporary path is requested
	 * @return String The full path to the attachment file
	 */
	public function getAttachmentTmpPath($filename)
	{
		return tempnam($this->getAttachmentFolder(), basename($filename));
	}

	/**
	 * Obtain the full path of the given filename on the harddisk
	 *
	 * @param String $filename The file for which the path is requested
	 * @return String The full path to the attachment file
	 */
	public function getAttachmentPath($filename)
	{
		return $this->getAttachmentFolder() . DIRECTORY_SEPARATOR . basename($filename);
	}

	/**
	 * Add a file which was uploaded to the server by moving it to the attachments directory,
	 * and then register (addAttachmentFile) it.
	 *
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 * @param String $filename The filename of the attachment
	 * @param String $uploadedfile The file which was uploaded and will be moved to the attachments directory
	 * @param Array $fileinfo The attachment data
	 * @return The attachment identifier to be used for referencing the file in the tmp folder
	 */
	public function addUploadedAttachmentFile($message_id, $filename, $uploadedfile, $fileinfo)
	{
		// Create the destination path, the attachment must
		// be placed in the attachment folder with a unique name.
		$filepath = $this->getAttachmentTmpPath($filename);

		// Obtain the generated filename
		$tmpname = basename($filepath);

		move_uploaded_file($uploadedfile, $filepath);

		$this->addAttachmentFile($message_id, $tmpname, $fileinfo);

		return $tmpname;
	}

	/**
	 * Move a file from an alternative location to the attachments directory,
	 * this will call addAttachmentFile to register the attachment to the state
	 *
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 * @param String $filename The filename of the attachment
	 * @param String $sourcefile The path of the file to move to the attachments directory
	 * @param Array $fileinfo The attachment data
	 * @return The attachment identifier to be used for referencing the file in the tmp folder
	 */
	public function addProvidedAttachmentFile($message_id, $filename, $sourcefile, $fileinfo)
	{
		// Create the destination path, the attachment must
		// be placed in the attachment folder with a unique name.
		$filepath = $this->getAttachmentTmpPath($filename);

		// Obtain the generated filename
		$tmpname = basename($filepath);

		// Move the uploaded file to tmpname loaction
		rename($sourcefile, $filepath);

		$this->addAttachmentFile($message_id, $tmpname, $fileinfo);

		return $tmpname;
	}

	/**
	 * Delete a file which was previously uploaded to the attachments directory,
	 * this will call removeAttachmentFile to unregister the attachment from the state
	 *
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 * @param String $filename The filename of the attachment to delete
	 */
	public function deleteUploadedAttachmentFile($message_id, $filename)
	{
		// Create the destination path, the attachment has
		// previously been placed in the attachment folder
		$filepath = $this->getAttachmentPath($filename);
		if (is_file($filepath)) {
			unlink($filepath);
		}

		$this->removeAttachmentFile($message_id, basename($filepath));
	}

	/**
	 * Obtain all files which were registered for the given $message_id
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 * @return Array The array of attachments
	 */
	public function getAttachmentFiles($message_id)
	{
		if ($this->files && isset($this->files[$message_id])) {
			return $this->files[$message_id];
		}

		return false;
	}

	/**
	 * Obtain a single attachment file which belongs to the given $message_id
	 * and is identified by the given $attachid.
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 * @param String $attachid The unique identifier for referencing the
	 * attachment
	 * @return Array The attachment description for the requested attachment
	 */
	public function getAttachmentFile($message_id, $attachid)
	{
		if ($this->files && isset($this->files[$message_id]) && isset($this->files[$message_id][$attachid])) {
			return $this->files[$message_id][$attachid];
		}

		return false;
	}

	/**
	 * Add an attachment file to the message
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 * @param String $name The name of the new attachment
	 * @param Array $attachment The attachment data
	 */
	public function addAttachmentFile($message_id, $name, $attachment)
	{
		if (!$this->files) {
			$this->files = Array( $message_id => Array() );
		} else if (!isset($this->files[$message_id])) {
			$this->files[$message_id] = Array();
		}

		$this->files[$message_id][$name] = $attachment;
	}

	/**
	 * Remove an attachment file from the message
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 * @param String $name The name of the attachment
	 */
	public function removeAttachmentFile($message_id, $name)
	{
		if ($this->files && isset($this->files[$message_id])) {
			unset($this->files[$message_id][$name]);
		}
	}

	/**
	 * Remove all attachment files from the message
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 */
	public function clearAttachmentFiles($message_id)
	{
		if ($this->files) {
			unset($this->files[$message_id]);
		}
	}

	/**
	 * Obtain all files which were removed for the given $message_id
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 * @return Array The array of attachments
	 */
	public function getDeletedAttachments($message_id)
	{
		if ($this->deleteattachment && isset($this->deleteattachment[$message_id])) {
			return $this->deleteattachment[$message_id];
		}

		return false;
	}

	/**
	 * Add a deleted attachment file to the message
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 * @param String $name The name of the attachment
	 */
	public function addDeletedAttachment($message_id, $name)
	{
		if (!$this->deleteattachment) {
			$this->deleteattachment = Array( $message_id => Array() );
		} else if (!isset($this->deleteattachment[$message_id])) {
			$this->deleteattachment[$message_id] = Array();
		}

		$this->deleteattachment[$message_id][] = $name;
	}

	/**
	 * Remove a deleted attachment from the message
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 * @param String $name The name of the attachment
	 */
	public function removeDeletedAttachment($message_id, $name)
	{
		if ($this->deleteattachment && isset($this->deleteattachment[$message_id])) {
			$index = array_search($name, $this->deleteattachment[$message_id]);
			if ($index !== false) {
				unset($this->deleteattachment[$message_id][$index]);
				$this->deleteattachment[$message_id] = array_values($this->deleteattachment[$message_id]);
			}
		}
	}

	/**
	 * Remove all deleted attachment files from the message
	 * @param String $message_id The unique identifier for referencing the
	 * attachments for a single message
	 */
	public function clearDeletedAttachments($message_id)
	{
		if ($this->deleteattachment) {
			unset($this->deleteattachment[$message_id]);
		}
	}

	/**
	 * Close the state and flush all information back
	 * to the State file on the disk.
	 */
	public function close()
	{
		$this->state->write("files", $this->files, false);
		$this->state->write("deleteattachment", $this->deleteattachment, false);
		$this->state->flush();
		$this->state->close();
	}

	/**
	 * Cleans all old attachments in the attachment directory
	 */
	public function clean() {
		cleanTemp($this->basedir, UPLOADED_ATTACHMENT_MAX_LIFETIME);
	}
}

?>
