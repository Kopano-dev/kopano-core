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
	 * Hierarchy Module
	 *
	 * @todo
	 * - Check the code at deleteFolder and at copyFolder. Looks the same.
	 */
	class HierarchyModule extends Module
	{
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param string $folderentryid Entryid of the folder. Data will be selected from this folder.
		 * @param array $data list of all actions.
		 */
		function HierarchyModule($id, $data)
		{
			$this->columns = array();
			$this->columns["entryid"] = PR_ENTRYID;
			$this->columns["store_entryid"] = PR_STORE_ENTRYID;
			$this->columns["parent_entryid"] = PR_PARENT_ENTRYID;
			$this->columns["display_name"] = PR_DISPLAY_NAME;
			$this->columns["container_class"] = PR_CONTAINER_CLASS;
			$this->columns["access"] = PR_ACCESS;
			$this->columns["rights"] = PR_RIGHTS;
			$this->columns["content_count"] = PR_CONTENT_COUNT;
			$this->columns["content_unread"] = PR_CONTENT_UNREAD;
			$this->columns["subfolders"] = PR_SUBFOLDERS;
			$this->columns["store_support_mask"] = PR_STORE_SUPPORT_MASK;
		
			parent::Module($id, $data, array(OBJECT_SAVE, OBJECT_DELETE, TABLE_SAVE, TABLE_DELETE));
		}
		
		/**
		 * Function which returns a list of entryids, which is used to register this module. It
		 * returns the ipm_subtree entryids of every message store.
		 * @return array list of entryids
		 */
		function getEntryID()
		{
			$entryid = array();
			$storelist = $GLOBALS["mapisession"]->getAllMessageStores();
			
			foreach($storelist as $store)
			{
				$store_props = mapi_getprops($store, array(PR_IPM_SUBTREE_ENTRYID));
				/**
				 * register notification events to IPM_SUBTREE folder of every store
				 * because we are not doing anything above folder IPM_SUBTREE
				 */
				$ipm_subtree = mapi_msgstore_openentry($store, $store_props[PR_IPM_SUBTREE_ENTRYID]);
				$ipm_subtree_props = mapi_getprops($ipm_subtree, array(PR_ENTRYID));
				array_push($entryid, bin2hex($ipm_subtree_props[PR_ENTRYID]));
			}
			
			return $entryid;
		}
		
		/**
		 * Executes all the actions in the $data variable.
		 * @return boolean true on success or false on fialure.
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
				
					switch($action["attributes"]["type"])
					{
						case "list":
							$this->hierarchyList();
							$result = true;
							break;
						case "add":
							if($store && $parententryid && isset($action["name"]) && isset($action["type"])) {
								$result = $this->addFolder($store, $parententryid, $action["name"], $action["type"]);
							}
							break;
						case "modify":
							if($store && $entryid && isset($action["name"])) {
								$result = $this->modifyFolder($store, $entryid, $action["name"]);
							}
							break;
						case "delete":
							if($store && $parententryid && $entryid) {
								$result = $this->deleteFolder($store, $parententryid, $entryid);
							}
							break;
						case "emptyfolder":
							if($store && $entryid) {
								$result = $this->emptyFolder($store, $entryid);
							}
							break;
						case "copy":
							$destentryid = false;
							if(isset($action["destinationentryid"])) {
								$destentryid = hex2bin($action["destinationentryid"]);
							}
							$deststore = $store;
							if(isset($action["destinationstoreid"])) {
								$deststore = $GLOBALS['mapisession']->openMessageStore(hex2bin($action["destinationstoreid"]));
							}

							$moveFolder = false;
							if(isset($action["movefolder"])) {
								$moveFolder = $action["movefolder"];
							}

							if($store && $parententryid && $entryid && $destentryid && $deststore) {
								$result = $this->copyFolder($store, $parententryid, $entryid, $destentryid, $deststore, $moveFolder);
							}
							break;
						case "readflags":
							if($store && $entryid) {
								$result = $this->setReadFlags($store, $entryid);
							}
							break;
						
						case "closesharedfolder":
							$GLOBALS["mapisession"]->removeUserStore($action["username"]);

							$data = array();
							$data["attributes"] = array("type" => "closesharedfolder");
							$data["username"] = $action["username"];

							array_push($this->responseData["action"], $data);
							$GLOBALS["bus"]->addData($this->responseData);
							$result = true;
							break;
						
						case "opensharedfolder":
							$result = false;
							$store = $GLOBALS["mapisession"]->addUserStore($action["username"]);
							$openingItem = ($action["foldertype"] == "all") ? _("store") : _("folder");
							if ($store){
								$stores = Array($store);
								// Only open the archive store when a store is opened and not just a folder
								if($action['foldertype'] == 'all'){
									$stores = array_merge($stores, array_values( $GLOBALS["mapisession"]->getArchivedStores($GLOBALS["mapisession"]->resolveStrictUserName($action["username"])) ));
								}
								$data = $GLOBALS["operations"]->getHierarchyList($this->columns, HIERARCHY_GET_SPECIFIC, $stores);
								// TODO: Perhaps some more error handling could be done here
								if(is_array($data["store"]) && count($data["store"]) > 0) {
									array_push($this->responseData["action"], $data);
									$GLOBALS["bus"]->addData($this->responseData);	
									$result = true;
								} else {
									// access denied
									$data = array();
									$data["attributes"] = array("type" => "error");
									$data["error"] = array();
									$data["error"]["hresult"] = mapi_last_hresult();
									$data["error"]["hresult_name"] = get_mapi_error_name();
									$data["error"]["message"] = _("You have insufficient privileges to open this ") . $openingItem . ".";
									$data["error"]["username"] = $action["username"];
									array_push($this->responseData["action"], $data);								
									$GLOBALS["bus"]->addData($this->responseData);
								}
							} else {
								// store no exist
								$data["attributes"] = array("type" => "error");
								$data["error"] = array();
								$data["error"]["hresult"] = mapi_last_hresult();
								$data["error"]["hresult_name"] = get_mapi_error_name();
								if(mapi_last_hresult() == MAPI_E_NOT_FOUND) {
									$data["error"]["message"] = _("User could not be resolved") . ".";
								} else {
									$data["error"]["message"] = _("You have insufficient privileges to open this ") . $openingItem . ".";
								}
								$data["error"]["username"] = $action["username"];
								array_push($this->responseData["action"], $data);								
								$GLOBALS["bus"]->addData($this->responseData);
							}
							break;
						case "addtofavorite":
							if($store && $entryid && isset($action["favoritename"]) && isset($action["flags"])) {
								$favoritename = $action["favoritename"];
								$flags = $action["flags"];
								$result = $this->addToFavorite($store, $entryid, $favoritename, $flags);
							}
							break;
					}
				}
			}
			
			return $result;
		}
		
		/**
		 * Generates the hierarchy list. All folders and subfolders are added to response data.
		 */
		function hierarchyList()
		{
			$data = $GLOBALS["operations"]->getHierarchyList($this->columns);
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
		}
		
		/**
		 * Adds a folder to the hierarchylist.
		 * @param object $store Message Store Object.
		 * @param string $entryid entryid of the parent folder.
		 * @param string $name name of the new folder.
		 * @param string $type type of the folder (calendar, mail, ...).
		 * @return boolean true on success or false on failure.
		 */
		function addFolder($store, $entryid, $name, $type)
		{
			$props = array();
			$result = $GLOBALS["operations"]->createFolder($store, $entryid, $name, $type, $props);

			if($result && isset($props[PR_ENTRYID])) {
				$GLOBALS["bus"]->notify(bin2hex($props[PR_ENTRYID]), OBJECT_SAVE, $props);
				
				$storeprops = mapi_getprops($store, array(PR_ENTRYID));
				$props = array();
				$props[PR_ENTRYID] = $entryid;
				$props[PR_STORE_ENTRYID] = $storeprops[PR_ENTRYID];
				$GLOBALS["bus"]->notify(bin2hex($entryid), OBJECT_SAVE, $props);
			}
			
			return $result;
		}
		
		function addToFavorite($store, $entryid, $favoritename, $flags)
		{
			$result = false;

			$folder = mapi_msgstore_openentry($store, $entryid);
			if($folder)
				$result = mapi_favorite_add($GLOBALS["mapisession"]->getSession(), $folder, $favoritename, intval($flags));

			/**
			 * folders of public store can only be added in favorites folder
			 * so $store passed here will be always public store
			 */
			// open public store and find favorites folder entryid
			$storeProps = mapi_getprops($store, array(PR_IPM_FAVORITES_ENTRYID));
			$favoritesFolder = mapi_msgstore_openentry($store, $storeProps[PR_IPM_FAVORITES_ENTRYID]);

			$favoritesFolderProps = mapi_getprops($favoritesFolder, array(PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID));

			// notify favorites folder that folder has been added
			$GLOBALS["bus"]->notify(bin2hex($favoritesFolderProps[PR_ENTRYID]), OBJECT_SAVE, $favoritesFolderProps);

			return $result;
		}
		
		/**
		 * Modifies a folder off the hierarchylist.
		 * @param object $store Message Store Object.
		 * @param string $entryid entryid of the folder.
		 * @param string $name name of the folder.
		 * @return boolean true on success or false on failure.
		 */
		function modifyFolder($store, $entryid, $name)
		{
			$props = array();
			$result = $GLOBALS["operations"]->modifyFolder($store, $entryid, $name, $props);

			if($result && isset($props[PR_ENTRYID])) {
				$GLOBALS["bus"]->notify(bin2hex($props[PR_ENTRYID]), OBJECT_SAVE, $props);
			}
			
			return $result;
		}
		
		/**
		 * Deletes a folder in the hierarchylist.
		 * @param object $store Message Store Object.
		 * @param string $parententryid entryid of the parent folder.
		 * @param string $entryid entryid of the folder.
		 * @return boolean true on success or false on failure.
		 */
		function deleteFolder($store, $parententryid, $entryid)
		{
			$props = array();
			$result = $GLOBALS["operations"]->deleteFolder($store, $parententryid, $entryid, $props);

			if(isset($props[PR_ENTRYID])) {
				if($result) {
					$GLOBALS["bus"]->notify(bin2hex($parententryid), OBJECT_SAVE, $props);
					
					$props = array();
					$props[PR_PARENT_ENTRYID] = $parententryid;
					
					$storeprops = mapi_getprops($store, array(PR_ENTRYID, PR_IPM_WASTEBASKET_ENTRYID));
					$props[PR_STORE_ENTRYID] = $storeprops[PR_ENTRYID];
					$GLOBALS["bus"]->notify(bin2hex($parententryid), OBJECT_SAVE, $props);
					
					$props[PR_PARENT_ENTRYID] = $storeprops[PR_IPM_WASTEBASKET_ENTRYID];
					$GLOBALS["bus"]->notify(bin2hex($parententryid), OBJECT_SAVE, $props);
				}
			} else {
				$props[PR_ENTRYID] = $entryid;
				$props[PR_PARENT_ENTRYID] = $parententryid;
				
				if($result) {
					$storeprops = mapi_getprops($store, array(PR_ENTRYID, PR_IPM_FAVORITES_ENTRYID));
					$props[PR_STORE_ENTRYID] = $storeprops[PR_ENTRYID];
					$GLOBALS["bus"]->notify(bin2hex($parententryid), OBJECT_SAVE, $props);
					
					$GLOBALS["bus"]->notify(bin2hex($parententryid), OBJECT_DELETE, $props);

					// Notifying corresponding folder in 'Favorites'
					if (isset($storeprops[PR_IPM_FAVORITES_ENTRYID])){
						$folderEntryID = "00000001". substr(bin2hex($entryid), 8);
						$props[PR_ENTRYID] = hex2bin($folderEntryID);
						$props[PR_PARENT_ENTRYID] = $storeprops[PR_IPM_FAVORITES_ENTRYID];
						$GLOBALS["bus"]->notify(bin2hex($parententryid), OBJECT_DELETE, $props);
					}
				}
			}
			
			return $result;
		}
		
		/**
		 * Deletes all messages in a folder.
		 * @param object $store Message Store Object.
		 * @param string $entryid entryid of the folder.
		 * @return boolean true on success or false on failure.
		 */
		function emptyFolder($store, $entryid)
		{
			$props = array();
			$result = $GLOBALS["operations"]->emptyFolder($store, $entryid, $props);

			if($result && isset($props[PR_ENTRYID])) {
				$GLOBALS["bus"]->notify(bin2hex($props[PR_ENTRYID]), OBJECT_SAVE, $props);
				$GLOBALS["bus"]->notify(bin2hex($props[PR_ENTRYID]), TABLE_SAVE, $props);
			}
			
			return $result;
		}
		
		/**
		 * Copies of moves a folder in the hierarchylist.
		 * @param object $store Message Store Object.
		 * @param string $parententryid entryid of the parent folder.
		 * @param string $sourcefolderentryid entryid of the folder to be copied of moved.
		 * @param string $destfolderentryid entryid of the destination folder.
		 * @param string $action move or copy the folder.
		 * @return boolean true on success or false on failure.
		 */
		function copyFolder($store, $parententryid, $sourcefolderentryid, $destfolderentryid, $deststore, $moveFolder)
		{
			$props = array();
			$result = $GLOBALS["operations"]->copyFolder($store, $parententryid, $sourcefolderentryid, $destfolderentryid, $deststore, $moveFolder, $props);

			if($result) {
				if($moveFolder) {
					$GLOBALS["bus"]->notify(bin2hex($props[PR_ENTRYID]), OBJECT_SAVE, $props);
					// if move folder then refresh parent of source folder
					$sourcefolder = mapi_msgstore_openentry($store, $parententryid);
					$folderProps = mapi_folder_getprops($sourcefolder, array(PR_ENTRYID, PR_STORE_ENTRYID));
					$GLOBALS["bus"]->notify(bin2hex($folderProps[PR_ENTRYID]), OBJECT_SAVE, $folderProps);
				}

				// refresh destination folder for copy & move folder
				$folder = mapi_msgstore_openentry($deststore, $destfolderentryid);
				
				$hierarchyTable = mapi_folder_gethierarchytable($folder);
				mapi_table_sort($hierarchyTable, array(PR_DISPLAY_NAME => TABLE_SORT_ASCEND)); 
				
				$subfolders = mapi_table_queryallrows($hierarchyTable, array(PR_ENTRYID));
	
				if (is_array($subfolders)) {
					foreach($subfolders as $subfolder)
					{
						$folderObject = mapi_msgstore_openentry($deststore, $subfolder[PR_ENTRYID]); 
						$folderProps = mapi_folder_getprops($folderObject, array(PR_ENTRYID, PR_STORE_ENTRYID));
						$GLOBALS["bus"]->notify(bin2hex($subfolder[PR_ENTRYID]), OBJECT_SAVE, $folderProps);
					}
				}
				$folderProps = mapi_folder_getprops($folder, array(PR_ENTRYID, PR_STORE_ENTRYID));
				$GLOBALS["bus"]->notify(bin2hex($folderProps[PR_ENTRYID]), OBJECT_SAVE, $folderProps);
			}
			
			return $result;
		}
		
		/**
		 * Set all messages read.
		 * @param object $store Message Store Object.
		 * @param string $entryid entryid of the folder.
		 * @return boolean true on success or false on failure.
		 */
		function setReadFlags($store, $entryid)
		{
			$props = array();
			$result = $GLOBALS["operations"]->setReadFlags($store, $entryid, $props);

			if($result && isset($props[PR_ENTRYID])) {
				$GLOBALS["bus"]->notify(bin2hex($props[PR_ENTRYID]), OBJECT_SAVE, $props);
				$GLOBALS["bus"]->notify(bin2hex($props[PR_ENTRYID]), TABLE_SAVE, $props);
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

			$data = array();
			$data["attributes"] = array("type" => "folder");

			switch($event)
			{
				case OBJECT_SAVE:
				case TABLE_SAVE:
				case TABLE_DELETE:
					if(isset($props[PR_STORE_ENTRYID])) {
						$store = $GLOBALS["mapisession"]->openMessageStore($props[PR_STORE_ENTRYID]);
						
						$folder = false;
						if(isset($props[PR_PARENT_ENTRYID])) {
							$folder = mapi_msgstore_openentry($store, $props[PR_PARENT_ENTRYID]);
						} else if(isset($props[PR_ENTRYID])) {
							$folder = mapi_msgstore_openentry($store, $props[PR_ENTRYID]);
						}
						
						if($folder) {
							$folderProps = mapi_folder_getprops($folder, $this->columns);

							// If this folder belongs to Favorites folder,then change PARENT_ENTRYID manually.
							if (substr(bin2hex($folderProps[PR_ENTRYID]), 0, 8) == "00000001") {
								$storeProps = mapi_getprops($store, array(PR_IPM_FAVORITES_ENTRYID));

								if (isset($storeProps[PR_IPM_FAVORITES_ENTRYID])) {
									$favFolder = mapi_msgstore_openentry($store, $storeProps[PR_IPM_FAVORITES_ENTRYID]);
									$favHierarchyTable = mapi_folder_gethierarchytable($favFolder);

									$folders = mapi_table_queryallrows($favHierarchyTable, array(PR_DISPLAY_NAME), array(RES_PROPERTY,
																											array(  RELOP => RELOP_EQ,
																													ULPROPTAG => PR_ENTRYID,
																													VALUE => array (PR_ENTRYID => $folderProps[PR_ENTRYID])
																											)
																										)
																	);

									// Update folderProps to properties of folder which is under 'FAVORITES'
									$folderProps[PR_DISPLAY_NAME] = $folders[0][PR_DISPLAY_NAME];
									$folderProps[PR_PARENT_ENTRYID] = $storeProps[PR_IPM_FAVORITES_ENTRYID];
								}
							}
							
							$data["folder"] = $GLOBALS["operations"]->setFolder($folderProps);
						}
					}
					break;
				case OBJECT_DELETE:
					if(isset($props[PR_ENTRYID]) && isset($props[PR_PARENT_ENTRYID])) {
						$data["folder"] = array();
						$data["folder"]["folderdelete"] = 1;
						$data["folder"]["entryid"] = bin2hex($props[PR_ENTRYID]);
						$data["folder"]["parent_entryid"] = bin2hex($props[PR_PARENT_ENTRYID]);
					}
					break;
			}

			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
		}
	}
?>