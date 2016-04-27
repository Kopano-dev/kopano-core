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
	 * Contact Module
	 */
	class ContactListModule extends ListModule
	{
		/**
		 * @var Array properties of contact item that will be used to get data
		 */
		var $properties = null;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function ContactListModule($id, $data)
		{
			$this->tablecolumns = $GLOBALS["TableColumns"]->getContactListTableColumns();

			parent::ListModule($id, $data, array(OBJECT_SAVE, TABLE_SAVE, TABLE_DELETE));

			$this->start = 0;
		}

		/**
		 * Executes all the actions in the $data variable.
		 * @return boolean true on success of false on fialure.
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

					$this->generatePropertyTags($store, $entryid, $action);

					switch($action["attributes"]["type"])
					{
						case "list":
							$this->getDelegateFolderInfo($store);
							if ($this->searchActive) {
								// if search is active then send results from search folder
								$result = $this->search($store, $entryid, $action);
							} else {
								$result = $this->messageList($store, $entryid, $action);
							}
							break;
						case "search":
							$result = $this->search($store, $entryid, $action);
							break;
						case "updatesearch":
							$result = $this->updatesearch($store, $entryid, $action);
							break;
						case "stopsearch":
							$result = $this->stopSearch($store, $entryid, $action);
							break;
						case "save":
							$result = $this->save($store, $parententryid, $action);
							break;
						case "read_flag":
							$result = $this->setReadFlag($store, $entryid, $action);
							break;
						case "delete":
							$result = $this->delete($store, $parententryid, $entryid, $action);
							break;
						case "copy":
							$result = $this->copy($store, $parententryid, $entryid, $action);
							break;
						case "convert_contact":
							$result = $this->getContactData($store, $entryid, $action);
							break;
					}
				}
			}

			return $result;
		}


		/**
		 * Function which get data of selected contacts to send as response
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure 
		 */
		function getContactData($store, $parententryid, $action)
		{
			$result = false;
			if($store){
				if(isset($action["messages"]["message"]) && is_array($action["messages"]["message"])){

					if (!isset($action["messages"]["message"][0])){
						$msg = array($action["messages"]["message"]);
					}else{
						$msg = $action["messages"]["message"];
					}

					$data = array();
					$data["attributes"] = array("type" => "convert_contact");
					$data["item"] = array();
					$data["targetfolder"] = $action["targetfolder"];
					$data["targetfolderentryid"] = $action["parententryid"];

					foreach($msg as $messageItem){
						$items = array();
						$message = mapi_msgstore_openentry($store, hex2bin($messageItem['id']));
						// check the message type to get respective properties
						if($messageItem['type'] == DistList){
							$props = mapi_getprops($message, array(PR_DISPLAY_NAME, PR_MESSAGE_CLASS, PR_BODY, PR_OBJECT_TYPE));

							$memberItem = $GLOBALS["operations"]->expandDistributionList($store, hex2bin($messageItem['id']), false, $GLOBALS["properties"]->getContactABProperties(), $GLOBALS["properties"]->getAddressBookProperties());
							
							$items["dl_name"] = w2u($props[PR_DISPLAY_NAME]);
							$items["message_class"] = w2u($props[PR_MESSAGE_CLASS]);
							$items["body"] = w2u($props[PR_BODY]);
							$items["objecttype"] = $props[PR_OBJECT_TYPE];
							$items["addrtype"] = "ZARAFA";
							$items["members"] = array("member"=>$memberItem);
						}else{
							$items = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);
						}
						array_push($data["item"], $items);
					}

					array_push($this->responseData["action"], $data);
					$GLOBALS["bus"]->addData($this->responseData);
					$result = true;
				}
			}
			return $result;
		}

		/**
		 * Function which retrieves a list of contacts in a contact folder.
		 * search restriction if present then it will overwrite character restriction
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the folder
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function messageList($store, $entryid, $action)
		{
			$result = false;
			$this->searchFolderList = false; // Set to indicate this is not the search result, but a normal folder content

			// When it is a searchfolder, we want the folder name column
			if($this->searchActive) {
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "parent_entryid", "visible", true);
			} else {
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "parent_entryid", "visible", false);
			}

			if(isset($action["data_retrieval"]) && $action["data_retrieval"] == "normal") {
				// Retrieve the data for a normal table view with rows
				// Use the parent class "messageList" method
				$result = parent::messageList($store, $entryid, $action);
			} else { 
				// Retrieve the data for the address cards view
				if($store && $entryid) {
					$character = "a";
					if(isset($action["restriction"])) {
						if(isset($action["restriction"]["character"])) {
							$character = $action["restriction"]["character"];
						}
						if(isset($action["restriction"]["start"])) {
							// Set start variable
							$this->start = (int) $action["restriction"]["start"];
						}
					}

					$restriction = array();
					if($character != "...") {
						switch ($character){
							case "123":
								$restriction = array(
												RES_AND,
												array(
													array(
														RES_PROPERTY,
														array(RELOP => RELOP_GE,
															ULPROPTAG => $this->properties["fileas"],
															VALUE => array(
																$this->properties["fileas"] => "0" 
															)
														)
													),
													array(
														RES_PROPERTY,
														array(RELOP => RELOP_LE,
															ULPROPTAG => $this->properties["fileas"],
															VALUE => array(
																$this->properties["fileas"] => "9"
															)
														)
													)
												)
											);
								break;
							case "z":
								$restriction = array(
													RES_PROPERTY,
													array(RELOP => RELOP_GE,
														ULPROPTAG => $this->properties["fileas"],
														VALUE => array(
															$this->properties["fileas"] => "z" 
														)
													)
												);
								break;
							default:
								$restriction = array(
												RES_AND,
												array(
													array(
														RES_PROPERTY,
														array(
															RELOP => RELOP_GE,
															ULPROPTAG => $this->properties["fileas"],
															VALUE => array(
																$this->properties["fileas"] => $character
															)
														)
													),
													array(
														RES_PROPERTY,
														array(
															RELOP => RELOP_LT,
															ULPROPTAG => $this->properties["fileas"],
															VALUE => array(
																$this->properties["fileas"] => chr(ord($character)+1)
															)
														)
													)
												)
											);
								break;
						}
					}

					// overwrite character restriction if search restriction exists
					$this->parseSearchRestriction($action);
					if($this->searchRestriction != false) {
						$restriction = $this->searchRestriction;
					}

					$sort = array($this->properties["fileas"]=>TABLE_SORT_ASCEND);

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
					$items = array_merge($data, $GLOBALS["operations"]->getTable($store, $entryid, $this->properties, $sort, $this->start, false, $restriction));
					
					for($i=0; $i<count($items["item"]); $i++){
						// Disable private items
						$items["item"][$i] = $this->disablePrivateItem($items["item"][$i]);
					}
					$data = array_merge($data, $items);

					array_push($this->responseData["action"], $data);
					$GLOBALS["bus"]->addData($this->responseData);

					$result = true;
				} 
			}
			
			return $result;
		}

		/**
		 *	Function will create search restriction based on restriction array
		 *	@param object $action the action data, sent by the client
		 */
		function parseSearchRestriction($action)
		{
			if(isset($action["restriction"])) {
				if(isset($action["restriction"]["selectedmessageid"])) {
					$this->selectedMessageId = hex2bin($action["restriction"]["selectedmessageid"]);
				} elseif(isset($action["restriction"]["start"])) {
					// Set start variable
					$this->start = (int) $action["restriction"]["start"];
					unset($this->selectedMessageId);
				}

				if(isset($action["restriction"]["search"])) {
					// if the restriction is a associative array, it means that only one property is requested
					// so we must build an non-associative array arround it
					if (is_assoc_array($action["restriction"]["search"])){
						$action["restriction"]["search"] = Array($action["restriction"]["search"]);
					}

					// create restriction array
					$res_or = Array();

					foreach($action["restriction"]["search"] as $i => $search) {
						$prop = false;

						// convert search property to MAPI property
						switch($search["property"]) {
							case "full_name":
								$prop = PR_DISPLAY_NAME;
								break;
							case "prefix":
								$prop = PR_DISPLAY_NAME_PREFIX;
								break;
							case "suffix":
								$prop = PR_GENERATION;
								break;
							case "file_as":
								$prop = $this->properties["fileas"];
								break;
							case "company_name":
								$prop = PR_COMPANY_NAME;
								break;
							case "email_address_display_name_1":
								$prop = $this->properties["email_address_display_name_1"];
								break;
							case "email_address_display_name_2":
								$prop = $this->properties["email_address_display_name_2"];
								break;
							case "email_address_display_name_3":
								$prop = $this->properties["email_address_display_name_3"];
								break;
							case "home_telephone_number":
								$prop = PR_HOME_TELEPHONE_NUMBER;
								break;
							case "cellular_telephone_number":
								$prop = PR_CELLULAR_TELEPHONE_NUMBER;
								break;
							case "office_telephone_number":
								$prop = PR_OFFICE_TELEPHONE_NUMBER;
								break;
							case "business_fax_number":
								$prop = PR_BUSINESS_FAX_NUMBER;
								break;
							case "business_address":
								$prop = $this->properties["business_address"];
								break;
							case "home_address":
								$prop = $this->properties["home_address"];
								break;
							case "other_address":
								$prop = $this->properties["other_address"];
								break;
						}

						// build restriction
						if($prop !== false) {
							array_push($res_or, Array(RES_AND,
													Array(
														Array(RES_EXIST, // check first if the property exists
															Array(
																ULPROPTAG => $prop
															)
														),
														Array(RES_CONTENT,
															Array(
																FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
																ULPROPTAG => $prop,
																VALUE => u2w($search["value"])
															)
														)
													)
												)
										);
						}
					}

					if(count($res_or) > 0) {
						$this->searchRestriction = Array(RES_OR, $res_or);
					} else {
						$this->searchRestriction = false;
					}
				} else {
					$this->searchRestriction = false;
				}
			}
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
			$this->properties = $GLOBALS["properties"]->getContactProperties($store);

			$this->sort = array();
			$this->sort[$this->properties["fileas"]] = TABLE_SORT_ASCEND;
		}
	}
?>
