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
	
	class TodayTaskListModule extends ListModule
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
		function TodayTaskListModule($id, $data)
		{
			parent::ListModule($id, $data, array(OBJECT_SAVE, TABLE_SAVE, TABLE_DELETE));
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

				// Restriction for task
				$taskRestriction = array(
									RES_PROPERTY,
									array(RELOP => RELOP_EQ,
										  ULPROPTAG => $this->properties["complete"],
										  VALUE => "0" 
									)
								);

				// Sort
				$this->parseSortOrder($action);

				// Create the data array, which will be send back to the client
				$data = array();
				$data["attributes"] = array("type" => "list");

				$firstSortColumn = reset(array_keys($this->sort)); // get first key of the sort array
				$data["sort"] = array();
				$data["sort"]["attributes"] = array();
				$data["sort"]["attributes"]["direction"] = (isset($this->sort[$firstSortColumn]) && $this->sort[$firstSortColumn] == TABLE_SORT_ASCEND) ? "asc" : "desc";
				$data["sort"]["_content"] = array_search($firstSortColumn, $this->properties);

				if(isset($this->selectedMessageId)) {
					$this->start = $GLOBALS["operations"]->getStartRow($store, $entryid, $this->selectedMessageId, $this->sort, false, $this->searchRestriction);
				}

				// Get the table and merge the arrays
				$data = array_merge($data, $GLOBALS["operations"]->getTable($store, $entryid, $this->properties, $this->sort, $this->start, false, $taskRestriction));

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
				
				$result = true;
			}

			return $result;
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
