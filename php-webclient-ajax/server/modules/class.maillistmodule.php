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
	 * Mail Module
	 */
	class MailListModule extends ListModule
	{
		/**
		 * @var array the special columns for a normal mail folder.
		 */
		var $mailcolumns;
		
		/**
		 * @var array the special columns for a sent mail folder.
		 */
		var $sentcolumns;

		/**
		 * @var Array properties of mail item that will be used to get data
		 */
		var $properties = null;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function MailListModule($id, $data)
		{
			// Default Columns
			$this->tablecolumns = $GLOBALS["TableColumns"]->getMailListTableColumns();

			parent::ListModule($id, $data, array(OBJECT_SAVE, TABLE_SAVE, TABLE_DELETE));

			$this->sort = array();
		}

		/**
		 * Function which returns an entryid, which is used to register this module. 
		 * The maillistmodule has a special feature that it supports an empty store 
		 * entryid and folder entryid, which means the default inbox of your default 
		 * store. This is used when opening the initial view when loggin on to the 
		 * webaccess.
		 * It first tries to use the normal Module::getEntryID method, but if that 
		 * returns false it will look for the default values.
		 * @return string an entryid if found, false if entryid not found.
		 */
		function getEntryID(){
			$entryid = parent::getEntryID();
			if(!$entryid) {
				$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
				$inbox = mapi_msgstore_getreceivefolder($store);
				$props = mapi_getprops($inbox, array(PR_ENTRYID));
				$entryid = bin2hex($props[PR_ENTRYID]);
			}
			return $entryid;
		}

		/**
		 * Executes all the actions in the $data variable.
		 * @return boolean true on success of false on fialure.
		 */
		function execute()
		{
			$result = false;
			$GLOBALS['PluginManager']->triggerHook("server.module.maillistmodule.execute.before", array('moduleObject' =>& $this));

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
								$result = $this->search($store, $entryid, $action);
							} else {
								/**
								 * The maillist module has a special feature that it supports an empty
								 * store entryid and folder entryid, which means the default inbox
								 * of your default store. This is used when opening the initial view
								 * when logging on to the webaccess.
								 * this feature is only needed when action type is list
								 */
								if(!$store) {
									$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
								}

								if(!$entryid) {
									// The maillistmodule implements its own getEntryID function, so 
									// $this->entryid property should contain the correct entryid.
									$entryid = hex2bin($this->entryid);
								}

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
						case "cancelInvitation":
							$GLOBALS["operations"]->cancelInvitation($store, $entryid);
							break;
						// will be called when a mail is converted into task  
						case "createTask":
							$result = $this->CreateTaskFromMail($store, $entryid);
							break;
						case "convert_meeting":
							$result = $this->getMeetingData($store, $entryid, $action);
							break;
						case "acceptMeeting":
							if (isset($action["entryid"])) {
								$message = $GLOBALS["operations"]->openMessage($store, hex2bin($action["entryid"]));
								/**
								 * Get message class from original message. This can be changed to 
								 * IPM.Appointment if the item is a Meeting Request in the maillist. 
								 * After Accepting/Declining the message is moved and changed.
								 */
								$originalMessageProps = mapi_getprops($message, array(PR_MESSAGE_CLASS));
								$req = new Meetingrequest($store, $message, $GLOBALS["mapisession"]->getSession(), ENABLE_DIRECT_BOOKING);

								$basedate = (isset($action['basedate']) ? $action['basedate'] : false);
								// sendResponse flag if it is set then send the mail response to the organzer.
								$sendResponse = true;
								if(isset($action["noResponse"]) && $action["noResponse"] == "true") {
									$sendResponse = false;
								}
								// We are accepting MR from maillist so set delete the actual mail flag.
								$delete = (stristr($originalMessageProps[PR_MESSAGE_CLASS], 'IPM.Schedule.Meeting') !== false) ? true : false;

								$req->doAccept(false, $sendResponse, $delete, false, false, false, true, $store, $basedate);
							}
							break;
					}
				}
			}
			$GLOBALS['PluginManager']->triggerHook("server.module.maillistmodule.execute.after", array('moduleObject' =>& $this));
			
			return $result;
		}

		/**
		 * Function which get data of selected meeting item to send as response
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure 
		 */
		function getMeetingData($store, $parententryid, $action)
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
					$data["attributes"] = array("type" => "convert_meeting");
					$data["item"] = array();
					$data["targetfolder"] = $action["targetfolder"];
					$data["targetfolderentryid"] = $action["parententryid"];

					foreach($msg as $messageItem){
						$items = array();
						$message = mapi_msgstore_openentry($store, hex2bin($messageItem['id']));
						$items = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);
						array_push($data["item"], $items);
					}

					array_push($this->responseData["action"], $data);
					$GLOBALS["bus"]->addData($this->responseData);
					$result = true;
				}
			}
			return $result;
		}

		function parseSearchRestriction($action)
		{
			// TODO: let javascript generate the MAPI restriction just as with the rules

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

					$searchprops = Array();
					foreach($action["restriction"]["search"] as $i=>$search){
						// Just take the last search term since they are all the same
						$searchterms = $search["value"];
						
						$prop = false;
						// convert search property to MAPI property
						switch($search["property"]){
							case "subject":
								$prop = PR_SUBJECT;
								break;
							case "body":
								$prop = PR_BODY;
								break;
							case "to":
								$prop = PR_DISPLAY_TO;
								break;
							case "cc":
								$prop = PR_DISPLAY_CC;
								break;
							case "sender_name":
								$prop = PR_SENDER_NAME;
								break;
							case "sender_email":
								$prop = PR_SENDER_EMAIL_ADDRESS;
								break;
							case "sent_representing_name":
								$prop = PR_SENT_REPRESENTING_NAME;
								break;
							case "sent_representing_email":
								$prop = PR_SENT_REPRESENTING_EMAIL_ADDRESS;
								break;
						}

						if($prop)
							array_push($searchprops, $prop);
					}

					$searchterms = preg_split("/[\.\/\~\,\ \@]+/", $searchterms);

					$res_and = array();
					foreach($searchterms as $term) {
						if(empty($term)) {
							continue;
						}

						$res_or = array();
						
						foreach($searchprops as $property) {
							array_push($res_or, 
								Array(RES_CONTENT,
									Array(
										FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
										ULPROPTAG=>$property,
										VALUE => utf8_to_windows1252($term)
									)
								)
							);
						}
						
						array_push($res_and, Array(RES_OR, $res_or));
					}
						
					if (count($res_and)>0){
						$this->searchRestriction = Array(RES_AND,$res_and);
					}else{
						$this->searchRestriction = false;
					}
				}else{
					$this->searchRestriction = false;
				}
			}
		}


		/**
		 * Function which retrieves a list of messages in a folder. It verifies if
		 * the given entryid is a default send folder, like outbox, sentmail of draft 
		 * folder. If so, different columns should be visible (to and submit time).		 
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the folder
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function messageList($store, $entryid, $action)
		{
			$this->searchFolderList = false; // Set to indicate this is not the search result, but a normal folder content
			// begin column changes

			// When it is a searchfolder we want the original folder column
			if ($this->searchActive){
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "parent_entryid", "visible", true);
			}else{
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "parent_entryid", "visible", false);
			}

			// When this folder is the Outbox, Sent Mail or Drafts folder we want different default columns
			$props = mapi_getprops($store, array(PR_IPM_OUTBOX_ENTRYID, PR_IPM_SENTMAIL_ENTRYID));
			$rootcontainer = mapi_msgstore_openentry($store);
			$props = array_merge($props, mapi_getprops($rootcontainer, array(PR_IPM_DRAFTS_ENTRYID)));
			
			$inbox = mapi_msgstore_getreceivefolder($store);
			$inboxprops = mapi_getprops($inbox, array(PR_ENTRYID));
			$inboxentryid = $inboxprops[PR_ENTRYID];
			
			if(array_search($entryid, $props) !== false) {
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "sent_representing_name", "visible", false);
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "message_delivery_time", "visible", false);
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "display_to", "visible", true);
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "client_submit_time", "visible", true);

				$this->sort[$this->properties["client_submit_time"]] = TABLE_SORT_DESCEND;
			} else {
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "display_to", "visible", false);
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "client_submit_time", "visible", false);
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "sent_representing_name", "visible", true);
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "message_delivery_time", "visible", true);

				$this->sort[$this->properties["message_delivery_time"]] = TABLE_SORT_DESCEND;
			}
			// end column changes

			// Restriction
			$this->parseSearchRestriction($action);

			// Sort
			$this->parseSortOrder($action, null, true);
				
			// List columns visible
			$GLOBALS["TableColumns"]->parseVisibleColumns($this->tablecolumns, $action);
				
			// Create the data array, which will be send back to the client
			$data = array();
			$data["attributes"] = array("type" => "list");
			$data["column"] = $this->tablecolumns;

			$firstSortColumn = reset(array_keys($this->sort)); // get first key of the sort array
			$data["sort"] = array();
			$data["sort"]["attributes"] = array();
			$data["sort"]["attributes"]["direction"] = (isset($this->sort[$firstSortColumn]) && $this->sort[$firstSortColumn] == TABLE_SORT_ASCEND) ? "asc":"desc";

			/**
			 * If MV_INSTANCE flag is set then remove it,
			 * It is added in parseSortOrder function to allow multiple instances.
			 */
			$data["sort"]["_content"] = array_search($firstSortColumn &~ MV_INSTANCE, $this->properties);
				
            // Return some information about the folder. The entryid and storeid may be unknown to the client if it passed
            // an empty storeid and folder entryid.
			$folder = mapi_msgstore_openentry($store, $entryid);
			$folderprops = mapi_getprops($folder, array(PR_DISPLAY_NAME, PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID));
			
			//check if the folder is not the root folder.
			if($entryid != $folderprops[PR_ENTRYID]){
				$data["folder_title"] = array();
				$data["folder_title"]["_content"] = w2u($folderprops[PR_DISPLAY_NAME]);
			}
			$data["entryid"] = array();
			$data["entryid"]["_content"] = bin2hex($folderprops[PR_ENTRYID]);
			$data["storeid"] = array();
			$data["storeid"]["_content"] = bin2hex($folderprops[PR_STORE_ENTRYID]);
			$data["isinbox"] = array();
			$data["isinbox"]["_content"] = $folderprops[PR_ENTRYID] == $inboxentryid;

			if(isset($this->selectedMessageId)) {
				$this->start = $GLOBALS["operations"]->getStartRow($store, $entryid, $this->selectedMessageId, $this->sort, false, $this->searchRestriction);
			}

			// Get the table and merge the arrays
			$items = array_merge($data, $GLOBALS["operations"]->getTable($store, $entryid, $this->properties, $this->sort, $this->start, false, $this->searchRestriction));

			for($i=0; $i<count($items["item"]); $i++){
				// Disable private items
				$items["item"][$i] = $this->disablePrivateItem($items["item"][$i]);
			}
			$data = array_merge($data, $items);
				
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
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
			$this->properties = $GLOBALS["properties"]->getMailProperties($store);
		}
	}
?>
