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
	 * ItemModule
	 * Module which openes, creates, saves and deletes an item. It
	 * extends the Module class.
	 */
	class ItemModule extends Module
	{
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function ItemModule($id, $data)
		{
			parent::Module($id, $data);
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

					$overQouta = $this->checkOverQoutaRestriction($store, $action);
					if(!empty($overQouta)) {
						if($overQouta == "quota_hard") {
							$errorMessage = _("The message store has exceeded its hard quota limit.") . "\n\n" .
											_("To reduce the amount of data in this message store, select some items that you no longer need, and permanently (SHIFT + DEL) delete them.");
						} else if($overQouta == "quota_soft") {
							$errorMessage = _("The message store has exceeded its soft quota limit.") . "\n\n" .
											_("To reduce the amount of data in this message store, select some items that you no longer need, and permanently (SHIFT + DEL) delete them.");
						}

						$this->sendFeedback(false, $errorMessage);

						continue;
					}

					switch($action["attributes"]["type"])
					{
						case "open":
							$result = $this->open($store, $entryid, $action);
							break;
						case "save":
							$accessToFolder = true;
							/**
							 * Check for the folder access.
							 * If store and parententryid is not passed then it is
							 * users default messagestore and user has permission on it.
							 * For other folders check permissions on folder.
							 */
							if($store && $parententryid)
								$accessToFolder = $GLOBALS["operations"]->checkFolderAccess($store, $parententryid);

							if($accessToFolder) {
								// If folder is accessible then save the message.
								$result = $this->save($store, $parententryid, $action);
							} else {
								// If folder is not accessible then show error message.
								$errorMessage = _("You have insufficient privileges to save items in this folder") . ".";
								$this->sendFeedback(false, $errorMessage);
							}
							break;
						case "delete":
							$result = $this->delete($store, $parententryid, $entryid, $action);
							$GLOBALS["operations"]->publishFreeBusy($store, $parententryid);
							break;
						case "read_flag":
							$result = $this->setReadFlag($store, $parententryid, $entryid, $action);
							break;
						case "checknames":
							$result = $this->checkNames($store, $action);
							break;
						case "cancelMeetingRequest":
						case "declineMeetingRequest":
						case "acceptMeetingRequest":
							$message = $GLOBALS["operations"]->openMessage($store, $entryid);
							$basedate = (isset($action['basedate']) ? $action['basedate'] : false);
							$delete = false;
							/**
							 * Get message class from original message. This can be changed to
							 * IPM.Appointment if the item is a Meeting Request in the maillist.
							 * After Accepting/Declining the message is moved and changed.
							 */
							$originalMessageProps = mapi_getprops($message, array(PR_MESSAGE_CLASS));
							$req = new Meetingrequest($store, $message, $GLOBALS["mapisession"]->getSession(), ENABLE_DIRECT_BOOKING);

							// Update extra body information
							if(isset($action['meetingTimeInfo']) && strlen($action['meetingTimeInfo'])) {
								$req->setMeetingTimeInfo($action['meetingTimeInfo']);
								unset($action['meetingTimeInfo']);
							}

							// sendResponse flag if it is set then send the mail response to the organzer.
							$sendResponse = true;
							if(isset($action["noResponse"]) && $action["noResponse"] == "true") {
								$sendResponse = false;
							}
							$body = isset($action["body"]) ? u2w($action["body"]) : false;

							if($action["attributes"]["type"] == 'acceptMeetingRequest') {
								$tentative = isset($action["tentative"]) ? $action["tentative"] : false;
								$newProposedStartTime = isset($action["proposed_starttime"]) ? $action["proposed_starttime"] : false;
								$newProposedEndTime = isset($action["proposed_endtime"]) ? $action["proposed_endtime"] : false;

								// We are accepting MR from preview-read-mail so set delete the actual mail flag.
								$delete = (stristr($originalMessageProps[PR_MESSAGE_CLASS], 'IPM.Schedule.Meeting') !== false) ? true : false;
								$req->doAccept($tentative, $sendResponse, $delete, $newProposedStartTime, $newProposedEndTime, $body, true, $store, $basedate);
							} else {
								$delete = $req->doDecline($sendResponse, $store, $basedate, $body);
							}


							// Publish updated free/busy information
							$GLOBALS["operations"]->publishFreeBusy($store);

							/**
							 * Now if the item is the Meeting Request that was sent to the attendee
							 * it is removed when the user has clicked on Accept/Decline. If the
							 * item is the appointment in the calendar it will not be moved. To only
							 * notify the bus when the item is a Meeting Request we are going to
							 * check the PR_MESSAGE_CLASS and see if it is "IPM.Meeting*".
							 */
							$messageProps = mapi_getprops($message, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID, PR_MESSAGE_CLASS));

							if($delete){
								$GLOBALS["bus"]->notify(bin2hex($messageProps[PR_PARENT_ENTRYID]), TABLE_DELETE, $messageProps); // send TABLE_DELETE event because the message has moved
							}

							if (!$delete)
								$GLOBALS["bus"]->notify(bin2hex($messageProps[PR_PARENT_ENTRYID]), TABLE_SAVE, $messageProps); // send TABLE_SAVE event because an occurrence is deleted
							break;
						case "cancelInvitation":
							$accessToFolder = false;
							// Find the parententryid using the store and the entryid when it has not been sent in the request
							if($store && $entryid && !$parententryid){
								$folder = mapi_msgstore_openentry($store, $entryid);
								$folderProps = mapi_getprops($folder, Array(PR_PARENT_ENTRYID));
								$parententryid = $folderProps[PR_PARENT_ENTRYID];
							}

							if($store && $parententryid)
								$accessToFolder = $GLOBALS["operations"]->checkFolderAccess($store, $parententryid);

							if($accessToFolder) {
								// If folder is accessible then cancel inviation the message.
								$GLOBALS["operations"]->cancelInvitation($store, $entryid, $action);
							} else {
								// If folder is not accessible then show error message.
								$errorMessage = _("You have insufficient privileges to delete items in this folder") . ".";
								$this->sendFeedback(false, $errorMessage);
							}
							break;
						case "removeFromCalendar":
							$GLOBALS["operations"]->removeFromCalendar($store, $entryid);
							break;

						case "resolveConflict":
							$result = $this->resolveConflict($store, $parententryid, $entryid, $action);
							break;

						case "attach_items":
							$result = $this->forwardMultipleItems($store, $parententryid, $action);
							break;

						//this will convert selected message into message of particular target folder.
						case "convert_item":
							$result = $this->getItemData($store, $parententryid, $action);
							break;

						case "getAttachments":
							// Get list of uploaded attachments.
							$result = $this->getAttachments($store, $entryid, $action);
							break;

						case "reclaimownership":
							$message = $GLOBALS["operations"]->openMessage($store, $entryid);
							$tr = new TaskRequest($store, $message, $GLOBALS["mapisession"]->getSession());
							$tr->reclaimownership();
							break;

						case "acceptTaskRequest":
						case "declineTaskRequest":
							$message = $GLOBALS["operations"]->openMessage($store, $entryid);
							// The task may be a delegated task, do an update if needed (will fail for non-delegated tasks)
							$tr = new TaskRequest($store, $message, $GLOBALS["mapisession"]->getSession());
							if ($action["attributes"]["type"] == "acceptTaskRequest") {
								$result = $tr->doAccept(_("Task Accepted:") . " ");
							} else {
								$result = $tr->doDecline(_("Task Declined:") . " ");
							}

							// Notify Inbox that task request has been deleted
							if (is_array($result))
								$GLOBALS["bus"]->notify(bin2hex($result[PR_PARENT_ENTRYID]), TABLE_DELETE, $result);

							mapi_savechanges($message);
							$result = true;
							break;
					}
				}
			}

			return $result;
		}

		/**
		 * Function which sets selected messages as attachment to the item
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function forwardMultipleItems($store, $parententryid, $action)
		{
			$result = false;

			if($store){
				if(isset($action["entryids"]) && $action["entryids"]){
					if(!is_array($action["entryids"])){
						$action["entryids"] = array($action["entryids"]);
					}

					// add attach items to attachment state
					$attachments = $GLOBALS["operations"]->setAttachmentInSession($store, $action["entryids"], $action["dialog_attachments"]);
				}

				$data = array();
				$data["attributes"] = array("type" => "attach_items");
				$data["item"] = array();
				$data["item"]["parententryid"] = bin2hex($parententryid);
				$data["item"]["attachments"] = $attachments;

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);

				$result = true;
			}
			return $result;
		}


		/**
		 * Function which get data of selected messages to send as response
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function getItemData($store, $parententryid, $action)
		{
			$result = false;
			$msg = array();

			if($store){
				if(isset($action["messages"]["message"]) && is_array($action["messages"]["message"])){

					if (!isset($action["messages"]["message"][0])){
						$msg = array($action["messages"]["message"]);
					}else{
						$msg = $action["messages"]["message"];
					}

					$data = array();
					$data["attributes"] = array("type" => "convert_item");
					$data["item"] = array();
					$messageIsDistList = false;

					foreach($msg as $messageItem){
						$items = array();
						$message = mapi_msgstore_openentry($store, hex2bin($messageItem['id']));

						// get the properties of selected message
						switch($messageItem['type']) {
							case "Appointment":
							case "Schedule":
								$properties = $GLOBALS["properties"]->getAppointmentProperties();
								break;
							case "Contact":
								$properties = $GLOBALS["properties"]->getContactProperties();
								break;
							case "DistList":
								$messageIsDistList = true;
								$props = mapi_getprops($message, array(PR_DISPLAY_NAME, PR_MESSAGE_CLASS, PR_BODY, PR_OBJECT_TYPE));

								$memberItem = $GLOBALS["operations"]->expandDistributionList($store, hex2bin($messageItem['id']), false, $GLOBALS["properties"]->getContactABProperties(), $GLOBALS["properties"]->getAddressBookProperties());

								$items["dl_name"] = w2u($props[PR_DISPLAY_NAME]);
								$items["message_class"] = w2u($props[PR_MESSAGE_CLASS]);
								$items["body"] = w2u($props[PR_BODY]);
								$items["objecttype"] = $props[PR_OBJECT_TYPE];
								$items["addrtype"] = "ZARAFA";
								$items["members"] = array("member"=>$memberItem);
								break;
							case "Task":
								$properties = $GLOBALS["properties"]->getTaskProperties();
								break;
							case "StickyNote":
								$properties = $GLOBALS["properties"]->getStickyNoteProperties();
								break;
							case "Note":
							default:
								$properties = $GLOBALS["properties"]->getMailProperties();
						}
						if(!$messageIsDistList)
							$items = $GLOBALS["operations"]->getMessageProps($store, $message, $properties, true);

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

				// Decode smime signed messages on this message
 				parse_smime($store, $message);

				if (isset($this->plaintext) && $this->plaintext){
					$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);
				}else{
					$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, false);
				}

				if(isset($data["item"]["message_class"]) && $data["item"]["message_class"] == "IPM.DistList") {
					// remove non-client props
					unset($data["item"]["members"]);
					unset($data["item"]["oneoff_members"]);

					// get members
					$messageProps = mapi_getprops($message, array($this->properties["members"], $this->properties["oneoff_members"]));
					$members = isset($messageProps[$this->properties["members"]]) ? $messageProps[$this->properties["members"]] : array();
					$oneoff_members = isset($messageProps[$this->properties["oneoff_members"]]) ? $messageProps[$this->properties["oneoff_members"]] : array();

					// parse oneoff members
					foreach($oneoff_members as $key=>$item) {
						$oneoff_members[$key] = mapi_parseoneoff($item);
					}

					$data["item"]["members"]["member"] = array();
					$count = 0;

					foreach($members as $key=>$item) {
						$parts = unpack("Vnull/A16guid/Ctype/A*entryid", $item);

						if($parts["entryid"] != null) {
							if ($parts["guid"] == hex2bin("812b1fa4bea310199d6e00dd010f5402")) {
								$item = mapi_parseoneoff($item);
								$item["distlisttype"] = "ONEOFF";
								$item["entryid"] = "oneoff_" . (++$count). "_" . bin2hex($members[$key]);
								$item["icon_index"] = 512;
								$item["message_class"] = "IPM.DistListItem.OneOffContact";
							} else {
								$item = array();
								$item["name"] = $oneoff_members[$key]["name"];
								$item["type"] = $oneoff_members[$key]["type"];
								$item["address"] = $oneoff_members[$key]["address"];
								$item["entryid"] = array("attributes" => array("type" => "binary"), "_content" => bin2hex($parts["entryid"]));

								$updated_info = $this->updateItem($store, $oneoff_members[$key], $parts);
								if ($updated_info){
									$item["name"] = $updated_info["name"];
									$item["type"] = $updated_info["type"];
									$item["address"] = $updated_info["email"];
								}else{
									$item["missing"] = "1";
								}

								switch($parts["type"]) {
		                            case 0:
										$item["missing"] = "0";
										$item["distlisttype"] = "ONEOFF";
										$item["icon_index"] = 512;
										$item["message_class"] = "IPM.DistListItem.OneOffContact";
										break;
									case DL_USER:
										$item["distlisttype"] = "DL_USER";
										$item["icon_index"] = 512;
										$item["message_class"] = "IPM.Contact";
										break;
									case DL_USER2:
										$item["distlisttype"] = "DL_USER2";
										$item["icon_index"] = 512;
										$item["message_class"] = "IPM.Contact";
										break;
									case DL_USER3:
										$item["distlisttype"] = "DL_USER3";
										$item["icon_index"] = 512;
										$item["message_class"] = "IPM.Contact";
										break;
									case DL_USER_AB:
										$item["distlisttype"] = "DL_USER_AB";
										$item["icon_index"] = 512;
										$item["message_class"] = "IPM.DistListItem.AddressBookUser";
										break;
									case DL_DIST:
										$item["distlisttype"] = "DL_DIST";
										$item["icon_index"] = 514;
										$item["message_class"] = "IPM.DistList";
										break;
									case DL_DIST_AB:
										$item["distlisttype"] = "DL_DIST_AB";
										$item["icon_index"] = 514;
										$item["message_class"] = "IPM.DistListItem.AddressBookGroup";
										break;
								}
							}

							$item["name"] = w2u($item["name"]);
							$item["address"] = w2u($item["address"]);

							$item["message_flags"] = 1;
							array_push($data["item"]["members"]["member"], $item);
						}
					}
				}

				// Check for meeting request, do processing if necessary
				if(isset($data["item"]["message_class"]) && strpos($data["item"]["message_class"], "IPM.Schedule.Meeting") !== false) {
					$req = new Meetingrequest($store, $message, $GLOBALS["mapisession"]->getSession(), ENABLE_DIRECT_BOOKING);
					if($req->isMeetingRequestResponse()) {
						if($req->isLocalOrganiser()) {
							// We received a meeting request response, and we're the organiser
							$apptProps = $req->processMeetingRequestResponse();
						}else if(isset($data["item"]["rcvd_representing_name"])){
							// We can also received a meeting request response, when we are delegate
							$apptProps = $req->processMeetingRequestResponseAsDelegate();
						}
						mapi_savechanges($message);

						if (isset($apptProps))
							$data["item"]["appointment"] = $apptProps;
						else
							$data["item"]["appt_not_found"] = true;
					} else if($req->isMeetingRequest()) {
						if(!$req->isLocalOrganiser()) {
							if ($req->isMeetingOutOfDate()) {
								$data["item"]["out_of_date"] = true;
								$messageProps = mapi_getprops($message, array(PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID));
								$GLOBALS["bus"]->notify(bin2hex($messageProps[PR_PARENT_ENTRYID]), TABLE_SAVE, $messageProps);
							} else {
								/**
								 * put item in calendar 'tentatively' if this is latest update,
								 * i.e if meeting request is not out of date.
								 */
								$req->doAccept(true, false, false);
								// Publish updated free/busy information
								$GLOBALS["operations"]->publishFreeBusy($store);
							}

							// We received a meeting request, show it to the user
							$data["item"]["ismeetingrequest"] = 1;
							// Show user whether meeting request conflict with other appointment or not.
							$meetingConflicts = $req->isMeetingConflicting();
							/**
							 * if $meetingConflicts is boolean and true then its a normal meeting.
							 * if $meetingConflictis is integer then it indicates no of instances of a recurring meeting which conflicts with Calendar.
							 */
							if (is_bool($meetingConflicts) && $meetingConflicts) {
								$data["item"]["meetingconflicting"] = _('Conflicts with another appointment on your Calendar.');
							} else if ($meetingConflicts) {
								if ($meetingConflicts > 1) $data["item"]["meetingconflicting"] = sprintf(_('%s occurrences of this recurring appointment conflict with other appointments on your Calendar.'), $meetingConflicts);
								else $data["item"]["meetingconflicting"] = sprintf(_('%s occurrence of this recurring appointment conflicts with other appointment on your Calendar.'), $meetingConflicts);
							}
						}
					} else if($req->isMeetingCancellation()) {
						if(!$req->isLocalOrganiser()) {
							// Let's do some processing of this Meeting Cancellation Object we received
							$req->processMeetingCancellation();
							// We received a cancellation request, show it to the user
							$data["item"]["ismeetingcancel"] = 1;
						}
					}
				}

				if (isset($data["item"]["message_class"]) && strpos($data["item"]["message_class"], "IPM.TaskRequest") !== false) {
					$tr = new TaskRequest($store, $message, $GLOBALS["mapisession"]->getSession());
					$properties = $GLOBALS["properties"]->getTaskProperties();

					// @FIXME is this code used anywhere?
					if($tr->isTaskRequest()) {
						$tr->processTaskRequest();
						$task = $tr->getAssociatedTask(false);
						$taskProps = $GLOBALS["operations"]->getMessageProps($store, $task, $properties, true);
						$data["item"] = $taskProps;

						// notify task folder that new task has been created
						$GLOBALS["bus"]->notify($taskProps["parent_entryid"]["_content"], TABLE_SAVE, array(
							PR_ENTRYID => hex2bin($taskProps["entryid"]["_content"]),
							PR_PARENT_ENTRYID => hex2bin($taskProps["parent_entryid"]["_content"]),
							PR_STORE_ENTRYID => hex2bin($taskProps["store_entryid"]["_content"])
						));
					}

					if($tr->isTaskRequestResponse()) {
						$tr->processTaskResponse();
						$task = $tr->getAssociatedTask(false);

						$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $task, $properties, true);
					}
				}

				// Open embedded message in embedded message in ...
				$attachNum = $this->getAttachNum($action);
				if($attachNum) {
					$attachedMessage = $GLOBALS["operations"]->openMessage($store, $entryid, $attachNum);
					$data["item"] = $GLOBALS["operations"]->getEmbeddedMessageProps($store, $attachedMessage, $this->properties, $entryid, $attachNum);
				}

				// check if this message is a NDR (mail)message, if so, generate a new body message
				if(isset($data["item"]["message_class"]) && $data["item"]["message_class"] == "REPORT.IPM.Note.NDR"){
					$data["item"]["body"] = $GLOBALS["operations"]->getNDRbody($GLOBALS["operations"]->openMessage($store, $entryid));
				}

				// Allowing to hook in just before the data sent away to be sent to the client
				$GLOBALS['PluginManager']->triggerHook("server.module.itemmodule.open.after", array(
					'moduleObject' =>& $this,
					'store' => $store,
					'entryid' => $entryid,
					'action' => $action,
					'message' =>& $message,
					'data' =>& $data
					));

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);

				$result = true;
			}

			return $result;
		}

		/**
		 * Function which saves an item.
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid parent entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function save($store, $parententryid, $action)
		{
			$result = false;

			if(isset($action["props"])) {

				if(!$store && !$parententryid) {
					if(isset($action["props"]["message_class"])) {
						$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
						$parententryid = $this->getDefaultFolderEntryID($store, $action["props"]["message_class"]);
					}
				}else if(!$parententryid){
					if(isset($action["props"]["message_class"]))
						$parententryid = $this->getDefaultFolderEntryID($store, $action["props"]["message_class"]);
				}

				if($store && $parententryid) {
					$messageProps = array(); // returned props
					$result = $GLOBALS["operations"]->saveMessage($store, $parententryid, Conversion::mapXML2MAPI($this->properties, $action["props"]), false, (isset($action["dialog_attachments"])?$action["dialog_attachments"]:null), $messageProps);

					if($result) {
						$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_SAVE, $messageProps);
					}
				}
			}

			return $result;
		}

		/**
		 * Function which deletes an item.
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid parent entryid of the message
		 * @param string $entryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function delete($store, $parententryid, $entryid, $action)
		{
			$result = false;

			if($store && $parententryid && $entryid) {
				$props = array();
				$props[PR_PARENT_ENTRYID] = $parententryid;
				$props[PR_ENTRYID] = $entryid;

				$storeprops = mapi_getprops($store, array(PR_ENTRYID));
				$props[PR_STORE_ENTRYID] = $storeprops[PR_ENTRYID];

				$result = $GLOBALS["operations"]->deleteMessages($store, $parententryid, $entryid);

				if($result) {
					$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_DELETE, $props);

					// Notify the client of the successful delete action
					$data = array();
					$data["attributes"] = array("type" => "deleted");
					$data['item'] = Array(
						'entryid' => bin2hex($entryid),
						'parent_entryid' => bin2hex($parententryid),
						'store_entryid' => bin2hex($storeprops[PR_ENTRYID])
					);
					array_push($this->responseData["action"], $data);
					$GLOBALS["bus"]->addData($this->responseData);

				}
			}

			return $result;
		}

		/**
		 * Function which sets the PR_MESSAGE_FLAGS property of an item.
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid parent entryid of the message
		 * @param string $entryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function setReadFlag($store, $parententryid, $entryid, $action)
		{
			$result = false;

			if($store && $parententryid && $entryid) {
				$flags = "read,noreceipt";
				if(isset($action["flag"])) {
					$flags = $action["flag"];
				}

				$props = array();
				$result = $GLOBALS["operations"]->setMessageFlag($store, $entryid, $flags, $props);

				if($result) {
					$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_SAVE, $props);
				}
			}

			return $result;
		}

		/**
		 * Function which returns the entryid of a default folder.
		 * @param object $store MAPI Message Store Object
		 * @param string $messageClass the class of the folder
		 * @return string entryid of a default folder, false if not found
		 */
		function getDefaultFolderEntryID($store, $messageClass)
		{
			$entryid = false;

			if($store) {
				$rootcontainer = mapi_msgstore_openentry($store);
				$rootcontainerprops = mapi_getprops($rootcontainer, array(PR_IPM_DRAFTS_ENTRYID, PR_IPM_APPOINTMENT_ENTRYID, PR_IPM_CONTACT_ENTRYID, PR_IPM_JOURNAL_ENTRYID, PR_IPM_NOTE_ENTRYID, PR_IPM_TASK_ENTRYID));

				switch($messageClass)
				{
					case "IPM.Appointment":
						if(isset($rootcontainerprops[PR_IPM_APPOINTMENT_ENTRYID])) {
							$entryid = $rootcontainerprops[PR_IPM_APPOINTMENT_ENTRYID];
						}
						break;
					case "IPM.Contact":
					case "IPM.DistList":
						if(isset($rootcontainerprops[PR_IPM_CONTACT_ENTRYID])) {
							$entryid = $rootcontainerprops[PR_IPM_CONTACT_ENTRYID];
						}
						break;
					case "IPM.StickyNote":
						if(isset($rootcontainerprops[PR_IPM_NOTE_ENTRYID])) {
							$entryid = $rootcontainerprops[PR_IPM_NOTE_ENTRYID];
						}
						break;
					case "IPM.Task":
						if(isset($rootcontainerprops[PR_IPM_TASK_ENTRYID])) {
							$entryid = $rootcontainerprops[PR_IPM_TASK_ENTRYID];
						}
						break;
					default:
						if(isset($rootcontainerprops[PR_IPM_DRAFTS_ENTRYID])) {
							$entryid = $rootcontainerprops[PR_IPM_DRAFTS_ENTRYID];
						}
						break;
				}
			}

			return $entryid;
		}

		function resolveConflict($store, $parententryid, $entryid, $action)
		{

			if(!is_array($entryid)) {
				$entryid = array($entryid);
			}
			$srcmessage = mapi_openentry($GLOBALS["mapisession"]->getSession(), $entryid[0], 0);
			if(!$srcmessage)
				return false;

			$dstmessage = mapi_openentry($GLOBALS["mapisession"]->getSession(), hex2bin($action["conflictentryid"]), MAPI_MODIFY);
			if(!$dstmessage)
				return false;

			$srcfolder = mapi_openentry($GLOBALS["mapisession"]->getSession(), $parententryid, MAPI_MODIFY);

			$result = mapi_copyto($srcmessage, array(), array(PR_CONFLICT_ITEMS, PR_SOURCE_KEY, PR_CHANGE_KEY, PR_PREDECESSOR_CHANGE_LIST), $dstmessage);
			if(!$result)
				return $result;

			//remove srcmessage entryid from PR_CONFLICT_ITEMS
			$props = mapi_getprops($dstmessage, array(PR_CONFLICT_ITEMS));
			if(isset($props[PR_CONFLICT_ITEMS])){
				$binentryid = hex2bin($entryid[0]);
				foreach($props[PR_CONFLICT_ITEMS] as $i => $conflict){
					if($conflict == $binentryid){
						array_splice($props[PR_CONFLICT_ITEMS],$i,1);
					}else{
						$tmp = mapi_openentry($GLOBALS["mapisession"]->getSession(), $conflict, 0);
						if(!$tmp){
							array_splice($props[PR_CONFLICT_ITEMS],$i,1);
						}
						unset($tmp);
					}
				}
				if(count($props[PR_CONFLICT_ITEMS]) == 0){
					mapi_setprops($dstmessage, $props);
				}else{
					mapi_deleteprops($dstmessage, array(PR_CONFLICT_ITEMS));
				}
			}


			mapi_savechanges($dstmessage);

			$result = mapi_folder_deletemessages($srcfolder, $entryid);

			$props = array();
			$props[PR_PARENT_ENTRYID] = $parententryid;
			$props[PR_ENTRYID] = $entryid[0];

			$storeprops = mapi_getprops($store, array(PR_ENTRYID));
			$props[PR_STORE_ENTRYID] = $storeprops[PR_ENTRYID];
			$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_DELETE, $props);

			if(!$result)
				return $result;
		}

		function updateItem($store, $oneoff, $parts) {
			$result = false;
			$number = 1; // needed for contacts
			switch($parts["type"]) {
				case DL_USER3:
					$number++;
				case DL_USER2:
					$number++;
				case DL_USER:
					$item = mapi_msgstore_openentry($store, $parts["entryid"]);
					if(mapi_last_hresult() == NOERROR) {
						$properties = $GLOBALS["properties"]->getContactProperties();
						$props = mapi_getprops($item, $properties);
						if(is_int(array_search(($number - 1), $props[$properties["address_book_mv"]])) &&
							isset($props[$properties["email_address_" . $number]]) &&
							isset($props[$properties["email_address_display_name_" . $number]]) &&
							isset($props[$properties["email_address_type_" . $number]])){

							$result = array(
										"name" => $props[$properties["email_address_display_name_" . $number]],
										"email" => $props[$properties["email_address_" . $number]],
										"type" => $props[$properties["email_address_type_" . $number]]
									);
						}
					}
					break;
				case DL_DIST:
					$item = mapi_msgstore_openentry($store, $parts["entryid"]);
					if(mapi_last_hresult() == NOERROR) {
						$props = mapi_getprops($item, array(PR_DISPLAY_NAME));
						$result = array(
									"name" => $props[PR_DISPLAY_NAME],
									"email" => $props[PR_DISPLAY_NAME],
									"type" => "SMTP"
								);
					}
					break;
				case DL_USER_AB:
				case DL_DIST_AB:
					$ab = $GLOBALS["mapisession"]->getAddressBook();
					$item = mapi_ab_openentry($ab, $parts["entryid"]);
					if(mapi_last_hresult() == NOERROR) {
						$props = mapi_getprops($item, array(PR_DISPLAY_NAME, PR_SMTP_ADDRESS, PR_ADDRTYPE));
						$result = array(
									"name" => $props[PR_DISPLAY_NAME],
									"email" => $props[PR_SMTP_ADDRESS],
									"type" => $props[PR_ADDRTYPE]
								);
					}
					break;
			}
			return $result;
		}

		/**
		 * Function which returns list of uploaded files for perticular dialog
		 * returns list of file as XML response.
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the message
		 * @param array $action the action data, sent by the client
		 */
		function getAttachments($store, $entryid, $action)
		{
			$attachment_state = new AttachmentState();
			$attachment_state->open();

			// Create the data array of information, which will be send back to the client.
			$data = array();
			$data["attributes"] = array("type" => "getAttachments");
			$dialog_attachments = $action["dialog_attachments"];
			$data["files"] = array();

			$files = $attachment_state->getAttachmentFiles($dialog_attachments);

			// Get uploaded attachments' information.
			if($files) {
				foreach($files as $tmpname => $file) {
					$filedata = array();
					$filedata["attach_num"] = $tmpname;
					$filedata["filetype"] = $file["type"];
					$filedata["name"] = $file["name"];
					$filedata["size"] = $file["size"];

					array_push($data["files"], $filedata);
				}
			}

			$attachment_state->close();

			// Add file information into response data.
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
		}

		/**
		 * Checks whether the attachnum information is passed in the $action Array and returns this.
		 * Otherwise it will return false.
		 * @param Array $action Action information
		 * @return Boolean|Array Returns the attach num array or false if none is set
		 */
		function getAttachNum($action){
			// TODO: Not 100% sure why the rootentryid is still checked
			if(isset($action["rootentryid"]) && isset($action["attachments"])){
				if(isset($action["attachments"]["attach_num"]) && is_array($action["attachments"]["attach_num"])) {
					return $action["attachments"]["attach_num"];
				}
			}
			return false;
		}
	}
?>
