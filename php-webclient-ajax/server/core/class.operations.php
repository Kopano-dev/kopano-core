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
	* General operations
	*
	* All mapi operations, like create, change and delete, are set in this class.
	* A module calls one of these methods.
	*
	* Note: All entryids in this class are binary
	*
	* @todo This class is bloated. It also returns data in various arbitrary formats
	* that other functions depend on, making lots of code almost completely unreadable.
	* @package core
	*/

	include_once("server/core/class.filter.php");
	include_once("mapi/class.recurrence.php");
	include_once("mapi/class.taskrecurrence.php");
	include_once("mapi/class.meetingrequest.php");
	include_once("mapi/class.taskrequest.php");
	include_once("mapi/class.freebusypublish.php");

	class Operations
	{
		function Operations()
		{
		}

		/**
		* Gets the hierarchy list of all required stores.
		*
		* getHierarchyList builds an entire hierarchy list of all folders that should be shown in various places. Most importantly,
		* it generates the list of folders to be show in the hierarchylistmodule (left-hand folder browser) on the client.
		*
		* It is also used to generate smaller hierarchy lists, for example for the 'create folder' dialog.
		*
		* The returned array is a flat array of folders, so if the caller wishes to build a tree, it is up to the caller to correlate
		* the entryids and the parent_entryids of all the folders to build the tree.
		*
		* The return value is an associated array with the following keys:
		* - attributes: array("type" => "list")
		* - store: array of stores
		*
		* Each store contains:
        * - attributes: array("id" => entryid of store, name => name of store, subtree => entryid of viewable root, type => default|public|other, foldertype => "all")
		* - folder: array of folders with each an array of properties (see Operations::setFolder() for properties)
		*
		* @param array $properties MAPI property mapping for folders
		* @param int $type Which stores to fetch (HIERARCHY_GET_ALL | HIERARCHY_GET_DEFAULT | HIERARCHY_GET_SPECIFIC )
		* @param object $store Only when $type == HIERARCHY_GET_SPECIFIC
		*
		* @return array Return structure
		*/
		function getHierarchyList($properties, $type = HIERARCHY_GET_ALL, $store = null)
		{

			switch($type)
			{
				case HIERARCHY_GET_ALL:
					$storelist = $GLOBALS["mapisession"]->getAllMessageStores();
					break;

				case HIERARCHY_GET_DEFAULT:
					$storelist = array($GLOBALS["mapisession"]->getDefaultMessageStore());
					break;

				case HIERARCHY_GET_SPECIFIC:
					$storelist = (is_array($store))?$store:array($store);
					break;
			}

			$data = array();
			$data["attributes"] = array("type" => "list");
			$data["store"] = array();

			foreach($storelist as $store)
			{
				$msgstore_props = mapi_getprops($store, array(PR_ENTRYID, PR_DISPLAY_NAME, PR_IPM_SUBTREE_ENTRYID, PR_IPM_OUTBOX_ENTRYID, PR_IPM_SENTMAIL_ENTRYID, PR_IPM_WASTEBASKET_ENTRYID, PR_MDB_PROVIDER, PR_IPM_PUBLIC_FOLDERS_ENTRYID, PR_IPM_FAVORITES_ENTRYID, PR_MAILBOX_OWNER_ENTRYID));

				$inboxProps = array();

				switch ($msgstore_props[PR_MDB_PROVIDER])
				{
					case ZARAFA_SERVICE_GUID:
						$storeType = "default";
						break;
					case ZARAFA_STORE_PUBLIC_GUID:
						$storeType = "public";
						break;
					case ZARAFA_STORE_DELEGATE_GUID:
						$storeType = "other";
						break;
					case ZARAFA_STORE_ARCHIVER_GUID:
						$storeType = 'archive';
				}

				/**
				 * storetype is public and if public folder is disabled
				 * then continue in loop for next store.
				 */
				if($storeType == "public" && ENABLE_PUBLIC_FOLDERS == false)
					continue;

				$storeData = array();
				$storeData["attributes"] = array(	"id" => bin2hex($msgstore_props[PR_ENTRYID]),
													"name" => windows1252_to_utf8($msgstore_props[PR_DISPLAY_NAME]),
													"subtree" => bin2hex($msgstore_props[PR_IPM_SUBTREE_ENTRYID]),
													'mailbox_owner' => bin2hex($msgstore_props[PR_MAILBOX_OWNER_ENTRYID]),
													"type" => $storeType,
													"foldertype" => "all"
												);

				// save username  and emailaddress if other store
				if ($storeType == "other"){
					$username = $GLOBALS["mapisession"]->getUserNameOfStore($msgstore_props[PR_ENTRYID]);
					$userinfo = mapi_zarafa_getuser_by_name($store, u2w($username));
					$storeData["attributes"]["username"] = $username;
					$storeData["attributes"]["emailaddress"] = $userinfo["emailaddress"];
					$storeData["attributes"]["userfullname"] = w2u($userinfo["fullname"]);
				}

				// public store doesn't have an inbox
				if ($storeType != "public"){
					$inbox = mapi_msgstore_getreceivefolder($store);

					if(!mapi_last_hresult()) {
						$inboxProps = mapi_getprops($inbox, array(PR_ENTRYID));
					}
				}

				$root = mapi_msgstore_openentry($store, null);
				$rootProps = mapi_getprops($root, array(PR_IPM_APPOINTMENT_ENTRYID, PR_IPM_CONTACT_ENTRYID, PR_IPM_DRAFTS_ENTRYID, PR_IPM_JOURNAL_ENTRYID, PR_IPM_NOTE_ENTRYID, PR_IPM_TASK_ENTRYID, PR_ADDITIONAL_REN_ENTRYIDS));

				$additional_ren_entryids = array();
				if(isset($rootProps[PR_ADDITIONAL_REN_ENTRYIDS])) $additional_ren_entryids = $rootProps[PR_ADDITIONAL_REN_ENTRYIDS];

				$defaultfolders = array(
						"inbox"			=>	array("inbox"=>PR_ENTRYID),
						"outbox"		=>	array("store"=>PR_IPM_OUTBOX_ENTRYID),
						"sent"			=>	array("store"=>PR_IPM_SENTMAIL_ENTRYID),
						"wastebasket"	=>	array("store"=>PR_IPM_WASTEBASKET_ENTRYID),
						"favorites"		=>	array("store"=>PR_IPM_FAVORITES_ENTRYID),
						"publicfolders"	=>	array("store"=>PR_IPM_PUBLIC_FOLDERS_ENTRYID),
						"calendar"		=>	array("root" =>PR_IPM_APPOINTMENT_ENTRYID),
						"contact"		=>	array("root" =>PR_IPM_CONTACT_ENTRYID),
						"drafts"		=>	array("root" =>PR_IPM_DRAFTS_ENTRYID),
						"journal"		=>	array("root" =>PR_IPM_JOURNAL_ENTRYID),
						"note"			=>	array("root" =>PR_IPM_NOTE_ENTRYID),
						"task"			=>	array("root" =>PR_IPM_TASK_ENTRYID),
						"junk"			=>	array("additional" =>4),
						"syncissues"	=>	array("additional" =>1),
						"conflicts"		=>	array("additional" =>0),
						"localfailures"	=>	array("additional" =>2),
						"serverfailures"=>	array("additional" =>3),
				);

				$storeData["defaultfolders"] = array();
				foreach($defaultfolders as $key=>$prop){
					$tag = reset($prop);
					$from = key($prop);
					switch($from){
						case "inbox":
							if(isset($inboxProps[$tag])) $storeData["defaultfolders"][$key] = bin2hex($inboxProps[$tag]);
							break;
						case "store":
							if(isset($msgstore_props[$tag])) $storeData["defaultfolders"][$key] = bin2hex($msgstore_props[$tag]);
							break;
						case "root":
							if(isset($rootProps[$tag])) $storeData["defaultfolders"][$key] = bin2hex($rootProps[$tag]);
							break;
						case "additional":
							if(isset($additional_ren_entryids[$tag])) $storeData["defaultfolders"][$key] = bin2hex($additional_ren_entryids[$tag]);
					}
				}

				$storeData["folder"] = array();

				// check if we just want a single folder or a whole store
				$singleFolder = false;
				if ($storeType == "other"){
					$otherusers = $GLOBALS["mapisession"]->retrieveOtherUsersFromSettings();

					if(is_array($otherusers)) {
						foreach($otherusers as $username=>$sharedfolders) {
							if (strcasecmp($username, $storeData["attributes"]["username"]) == 0){
								if (isset($sharedfolders["all"])){
									break; // when foldertype is "all" we only need the whole store, so break the for-loop
								}

								// Get the user information and from store and username to show full name of user.
								$userinfo = mapi_zarafa_getuser_by_name($store, $username);

								// add single folder
								$singleFolder = true;
								foreach($sharedfolders as $type => $sharedfolder){
									// duplicate storeData and remove defaultfolder data
									$folderData = $storeData;
									unset($folderData["defaultfolders"]);
									if (!isset($storeData["defaultfolders"][$sharedfolder["type"]])){
										continue; // TODO: no rights? give error to the user
									}

									if($sharedfolder["show_subfolders"] == "true") {
										$add_subfolders = "true";
									} else {
										$add_subfolders = "false";
									}

									$folderEntryID = hex2bin($storeData["defaultfolders"][$sharedfolder["type"]]);

									// load folder props
									$folder = mapi_msgstore_openentry($store, $folderEntryID);
									if (mapi_last_hresult()!=NOERROR){
										continue; // TODO: no rights? give error to the user
									}

									// check if we need subfolders or not
									if($add_subfolders === "true") {
										// add folder data (with all subfolders recursively)
										// get parent folder's properties
										$folderProps = mapi_getprops($folder, $properties);
										$tempFolderProps = $this->setFolder($folderProps);

										array_push($folderData['folder'], $tempFolderProps);

										// get subfolders
										if($tempFolderProps["subfolders"] != -1) {
											$subfoldersData = array();
											$subfoldersData["folder"] = array();
											$this->getSubFolders($folder, $store, $properties, $subfoldersData);

											$folderData['folder'] = array_merge($folderData['folder'], $subfoldersData["folder"]);
										}
									} else {
										$folderProps = mapi_getprops($folder, $properties);
										$tempFolderProps = $this->setFolder($folderProps);
 	                                    $tempFolderProps["subfolders"] = -1;
 	                                    array_push($folderData['folder'], $tempFolderProps);
									}

									// change it from storeData to folderData
									$folderData["attributes"]["name"] = w2u($folderProps[PR_DISPLAY_NAME]) ." "._("of")." ".w2u($userinfo["fullname"]);
									$folderData["attributes"]["subtree"] = bin2hex($folderEntryID);
									$folderData["attributes"]["foldertype"] = $sharedfolder["type"];
									$folderData["attributes"]["id"] .= "_" . $sharedfolder["type"];
									$folderData["defaultfolders"] = array($sharedfolder["type"] => bin2hex($folderEntryID));

									// add the folder to the list
									array_push($data["store"], $folderData);
								}
							}
						}
					}
				}

				// add entire store
				if(!$singleFolder && isset($msgstore_props[PR_IPM_SUBTREE_ENTRYID])) {
					$folder = mapi_msgstore_openentry($store, $msgstore_props[PR_IPM_SUBTREE_ENTRYID]);

					if($folder) {
						// Add root folder
						array_push($storeData['folder'], $this->setFolder(mapi_getprops($folder, $properties)));

						if($storeType != "public") {
							// Recursively add all subfolders
							$this->getSubFolders($folder, $store, $properties, $storeData);
						} else {
							$this->getSubFoldersPublic($folder, $store, $properties, $storeData);
						}
						array_push($data["store"], $storeData);
					}
				}
			}

			return $data;
		}

		/**
		 * Helper function to get the subfolders of a folder
		 *
		 * @access private
		 * @param object $folder Mapi Folder Object.
		 * @param object $store Message Store Object
		 * @param array $properties MAPI property mappings for folders
		 * @param array $storeData Reference to an array. The folder properties are added to this array.
		 */
		function getSubFolders($folder, $store, $properties, &$storeData)
		{
			$hierarchyTable = mapi_folder_gethierarchytable($folder, CONVENIENT_DEPTH | MAPI_DEFERRED_ERRORS);

			/**
			 * remove hidden folders, folders with PR_ATTR_HIDDEN property set
			 * should not be shown to the client
			 */
			$restriction =	Array(RES_OR,
								Array(
									Array(RES_PROPERTY,
										Array(
											RELOP => RELOP_EQ,
											ULPROPTAG => PR_ATTR_HIDDEN,
											VALUE => Array( PR_ATTR_HIDDEN => false )
										)
									),
									Array(RES_NOT,
										Array(
											Array(RES_EXIST,
												Array(
													ULPROPTAG => PR_ATTR_HIDDEN
												)
											)
										)
									)
								)
							);

			// Make sure our folders are sorted by name. The ordering from the depth is not important
			// because we send the parent in the folder information anyway.
			mapi_table_sort($hierarchyTable, array(PR_DISPLAY_NAME => TABLE_SORT_ASCEND), TBL_BATCH);
			$subfolders = mapi_table_queryallrows($hierarchyTable, array_merge(Array(PR_ENTRYID,PR_SUBFOLDERS), $properties), $restriction);

			if (is_array($subfolders)) {
				foreach($subfolders as $subfolder)
				{
					array_push($storeData['folder'], $this->setFolder($subfolder));
				}
			}
		}

		/**
		 * Helper to loop through the hierarchy list.in the public and replace the parent entry id
		 *
		 * @access private
		 * @param object $folder Mapi Folder Object
		 * @param object $store Message Store Object
		 * @param array $properties MAPI property mappings for folders
		 * @param array $storeData Reference to an array. The folder properties are added to this array
		*/
		function getSubFoldersPublic($folder, $store, $properties, &$storeData)
		{
			$hierarchyTable = mapi_folder_gethierarchytable($folder, MAPI_DEFERRED_ERRORS);
			mapi_table_sort($hierarchyTable, array(PR_DISPLAY_NAME => TABLE_SORT_ASCEND), TBL_BATCH);

			if (mapi_table_getrowcount($hierarchyTable) == 0) {
				return false;
			}

			/**
			 * remove hidden folders, folders with PR_ATTR_HIDDEN property set
			 * should not be shown to the client
			 */
			$restriction =	Array(RES_OR,
								Array(
									Array(RES_PROPERTY,
										Array(
											RELOP => RELOP_EQ,
											ULPROPTAG => PR_ATTR_HIDDEN,
											VALUE => Array( PR_ATTR_HIDDEN => false )
										)
									),
									Array(RES_NOT,
										Array(
											Array(RES_EXIST,
												Array(
													ULPROPTAG => PR_ATTR_HIDDEN
												)
											)
										)
									)
								)
							);

			$subfolders = mapi_table_queryallrows($hierarchyTable, array_merge(Array(PR_ENTRYID,PR_SUBFOLDERS,PR_PARENT_ENTRYID), $properties), $restriction);

			$folderEntryid = mapi_getprops($folder, array(PR_ENTRYID));
			if (is_array($subfolders)) {
				foreach($subfolders as $subfolder)
				{
					$subfolder[PR_PARENT_ENTRYID] = $folderEntryid[PR_ENTRYID];

					if($subfolder[PR_SUBFOLDERS]) {
						$folderObject = mapi_msgstore_openentry($store, $subfolder[PR_ENTRYID]);

						if ($folderObject) {
							if($this->getSubFoldersPublic($folderObject, $store, $properties, $storeData) == false)
								$subfolder[PR_SUBFOLDERS] = false;
						}
					}
					array_push($storeData['folder'], $this->setFolder($subfolder));
				}
			}

			return true;
		}

		/**
		 * Convert MAPI properties into useful XML properties for a folder
		 *
		 * @access private
		 * @param array $folderProps Properties of a folder
		 * @return array List of properties of a folder
		 * @todo The name of this function is misleading because it doesn't 'set' anything, it just reads some properties.
		 */
		function setFolder($folderProps)
		{
			$props = array();
			$props["entryid"] = bin2hex($folderProps[PR_ENTRYID]);
			$props["parent_entryid"] = bin2hex($folderProps[PR_PARENT_ENTRYID]);
			$props["store_entryid"] = bin2hex($folderProps[PR_STORE_ENTRYID]);
			$props["display_name"] = windows1252_to_utf8($folderProps[PR_DISPLAY_NAME]);

			if(isset($folderProps[PR_CONTAINER_CLASS])) {
				$props["container_class"] = $folderProps[PR_CONTAINER_CLASS];
			} else {
				$props["container_class"] = "IPF.Note";
			}

			//Access levels
			$props["access"] = array();
			$props["access"]["modify"]          = $folderProps[PR_ACCESS] & MAPI_ACCESS_MODIFY;
			$props["access"]["read"]            = $folderProps[PR_ACCESS] & MAPI_ACCESS_READ;
			$props["access"]["delete"]          = $folderProps[PR_ACCESS] & MAPI_ACCESS_DELETE;
			$props["access"]["create_hierarchy"]= $folderProps[PR_ACCESS] & MAPI_ACCESS_CREATE_HIERARCHY;
			$props["access"]["create_contents"] = $folderProps[PR_ACCESS] & MAPI_ACCESS_CREATE_CONTENTS;

			//Rights levels
			$props["rights"] = array();
			if(isset($folderProps[PR_RIGHTS])) {	// store don't have PR_RIGHTS property
				$props["rights"]["deleteowned"]     = $folderProps[PR_RIGHTS] & ecRightsDeleteOwned;
				$props["rights"]["deleteany"]       = $folderProps[PR_RIGHTS] & ecRightsDeleteAny;
			}
			$props["content_count"] = $folderProps[PR_CONTENT_COUNT];
			$props["content_unread"] = $folderProps[PR_CONTENT_UNREAD];
			$props["subfolders"] = $folderProps[PR_SUBFOLDERS]?1:-1;
			$props["store_support_mask"] = $folderProps[PR_STORE_SUPPORT_MASK];

			return $props;
		}

		/**
		 * Create a MAPI folder
		 *
		 * This function simply creates a MAPI folder at a specific location with a specific folder
		 * type.
		 *
		 * @param object $store MAPI Message Store Object in which the folder lives
		 * @param string $parententryid The parent entryid in which the new folder should be created
		 * @param string $name The name of the new folder
		 * @param string $type The type of the folder (PR_CONTAINER_CLASS, so value should be 'IPM.Appointment', etc)
		 * @param array $folderProps reference to an array which will be filled with PR_ENTRYID and PR_STORE_ENTRYID of new folder
		 * @return boolean true if action succeeded, false if not
		 */
		function createFolder($store, $parententryid, $name, $type, &$folderProps)
		{
			$result = false;
			$folder = mapi_msgstore_openentry($store, $parententryid);
			$name = utf8_to_windows1252($name);

			if($folder) {
				$foldername = $this->checkFolderNameConflict($store, $folder, $name);
				$new_folder = mapi_folder_createfolder($folder, $foldername);

				if($new_folder) {
					mapi_setprops($new_folder, array(PR_CONTAINER_CLASS => $type));
					$result = true;

					$folderProps = mapi_getprops($new_folder, array(PR_ENTRYID, PR_STORE_ENTRYID));
				}
			}

			return $result;
		}

		/**
		 * Rename a folder
		 *
		 * This function renames the specified folder. However, a conflict situation can arise
		 * if the specified folder name already exists. In this case, the folder name is postfixed with
		 * an ever-higher integer to create a unique folder name.
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid The entryid of the folder to rename
		 * @param string $name The new name of the folder
		 * @param array $folderProps reference to an array which will be filled with PR_ENTRYID and PR_STORE_ENTRYID
		 * @return boolean true if action succeeded, false if not
		 * @todo Function name should be renameFolder()
		 */
		function modifyFolder($store, $entryid, $name, &$folderProps)
		{
			$result = false;
			$folder = mapi_msgstore_openentry($store, $entryid);
			$name = utf8_to_windows1252($name);

			if($folder && !$this->isSpecialFolder($store, $entryid)) {
				$foldername = $this->checkFolderNameConflict($store, $folder, $name);
				$props = array(PR_DISPLAY_NAME => $foldername);

				mapi_setprops($folder, $props);
				mapi_savechanges($folder);
				$result = true;

				$folderProps = mapi_getprops($folder, array(PR_ENTRYID, PR_STORE_ENTRYID));
			}
			return $result;
		}

		/**
		 * Check if a folder is 'special'
		 *
		 * All default MAPI folders such as 'inbox', 'outbox', etc have special permissions; you can not rename them for example. This
		 * function returns TRUE if the specified folder is 'special'.
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid The entryid of the folder
		 * @return boolean true if folder is a special folder, false if not
		 */
		function isSpecialFolder($store, $entryid)
		{
			$result = true;

			$msgstore_props = mapi_getprops($store, array(PR_IPM_SUBTREE_ENTRYID, PR_IPM_OUTBOX_ENTRYID, PR_IPM_SENTMAIL_ENTRYID, PR_IPM_WASTEBASKET_ENTRYID, PR_MDB_PROVIDER, PR_IPM_PUBLIC_FOLDERS_ENTRYID, PR_IPM_FAVORITES_ENTRYID));

			// "special" folders don't exists in public store
			if ($msgstore_props[PR_MDB_PROVIDER]==ZARAFA_STORE_PUBLIC_GUID){
				$result = false;
			}else if (array_search($entryid, $msgstore_props)===false){
				$inbox = mapi_msgstore_getreceivefolder($store);
				if($inbox) {
					$inboxProps = mapi_getprops($inbox, array(PR_ENTRYID, PR_IPM_APPOINTMENT_ENTRYID, PR_IPM_CONTACT_ENTRYID, PR_IPM_DRAFTS_ENTRYID, PR_IPM_JOURNAL_ENTRYID, PR_IPM_NOTE_ENTRYID, PR_IPM_TASK_ENTRYID, PR_ADDITIONAL_REN_ENTRYIDS));
					if (array_search($entryid, $inboxProps)===false){
						if (isset($inboxProps[PR_ADDITIONAL_REN_ENTRYIDS]) && is_array($inboxProps[PR_ADDITIONAL_REN_ENTRYIDS])){
							if (array_search($entryid, $inboxProps[PR_ADDITIONAL_REN_ENTRYIDS])===false) {
								$result = false;
							}
						} else {
							$result = false;
						}
					}
				}
			}

			return $result;
		}

		/**
		 * Delete a folder
		 *
		 * Deleting a folder normally just moves the folder to the wastebasket, which is what this function does. However,
		 * if the folder was already in the wastebasket, then the folder is really deleted.
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid The parent in which the folder should be deleted
		 * @param string $entryid The entryid of the folder which will be deleted
		 * @param array $folderProps reference to an array which will be filled with PR_ENTRYID, PR_STORE_ENTRYID of the deleted object
		 * @return boolean true if action succeeded, false if not
		 * @todo subfolders of folders in the wastebasket should also be hard-deleted
		 */
		function deleteFolder($store, $parententryid, $entryid, &$folderProps)
		{
			$result = false;
			$msgprops = mapi_getprops($store, array(PR_IPM_WASTEBASKET_ENTRYID));
			$folder = mapi_msgstore_openentry($store, $parententryid);

			if($folder && !$this->isSpecialFolder($store, $entryid)) {
				if(isset($msgprops[PR_IPM_WASTEBASKET_ENTRYID])) {
					// TODO: check if not only $parententryid=wastebasket, but also the parents of that parent...
					if($msgprops[PR_IPM_WASTEBASKET_ENTRYID] == $parententryid) {
						if(mapi_folder_deletefolder($folder, $entryid, DEL_MESSAGES | DEL_FOLDERS)) {
							$result = true;

							// if exists, also delete settings made for this folder (client don't need an update for this)
							$GLOBALS["settings"]->delete("folders/entryid_".bin2hex($entryid));
						}
					} else {
						$wastebasket = mapi_msgstore_openentry($store, $msgprops[PR_IPM_WASTEBASKET_ENTRYID]);

						$deleted_folder = mapi_msgstore_openentry($store, $entryid);
						$props = mapi_getprops($deleted_folder, array(PR_DISPLAY_NAME));
						$foldername = $this->checkFolderNameConflict($store, $wastebasket, $props[PR_DISPLAY_NAME]);

						if(mapi_folder_copyfolder($folder, $entryid, $wastebasket, $foldername, FOLDER_MOVE)) {
							$result = true;

							$folderProps = mapi_getprops($deleted_folder, array(PR_ENTRYID, PR_STORE_ENTRYID));
						}
					}
				} else {
					if(mapi_folder_deletefolder($folder, $entryid, DEL_MESSAGES | DEL_FOLDERS)) {
						$result = true;

						// if exists, also delete settings made for this folder (client don't need an update for this)
						$GLOBALS["settings"]->delete("folders/entryid_".bin2hex($entryid));
					}
				}
			}

			return $result;
		}

		/**
		 * Empty folder
		 *
		 * Removes all items from a folder. This is a real delete, not a move.
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid The entryid of the folder which will be emptied
		 * @param array $folderProps reference to an array which will be filled with PR_ENTRYID and PR_STORE_ENTRYID of the emptied folder
		 * @return boolean true if action succeeded, false if not
		 */
		function emptyFolder($store, $entryid, &$folderProps)
		{
			$result = false;
			$folder = mapi_msgstore_openentry($store, $entryid);

			if($folder) {
				$result = mapi_folder_emptyfolder($folder, DEL_ASSOCIATED);

				$hresult = mapi_last_hresult();
				if ($hresult == NOERROR){
					$folderProps = mapi_getprops($folder, array(PR_ENTRYID, PR_STORE_ENTRYID));
					$result = true;
				}
			}

			return $result;
		}

		/**
		 * Copy or move a folder
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $parentfolderentryid The parent entryid of the folder which will be copied or moved
		 * @param string $sourcefolderentryid The entryid of the folder which will be copied or moved
		 * @param string $destfolderentryid The entryid of the folder which the folder will be copied or moved to
		 * @param boolean $moveFolder true - move folder, false - copy folder
		 * @param array $folderProps reference to an array which will be filled with entryids
		 * @return boolean true if action succeeded, false if not
		 */
		function copyFolder($store, $parentfolderentryid, $sourcefolderentryid, $destfolderentryid, $deststore, $moveFolder, &$folderProps)
		{
			$result = false;
			$sourceparentfolder = mapi_msgstore_openentry($store, $parentfolderentryid);
			$destfolder = mapi_msgstore_openentry($deststore, $destfolderentryid);
			if(!$this->isSpecialFolder($store, $sourcefolderentryid) && $sourceparentfolder && $destfolder && $deststore) {
				$folder = mapi_msgstore_openentry($store, $sourcefolderentryid);
				$props = mapi_getprops($folder, array(PR_DISPLAY_NAME));
				$foldername = $this->checkFolderNameConflict($deststore, $destfolder, $props[PR_DISPLAY_NAME]);
				if($moveFolder) {
					if(mapi_folder_copyfolder($sourceparentfolder, $sourcefolderentryid, $destfolder, $foldername, FOLDER_MOVE)) {
						$result = true;
						$folderProps = mapi_getprops($folder, array(PR_ENTRYID, PR_STORE_ENTRYID));
					}
				} else {
					if(mapi_folder_copyfolder($sourceparentfolder, $sourcefolderentryid, $destfolder, $foldername, COPY_SUBFOLDERS)) {
						$result = true;
					}
				}
			}
			return $result;
		}

		/**
		 * Set the readflags of all messages in a folder to 'read'
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid The entryid of the folder
		 * @param array $folderProps reference to an array which will be filled with PR_ENTRYID and PR_STORE_ENTRYID of the folder
		 * @return boolean true if action succeeded, false if not
		 * @todo This function is message a 'set unread' option
		 */
		function setReadFlags($store, $entryid, &$folderProps)
		{
			$result = false;
			$folder = mapi_msgstore_openentry($store, $entryid);

			if($folder) {
				if(mapi_folder_setreadflags($folder, array(), SUPPRESS_RECEIPT)) {
					$result = true;

					$folderProps = mapi_getprops($folder, array(PR_ENTRYID, PR_STORE_ENTRYID));
				}
			}

			return $result;
		}

		/**
		 * Read MAPI table
		 *
		 * This function performs various operations to open, setup, and read all rows from a MAPI table.
		 *
		 * The output from this function is an XML array structure which can be sent directly to XML serialisation.
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid The entryid of the folder to read the table from
		 * @param array $properties The set of properties which will be read
		 * @param array $sort The set properties which the table will be sort on (formatted as a MAPI sort order)
		 * @param integer $start Starting row at which to start reading rows
		 * @param integer $rowcount Number of rows which should be read
		 * @param array $restriction Table restriction to apply to the table (formatted as MAPI restriction)
		 * @return array XML array structure with row data
		 */
		function getTable($store, $entryid, $properties, $sort, $start, $rowcount = false, $restriction = false)
		{
			$data = array();
			$folder = mapi_msgstore_openentry($store, $entryid);

			if($folder) {
				$table = mapi_folder_getcontentstable($folder, MAPI_DEFERRED_ERRORS);

				if(!$rowcount) {
					$rowcount = $GLOBALS["settings"]->get("global/rowcount", 50);
				}

				$data["page"] = array();
				$data["page"]["start"] = $start;
				$data["page"]["rowcount"] = $rowcount;
				if(is_array($restriction)) {
					mapi_table_restrict($table, $restriction, TBL_BATCH);
				}

				if (is_array($sort) && count($sort)>0){
					/**
					 * If the sort array contains the PR_SUBJECT column we should change this to
					 * PR_NORMALIZED_SUBJECT to make sure that when sorting on subjects: "sweet" and
					 * "RE: sweet", the first one is displayed before the latter one. If the subject
					 * is used for sorting the PR_MESSAGE_DELIVERY_TIME must be added as well as
					 * Outlook behaves the same way in this case.
					 */
					if(isset($sort[PR_SUBJECT])){
						$sortReplace = Array();
						foreach($sort as $key => $value){
							if($key == PR_SUBJECT){
								$sortReplace[PR_NORMALIZED_SUBJECT] = $value;
								$sortReplace[PR_MESSAGE_DELIVERY_TIME] = TABLE_SORT_DESCEND;
							}else{
								$sortReplace[$key] = $value;
							}
						}
						$sort = $sortReplace;
					}
					mapi_table_sort($table, $sort, TBL_BATCH);
				}

				$totalrowcount = mapi_table_getrowcount($table);
				$data["page"]["totalrowcount"] = $totalrowcount;
				$data["item"] = array();

				/**
				 * Retrieving the entries should be done in batches to prevent large ammounts of
				 * items in one list clogging up the memory limit. This is especially important when
				 * dealing with contactlists in the addressbook. Those lists can contain 10K items.
				 */
				$batchcount = 50;
				$end = min($totalrowcount, ($start+$rowcount));
				$diff = $end - $start;
				$numBatches = ceil($diff/$batchcount);

				for($i=0;$i<$numBatches;$i++){
					$batchStart = $start + ($i * $batchcount);
					if($batchStart + $batchcount > $end){
						$batchcount = $end - $batchStart;	// Prevent the last batch to include too many items
					}

					$rows = mapi_table_queryrows($table, $properties, $batchStart, $batchcount);

					foreach($rows as $row){
						array_push($data["item"], Conversion::mapMAPI2XML($properties, $row));
					}
				}
			}

			return $data;
		}

		/**
		 * Find the start row that will cause getTable to output a batch that contains a certain message.
		 *
		 * This function performs various operations to open, setup, and read all rows from a MAPI table.
		 *
		 * The output from this function is an integer, specifying the row to start getTable with.
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid The entryid of the folder to read the table from
		 * @param string $messageid The entryid of the message to include
		 * @param array $sort The set properties which the table will be sort on (formatted as a MAPI sort order)
		 * @param integer $rowcount Number of rows which should be read
		 * @param array $restriction Table restriction to apply to the table (formatted as MAPI restriction)
		 * @return integer row to start reading
		 */
		function getStartRow($store, $entryid, $messageid, $sort, $rowcount = false, $restriction = false)
		{
			$start = 0;
			$folder = mapi_msgstore_openentry($store, $entryid);

			if($folder) {
				$table = mapi_folder_getcontentstable($folder, MAPI_DEFERRED_ERRORS);

				if(!$rowcount) {
					$rowcount = $GLOBALS["settings"]->get("global/rowcount", 50);
				}

				if(is_array($restriction)) {
					mapi_table_restrict($table, $restriction, TBL_BATCH);
				}

				if (is_array($sort) && count($sort)>0){
					/**
					 * If the sort array contains the PR_SUBJECT column we should change this to
					 * PR_NORMALIZED_SUBJECT to make sure that when sorting on subjects: "sweet" and
					 * "RE: sweet", the first one is displayed before the latter one. If the subject
					 * is used for sorting the PR_MESSAGE_DELIVERY_TIME must be added as well as
					 * Outlook behaves the same way in this case.
					 */
					if(isset($sort[PR_SUBJECT])){
						$sortReplace = Array();
						foreach($sort as $key => $value){
							if($key == PR_SUBJECT){
								$sortReplace[PR_NORMALIZED_SUBJECT] = $value;
								$sortReplace[PR_MESSAGE_DELIVERY_TIME] = TABLE_SORT_DESCEND;
							}else{
								$sortReplace[$key] = $value;
							}
						}
						$sort = $sortReplace;
					}
					mapi_table_sort($table, $sort, TBL_BATCH);
				}

				// Not all php-ext implementations have mapi_table_findrow, so check
				// before usage.
				if (function_exists('mapi_table_findrow')) {
					$rowNum = mapi_table_findrow($table, array(RES_PROPERTY, array(RELOP => RELOP_EQ, ULPROPTAG => PR_ENTRYID, VALUE => $messageid)));
					if ($rowNum !== false) {
						$start = floor($rowNum / $rowcount) * $rowcount;
					}
				} else {
					dump('Performing slow search', 'class.operations.php - getStartRow()');
					// If we can't find, we'll just do a brute force search.
					$rowNum = 0;
					$done = false;
					while (!$done) {
						$rows = mapi_table_queryrows($table, array(PR_ENTRYID), $rowNum, 64);
						foreach ($rows as $row) {
							if ($row[PR_ENTRYID] == $messageid) {
								$done = true;
								$start = floor($rowNum / $rowcount) * $rowcount;
								break;
							}
							$rowNum++;
						}
						if(count($rows) < 64)
							break;
					}
				}
			}

			return $start;
		}

		/**
		* Returns TRUE of the MAPI message only has inline attachments
		*
		* @param mapimessage $message The MAPI message object to check
		* @return boolean TRUE if the item contains only inline attachments, FALSE otherwise
		* @deprecated This function is not used, because it is much too slow to run on all messages in your inbox
		*/
		function hasOnlyInlineAttachments($message)
		{
			$attachmentTable = mapi_message_getattachmenttable($message);
			if($attachmentTable) {
				$attachments = mapi_table_queryallrows($attachmentTable, array(PR_ATTACHMENT_HIDDEN));
				foreach($attachments as $attachmentRow)	{
					if(!isset($attachmentRow[PR_ATTACHMENT_HIDDEN]) || !$attachmentRow[PR_ATTACHMENT_HIDDEN]) {
						return false;
					}
				}
			}
			return true;
		}

		/**
		 * Read message properties
		 *
		 * Reads a message and returns the data as an XML array structure with all data from the message that is needed
		 * to show a message (for example in the preview pane)
		 *
		 * @param object $store MAPI Message Store Object
		 * @param object $message The MAPI Message Object
		 * @param array $properties Mapping of properties that should be read
		 * @param boolean $html2text true - body will be converted from html to text, false - html body will be returned
		 * @param string $entryid EntryID used to open $message or the parent
		 * @param array $attachNum Array of attachment numbers to get to $message
		 * @return array item properties
		 * @todo Function name is misleading as it doesn't just get message properties
		 */
		function getMessageProps($store, $message, $properties, $html2text = false, $entryid = false, $attachNum = false)
		{
			$props = array();

			if($message) {
				if (!isset($properties[PR_ENTRYID]))
					$properties["entryid"] = PR_ENTRYID;

				$messageprops = mapi_getprops($message, $properties);

				if (!$entryid)
					$entryid = $messageprops[PR_ENTRYID];

				$props = Conversion::mapMAPI2XML($properties, $messageprops);

				// Get actual SMTP address for sent_representing_email_address and received_by_email_address
				$smtpprops = mapi_getprops($message, array(PR_SENT_REPRESENTING_ENTRYID, PR_RECEIVED_BY_ENTRYID, PR_SENDER_ENTRYID));

				if(isset($smtpprops[PR_SENT_REPRESENTING_ENTRYID]))
    				$props["sent_representing_email_address"] = $this->getEmailAddressFromEntryID($smtpprops[PR_SENT_REPRESENTING_ENTRYID]);

				if(isset($smtpprops[PR_SENDER_ENTRYID]))
    				$props["sender_email_address"] = $this->getEmailAddressFromEntryID($smtpprops[PR_SENDER_ENTRYID]);

                if(isset($smtpprops[PR_RECEIVED_BY_ENTRYID]))
                    $props["received_by_email_address"] = $this->getEmailAddressFromEntryID($smtpprops[PR_RECEIVED_BY_ENTRYID]);

				// Get body content
				$plaintext = $this->isPlainText($message);

				$content = false;
				if (!$plaintext) {
					$cpprops = mapi_message_getprops($message, array(PR_INTERNET_CPID));
					$codepage = isset($cpprops[PR_INTERNET_CPID]) ? $cpprops[PR_INTERNET_CPID] : 1252;
					$content = Conversion::convertCodepageStringToUtf8($codepage, mapi_message_openproperty($message, PR_HTML));

					if($content) {
						$filter = new filter();

						if(!$html2text) {
							$msgstore_props = mapi_getprops($store, array(PR_ENTRYID));

							$content = $filter->safeHTML($content, bin2hex($msgstore_props[PR_ENTRYID]), bin2hex($entryid), $attachNum);
							$props["isHTML"] = true;
						} else {
							$content = false;
						}
					}
				}

				if(!$content) {
					$content = mapi_message_openproperty($message, PR_BODY);
					$props["isHTML"] = false;
				}

				$props["body"] = trim(windows1252_to_utf8($content), "\0");

				// Get reply-to information
				$messageprops = mapi_getprops($message, array(PR_REPLY_RECIPIENT_ENTRIES));
				if(isset($messageprops[PR_REPLY_RECIPIENT_ENTRIES])) {
					$props["reply-to"] = $this->readReplyRecipientEntry($messageprops[PR_REPLY_RECIPIENT_ENTRIES]);
				}

				// Get recipients
				$recipientTable = mapi_message_getrecipienttable($message);
				if($recipientTable) {
					$recipients = mapi_table_queryallrows($recipientTable, array(PR_ADDRTYPE, PR_ENTRYID, PR_DISPLAY_NAME, PR_EMAIL_ADDRESS, PR_SMTP_ADDRESS, PR_RECIPIENT_TYPE, PR_RECIPIENT_FLAGS, PR_PROPOSEDNEWTIME, PR_PROPOSENEWTIME_START, PR_PROPOSENEWTIME_END, PR_RECIPIENT_TRACKSTATUS, PR_OBJECT_TYPE));

					$props["recipients"] = array();
					$props["recipients"]["recipient"] = array();

					foreach($recipients as $recipientRow)
					{
						if (isset($recipientRow[PR_RECIPIENT_FLAGS]) && (($recipientRow[PR_RECIPIENT_FLAGS] & recipExceptionalDeleted) == recipExceptionalDeleted))
							continue;

						$recipient = array();
						$recipient["display_name"] = windows1252_to_utf8(isset($recipientRow[PR_DISPLAY_NAME])?$recipientRow[PR_DISPLAY_NAME]:'');
						$recipient["email_address"] = isset($recipientRow[PR_SMTP_ADDRESS])?$recipientRow[PR_SMTP_ADDRESS]:'';
						$recipient["email_address"] = windows1252_to_utf8($recipient["email_address"]);
						$recipient["objecttype"] = $recipientRow[PR_OBJECT_TYPE];

						if(isset($recipientRow[PR_RECIPIENT_FLAGS])){
							$recipient["recipient_flags"] = $recipientRow[PR_RECIPIENT_FLAGS];
						}

						if ($recipientRow[PR_ADDRTYPE]=="ZARAFA"){
							$recipient["entryid"] = bin2hex($recipientRow[PR_ENTRYID]);
							// Get the SMTP address from the addressbook if no address is found
							if($recipient["email_address"] == ""){
								$recipient["email_address"] = windows1252_to_utf8($GLOBALS['operations']->getEmailAddressFromEntryID($recipientRow[PR_ENTRYID]));
							}
						}
						if($recipient["email_address"] == ""){
							$recipient["email_address"] = isset($recipientRow[PR_EMAIL_ADDRESS])?w2u($recipientRow[PR_EMAIL_ADDRESS]):'';
						}

						switch($recipientRow[PR_RECIPIENT_TYPE])
						{
							case MAPI_ORIG:
								$recipient["type"] = "orig";
								$recipient["recipient_attendees"] = _("Organizor");
								break;
							case MAPI_TO:
								$recipient["type"] = "to";
								$recipient["recipient_attendees"] = _("Required");
								break;
							case MAPI_CC:
								$recipient["type"] = "cc";
								$recipient["recipient_attendees"] = _("Optional");
								break;
							case MAPI_BCC:
								$recipient["type"] = "bcc";
								$recipient["recipient_attendees"] = _("Resource");
								break;
						}

						// Set propose new time properties
						if(isset($recipientRow[PR_PROPOSEDNEWTIME]) && isset($recipientRow[PR_PROPOSENEWTIME_START]) && isset($recipientRow[PR_PROPOSENEWTIME_END])){
							$recipient["proposenewstarttime"] = $recipientRow[PR_PROPOSENEWTIME_START];
							$recipient["proposenewendtime"] = $recipientRow[PR_PROPOSENEWTIME_END];
							$recipient["proposednewtime"] = $recipientRow[PR_PROPOSEDNEWTIME];
						}else{
							$recipient["proposednewtime"] = 0;
						}

						$recipientTrackingStatus = (isset($recipientRow[PR_RECIPIENT_TRACKSTATUS]))?$recipientRow[PR_RECIPIENT_TRACKSTATUS]:0;
						//Switch case for setting the track status of each recipient
						switch($recipientTrackingStatus)
						{
							case 0:
								$recipient["recipient_status"] = _("No Response");
								break;
							case 1:
								$recipient["recipient_status"] = _("Organizor");
								break;
							case 2:
								$recipient["recipient_status"] = _("Tentative");
								break;
							case 3:
								$recipient["recipient_status"] = _("Accepted");
								break;
							case 4:
								$recipient["recipient_status"] = _("Declined");
								break;
							case 5:
								$recipient["recipient_status"] = _("Not Responded");
								break;
						}
						$recipient["recipient_status_num"] = $recipientTrackingStatus;
						array_push($props["recipients"]["recipient"], $recipient);
					}
				}

				// Get attachments
				$hasattachProp = mapi_getprops($message, array(PR_HASATTACH));
				if (isset($hasattachProp[PR_HASATTACH])&& $hasattachProp[PR_HASATTACH]){
					$attachmentTable = mapi_message_getattachmenttable($message);

					$attachments = mapi_table_queryallrows($attachmentTable, array(PR_ATTACH_NUM, PR_ATTACH_SIZE, PR_ATTACH_LONG_FILENAME, PR_ATTACH_FILENAME, PR_ATTACHMENT_HIDDEN, PR_DISPLAY_NAME, PR_ATTACH_METHOD, PR_ATTACH_CONTENT_ID, PR_ATTACH_MIME_TAG, PR_ATTACHMENT_CONTACTPHOTO, PR_EC_WA_ATTACHMENT_HIDDEN_OVERRIDE));
					$props["attachments"] = array();
					$props["attachments"]["attachment"] = array();
					$hasOnlyHiddenAttach = true;

					foreach($attachments as $attachmentRow)
					{
							$attachment = array();
							$attachment["attach_num"] = $attachmentRow[PR_ATTACH_NUM];
							$attachment["attach_method"] = $attachmentRow[PR_ATTACH_METHOD];
							$attachment["size"] = $attachmentRow[PR_ATTACH_SIZE];
							if(isset($attachmentRow[PR_ATTACH_CONTENT_ID]) && $attachmentRow[PR_ATTACH_CONTENT_ID]) {
								$attachment["cid"] = $attachmentRow[PR_ATTACH_CONTENT_ID];
							}
							if(isset($attachmentRow[PR_ATTACH_MIME_TAG]) && $attachmentRow[PR_ATTACH_MIME_TAG]) {
								$attachment["filetype"] = $attachmentRow[PR_ATTACH_MIME_TAG];
							}
							$attachment["hidden"] = isset($attachmentRow[PR_ATTACHMENT_HIDDEN]) ? $attachmentRow[PR_ATTACHMENT_HIDDEN]:false;

							// Use PR_EC_WA_ATTACHMENT_HIDDEN_OVERRIDE if available
							if(isset($attachmentRow[PR_EC_WA_ATTACHMENT_HIDDEN_OVERRIDE]))
								$attachment["hidden"] = $attachmentRow[PR_EC_WA_ATTACHMENT_HIDDEN_OVERRIDE];

							if(!$attachment["hidden"]){
								$hasOnlyHiddenAttach = false;
							}

							if(isset($attachmentRow[PR_ATTACH_LONG_FILENAME]))
								$attachment["name"] = $attachmentRow[PR_ATTACH_LONG_FILENAME];
							else if(isset($attachmentRow[PR_ATTACH_FILENAME]))
								$attachment["name"] = $attachmentRow[PR_ATTACH_FILENAME];
							else if(isset($attachmentRow[PR_DISPLAY_NAME]))
								$attachment["name"] = $attachmentRow[PR_DISPLAY_NAME];
							else
								$attachment["name"] = "untitled";

							if (isset($attachment["name"])){
								$attachment["name"] = windows1252_to_utf8($attachment["name"]);
							}

							if(isset($attachmentRow[PR_ATTACHMENT_CONTACTPHOTO]) && $attachmentRow[PR_ATTACHMENT_CONTACTPHOTO]) {
								$attachment["attachment_contactphoto"] = $attachmentRow[PR_ATTACHMENT_CONTACTPHOTO];
								$attachment["hidden"] = true;

								//Open contact photo attachement in binary format.
								$attach = mapi_message_openattach($message, $attachment["attach_num"]);
								$photo = mapi_attach_openbin($attach,PR_ATTACH_DATA_BIN);

								// Process photo and restrict its size to 96.
								if ($photo) {
									$compressionRatio=1;
									for ($length=2;$length<=strlen($photo);) {
										$partinfo = unpack("Cmarker/Ccode/nlength",substr($photo,$length,4));
										if ($partinfo['marker'] != 0xff) break; // error in structure???
										if ($partinfo['code'] >= 0xc0 &&
											$partinfo['code'] <= 0xc3) { // this is the size block
											$photo_size = unpack("Cunknown/ny/nx",substr($photo,$length+4,5));
											// find the resize factor, picture should be not higher than 96 pixel.
											$compressionRatio = ceil($photo_size['y']/96);
											break;
										} else { // jump to next block
											$length = $length+$partinfo['length']+2;
										}
									}
									if ($partinfo['marker'] == 0xff) {
										$attachment["attachment_contactphoto_sizex"] = (int)($photo_size['x'] / $compressionRatio);
										$attachment["attachment_contactphoto_sizey"] = (int)($photo_size['y'] / $compressionRatio);
									}
								}
							}

							if ($attachment["attach_method"] == ATTACH_EMBEDDED_MSG){
								// open attachment to get the message class
								$attach = mapi_message_openattach($message, $attachment["attach_num"]);
								$embMessage = mapi_attach_openobj($attach);
								$embProps = mapi_getprops($embMessage, array(PR_MESSAGE_CLASS));
								if (isset($embProps[PR_MESSAGE_CLASS]))
									$attachment["attach_message_class"] = $embProps[PR_MESSAGE_CLASS];
							}

							array_push($props["attachments"]["attachment"], $attachment);
					}

					// If only hidden attachments, then we dont want to show attach icon in list view.
					if ($hasOnlyHiddenAttach)
						$props["hasattach"] = '0';
				}
			}
			return $props;
		}

		/**
		 * Get and convert properties of a message into an XML array structure
		 *
		 * @param object $store MAPI Message Store Object
		 * @param object $item The MAPI Object
		 * @param array $properties Mapping of properties that should be read
		 * @return array XML array structure
		 * @todo Function name is misleading, especially compared to getMessageProps()
		 */
		function getProps($store, $item, $properties)
		{
			$props = array();

			if($item) {
				$itemprops = mapi_getprops($item, $properties);
				$props = Conversion::mapMAPI2XML($properties, $itemprops);
			}

			return $props;
		}

		/**
		 * Get embedded message data
		 *
		 * Returns the same data as getMessageProps, but then for a specific sub/sub/sub message
		 * of a MAPI message.
		 *
		 * @param object $store MAPI Message Store Object
		 * @param object $message MAPI Message Object
		 * @param array $properties a set of properties which will be selected
		 * @param array $entryid Entryid of the root message that is used for referencing to attachments for downloading
		 * @param array $attach_num a list of attachment numbers (aka 2,1 means 'attachment nr 1 of attachment nr 2')
		 * @return array item XML array structure of the embedded message
		 */
		function getEmbeddedMessageProps($store, $message, $properties, $entryid, $attach_num)
		{
			$msgprops = mapi_getprops($message, Array(PR_MESSAGE_CLASS));
			switch($msgprops[PR_MESSAGE_CLASS]){
				case 'IPM.Note':
					$html2text = false;
					break;
				default:
					$html2text = true;
			}
			$props = $this->getMessageProps($store, $message, $properties, $html2text, $entryid, $attach_num);

			return $props;
		}

		/**
		 * Create a MAPI message
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid The entryid of the folder in which the new message is to be created
		 * @return mapimessage Created MAPI message resource
		 */
		function createMessage($store, $parententryid)
		{
			$folder = mapi_msgstore_openentry($store, $parententryid);
			return mapi_folder_createmessage($folder);
		}

		/**
		 * Open a MAPI message
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the message
		 * @param array $attachNum a list of attachment numbers (aka 2,1 means 'attachment nr 1 of attachment nr 2')
		 * @return object MAPI Message
		 */
		function openMessage($store, $entryid, $attachNum = false)
		{

			$message = mapi_msgstore_openentry($store, $entryid);

			// Needed for S/MIME messages with embedded message attachments
			parse_smime($store, $message);

			if($message && $attachNum) {
				foreach($attachNum as $num){
					$attachment = mapi_message_openattach($message, $num);

					if($attachment) {
  						$message = mapi_attach_openobj($attachment);
  					}else{
						return false;
					}
  				}
			}
			return $message;
		}

		/**
		 * Save a MAPI message
		 *
		 * The to-be-saved message can be of any type, including e-mail items, appointments, contacts, etc. The message may be pre-existing
		 * or it may be a new message.
		 *
		 * The dialog_attachments parameter represents a unique ID which for the dialog in the client for which this function was called; This
		 * is used as follows; Whenever a user uploads an attachment, the attachment is stored in a temporary place on the server. At the same time,
		 * the temporary server location of the attachment is saved in the session information, accompanied by the $dialog_attachments unique ID. This
		 * way, when we save the message into MAPI, we know which attachment was previously uploaded ready for this message, because when the user saves
		 * the message, we pass the same $dialog_attachments ID as when we uploaded the file.
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid Parent entryid of the message
		 * @param array $props The MAPI properties to be saved
		 * @param array $recipients XML array structure of recipients for the recipient table
		 * @param string $dialog_attachments Unique check number which checks if attachments should be added ($_SESSION)
		 * @param array $messageProps reference to an array which will be filled with PR_ENTRYID and PR_STORE_ENTRYID of the saved message
		 * @param mixed $copyAttachments If set we copy all attachments from this entryid to this item
		 * @param mixed $copyAttachmentsStore If set we use this store to copy the attachments from
		 * @param array $propertiesToDelete Properties specified in this array are deleted from the MAPI message
		 * @param array $add_inline_attachments if set this holds all attachments that should be saved as inline attachments
		 * @param array $delete_inline_attachments if set this holds all attachments that should be removed which were saved inline attachments.
		 * @param boolean $copy_inline_attachments_only if true then copy only inline attachments.
		 * @return mapimessage Saved MAPI message resource
		 */
		function saveMessage($store, $parententryid, $props, $recipients, $dialog_attachments, &$messageProps, $copyAttachments = false, $copyAttachmentsStore = false, $propertiesToDelete = array(), $add_inline_attachments = array(), $delete_inline_attachments = array(), $copy_inline_attachments_only = false, $save_changes = true)
		{
			$message = false;

			// Check if an entryid is set, otherwise create a new message
			if(isset($props[PR_ENTRYID]) && !empty($props[PR_ENTRYID])) {
				$message = $this->openMessage($store, $props[PR_ENTRYID]);
			} else {
				$message = $this->createMessage($store, $parententryid);
			}

			if($message) {
				$property = false;
				$body = "";

				// Check if the body is set.
				if (isset($props[PR_BODY])) {
					$body = $props[PR_BODY];
					$property = PR_BODY;
					$bodyPropertiesToDelete = array(PR_HTML, PR_RTF_COMPRESSED);

					if(isset($props[PR_HTML])) {
						$body = $this->fckEditor2html($props[PR_BODY],$props[PR_SUBJECT]);
						$property = PR_HTML;
						$bodyPropertiesToDelete = array(PR_BODY, PR_RTF_COMPRESSED);
						unset($props[PR_HTML]);
					}
					unset($props[PR_BODY]);

					$propertiesToDelete = array_unique(array_merge($propertiesToDelete, $bodyPropertiesToDelete));
				}

				if(isset($props[PR_SENT_REPRESENTING_EMAIL_ADDRESS]) && strlen($props[PR_SENT_REPRESENTING_EMAIL_ADDRESS]) > 0 && !isset($props[PR_SENT_REPRESENTING_ENTRYID])){
					// Set FROM field properties
					$props[PR_SENT_REPRESENTING_ENTRYID] = mapi_createoneoff($props[PR_SENT_REPRESENTING_NAME], $props[PR_SENT_REPRESENTING_ADDRTYPE], $props[PR_SENT_REPRESENTING_EMAIL_ADDRESS]);
				}

				// remove mv properties when needed
				foreach($props as $propTag=>$propVal){
					if((mapi_prop_type($propTag) & MV_FLAG) == MV_FLAG) {
						if(is_array($propVal) && count($propVal) == 0) {
							// if empty array then also remove that property
							$propertiesToDelete[] = $propTag;
						} else if(is_null($propVal)) {
							$propertiesToDelete[] = $propTag;
						}
					}

					// Empty PT_SYSTIME values mean they should be deleted (there is no way to set an empty PT_SYSTIME)
					if(mapi_prop_type($propTag) == PT_SYSTIME && strlen($propVal) == 0)
					    $propertiesToDelete[] = $propTag;
				}

				foreach($propertiesToDelete as $prop){
					unset($props[$prop]);
				}

				// Set the properties
				mapi_setprops($message, $props);

				// Delete the properties we don't need anymore
				mapi_deleteprops($message, $propertiesToDelete);

				if ($property != false){
					// Stream the body to the PR_BODY or PR_HTML property
					$stream = mapi_openpropertytostream($message, $property, MAPI_CREATE | MAPI_MODIFY);
					mapi_stream_setsize($stream, strlen($body));
					mapi_stream_write($stream, $body);
					mapi_stream_commit($stream);
				}

				// Check if $recipients is set
				if(is_array($recipients) && count($recipients) > 0){
					// Save the recipients
					$this->setRecipients($message, $recipients);
				}

				// Save the attachments with the $dialog_attachments
				$this->setAttachments($copyAttachmentsStore, $message, $dialog_attachments, $copyAttachments, $add_inline_attachments, $delete_inline_attachments, $copy_inline_attachments_only);

				// Set 'hideattacments' if message has only inline attachments.
				$properties = $GLOBALS["properties"]->getMailProperties();
				if($this->hasOnlyInlineAttachments($message)){
					mapi_setprops($message, array($properties["hideattachments"] => true));
				} else {
					mapi_deleteprops($message, array($properties["hideattachments"]));
				}

				// Save changes
				if ($save_changes) mapi_savechanges($message);

				// Get the PR_ENTRYID, PR_PARENT_ENTRYID and PR_STORE_ENTRYID properties of this message
				$messageProps = mapi_getprops($message, array(PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID));
			}

			return $message;
		}

		/**
		 * Save an appointment item.
		 *
		 * This is basically the same as saving any other type of message with the added complexity that
		 * we support saving exceptions to recurrence here. This means that if the client sends a basedate
		 * in the action, that we will attempt to open an existing exception and change that, and if that
		 * fails, create a new exception with the specified data.
		 *
		 * @param mapistore $store MAPI store of the message
		 * @param string $parententryid Parent entryid of the message (folder entryid, NOT message entryid)
		 * @param array $action Action array containing XML request
		 * @return array of PR_ENTRYID, PR_PARENT_ENTRYID and PR_STORE_ENTRYID properties of modified item
		 */
		function saveAppointment($store, $parententryid, $action)
		{
			$messageProps = false;
			// It stores the values that is exception allowed or not false -> not allowed
			$isExceptionAllowed = true;
			$delete = false;	// Flag for MeetingRequest Class whether to send update or cancel mail.
			$basedate = false;	// Flag for MeetingRequest Class whether to send an exception or not.
			$isReminderTimeAllowed = true;	// Flag to check reminder minutes is in range of the occurences
			$properties = $GLOBALS["properties"]->getAppointmentProperties();
			$oldProps = array();

			if($store && $parententryid) {
				if(isset($action["props"])) {
					$messageProps = array();
					$recips = array();
					if(isset($action["recipients"]["recipient"])) {
						$recips = $action["recipients"]["recipient"];
					} else {
						$recips = false;
					}

					if(isset($action["props"]["entryid"]) && !empty($action["props"]["entryid"]) ) {
						// Modify existing or add/change exception
						$message = mapi_msgstore_openentry($store, hex2bin($action["props"]["entryid"]));

						if($message) {
							// Check if appointment is an exception to a recurring item
							if(isset($action["props"]["basedate"]) && $action["props"]["basedate"] > 0) {
								$exception = true;

								// Create recurrence object
								$recurrence = new Recurrence($store, $message);

								$basedate = (int) $action["props"]["basedate"];

								if(isset($action["delete"]) && $action["delete"]) {
									$delete = true;
									$isExceptionAllowed = $recurrence->createException(array(), $basedate, true);
								} else {
									$exception_recips = $recips ? $this->createRecipientList($recips, true) : array();
									if(isset($action["props"]["reminder_minutes"]) && $action["props"]["reminder_minutes"]){
										$isReminderTimeAllowed = $recurrence->isValidReminderTime($basedate, $action["props"]["reminder_minutes"], $action["props"]["startdate"]);
									}

									// As the reminder minutes occurs before other occurences don't modify the item.
									if($isReminderTimeAllowed){
										if($recurrence->isException($basedate)){
											$oldProps = $recurrence->getExceptionProperties($recurrence->getChangeException($basedate));
											$isExceptionAllowed = $recurrence->modifyException(Conversion::mapXML2MAPI($properties, $action["props"]), $basedate, $exception_recips);
										} else {
											$oldProps[$properties['startdate']] = $recurrence->getOccurrenceStart($basedate);
											$oldProps[$properties['duedate']] = $recurrence->getOccurrenceEnd($basedate);
											$isExceptionAllowed = $recurrence->createException(Conversion::mapXML2MAPI($properties, $action["props"]), $basedate, false, $exception_recips);
										}
										mapi_savechanges($message);
									}
								}
							} else {
								$oldProps = mapi_getprops($message, array($properties['startdate'], $properties['duedate']));
								$recipTable = mapi_message_getrecipienttable($message);
								$oldRecipients = mapi_table_queryallrows($recipTable, array(PR_ENTRYID, PR_DISPLAY_NAME, PR_EMAIL_ADDRESS, PR_RECIPIENT_ENTRYID, PR_RECIPIENT_TYPE, PR_SEND_INTERNET_ENCODING, PR_SEND_RICH_INFO, PR_RECIPIENT_DISPLAY_NAME, PR_ADDRTYPE, PR_DISPLAY_TYPE, PR_RECIPIENT_TRACKSTATUS, PR_RECIPIENT_FLAGS, PR_ROWID, PR_SEARCH_KEY));

								// Modifying non-exception (the series) or normal appointment item
								$message = $GLOBALS["operations"]->saveMessage($store, $parententryid, Conversion::mapXML2MAPI($properties, $action["props"]), $recips, $action["dialog_attachments"], $messageProps, false, false, array(), array(), array(), false, false);

								// Only save recurrence if it has been changed by the user (because otherwise we'll reset
								// the exceptions)
								if(isset($action['props']['recurring_reset']) && $action['props']['recurring_reset'] == 1) {
									$recur = new Recurrence($store, $message);

									if(isset($action['props']['timezone'])) {
										$tzprops = Array('timezone','timezonedst','dststartmonth','dststartweek','dststartday','dststarthour','dstendmonth','dstendweek','dstendday','dstendhour');

										// Get timezone info
										$tz = Array();
										foreach($tzprops as $tzprop) {
											$tz[$tzprop] = $action['props'][$tzprop];
										}
									}

									// Act like the 'props' are the recurrence pattern; it has more information but that
									// is ignored
									$recur->setRecurrence($tz, $action['props']);
								}
							}
							// Get the properties of the main object of which the exception was changed, and post
							// that message as being modified. This will cause the update function to update all
							// occurrences of the item to the client
							$messageProps = mapi_getprops($message, array(PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID));
						}
					} else {
						//Set sender of new Appointment.
						$this->setSenderAddress($store, $action);

						$message = $GLOBALS["operations"]->saveMessage($store, $parententryid, Conversion::mapXML2MAPI($properties, $action["props"]), $recips, $action["dialog_attachments"], $messageProps,  false, false, array(), array(), array(), false, false);

						if(isset($action['props']['timezone'])) {
							$tzprops = Array('timezone','timezonedst','dststartmonth','dststartweek','dststartday','dststarthour','dstendmonth','dstendweek','dstendday','dstendhour');

							// Get timezone info
							$tz = Array();
							foreach($tzprops as $tzprop) {
								$tz[$tzprop] = $action['props'][$tzprop];
							}
						}

						// Set recurrence
						if(isset($action['props']['recurring']) && $action['props']['recurring'] == 1) {
							$recur = new Recurrence($store, $message);
							$recur->setRecurrence($tz, $action['props']);
						}
					}
				}
			}

			$result = false;
			// Check to see if it should be sent as a meeting request
			if(isset($action["send"]) && $action["send"] && $isExceptionAllowed) {
				$request = new Meetingrequest($store, $message, $GLOBALS["mapisession"]->getSession(), ENABLE_DIRECT_BOOKING);
				$request->updateMeetingRequest($basedate);

				$isRecurrenceChanged = isset($action['props']['recurring_reset']) && $action['props']['recurring_reset'] == 1;
				$request->checkSignificantChanges($oldProps, $basedate, $isRecurrenceChanged);

				// Update extra body information
				if(isset($action['props']['meetingTimeInfo']) && strlen($action['props']['meetingTimeInfo']))
					$request->setMeetingTimeInfo($action['props']['meetingTimeInfo']);

				$deletedRecipients = false;
				if (isset($oldRecipients) && count($oldRecipients) > 0) {
					$deletedRecipients = $this->getDeletedRecipients($message, $oldRecipients);
				}

				$sendMeetingRequestResult = $request->sendMeetingRequest($delete, false, $basedate, $deletedRecipients);

				if($recips) $this->addEmailsToRecipientHistory($recips);

				if($sendMeetingRequestResult === true){
					mapi_savechanges($message);
					// Return message properties that can be sent to the bus to notify changes
					$result = $messageProps;
				}else{
					$sendMeetingRequestResult[PR_ENTRYID] = $messageProps[PR_ENTRYID];
					$sendMeetingRequestResult[PR_PARENT_ENTRYID] = $messageProps[PR_PARENT_ENTRYID];
					$sendMeetingRequestResult[PR_STORE_ENTRYID] = $messageProps[PR_STORE_ENTRYID];
					$result = $sendMeetingRequestResult;
				}
			}else{
				mapi_savechanges($message);

				if(isset($isExceptionAllowed)){
					if($isExceptionAllowed === false) {
						$messageProps["isexceptionallowed"] = 'false';
					}
				}

				if(isset($isReminderTimeAllowed)){
					if($isReminderTimeAllowed === false) {
						$messageProps["remindertimeerror"] = false;
					}
				}
				// Return message properties that can be sent to the bus to notify changes
				$result = $messageProps;
			}

			if ($store && $parententryid) {
				// Publish updated free/busy information
				$GLOBALS["operations"]->publishFreeBusy($store, $parententryid);
			}

			return $result;
		}

		/**
		 * Return recipients which are removed from given original recipient list
		 * @param MAPI_Message $message
		 * @param Array $oldRecipients old recipient list
		 */
		function getDeletedRecipients($message, $oldRecipients)
		{
			$recips = array();
			$recipTable = mapi_message_getrecipienttable($message);
			$recipientRows = mapi_table_queryallrows($recipTable, array(PR_ENTRYID, PR_DISPLAY_NAME, PR_EMAIL_ADDRESS, PR_RECIPIENT_ENTRYID, PR_RECIPIENT_TYPE, PR_SEND_INTERNET_ENCODING, PR_SEND_RICH_INFO, PR_RECIPIENT_DISPLAY_NAME, PR_ADDRTYPE, PR_DISPLAY_TYPE, PR_RECIPIENT_TRACKSTATUS, PR_RECIPIENT_FLAGS, PR_ROWID, PR_SEARCH_KEY));

			foreach($oldRecipients as $oldRecip) {
				$found = false;

				foreach($recipientRows as $recipient) {
					if ($oldRecip[PR_SEARCH_KEY] == $recipient[PR_SEARCH_KEY])
						$found = true;
				}

				if (!$found && ($oldRecip[PR_RECIPIENT_FLAGS] & recipOrganizer) != recipOrganizer)
					$recips[] = $oldRecip;
			}

			return $recips;
		}

		/**
		 * Set sent_representing_email_address property of Appointment.
		 *
		 * Before saving any new appointment, sent_representing_email_address property of appointment
		 * should contain email_address of user, who is the owner of store(in which the appointment
		 * is created).
		 *
		 * @param mapistore $store  MAPI store of the message
		 * @param array     $action reference to action array containing XML request
		 */
		function setSenderAddress($store, &$action)
		{
			$storeProps = mapi_getprops($store, array(PR_MAILBOX_OWNER_ENTRYID));
			// check for public store
			if(!isset($storeProps[PR_MAILBOX_OWNER_ENTRYID])) {
				$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
				$storeProps = mapi_getprops($store, array(PR_MAILBOX_OWNER_ENTRYID));
			}
			$mailuser = mapi_ab_openentry($GLOBALS["mapisession"]->getAddressbook(), $storeProps[PR_MAILBOX_OWNER_ENTRYID]);
			if($mailuser){
				$userprops = mapi_getprops($mailuser, array(PR_ADDRTYPE, PR_DISPLAY_NAME, PR_EMAIL_ADDRESS, PR_SMTP_ADDRESS));
				$action["props"]["sent_representing_entryid"]       = bin2hex($storeProps[PR_MAILBOX_OWNER_ENTRYID]);
				// we do conversion here, because before passing props to saveMessage() props are converted from utf8-to-w
				$action["props"]["sent_representing_name"]          = w2u($userprops[PR_DISPLAY_NAME]);
				$action["props"]["sent_representing_addrtype"]      = $userprops[PR_ADDRTYPE];
				if($userprops[PR_ADDRTYPE] == 'SMTP'){
					$emailAddress = $userprops[PR_EMAIL_ADDRESS];
				}else{
					$emailAddress = $userprops[PR_SMTP_ADDRESS];
				}
				$action["props"]["sent_representing_email_address"] = $emailAddress;
				$action["props"]["sent_representing_search_key"]    = strtoupper($userprops[PR_ADDRTYPE]) . ":" .strtoupper($emailAddress);
			}
		}

		/**
		 * Submit a message for sending
		 *
		 * This function is an extension of the saveMessage() function, with the extra functionality
		 * that the item is actually sent and queued for moving to 'Sent Items'. Also, the e-mail addresses
		 * used in the message are processed for later auto-suggestion.
		 *
		 * @see Operations::saveMessage() for more information on the parameters, which are identical.
		 *
		 * @param mapistore $store MAPI Message Store Object
		 * @param array $props The properties to be saved
		 * @param array $recipients XML array structure of recipients for the recipient table
		 * @param string $dialog_attachments Unique ID of attachments for this message
		 * @param array $messageProps reference to an array which will be filled with PR_ENTRYID, PR_PARENT_ENTRYID and PR_STORE_ENTRYID
		 * @param array $copyAttachments if set this holds the entryid of the message which holds the attachments
		 * @param array $copyAttachmentsStore if set we this this store to copy the attachments from
		 * @param array $add_inline_attachments if set this holds all attachments that should be saved as inline attachments
		 * @param array $delete_inline_attachments if set this holds all attachments that should be removed which were saved inline attachments.
		 * @param boolean $copy_inline_attachments_only if true then copy only inline attachments.
		 * @param boolean $send_as_onbehalf only when sending message from other store but dont want to set onbehalf of.
		 * @return boolean true if action succeeded, false if not
		 *
		 */
		function submitMessage($store, $props, $recipients, $dialog_attachments, &$messageProps, $copyAttachments = false, $copyAttachmentsStore = false, $add_inline_attachments = array(), $delete_inline_attachments = array(), $copy_inline_attachments_only = false, $replyOrigMessageDetails = false, $send_as_onbehalf = true)
		{
			$result = false;
			$message = false;
			$origStore = $store;

			// Get the outbox and sent mail entryid, ignore the given $store, use the default store for submitting messages
			$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
			$storeprops = mapi_getprops($store, array(PR_IPM_OUTBOX_ENTRYID, PR_IPM_SENTMAIL_ENTRYID, PR_ENTRYID));
			$origStoreprops = mapi_getprops($origStore, array(PR_ENTRYID));

			if(isset($storeprops[PR_IPM_OUTBOX_ENTRYID])) {
				if(isset($storeprops[PR_IPM_SENTMAIL_ENTRYID])) {
					$props[PR_SENTMAIL_ENTRYID] = $storeprops[PR_IPM_SENTMAIL_ENTRYID];
				}

				// Set reply to settings
				$replyTo = $GLOBALS['settings']->get("createmail/reply_to",null);
				if (!empty($replyTo)){
					// if format is like name <email address> then split the different parts
					if(strpos($replyTo, "<") !== false) {
						$displayName = trim(substr($replyTo, 0, strpos($replyTo, "<")));
						$emailAddress = trim(substr($replyTo, strpos($replyTo, "<") + 1, -1));
					} else {
						$displayName = $replyTo;
						$emailAddress = $replyTo;
					}

					$props[PR_REPLY_RECIPIENT_NAMES] = $displayName;
					$replyRecip = array(array(PR_EMAIL_ADDRESS=>$emailAddress, PR_ADDRTYPE=>"SMTP", PR_DISPLAY_NAME=>$displayName));
					$props[PR_REPLY_RECIPIENT_ENTRIES] = $this->writeFlatEntryList($replyRecip);
				}
				/**
				 * Check if replying then set PR_INTERNET_REFERENCES and PR_IN_REPLY_TO_ID properties in props.
				 */
				if(isset($replyOrigMessageDetails) && $replyOrigMessageDetails){
					$originalMessageStore = $GLOBALS["mapisession"]->openMessageStore(hex2bin($replyOrigMessageDetails["storeid"]));
					$origmessage = mapi_msgstore_openentry($originalMessageStore, hex2bin($replyOrigMessageDetails["entryid"]));
					$origmsgprops = mapi_getprops($origmessage);
					if(isset($origmsgprops[PR_INTERNET_MESSAGE_ID])){
						$props[PR_INTERNET_REFERENCES] = $origmsgprops[PR_INTERNET_MESSAGE_ID];
						$props[PR_IN_REPLY_TO_ID] = $origmsgprops[PR_INTERNET_MESSAGE_ID];
					}
				}

				if($origStoreprops[PR_ENTRYID] !== $storeprops[PR_ENTRYID]) {
					// set properties for "on behalf of" mails
					$origStoreProps = mapi_getprops($origStore, array(PR_MAILBOX_OWNER_ENTRYID, PR_MDB_PROVIDER));

					// set PR_SENDER_* properties, which contains currently logged users data
					$ab = $GLOBALS['mapisession']->getAddressbook();
					$abitem = mapi_ab_openentry($ab, $GLOBALS["mapisession"]->getUserEntryID());
					$abitemprops = mapi_getprops($abitem, array(PR_DISPLAY_NAME, PR_EMAIL_ADDRESS));

					$props[PR_SENDER_ENTRYID] = $GLOBALS["mapisession"]->getUserEntryID();
					$props[PR_SENDER_NAME] = $abitemprops[PR_DISPLAY_NAME];
					$props[PR_SENDER_EMAIL_ADDRESS] = $abitemprops[PR_EMAIL_ADDRESS];
					$props[PR_SENDER_ADDRTYPE] = "ZARAFA";

					/**
					 * if delegate store then set PR_SENT_REPRESENTING_* properties
					 * based on delegate store's owner data
					 * if public store then set PR_SENT_REPRESENTING_* properties based on
					 * default store's owner data
					 */
					if($origStoreProps[PR_MDB_PROVIDER] == ZARAFA_STORE_DELEGATE_GUID && $send_as_onbehalf) {
						$abitem = mapi_ab_openentry($ab, $origStoreProps[PR_MAILBOX_OWNER_ENTRYID]);
						$abitemprops = mapi_getprops($abitem, array(PR_DISPLAY_NAME, PR_EMAIL_ADDRESS));

						$props[PR_SENT_REPRESENTING_ENTRYID] = $origStoreProps[PR_MAILBOX_OWNER_ENTRYID];
						$props[PR_SENT_REPRESENTING_NAME] = $abitemprops[PR_DISPLAY_NAME];
						$props[PR_SENT_REPRESENTING_EMAIL_ADDRESS] = $abitemprops[PR_EMAIL_ADDRESS];
						$props[PR_SENT_REPRESENTING_ADDRTYPE] = "ZARAFA";
					} else if($origStoreProps[PR_MDB_PROVIDER] == ZARAFA_STORE_PUBLIC_GUID) {
						$props[PR_SENT_REPRESENTING_ENTRYID] = $props[PR_SENDER_ENTRYID];
						$props[PR_SENT_REPRESENTING_NAME] = $props[PR_SENDER_NAME];
						$props[PR_SENT_REPRESENTING_EMAIL_ADDRESS] = $props[PR_SENDER_EMAIL_ADDRESS];
						$props[PR_SENT_REPRESENTING_ADDRTYPE] = $props[PR_SENDER_ADDRTYPE];
					}

					/**
					 * we are sending mail from delegate's account, so we can't use delegate's outbox and sent itmes folder
					 * so we have to copy the mail from delegate's store to logged user's store and in outbox folder and then
					 * we can send mail from logged user's outbox folder
					 *
					 * if we remove PR_ENTRYID property in props before passing it to saveMessage function then it will assume
					 * that item doesn't exist and it will create a new item (in outbox of logged in user)
					 */
					if(isset($props[PR_ENTRYID])) {
						$oldEntryId = $props[PR_ENTRYID];
						unset($props[PR_ENTRYID]);
					}

					// if we are sending mail from drafts folder then we have to copy its attachments also
					if($copyAttachments === false && $copyAttachmentsStore === false) {
						$copyAttachments = $oldEntryId;
						$copyAttachmentsStore = $origStore;
					}

					// Save the new message properties
					$message = $this->saveMessage($store, $storeprops[PR_IPM_OUTBOX_ENTRYID], $props, $recipients, $dialog_attachments, $messageProps, $copyAttachments, $copyAttachmentsStore, array(), $add_inline_attachments, $delete_inline_attachments, $copy_inline_attachments_only);

					// FIXME: currently message is deleted from original store and new message is created
					// in current user's store, but message should be moved

					// delete message from its original location
					$folder = mapi_msgstore_openentry($origStore, $props[PR_PARENT_ENTRYID]);
					mapi_folder_deletemessages($folder, array($oldEntryId), DELETE_HARD_DELETE);
				}else{
					// When the message is in your own store, just move it to your outbox. We move it manually so we know the new entryid after it has been moved.
					$outbox = mapi_msgstore_openentry($store, $storeprops[PR_IPM_OUTBOX_ENTRYID]);

					// Open the old and the new message
					$oldentryid = $props[PR_ENTRYID];
					$newmessage = mapi_folder_createmessage($outbox);
					$message = mapi_msgstore_openentry($store, $oldentryid);

					// Copy the entire message
					mapi_copyto($message, array(), array(), $newmessage);
					mapi_savechanges($newmessage);

					// Remember the new entryid
					$newprops = mapi_getprops($newmessage, array(PR_ENTRYID));
					$props[PR_ENTRYID] = $newprops[PR_ENTRYID];

					// Delete the old message
					mapi_folder_deletemessages($outbox, array($oldentryid));

					// Save the new message properties
					$message = $this->saveMessage($store, $storeprops[PR_IPM_OUTBOX_ENTRYID], $props, $recipients, $dialog_attachments, $messageProps, $copyAttachments, $copyAttachmentsStore, array(), $add_inline_attachments, $delete_inline_attachments, $copy_inline_attachments_only);
				}

				if($message) {
					// Submit the message (send)
					mapi_message_submitmessage($message);
					$tmp_props = mapi_getprops($message, array(PR_PARENT_ENTRYID));
					$messageProps[PR_PARENT_ENTRYID] = $tmp_props[PR_PARENT_ENTRYID];
					$result = true;

					$this->addEmailsToRecipientHistory($recipients);
				}
			}

			return $result;
		}

		/**
		 * Delete messages
		 *
		 * This function does what is needed when a user presses 'delete' on a MAPI message. This means that:
		 *
		 * - Items in the own store are moved to the wastebasket
		 * - Items in the wastebasket are deleted
		 * - Items in other users stores are moved to our own wastebasket
		 * - Items in the public store are deleted
		 *
		 * @param mapistore $store MAPI Message Store Object
		 * @param string $parententryid parent entryid of the messages to be deleted
		 * @param array $entryids a list of entryids which will be deleted
		 * @param boolean $softDelete flag for soft-deleteing (when user presses Shift+Del)
		 * @return boolean true if action succeeded, false if not
		 */
		function deleteMessages($store, $parententryid, $entryids, $softDelete = false)
		{
			$result = false;
			if(!is_array($entryids)) {
				$entryids = array($entryids);
			}

			$folder = mapi_msgstore_openentry($store, $parententryid);

			$msgprops = mapi_getprops($store, array(PR_IPM_WASTEBASKET_ENTRYID, PR_MDB_PROVIDER));

			switch($msgprops[PR_MDB_PROVIDER]){
				case ZARAFA_STORE_DELEGATE_GUID:
				case ZARAFA_STORE_ARCHIVER_GUID:
					// with a store from an other user we need our own waste basket...
					if(isset($msgprops[PR_IPM_WASTEBASKET_ENTRYID]) && $msgprops[PR_IPM_WASTEBASKET_ENTRYID] == $parententryid || $softDelete == true) {
						// except when it is the waste basket itself
						$result = mapi_folder_deletemessages($folder, $entryids);
					}else{
						$defaultstore = $GLOBALS["mapisession"]->getDefaultMessageStore();
						$msgprops = mapi_getprops($defaultstore, array(PR_IPM_WASTEBASKET_ENTRYID, PR_MDB_PROVIDER));

						if(isset($msgprops[PR_IPM_WASTEBASKET_ENTRYID]) && $msgprops[PR_IPM_WASTEBASKET_ENTRYID] != $parententryid) {
							$result = $this->copyMessages($store, $parententryid, $defaultstore, $msgprops[PR_IPM_WASTEBASKET_ENTRYID], $entryids, true);
							if (!$result){ // if moving fails, try normal delete
								$result = mapi_folder_deletemessages($folder, $entryids);
							}
						}else{
							$result = mapi_folder_deletemessages($folder, $entryids);
						}
					}
					break;

				case ZARAFA_STORE_PUBLIC_GUID:
					// always delete in public store
					$result = mapi_folder_deletemessages($folder, $entryids);
					break;

				case ZARAFA_SERVICE_GUID:
					// delete message when in your own waste basket, else move it to the waste basket
					if(isset($msgprops[PR_IPM_WASTEBASKET_ENTRYID]) && $msgprops[PR_IPM_WASTEBASKET_ENTRYID] == $parententryid  || $softDelete == true) {
						$result = mapi_folder_deletemessages($folder, $entryids);
					}else{
						$result = $this->copyMessages($store, $parententryid, $store, $msgprops[PR_IPM_WASTEBASKET_ENTRYID], $entryids, true);
						if (!$result){ // if moving fails, try normal delete
							$result = mapi_folder_deletemessages($folder, $entryids);
						}
					}
					break;
			}

			return $result;
		}

		/**
		 * Copy or move messages
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid parent entryid of the messages
		 * @param string $destentryid destination folder
		 * @param array $entryids a list of entryids which will be copied or moved
		 * @param boolean $moveMessages true - move messages, false - copy messages
		 * @return boolean true if action succeeded, false if not
		 * @todo Duplicate code here which is easily fixed
		 */
		function copyMessages($store, $parententryid, $destStore, $destentryid, $entryids, $moveMessages)
		{
			$result = false;
			$sourcefolder = mapi_msgstore_openentry($store, $parententryid);
			$destfolder = mapi_msgstore_openentry($destStore, $destentryid);

			if(!is_array($entryids)) {
				$entryids = array($entryids);
			}

			if($moveMessages) {
				mapi_folder_copymessages($sourcefolder, $entryids, $destfolder, MESSAGE_MOVE);
				$result = (mapi_last_hresult()==NOERROR);
			} else {
				mapi_folder_copymessages($sourcefolder, $entryids, $destfolder);
				$result = (mapi_last_hresult()==NOERROR);
			}

			return $result;
		}

		/**
		 * Set message read flag
		 *
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the message
		 * @param array $flags Array of options, may contain "unread" and "noreceipt". The absence of these flags indicate the opposite ("read" and "sendreceipt").
		 * @param array $messageProps reference to an array which will be filled with PR_ENTRYID, PR_STORE_ENTRYID and PR_PARENT_ENTRYID of the message
		 * @return boolean true if action succeeded, false if not
		 */
		function setMessageFlag($store, $entryid, $flags, &$messageProps)
		{
			$result = false;
			$message = $this->openMessage($store, $entryid);

			if($message) {
				$flags = explode(",",$flags);
				$flag = 0; // set read and sent receipt if required
				foreach($flags as $value){
					switch($value){
						case "unread":
							$flag |= CLEAR_READ_FLAG;
							break;
						case "noreceipt":
							$flag |= SUPPRESS_RECEIPT;
							break;
					}
				}
				$result = mapi_message_setreadflag($message, $flag);

				$messageProps = mapi_getprops($message, array(PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID, PR_MESSAGE_FLAGS));
			}

			return $result;
		}

		/**
		 * Create a unique folder name based on a provided new folder name
		 *
		 * checkFolderNameConflict() checks if a folder name conflict is caused by the given $foldername.
		 * This function is used for copying of moving a folder to another folder. It returns
		 * a unique foldername.
		 *
		 * @access private
		 * @param object $store MAPI Message Store Object
		 * @param object $folder MAPI Folder Object
		 * @param string $foldername the folder name
		 * @return string correct foldername
		 */
		function checkFolderNameConflict($store, $folder, $foldername)
		{
			$folderNames = array();

			$hierarchyTable = mapi_folder_gethierarchytable($folder);
			mapi_table_sort($hierarchyTable, array(PR_DISPLAY_NAME => TABLE_SORT_ASCEND));

			$subfolders = mapi_table_queryallrows($hierarchyTable, array(PR_ENTRYID));

			if (is_array($subfolders)) {
				foreach($subfolders as $subfolder)
				{
					$folderObject = mapi_msgstore_openentry($store, $subfolder[PR_ENTRYID]);
					$folderProps = mapi_folder_getprops($folderObject, array(PR_DISPLAY_NAME));

					array_push($folderNames, strtolower($folderProps[PR_DISPLAY_NAME]));
				}
			}

			if(array_search(strtolower($foldername), $folderNames) !== false) {
				$i = 1;

				while(array_search((strtolower($foldername) . $i), $folderNames) !== false)
				{
					$i++;
				}

				$foldername .= $i;
			}

			return $foldername;
		}

		/**
		 * Set the recipients of a MAPI message
		 *
		 * @access private
		 * @param object $message MAPI Message Object
		 * @param array $recipients XML array structure of recipients
		 */
		function setRecipients($message, $recipients)
		{
			$recipients = $this->createRecipientList($recipients);

			$recipientTable = mapi_message_getrecipienttable($message);
			$recipientRows = mapi_table_queryallrows($recipientTable, array(PR_ROWID));
			foreach($recipientRows as $recipient)
			{
				mapi_message_modifyrecipients($message, MODRECIP_REMOVE, array($recipient));
			}

			if(count($recipients) > 0){
				mapi_message_modifyrecipients($message, MODRECIP_ADD, $recipients);
			}
		}

		/**
		 * Delete properties in a message
		 *
		 * @todo Why does this function call savechange while most other functions
		 *       here do not? (for example setRecipients)
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid Entryid of the message in which to delete the properties
		 * @param array $props array of property tags which to be deleted
		 */
		function deleteProps($store, $entryid, $props)
		{
			$message = $this->openMessage($store, $entryid);
			mapi_deleteprops($message, $props);
			mapi_savechanges($message);
		}

		/**
		 * Set attachments in a MAPI message
		 *
		 * This function reads any attachments that have been previously uploaded and copies them into
         * the passed MAPI message resource. For a description of the dialog_attachments variable and
         * generally how attachments work when uploading, see Operations::saveMessage()
		 *
		 * @see Operations::saveMessage()
		 * @param object $message MAPI Message Object
		 * @param string $dialog_attachments unique dialog_attachments, which is set in the $_SESSION variable
		 * @param boolean $copyfromEntryid if set, copy the attachments from this entryid in addition to the uploaded attachments
		 * @param array $add_inline_attachments if set this holds all attachments that should be saved as inline attachments
		 * @param array $delete_inline_attachments if set this holds all attachments that should be removed which were saved inline attachments.
		 * @param boolean $copy_inline_attachments_only if true then copy only inline attachments.
		 */
		function setAttachments($store, $message, $dialog_attachments, $copyfromEntryid=false, $add_inline_attachments = array(), $delete_inline_attachments = array(), $copy_inline_attachments_only = false)
		{
			$attachment_state = new AttachmentState();
			$attachment_state->open();

			// check if we need to copy attachments from a forwared message first
			if ($copyfromEntryid !== false){
				$copyfrom = mapi_msgstore_openentry($store, $copyfromEntryid);

				parse_smime($store, $copyfrom);

				$attachmentTable = mapi_message_getattachmenttable($copyfrom);
				if($attachmentTable && isset($dialog_attachments)) {
					$attachments = mapi_table_queryallrows($attachmentTable, array(PR_ATTACH_NUM, PR_ATTACH_SIZE, PR_ATTACH_LONG_FILENAME, PR_ATTACHMENT_HIDDEN, PR_DISPLAY_NAME, PR_ATTACH_METHOD, PR_ATTACH_MIME_TAG));

					$deletedAttachments = $attachment_state->getDeletedAttachments($dialog_attachments);

					foreach($attachments as $props){
						// check if this attachment is "deleted"
						if ($deletedAttachments && in_array($props[PR_ATTACH_NUM], $deletedAttachments)) {
							// skip attachment, remove reference from state as it no longer applies.
							$attachment_state->removeDeletedAttachment($dialog_attachments, $props[PR_ATTACH_NUM]);
							continue;
						}

						// If reply message, then copy only inline attachments.
						if ($copy_inline_attachments_only){
							if (!$props[PR_ATTACHMENT_HIDDEN]){
								// for inline images hidden should be true
								// skip normal attachments
								continue;
							}
						}

						$messageProps = mapi_getprops($copyfrom, array(PR_MESSAGE_CLASS));
						if($messageProps[PR_MESSAGE_CLASS] == "IPM.Note.SMIME.MultipartSigned") {
							if($props[PR_ATTACH_MIME_TAG] == "multipart/signed") {
								// skip signed attachments
								continue;
							}
						}

						$old = mapi_message_openattach($copyfrom, (int) $props[PR_ATTACH_NUM]);
						$new = mapi_message_createattach($message);
						mapi_copyto($old, array(), array(), $new, 0);
						mapi_savechanges($new);
					}
				}
			} else {
				// Check if attachments should be deleted. This is set in the "upload_attachment.php" file
				if (isset($dialog_attachments)) {
					$deleted = $attachment_state->getDeletedAttachments($dialog_attachments);
					if ($deleted) {
						foreach ($deleted as $attach_num) {
							mapi_message_deleteattach($message, (int) $attach_num);
						}
						$attachment_state->clearDeletedAttachments($dialog_attachments);
					}
				}
			}

			// Set contentId to saved attachments.
			foreach ($add_inline_attachments as $attach){
				if ($attach){
					$msgattachment = mapi_message_openattach($message, (int) $attach["attach_Num"]);
					if ($msgattachment){
						$props = array(PR_ATTACH_CONTENT_ID => $attach["cid"], PR_ATTACHMENT_HIDDEN => true);
						mapi_setprops($msgattachment, $props);
						mapi_savechanges($msgattachment);
					}
				}
			}

			// Delete saved inline images if removed from body.
			foreach ($delete_inline_attachments as $attach){
				if ($attach){
					$msgattachment = mapi_message_openattach($message, (int) $attach["attach_Num"]);
					if ($msgattachment){
						mapi_message_deleteattach($message, (int) $attach["attach_Num"]);
						mapi_savechanges($message);
					}
				}
			}

			$files = $attachment_state->getAttachmentFiles($dialog_attachments);
			if ($files) {
				// Loop through the uploaded attachments
				foreach ($files as $tmpname => $fileinfo) {
					$filepath = $attachment_state->getAttachmentPath($tmpname);

					if (is_file($filepath)) {
						// Dont save (inline)attachment if deleted from body.
						$fileDeleted = false;
						foreach ($delete_inline_attachments as $attach){
							if ($tmpname == $attach["attach_Num"]){
								$attachment_state->deleteUploadedAttachmentFile($dialog_attachments, $tmpname);
								$fileDeleted = true;
							}
						}

						$fileDeleted = false;
						if (!$fileDeleted){
							// Set contentId if attachment is inline
							$cid = "";
							foreach ($add_inline_attachments as $attach){
								if ($tmpname == $attach["attach_Num"]){
									$cid = $attach["cid"];
								}
							}

							// Set attachment properties
							$props = Array(PR_ATTACH_LONG_FILENAME => $fileinfo["name"],
										   PR_DISPLAY_NAME => $fileinfo["name"],
										   PR_ATTACH_METHOD => ATTACH_BY_VALUE,
										   PR_ATTACH_DATA_BIN => "",
										   PR_ATTACH_MIME_TAG => $fileinfo["type"],
										   PR_ATTACH_CONTENT_ID => empty($cid)?false:$cid,
										   PR_ATTACHMENT_HIDDEN => $cid?true:false);

							// Create attachment and set props
							$attachment = mapi_message_createattach($message);
							mapi_setprops($attachment, $props);

							// Stream the file to the PR_ATTACH_DATA_BIN property
							$stream = mapi_openpropertytostream($attachment, PR_ATTACH_DATA_BIN, MAPI_CREATE | MAPI_MODIFY);
							$handle = fopen($filepath, "r");
							while (!feof($handle))
							{
								$contents = fread($handle, BLOCK_SIZE);
								mapi_stream_write($stream, $contents);
							}

							// Commit the stream and save changes
							mapi_stream_commit($stream);
							mapi_savechanges($attachment);
							fclose($handle);
							unlink($filepath);
						}
					} else {
						// thats means the attachment is a message item and is uploaded in sesssion file as mapi message Obj
						$props = array();
						$props[PR_ATTACH_METHOD] = ATTACH_EMBEDDED_MSG;
						$props[PR_DISPLAY_NAME] = $fileinfo["name"];

						//Create new attachment.
						$attachment = mapi_message_createattach($message);
						mapi_message_setprops($attachment, $props);

						//open embedded msg
						$imessage = mapi_attach_openobj($attachment, MAPI_CREATE | MAPI_MODIFY);
						$copyfromStore = $GLOBALS["mapisession"]->openMessageStore($fileinfo["storeid"]);
						$copyfrom = mapi_msgstore_openentry($copyfromStore , $fileinfo["entryid"]);

						// Copy the properties from the source message to the attachment
						mapi_copyto($copyfrom, array(), array(), $imessage, 0);

						// copy attachment property to embedded message
						$attachmentTable = mapi_message_getattachmenttable($copyfrom);
						if($attachmentTable) {
							$attachments = mapi_table_queryallrows($attachmentTable, array(PR_ATTACH_NUM, PR_ATTACH_SIZE, PR_ATTACH_LONG_FILENAME, PR_ATTACHMENT_HIDDEN, PR_DISPLAY_NAME, PR_ATTACH_METHOD));

							foreach($attachments as $attach_props){
								if ($attach_props[PR_ATTACH_METHOD] == ATTACH_EMBEDDED_MSG)
									continue;

								$attach_old = mapi_message_openattach($copyfrom, $attach_props[PR_ATTACH_NUM]);
								$attach_newResourceMsg = mapi_message_createattach($imessage);
								mapi_copyto($attach_old, array(), array(), $attach_newResourceMsg, 0);
								mapi_savechanges($attach_newResourceMsg);
							}
						}

						//copy the recipient of embedded message
						$recipienttable = mapi_message_getrecipienttable($copyfrom);
						$recipients = mapi_table_queryallrows($recipienttable, $GLOBALS["properties"]->getRecipientProperties());
						$copy_to_recipientTable = mapi_message_getrecipienttable($imessage);
						$copy_to_recipientRows = mapi_table_queryallrows($copy_to_recipientTable, array(PR_ROWID));
						foreach($copy_to_recipientRows as $recipient) {
							mapi_message_modifyrecipients($imessage, MODRECIP_REMOVE, array($recipient));
						}
						mapi_message_modifyrecipients($imessage, MODRECIP_ADD, $recipients);

						//save changes in the embedded message and the final attachment
						mapi_savechanges($imessage);
						mapi_savechanges($attachment);
					}
				}

				// Delete all the files in the state.
				$attachment_state->clearAttachmentFiles($dialog_attachments);
			}

			$attachment_state->close();
		}

		/**
		 * Create a MAPI recipient list from an XML array structure
		 *
		 * This functions is used for setting the recipient table of a message.
		 * @param array $recipientList a list of recipients as XML array structure
		 * @return array list of recipients with the correct MAPI properties ready for mapi_message_modifyrecipients()
		 */
		function createRecipientList($recipientList, $isException = false)
		{
			$recipients = array();
			$session = $GLOBALS["mapisession"]->getSession();
			$addrbook = mapi_openaddressbook($session);

			foreach($recipientList as $recipientItem)
			{
				if ($isException) {
					// We do not add organizer to exception msg in organizer's calendar.
					if (isset($recipientItem[PR_RECIPIENT_FLAGS]) && $recipientItem[PR_RECIPIENT_FLAGS] == 3)
						continue;

					$recipient[PR_RECIPIENT_FLAGS] = (recipSendable | recipExceptionalResponse | recipReserved);
				}

				// resolve users based on email address with strict matching
				if(!empty($recipientItem["address"]) && !empty($recipientItem["name"])) {
					// Parse groups ( '[Group name]' )
					if(preg_match("/^\[([^]]+)\]$/", $recipientItem["address"], $matches)) {
						$recipientItem["address"] = $matches[1];
						$recipientItem["name"] = $matches[1];
						$recipientItem["objecttype"] = MAPI_DISTLIST;
					} else {
						$recipientItem["objecttype"] = MAPI_MAILUSER;
					}

					$recipient = array();
					$recipient[PR_SMTP_ADDRESS] = u2w($recipientItem["address"]);
					$recipient[PR_OBJECT_TYPE] = $recipientItem["objecttype"];

					// Re-lookup information in GAB if possible so we have up-to-date name for given address
					$user = array( array( PR_DISPLAY_NAME => $recipientItem["address"] ) );
					$user = mapi_ab_resolvename($addrbook, $user, EMS_AB_ADDRESS_LOOKUP);
					if(mapi_last_hresult() == NOERROR) {
						$recipient[PR_DISPLAY_NAME] = $user[0][PR_DISPLAY_NAME];
						$recipient[PR_EMAIL_ADDRESS] = $user[0][PR_EMAIL_ADDRESS];
						$recipient[PR_SEARCH_KEY] = $user[0][PR_SEARCH_KEY];
						$recipient[PR_ADDRTYPE] = $user[0][PR_ADDRTYPE];
						$recipient[PR_ENTRYID] = $user[0][PR_ENTRYID];
					} else {
						$recipient[PR_DISPLAY_NAME] = u2w($recipientItem["name"]);
						$recipient[PR_EMAIL_ADDRESS] = u2w($recipientItem["address"]);
						$recipient[PR_SEARCH_KEY] = u2w($recipientItem["address"]);
						$recipient[PR_ADDRTYPE] = "SMTP";
						$recipient[PR_ENTRYID] = mapi_createoneoff($recipient[PR_DISPLAY_NAME], $recipient[PR_ADDRTYPE], $recipient[PR_EMAIL_ADDRESS]);
					}


					if (isset($recipientItem["recipient_status_num"])){
						$recipient[PR_RECIPIENT_TRACKSTATUS] = $recipientItem["recipient_status_num"];
					}

					switch(strtolower($recipientItem["type"]))
					{
						case "mapi_to":
							$recipient[PR_RECIPIENT_TYPE] = MAPI_TO;
							break;
						case "mapi_cc":
							$recipient[PR_RECIPIENT_TYPE] = MAPI_CC;
							break;
						case "mapi_bcc":
							$recipient[PR_RECIPIENT_TYPE] = MAPI_BCC;
							break;
					}

					if(isset($recipientItem["recipient_flags"])){
						$recipient[PR_RECIPIENT_FLAGS] = $recipientItem["recipient_flags"];
					}else{
						$recipient[PR_RECIPIENT_FLAGS] = 1;
					}

					if(isset($recipientItem["proposednewtime"]) && isset($recipientItem["proposenewstarttime"]) && isset($recipientItem["proposenewendtime"])){
						$recipient[PR_PROPOSEDNEWTIME] = true;
						$recipient[PR_PROPOSENEWTIME_START] = intval($recipientItem["proposenewstarttime"]);
						$recipient[PR_PROPOSENEWTIME_END] = intval($recipientItem["proposenewendtime"]);
					}

					array_push($recipients, $recipient);
				}
			}

			return $recipients;
		}

		/**
		 * Create a reply-to property value
		 *
		 * @param array $recipients list of recipients
		 * @return string the value for the PR_REPLY_RECIPIENT_ENTRIES property
		 * @todo remove it, it is unused
		 */
		function createReplyRecipientEntry($replyTo)
		{
			// TODO: implement. Is used for the setting: Reply To
			// for now, use the old function:
			return $this->createReplyRecipientEntry_old($replyTo);
		}

		/**
		 * Parse reply-to value from PR_REPLY_RECIPIENT_ENTRIES property
		 * @param string $flatEntryList the PR_REPLY_RECIPIENT_ENTRIES value
		 * @return array list of recipients in XML array structure
		 */
		function readReplyRecipientEntry($flatEntryList)
		{
			// Unpack number of entries, the byte count and the entries
			$unpacked = unpack("V1cEntries/V1cbEntries/a*abEntries", $flatEntryList);

			$abEntries = Array();
			$stream = $unpacked['abEntries'];
			$pos = 8;
			for ($i=0; $i<$unpacked['cEntries']; $i++)
			{
				$findEntry = unpack("a".$pos."before/V1cb/a*after", $flatEntryList);
				// Go to after the unsigned int
				$pos += 4;
				$entry = unpack("a".$pos."before/a".$findEntry['cb']."abEntry/a*after", $flatEntryList);
				// Move to after the entry
				$pos += $findEntry['cb'];
				// Move to next 4-byte boundary
				$pos += $pos%4;
				// One one-off entry id
				$abEntries[] = $entry['abEntry'];
			}

			$recipients = Array();
			foreach ($abEntries as $abEntry)
			{
				// Unpack the one-off entry identifier
				$findID = unpack("V1version/a16mapiuid/v1flags/v1abFlags/a*abEntry", $abEntry);

				// charset conversion
				if (($findID['abFlags'] & MAPI_ONE_OFF_UNICODE)) {
					// when unicode then we need to convert data from UCS-2LE to UTF-8 format
					// UCS-2LE is a 2 byte charset so we always need to have string length in modulo of 2
					// if it is not then probably unpack has removed last null character so add it again
					if(strlen($findID["abEntry"]) % 2) {
						// add null character
						$findID["abEntry"] = $findID["abEntry"] . chr(0x00);
					}
					$findID["abEntry"] = iconv("UCS-2LE", "UTF-8", $findID["abEntry"]);
				} else {
					// if not unicode then we need to convert data from windows-1252 to UTF-8 format
					$findID["abEntry"] = iconv("windows-1252", "UTF-8", $findID["abEntry"]);
				}

				// Split the entry in its three fields
				$tempArray = explode("\0", $findID["abEntry"]);

				// Put data in recipient array
				$recipients[] = Array("display_name" => $tempArray[0], "email_address" => $tempArray[2]);
			}

			return $recipients;
		}

		/**
		 * Build full-page HTML from the FCK Editor fragment
		 *
		 * This function basically takes the generated HTML from FCK editor and embeds it in
		 * a standonline HTML page (including header and CSS) to form.
		 *
		 * @param string $fck_html This is the HTML created by the FCK Editor
		 * @param string $title  Optional, this string is placed in the <title>
		 * @return string full HTML message
		 *
		 */
		function fckEditor2html($fck_html, $title = "Zarafa WebAccess"){
			$html = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">"
					."<html>\n"
					."<head>\n"
					."  <meta name=\"Generator\" content=\"Zarafa WebAccess v".phpversion("mapi")."\">\n"
					."  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
					."  <title>".htmlspecialchars($title)."</title>\n"
					."  <style type=\"text/css\">\n";

			// embed CSS file in e-mail
			$css_file = FCKEDITOR_PATH."/fck_editorarea.css";
			$html .= file_get_contents($css_file);

			$html .="  </style>\n"
					."</head>\n"
					."<body>\n"
					. $fck_html . "\n"
					."</body>\n"
					."</html>";

			return $html;
		}

		function calcFolderMessageSize($folder, $recursive=false)
		{
			$size = 0;
			if ($recursive){
				trigger_error("Recursive calculation of folder messageSize not yet implemented", E_USER_NOTICE);
			}

			$table = mapi_folder_getcontentstable($folder);

			$item_count = mapi_table_getrowcount($table);
			for($i = 0; $i<$item_count; $i+=50){
				$messages = mapi_table_queryrows($table, array(PR_MESSAGE_SIZE), $i, 50);
				foreach($messages as $message){
					if (isset($message[PR_MESSAGE_SIZE])){
						$size += $message[PR_MESSAGE_SIZE];
					}
				}
			}
			return $size;
		}



		/**
		 * Save task item.
		 *
		 * just one step more before saving task message that to support recurrence and task request here. We need
		 * to regenerate task if it is recurring and client has changed either set as complete or delete or
		 * given new start or end date.
		 *
		 * @param mapistore $store MAPI store of the message
		 * @param string $parententryid Parent entryid of the message (folder entryid, NOT message entryid)
		 * @param array $action Action array containing XML request
		 * @return array of PR_ENTRYID, PR_PARENT_ENTRYID and PR_STORE_ENTRYID properties of modified item
		 */
		function saveTask($store, $parententryid, $action)
		{
			$properties = $GLOBALS["properties"]->getTaskProperties();
			$send = isset($action["send"]) ? $action["send"] : false;

			if($store && $parententryid) {
				if(isset($action["props"])) {

					$props = $action["props"];

					if (!isset($action["props"]["entryid"])) {

						// Fetch message store properties of a current store.
						$msgstoreProps = mapi_getprops($store, array(PR_USER_ENTRYID, PR_MDB_PROVIDER, PR_MAILBOX_OWNER_ENTRYID));

						// Get current session and open addressbook
						$addrbook = $GLOBALS["mapisession"]->getAddressbook();

						// Open addressbook entry for current user.
						$userObject = mapi_ab_openentry($addrbook, $msgstoreProps[PR_USER_ENTRYID]);

						if(mapi_last_hresult() == NOERROR){
							$userProps = mapi_getprops($userObject);
							if(mapi_last_hresult() == NOERROR){
								// Store PR_SENDER_* properties for task in props variable.
								$props["sender_email_address"] = $userProps[PR_EMAIL_ADDRESS];
								$props["sender_name"] = $userProps[PR_DISPLAY_NAME];
								$props["sender_addrtype"] = $userProps[PR_ADDRTYPE];
								$props["sender_entryid"] = bin2hex($userProps[PR_ENTRYID]);
								$props["sender_search_key"] = $userProps[PR_SEARCH_KEY];
							}
						}

						// Store PR_SENT_REPRESENTING_* properties for task according to store type.
						switch ($msgstoreProps[PR_MDB_PROVIDER])
						{
							case ZARAFA_STORE_PUBLIC_GUID:
								/**
								 * store type is "public"
								 * Store PR_SENT_REPRESENTING_* properties for task in props variable.
								 */
								$props["sent_representing_entryid"] = bin2hex($userProps[PR_ENTRYID]);
								$props["sent_representing_name"] = $userProps[PR_DISPLAY_NAME];
								$props["sent_representing_addrtype"] = $userProps[PR_ADDRTYPE];
								$props["sent_representing_email_address"] = $userProps[PR_EMAIL_ADDRESS];
								$props["sent_representing_search_key"] = $userProps[PR_SEARCH_KEY];
								break;
							case ZARAFA_SERVICE_GUID:
								// store type is "default"
							case ZARAFA_STORE_DELEGATE_GUID:
								/**
								 * store type is "delegate"
								 * Open addressbook entry for mailbox owner.
								 */
								$ownerObject = mapi_ab_openentry($addrbook, $msgstoreProps[PR_MAILBOX_OWNER_ENTRYID]);
								if(mapi_last_hresult() == NOERROR){
									$ownerProps = mapi_getprops($ownerObject);
									if(mapi_last_hresult() == NOERROR){
										// Store PR_SENT_REPRESENTING_* properties for task in props variable.
										$props["sent_representing_entryid"] = bin2hex($ownerProps[PR_ENTRYID]);
										$props["sent_representing_name"] = $ownerProps[PR_DISPLAY_NAME];
										$props["sent_representing_addrtype"] = $ownerProps[PR_ADDRTYPE];
										$props["sent_representing_email_address"] = $ownerProps[PR_EMAIL_ADDRESS];
										$props["sent_representing_search_key"] = $ownerProps[PR_SEARCH_KEY];
									}
								}
								break;
						}
					}

					$messageProps = array();
					$recips = array();
					if(isset($action["recipients"]["recipient"])) {
						$recips = $action["recipients"]["recipient"];
					} else {
						$recips = false;
					}

					if (isset($action["props"]["entryid"]) && !empty($action["props"]["entryid"])) {
						$message = mapi_msgstore_openentry($store, hex2bin($action["props"]["entryid"]));

						if ($message) {
							$messageProps = mapi_getprops($message, array(PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID, $properties['recurring']));

							if ((isset($messageProps[$properties['recurring']]) && $messageProps[$properties['recurring']]) ||
								(isset($props['recurring']) && $props['recurring'])) {
								$recur = new TaskRecurrence($store, $message);

								if (isset($props['recurring_reset']) && $props['recurring_reset'] == 1) {
									$msgProps = $recur->setRecurrence($props);
								} else if ((isset($props['complete']) && $props['complete'] == 1)) {
									$msgProps = $recur->markOccurrenceComplete($props);
								}
							}
							mapi_savechanges($message);

							$message = $GLOBALS["operations"]->saveMessage($store, $parententryid, Conversion::mapXML2MAPI($properties, $props), $recips, (isset($action["dialog_attachments"]) ? $action["dialog_attachments"] : null), $messageProps);
							if (isset($msgProps) && $msgProps) $messageProps = $msgProps;
						}
					} else {
						//New message
						$message = $GLOBALS["operations"]->saveMessage($store, $parententryid, Conversion::mapXML2MAPI($properties, $props), $recips, (isset($action["dialog_attachments"]) ? $action["dialog_attachments"] : null), $messageProps);

						// Set recurrence
						if (isset($action['props']['recurring']) && $action['props']['recurring'] == 1) {
							$recur = new TaskRecurrence($store, $message);
							$recur->setRecurrence($props);
						}
					}

					if ($message) {
						// The task may be a delegated task, do an update if needed (will fail for non-delegated tasks)
						$tr = new TaskRequest($store, $message, $GLOBALS["mapisession"]->getSession());

						// @TODO: check whether task is request and not a normal task
						switch($send)
						{
							case "accept":
								$result = $tr->doAccept(_("Task Accepted:") . " ");
								break;
							case "decline":
								$result = $tr->doDecline(_("Task Declined:") . " ");
								break;
							case "request":
								$tr->sendTaskRequest(_("Task Request:") . " ");
								break;
							case "unassign":
								$tr->createUnassignedCopy();
								break;
							case "reclaim":
								$tr->reclaimownership();
								break;
							default:
								if (isset($props["messagechanged"]) && $props["messagechanged"])
									$tr->doUpdate(_("Task Updated:") . " ", _("Task Completed:") . " ");
						}

						// Notify Inbox that task request has been deleted
						if (isset($result) && is_array($result))
							$GLOBALS["bus"]->notify(bin2hex($result[PR_PARENT_ENTRYID]), TABLE_DELETE, $result);

						$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_SAVE, $messageProps);
					}
				}
			}

			mapi_savechanges($message);

			// Return message properties that can be sent to the bus to notify changes
			return $messageProps;
		}

		/**
		 * Deletes a task.
		 *
		 * deletes occurrence if task is a recurring item.
		 * @param mapistore $store MAPI Message Store Object
		 * @param string $parententryid parent entryid of the messages to be deleted
		 * @param array $entryids a list of entryids which will be deleted
		 * @param boolean $softDelete flag for soft-deleteing (when user presses Shift+Del)
		 * @return boolean true if action succeeded, false if not
		 */
		function deleteTask($store, $parententryid, $entryids, $action)
		{
			$result = false;

			// If user wants to delete only occurrence then delete this occurrence
			if (!is_array($entryids) && isset($action['deleteFlag'])) {
				$message = mapi_msgstore_openentry($store, $entryids);

				if ($message) {
					if ($action['deleteFlag'] == 'occurrence') {
						$recur = new TaskRecurrence($store, $message);
						$action['deleteOccurrence'] = true;
						$occurrenceDeleted = $recur->deleteOccurrence($action);
					} else if ($action['deleteFlag'] == 'decline' || $action['deleteFlag'] == 'complete') {
						$taskReq = new TaskRequest($store, $message, $GLOBALS["mapisession"]->getSession());

						if ($action['deleteFlag'] == 'decline') $taskReq->doDecline(_("Task Declined:") . " ");
						else if ($action['deleteFlag'] == 'complete') $taskReq->sendCompleteUpdate(_("Task Updated:") . " ", $action, _("Task Completed:") . " ");
					}
				}
			}

			// Deleting occurrence failed, maybe that was its last occurrence, so now we delete whole series.
			if (!isset($occurrenceDeleted) || !$occurrenceDeleted) {
				// If softdelete is set then set it in softDelete variable and pass it for deleteing message.
				$softDelete = isset($action["softdelete"]) ? $action["softdelete"] : false;
				$result = $GLOBALS["operations"]->deleteMessages($store, $parententryid, $entryids, $softDelete);
			} else {
				$result = array('occurrenceDeleted' => true);
			}

			return $result;
		}


