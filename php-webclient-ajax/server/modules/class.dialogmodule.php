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
	* Dialog Module
	* 
	* This module is available in every dialog module and contains
	* some general functions (requests) that can be used when there is 
	* no other module available to do this.
	*/
	class DialogModule extends Module
	{
		/**
		* Constructor
		* @param int $id unique id.
		* @param array $data list of all actions.
		*/
		function DialogModule($id, $data)
		{
			parent::Module($id, $data, array());
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
						case "abhierarchy":
							$result = $this->retrieveAddressbookHierarchy($action);
							break;
					}
				}
			}
			return $result;
		}

		function retrieveAddressbookHierarchy($action)
		{
			$data = array();
			$data["attributes"] = array("type" => "abhierarchy");
			$data["callbackid"] = $action["callbackid"]; // send callbackid back

			$storeslist = false;
			// fix input data
			if (isset($action["contacts"])){
				if (isset($action["contacts"]["stores"]["store"]) && !is_array($action["contacts"]["stores"]["store"])){
					$action["contacts"]["stores"]["store"] = array($action["contacts"]["stores"]["store"]);
				}
				if (isset($action["contacts"]["stores"]["folder"]) && !is_array($action["contacts"]["stores"]["folder"])){
					$action["contacts"]["stores"]["folder"] = array($action["contacts"]["stores"]["folder"]);
				}
				$storeslist = $action["contacts"]["stores"];
			}
			
			$folders = $GLOBALS["operations"]->getAddressbookHierarchy($storeslist);

			$data = array_merge($data, array("folder"=>$folders));
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
			return true;
		}
	}
?>
