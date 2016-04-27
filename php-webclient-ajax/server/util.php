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
     * Utility functions
     *
     * @package core
     */

	/**
	 * Function which reads the XML. This XML is send by the WebClient.
	 * @return string XML
	 */
	function readXML() {
		$xml = "";
		$putData = fopen("php://input", "r");

		while($block = fread($putData, 1024))
		{
			$xml .= $block;
		}

		fclose($putData);
		return $xml;
	}

	/**
	 * Function which is called every time the "session_start" method is called.
	 * It unserializes the objects in the session. This function called by PHP.
	 * @param string @className the className of the object in the session
	 */
	function sessionModuleLoader($className)
	{
		$className = strtolower($className); // for PHP5 set className to lower case to find the file (see ticket #839 for more information)

		switch($className)
		{
			case "bus":
				require_once("core/class.bus.php");
				break;

			default:
				if(array_search($className, $GLOBALS["availableModules"])!==false) {
					require_once("modules/class." . $className . ".php");
				}elseif(isset($GLOBALS["availablePluginModules"][ $className ])) {
					require_once($GLOBALS["availablePluginModules"][ $className ]['file']);
				}
				break;
		}
		if (!class_exists($className)){
			trigger_error("Can't load ".$className." while unserializing the session.", E_USER_WARNING);
		}
	}

	/**
	 * Function which returns a list of available modules.
	 * @return array a list of available modules
	 */
	function getAvailableModules()
	{
		$modules = array();
		$dir = opendir("server/modules");

		while(($file = readdir($dir)) !== false)
		{
			$file = substr($file,6,-4);
			if ($file !== false && substr($file,-6)=="module"){
				array_push($modules, $file);
			}
		}

		return $modules;
	}

	/**
	 * Will extract a specific list of arguments from the $_GET object and construct a properly
	 * (raw)urlencoded REQUEST_URI to be used for appending to an URL.
	 * A token can be supplied to change the first character. By default it will set an ? as the
	 * first character to directly append it to an URL. When other arguments preceed this string it
	 * can be changed to an & or to any other value, including to no character by supplying an empty
	 * string.
	 * @param String $token (Optional) First character of the returned string. Defaults to "?".
	 * @return String The REQUEST URI containing the values from the specific list of $_GET-arguments.
	 */
	function getActionRequestURI($token = '?'){
		$requestURI = '';
		$allowedArgs = Array(
			'action',
			'url',
			'to',
			'cc',
			'bcc',
			'storeid',
			'parententryid',
			'entryid',
			'message_action',
			'subject',
			'body');

		$urlArgs = Array();
		if($_GET){
			// Put all allowed arguments in a key/value list
			for($i=0,$len=count($allowedArgs);$i<$len;$i++){
				$argName = $allowedArgs[$i];
				if(isset($_GET[ $argName ])){
					$urlArgs[ $argName ] = $_GET[ $argName ];
				}
			}

			// Create the correctly encoded string for each argument that was extracted \
			$formattedArgsList = Array();
			foreach($urlArgs as $argName => $argValue){
				$formattedArgsList[] = rawurlencode($argName) . '=' . rawurlencode($argValue);
			}

			// Put them together and ... DONE!
			$requestURI = $token . implode('&', $formattedArgsList);
		}
 		return $requestURI;
	}


	/**
	 * Function which replaces some characters to correct XML values.
	 * @param string @string string which should be converted
	 * @return string correct XML
	 */
	function xmlentities($string) {
		$string = str_replace("\x00", "", $string);
		$string = preg_replace("/[\x01-\x08\x0b\x0c\x0e-\x1f]+/", "", $string); // remove control chars, they would break the xml, and aren't needed anyway
		return str_replace ( array ( '&', '"', "'", '<', '>'), array ( '&amp;' , '&quot;', '&apos;' , '&lt;' , '&gt;'), $string );
	}

	/**
	 * Function which checks if an array is an associative array.
	 * @param array $data array which should be verified
	 * @return boolean true if the given array is an associative array, false if not
	 */
	function is_assoc_array($data) {
		return is_array($data) && !empty($data) && !preg_match('/^\d+$/', implode('', array_keys($data)));
	}

	/**
	 * Function which compares two users on there full name. Used for sorting the user list.
	 * @param array $a user
	 * @param array $b user
	 * @return integer -1 - $a < $b || 0 - equal || 1 - $a > $b
	 */
	function cmpUserList($a, $b) {
		if (strtolower($a["fullname"]) == strtolower($b["fullname"])) {
			return 0;
		}

		return (strtolower($a["fullname"]) < strtolower($b["fullname"])) ? -1 : 1;
	}

	/**
	 * Function which compares two groups on there group name. Used for sorting the group list.
	 * @param array $a group
	 * @param array $b group
	 * @return integer -1 - $a < $b || 0 - equal || 1 - $a > $b
	 */
	function cmpGroupList($a, $b) {
		if (strtolower($a["groupname"]) == strtolower($b["groupname"])) {
			return 0;
		}

		return (strtolower($a["groupname"]) < strtolower($b["groupname"])) ? -1 : 1;
	}

	/**
	 * Function which is simular to the php function array_merge_recursive, but this
	 * one overwrites duplicates.
	 * @param array $array1 array
	 * @param array $array1 array
	 * @return array merged array
	 */
	function array_merge_recursive_overwrite($array1, $array2)
	{
		if (!is_array($array1) || !is_array($array2)) {
			return $array2;
		}

		foreach ($array2 as $key=>$value) {
			if (isset($array1[$key])){
				$array1[$key] = array_merge_recursive_overwrite($array1[$key], $value);
			}else{
				$array1[$key] = $value;
			}
		}
		return $array1;
	}

	/**
	 * Function which adds a download url for inline attachments in mail body's.
	 * This function is called in the filter class (it is a preg_replace_callback function)
	 * @param array $match the information which part of the body is found.
	 * @return string download string
	 */
	function inline_attachments($match)
	{
		if(array_key_exists("2", $match)) {
			return "='" . BASE_URL . "index.php?load=download_attachment&store=" . $GLOBALS["preg_replace"]["storeid"] . "&amp;entryid=" . $GLOBALS["preg_replace"]["entryid"] . "&amp;attachNum[]=" . join(",", $GLOBALS["preg_replace"]["attachNum"]) ."&amp;attachCid=" . urlencode($match[2]) . "&amp;openType=inline'";
		} else {
			return "=''";
		}
	}

	/**
	 * Function which adds a download url for inline attachments in mail body's.
	 * The regular expression written for this replacement works for only 'img' tags. We search for all parts of img tag,
	 * replace link for src part and put together with all other parts in img tag. This is done after filtering html in filter class.
	 * @param array $match the information which part of the body is found.
	 * @return string download string
	 */
	function inline_img_attachments($match)
	{
		// groups 1 contains value of src attribute
		if (array_key_exists("1", $match)) {
			// exclude external links to images
			if(preg_match("/https{0,1}:\/\//msi", $match[1]) === 1 || strpos($match[1], "data:image") === 0) {
				// return links as it is without changing anything
				return $match[0];
			}

			$cid = str_replace("cid:", "", $match[1]);

			return str_replace($match[1], BASE_URL . "index.php?load=download_attachment&store=" . $GLOBALS["preg_replace"]["storeid"] . "&amp;entryid=" . $GLOBALS["preg_replace"]["entryid"] . "&amp;attachNum[]=" . join(",", $GLOBALS["preg_replace"]["attachNum"]) ."&amp;attachCid=" . urlencode($cid) . "&amp;openType=inline\"", $match[0]);
		} else {
			return $match[0];
		}
	}

	/**
	 * Function which adds a new mail popup on links which are mail addresses in mail body's.
	 * This function is called in the filter class (it is a preg_replace_callback function)
	 * @param array $match the information which part of the body is found.
	 * @return string new mail popup string
	 */
	function mailto_newmail($match)
	{
		$js_options = "'toolbar=0,scrollbars=1,location=0,status=1,menubar=0,resizable=1,width=780,height=560,top='+((screen.height/2)-280)+',left='+((screen.width/2)-390)";
		$newMail = array("TO"=>$match[4]);

		/**
		 * $match[5] will contain whole string of parameters passed in mailto link, we can't actually
		 * use reg expr to check different parts, so we have to split parameters
		 */
		if (!empty($match[5])) {
			$parameters = explode('&amp;', $match[5]);
			for($index=0; $index < count($parameters); $index++) {
				$param = explode('=', $parameters[$index]);
				$newMail[strtoupper($param[0])] = $param[1];
			}
		}

		// build a string of parameters which will be added in URL
		$parameterString = "";
		if(isset($newMail["TO"])) $parameterString .= '&to=' . $newMail['TO'];
		if(isset($newMail["CC"])) $parameterString .= '&cc=' . $newMail['CC'];
		if(isset($newMail["BCC"])) $parameterString .= '&bcc=' . $newMail['BCC'];
		if(isset($newMail["BODY"])) $parameterString .= '&body=' . $newMail['BODY'];
		if(isset($newMail["SUBJECT"])) $parameterString .= '&subject=' . $newMail['SUBJECT'];

		// 'encode' newMail array
		$newMailString = bin2hex(serialize($newMail));
		return '<'.$match[1].$match[2].'='.$match[3].'mailto:'.$match[4].(!empty($match[5])?'?'.$match[5]:'').$match[6].' onclick='."\"parent.webclient.openWindow(this, 'createmail', 'index.php?load=dialog&task=createmail_standard" . $parameterString . "'); return false;\"".$match[7].'>';
	}


	function microtime_float() {
		list($usec, $sec) = explode(" ", microtime());
		return ((float)$usec + (float)$sec);
	}

	/**
	 * gets maximum upload size of attachment from php ini settings
	 * important settings are upload_max_filesize and post_max_size
	 * upload_max_filesize specifies maximum upload size for attachments
	 * post_max_size specifies maximum size of a post request, we are uploading attachment using post method
	 * that's why we need to check this also
	 * these values are overwritten in .htaccess file of WA
	 */
	function getMaxUploadSize($as_string = false)
	{
		$upload_max_value = strtoupper(ini_get('upload_max_filesize'));
		$post_max_value = strtoupper(ini_get('post_max_size'));

		/**
		 * if POST_MAX_SIZE is lower then UPLOAD_MAX_FILESIZE, then we have to check based on that value
		 * as we will not be able to upload attachment larger then POST_MAX_SIZE (file size + header data)
		 * so set POST_MAX_SIZE value to higher then UPLOAD_MAX_FILESIZE
		 */

		// calculate upload_max_value value to bytes
		if (strpos($upload_max_value, "K")!== false){
			$upload_max_value = ((int) $upload_max_value) * 1024;
		} else if (strpos($upload_max_value, "M")!== false){
			$upload_max_value = ((int) $upload_max_value) * 1024 * 1024;
		} else if (strpos($upload_max_value, "G")!== false){
			$upload_max_value = ((int) $upload_max_value) * 1024 * 1024 * 1024;
		}

		// calculate post_max_value value to bytes
		if (strpos($post_max_value, "K")!== false){
			$post_max_value = ((int) $post_max_value) * 1024;
		} else if (strpos($post_max_value, "M")!== false){
			$post_max_value = ((int) $post_max_value) * 1024 * 1024;
		} else if (strpos($post_max_value, "G")!== false){
			$post_max_value = ((int) $post_max_value) * 1024 * 1024 * 1024;
		}

		// check which one is larger
		$value = $upload_max_value;
		if($upload_max_value > $post_max_value) {
			$value = $post_max_value;
		}

		if ($as_string){
			// make user readable string
			if ($value > (1024 * 1024 * 1024)){
				$value = round($value / (1024 * 1024 * 1024), 1) ." ". _("GB");
			}else if ($value > (1024 * 1024)){
				$value = round($value / (1024 * 1024), 1) ." ". _("MB");
			}else if ($value > 1024){
				$value = round($value / 1024, 1) ." ". _("KB");
			}else{
				$value = $value ." ". _("B");
			}
		}

		return $value;
	}

	/**
	 * Get the maximum number of files allowed to be uploaded simultaneously.
	 *
	 * zero means unlimited
	 */
	function GetMaxFileUploads()
	{
		$maxfileuploads = intval(ini_get('max_file_uploads'));

		if ($maxfileuploads == 0 || $maxfileuploads > FILE_QUEUE_LIMIT)
			$maxfileuploads = FILE_QUEUE_LIMIT;

		return $maxfileuploads;
	}

	/**
	 * cleanTemp
	 *
	 * Cleans up the temp directory.
	 * @param String $directory The path to the temp dir or sessions dir.
	 * @param Integer $maxLifeTime The maximum allowed age of files in seconds.
	 * @param Boolean $recursive False to prevent the folder to be cleaned up recursively
	 * @param Boolean $removeSubs False to prevent empty subfolders from being deleted
	 * @return Boolean True if the folder is empty
	 */
	function cleanTemp($directory = TMP_PATH, $maxLifeTime = STATE_FILE_MAX_LIFETIME, $recursive = true, $removeSubs = true)
	{
		if (!is_dir($directory)) {
			return;
		}

		// PHP doesn't do this by itself, so before running through
		// the folder, we should flush the statcache, so the 'atime'
		// is current.
		clearstatcache();

		$dir = opendir($directory);
		$is_empty = true;

		while ($file = readdir($dir)) {
			// Skip special folders
			if ($file === '.' || $file === '..') {
				continue;
			}

			$path = $directory . DIRECTORY_SEPARATOR . $file;

			if (is_dir($path)) {
				// If it is a directory, check if we need to
				// recursively clean this subfolder.
				if ($recursive) {
					// If cleanTemp indicates the subfolder is empty,
					// and $removeSubs is true, we must delete the subfolder
					// otherwise the currently folder is not empty.
					if (cleanTemp($path, $maxLifeTime, $recursive) && $removeSubs) {
						rmdir($path);
					} else {
						$is_empty = false;
					}
				} else {
					// We are not cleaning recursively, the current
					// folder is not empty.
					$is_empty = false;
				}
			} else {
				$fileinfo = stat($path);

				if ($fileinfo && $fileinfo["atime"] < time() - $maxLifeTime) {
					unlink($path);
				} else {
					$is_empty = false;
				}
			}
		}

		return $is_empty;
	}

	function cleanSearchFolders()
	{
		$store = $GLOBALS["mapisession"]->getDefaultMessageStore();

		$storeProps = mapi_getprops($store, array(PR_STORE_SUPPORT_MASK, PR_FINDER_ENTRYID));
		if (($storeProps[PR_STORE_SUPPORT_MASK] & STORE_SEARCH_OK)!=STORE_SEARCH_OK) {
			return;
		}

		$finderfolder = mapi_msgstore_openentry($store, $storeProps[PR_FINDER_ENTRYID]);
		if (mapi_last_hresult()!=0){
			return;
		}

		$hierarchytable = mapi_folder_gethierarchytable($finderfolder);
		mapi_table_restrict($hierarchytable, array(RES_AND,
												array(
													array(RES_CONTENT,
														array(
															FUZZYLEVEL	=> FL_PREFIX,
															ULPROPTAG	=> PR_DISPLAY_NAME,
															VALUE		=> array(PR_DISPLAY_NAME=>"WebAccess Search Folder")
														)
													),
													array(RES_PROPERTY,
														array(
															RELOP		=> RELOP_LT,
															ULPROPTAG	=> PR_LAST_MODIFICATION_TIME,
															VALUE		=> array(PR_LAST_MODIFICATION_TIME=>(time()-ini_get("session.gc_maxlifetime")))
														)
													)
												)
											), TBL_BATCH
		);

		$folders = mapi_table_queryallrows($hierarchytable, array(PR_ENTRYID, PR_DISPLAY_NAME, PR_LAST_MODIFICATION_TIME));
		foreach($folders as $folder){
			mapi_folder_deletefolder($finderfolder, $folder[PR_ENTRYID]);
		}
	}

	function dechex_32($dec){
		// Because on 64bit systems PHP handles integers as 64bit,
		// we need to convert these 64bit integers to 32bit when we
		// want the hex value
		$result = unpack("H*",pack("N", $dec));
		return $result[1];
	}

	/**
	 * Replace control characters in xml to '?'
	 * so xml parser will not break on control characters
	 * http://en.wikipedia.org/wiki/Windows-1252
	 * @param	string	$xml The XML string
	 * @return	string	$xml The valid utf-8 xml string
	 */
	function replaceControlCharactersInXML($xml) {
		/**
		 * don't remove CR => \x0D, LF => \x0A, HT => \x09, VT => \x0B
		 */
		$search = array("\x00", "\x01", "\x02", "\x03", "\x04", "\x05", "\x06",
			"\x07", "\x08", "\x0C", "\x0E", "\x0F", "\x10", "\x11", "\x12", "\x13",
			"\x14", "\x15", "\x16", "\x17", "\x18", "\x19", "\x1A", "\x1B", "\x1C",
			"\x1D", "\x1E", "\x1F", "\x7F");

		$replace = "?";

		$xml = str_replace($search, $replace, $xml);

		return $xml;
	}

	// PHP4 version of stripos
	if (!function_exists("stripos")) {
		function stripos($str,$needle,$offset=0)
		{
			return strpos(strtolower($str),strtolower($needle),$offset);
		}
	}

	/**
	 * checkTrialVersion
	 * Checks whether the zarafa-server we are connected to has a trial license or not, based on the
	 * capabilities list.
	 * @return Boolean returns true on a trial license, false when otherwise.
	 */
	function checkTrialVersion(){
		$capabilities = mapi_zarafa_getcapabilities($GLOBALS['mapisession']->getDefaultMessageStore());
		return (is_array($capabilities)&&array_search('TRIAL', $capabilities)!==false?true:false);
	}

	/**
	 * getDaysLeftOnTrialPeriod
	 * Returns the number of days left on the trial of the connected zarafa-server. This number is
	 * based on the remaining seconds left in the trial and rounded up to whole days.
	 * @return Integer Number of days remaining on trial. Returns 0 when not on a trial license.
	 */
	function getDaysLeftOnTrialPeriod(){
		$secondsLeft = 0;
		$capabilities = mapi_zarafa_getcapabilities($GLOBALS['mapisession']->getDefaultMessageStore());
		if(is_array($capabilities)){
			for($i=0;$i<count($capabilities);$i++){
				if(substr($capabilities[$i], 0, 6) == 'TRIAL.'){
					$secondsLeft = substr($capabilities[$i], 6);
					break;
				}
			}
		}

		return ceil($secondsLeft/60/60/24);
	}
	/**
	 * getMIMEType
	 * Returns the MIME type of the file or content type of the file,
	 * it uses file's extention for getting MIME type.
	 * @return string MIME type of the file.
	 */
	function getMIMEType($filename)
	{
		// Set default content type.
		$contentType = "application/octet-stream";
		// Parse the extension of the filename to get the content type
		if(strrpos($filename, ".") !== false) {
			$extension = strtolower(substr($filename, strrpos($filename, ".")));
			if (is_readable(MIME_TYPES)){
				$fh = fopen(MIME_TYPES,"r");
				$ext_found = false;
				while (!feof($fh) && !$ext_found){
					$line = fgets($fh);
					preg_match("/(\.[a-z0-9]+)[ \t]+([^ \t\n\r]*)/i", $line, $result);
					if (count($result) > 0 && $extension == $result[1]){
						$ext_found = true;
						$contentType = $result[2];
					}
				}
				fclose($fh);
			}
		}
		return $contentType;
	}

	/**
	 * This function will encode the input string for the header based on the browser that makes the
	 * HTTP request. MSIE has an issue with unicode filenames. All browsers do not seem to follow
	 * the RFC specification. Firefox requires an unencoded string in the HTTP header. MSIE will
	 * break on this and requires encoding.
	 * @param String $input Unencoded string
	 * @return String Encoded string
	 */
	function browserDependingHTTPHeaderEncode($input){
		if(strpos($_SERVER['HTTP_USER_AGENT'], 'MSIE') === false){
			return $input;
		}else{
			return rawurlencode($input);
		}
	}

	/**
	 * Shortcut function that uses getPropIdsFromStrings() to convert a string to the proper ID of
	 * the named property. When the Id is retrieved it is converted to hex. 0xFFFFFFFF00000000 is
	 * substracted from the result.
	 * @param String $id String with the ID of the property
	 * @return String Hex ID of the named property preceeded by "0x"
	 */
	function convertStringToHexNamedPropId($id){
		$propID = getPropIdsFromStrings($GLOBALS["mapisession"]->getDefaultMessageStore(), Array('prop'=>$id));
		return '0x' . dechex( $propID['prop']&0xffffffff );
	}

	/**
	 * Function will be used to decode smime messages and convert it to normal messages
	 * @param {MAPIStore} $store user's store
	 * @param {MAPIMessage} $message smime message
	 */
	function parse_smime($store, $message)
	{
		$props = mapi_getprops($message, array(PR_MESSAGE_CLASS));

		if(isset($props[PR_MESSAGE_CLASS]) && $props[PR_MESSAGE_CLASS] == 'IPM.Note.SMIME.MultipartSigned') {
			// this is a signed message. decode it.
			$atable = mapi_message_getattachmenttable($message);

			$rows = mapi_table_queryallrows($atable, Array(PR_ATTACH_MIME_TAG, PR_ATTACH_NUM));
			$attnum = false;

			foreach($rows as $row) {
				if(isset($row[PR_ATTACH_MIME_TAG]) && $row[PR_ATTACH_MIME_TAG] == 'multipart/signed') {
					$attnum = $row[PR_ATTACH_NUM];
					break;
				}
			}

			if($attnum !== false) {
				$att = mapi_message_openattach($message, $attnum);
				$data = mapi_openproperty($att, PR_ATTACH_DATA_BIN);

				mapi_message_deleteattach($message, $attnum);

				mapi_inetmapi_imtomapi($GLOBALS['mapisession']->getSession(), $store, $GLOBALS['mapisession']->getAddressbook(), $message, $data, Array("parse_smime_signed" => 1));
			}
		}
	}
?>
