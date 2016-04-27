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
	 * Date Picker Module
	 */
	
	require_once("mapi/class.recurrence.php");
	
	class DatePickerListModule extends ListModule
	{
		/**
		 * @var Array properties of appointment item that will be used to get data
		 */
		var $properties = null;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function DatePickerListModule($id, $data)
		{
			parent::ListModule($id, $data, array(TABLE_SAVE, TABLE_DELETE));
		}
		
		/**
		 * Function which retrieves a list of appointments in a calendar folder.
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
				$startdate = false;
				$enddate = false;
				
				$startdate = mktime();
				if(isset($action["restriction"])) {	
					if(isset($action["restriction"]["startdate"])) {
						$startdate = (int) $action["restriction"]["startdate"];
					}
				}
				
				$data = array();
				$data["attributes"] = array("type" => "list");
				$data["item"] = $this->getCalendarItems($store, $entryid, $startdate, $startdate + (31*24*60*60));

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
			}
			
			return $result;
		}
		
		/**
		 * Function to return all Calendar items in a given timeframe. This
		 * function also takes recurring items into account.
		 * @param Object $store message store
		 * @param Object $calendar folder
		 * @param Date $start startdate of the interval
		 * @param Date $end enddate of the interval
		 */
		function getCalendarItems($store, $entryid, $start, $end)
		{
			$items = Array();
		
			$restriction = 
						// OR
						// (item[end] >= start && item[start] <= end) ||
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
														 Array(RELOP => RELOP_LE,
															   ULPROPTAG => $this->properties["startdate"],
															   VALUE => $end
															   )
														 )
												   )
											 ),
									   // OR
									   Array(RES_OR,
											 Array(
												   // OR
												   // (EXIST(recurrence_enddate_property) && item[isRecurring] == true && (item[end] < item[start] || item[end] >= start))
												   Array(RES_AND,
														 Array(
															   Array(RES_EXIST,
																	 Array(ULPROPTAG => $this->properties["enddate_recurring"],
																		   )
																	 ),
															   Array(RES_PROPERTY,
																	 Array(RELOP => RELOP_EQ,
																		   ULPROPTAG => $this->properties["recurring"],
																		   VALUE => true
																		   )
																	 ),
															   Array(RES_OR,
															   		// This is a hack. If we can see that the enddate_recurring is BEFORE the startdate, 
															   		// then the enddate_recurring is bogus and we should ignore it!
															   		Array(
																		Array(RES_COMPAREPROPS,
																			Array(RELOP => RELOP_LT,
																				ULPROPTAG1 => $this->properties["enddate_recurring"],
																				ULPROPTAG2 => $this->properties["startdate"]
																			)
																		),
																	
																		
																	   Array(RES_PROPERTY,
																			 Array(RELOP => RELOP_GE,
																				   ULPROPTAG => $this->properties["enddate_recurring"],
																				   VALUE => $start
																				   )
																			 )
																		)
																	)
															   )
														 ),
												   // OR
												   // (!EXIST(recurrence_enddate_property) && item[isRecurring] == true && item[start] <= end)

												   Array(RES_AND,
														 Array(
															   Array(RES_NOT,
																	 Array(
																		   Array(RES_EXIST,
																				 Array(ULPROPTAG => $this->properties["enddate_recurring"]
																					   )
																				 )
																		   )
																	 ),
															   Array(RES_PROPERTY,
																	 Array(RELOP => RELOP_LE,
																		   ULPROPTAG => $this->properties["startdate"],
																		   VALUE => $end
																		   )
																	 ),
															   Array(RES_PROPERTY,
																	 Array(RELOP => RELOP_EQ,
																		   ULPROPTAG => $this->properties["recurring"],
																		   VALUE => true
																		   )
																	 )
															   )
														 )
												   )
											 ) // EXISTS OR
									   )
								 );		// global OR

            // We only need a few properties for the datepicker
            $rowproperties = array("subject" => $this->properties["subject"],
                                   "startdate" => $this->properties["startdate"],
                                   "duedate" => $this->properties["duedate"],
                                   "location" => $this->properties["location"],
                                   "entryid" => $this->properties["entryid"],
                                   "recurring" => $this->properties["recurring"],
                                   "timezone_data" => $this->properties["timezone_data"],
                                   "recurring_data" => $this->properties["recurring_data"],
				                   "busystatus" => $this->properties["busystatus"]);

			$folder = mapi_msgstore_openentry($store, $entryid);
			$table = mapi_folder_getcontentstable($folder);
			$calendaritems = mapi_table_queryallrows($table, $rowproperties, $restriction);
		
			foreach($calendaritems as $calendaritem)
			{
				if (isset($calendaritem[$this->properties["recurring"]]) && $calendaritem[$this->properties["recurring"]]) {
					$recurrence = new Recurrence($store, $calendaritem);
					$recuritems = $recurrence->getItems($start, $end);
					
					foreach($recuritems as $recuritem)
					{
						array_push($items, Conversion::mapMAPI2XML($rowproperties, $recuritem));
					}
				} else {
					array_push($items, Conversion::mapMAPI2XML($rowproperties, $calendaritem));
				} 
			} 
			
			return $items;
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
		}
	}
?>