////////////////////////////////////////////
// FIXME: OLD FUNCTIONS!!!                //
////////////////////////////////////////////



		/**
		* Parse string of recipients into individual recipients
		*
		* @param string $inputText The string from which to create recipient array (eg. 'john doe <john@doe.org>; jane@domain.com')
		* @param integer $rType An optional PR_RECIPIENT_TYPE
		* @return array Array of arrays (table) with message recipients, false if no valid recipients found
		*/
		function createRecipientList_old($inputText, $rType="")
		{
			$parsedRcpts = Array();
			// Data is separated by , or ; ; so we split by this first.
			$splitArray = preg_split("/[;]/", $inputText);
			$errors = 0;
			for ($i=0; $i<count($splitArray); $i++)
			{
				if($splitArray[$i])
				{
					$rcptMatches = Array();
					// Check format of the address
					if(preg_match('/([^<]*)<([^>]*)>/', $splitArray[$i], $rcptMatches)==0)
					{
						// Address only
						$parsedRcpts[$i][PR_DISPLAY_NAME] = trim($splitArray[$i]);
						$parsedRcpts[$i][PR_EMAIL_ADDRESS] = trim($splitArray[$i]);
					} else
					{
						// Address with name (a <b@c.nl> format)
						if (trim($rcptMatches[1]))
						{
							// Name available, so use it as name
							$parsedRcpts[$i][PR_DISPLAY_NAME] = trim($rcptMatches[1]);
						} else
						{
							// Name not available, use mail address as display name
							$parsedRcpts[$i][PR_DISPLAY_NAME] = trim($rcptMatches[2]);
						}
						$parsedRcpts[$i][PR_EMAIL_ADDRESS] = trim($rcptMatches[2]);
					}
					// Other properties, we only support SMTP here
					$parsedRcpts[$i][PR_ADDRTYPE] = "SMTP";
					if ($rType!="")
						$parsedRcpts[$i][PR_RECIPIENT_TYPE] = $rType;
					$parsedRcpts[$i][PR_OBJECT_TYPE] = MAPI_MAILUSER;

					// One-Off entry identifier; needed for Zarafa
					$parsedRcpts[$i][PR_ENTRYID] = mapi_createoneoff($parsedRcpts[$i][PR_DISPLAY_NAME], $parsedRcpts[$i][PR_ADDRTYPE], $parsedRcpts[$i][PR_EMAIL_ADDRESS]);
				}
			}
			if (count($parsedRcpts)>0&&$errors==0)
				return $parsedRcpts;
			else
				return false;
		}

		/**
		* Create a flat entrylist (used for PR_REPLY_RECIPIENT_ENTRIES) from a list of recipients
		*
		* These flatentrylists are used in PR_REPLY_RECIPIENT_ENTRIES, remember to
		* keep this property synchronized with PR_REPLY_RECIPIENT_NAMES.
		*
		* @param String $recipientArray The array with recipients to convert
		* @return boolean Returns the resulting flatentrylist
		*/
		function writeFlatEntryList($recipientArray)
		{
			$oneOffs = Array();
			foreach ($recipientArray as $recipient)
			{
				// Add display name if it doesn't exist
				if (!array_key_exists(PR_DISPLAY_NAME, $recipient)||empty($recipient[PR_DISPLAY_NAME]))
					$recipient[PR_DISPLAY_NAME] = $recipient[PR_EMAIL_ADDRESS];

				$oneOffs[] = mapi_createoneoff($recipient[PR_DISPLAY_NAME], $recipient[PR_ADDRTYPE], $recipient[PR_EMAIL_ADDRESS]);
			}

			// Construct string from array with (padded) One-Off entry identifiers
			//
			// Remember, if you want to take the createOneOff part above out: that code
			// produces a padded OneOff and we add the right amount of null characters
			// below.
			//
			// So below is a wrong method for composing a flatentrylist from oneoffs and
			// above is a wrong method form composing a oneoff.
			$flatEntryString = "";
			for ($i=0; $i<count($oneOffs); $i++)
			{
				$flatEntryString .= pack("Va*", strlen($oneOffs[$i]), $oneOffs[$i]);
				// Fill to 4-byte boundary
				$rest = strlen($oneOffs[$i])%4;
				for ($j=0;$j<$rest;$j++)
					$flatEntryString .= "\0";
			}
			// Pack the string with the number of flatentries and the stringlength
			return pack("V2a*", count($oneOffs), strlen($flatEntryString), $flatEntryString);
		}

		/**
		* Get a text body for a Non-Delivery report
		*
		* This function reads the necessary properties from the passed message and constructs
		* a user-readable NDR message from those properties
		*
		* @param mapimessage $message The NDR message to read the information from
		* @return string NDR body message as plaintext message.
		*/
		function getNDRbody($message)
		{
			$message_props  = mapi_getprops($message, array(PR_ORIGINAL_SUBJECT,PR_ORIGINAL_SUBMIT_TIME));

			// Use PR_BODY if it's there. Otherwise, create a recipient failed message.
			$body = mapi_openproperty($message, PR_BODY);
			if ($body == false) {
				$body = _("Your message did not reach some or all of the intended recipients")."\n\n";
				$body .= "\t"._("Subject").": ".$message_props[PR_ORIGINAL_SUBJECT]."\n";
				$body .= "\t"._("Sent").":    ".strftime("%a %x %X",$message_props[PR_ORIGINAL_SUBMIT_TIME])."\n\n";
				$body .= _("The following recipient(s) could not be reached").":\n";

				$recipienttable = mapi_message_getrecipienttable($message);
				$recipientrows = mapi_table_queryallrows($recipienttable,array(PR_DISPLAY_NAME,PR_REPORT_TIME,PR_REPORT_TEXT));
				foreach ($recipientrows as $recipient){
					$body .= "\n\t".$recipient[PR_DISPLAY_NAME]." on ".strftime("%a %x %X",$recipient[PR_REPORT_TIME])."\n";
					$body .= "\t\t".$recipient[PR_REPORT_TEXT]."\n";
				}
			}

			// Bon voyage!
			return $body;
		}

		/**
		* Detect plaintext body type of message
		*
		* @param mapimessage $message MAPI message resource to check
		* @return boolean TRUE if the message is a plaintext message, FALSE if otherwise
		*/
		function isPlainText($message)
		{
			$rtf = mapi_message_openproperty($message, PR_RTF_COMPRESSED);

			if (!$rtf)
				return true; // no RTF is found, so we use plain text

			// get first line of the RTF (removing all other lines after opening/decompressing)
			$rtf = preg_replace("/(\n.*)/m","",mapi_decompressrtf($rtf));

			// check if "\fromtext" exists, if so, it was plain text
			return strpos($rtf,"\\fromtext") !== false;
		}

		/**
		* Parse email recipient list and add all e-mail addresses to the recipient history
		*
		* The recipient history is used for auto-suggestion when writing e-mails. This function
		* opens the recipient history property (PR_EC_RECIPIENT_HISTORY) and updates or appends
		* it with the passed email addresses.
		*
		* @param emailAddresses XML array structure with recipients
		*/
		function addEmailsToRecipientHistory($emailAddresses){
			if(is_array($emailAddresses) && count($emailAddresses) > 0){
				// Retrieve the recipient history
				$stream = mapi_openpropertytostream($GLOBALS["mapisession"]->getDefaultMessageStore(), PR_EC_RECIPIENT_HISTORY);
				$hresult = mapi_last_hresult();

				if($hresult == NOERROR){
					$stat = mapi_stream_stat($stream);
					mapi_stream_seek($stream, 0, STREAM_SEEK_SET);
					$xmlstring = '';
					for($i=0;$i<$stat['cb'];$i+=1024){
						$xmlstring .= mapi_stream_read($stream, 1024);
					}

					if($xmlstring !== "") {
						// don't pass empty string to xml parser otherwise it will give error
						$xml = new XMLParser();
						$recipient_history = $xml->getData(w2u($xmlstring));
					}
				}

				/**
				 * Check to make sure the recipient history is returned in array format
				 * and not a PEAR error object.
				 */
				if(!isset($recipient_history) || !is_array($recipient_history) || !isset($recipient_history['recipients']['recipient'])){
					$recipient_history = Array(
						'recipients' => Array(
							'recipient' => Array()
						)
					);
				}else{
					/**
					 * When only one recipient is found in the XML it is saved as a single dimensional array
					 * in $recipient_history['recipients']['recipient'][RECIPDATA]. When multiple recipients
					 * are found, a multi-dimensional array is used in the format
					 * $recipient_history['recipients']['recipient'][0][RECIPDATA].
					 */
					if($recipient_history['recipients']['recipient']){
						if(!is_numeric(key($recipient_history['recipients']['recipient']))){
							$recipient_history['recipients']['recipient'] = Array(
								0 => $recipient_history['recipients']['recipient']
							);
						}
					}
				}

				$l_aNewHistoryItems = Array();
				// Loop through all new recipients
				for($i=0;$i<count($emailAddresses);$i++){
					$emailAddresses[$i]['name'] = $emailAddresses[$i]['name'];
					$emailAddresses[$i]['address'] = $emailAddresses[$i]['address'];

					if(preg_match("/^\[([^]]+)\]$/", $emailAddresses[$i]['name'], $matches)) {
						$emailAddresses[$i]['name'] = $matches[1];
						$emailAddresses[$i]['address'] = $matches[1];
						$emailAddresses[$i]['objecttype'] = MAPI_DISTLIST;
					} else {
						$emailAddresses[$i]['objecttype'] = MAPI_MAILUSER;
					}

					$l_bFoundInHistory = false;
					// Loop through all the recipients in history
					if(is_array($recipient_history['recipients']['recipient'])) {
                        for($j=0;$j<count($recipient_history['recipients']['recipient']);$j++){
                            // Email address already found in history
                            if($emailAddresses[$i]['address'] == $recipient_history['recipients']['recipient'][$j]['email']){
                                $l_bFoundInHistory = true;
                                // Check if a name has been supplied.
                                if(strlen(trim($emailAddresses[$i]['name'])) > 0){
                                    // Check if the name is not the same as the email address
                                    if(trim($emailAddresses[$i]['name']) != $emailAddresses[$i]['address']){
                                        $recipient_history['recipients']['recipient'][$j]['name'] = trim($emailAddresses[$i]['name']);
                                    // Check if the recipient history has no name for this email
                                    }elseif(strlen(trim($recipient_history['recipients']['recipient'][$j]['name'])) == 0){
                                        $recipient_history['recipients']['recipient'][$j]['name'] = trim($emailAddresses[$i]['name']);
                                    }
                                }
                                $recipient_history['recipients']['recipient'][$j]['count']++;
                                $recipient_history['recipients']['recipient'][$j]['last_used'] = mktime();
                                break;
                            }
                        }
                    }
					if(!$l_bFoundInHistory && !isset($l_aNewHistoryItems[$emailAddresses[$i]['address']])){
						$l_aNewHistoryItems[$emailAddresses[$i]['address']] = Array(
							'name' => $emailAddresses[$i]['name'],
							'email' => $emailAddresses[$i]['address'],
							'count' => 1,
							'last_used' => mktime(),
							'objecttype' => $emailAddresses[$i]['objecttype']

							);
					}
				}
				if(count($l_aNewHistoryItems) > 0){
					foreach($l_aNewHistoryItems as $l_aValue){
						$recipient_history['recipients']['recipient'][] = $l_aValue;
					}
				}

				$xml = new XMLBuilder();
				$l_sNewRecipientHistoryXML = u2w($xml->build($recipient_history));

				$stream = mapi_openpropertytostream($GLOBALS["mapisession"]->getDefaultMessageStore(), PR_EC_RECIPIENT_HISTORY, MAPI_CREATE | MAPI_MODIFY);
				mapi_stream_write($stream, $l_sNewRecipientHistoryXML);
				mapi_stream_commit($stream);
				mapi_savechanges($GLOBALS["mapisession"]->getDefaultMessageStore());
			}
		}

		/**
		* Extract all email addresses from a list of recipients
		*
		* @param string $p_sRecipients String containing e-mail addresses, as typed by user (eg. '<john doe> john@doe.org; jane@doe.org')
		* @return array Array of e-mail address parts (eg. 'john@doe.org', 'jane@doe.org')
		*
		* this function is currently unused
		*/
		function extractEmailAddresses($p_sRecipients){
			$l_aRecipients = explode(';', $p_sRecipients);
			$l_aReturn = Array();
			for($i=0;$i<count($l_aRecipients);$i++){
				$l_aRecipients[$i] = trim($l_aRecipients[$i]);
				$l_sRegex = '/^([^<]*<){0,1}(([a-z0-9=_\+\.\-\'\/])+\@(([a-z0-9\-])+\.)+([a-z0-9]{2,5})+)>{0,1}$/';
				preg_match($l_sRegex, $l_aRecipients[$i], $l_aMatches);
				$l_aReturn[] = $l_aMatches[0];
			}
			return $l_aReturn;
		}

		/**
		* Get the SMTP e-mail of an addressbook entry
		*
		* @param string $entryid Addressbook entryid of object
		* @return string SMTP e-mail address of that entry or FALSE on error
		*/
		function getEmailAddressFromEntryID($entryid) {
		    $mailuser = mapi_ab_openentry($GLOBALS["mapisession"]->getAddressbook(), $entryid);
		    if(!$mailuser)
		        return false;

            $abprops = mapi_getprops($mailuser, array(PR_SMTP_ADDRESS, PR_EMAIL_ADDRESS));

            if(isset($abprops[PR_SMTP_ADDRESS]))
                return $abprops[PR_SMTP_ADDRESS];
            else if(isset($abprops[PR_EMAIL_ADDRESS]))
				return $abprops[PR_EMAIL_ADDRESS];
			else
                return false;
		}

		/**
		* Get all rules of a store
		*
		* This function opens the rules table for the specified store, and reads
		* all rules with PR_RULE_PROVIDER equal to 'RuleOrganizer'. These are the rules
		* that the user sees when managing rules from Outlook.
		*
		* @param mapistore $store Store in which rules reside
		* @param array $properties Property mappings for rules properties
		* @return array XML array structure of rules from the store
		*/
		function getRules($store, $properties) {
			$inbox = mapi_msgstore_getreceivefolder($store);

			if(!$inbox)
				return false;

			$modifyrulestable = mapi_folder_openmodifytable($inbox);
			$rulestable = mapi_rules_gettable($modifyrulestable);

			mapi_table_restrict($rulestable, array(RES_CONTENT,
												array(
													FUZZYLEVEL	=>	FL_FULLSTRING | FL_IGNORECASE,
													ULPROPTAG	=>	PR_RULE_PROVIDER,
													VALUE		=>	array(
														PR_RULE_PROVIDER	=>	"RuleOrganizer"
													)
												)
											)
			);


			mapi_table_sort($rulestable, array(PR_RULE_SEQUENCE => TABLE_SORT_ASCEND));
			$rows = mapi_table_queryrows($rulestable, $properties, 0, 0x7fffffff);

			$xmlrows = array();

			foreach($rows as $row) {
				$xmlrow = Conversion::mapMAPI2XML($properties, $row);
				array_push($xmlrows, $xmlrow);
			}

			return $xmlrows;
		}

		/**
		* Update rules in a store
		*
		* This function replaces all rules in a store with the passed new ruleset.
		*
		* @param mapistore $store The store whose rules should be modified
		* @param array $rules The XML array structure with the new rules
		* @param array $properties The property mapping for rules properties
		* @return boolean True on success, false on error
		*/
		function updateRules($store, $rules, $properties)
		{
			if (!is_array($rules))
				return false;

			$inbox = mapi_msgstore_getreceivefolder($store);

			if(!$inbox)
				return false;

			$modifyrulestable = mapi_folder_openmodifytable($inbox);

			// get provider data id's from all rules
			$rulestable = mapi_rules_gettable($modifyrulestable);
			mapi_table_restrict($rulestable, array(RES_CONTENT,
												array(
													FUZZYLEVEL	=>	FL_FULLSTRING | FL_IGNORECASE,
													ULPROPTAG	=>	PR_RULE_PROVIDER,
													VALUE		=>	array(
														PR_RULE_PROVIDER	=>	"RuleOrganizer"
													)
												)
											)
			);
			mapi_table_sort($rulestable, array(PR_RULE_SEQUENCE => TABLE_SORT_ASCEND));
			$current_rules = mapi_table_queryrows($rulestable, $properties, 0, 0x7fffffff);
			$provider_data = array();
			foreach($current_rules as $row){
				$data = unpack("Vnum/Vid/a*datetime", $row[PR_RULE_PROVIDER_DATA]);
				$provider_data[$row[PR_RULE_ID]] = $data["id"];
			}

			// delete all rules from the rules table
			$deleteRules = array();
			foreach($current_rules as $delRow){
				$deleteRules[] = array(
						"rowflags"	=>	ROW_REMOVE,
						"properties"=>	$delRow
				);
			}
			mapi_rules_modifytable($modifyrulestable, $deleteRules);

			if (!isset($rules[0]))
				$rules = array($rules);

			// find rules to delete
			$deleteRules = array();
			$deletedRowIDs = array();
			foreach($rules as $key=>$rule){
				if (isset($rule["deleted"])){
					$deleteRules[] = $rule;
					$convertedRule = Conversion::mapXML2MAPI($properties, $rule);
					$deletedRowIDs[] = $convertedRule[PR_RULE_ID];
					unset($rules[$key]);
				}
			}

			// add/modify new rules
			foreach($rules as $k=>$rule){
				$rules[$k] = Conversion::mapXML2MAPI($properties, $rule);
			}

			$rows = array();

			$modified = array();

			// add/update rules from the client
			foreach($rules as $rule){

				if (!isset($rule[PR_RULE_ID])){
					$row_flags = ROW_ADD;
					if (is_array($provider_data)){
						$dataId = max($provider_data)+1;
					}else{
						$dataId = 1;
					}
					$provider_data[] = $dataId;
				}else{
					$row_flags = ROW_ADD;
					$dataId = $provider_data[$rule[PR_RULE_ID]];
					$modified[] = $rule[PR_RULE_ID];
				}

				if (!isset($rule[PR_RULE_ACTIONS])){
					$rule[PR_RULE_ACTIONS] = array(array("action"=>OP_DEFER_ACTION, "dam"=>hex2bin("E0C810000120000100000000000000010000000000000001000000360000000200FFFF00000C004352756C65456C656D656E7490010000010000000000000001000000018064000000010000000000000001000000")));
				}
				if (!isset($rule[PR_RULE_CONDITION])){
					$rule[PR_RULE_CONDITION] = array(RES_EXIST, array(ULPROPTAG=>PR_MESSAGE_CLASS));
				}
				if (!isset($rule[PR_RULE_SEQUENCE])){
					$rule[PR_RULE_SEQUENCE] = 10;
				}

				if (!isset($rule[PR_RULE_STATE])){
					$rule[PR_RULE_STATE] = ST_DISABLED;
				}

				if (!isset($rule[PR_RULE_NAME]) || $rule[PR_RULE_NAME]==""){
					$rule[PR_RULE_NAME] = _("Untitled rule");
				}

				$rule[PR_RULE_LEVEL] = 0;
				$rule[PR_RULE_PROVIDER] = "RuleOrganizer";
				$rule[PR_RULE_PROVIDER_DATA] = pack("VVa*", 1, $dataId, Conversion::UnixTimeToCOleDateTime(time()));

				$rows[] = array(
						"rowflags"	=>	$row_flags,
						"properties"=>	$rule
				);
			}
			// update PR_RULE_PROVIDER_DATA for every other rule
			foreach($current_rules as $rule){
				if (!in_array($rule[PR_RULE_ID], $modified) && !in_array($rule[PR_RULE_ID], $deletedRowIDs)){

					$dataId = $provider_data[$rule[PR_RULE_ID]];
					$rule[PR_RULE_PROVIDER_DATA] = pack("VVa*", 1, $dataId, Conversion::UnixTimeToCOleDateTime(time()));

					$rows[] = array("properties"=>$rule, "rowflags"=> ROW_MODIFY);
				}
			}

			// sort rules on PR_RULE_SEQUENCE to fix the order
			usort($rows, array($this, "cmpRuleSequence"));
			$seq = 10;
			foreach($rows as $k=>$row){
				$rows[$k]["properties"][PR_RULE_SEQUENCE] = $seq;
				$seq++;
				unset($rows[$k]["properties"][PR_RULE_ID]); // remove RULE_ID because this is the property Outlook does its sort when displaying rules instead of RULE_SEQUENCE!
			}

			// sort the rules, so they will get a RULE_ID in the right order when adding the rules
			uasort($rows, create_function('$a,$b','return $a["properties"][PR_RULE_SEQUENCE]>$b["properties"][PR_RULE_SEQUENCE];'));

			mapi_rules_modifytable($modifyrulestable, $rows);
			$result = mapi_last_hresult();

			// delete (outlook) client rules
			$assocTable = mapi_folder_getcontentstable($inbox, MAPI_ASSOCIATED);
			mapi_table_restrict($assocTable, array(RES_CONTENT,
												array(
													FUZZYLEVEL	=>	FL_FULLSTRING | FL_IGNORECASE,
													ULPROPTAG	=>	PR_MESSAGE_CLASS,
													VALUE		=>	array(
														PR_MESSAGE_CLASS	=>	"IPM.RuleOrganizer"
													)
												)
											)
			);
			$messages = mapi_table_queryallrows($assocTable, array(PR_ENTRYID, PR_MESSAGE_CLASS));
			$delete = array();
			for($i=0;$i<count($messages);$i++){
				if ($messages[$i][PR_MESSAGE_CLASS] == "IPM.RuleOrganizer"){ // just to be sure that the restriction worked ;)
					array_push($delete, $messages[$i][PR_ENTRYID]);
				}
			}
			if (count($delete)>0){
				mapi_folder_deletemessages($inbox, $delete);
			}


			return ($result==NOERROR);
		}

		/**
		* @access private
		*/
		function cmpRuleSequence($a, $b)
		{
			if ($a["properties"][PR_RULE_SEQUENCE]==$b["properties"][PR_RULE_SEQUENCE])
				return 0;
			return ($a["properties"][PR_RULE_SEQUENCE]<$b["properties"][PR_RULE_SEQUENCE])?-1:1;
		}

		/**
		* Get folder name
		*
		* @param $storeid Store entryid of store in which folder resides
		* @param $folderid Folder entryid of folder
		* @return string Folder name of specified folder
		*/
		function getFolderName($storeid, $folderid) {
			$folder = mapi_openentry($GLOBALS["mapisession"]->getSession(), $folderid, 0);
			if (!$folder) {
				$store = $GLOBALS["mapisession"]->openMessageStore($storeid);
				if(!$store)
					return false;

				$folder = mapi_msgstore_openentry($store, $folderid);
				if(!$folder)
					return false;
			}

			$folderprops = mapi_getprops($folder, array(PR_DISPLAY_NAME));

			return $folderprops[PR_DISPLAY_NAME];
		}

		/**
		* Send a meeting cancellation
		*
		* This function sends a meeting cancellation for the meeting references by the passed entryid. It
		* will send the meeting cancellation and move the item itself to the waste basket.
		*
		* @param mapistore $store The store in which the meeting request resides
		* @param string $entryid Entryid of the appointment for which the cancellation should be sent.
		*/
		function cancelInvitation($store, $entryid, $action = false) {
			$message = $GLOBALS["operations"]->openMessage($store, $entryid);

			$req = new Meetingrequest($store, $message, $GLOBALS["mapisession"]->getSession(), ENABLE_DIRECT_BOOKING);

			if(isset($action["exception"]) && $action["exception"] == true) {
				$recurrence = new Recurrence($store, $message);
				$basedate = (int) $action["basedate"];

				if(isset($action["delete"]) && $action["delete"] == true) {
					$recurrence->createException(array(), $basedate, true);
				}

				// if recurrence pattern of meeting request is changed then need to send full update
				// Send the meeting request
				$req->updateMeetingRequest();
				$req->sendMeetingRequest(((isset($action["delete"]) && $action["delete"]) ? 1 : 0), u2w(_("Canceled: ")), isset($basedate) ? $basedate : false);

				// save changes in the message
				mapi_message_savechanges($message);

				// Notify the bus that the message has been deleted
				$messageProps = mapi_getprops($message, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID));
				$GLOBALS["bus"]->notify(bin2hex($messageProps[PR_PARENT_ENTRYID]), TABLE_SAVE, $messageProps);
			} else {
				// Send the cancellation
				$req->updateMeetingRequest();
				$req->sendMeetingRequest(true, u2w(_("Canceled: ")));

				// save changes in the message
				mapi_message_savechanges($message);

				// Get current user's store, to get the wastebasket.
				$defaultStore = $req->openDefaultStore();
				$root = mapi_msgstore_openentry($store);
				$storeprops = mapi_getprops($defaultStore, array(PR_IPM_WASTEBASKET_ENTRYID));
				$wastebasket = mapi_msgstore_openentry($defaultStore, $storeprops[PR_IPM_WASTEBASKET_ENTRYID]);

				// Move the message to the deleted items
				mapi_folder_copymessages($root, array($entryid), $wastebasket, MESSAGE_MOVE);

				// Notify the bus that the message has been deleted
				$messageProps = mapi_getprops($message, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID));
				$GLOBALS["bus"]->notify(bin2hex($messageProps[PR_PARENT_ENTRYID]), TABLE_DELETE, $messageProps);
			}
		}

		/**
		* Remove all appointments for a certain meeting request
		*
		* This function searches the default calendar for all meeting requests for the specified
		* meeting. All those appointments are then removed.
		*
		* @param mapistore $store Mapi store in which the meeting request and the calendar reside
		* @param string $entryid Entryid of the meeting request or appointment for which all items should be deleted
		*/
		function removeFromCalendar($store, $entryid) {
			$message = $GLOBALS["operations"]->openMessage($store, $entryid);

			$req = new Meetingrequest($store, $message, $GLOBALS["mapisession"]->getSession(), ENABLE_DIRECT_BOOKING);

			$req->doRemoveFromCalendar();

			// Notify the bus that the message has been deleted
			$messageProps = mapi_getprops($message, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID));
			$GLOBALS["bus"]->notify(bin2hex($messageProps[PR_PARENT_ENTRYID]), TABLE_DELETE, $messageProps);
		}

		/**
		* Get addressbook hierarchy
		*
		* This function returns the entire hierarchy of the addressbook, with global addressbooks, and contacts
		* folders.
		*
		* The input parameter $storelist is an associative array with the format:
		* $storelist["store"] = array of store objects to traverse for all contacts folders
		* $storelist["folder"] = array of store objects to traverse for only the default contacts folder
		*
		* The output array contains an associative array for each found contact folder. Each entry contains
		* "display_name" => Name of the folder, "entryid" => entryid of the folder, "parent_entryid" => parent entryid
		* "storeid" => store entryid, "type" => gab | contacts
		*
		* @param array Associative array with store information
		* @return array Array of associative arrays with addressbook container information
		* @todo Fix bizarre input parameter format
		*/
		function getAddressbookHierarchy($storelist=false)
		{
			$ab = $GLOBALS["mapisession"]->getAddressbook();
			$dir = mapi_ab_openentry($ab);
			$table = mapi_folder_gethierarchytable($dir, CONVENIENT_DEPTH);
			$items = mapi_table_queryallrows($table, array(PR_DISPLAY_NAME, PR_ENTRYID, PR_PARENT_ENTRYID, PR_DEPTH));

			$folders = array();

			$parent = false;
			foreach($items as $item){
				// TODO: fix for missing PR_PARENT_ENTRYID, see #2190
				if ($item[PR_DEPTH]==0)
					$parent = $item[PR_ENTRYID];

				$item[PR_PARENT_ENTRYID] = $parent;

				$folders[] = array(
								"display_name"	=> w2u($item[PR_DISPLAY_NAME]),
								"entryid"		=> array("attributes"=>array("type"=>"binary"), "_content"=> bin2hex($item[PR_ENTRYID])),
								"parent_entryid"=> array("attributes"=>array("type"=>"binary"), "_content"=> bin2hex($item[PR_PARENT_ENTRYID])),
								"type"			=> "gab"
							);
			}

			if ($storelist){
				foreach($storelist["store"] as $store){
					if (empty($store))
						continue;

					$cur_store = $GLOBALS["mapisession"]->openMessageStore(hex2bin($store));
					$store_props = mapi_getprops($cur_store, array(PR_IPM_SUBTREE_ENTRYID, PR_IPM_PUBLIC_FOLDERS_ENTRYID, PR_MDB_PROVIDER, PR_DISPLAY_NAME, PR_MAILBOX_OWNER_NAME, PR_IPM_WASTEBASKET_ENTRYID));
					/**
					 * Check to see if we are dealing with a public store or not. The first level below
					 * the public folder contains folders that are linked. The CONVENIENT_DEPTH property
					 * will not work there. The PR_IPM_PUBLIC_FOLDERS_ENTRYID holds the entryID of the
					 * folder that we can use as the $subtree.
					*/
					// Public store
					if($store_props[PR_MDB_PROVIDER] == ZARAFA_STORE_PUBLIC_GUID){
							$subtree = mapi_msgstore_openentry($cur_store, $store_props[PR_IPM_PUBLIC_FOLDERS_ENTRYID]);
							if (mapi_last_hresult()) continue;
					// Private store
					} else {
							$subtree = mapi_msgstore_openentry($cur_store, $store_props[PR_IPM_SUBTREE_ENTRYID]);
							if (mapi_last_hresult()) continue;
					}

					// Get "Deleted Items" folder
					if (isset($store_props[PR_IPM_WASTEBASKET_ENTRYID])) {
						$deleted_items_folder = mapi_msgstore_openentry($cur_store, $store_props[PR_IPM_WASTEBASKET_ENTRYID]);
					}

					// Remove folder from hierarchylist that are also in "Deleted Items" folder
					$deletedFoldesrRestriction = array();

					if (isset($deleted_items_folder)) {
						// Get all contact folders from "Deleted Items" folder
						$hie = mapi_folder_gethierarchytable($deleted_items_folder, CONVENIENT_DEPTH);
						mapi_table_restrict($hie, array(RES_CONTENT,
													array(
															FUZZYLEVEL	=>	FL_PREFIX|FL_IGNORECASE,
															ULPROPTAG	=>	PR_CONTAINER_CLASS,
															VALUE		=>	array(PR_CONTAINER_CLASS	=>	"IPF.Contact")
													)
												),
												TBL_BATCH
						);

						$deleted_folders = mapi_table_queryallrows($hie, array(PR_ENTRYID));

						/**
						 * Prepare restriction that removes all folders
						 * which are also in "Deleted Items" folder.
						 */
						$deletedFoldersRestriction = array();
						foreach($deleted_folders as $folder){
							$deletedFoldersRestriction[] = 	array(RES_PROPERTY,
														array(
																RELOP		=>	RELOP_NE,
																ULPROPTAG	=>	PR_ENTRYID,
																VALUE		=>	array(PR_ENTRYID	=>	$folder[PR_ENTRYID])
														)
													);
						}
					}

					// Get all contact folders from hierarchylist
					$hierarchy = mapi_folder_gethierarchytable($subtree, CONVENIENT_DEPTH);

					// Perform restriction on folders are found in 'Deleted Items'
					if (count($deletedFoldersRestriction) > 0){
						mapi_table_restrict($hierarchy, array(RES_AND,
															array(
																array(RES_AND,
																	$deletedFoldersRestriction
																),
																array(RES_CONTENT,
																	array(
																			FUZZYLEVEL	=>	FL_PREFIX|FL_IGNORECASE,
																			ULPROPTAG	=>	PR_CONTAINER_CLASS,
																			VALUE		=>	array(PR_CONTAINER_CLASS	=>	"IPF.Contact")
																	)
																)
															)
														),
														TBL_BATCH
						);
					} else {
						mapi_table_restrict($hierarchy, array(RES_CONTENT,
															array(
																	FUZZYLEVEL	=>	FL_PREFIX|FL_IGNORECASE,
																	ULPROPTAG	=>	PR_CONTAINER_CLASS,
																	VALUE		=>	array(PR_CONTAINER_CLASS	=>	"IPF.Contact")
															)
														),
														TBL_BATCH
						);
					}

					switch($store_props[PR_MDB_PROVIDER]){
						case ZARAFA_STORE_PUBLIC_GUID:
							$store_owner = " - " . w2u($store_props[PR_DISPLAY_NAME]);
							break;
						case ZARAFA_STORE_DELEGATE_GUID:
						default:
							$store_owner = " - " . w2u($store_props[PR_MAILBOX_OWNER_NAME]);
							break;
					}

					$store_folders = mapi_table_queryallrows($hierarchy, array(PR_DISPLAY_NAME, PR_ENTRYID));
					foreach($store_folders as $item){
						$folders[] = array(
										"display_name"	=> w2u($item[PR_DISPLAY_NAME]) . $store_owner,
										"entryid"		=> array("attributes"=>array("type"=>"binary"), "_content"=> bin2hex($item[PR_ENTRYID])),
										"parent_entryid"=> array("attributes"=>array("type"=>"binary"), "_content"=> bin2hex($item[PR_ENTRYID])),
										"storeid"		=> array("attributes"=>array("type"=>"binary"), "_content"=> $store),
										"type"			=> "contacts"
									);
					}
				}

				foreach($storelist["folder"] as $store){
					if (empty($store))
						continue;

					$cur_store = $GLOBALS["mapisession"]->openMessageStore(hex2bin($store));
					$store_props = mapi_getprops($cur_store, array(PR_MDB_PROVIDER, PR_DISPLAY_NAME, PR_MAILBOX_OWNER_NAME));
					$root_folder = mapi_msgstore_openentry($cur_store, null);
					$root_props = mapi_getprops($root_folder, array(PR_IPM_CONTACT_ENTRYID));
					$contact_folder = mapi_msgstore_openentry($cur_store, $root_props[PR_IPM_CONTACT_ENTRYID]);
					if (mapi_last_hresult()) continue;

					switch($store_props[PR_MDB_PROVIDER]){
						case ZARAFA_STORE_PUBLIC_GUID:
							$store_owner = " - " . w2u($store_props[PR_DISPLAY_NAME]);
							break;
						case ZARAFA_STORE_DELEGATE_GUID:
						default:
							$store_owner = " - " . w2u($store_props[PR_MAILBOX_OWNER_NAME]);
							break;
					}

					$contact_props = mapi_getprops($contact_folder, array(PR_DISPLAY_NAME, PR_ENTRYID));
					$folders[] = array(
									"display_name"	=> w2u($item[PR_DISPLAY_NAME]) . $store_owner,
									"entryid"		=> array("attributes"=>array("type"=>"binary"), "_content"=> bin2hex($contact_props[PR_ENTRYID])),
									"parent_entryid"=> array("attributes"=>array("type"=>"binary"), "_content"=> bin2hex($contact_props[PR_ENTRYID])),
									"storeid"		=> array("attributes"=>array("type"=>"binary"), "_content"=> $store),
									"type"			=> "contacts"
								);
				}
			}

			return $folders;
		}

		/**
		 * Publishing the FreeBusy information of the default calendar. The
		 * folderentryid argument is used to check if the default calendar
		 * should be updated or not.
		 *
		 * @param $store MAPIobject Store object of the store that needs publishing
		 * @param $folderentryid binary entryid of the folder that needs to be updated.
		 */
		function publishFreeBusy($store, $folderentryid=false){
			// Publish updated free/busy information
			// First get default calendar from the root folder
			$rootFolder = mapi_msgstore_openentry($store, null);
			$rootFolderProps = mapi_getprops($rootFolder, array(PR_IPM_APPOINTMENT_ENTRYID));

			// If no folderentryid supplied or if the supplied entryid matches the default calendar.
			if(!$folderentryid || $rootFolderProps[PR_IPM_APPOINTMENT_ENTRYID] == $folderentryid){
				// Get the calendar and owner entryID
				$calendar = mapi_msgstore_openentry($store, $rootFolderProps[PR_IPM_APPOINTMENT_ENTRYID]);
				$storeProps = mapi_msgstore_getprops($store, array(PR_MAILBOX_OWNER_ENTRYID));
				if (isset($storeProps[PR_MAILBOX_OWNER_ENTRYID])){
					// Lets share!
					$pub = new FreeBusyPublish($GLOBALS["mapisession"]->getSession(), $store, $calendar, $storeProps[PR_MAILBOX_OWNER_ENTRYID]);
					$pub->publishFB(time() - (7 * 24 * 60 * 60), 6 * 30 * 24 * 60 * 60); // publish from one week ago, 6 months ahead
				}
			}
		}

		/**
		 * get quota information of store
		 *
		 * @param $store MAPIobject Store object of the store
		 * @return array information about quota and current store size (in KB)
		 */
		function getQuotaDetails($store = null){
			$checkQouta = true;

			if($store === null) {
				$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
			}

			// @TODO use mapi_zarafa_getquota here
			$storeProps = mapi_getprops($store, array(PR_MDB_PROVIDER, PR_QUOTA_WARNING_THRESHOLD, PR_QUOTA_SEND_THRESHOLD, PR_QUOTA_RECEIVE_THRESHOLD, PR_MESSAGE_SIZE_EXTENDED));

			switch($storeProps[PR_MDB_PROVIDER]){
				case ZARAFA_STORE_PUBLIC_GUID:
					$checkQouta = false;
					break;
				case ZARAFA_STORE_DELEGATE_GUID:
				default:
					$checkQouta = true;
					break;
			}

			if(!$checkQouta) {
				return false;
			} else {
				return array(
					"store_size" => round($storeProps[PR_MESSAGE_SIZE_EXTENDED]/1024),
					"quota_warning" => $storeProps[PR_QUOTA_WARNING_THRESHOLD],
					"quota_soft" => $storeProps[PR_QUOTA_SEND_THRESHOLD],
					"quota_hard" => $storeProps[PR_QUOTA_RECEIVE_THRESHOLD]
				);
			}
		}
		/**
		 * Function checks whether user has an access over the specified folder or not.
		 * @param object MAPI Message Store Object
		 * @param string $parententryid entryid of the folder
		 * @return boolean true if user has an access over the folder, false if not.
		 */
		function checkFolderAccess($store, $parententryid)
		{
			$folder = mapi_msgstore_openentry($store, $parententryid);
			$accessToFolder = false;
			if($folder){
				$folderProps = mapi_getProps($folder, Array(PR_ACCESS));
				$folderProps[PR_ACCESS];
				if(($folderProps[PR_ACCESS] & MAPI_ACCESS_CREATE_CONTENTS) != 0){
					$accessToFolder = true;
				}
			}
			return $accessToFolder;
		}

		/**
		 * Function stores the uploaded items in attachments state file.
		 * @param object MAPI Message Store Object
		 * @param array $entryids entryids of the selected message items
		 * @param string $dialog_attachments
		 * @return array of attachment items data
		 */
		function setAttachmentInSession($store, $entryids, $dialog_attachments)
		{
			$attachments = array();
			$attachments["attachment"] = array();

			$attachment_state = new AttachmentState();
			$attachment_state->open();

			foreach($entryids as $key => $entryid) {
				$message = mapi_msgstore_openentry($store, hex2bin($entryid));
				$msg_props = mapi_getprops($message, array(PR_STORE_ENTRYID, PR_ENTRYID, PR_MESSAGE_SIZE, PR_SUBJECT,  PR_MESSAGE_CLASS));

				// stripping path details
				$tmpname = w2u($msg_props[PR_SUBJECT]) . rand();

				// Add file information to the session
				$attachment_state->addAttachmentFile($dialog_attachments, $tmpname, Array(
					"name"			=> empty($msg_props[PR_SUBJECT]) ? _("Untitled") : w2u($msg_props[PR_SUBJECT]),
					"size"			=> $msg_props[PR_MESSAGE_SIZE],
					"type"			=> 'message/rfc822',
					"sourcetype"	=> 'items',
					"storeid"		=> $msg_props[PR_STORE_ENTRYID],
					"entryid"		=> $msg_props[PR_ENTRYID],
					"message_class" => $msg_props[PR_MESSAGE_CLASS]
				));

				// get attachment data, modify and send it to client
				$attachment = $attachment_state->getAttachmentFile($dialog_attachments, $tmpname);

				// add extra data
				$attachment["attach_num"] = $tmpname;
				$attachment["entryid"] = bin2hex($attachment["entryid"]);
				$attachment["storeid"] = bin2hex($attachment["storeid"]);
				$attachment["attach_message_class"] = 'IPM.NOTE';

				array_push($attachments["attachment"], $attachment);
			}

			$attachment_state->close();

			return $attachments;
		}
	}
?>
