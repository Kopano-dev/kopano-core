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
	 * Module
	 * Superclass of every module. Many default functions are defined in this class.
	 */
	class Module
	{
		/**
		 * @var int unique id of the class.
		 */
		var $id;
		
		/**
		 * @var string entryid, which will be registered by the bus object.
		 */
		var $entryid;
		
		/**
		 * @var array list of all actions, which is received from the client.
		 */
		var $data;
		
		/**
		 * @var array list of the results, which is send to the client.
		 */
		var $responseData;
		
		/**
		 * @var array list of all the errors occurred.
		 */
		var $errors;
		
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function Module($id, $data, $events = false)
		{
			$this->id = $id;
			$this->data = $data;
			$this->errors = array();
			$this->responseData = array();
			$this->responseData["attributes"] = array("name" => strtolower(get_class($this)), "id" => $id);
			$this->responseData["action"] = array();

			if($events) {
				if(isset($GLOBALS["bus"])) {
					$this->entryid = $this->getEntryID();

					if($this->entryid) {
						if(is_array($this->entryid)) {
							foreach($this->entryid as $entryid)
							{
								$GLOBALS["bus"]->register($this, $entryid, $events, true);
							}
						} else {
							$GLOBALS["bus"]->register($this, $this->entryid, $events);
						}
					}
				}
			}
		}
		
		/**
		 * Executes all the actions in the $data variable.
		 * @return boolean true on success of false on fialure.
		 */
		function execute()
		{
			$result = false;
			
			// you must implement this function for each module
			
			return $result;
		}

		/**
		 * checks user store for over qouta restrictions, if qouta limit is exceeded then it will block some operations
		 * @param object $store MAPI Message Store Object
		 * @param array $action the action data, sent by the client
		 * @return string qouta name which is exceeded or a blank string
		 */
		function checkOverQoutaRestriction($store, $action)
		{
			$result = "";
			$actionType = "";

			switch($action["attributes"]["type"]) {
				/*case "acceptMeetingRequest":
				case "cancelMeetingRequest":
				case "declineMeetingRequest":
				case "cancelInvitation":
					$actionType = "send";
					break;*/
				case "save":
					if(isset($action["send"]) && $action["send"]) {
						$actionType = "send";
					} else {
						$actionType = "save";
					}
					break;
			}

			if(empty($actionType)) {
				// for other action types don't check for qouta limits
				return false;
			}

			$quotaDetails = $GLOBALS["operations"]->getQuotaDetails($store);

			if($quotaDetails !== false) {
				if($quotaDetails["quota_hard"] !== 0 && $quotaDetails["store_size"] > $quotaDetails["quota_hard"]) {
					if($actionType == "save" || $actionType == "send") {
						// hard quota is not handled yet
						//$result = "quota_hard";
					}
				}

				// if hard quota limit doesn't restrict the operation then check for soft qouta limit
				if($quotaDetails["quota_soft"] !== 0 && $quotaDetails["store_size"] > $quotaDetails["quota_soft"] && empty($result)) {
					if($actionType == "send") {
						$result = "quota_soft";
					}
				}
			}

			return $result;
		}

		/**
		 * sends a success or error message to client based on parameters passed
		 * @param boolean $success operation completed successfully or not
		 * @param string $message the error message which will be shown to client on any failure of operation
		 */
		function sendFeedback($success = false, $message = "")
		{
			if($success == true) {
				// Send success message to client
				$data = array();
				$data["attributes"] = array("type" => "success");

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
			} else {
				// send error message to client
				$data = array();
				$data["attributes"] = array("type" => "error");
				$data["error"] = array();
				$data["error"]["message"] = $message;
				
				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
			}
		}

		/**
		 * Function which returns an entryid, which is used to register this module. It
		 * searches in the class variable $data for a ParentEntryID or an EntryID.
		 * @return string an entryid if found, false if entryid not found.
		 */
		function getEntryID()
		{
			$entryid = false;
			foreach($this->data as $action)
			{
				if(isset($action["parententryid"]) && $action["parententryid"] != "") {
					$entryid = $action["parententryid"];
				} else if(isset($action["entryid"]) && $action["entryid"] != "") {
					$entryid = $action["entryid"];
				}
			}
			
			return $entryid;
		}
		
		/**
		 * Returns all the errors, which occurred.
		 * @return array An array of all the errors, which occurred.
		 */
		function getErrors()
		{
			return $this->errors;
		}
		
		/**
		 * Returns the response data.
		 * @return array An array of the response data. This data is send to the client.
		 */
		function getData()
		{
			return $this->responseData;
		}
		
		/**
		 * Sets the action data, which will be executed.
		 * @param array $data array of all the actions.
		 */
		function setData($data)
		{
			$this->data = $data;
		}
		
		/**
		 * Returns the id.
		 * @return int id.
		 */
		function getId()
		{
			return $this->id;
		}
		
		/**
		 * Function which resets the data and the response data class variable.
		 */
		function reset()
		{
			$this->data = array();
			
			$this->responseData = array();
			$this->responseData["attributes"] = array("name" => strtolower(get_class($this)), "id" => $this->id);
			$this->responseData["action"] = array();
		}
		
		/**
		 * Function which returns MAPI Message Store Object. It
		 * searches in the variable $action for a storeid.
		 * @param array $action the XML data retrieved from the client
		 * @return object MAPI Message Store Object, false if storeid is not found in the $action variable 
		 */
		function getActionStore($action)
		{
			$store = null;

			if(isset($action["store"]) && $action["store"] != "") {
				$store = $GLOBALS["mapisession"]->openMessageStore(hex2bin($action["store"]));
			}
			
			return $store;
		}
		
		/**
		 * Function which returns a parent entryid. It
		 * searches in the variable $action for a parententryid.
		 * @param array $action the XML data retrieved from the client
		 * @return object MAPI Message Store Object, false if parententryid is not found in the $action variable 
		 */
		function getActionParentEntryID($action)
		{
			$parententryid = false;
			
			if(isset($action["parententryid"]) && $action["parententryid"] != "") {
				$parententryid = hex2bin($action["parententryid"]);
			}
			
			return $parententryid;
		}
		
		/**
		 * Function which returns an entryid. It
		 * searches in the variable $action for an entryid.
		 * @param array $action the XML data retrieved from the client
		 * @return object MAPI Message Store Object, false if entryid is not found in the $action variable 
		 */
		function getActionEntryID($action)
		{
			$entryid = false;
			
			if(isset($action["entryid"]) && $action["entryid"] != "") {
				if(!is_array($action["entryid"])) {
					$entryid = hex2bin($action["entryid"]);
				} else if(is_array($action["entryid"])) {
					$entryid = array();
					foreach($action["entryid"] as $action_entryid)
					{
						array_push($entryid, hex2bin($action_entryid));
					}
				}
			}
			
			return $entryid;
		}
	}
?>
