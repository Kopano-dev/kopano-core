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
	 * Hierarchy Copy/Move Module
	 */ 
	class HierarchySelectModule extends Module
	{
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param string $folderentryid Entryid of the folder. Data will be selected from this folder.
		 * @param array $data list of all actions.
		 */
		function HierarchySelectModule($id, $data)
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
		
			parent::Module($id, $data);
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
					switch($action["attributes"]["type"])
					{
						case "list":
							$this->hierarchyList();
							$result = true;
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
			$data = $GLOBALS["operations"]->getHierarchyList($this->columns, HIERARCHY_GET_ALL);
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
		}
	}
?>
