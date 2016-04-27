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
	 * Appointment Module
	*/
	
	require_once("mapi/class.recurrence.php");
	
	class AppointmentListModule extends ListModule
	{
		/**
		 * @var object Recurrence class object.
		 */
		var $recurrence;
		
		/**
		 * @var object MeetingRequest class object.
		 */
		var $meetingrequest;
		
		/**
		 * @var date start interval of view visible
		 */
		var $startdate;
		
		/**
		 * @var date end interval of view visible
		 */
		var $enddate;

		/**
		 * @var Array properties of appointment item that will be used to get data
		 */
		var $properties = null;
		
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function AppointmentListModule($id, $data)
		{
			$this->tablecolumns = $GLOBALS["TableColumns"]->getAppointmentListTableColumns();

			parent::ListModule($id, $data, array(OBJECT_SAVE, TABLE_SAVE, TABLE_DELETE));

			$this->startdate = false;
			$this->enddate = false;
		}
		
		/**
		 * Function which retrieves a list of calendar items in a calendar folder
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the folder
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function messageList($store, $entryid, $action)
		{
			$result = false;

			if($store && $entryid) {
				$type = false;
				
				if(isset($action["restriction"])) {
					if(isset($action["restriction"]["startdate"])) {
						$this->startdate = (int) $action["restriction"]["startdate"];
					} else {
						 // If start date is not passed then set it as false, otherwise it will use previously saved start date.
						$this->startdate = false;
					}
					
					if(isset($action["restriction"]["duedate"])) {
						$this->enddate = (int) $action["restriction"]["duedate"];
					} else {
						// If end date is not passed then set it as false, otherwise it will use previously saved end date.
						$this->enddate = false;
					}
				}
				
				if($this->startdate && $this->enddate) {
					$data = array();
					$data["attributes"] = array("type" => "list");
					$data["column"] = $this->tablecolumns;
					$data["item"] = $this->getCalendarItems($store, $entryid, $this->startdate, $this->enddate);
					$data["restriction"] = $action["restriction"];	// Send restriction back to client
	
					array_push($this->responseData["action"], $data);
					$GLOBALS["bus"]->addData($this->responseData);
				}else{
					// for list view in calendar as startdate and enddate in passed as false
					// this will set sorting and paging for items in listview.
					
					$result = parent::messageList($store, $entryid, $action);
				}
			}
			
			return $result;
		}
		
		/**
		 * Function to return all Calendar items in a given timeframe. This
		 * function also takes recurring items into account.
		 * @param object $store message store
		 * @param object $calendar folder
		 * @param date $start startdate of the interval
		 * @param date $end enddate of the interval
		 */
		function getCalendarItems($store, $entryid, $start, $end)
		{
			$items = Array();
		
			$restriction = 
						// OR
						// (item[end] >= start && item[start] < end) ||
						Array(RES_OR,
								 Array(
									   Array(RES_AND,
											 Array(
												   Array(RES_PROPERTY,
														 Array(RELOP => RELOP_GE,
															   ULPROPTAG => $this->properties["duedate"],
															   VALUE => $start
															   )
														 ),
												   Array(RES_PROPERTY,
														 Array(RELOP => RELOP_LT,
															   ULPROPTAG => $this->properties["startdate"],
															   VALUE => $end
															   )
														 )
												   )
											 ),
										//OR
										//(item[isRecurring] == true)
										Array(RES_PROPERTY,
											Array(RELOP => RELOP_EQ,
												ULPROPTAG => $this->properties["recurring"],
												VALUE => true
													)
											)		
									)
							);// global OR

			$folder = mapi_msgstore_openentry($store, $entryid);
			$table = mapi_folder_getcontentstable($folder);
			$calendaritems = mapi_table_queryallrows($table, $this->properties, $restriction);

			foreach($calendaritems as $calendaritem)
			{
				$item = null;
				if (isset($calendaritem[$this->properties["recurring"]]) && $calendaritem[$this->properties["recurring"]]) {
					$recurrence = new Recurrence($store, $calendaritem);
					$recuritems = $recurrence->getItems($start, $end);
					
					foreach($recuritems as $recuritem)
					{
						$item = Conversion::mapMAPI2XML($this->properties, $recuritem);
						if(isset($recuritem["exception"])) {
							$item["exception"] = true;
						}

						if(isset($recuritem["basedate"])) {
							$item["basedate"] = array();
							$item["basedate"]["attributes"] = array();
							$item["basedate"]["attributes"]["unixtime"] = $recuritem["basedate"];
							$item["basedate"]["_content"] = strftime("%a %d-%m-%Y %H:%M", $recuritem["basedate"]);
						}
						$item = $this->disablePrivateItem($item, true);
						array_push($items, $item);
					}
				} else {
					$item = Conversion::mapMAPI2XML($this->properties, $calendaritem);
					$item = $this->disablePrivateItem($item, true);
					array_push($items,$item);
				}

			} 
			usort($items, array("AppointmentListModule","compareCalendarItems"));

			return $items;
		}

		/**
		 * Function which saves an item. This function saves a normal appointment, a
		 * recurring items and its exceptions and meetingrequests.
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid entryid of the folder
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function save($store, $parententryid, $action)
		{
			$result = false;

			// Save the properties
			$messageProps = $GLOBALS["operations"]->saveAppointment($store, $parententryid, $action);
			
			/**
			 * If there is an error in exception then send the failed action to the client
			 * and send the error message with it.
			 */
			if(isset($messageProps["isexceptionallowed"])) {
				$data = array();
				$data["attributes"] = array("type" => "failed");
				
				if($messageProps["isexceptionallowed"] === "false"){
					$data["action_type"] = "moveoccurence";
					$data["error_message"] = _("Unable to reschedule occurrence: either the new occurrence skips over an existing occurrence, or an occurrence is already present on this day.");
				}
 				array_push($this->responseData["action"],$data);
 				$GLOBALS["bus"]->addData($this->responseData);
 			} else if (isset($messageProps['error'])) {
				$data = array();
				$data["attributes"] = array("type" => "failed");
				
				$data["action_type"] = "bookresource";
				$data["errorcode"] = $messageProps['error'];
				$data["displayname"] = $messageProps['displayname'];

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
			} else if(isset($messageProps["remindertimeerror"])) {
				$data = array();
				$data["attributes"] = array("type" => "failed");
				
				if(!$messageProps["remindertimeerror"]){
					$data["action_type"] = "remindertime";
					$data["error_message"] = _("Cannot set a reminder to appear before the previous occurence. Reset reminder to save the change");
				}
 				array_push($this->responseData["action"],$data);
 				$GLOBALS["bus"]->addData($this->responseData);
 			}

			// Send notifications that this object has changed
			if($messageProps){
				$GLOBALS["bus"]->notify($this->entryid, TABLE_SAVE, $messageProps);
				$result = true;
			}
			return $result;
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

				if($entryids) {
					// If softdelete is set then set it in softDelete variable and pass it for deleteing message.
					$softDelete = $action["softdelete"] ? $action["softdelete"] : false;
					$result = $GLOBALS["operations"]->deleteMessages($store, $parententryid, $entryids, $softDelete);
				} else if(isset($action["exception"])) {
					$result = $this->recurrence->saveException($store, $action["exception"]);
					
					// TODO: Add basedate to props. The client will verify if base date exists.
				}
				$GLOBALS["operations"]->publishFreeBusy($store, $parententryid);

				if($result) {
					$GLOBALS["bus"]->notify($this->entryid, TABLE_DELETE, $props);
				}
			}
			
			return $result;
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
			
			switch($event)
			{
				case TABLE_SAVE:
					$data = array();
					$data["attributes"] = array("type" => "item");

					if(isset($props[PR_STORE_ENTRYID])) {
						$store = $GLOBALS["mapisession"]->openMessageStore($props[PR_STORE_ENTRYID]);
						
						if(isset($props[PR_ENTRYID])) {
							$message = $GLOBALS["operations"]->openMessage($store, $props[PR_ENTRYID]);
							$messageProps = mapi_getprops($message, $this->properties);
							
							/**
							 * in calendar list view recurring items are not multi instances objects
							 * so we don't have to do any special handling for that just check
							 * startdate and enddate are false then the request has come from calednar list view 
							 * so just update item in a normal way
							 */
							if ($this->startdate && $this->enddate && isset($messageProps[$this->properties['recurring']]) && $messageProps[$this->properties['recurring']]){
								// Because this is a multi-instanced object, we have to send a DELETE first to delete all occurring items
								// before re-adding the updated items.
								$data["attributes"] = array("type" => "delete");
								$item["parent_entryid"] = bin2hex($props[PR_PARENT_ENTRYID]);
								$item["entryid"] = array(bin2hex($props[PR_ENTRYID]));
								$data["item"] = array($item);
								array_push($this->responseData["action"], $data);
								
								$recurrence = new Recurrence($store, $messageProps);

								$recuritems = $recurrence->getItems($this->startdate, $this->enddate);
								
								$items = array();
								foreach($recuritems as $recuritem)
								{
									$item = Conversion::mapMAPI2XML($this->properties, $recuritem);
									if(isset($recuritem["exception"])) {
										$item["exception"] = true;
									}

									if(isset($recuritem["basedate"])) {
										$item["basedate"] = array();
										$item["basedate"]["attributes"] = array();
										$item["basedate"]["attributes"]["unixtime"] = $recuritem["basedate"];
										$item["basedate"]["_content"] = strftime("%a %d-%m-%Y %H:%M", $recuritem["basedate"]);
									}
						
									// This is a multi-instanced message; ie there can be more than one entry of this
									// item with the same entryid in the view. If you don't specify this, then the client
									// will be updating the same item multiple times, instead of just adding them all
									// to the view.
									$item["attributes"] = array("instance" => 1);
									
									array_push($items, $item);
								}
								$data["attributes"] = array("type" => "item");
								$data["item"] = $items;
							} else {
								$data["attributes"] = array("type" => "item");
								$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $GLOBALS["operations"]->openMessage($store, $props[PR_ENTRYID]), $this->properties);
							}
						}
					}
					
					array_push($this->responseData["action"], $data);
					break;
				case TABLE_DELETE:
					if(isset($props[PR_ENTRYID]) && isset($props[PR_PARENT_ENTRYID])) {

						// Get items, which are shown under the table.
						$store = $GLOBALS["mapisession"]->openMessageStore($props[PR_STORE_ENTRYID]);
						
						$data = array();
						$data["attributes"] = array("type" => "item");
						$data["delete"] = 1;
						
						$deletedrows = 1;
						if(is_array($props[PR_ENTRYID])) {
							$deletedrows = count($props[PR_ENTRYID]);
						}

						$newItemsStart = ($this->start + (int)$GLOBALS["settings"]->get("global/rowcount", 50)) - $deletedrows;
						$data = array_merge($data, $GLOBALS["operations"]->getTable($store, $props[PR_PARENT_ENTRYID], $this->properties, $this->sort, $newItemsStart, $deletedrows));

						array_push($this->responseData["action"], $data);
						
						$data = array();
						$data["attributes"] = array("type" => "delete");
						$data["parent_entryid"] = bin2hex($props[PR_PARENT_ENTRYID]);
						
						if(is_array($props[PR_ENTRYID])) {
							$data["entryid"] = array();
							
							foreach($props[PR_ENTRYID] as $entryid)
							{
								array_push($data["entryid"], bin2hex($entryid));
							}
						} else {
							$data["entryid"] = bin2hex($props[PR_ENTRYID]);
						}
						
						array_push($this->responseData["action"], $data);
					}
					break;
			}
			
			$GLOBALS["bus"]->addData($this->responseData);
		}
		
		/**
		 * Function will sort items for the month view
		 * small startdate->attributes->unixtime on top	 
		 */		 		
		function compareCalendarItems($a, $b)
		{
			$start_a = $a["startdate"]["attributes"]["unixtime"];
			$start_b = $b["startdate"]["attributes"]["unixtime"];
		
		   if ($start_a == $start_b) {
		       return 0;
		   }
		   return ($start_a < $start_b) ? -1 : 1;
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
			$this->properties = $GLOBALS["properties"]->getAppointmentProperties($store);

			$this->sort = array();
			$this->sort[$this->properties["subject"]] = TABLE_SORT_ASCEND;
		}
	}
?>
