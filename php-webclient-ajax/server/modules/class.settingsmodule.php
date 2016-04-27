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
	* Settings Module
	*/
	class SettingsModule extends Module
	{
		/**
		* Constructor
		* @param int $id unique id.
		* @param array $data list of all actions.
		*/
		function SettingsModule($id, $data)
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
						case "retrieveAll":
							$this->retrieveAll($action["attributes"]["type"]);
							$result = true;
							break;
						case "set":
							if (isset($action["path"]) && isset($action["value"])){
								$this->set($action["path"], $action["value"]);
								$result = true;
							}else{
								$result = false;
							}
							break;
						case "delete":
							if (isset($action["path"])){
								$this->deleteSetting($action);
								$result = true;
							}else{
								$result = false;
							}
							break;
						case "convert":
							if (isset($action["html"])&&isset($action["callback"])){
								$this->convertHTML2Text($action);
								$result = true;
							}else{
								$result = false;
							}
							break;
					}
				}
			}
			return $result;
		}
		
		function retrieveAll($type)
		{
			$data = $GLOBALS['settings']->get();
			$data["attributes"] = array("type" => $type);
			
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
		}
		
		function set($path, $value)
		{
			$GLOBALS['settings']->set($path, $value);
		}

		function deleteSetting($action)
		{
			$GLOBALS['settings']->delete($action["path"]);
		}
		
		function convertHTML2Text($action)
		{
			$filter = new filter();
			
			$data = array();
			$data["text"] = $filter->html2text($action["html"]);
			$data["callback"] = $action["callback"];
			$data["attributes"] = array("type" => $action["attributes"]["type"]);
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
		}
	}
?>
