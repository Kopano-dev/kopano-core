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
	include_once('mapi/class.taskrecurrence.php');

	/**
	 * Task Module
	 */
	class TaskListModule extends ListModule
	{
		/**
		 * @var Array properties of task item that will be used to get data
		 */
		var $properties = null;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function TaskListModule($id, $data)
		{
			$this->tablecolumns = $GLOBALS["TableColumns"]->getTaskListTableColumns();

			/*
			*NOTE:
			*1)If value of visibility of any column in above table(i.e tablecolumns) is modifyed,
			*then the same is to be done in the table defined below(i.e insertcolumns) for the respective columns.
			*2)$name parameter of addInputColumn() should contain the value of id of corresponding header column.
			*/
			
			$userdata = $_SESSION["username"];
			$storeid = $data[0]["store"];
			$entryid = $data[0]["entryid"];
			
			$session = $GLOBALS["mapisession"]->getSession();			
			$store = mapi_openmsgstore($session, hex2bin($storeid));	
			$result = mapi_last_hresult();		
			$storeProps = mapi_getprops($store , array(PR_DISPLAY_NAME, PR_MDB_PROVIDER));			
			switch($storeProps[PR_MDB_PROVIDER]){
				  case ZARAFA_SERVICE_GUID:
					$readonly= "readonly";
				    break;
				  
				  case ZARAFA_STORE_PUBLIC_GUID:
				    $readonly = "";
				    break;
			}
			
			$username = mapi_zarafa_getuser_by_name($store,$userdata);			

			// create array to pass data to inputcolumns
			$columnData = array();
			$columnData["owner"] = array();
			$columnData["owner"]["readonly"] = $readonly;
			$columnData{"owner"}["title"] = w2u($username["fullname"]);

			$this->insertcolumns = $GLOBALS["TableColumns"]->getTaskListInputColumns($columnData);

			parent::ListModule($id, $data, array(OBJECT_SAVE, TABLE_SAVE, TABLE_DELETE));

			$this->start = 0;
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

			if(isset($action["props"])) {
				$props = $action["props"];
				
				if(!$store && !$parententryid) {
					if(isset($action["props"]["message_class"])) {
						$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
						$parententryid = $this->getDefaultFolderEntryID($store, $action["props"]["message_class"]);
					}
				}

				if ($store && $parententryid) {
					$messageProps = $GLOBALS["operations"]->saveTask($store, $parententryid, $action);

					if($messageProps) {
						$result = true;
						$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_SAVE, $messageProps);
					}
				}
			}

			return $result;
		}

		/**
		 * Function will create search restriction based on restriction array
		 * @param object $action the action data, sent by the client
		 *
		 * this function is normally used to add search restriction when getting data from listmodule
		 * but this is a special case in which it is used to hide completed tasks based on user settings
		 * in future if search is implemented in tasks then we have to combine that restriction with this one
		 */
		function parseSearchRestriction($action)
		{
			$this->searchRestriction = false;

			if(isset($action["restriction"])) {
				if(isset($action["restriction"]["start"])) {
					// Set start variable
					$this->start = (int) $action["restriction"]["start"];
				}
			}

			// apply restriction for hiding completed tasks
			$showCompletedTasks = $GLOBALS["settings"]->get("tasks/show_completed", "true");

			if($showCompletedTasks === "false") {
				$this->searchRestriction = Array(RES_PROPERTY,
												Array(
													RELOP => RELOP_EQ,
													ULPROPTAG => $this->properties["complete"],
													VALUE => false
												)
											);
			}
		}

		/**
		 * Function which deletes one or more items. Ite verifies if the delete
		 * is a action on a recurring item and if only one occurrence should be
		 * deleted in the recurrence.		  		 
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid entryid of the folder
		 * @param array $entryid list of entryids which will be deleted		 
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function delete($store, $parententryid, $entryids, $action)
		{
			$result = false;

			if($store && $parententryid) {
				$props = array();
				$props[PR_PARENT_ENTRYID] = $parententryid;
				$props[PR_ENTRYID] = $entryids;

				$storeprops = mapi_getprops($store, array(PR_ENTRYID));
				$props[PR_STORE_ENTRYID] = $storeprops[PR_ENTRYID];

				$result = $GLOBALS["operations"]->deleteTask($store, $parententryid, $entryids, $action);

				if (isset($result['occurrenceDeleted']) && $result['occurrenceDeleted']) {
					// Occurrence deleted, update item
					$GLOBALS["bus"]->notify($this->entryid, TABLE_SAVE, $props);
				} else {
					$GLOBALS["bus"]->notify($this->entryid, TABLE_DELETE, $props);
				}
			}

			return $result;
		}

		/**
		 * Function will generate property tags based on passed MAPIStore to use
		 * in module. These properties are regenerated for every request so stores
		 * residing on different servers will have proper values for property tags.
		 * @param MAPIStore $store store that should be used to generate property tags.
		 * @param Binary $entryid entryid of message/folder
		 * @param Array $action action data sent by client
		 */
		function generatePropertyTags($store, $entryid, $action)
		{
			$this->properties = $GLOBALS["properties"]->getTaskProperties($store);

			$this->sort = array();
			$this->sort[$this->properties["duedate"]] = TABLE_SORT_DESCEND;
		}
	}
?>