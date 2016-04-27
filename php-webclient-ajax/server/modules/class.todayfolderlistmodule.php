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
	 * TodayFolderListModule extend ListModule
	 *
	 * @todo
	 * - Check the code at deleteFolder and at copyFolder. Looks the same.
	 */
	class TodayFolderListModule extends ListModule
	{
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param string $folderentryid Entryid of the folder. Data will be selected from this folder.
		 * @param array $data list of all actions.
		 */
		function TodayFolderListModule($id, $data)
		{
			$this->columns = array();
			$this->columns["entryid"] = PR_ENTRYID;
			$this->columns["store_entryid"] = PR_STORE_ENTRYID;
			$this->columns["parent_entryid"] = PR_PARENT_ENTRYID;
			$this->columns["display_name"] = PR_DISPLAY_NAME;
			$this->columns["container_class"] = PR_CONTAINER_CLASS;
			$this->columns["content_count"] = PR_CONTENT_COUNT;
			$this->columns["content_unread"] = PR_CONTENT_UNREAD;

			/**
			 * Copying selected folders entryids for notification
			 * $data = array(
			 * 			0 = array(
			 * 				"entryid" = array()
			 */
			foreach ($data as $key => $action){
				if (isset($action["selectedfolders"]) && isset($action["selectedfolders"]["entryids"])){
					if (is_array($action["selectedfolders"]["entryids"])) $data[$key]["entryid"] = $action["selectedfolders"]["entryids"];
				}
			}

			parent::ListModule($id, $data, array(OBJECT_SAVE, OBJECT_DELETE, TABLE_SAVE, TABLE_DELETE));
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
							$this->folderList($action);
							$result = true;
							break;
					}
				}
			}
			
			return $result;
		}
		
		/**
		 * Generates only the required folder's list with required properties
		 * @param object $action contains all request data
		 */
		function folderList($action)
		{
			$data = array();
			$store = $GLOBALS["mapisession"]->openMessageStore(hex2bin($action["store"]));
			$requiredFolders = $action["selectedfolders"]["entryids"];

			for ($j = 0; $j < count($requiredFolders); $j++) {
				$folder = mapi_msgstore_openentry($store, hex2bin($requiredFolders[$j]));
				$props = mapi_getprops($folder, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID, PR_DISPLAY_NAME, PR_CONTAINER_CLASS, PR_CONTENT_COUNT, PR_CONTENT_UNREAD));
				if(!isset($data["folder"]))
					$data["folder"] = array();
				array_push($data["folder"], $this->setFolderProps($props));
			}
			$data["attributes"] = array("type" => "list");
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
		}
		
		/**
		 * Function which sets the properties of folder to be retrieved.
		 * @param object $props mapi property object of a folder.
		 * @return object $data the object in formatted way.
		 */

		function setFolderProps($props){
			$data = array();
			if(isset($props)){
				$data["entryid"] = bin2hex($props[PR_ENTRYID]);
				$data["store_entryid"] = bin2hex($props[PR_STORE_ENTRYID]);
				$data["parent_entryid"] = bin2hex($props[PR_PARENT_ENTRYID]);
				$data["display_name"] = w2u($props[PR_DISPLAY_NAME]);
				$data["content_count"] = $props[PR_CONTENT_COUNT];
				$data["content_unread"] = $props[PR_CONTENT_UNREAD];
			}
			return $data;
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
			$entryid = $props[PR_ENTRYID];
			$actionType = "folder";

			/**
			 * At every notify we have todo same thing only entryid changes.
			 */
			switch($event)
			{
				case OBJECT_SAVE:
					$update = true;
					$entryid = $props[PR_ENTRYID];
					break;
				case TABLE_SAVE:
					$update = true;
					// new mail notification(INBOX) or saving a draft(DRAFTS)
					if (isset($props[PR_PARENT_ENTRYID])) {
						$entryid = $props[PR_PARENT_ENTRYID];
					} else {
						// user manually marks unread on folder context menu.(ANY FOLDER)
						$entryid = $props[PR_ENTRYID];
					}
					break;
				case TABLE_DELETE:
					$update = true;
					$entryid = $props[PR_PARENT_ENTRYID];
			 /**
			  * case OBJECT_DELETE:
			  * when a folder is deleted, hierarchy selects its parent folder in view at clientside.
			  */
			}

			if ($update == true){
				$data = array();
				$data["attributes"] = array("type" => $actionType);
				$data["folder"] = array();

				if (isset($props[PR_STORE_ENTRYID]) && isset($entryid)){
					$store = $GLOBALS["mapisession"]->openMessageStore($props[PR_STORE_ENTRYID]);
					$folder = mapi_msgstore_openentry($store, $entryid);
					$folderProps = mapi_getprops($folder, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID, PR_DISPLAY_NAME, PR_CONTAINER_CLASS, PR_CONTENT_COUNT, PR_CONTENT_UNREAD));

					array_push($data["folder"], $this->setFolderProps($folderProps));
					array_push($this->responseData["action"], $data);
				}
				$GLOBALS["bus"]->addData($this->responseData);
			}
		}
	}
?>
