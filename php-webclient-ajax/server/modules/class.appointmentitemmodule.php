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
	include_once('mapi/class.recurrence.php');
	
	/**
	 * Appointment ItemModule
	 * Module which openes, creates, saves and deletes an item. It 
	 * extends the Module class.
	 */
	class AppointmentItemModule extends ItemModule
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
		function AppointmentItemModule($id, $data)
		{
			parent::ItemModule($id, $data);
		}
		
		function open($store, $entryid, $action)
		{
			$attachNum = $this->getAttachNum($action);
			if(isset($action["basedate"])) {
				$basedate = $action["basedate"];
				
				$message = $GLOBALS["operations"]->openMessage($store, $entryid, $attachNum);

				$recur = new Recurrence($store, $message);
				
				$exceptionatt = $recur->getExceptionAttachment($basedate);

				if($exceptionatt) {
					$exceptionattProps = mapi_getprops($exceptionatt, array(PR_ATTACH_NUM));
					// Existing exception (open existing item, which includes basedate)
					$exception = mapi_attach_openobj($exceptionatt, 0);
				
    	            $data = array();
        	        $data["attributes"] = array("type" => "item");
					// First add all the properties from the series-message and than overwrite them with the ones form the exception
					$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);
            	    $exceptionProps = $GLOBALS["operations"]->getMessageProps($store, $exception, $this->properties, true);

					// HACK: when occurrence is dragged in OL, recipients are not set in occurrence
					if (isset($exceptionProps['recipients']) && count($exceptionProps['recipients']['recipient']) == 0)
						unset($exceptionProps['recipients']);
					// HACK: When body is empty use the body of the series-message
					if(strlen($exceptionProps['body']) == 0){
						unset($exceptionProps['body'], $exceptionProps['isHTML']);
					}

					/**
					 * If recurring item has set reminder to true then
					 * all occurrences before the 'flagdueby' value(of recurring item)
					 * should not show that reminder is set.
					 */
					if ($data['item']['reminder'] == 1 && !(isset($exceptionProps['reminder']) && $exceptionProps['reminder'] == 0)) {
						$flagDueByDay = $recur->dayStartOf($data['item']['flagdueby']['attributes']['unixtime']);

						if ($flagDueByDay > $basedate)	$exceptionProps['reminder'] = false;
					}

					$data["item"] = array_merge($data['item'], $exceptionProps);

            	    // The entryid should be the entryid of the main message, not that of the attachment
            	    $data["item"]["entryid"] = bin2hex($entryid);
                    
                    // Make sure we are using the passed basedate and not something wrong in the opened item
					$data["item"]["basedate"]["attributes"]["unixtime"] = $basedate;
					$data["item"]["basedate"]["_content"] = strftime("%a %d-%m-%Y %H:%M", $basedate);
					$data["item"]["occurrAttachNum"] = $exceptionattProps[PR_ATTACH_NUM];
                     
            	    array_push($this->responseData["action"], $data);
					$GLOBALS["bus"]->addData($this->responseData);
					return;
				}
				
				// Recurring but non-existing exception (same as normal open, but add basedate, startdate and enddate)
				$data = array();
				$data["attributes"] = array("type" => "item");
				$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);
				$data["item"]["basedate"]["attributes"]["unixtime"] = $basedate;
				$data["item"]["basedate"]["_content"] = $basedate;
				$data["item"]["startdate"]["attributes"]["unixtime"] = $recur->getOccurrenceStart($basedate);
				$data["item"]["startdate"]["_content"] = $recur->getOccurrenceStart($basedate);
				$data["item"]["duedate"]["attributes"]["unixtime"] = $recur->getOccurrenceEnd($basedate);
				$data["item"]["duedate"]["_content"] = $recur->getOccurrenceEnd($basedate);
				$data["item"]["commonstart"] = $data["item"]["startdate"];
				$data["item"]["commonend"] = $data["item"]["duedate"];
				unset($data["item"]["reminder_time"]);

				/**
				 * If recurring item has set reminder to true then
				 * all occurrences before the 'flagdueby' value(of recurring item)
				 * should not show that reminder is set.
				 */
				if ($data['item']['reminder'] == 1) {
					$flagDueByDay = $recur->dayStartOf($data['item']['flagdueby']['attributes']['unixtime']);

					if ($flagDueByDay > $basedate)	$data["item"]['reminder'] = false;
				}

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
				return;
			}
			
			// Normal item (may be the 'entire series' for a recurring item)
			$data = array();
			$data["attributes"] = array("type" => "item");

			$message = $GLOBALS["operations"]->openMessage($store, $entryid, $attachNum);
			if(!$attachNum){
				// Get the standard properties
				$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);
			}else{
				// Get the sub-message properties
				$data["item"] = $GLOBALS["operations"]->getEmbeddedMessageProps($store, $message, $this->properties, $entryid, $attachNum);
			}

			// Get the recurrence information			
			$recur = new Recurrence($store, $message);
			$recurpattern = $recur->getRecurrence();
			$tz = $recur->tz; // no function to do this at the moment

			// Add the recurrence pattern to the data
			if(isset($recurpattern) && is_array($recurpattern))			
				$data["item"] += $recurpattern;

			// Add the timezone information to the data
			if(isset($tz) && is_array($tz))			
				$data["item"] += $tz;
			
			// Send the data
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
		}
		
		function save($store, $parententryid, $action)
		{
			$result = false;

			// Save appointment (saveAppointment takes care of creating/modifying exceptions to recurring
			// items if necessary)
			$messageProps = $GLOBALS["operations"]->saveAppointment($store, $parententryid, $action);

			// Notify the bus if the save was OK
			if($messageProps && !(is_array($messageProps) && isset($messageProps['error'])) && !isset($messageProps["remindertimeerror"]) ){
				$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_SAVE, $messageProps);
				$result = true;
			}

			//for reminder time
			if(!$result && isset($messageProps['remindertimeerror']) && !$messageProps["remindertimeerror"]){
				$data = array();
				$data["attributes"] = array("type" => "saved");
				$data["remindertime"] = "remindertime";
				$data["error_message"] = _("Cannot set a reminder to appear before the previous occurence. Reset reminder to save the change");
			}else if (isset($messageProps['isexceptionallowed']) && $messageProps["isexceptionallowed"] === 'false'){
				$data = array();
				$data["attributes"] = array("type" => "saved");
				$data["proposetime"] = "proposetime";
				$data["error_message"] = _("Two occurrences cannot occur on the same day");
			}else{
				// Recurring but non-existing exception (same as normal open, but add basedate, startdate and enddate)
				$data = array();
				$data["attributes"] = array("type" => "saved");
				$data["meeting_request_saved"] = ($result) ? '1' : '0';
				$data["sent_meetingrequest"] = ($result && !empty($action['send'])) ? '1' : '0';
				$data["direct_booking_enabled"] = ENABLE_DIRECT_BOOKING ? '1' : '0';
				if(is_array($messageProps) && isset($messageProps['error'])){
					$data["errorcode"] = $messageProps['error'];
					$data["displayname"] = $messageProps['displayname'];
				}
			}
			
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);

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
			$this->properties = $GLOBALS["properties"]->getAppointmentProperties($store);
		}
	}
?>
