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
	* Download Attachment
	* This file is used to download an attachment of a message.
	*/
	require_once("client/layout/dialogs/utils.php");

	// include class file for creating a zip archive file for all attachments.
	include_once("server/core/class.createZipArchive.php");

	// Get store id
	$storeid = false;
	if(isset($_GET["store"])) {
		$storeid = get("store", false, false, ID_REGEX);
	}

	// Get message entryid
	$entryid = false;
	if(isset($_GET["entryid"])) {
		$entryid = get("entryid", false, false, ID_REGEX);
	}

	// Check which type isset
	$openType = "inline";
	if(isset($_GET["openType"])) {
		$openType = get("openType", false, false, STRING_REGEX);
	}

	// Check download type isset
	$downloadType = "";
	if(isset($_GET["downloadType"])) {
		$downloadType = get("downloadType", false, false, STRING_REGEX);
	}

	// Check mailsubject is there or not
	$mailSubject = "";
	if(isset($_GET["mailSubject"])) {
		$mailSubject = get("mailSubject", false, false, false);
	}

	// Get number of attachment which should be opened.
	$attachNum = false;
	if(isset($_GET["attachNum"])) {
		$attachNum = array();
		/**
		 * if you are opening an already saved attachment then $_GET["attachNum"]
		 * will contain array of numeric index for that attachment (like 0 or 1 or 2)
		 *
		 * if you are opening a recently uploaded attachment then $_GET["attachNum"]
		 * will be a one element array and it will contain a string in "filename.randomstring" format
		 * like README.txtu6K6AH
		 */
		foreach($_GET["attachNum"] as $attachNumber) {
			if(preg_match_all(FILENAME_REGEX, $attachNumber, $matches))
				array_push($attachNum, $attachNumber);
		}
	}

	// Check if inline image should be opened
	$attachCid = false;
	if(isset($_GET["attachCid"])) {
		$attachCid = get("attachCid", false, false, FILENAME_REGEX);
	}

	if($downloadType == "downloadAll"){
		downloadAllFilesInZipArchive($storeid, $entryid, $openType, $attachNum, $attachCid, $mailSubject);
	}else{
		// open already saved attachment
		if($storeid && $entryid) {
			if(!openSavedAttachments($storeid, $entryid, $openType, $attachNum, $attachCid)) {
				// possibly we are accessing a temporary attachment in a saved message
				openTemporaryAttachments($storeid, $entryid, $openType, $attachNum);
			}
		} else {
			// Open recently uploaded (not saved) attachment
			openTemporaryAttachments($storeid, $entryid, $openType, $attachNum);
		}
	}

	/*********** Internal Functions ***********/
	/**
	 * Function will open already saved attachment from a saved message
	 *
	 * @param HexString $storeid store id
	 * @param HexString $entryid entryid of message
	 * @param String $openType can be inline / attachment (content disposition type)
	 * @param Array $attachNum numeric indexes of attachments to be opened
	 * @param String $attachCid attachment content id for inline images
	 * @param Boolean $getContentsOnly contains true if we only wants the contents of a file, to create a zip file of all files.
	 *
	 * @return Boolean true on success, false on failure
	 */
	function openSavedAttachments($storeid, $entryid, $openType, $attachNum, $attachCid, $getContentsOnly=false) {
		// Open the store
		$store = $GLOBALS["mapisession"]->openMessageStore(hex2bin($storeid));

		if($store) {
			// Open the message
			$message = mapi_msgstore_openentry($store, hex2bin($entryid));

			// Decode smime signed messages on this message
 			parse_smime($store, $message);

			if($message) {
				$attachment = false;

				// Check if attachNum isset
				if(is_array($attachNum) && count($attachNum) > 0) {
					// Loop through the attachNums, message in message in message ...
					for($i = 0; $i < (count($attachNum) - 1); $i++)
					{
						// Open the attachment
						$tempattach = mapi_message_openattach($message, (int) $attachNum[$i]);
 						if($tempattach) {
 							// Open the object in the attachment
							$message = mapi_attach_openobj($tempattach);
						}
					}

					/**
					 * check if we are opening really a saved attachment in saved message or
					 * opening a temporary (not saved) attachment in a saved message
					 * the only way to differentiate this is type of attachNum variable
					 * check above comments for $attachNum argument
					 */
					if(is_numeric($attachNum[(count($attachNum) - 1)])) {
						// Open the already saved attachment
						$attachment = mapi_message_openattach($message, (int) $attachNum[(count($attachNum) - 1)]);
					}
				}

				if($attachCid) { // Check if attachCid isset
					// If the inline image was in a submessage, we have to open that first
					if($attachment) {
						$message = mapi_attach_openobj($attachment);
					}

					// Get the attachment table
					$attachments = mapi_message_getattachmenttable($message);
					$attachTable = mapi_table_queryallrows($attachments, Array(PR_ATTACH_NUM, PR_ATTACH_CONTENT_ID, PR_ATTACH_CONTENT_LOCATION, PR_ATTACH_FILENAME));

					foreach($attachTable as $attachRow)
					{
						// Check if the CONTENT_ID or CONTENT_LOCATION is equal to attachCid
						if((isset($attachRow[PR_ATTACH_CONTENT_ID]) && $attachRow[PR_ATTACH_CONTENT_ID] == $attachCid)
							|| (isset($attachRow[PR_ATTACH_CONTENT_LOCATION]) && $attachRow[PR_ATTACH_CONTENT_LOCATION] == $attachCid)
							|| (isset($attachRow[PR_ATTACH_FILENAME]) && $attachRow[PR_ATTACH_FILENAME] == $attachCid)) {
							// Open the attachment
							$attachment = mapi_message_openattach($message, $attachRow[PR_ATTACH_NUM]);
						}
					}
				}

				// Check if the attachment is opened
				if($attachment) {
					// Get the props of the attachment
					$props = mapi_attach_getprops($attachment, array(PR_ATTACH_FILENAME, PR_ATTACH_LONG_FILENAME, PR_ATTACH_MIME_TAG, PR_DISPLAY_NAME, PR_ATTACH_METHOD));

					// Content Type
					$contentType = "application/octet-stream";
					// Filename
					$filename = "ERROR";

					// Set filename
					if(isset($props[PR_ATTACH_LONG_FILENAME])) {
						$filename = $props[PR_ATTACH_LONG_FILENAME];
					} else if(isset($props[PR_ATTACH_FILENAME])) {
						$filename = $props[PR_ATTACH_FILENAME];
					} else if(isset($props[PR_DISPLAY_NAME])) {
						$filename = $props[PR_DISPLAY_NAME];
					}

					$contentType = getMIMEType($filename);

					// Set the headers
					header("Pragma: public");
					header("Expires: 0"); // set expiration time
					header("Cache-Control: must-revalidate, post-check=0, pre-check=0");
					header("Content-Disposition: " . $openType . "; filename=\"" . browserDependingHTTPHeaderEncode($filename) . "\"");
					header("Content-Transfer-Encoding: binary");

					// Set content type header
					header("Content-Type: " . $contentType);

					// Open a stream to get the attachment data
					$stream = mapi_openpropertytostream($attachment, PR_ATTACH_DATA_BIN);
					$stat = mapi_stream_stat($stream);

					// if we want only contents then pass the content writing on header
					if($getContentsOnly){
						if($stream)
							return array("content" => mapi_stream_read($stream, $stat["cb"]), "filename" => $filename);
						return false;
					}

					// File length
					header("Content-Length: " . $stat["cb"]);
					for($i = 0; $i < $stat["cb"]; $i += BLOCK_SIZE) {
						// Print stream
						echo mapi_stream_read($stream, BLOCK_SIZE);
					}

					return true;		// success
				}
			}
		}

		return false;		// failure
	}

	/**
	 * Function will open temporary attachment, it doesn't matter the message itself is saved or not
	 *
	 * @param HexString $storeid store id
	 * @param HexString $entryid entryid of message
	 * @param String $openType can be inline / attachment (content disposition type)
	 * @param Array $attachNum numeric indexes of attachments to be opened
	 * @param Boolean $getContentsOnly contains true if we only wants the contents of a file, to create a zip file of all files.
	 */
	function openTemporaryAttachments($storeid, $entryid, $openType, $attachNum, $getContentsOnly=false) {
		if(isset($_GET["dialog_attachments"])) {
			$attachment_state = new AttachmentState();
			$attachment_state->open();

			// we can only open one attachment in a request,
			// so only use first argument of $attachNum
			$tmpname = $attachment_state->getAttachmentPath($attachNum[0]);
			$fileinfo = $attachment_state->getAttachmentFile($_GET["dialog_attachments"], $attachNum[0]);

			// Check if the file still exists
			if (is_file($tmpname)) {
				// Set the headers
				header("Pragma: public");
				header("Expires: 0"); // set expiration time
				header("Cache-Control: must-revalidate, post-check=0, pre-check=0");
				header("Content-Disposition: " . $openType . "; filename=\"" . $fileinfo['name'] . "\"");
				header("Content-Transfer-Encoding: binary");
				header("Content-Type: application/octet-stream");
				header("Content-Length: " . filesize($tmpname));

				// Open the uploaded file and print it
				$file = fopen($tmpname, "r");
				fpassthru($file);
				fclose($file);
			}

			$attachment_state->close();
		}
	}

	/**
	 * Function will create a zip file containing all attachments.
	 *
	 * @param HexString $storeid store id
	 * @param HexString $entryid entryid of message
	 * @param String $openType can be inline / attachment (content disposition type)
	 * @param Array $attachNum numeric indexes of attachments to be opened
	 * @param String $attachCid attachment content id for inline images
	 * @param String $mailSubject contains the subject of mail
	 */
	function downloadAllFilesInZipArchive($storeid, $entryid, $openType, $attachNum, $attachCid, $mailSubject) {
		/**
		 * @TODO :// Find some code, which can directly stream the data of zip file rather creating it in memory.
		 * increase the memory for a longer file creation.
		 * ini_set('memory_limit', '64M');
		 */

		$fileContents = array();

		// create a object of creating Zip archive File
		$createZipFile=new CreateZipFile;

		// set output directory
		$outputDir="";
		$fileNames = array();

		for($att=0;$att<count($attachNum);$att++){
			if(is_numeric($attachNum[$att])){
				$fileContent = openSavedAttachments($storeid, $entryid, $openType, array($attachNum[$att]), $attachCid, true);
			}else{
				$fileContent = openTemporaryAttachments($storeid, $entryid, $openType, array($attachNum[$att]), true);
			}

			//check for the duplicate file name
			if($fileContent){
				$fileContent["filename"] = updateDuplicateName($fileContent["filename"], $fileNames);
				array_push($fileNames, $fileContent["filename"]);

				// add the file into zip file.
				$createZipFile->addFile($fileContent["content"], $outputDir.$fileContent["filename"]);
			}
		}

		//create a zip file name from subject or from date.
		$fileName = ( ($mailSubject) ? $mailSubject : date("h_i_s-j_m_Y") ) . '.zip';

		// download the created zip file.
		$zipName = TMP_PATH . "/" . $fileName;

		$zipFile = fopen($zipName, "wb");
		$out = fwrite($zipFile, $createZipFile->getZippedfile());
		fclose($zipFile);
		$createZipFile->forceDownload($zipName, $fileName);
		@unlink($zipName);
	}

	/**
	 * Function which checks, if filename is already in filenames array or not.
	 * If name found duplicate, it converts it to a unique name.
	 *
	 * @param String $fileName file name to check for duplicate name
	 * @param Array $fileNames fileNames array to be checked
	 *
	 * @return String $fileName changed file name
	 */
	function updateDuplicateName($fileName, $fileNames){
		$tmpFileName = $fileName;
		$duplicateCounter = 1;
		while(in_array($tmpFileName, $fileNames)){
			$fileNameArr = split("[/\\.]", $fileName);
			if(count($fileNameArr) > 1){
				$extension = array_pop($fileNameArr);
				$fileNameArr[count($fileNameArr)-1] .= '_' . $duplicateCounter++;
				$tmpFileName = join(".", $fileNameArr) . "." . $extension;
			}else{
				$tmpFileName = join("", $fileNameArr) . "_" . $duplicateCounter++;
			}
		}
		return $tmpFileName;
	}
?>