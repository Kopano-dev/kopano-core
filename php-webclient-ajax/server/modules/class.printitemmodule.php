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
	 * Print ItemModule
	 * Currently only mail is supported by this module
	 */
	class PrintItemModule extends ItemModule
	{
		/**
		 * @var Array properties of item that will be used to get data
		 */
		var $properties = null;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function PrintItemModule($id, $data)
		{
			parent::ItemModule($id, $data);
		}

		/**
		 * Function which opens an item.
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function open($store, $entryid, $action)
		{
			$result = false;

			if($store && $entryid) {
				$data = array();
				$data["attributes"] = array("type" => "item");
				$message = $GLOBALS["operations"]->openMessage($store, $entryid);
				if (isset($this->plaintext) && $this->plaintext){
					$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);
				}else{
					$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, false);
				}

				if($data["item"]["recurring"] == 1) {
					if(isset($action["attachments"]) && isset($action["attachments"]["attach_num"]) && isset($action["attachments"]["attach_num"][0])) {
						$basedate = $action["attachments"]["attach_num"][0];

						$message = mapi_msgstore_openentry($store, $entryid);

						$recur = new Recurrence($store, $message);

						$exceptionatt = $recur->getExceptionAttachment($basedate);

						if($exceptionatt) {
							// Existing exception (open existing item, which includes basedate)
							$exception = mapi_attach_openobj($exceptionatt, 0);

							$data = array();
							$data["attributes"] = array("type" => "item");
							// First add all the properties from the series-message and than overwrite them with the ones form the exception
							$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);
							$exceptionProps = $GLOBALS["operations"]->getMessageProps($store, $exception, $this->properties, true);
							// HACK: When body is empty use the body of the series-message
							if(strlen($exceptionProps['body']) == 0){
								unset($exceptionProps['body'], $exceptionProps['isHTML']);
							}
							$data["item"] = array_merge($data['item'], $exceptionProps);

							// The entryid should be the entryid of the main message, not that of the attachment
							$data["item"]["entryid"] = bin2hex($entryid);
							
							array_push($this->responseData["action"], $data);
							$GLOBALS["bus"]->addData($this->responseData);
							return;
						}

						// Recurring but non-existing exception (same as normal open, but add basedate, startdate and enddate)
						$data = array();
						$data["attributes"] = array("type" => "item");
						$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);
						$data["item"]["basedate"]["attributes"]["unixtime"] = $basedate;
						$data["item"]["basedate"]["_content"] = strftime("%a %d-%m-%Y %H:%M", $basedate);
						$data["item"]["startdate"]["attributes"]["unixtime"] = $recur->getOccurrenceStart($basedate);
						//$data["item"]["startdate"]["_content"] = strftime("%a %d-%m-%Y %H:%M", $recur->getOccurrenceStart($basedate));
						$data["item"]["startdate"]["_content"] = $recur->getOccurrenceStart($basedate);
						$data["item"]["duedate"]["attributes"]["unixtime"] = $recur->getOccurrenceEnd($basedate);
						//$data["item"]["duedate"]["_content"] = strftime("%a %d-%m-%Y %H:%M", $recur->getOccurrenceEnd($basedate));
						$data["item"]["duedate"]["_content"] = $recur->getOccurrenceEnd($basedate);
						$data["item"]["commonstart"] = $data["item"]["startdate"];
						$data["item"]["commonend"] = $data["item"]["duedate"];
						unset($data["item"]["reminder_time"]);

						array_push($this->responseData["action"], $data);
						$GLOBALS["bus"]->addData($this->responseData);
						return;
					}

					$message = mapi_msgstore_openentry($store, $entryid);

					// Normal item (may be the 'entire series' for a recurring item)
					$data = array();
					$data["attributes"] = array("type" => "item");

					// Get the standard properties
					$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);

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

					array_push($this->responseData["action"], $data);
					$GLOBALS["bus"]->addData($this->responseData);
				}else{
					$result = parent::open($store, $entryid, $action);
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
			$message = $GLOBALS["operations"]->openMessage($store, $entryid);

			$props = mapi_getprops($message, Array(PR_MESSAGE_CLASS));

			switch($props[PR_MESSAGE_CLASS]){
				case "IPM.Task":
					$this->properties = $GLOBALS["properties"]->getTaskProperties($store);
					break;
				case "IPM.Appointment":
					$this->properties = $GLOBALS["properties"]->getAppointmentProperties($store);
					break;
				case "IPM.Contact":
					$this->properties = $GLOBALS["properties"]->getContactProperties($store);
					break;
				case "IPM.StickyNote":
					$this->properties = $GLOBALS["properties"]->getStickyNoteProperties($store);
					break;
				case "IPM.DistList":
					$this->properties = $GLOBALS["properties"]->getDistListProperties($store);
					break;
				case "IPM.Note":
				default:
					$this->properties = $GLOBALS["properties"]->getMailProperties($store);
			}
		}
	}
?>
