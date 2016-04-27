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
	 * ListModule
	 * Superclass of every module, which retreives a MAPI message list. It 
	 * extends the Module class.
	 */
	class ListModule extends Module
	{
		/**
		 * @var array list of columns which are selected in the previous request.
		 */
		var $properties;
		
		/**
		 * @var array list of columns which are shown in the table view.
		 */
		var $tablecolumns;
		
		/**
		 * @var array list of columns which are shown in the table view for quick edit.
		 */
		var $insertcolumns;
		
		/**
		 * @var array list of columns which are shown in the table view.
		 */
		var $newcolumns;
		
		/**
		 * @var array sort.
		 */
		var $sort;
		
		/**
		 * @var int startrow in the table.
		 */
		var $start;
		
		/**
		 * @var array contains (when needed) a restriction used when searching
		 */
		var $searchRestriction;

		/**
		 * @var bool contains check whether a search result is listed or just the contents of a normal folder
		 */
		var $searchFolderList;

		/**
		 * @var bool returns search restriction is active or not
		 */
		var $searchActive;

		/**
		 * @var hexString entryid of the created search folder
		 */
		var $searchFolderEntryId;

		/**
		 * @var array stores search criteria of previous request
		 */
		var $searchCriteriaCheck;

		/**
		 * @var array stores entryids and last modification time of 
		 * messages that are already sent to the server
		 */
		var $searchResults;

		/**
		 * @var bool returns search folder should be used or not
		 */
		var $useSearchFolder;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function ListModule($id, $data, $events = false)
		{
			$this->start = 0;
			
			$this->searchRestriction = false;
			$this->searchFolderList = false;
			$this->searchActive = false;
			$this->searchFolderEntryId = false;
			$this->searchCriteriaCheck = false;
			$this->useSearchFolder = false;

			$this->sort = array();
			
			parent::Module($id, $data, $events);
		}
		
		/**
		 * Executes all the actions in the $data variable.
		 * @return boolean true on success of false on fialure.
		 */
		function execute()
		{
			$result = false;
			
			foreach($this->data as $action)
			{
				if(isset($action["attributes"]) && isset($action["attributes"]["type"])) {
					$store = $this->getActionStore($action);
					$parententryid = $this->getActionParentEntryID($action);
					$entryid = $this->getActionEntryID($action);

					$this->generatePropertyTags($store, $entryid, $action);

					switch($action["attributes"]["type"])
					{
						case "list":
							$this->getDelegateFolderInfo($store);
							$result = $this->messageList($store, $entryid, $action);
							break;
						case "save":
							$accessToFolder = true;
							/**
							 * Check for the folder access.
							 * If store and parententryid is not passed then it is
							 * users default messagestore and user has permission on it.
							 * For other folders check permissions on folder.
							 */
							if($store && $parententryid)
								$accessToFolder = $GLOBALS["operations"]->checkFolderAccess($store, $parententryid);

							if($accessToFolder) {
								// If folder is accessible then save the message.
								$result = $this->save($store, $parententryid, $action);
							} else {
								// If folder is not accessible then show error message.
								$errorMessage = _("You have insufficient privileges to save items in this folder") . ".";
								$this->sendFeedback(false, $errorMessage);
							}
							break;
						case "read_flag":
							$result = $this->setReadFlag($store, $entryid, $action);
							break;
						case "delete":
							$result = $this->delete($store, $parententryid, $entryid, $action);
							break;
						case "copy":
							$result = $this->copy($store, $parententryid, $entryid, $action);
							break;
                        case "cancelInvitation":
							$accessToFolder = false;
							// Find the parententryid using the store and the entryid when it has not been sent in the request
							if($store && $entryid && !$parententryid){
								$folder = mapi_msgstore_openentry($store, $entryid);
								$folderProps = mapi_getprops($folder, Array(PR_PARENT_ENTRYID));
								$parententryid = $folderProps[PR_PARENT_ENTRYID];
							}

							if($store && $parententryid)
								$accessToFolder = $GLOBALS["operations"]->checkFolderAccess($store, $parententryid);

							if($accessToFolder) {
								// If folder is accessible then cancel inviation the message.
								$GLOBALS["operations"]->cancelInvitation($store, $entryid, $action);
							} else {
								// If folder is not accessible then show error message.
								$errorMessage = _("You have insufficient privileges to delete items in this folder") . ".";
								$this->sendFeedback(false, $errorMessage);
							}
                            break;
						case "declineMeeting":
							if (isset($action["props"]) && isset($action["props"]["entryid"])) {
								$message = $GLOBALS["operations"]->openMessage($store, hex2bin($action["props"]["entryid"]));
								$basedate = (isset($action["props"]['basedate']) ? $action["props"]['basedate'] : false);

								$req = new Meetingrequest($store, $message, $GLOBALS["mapisession"]->getSession(), ENABLE_DIRECT_BOOKING);

								$body = isset($action["body"]) ? $action["body"] : false;
								$req->doDecline(true, $store, $basedate, $body);

								// Publish updated free/busy information
								$GLOBALS["operations"]->publishFreeBusy($store);

								$messageProps = mapi_getprops($message, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID));
								$GLOBALS["bus"]->notify(bin2hex($messageProps[PR_PARENT_ENTRYID]), $basedate ? TABLE_SAVE : TABLE_DELETE, $messageProps);
							}
							break;
					}
				}
			}
			
			return $result;
		}
		
		/**
		 * Function which retrieves a list of messages in a folder
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the folder
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function messageList($store, $entryid, $action)
		{
			$this->searchFolderList = false; // Set to indicate this is not the search result, but a normal folder content
			$result = false;

			if($store && $entryid) {
				// Restriction
				$this->parseSearchRestriction($action);

				// Sort
				$this->parseSortOrder($action, null, true);
				
				// List columns visible
				$GLOBALS["TableColumns"]->parseVisibleColumns($this->tablecolumns, $action);
				
				// When it is a searchfolder, we want the folder name column
				if(isset($this->searchActive) && $this->searchActive) {
					// check column is already added or not
					if($GLOBALS["TableColumns"]->getColumn($this->tablecolumns, "parent_entryid") === false) {
						$GLOBALS["TableColumns"]->addColumn($this->tablecolumns, "parent_entryid", true, 6, _("In Folder"), _("Sort Folder"), 90, "folder_name");
					}
				} else {
					$GLOBALS["TableColumns"]->removeColumn($this->tablecolumns, "parent_entryid");
				}

				// Create the data array, which will be send back to the client
				$data = array();
				$data["attributes"] = array("type" => "list");
				$data["column"] = $this->tablecolumns;

				if(is_array($this->insertcolumns) && count($this->insertcolumns) > 0) {
					// insertcolumns are used for quick edit functionality
					$data["insertcolumn"] = $this->insertcolumns;
				}
				
				$firstSortColumn = reset(array_keys($this->sort)); // get first key of the sort array
				$data["sort"] = array();
				$data["sort"]["attributes"] = array();
				$data["sort"]["attributes"]["direction"] = (isset($this->sort[$firstSortColumn]) && $this->sort[$firstSortColumn] == TABLE_SORT_ASCEND) ? "asc" : "desc";
				/**
				 * If MV_INSTANCE flag is set then remove it,
				 * It is added in parseSortOrder function to allow multiple instances.
				 */
				$data["sort"]["_content"] = array_search($firstSortColumn &~ MV_INSTANCE, $this->properties);

				if(isset($this->selectedMessageId)) {
					$this->start = $GLOBALS["operations"]->getStartRow($store, $entryid, $this->selectedMessageId, $this->sort, false, $this->searchRestriction);
				}

				// Get the table and merge the arrays
				$items = $GLOBALS["operations"]->getTable($store, $entryid, $this->properties, $this->sort, $this->start, false, $this->searchRestriction);
				
				for($i=0; $i<count($items["item"]); $i++){
					// Disable private items
					$items["item"][$i] = $this->disablePrivateItem($items["item"][$i]);
				}
				$data = array_merge($data, $items);

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);

				$result = true;
			}
			
			return $result;
		}

		/**
		 *	Function will set search restrictions on search folder and start search process
		 *	and it will also parse visible columns and sorting data when sending results to client
		 *	@param		object		$store		MAPI Message Store Object
		 *	@param		hexString	$entryid	entryid of the folder
		 *	@param		object		$action		the action data, sent by the client
		 */
		function search($store, $entryid, $action)
		{
			if(!isset($action["use_searchfolder"]) || $action["use_searchfolder"] == null) {
				/**
				 * store doesn't support search folders so we can't use this
				 * method instead we will pass restriction to messageList and
				 * it will give us the restricted results
				 */
				$this->useSearchFolder = false;
				return $this->messageList($store, $entryid, $action);
			}

			$this->searchFolderList = true; // Set to indicate this is not the normal folder, but a search folder
			$this->searchRestriction = false;
			$this->isSearching = true;	// flag used to indicate that search has been stopped by user

			// Parse Restriction
			$this->parseSearchRestriction($action);
			if($this->searchRestriction == false) {
				// if error in creating restriction then send error to client
				$errorInfo = array();
				$errorInfo["error_message"] = _("Error in search, please try again") . ".";
				$errorInfo["original_error_message"] = "Error in parsing restrictions.";

				return $this->sendSearchErrorToClient($store, $entryid, $action, $errorInfo);
			}

			// create or open search folder
			$searchFolder = $this->createSearchFolder($store);
			if($searchFolder === false) {
				// if error in creating search folder then send error to client
				$errorInfo = array();
				$errorInfo["error_message"] = _("Error in search, please try again") . ".";
				$errorInfo["original_error_message"] = "Error in creating search folder.";

				return $this->sendSearchErrorToClient($store, $entryid, $action, $errorInfo);
			}

			$subfolder_flag = 0;
			if(isset($action["subfolders"]) && $action["subfolders"] == "true") {
				$subfolder_flag = RECURSIVE_SEARCH;
			}

			if(!is_array($entryid)) {
				$entryids = array($entryid);
			} else {
				$entryids = $entryid;
			}

			// check if searchcriteria has changed
			$restrictionCheck = md5(serialize($this->searchRestriction) . serialize($entryids) . $subfolder_flag);

			// check if there is need to set searchcriteria again
			if($restrictionCheck != $this->searchCriteriaCheck) {
				mapi_folder_setsearchcriteria($searchFolder, $this->searchRestriction, $entryids, $subfolder_flag);
				$this->searchCriteriaCheck = $restrictionCheck;

				if(mapi_last_hresult() != NOERROR) {
					// if error in creating restriction then send error to client
					$errorInfo = array();
					$errorInfo["error_message"] = _("Error in search, please try again") . ".";
					$errorInfo["original_error_message"] = "Error setting restrictions on searchfolder.";

					return $this->sendSearchErrorToClient($store, $entryid, $action, $errorInfo);
				}
			}

			unset($action["restriction"]);

			$this->searchActive = true;

			// Sort
			$this->parseSortOrder($action);

			// List columns visible
			$GLOBALS["TableColumns"]->parseVisibleColumns($this->tablecolumns, $action);

			// add folder name column if its not present
			if($GLOBALS["TableColumns"]->getColumn($this->tablecolumns, "parent_entryid") === false) {
				$GLOBALS["TableColumns"]->addColumn($this->tablecolumns, "parent_entryid", true, 6, _("In Folder"), _("Sort Folder"), 90, "folder_name");
			}

			// Create the data array, which will be send back to the client
			$data = array();
			$data["attributes"] = array("type" => "list");
			$data["column"] = $this->tablecolumns;

			// Generate output sort
			$data["sort"] = array();
			$data["sort"] = $this->generateSortOrder(array());		// we don't want any custom mapping of properties so pass empty array in parameter
			$data["has_sorted_results"] = $action["sort_result"];	// we use this as a flag; to keep difference between default search request and a sorting request

			// Wait until we have some data, no point in returning before we have data. Stop waiting after 10 seconds
			$start = time();
			$table = mapi_folder_getcontentstable($searchFolder);
			
			while(time() - $start < 10) {
				$count = mapi_table_getrowcount($table);
				$result = mapi_folder_getsearchcriteria($searchFolder);

				// Stop looping if we have data or the search is finished
				if($count > 0)
					break;
					
				if(($result["searchstate"] & SEARCH_REBUILD) == 0)
					break; // Search is done
				
				sleep(1);
			}

			// Get the table and merge the arrays
			$rowcount = $GLOBALS["settings"]->get("global/rowcount", 50);
			$table = $GLOBALS["operations"]->getTable($store, hex2bin($this->searchFolderEntryId), $this->properties, $this->sort, $this->start, $rowcount, false);
			$data = array_merge($data, $table);

			// remember which entryid's are send to the client
			$this->searchResults = array();
			foreach($table["item"] as $item) {
				// store entryid => last_modification_time mapping
				$this->searchResults[$item["entryid"]["_content"]] = $item["last_modification_time"]["_content"];
			}
			array_push($this->responseData["action"], $data);

			$result = mapi_folder_getsearchcriteria($searchFolder);

			$data = array();
			$data["attributes"] = array("type" => "search");
			$data["searchfolderentryid"] = array("attributes" => array("type" => "binary"), "_content" => $this->searchFolderEntryId);
			$data["searchstate"] = $result["searchstate"];
			$data["results"] = count($this->searchResults);

			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);

			return true;
		}

		/**
		 *	Function will check for the status of the search on server
		 *	and it will also send intermediate results of search, so we don't have to wait
		 *	untill search is finished on server to send results
		 *	@param		object		$store		MAPI Message Store Object
		 *	@param		hexString	$entryid	entryid of the folder
		 *	@param		object		$action		the action data, sent by the client
		 */
		function updatesearch($store, $entryid, $action)
		{
			// Only allow to return anything if the search is still going on
			if(!$this->isSearching){
				return true;
			}

			if(bin2hex($entryid) != $this->searchFolderEntryId) {
				// entryids not matched
				$data = array();
				$data["attributes"] = array("type" => "search");
				$data["searcherror"] = _("Error in search") . ".";
				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
				return true;
			}

			$folder = mapi_msgstore_openentry($store, hex2bin($this->searchFolderEntryId));
			$searchResult = mapi_folder_getsearchcriteria($folder);
			$searchState = $searchResult["searchstate"];
			$table = mapi_folder_getcontentstable($folder);

			if(is_array($this->sort) && count($this->sort) > 0) {
				// this sorting will be done on currently fetched results, not all results
				// @TODO find a way to do sorting on all search results
				mapi_table_sort($table, $this->sort);
			}

			$rowCount = $GLOBALS["settings"]->get("global/rowcount", 50);
			// searchResults contains entryids of messages
			// that are already sent to the server
			$numberOfResults = count($this->searchResults);

			if($numberOfResults < $rowCount) {
				$items = mapi_table_queryallrows($table, array(PR_ENTRYID, PR_LAST_MODIFICATION_TIME));

				foreach($items as $props) {
					$sendItemToClient = false;

					if(!array_key_exists(bin2hex($props[PR_ENTRYID]), $this->searchResults)) {
						$sendItemToClient = true;
					} else {
						/**
						 * it could happen that an item in search folder has been changed
						 * after we have sent it to client, so we have to again send it
						 * so we will have to use last_modification_time of item to check
						 * that item has been modified since we have sent it to client
						 */
						// TODO if any item is deleted from search folder it will be not notified to client
						if($this->searchResults[bin2hex($props[PR_ENTRYID])] < $props[PR_LAST_MODIFICATION_TIME]) {
							$sendItemToClient = true;
						}
					}

					if($sendItemToClient) {
						$itemData = array();
						$itemData["attributes"] = array("type" => "item", "searchfolder" => $this->searchFolderEntryId);
						// only get primitive properties, no need to get body, attachments or recipient information
						$itemData["item"] = $GLOBALS["operations"]->getProps($store, $GLOBALS["operations"]->openMessage($store, $props[PR_ENTRYID]), $this->properties);
						// store entryid => last_modification_time mapping
						$this->searchResults[bin2hex($props[PR_ENTRYID])] = $props[PR_LAST_MODIFICATION_TIME];

						array_push($this->responseData["action"], $itemData);
					}

					// when we have more results then fit in the client, we break here,
					// we only need to update the counters from this point
					$numberOfResults = count($this->searchResults);
					if($numberOfResults >= $rowCount) {
						break; 
					}
				}
			}

			$totalRowCount = mapi_table_getrowcount($table);

			$data = array();
			$data["attributes"] = array("type" => "search");
			$data["searchfolderentryid"] = array("attributes" => array("type" => "binary"), "_content" => $this->searchFolderEntryId);
			$data["searchstate"] = $searchState;
			$data["results"] = $numberOfResults;		// actual number of items that we are sending to client

			$data["page"] = array();
			$data["page"]["start"] = 0;
			$data["page"]["rowcount"] = $rowCount;
			$data["page"]["totalrowcount"] = $totalRowCount;	// total number of items

			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);

			return true;
		}

		/**
		 *	Function will stop search on the server if search folder exists
		 *	@param		object		$store		MAPI Message Store Object
		 *	@param		hexString	$entryid	entryid of the folder
		 *	@param		object		$action		the action data, sent by the client
		 */
		function stopSearch($store, $entryid, $action)
		{
			if(isset($this->isSearching) && $this->isSearching == true) {
				$this->isSearching = false;
				$this->searchActive = false;

				$searchFolder = mapi_msgstore_openentry($store, $entryid);
				$searchResult = mapi_folder_getsearchcriteria($searchFolder);

				// check if search folder exists and search is in progress
				if(mapi_last_hresult() == NOERROR && $searchResult !== false) {
					mapi_folder_setsearchcriteria($searchFolder, $searchResult['restriction'], $searchResult['folderlist'], STOP_SEARCH);
				}

				/**
				 * when stopping search process, we have to remove search folder also,
				 * so next search request with same restriction will not get uncompleted results
				 */
				if(method_exists($this, "deleteSearchFolder")) {
					$this->deleteSearchFolder($store, $entryid, $action);
				}
			}
		}

		/**
		 *	Function will create a search folder in FINDER_ROOT folder
		 *	if folder exists then it will open it
		 *	@param		object				$store			MAPI Message Store Object
		 *	@param		boolean				$openIfExists	open if folder exists
		 *	@return		mapiFolderObject	$folder			created search folder
		 */
		function createSearchFolder($store, $openIfExists = true)
		{
			if($this->searchFolderEntryId && $openIfExists) {
				$searchFolder = mapi_msgstore_openentry($store, hex2bin($this->searchFolderEntryId));
				if($searchFolder !== false && mapi_last_hresult() == NOERROR) {
					// search folder exists, don't create new search folder
					return $searchFolder;
				}
			}

			// create new search folder
			$searchFolderRoot = $this->getSearchFoldersRoot($store);
			if($searchFolderRoot === false) {
				// error in finding search root folder
				// or store doesn't support search folders
				return false;
			}

			// check for folder name conflict, if conflicts then function will return new name
			$folderName = $GLOBALS["operations"]->checkFolderNameConflict($store, $searchFolderRoot, "WebAccess Search Folder");
			$searchFolder = mapi_folder_createfolder($searchFolderRoot, $folderName, null, 0, FOLDER_SEARCH);

			if($searchFolder !== false && mapi_last_hresult() == NOERROR) {
				$props = mapi_getprops($searchFolder, array(PR_ENTRYID));
				$this->searchFolderEntryId = bin2hex($props[PR_ENTRYID]);
				return $searchFolder;
			} else {
				return false;
			}
		}

		/**
		 *	Function will open FINDER_ROOT folder in root container
		 *	public folder's don't have FINDER_ROOT folder
		 *	@param		object				$store		MAPI message store object
		 *	@return		mapiFolderObject	root		folder for search folders
		 */
		function getSearchFoldersRoot($store)
		{
			$searchRootFolder = true;

			// check if we can create search folders
			$storeProps = mapi_getprops($store, array(PR_STORE_SUPPORT_MASK, PR_FINDER_ENTRYID));
			if(($storeProps[PR_STORE_SUPPORT_MASK] & STORE_SEARCH_OK) != STORE_SEARCH_OK) {
				// store doesn't support search folders
				// public store don't have FINDER_ROOT folder
				$searchRootFolder = false;
			}

			if($searchRootFolder) {
				// open search folders root
				$searchRootFolder = mapi_msgstore_openentry($store, $storeProps[PR_FINDER_ENTRYID]);
				if(mapi_last_hresult() != NOERROR) {
					$searchRootFolder = false;
				}
			}

			return $searchRootFolder;
		}

		/**
		 *	Function will send error message to client if any error has occured in search
		 *	@param		object		$store		MAPI Message Store Object
		 *	@param		hexString	$entryid	entryid of the folder
		 *	@param		object		$action		the action data, sent by the client
		 *	@param		object		$errorInfo	the error information object
		 */
		function sendSearchErrorToClient($store, $entryid, $action, $errorInfo)
		{
			if($errorInfo) {
				$data = array();
				$data["attributes"] = array("type" => "search_error");
				$data["error_message"] = $errorInfo["error_message"];
				if(isset($errorInfo["searchfolderentryid"]) && $errorInfo["searchfolderentryid"]) {
					$data["searchfolderentryid"] = array("attributes" => array("type" => "binary"), "_content" => $errorInfo["searchfolderentryid"]);
				}

				if(isset($errorInfo["original_error_message"]) && $errorInfo["original_error_message"]) {
					dump($errorInfo["original_error_message"]);
					$data["original_error_message"] = $errorInfo["original_error_message"];
				}

				if(mapi_last_hresult() != NOERROR) {
					// if mapi error then send that data also
					$data["hresult"] = mapi_last_hresult();
					$data["hresult_name"] = get_mapi_error_name();
				}

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);

				// after sending error, remove error data
				$errorInfo = array();
			}

			return false;
		}

		/**
		 *	Function will create search restriction based on restriction array
		 *	if search is not implemented in module then it will not have this function,
		 *	therefore creating a fallback function that will parse start restriction
		 *	instead of search restriction
		 *	@param		object		$action		the action data, sent by the client
		 */
		function parseSearchRestriction($action)
		{
			if(isset($action["restriction"])) {
				if(isset($action["restriction"]["selectedmessageid"])) {
					$this->selectedMessageId = hex2bin($action["restriction"]["selectedmessageid"]);
				} elseif(isset($action["restriction"]["start"])) {
					// Set start variable
					$this->start = (int) $action["restriction"]["start"];
					unset($this->selectedMessageId);
				}
			}
		}

		/**
		 * Parses the incoming sort request and builds a MAPI sort order. Normally
		 * properties are mapped from the XML to MAPI by the standard $this->properties mapping. However,
		 * if you want other mappings, you can specify them in the optional $map mapping.
		 * 
		 * $allow_multi_instance is used for creating multiple instance of MV property related items.
		 * $properties is used for using a custom set of properties instead of properties stored in module
		 */
		function parseSortOrder($action, $map = false, $allow_multi_instance = false, $properties = false)
		{
			if(isset($action["sort"]) && isset($action["sort"]["column"])) {
				$this->sort = array();

				if(!$properties) {
					$properties = $this->properties;
				}

				// Unshift MVI_FLAG of MV properties. So the table is not sort on it anymore.
				// Otherwise the server would generate multiple rows for one item (categories).
				foreach($properties as $id => $property)
				{
					switch(mapi_prop_type($property)) 
					{
						case (PT_MV_STRING8 | MVI_FLAG):
						case (PT_MV_LONG | MVI_FLAG):
							$properties[$id] = $properties[$id] &~ MV_INSTANCE;
							break;
					}
				}
				
				// Loop through the sort columns
				foreach($action["sort"]["column"] as $column)
				{
					if(isset($column["attributes"]) && isset($column["attributes"]["direction"])) {
						if(isset($properties[$column["_content"]]) || ($map && isset($map[$column["_content"]]))) {
							if($map && isset($map[$column["_content"]])) 
								$property = $map[$column["_content"]];
							else
								$property = $properties[$column["_content"]];
							
							// Check if column is a MV property
							switch(mapi_prop_type($property)) 
							{
								case PT_MV_STRING8:
								case PT_MV_LONG:
									// Set MVI_FLAG.
									// The server will generate multiple rows for one item (for example: categories)
									if($allow_multi_instance){
										$properties[$column["_content"]] = $properties[$column["_content"]] | MVI_FLAG;
									}
									$property = $properties[$column["_content"]];
									break;
							}

							// Set sort direction
							switch(strtolower($column["attributes"]["direction"]))
							{
								default:
								case "asc":
									$this->sort[$property] = TABLE_SORT_ASCEND;
									break;
								case "desc":
									$this->sort[$property] = TABLE_SORT_DESCEND;
									break;
							}
						}
					}
				}
			}
			
		}
		
		/**
		 * Generates the output sort order to send to the client so it can show sorting in the
		 * column headers. Takes an optional $map when mapping columns to something other than their
		 * standard MAPI property. (same $map as parseSortOrder).
		 *
		 * $properties is used for using a custom set of properties instead of properties stored in module
		 */
		function generateSortOrder($map, $properties = false) {
			$data = array();
			
			if(!$properties) {
				$properties = $this->properties;
			}

			$firstSortColumn = array_shift(array_keys($this->sort));
			$data = array();
			$data["attributes"] = array();
			$data["attributes"]["direction"] = (isset($this->sort[$firstSortColumn]) && $this->sort[$firstSortColumn] == TABLE_SORT_ASCEND) ? "asc":"desc";
			$data["_content"] = array_search($firstSortColumn, $map) ? array_search($firstSortColumn, $map) : array_search($firstSortColumn, $properties);
			
			return $data;
		}

		/**
		 * Function which saves an item.
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid entryid of the folder
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function save($store, $parententryid, $action)
		{
			$result = false;

			if($store && $parententryid && isset($action["props"])) {
				$messageProps = array();
				$result = $GLOBALS["operations"]->saveMessage($store, $parententryid, Conversion::mapXML2MAPI($this->properties, $action["props"]), null, "", $messageProps);
	
				if($result) {
					$GLOBALS["bus"]->notify($this->entryid, TABLE_SAVE, $messageProps);
				}
			}
			
			return $result;
		}
		
		/**
		 * Function which sets the PR_MESSAGE_FLAGS property of an item.
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the item
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function setReadFlag($store, $entryid, $action)
		{
			$result = false;
			
			if($store && $entryid) {
				$flags = "read,noreceipt";
				if(isset($action["flag"])) {
					$flags = $action["flag"];
				}
			
				$props = array();
				$result = $GLOBALS["operations"]->setMessageFlag($store, $entryid, $flags, $props);
	
				if($result) {
					$GLOBALS["bus"]->notify($this->entryid, TABLE_SAVE, $props);
				}
			}
			
			return $result;
		}

		/**
		 * Function which create a task from a mail by copying its subject and body.
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the item
		 * @return boolean true on success or false on failure		 		 
		 */
		function createTaskFromMail($store, $entryid)
		{
			$result = false;

			if($store && $entryid) {
				
				$inbox = mapi_msgstore_getreceivefolder($store);
				if(mapi_last_hresult() == NOERROR) {
					$inboxprops = mapi_getprops($inbox, Array(PR_IPM_TASK_ENTRYID, PR_STORE_ENTRYID));
					$parententryid = bin2hex($inboxprops[PR_IPM_TASK_ENTRYID]);
				}
				// open the userstore to fetch the name of the owner of the store.
				$userstore = mapi_openmsgstore($GLOBALS["mapisession"]->getSession(),$inboxprops[PR_STORE_ENTRYID]);
				$userstoreprops = mapi_getprops($userstore, Array(PR_MAILBOX_OWNER_NAME));

				// open the mail to fetch the subject, body and importance.
				$message = mapi_msgstore_openentry($store, $entryid);
				$msgprops= mapi_getprops($message, Array(PR_SUBJECT, PR_BODY, PR_IMPORTANCE));

				$props = array ('message_class' => 'IPM.Task',
								'icon_index' => '1280',
								'subject' => w2u($msgprops[PR_SUBJECT]),
								'importance' => $msgprops[PR_IMPORTANCE],
								'body' => w2u($msgprops[PR_BODY]),
								'owner' => w2u($userstoreprops[PR_MAILBOX_OWNER_NAME]),
								'complete' => "false",
								);
				
				$messageProps = array(); // returned props
				$result = $GLOBALS["operations"]->saveMessage($store, hex2bin($parententryid),Conversion::mapXML2MAPI($GLOBALS["properties"]->getTaskProperties(), $props) , null, "", $messageProps);
				
				$data = array();
				$data["attributes"] = array("type" => "task_created");
				$data["success"] = $result;

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);

				if($result) {
					$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_SAVE, $messageProps);
				}
			}
			return $result;
		}
		
		/**
		 * Function which deletes one or more items.
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid entryid of the folder
		 * @param array $entryid list of entryids which will be deleted		 
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function delete($store, $parententryid, $entryids, $action)
		{
			$result = false;
			
			if($store && $parententryid && $entryids) {
				$props = array();
				$props[PR_PARENT_ENTRYID] = $parententryid;
				$props[PR_ENTRYID] = $entryids;

				$storeprops = mapi_getprops($store, array(PR_ENTRYID));
				$props[PR_STORE_ENTRYID] = $storeprops[PR_ENTRYID];

				// Check for soft delete (Shift + Del)
				$softDelete = isset($action["softdelete"]) && $action["softdelete"] ? $action["softdelete"] : false;
				
				$result = $GLOBALS["operations"]->deleteMessages($store, $parententryid, $entryids, $softDelete);

				if($result) {
					$GLOBALS["bus"]->notify($this->entryid, TABLE_DELETE, $props);

					// because we don't have real notifications and we know that most of the time deleted items are going to
					// the "Deleted Items"-folder, we notify the bus that that folder is changed to update the counters
					$msgprops = mapi_getprops($store, array(PR_IPM_WASTEBASKET_ENTRYID));

					if ($msgprops[PR_IPM_WASTEBASKET_ENTRYID]!=$this->entryid && !$softDelete){ // only when we are not deleting within the trash itself

						$props[PR_PARENT_ENTRYID] = $msgprops[PR_IPM_WASTEBASKET_ENTRYID];
						$GLOBALS["bus"]->notify(bin2hex($msgprops[PR_IPM_WASTEBASKET_ENTRYID]), TABLE_SAVE, $props);
					}
				}else{
					$data = array();
					$data["attributes"] = array("type" => "failed");
					$data["action_type"] = 'delete';
	
					array_push($this->responseData["action"], $data);
					$GLOBALS["bus"]->addData($this->responseData);
				}
			}
		
			return $result;
		}
		
		/**
		 * Function which copies or moves one or more items.
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid entryid of the folder
		 * @param array $entryid list of entryids which will be copied or moved		 
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function copy($store, $parententryid, $entryids, $action)
		{
			$result = false;
			
			if($store && $parententryid && $entryids) {
				$dest_store = $store;
				if(isset($action["destinationstore"])) {
					$dest_storeentryid = hex2bin($action["destinationstore"]);
					$dest_store = $GLOBALS["mapisession"]->openMessageStore($dest_storeentryid);
				}
				
				$dest_folderentryid = false;
				if(isset($action["destinationfolder"])) {
					$dest_folderentryid = hex2bin($action["destinationfolder"]);
				}

				$moveMessages = false;
				if(isset($action["movemessages"])) {
					$moveMessages = true;
				}

				// drag & drop from a public store to other store should always be copy instead of move
				$destStoreProps = mapi_getprops($dest_store, array(PR_MDB_PROVIDER));
				$storeProps = mapi_getprops($store, array(PR_MDB_PROVIDER));

				if($storeProps[PR_MDB_PROVIDER] == ZARAFA_STORE_PUBLIC_GUID && $destStoreProps[PR_MDB_PROVIDER] != ZARAFA_STORE_PUBLIC_GUID) {
					$moveMessages = false;
				}

				$props = array();
				$props[PR_PARENT_ENTRYID] = $parententryid;
				$props[PR_ENTRYID] = $entryids;			
				
				$storeprops = mapi_getprops($store, array(PR_ENTRYID));
				$props[PR_STORE_ENTRYID] = $storeprops[PR_ENTRYID];
				
				$result = $GLOBALS["operations"]->copyMessages($store, $parententryid, $dest_store, $dest_folderentryid, $entryids, $moveMessages);
				
				if($result) {
					if($moveMessages) {
						$GLOBALS["bus"]->notify($this->entryid, TABLE_DELETE, $props);
					}
					
					$props[PR_PARENT_ENTRYID] = $dest_folderentryid;
					$props[PR_STORE_ENTRYID] = $dest_storeentryid;
					$GLOBALS["bus"]->notify(bin2hex($dest_folderentryid), TABLE_SAVE, $props);
				}
			}
			
			return $result;
		}

		/**
		 * If an event elsewhere has occurred, it enters in this methode. This method
		 * executes one ore more actions, depends on the event.
		 * @param int $event Event.
		 * @param string $entryid Entryid.
		 * @param array $data array of data.
		 */
		function update($event, $entryid, $props)
		{
			$this->reset();

			switch($event)
			{
				case TABLE_SAVE:
					$data = array();
					$data["attributes"] = array("type" => "item");

					if(isset($props[PR_STORE_ENTRYID])) {
						$store = $GLOBALS["mapisession"]->openMessageStore($props[PR_STORE_ENTRYID]);
						
						if(isset($props[PR_ENTRYID])) {
							$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $GLOBALS["operations"]->openMessage($store, $props[PR_ENTRYID]), $this->properties);
						}
					}
					
					array_push($this->responseData["action"], $data);
					break;
				case TABLE_DELETE:
					// When user has used searchfolder the entryID of that folder should be used.
					if(!$this->searchFolderList){
						$folderEntryID = (isset($props[PR_PARENT_ENTRYID]))?$props[PR_PARENT_ENTRYID]:false;
					}else{
						$folderEntryID = (isset($this->searchFolderEntryId))?hex2bin($this->searchFolderEntryId):false;
					}

					if(isset($props[PR_ENTRYID]) && $folderEntryID) {
						// Get items, which are shown under the table.
						$store = $GLOBALS["mapisession"]->openMessageStore($props[PR_STORE_ENTRYID]);
						
						$deletedrows = 1;
						if(is_array($props[PR_ENTRYID])) {
							$deletedrows = count($props[PR_ENTRYID]);
						}

						$newItemsStart = ($this->start + (int)$GLOBALS["settings"]->get("global/rowcount", 50)) - $deletedrows;
						$newItems = $GLOBALS["operations"]->getTable($store, $folderEntryID, $this->properties, $this->sort, $newItemsStart, $deletedrows, $this->searchRestriction);
						
						if(count($newItems["item"]) > 0) {
							$data = array();
							$data["delete"] = 1;
							if(!$this->searchFolderList){
								$data["attributes"] = array ("type" => "item");
							}else{
								$data["attributes"] = array("type" => "item", "searchfolder" => $this->searchFolderEntryId);
							}
							$data["item"] = $newItems["item"];
						
							array_push($this->responseData["action"], $data);
						}
						
						$data = array();
						$data["attributes"] = array("type" => "delete");
						$data["page"] = $newItems["page"];
						$data["page"]["start"] = $this->start;
						$data["page"]["rowcount"] = $GLOBALS["settings"]->get("global/rowcount", 50);
						$data["parent_entryid"] = bin2hex($folderEntryID);
						
						if(is_array($props[PR_ENTRYID])) {
							$data["entryid"] = array();
							
							foreach($props[PR_ENTRYID] as $entryid)
							{
								array_push($data["entryid"], bin2hex($entryid));
							}
						} else {
							$data["entryid"] = bin2hex($props[PR_ENTRYID]);
						}
						
						array_push($this->responseData["action"], $data);
					}
					break;
			}
			
			$GLOBALS["bus"]->addData($this->responseData);
		}

		/* modified vesion of deletesmodule class */
		function getDelegateIndex($entryid, $delegates)
		{
			// Check if user is existing delegate.
			if(isset($delegates) && is_array($delegates)) {
				$eidstr = bin2hex($entryid);
				$obj = new EntryId();
				for($i=0; $i<count($delegates); $i++) {
					if ($obj->compareABEntryIds($eidstr, bin2hex($delegates[$i]))) {
						return $i;
					}
				}
			}
			return false;
		}

		/**
		 * Placeholder function
		 * This function will be overridden by child classes
		 * Basically, function will to remove some properties when the item is private
		 * and it will also check for delegate permissions.
		 * @param object $item item properties
		 * @param flag $displayItem checks wheather we should display the private items
		 * @return object $item item with modified properties
		 */
		function disablePrivateItem($item, $displayItem = false) 
		{
			$hideItemData = false;

			if((isset($item["private"]) && $item["private"] == true) || (isset($item["sensitivity"]) && $item["sensitivity"] == SENSITIVITY_PRIVATE)) {
				// check for delegate permissions
				if($this->storeProviderGuid[PR_MDB_PROVIDER] == ZARAFA_STORE_DELEGATE_GUID) {
					$hideItemData = true;

					// find delegate properties
					if($this->localFreeBusyFolder !== false) {
						$localFreeBusyFolderProps = mapi_getprops($this->localFreeBusyFolder, array(PR_SCHDINFO_DELEGATE_ENTRYIDS, PR_DELEGATES_SEE_PRIVATE));
						if(mapi_last_hresult() === NOERROR) {
							if(isset($localFreeBusyFolderProps[PR_SCHDINFO_DELEGATE_ENTRYIDS]) && isset($localFreeBusyFolderProps[PR_DELEGATES_SEE_PRIVATE])) {
								// if more then one delegates info is stored then find index of 
								// current user
								$userEntryId = $GLOBALS["mapisession"]->getUserEntryID();
								$userIndex = $this->getDelegateIndex($userEntryId, $localFreeBusyFolderProps[PR_SCHDINFO_DELEGATE_ENTRYIDS]);

								if($userIndex !== false && $localFreeBusyFolderProps[PR_DELEGATES_SEE_PRIVATE][$userIndex] === 1) {
									$hideItemData = false;
								}
							}
						}
					}
				}
			}
			if($hideItemData && $displayItem) {
				$item["subject"] = _("Private Appointment");
				$item["location"] = "";
				$item["reminder"] = 0;
				$item["disabled_item"] = 1;
				return $item;
			} else if(!$hideItemData){
				return $item;
			}
		}
		
		/**
		 * Function which gets the delegation details from localfreebusy folder 
		 * @param object $store MAPI Message Store Object
		 */
		function getDelegateFolderInfo($store)
		{
			$this->localFreeBusyFolder = false;

			if(!(isset($store) && $store)) {
				// only continue if store is passed
				return;
			}

			$this->storeProviderGuid = mapi_getprops($store, array(PR_MDB_PROVIDER));

			// open localfreebusy folder for delegate permissions
			$rootFolder = mapi_msgstore_openentry($store, null);
			$rootFolderProps = mapi_getprops($rootFolder, array(PR_FREEBUSY_ENTRYIDS));
			/**
			 *	PR_FREEBUSY_ENTRYIDS contains 4 entryids
			 *	PR_FREEBUSY_ENTRYIDS[0] gives associated freebusy folder in calendar
			 *	PR_FREEBUSY_ENTRYIDS[1] Localfreebusy (used for delegate properties)
			 *	PR_FREEBUSY_ENTRYIDS[2] global Freebusydata in public store
			 *	PR_FREEBUSY_ENTRYIDS[3] Freebusydata in IPM_SUBTREE
			 */
			// get localfreebusy folder
			$this->localFreeBusyFolder = mapi_msgstore_openentry($store, $rootFolderProps[PR_FREEBUSY_ENTRYIDS][1]);
			if(mapi_last_hresult() !== NOERROR) {
				$this->localFreeBusyFolder = false;
			}
		}
	}
?>
