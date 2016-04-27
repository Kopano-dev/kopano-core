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
	 * RestoreItemsList Module
	 */
	class RestoreItemsListModule extends ListModule
	{
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function RestoreItemsListModule($id, $data)
		{
			parent::ListModule($id, $data);// array(OBJECT_SAVE, TABLE_SAVE, TABLE_DELETE));
			$this->start = 0;
		}
		
		
		/**
		 * Executes all the actions in the $data variable.
		 * @return boolean true on success of false on failure.
		 */
		function execute()
		{
			$result = false;
			
			foreach($this->data as $action)
			{
				if(isset($action["attributes"]) && isset($action["attributes"]["type"])) {
					$store = $this->getActionStore($action);
					$parententryid = $this->getActionParentEntryID($action);
					$folderentryid = $this->getActionEntryID($action);
					//using regular expression we find action type and accordingly assigns the properties 
					$this->properties = (strpos($action["attributes"]["type"], "folder") !== false)? $GLOBALS["properties"]->getFolderProperties() : $GLOBALS["properties"]->getMailProperties();
					
					if(isset($action["attributes"]) &&($action["attributes"]["type"] != "list" && $action["attributes"]["type"] != "folderlist")){	
						$entrylist = array();
						if(isset($action["folders"])){
							//checks for the entryid tag when a client sends a response for restore or delete of folder. 
							if(isset($action["folders"]["folder"]) && isset($action["folders"]["folder"]["entryid"])) {
								array_push($entrylist, $action["folders"]["folder"]);
							}else {
									foreach($action["folders"]["folder"] as $key => $value) {
									array_push($entrylist, $action["folders"]["folder"][$key]);
								}
							}
						}else{
							if(isset($action["messages"]["message"]) && isset($action["messages"]["message"]["entryid"])) {
								array_push($entrylist, $action["messages"]["message"]);
							}else {
									foreach($action["messages"]["message"] as $key => $value) {
									array_push($entrylist, $action["messages"]["message"][$key]);
								}
							}
						}	
					}

					switch($action["attributes"]["type"])
					{
						case "list":
							$result = $this->messageList($store, $folderentryid, $action);
							break;
						
						case "deleteallmsg":
							$result = $this->deleteAll($store, $folderentryid, $action);
							break;

						case "restoreallmsg":
							$result = $this->restoreAll($store, $folderentryid, $action);
							break;
						
						case "deletemsg":
							$result = $this->deleteItems($store, $folderentryid, $entrylist, $action);
							break;

						case "restoremsg":
							$result = $this->restoreItems($store, $folderentryid, $entrylist, $action);
							break;

						case "folderlist":
							$result = $this->folderList($store, $folderentryid, $action);
							break;

						case "deleteallfolder":
							$result = $this->deleteAllFolder($store, $folderentryid, $action);
							break;

						case "restoreallfolder":
							$result = $this->restoreAllFolder($store, $folderentryid, $action);
							break;

						case "deletefolder":
							$result = $this->deleteFolder($store, $folderentryid, $entrylist, $action);
							break;

						case "restorefolder":
							$result = $this->restoreFolder($store, $folderentryid, $entrylist, $action);
							break;		
					}
				}
			}

			// check if $action["attributes"] is set and type is other then list/folderlist and 
			// return of action is true then call the new list of items to be sent to the client back.
			if(isset($action["attributes"])){
				
				switch($action["attributes"]["type"])
				{
					case "deletemsg":
					case "deleteallmsg":
					case "restoremsg":
					case "restoreallmsg":
						$result = $this->messageList($store, $folderentryid, $action);
						break;
					
					case "deletefolder":
					case "deleteallfolder":
					case "restorefolder":
					case "restoreallfolder":
						$result = $this->folderList($store, $folderentryid, $action);
						break;
				}
			}
			return $result;
		}

		/**
		 * Function to retrieve the list of items of particular folder's entry id
		 * @param object $store store object.
		 * @param binary $entryid entry id of that particular folder.
		 * @param object $action request data.
		 * return - true if result is successful.
		 */
		function messageList($store, $entryid, $action){
			
			//set the this->$sort variable.
			$this->parseSortOrder($action);

			$data = array();
			$data["attributes"] = array("type" => "list");
			$data["column"] = $this->tablecolumns;

			$folder = mapi_msgstore_openentry($store, $entryid);
			$table = mapi_folder_getcontentstable($folder, SHOW_SOFT_DELETES);
			
			//sort the table according to sort data
			if (is_array($this->sort) && count($this->sort)>0){
				mapi_table_sort($table, $this->sort);
			}
			$restoreitems = mapi_table_queryallrows($table, Array(PR_ENTRYID, PR_MESSAGE_CLASS, PR_SUBJECT, PR_SENDER_NAME, PR_DELETED_ON, PR_MESSAGE_SIZE ));
			$items = Array();
			foreach($restoreitems as $restoreitem)
			{
				$item = null;
				$item = Conversion::mapMAPI2XML($this->properties, $restoreitem);
				array_push($items,$item);
			} 
			$data["item"] = $items;

			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);

			return true;
		}
		
		/**
		 * Function to delete all items of particular folder's entry id
		 * @param object $store store object.
		 * @param binary $folderentryid entry id of that particular folder.
		 * @param object $action request data.
		 * return - true if result is successful.
		 */

		function deleteAll($store, $folderentryid, $action){
			$entrylist = Array();
			$folder = mapi_msgstore_openentry($store, $folderentryid);
			$table = mapi_folder_getcontentstable($folder, SHOW_SOFT_DELETES);
			$rows = mapi_table_queryallrows($table, array(PR_ENTRYID));
			for($i=0;$i<count($rows);$i++){
				array_push($entrylist, $rows[$i][PR_ENTRYID]);
			}
			mapi_folder_deletemessages($folder, $entrylist, DELETE_HARD_DELETE);
			$result = (mapi_last_hresult()==NOERROR);

			return $result;
		}

		/**
		 * Function to restore all items of particular folder's entry id
		 * @param object $store store object.
		 * @param binary $folderentryid entry id of that particular folder.
		 * @param object $action request data.
		 * return - true if result is successful.
		 */
		function restoreAll($store, $folderentryid, $action){
			$entryidlist = Array();
			$sfolder = mapi_msgstore_openentry($store, $folderentryid);
			$table = mapi_folder_getcontentstable($sfolder, SHOW_SOFT_DELETES);
			$rows = mapi_table_queryallrows($table, array(PR_ENTRYID));
			for($i=0;$i<count($rows);$i++){
				array_push($entryidlist, $rows[$i][PR_ENTRYID]);
			}
			mapi_folder_copymessages($sfolder, $entryidlist, $sfolder, MESSAGE_MOVE);
			$result = (mapi_last_hresult()==NOERROR);

			// as after moving the message/s the entryid gets changed, so need to notify about the folder
			// so that we can update the folder on parent page.
			$folderProps = mapi_getprops($sfolder, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID));
			$GLOBALS["bus"]->notify(bin2hex($folderentryid), TABLE_SAVE, $folderProps);
			
			return $result;
		}

		/**
		 * Function to delete selected items of particular folder
		 * @param object $store store object.
		 * @param binary $folderentryid entry id of that particular folder.
		 * @param array $entryidlist array of entry ids of messages to be deleted permanently
		 * @param object $action request data.
		 * return - true if result is successful.
		 */
		function deleteItems($store, $folderentryid, $entryidlist, $action){
			$binEntryIdList = Array();

			foreach($entryidlist as $key => $value){
				$binEntryIdList[] = hex2bin($entryidlist[$key]["entryid"]);
			}
			
			$sfolder = mapi_msgstore_openentry($store, $folderentryid);
			mapi_folder_deletemessages($sfolder, $binEntryIdList, DELETE_HARD_DELETE);
			$result = (mapi_last_hresult()==NOERROR);

			return $result;
		}
		
		/**
		 * Function to restore selected folder
		 * @param object $store store object.
		 * @param binary $folderentryid entry id of that particular folder.
		 * @param array $entryidlist array of entry ids of messages to be deleted permanently
		 * @param object $action request data.
		 * return - true if result is successful.
		 */
		function restoreItems($store, $folderentryid, $entryidlist, $action){
			$binEntryIdList = Array();
			
			foreach($entryidlist as $key => $value){
				$binEntryIdList[] = hex2bin($entryidlist[$key]["entryid"]);
			}

			$sfolder = mapi_msgstore_openentry($store, $folderentryid);
			mapi_folder_copymessages($sfolder, $binEntryIdList, $sfolder, MESSAGE_MOVE);
			$result = (mapi_last_hresult()==NOERROR);
			
			// as after moving the message/s the entryid gets changed, so need to notify about the folder
			// so that we can update the folder on parent page.
			$folderProps = mapi_getprops($sfolder, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID));
			$GLOBALS["bus"]->notify(bin2hex($folderentryid), TABLE_SAVE, $folderProps);

			return $result;
		}

		/**
		 * Function to retrieve the list of folders of particular folder's entry id
		 * @param object $store store object.
		 * @param binary $entryid entry id of that particular folder.
		 * @param object $action request data.
		 * return - true if result is successful.
		 */
		function folderList($store, $entryid, $action){	

			//set the this->$sort variable.
			$this->parseSortOrder($action);

			$data = array();
			$data["attributes"] = array("type" => "folderlist");
			$data["column"] = $this->tablecolumns;
			
			$folder = mapi_msgstore_openentry($store, $entryid);
			$table = mapi_folder_gethierarchytable($folder, SHOW_SOFT_DELETES);			
			
			//sort the table according to sort data
			if (is_array($this->sort) && count($this->sort)>0){
				mapi_table_sort($table, $this->sort);
			}
			$restoreitems = mapi_table_queryallrows($table, Array(PR_ENTRYID, PR_DISPLAY_NAME, PR_DELETED_ON, PR_CONTENT_COUNT));
			$items = Array();
			foreach($restoreitems as $restoreitem){
				$item = Conversion::mapMAPI2XML($this->properties, $restoreitem);
				array_push($items,$item);
			} 
			$data["item"] = $items;
	
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);

			return true;
		}	
	
		/**
		 * Function to delete all folders of particular
		 * @param object $store store object.
		 * @param binary $folderentryid entry id of that particular folder.
		 * @param object $action request data.
		 * return - true if result is successful.
		 */
		function deleteAllFolder($store, $folderentryid, $action){
			$folder = mapi_msgstore_openentry($store, $folderentryid);
			$table = mapi_folder_gethierarchytable($folder, SHOW_SOFT_DELETES);
			$rows = mapi_table_queryallrows($table, array(PR_ENTRYID));
			for($i=0;$i<count($rows);$i++){
				mapi_folder_deletefolder($folder, $rows[$i][PR_ENTRYID], DEL_FOLDERS | DEL_MESSAGES | DELETE_HARD_DELETE);
				$result = (mapi_last_hresult()==NOERROR);
				if(!$result){
					break;
				}
			}

			return $result;
		}

		/**
		 * Function to restore all folders of particular
		 * @param object $store store object.
		 * @param binary $folderentryid entry id of that particular folder.
		 * @param object $action request data.
		 * return - true if result is successful.
		 */
		function restoreAllFolder($store, $folderentryid, $action){
			$sfolder = mapi_msgstore_openentry($store, $folderentryid);
			$table = mapi_folder_gethierarchytable($sfolder, SHOW_SOFT_DELETES);
			$rows = mapi_table_queryallrows($table, array(PR_ENTRYID, PR_DISPLAY_NAME));	
			for($i=0;$i<count($rows);$i++){
				//checks for conflicting folder name before restoring the selected folder,
				//if names conflict a postfix of integer is add to folder name else nochange. 
				$foldername = $GLOBALS["operations"]->checkFolderNameConflict($store, $sfolder, $rows[$i][PR_DISPLAY_NAME]);
				mapi_folder_copyfolder($sfolder, $rows[$i][PR_ENTRYID], $sfolder, $foldername, FOLDER_MOVE);	
				$result = (mapi_last_hresult()==NOERROR);
				if(!$result){
					break;
				}			
			}
			
			// as after moving the folder/s the entryid gets changed, so need to notify about the folder
			// so that we can update the folder on parent page.
			$folderProps = mapi_getprops($sfolder, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID));
			$GLOBALS["bus"]->notify(bin2hex($folderentryid), TABLE_SAVE, $folderProps);
			
			return $result;
		}

		/**
		 * Function to delete selected folders
		 * @param object $store store object.
		 * @param binary $folderentryid entry id of that particular folder.
		 * @param array $entryidlist array of entry ids of folder to be deleted permanently
		 * @param object $action request data.
		 * return - true if result is successful.
		 */
		function deleteFolder($store, $folderentryid, $entryidlist, $action){
			$binEntryIdList = Array();
			
			foreach($entryidlist as $key => $value){
				$binEntryIdList[] = hex2bin($entryidlist[$key]["entryid"]);
			}
			
			$sfolder = mapi_msgstore_openentry($store, $folderentryid);

			foreach ($binEntryIdList as $key=>$folderlist){
				mapi_folder_deletefolder($sfolder, $folderlist, DEL_FOLDERS | DEL_MESSAGES | DELETE_HARD_DELETE);
				$result = (mapi_last_hresult()==NOERROR);
				if(!$result){
					break;
				}
			}
			
			return $result;
		}
		
		/**
		 * Function to restore selected items of particular folder
		 * @param object $store store object.
		 * @param binary $folderentryid entry id of that particular folder.
		 * @param array $entryidlist array of entry ids of folder to be deleted permanently
		 * @param object $action request data.
		 * return - true if result is successful.
		 */
		function restoreFolder($store, $folderentryid, $entryidlist, $action){	
			$binEntryIdList = Array();
			$displayNameList = Array();
			//here the entryids and displaynames are stored in seprate arrays so 
			//that folders display name can be checked for conflicting names which restoring folders. 
			foreach($entryidlist as $key => $value){
				$binEntryIdList[] = hex2bin($entryidlist[$key]["entryid"]);
				$displayNameList[] = u2w($entryidlist[$key]["display_name"]);
			}
			
			$sfolder = mapi_msgstore_openentry($store, $folderentryid);
			for($entryindex = 0;$entryindex < count($binEntryIdList); $entryindex++){
				mapi_folder_copyfolder($sfolder, $binEntryIdList[$entryindex], $sfolder, $displayNameList[$entryindex], FOLDER_MOVE);
				//here we check mapi errors for conflicting of names when folder/s is restored. 
				if(mapi_last_hresult()==MAPI_E_COLLISION){
					$foldername = $GLOBALS["operations"]->checkFolderNameConflict($store, $sfolder, $displayNameList[$entryindex]);
					mapi_folder_copyfolder($sfolder, $binEntryIdList[$entryindex], $sfolder, $foldername, FOLDER_MOVE);
					$result = (mapi_last_hresult()==NOERROR);
					if(!$result){
						break;
					}
				}			
			}
			
			// as after moving the folder's the entryid gets changed, so need to notify about the folder
			// so that we can update the folder on parent page.
			$folderProps = mapi_getprops($sfolder, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID));
			$GLOBALS["bus"]->notify(bin2hex($folderentryid), TABLE_SAVE, $folderProps);

			return $result;
		}
	}
?>
