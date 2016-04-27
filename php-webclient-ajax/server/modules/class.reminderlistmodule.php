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
	* Reminder Module
	*
	* TODO: add description
	*
	*/
	class ReminderListModule extends ListModule
	{
		/**
		 * @var Array properties of reminder item that will be used to get data
		 */
		var $properties = null;

		/**
		* Constructor
		* @param int $id unique id.
		* @param array $data list of all actions.
		*/
		function ReminderListModule($id, $data)
		{
			// Default Columns
			$this->tablecolumns = $GLOBALS["TableColumns"]->getReminderListTableColumns();

			parent::ListModule($id, $data, array());

			$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
			$this->reminderEntryId = $this->getReminderFolderEntryId($store);

			// Register this module on the bus, but don't specify any real event
			$GLOBALS["bus"]->register($this, REQUEST_ENTRYID, array(DUMMY_EVENT));
		}
		
		function execute()
		{
			$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
			$result = false;
			foreach($this->data as $action)
			{
				$this->generatePropertyTags($store, false, $action);

				if(isset($action["attributes"]) && isset($action["attributes"]["type"])) {
					switch($action["attributes"]["type"])
					{
						case "getreminders":
							$result = $this->getReminders($action);
							break;
						case "snooze":
							$entryid = $this->getActionEntryID($action);
							$snoozetime = 5;
							if (isset($action["snoozetime"]) && is_numeric($action["snoozetime"])){
								$snoozetime = $action["snoozetime"];
							}
							$result = $this->snoozeItem($store, $entryid, $snoozetime);
							break;
						case "dismiss":
							$entryid = $this->getActionEntryID($action);
							$result = $this->dismissItem($store, $entryid);
							break;
						default:
					}
				}
			}

			// until we have some sort of notifiction in this module, we always retrieve all reminders with every request!
			// $result = $this->getReminders($action);
			
			return $result;
		}
	

		function getReminderFolderEntryId($store)
		{
			$root = mapi_msgstore_openentry($store, null);
			$rootProps = mapi_getprops($root, array(PR_REM_ONLINE_ENTRYID));
			if (isset($rootProps[PR_REM_ONLINE_ENTRYID])){
				return $rootProps[PR_REM_ONLINE_ENTRYID];
			}

			// Reminder folder didn't exist, create one
			$entryid = $this->createReminderFolder($store);
			return $entryid;
		}
		
		function createReminderFolder($store)
		{
			$storeProps = mapi_getprops($store, array(PR_IPM_OUTBOX_ENTRYID, PR_IPM_WASTEBASKET_ENTRYID, PR_IPM_SUBTREE_ENTRYID));
			$root = mapi_msgstore_openentry($store, null);
			$rootProps = mapi_getprops($root, array(PR_ADDITIONAL_REN_ENTRYIDS, PR_IPM_DRAFTS_ENTRYID));


			$folders = array();
			if (isset($storeProps[PR_IPM_WASTEBASKET_ENTRYID]))
				$folders[] = $storeProps[PR_IPM_WASTEBASKET_ENTRYID];
			if (isset($rootProps[PR_ADDITIONAL_REN_ENTRYIDS])&&!empty($rootProps[PR_ADDITIONAL_REN_ENTRYIDS][4]))
				$folders[] = $rootProps[PR_ADDITIONAL_REN_ENTRYIDS][4]; // junk mail
			if (isset($rootProps[PR_IPM_DRAFTS_ENTRYID]))
				$folders[] = $rootProps[PR_IPM_DRAFTS_ENTRYID];
			if (isset($storeProps[PR_IPM_OUTBOX_ENTRYID]))
				$folders[] = $storeProps[PR_IPM_OUTBOX_ENTRYID];
			if (isset($rootProps[PR_ADDITIONAL_REN_ENTRYIDS])&&!empty($rootProps[PR_ADDITIONAL_REN_ENTRYIDS][0]))
				$folders[] = $rootProps[PR_ADDITIONAL_REN_ENTRYIDS][0]; // conflicts
			if (isset($rootProps[PR_ADDITIONAL_REN_ENTRYIDS])&&!empty($rootProps[PR_ADDITIONAL_REN_ENTRYIDS][2]))
				$folders[] = $rootProps[PR_ADDITIONAL_REN_ENTRYIDS][2]; // local failures
			if (isset($rootProps[PR_ADDITIONAL_REN_ENTRYIDS])&&!empty($rootProps[PR_ADDITIONAL_REN_ENTRYIDS][1]))
				$folders[] = $rootProps[PR_ADDITIONAL_REN_ENTRYIDS][1]; // sync issues
		
			$props = array(
				"parent_entryid"	=>	PR_PARENT_ENTRYID,
				"message_class"		=>	PR_MESSAGE_CLASS,
				"message_flags"		=>	PR_MESSAGE_FLAGS,
				"reminder_set"		=>	"PT_BOOLEAN:PSETID_Common:0x8503",
				"is_recurring"		=>	"PT_BOOLEAN:PSETID_Appointment:0x8223",
			);
			$props = getPropIdsFromStrings($store, $props);

			$folderRestriction = array();
			foreach($folders as $folder){
				$folderRestriction[] = 	array(RES_PROPERTY,
											array(
												RELOP		=>	RELOP_NE,
												ULPROPTAG	=>	$props["parent_entryid"],
												VALUE		=>	array($props["parent_entryid"]	=>	$folder)
											)
										);
			}

			$res = 
				array(RES_AND,
					array(
						array(RES_AND,
							$folderRestriction
						),
						array(RES_AND,
							array(
								array(RES_NOT,
									array(
										array(RES_AND,
											array(
												array(RES_EXIST,
													array(
														ULPROPTAG	=>	$props["message_class"]
													)
												),
												array(RES_CONTENT,
													array(
														FUZZYLEVEL	=>	FL_PREFIX,
														ULPROPTAG	=>	$props["message_class"],
														VALUE		=>	array($props["message_class"]	=>	"IPM.Schedule")
													)
												)
											)
										)
									)
								),
								array(RES_BITMASK,
									array(
										ULTYPE		=>	BMR_EQZ,
										ULPROPTAG	=>	$props["message_flags"],
										ULMASK		=>	MSGFLAG_SUBMIT
									)
								),
								array(RES_OR,
									array(
										array(RES_PROPERTY,
											array(
												RELOP		=>	RELOP_EQ,
												ULPROPTAG	=>	$props["reminder_set"],
												VALUE		=>	array($props["reminder_set"]	=>	true)
											)
										),
									)
								)
							)
						)
					)
				);

			$folder = mapi_folder_createfolder($root, _("Reminders"), "", OPEN_IF_EXISTS, FOLDER_SEARCH);
			mapi_setprops($folder, array(PR_CONTAINER_CLASS	=>	"Outlook.Reminder"));
			mapi_savechanges($folder);

			mapi_folder_setsearchcriteria($folder, $res, array($storeProps[PR_IPM_SUBTREE_ENTRYID]), RECURSIVE_SEARCH);
			$folderProps = mapi_getprops($folder, array(PR_ENTRYID));
			
			mapi_setprops($root, array(PR_REM_ONLINE_ENTRYID	=>	$folderProps[PR_ENTRYID]));
			mapi_savechanges($root);

			return $folderProps[PR_ENTRYID];
		}

		function getReminders($action)
		{
			$data = array();
			$data["attributes"] = array("type" => "getreminders");

			$data["column"] = $this->tablecolumns;

			$firstSortColumn = reset(array_keys($this->sort)); // get first key of the sort array
			$data["sort"] = array();
			$data["sort"]["attributes"] = array();
			$data["sort"]["attributes"]["direction"] = (isset($this->sort[$firstSortColumn]) && $this->sort[$firstSortColumn] == TABLE_SORT_ASCEND) ? "asc":"desc";
			$data["sort"]["_content"] = array_search($firstSortColumn, $this->properties);

			$store = $GLOBALS["mapisession"]->getDefaultMessageStore();

			$restriction = 	array(RES_AND,
								array(
									array(RES_PROPERTY,
										array(
											RELOP		=>	RELOP_LT,
											ULPROPTAG	=>	$this->properties["flagdueby"],
											VALUE		=>	array($this->properties["flagdueby"] =>	time())
										)
									),
									array(RES_PROPERTY,
										array(
											RELOP		=>	RELOP_EQ,
											ULPROPTAG	=>	$this->properties["reminder"],
											VALUE		=>	true
										)
									)
								)
			);

			$reminderfolder = mapi_msgstore_openentry($store, $this->reminderEntryId);
			if(!$reminderfolder) {
				if(mapi_last_hresult() == MAPI_E_NOT_FOUND) {
					$this->reminderEntryId = $this->createReminderFolder($store);
					$reminderfolder = mapi_msgstore_openentry($store, $this->reminderEntryId);
				}				
				if(!$reminderfolder)
					return false;
			}
				
			$remindertable = mapi_folder_getcontentstable($reminderfolder);
			if(!$remindertable)
				return false;
				
			mapi_table_restrict($remindertable, $restriction);
			mapi_table_sort($remindertable, array($this->properties["flagdueby"] => TABLE_SORT_DESCEND));

			$rows = mapi_table_queryrows($remindertable, $this->properties, 0, /* all rows */0x7ffffff);
			
			$data["item"] = array();

			foreach($rows as $row) {
				if(isset($row[$this->properties["appointment_recurring"]]) && $row[$this->properties["appointment_recurring"]]) {
					$recur = new Recurrence($store, $row);

					/**
					 * FlagDueBy == PidLidReminderSignalTime.
					 * FlagDueBy handles whether we should be showing the item; if now() is after FlagDueBy, then we should show a reminder
					 * for this recurrence. However, the item we will show is either the last passed occurrence (overdue), or the next occurrence, depending
					 * on whether we have reached the next occurrence yet (the remindertime of the next item is ignored).
					 *
					 * The way we handle this is to get all occurrences between the 'flagdueby' moment and the current time. This will
					 * yield N items (may be a lot of it was not dismissed for a long time). We can then take the last item in this list, and this is the item
					 * we will show to the user. The idea here is:
					 *
					 * The item we want to show is the last item in that list (new occurrences that have started uptil now should override old ones)
					 *
					 * Add the reminder_minutes (default 15 minutes for calendar, 0 for tasks) to check over the gap between FlagDueBy and the start time of the
					 * occurrence, if "now" would be in between these values.
					 */
					$remindertimeinseconds = $row[$this->properties["reminder_minutes"]] * 60;
					$occurrences = $recur->getItems($row[$this->properties["flagdueby"]], time() + ($remindertimeinseconds), 0, true);

					if(empty($occurrences))
						continue;

				    // More than one occurrence, use the last one instead of the first one after flagdueby
                    $occ = $occurrences[count($occurrences)-1];

                    // Bydefault, on occurrence reminder is true but if reminder value is set to false then we don't send popup reminder for this occurrence
                    if (!(isset($occ[$this->properties['reminder']]) && $occ[$this->properties['reminder']] == 0)) {
                        $row[$this->properties["remindertime"]] = $occ[$this->properties["appointment_startdate"]];
                        $row[$this->properties["appointment_startdate"]] = $occ[$this->properties["appointment_startdate"]];
                        $row[$this->properties["appointment_enddate"]] = $occ[$this->properties["appointment_startdate"]];
                    }
				}

				// Add the non-bogus rows
				array_push($data["item"], Conversion::mapMAPI2XML($this->properties, $row));
			}
			
			// Generate this handy MD5 so that the client can easily detect changes
			$data["rowchecksum"] = md5(serialize($data["item"]));
			
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);

			return true;
		}

		function snoozeItem($store, $entryid, $snoozetime)
		{
			$result = false;
			$message = mapi_msgstore_openentry($store, $entryid);
			if ($message){
				$newProps = array(PR_ENTRYID => $entryid);
				$props = mapi_getprops($message, $this->properties);
				
				$reminderTime = time()+($snoozetime*60);
				if (strtolower(substr($props[$this->properties["message_class"]],0, 15)) == "ipm.appointment"){
					if (isset($props[$this->properties["appointment_recurring"]]) && $props[$this->properties["appointment_recurring"]]){

						$recurrence = new Recurrence($store, $message);
						$nextReminder = $recurrence->getNextReminderTime(time());
						
						// flagdueby must be the snooze time or the time of the next instance, whichever is earlier
						if ($reminderTime < $nextReminder)
							$newProps[$this->properties["flagdueby"]] = $reminderTime;
						else
							$newProps[$this->properties["flagdueby"]] = $nextReminder;
					}else{
						$newProps[$this->properties["flagdueby"]] = $reminderTime;
					}
				}else{
					$newProps[$this->properties["flagdueby"]] = $reminderTime;
				}
				
				// save props
				mapi_setprops($message, $newProps);
				mapi_savechanges($message);
	
				$result = true;
			}
			
			if ($result){
				$props = mapi_getprops($message, array(PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID));
				$GLOBALS["bus"]->notify(bin2hex($props[PR_PARENT_ENTRYID]), TABLE_SAVE, $props);
			}
			return $result;
		}

		function dismissItem($store, $entryid)
		{
			$result = false;
			$message = mapi_msgstore_openentry($store, $entryid);
			if ($message){
				$newProps = array();
				$props = mapi_getprops($message, $this->properties);
				
				if (strtolower(substr($props[$this->properties["message_class"]],0, 15)) == "ipm.appointment"){
					if (isset($props[$this->properties["appointment_recurring"]]) && $props[$this->properties["appointment_recurring"]]){

						$recurrence = new Recurrence($store, $message);
						// check for next reminder after "now" for the next instance
						$nextReminder = $recurrence->getNextReminderTime(time());
						if($nextReminder)
    						$newProps[$this->properties["flagdueby"]] = $nextReminder;
                        else
                            $newProps[$this->properties["reminder"]] = false;
					}else{
						$newProps[$this->properties["reminder"]] = false;
					}
				} else if (strtolower(substr($props[$this->properties["message_class"]],0, 15)) == "ipm.task") {
					$newProps[$this->properties["reminder"]] = false;

					if (isset($props[$this->properties['task_recurring']]) && $props[$this->properties['task_recurring']] == 1) {
						$newProps[$this->properties['task_resetreminder']] = true;
					}
				} else {
					$newProps[$this->properties["reminder"]] = false;
				}
				
				// save props
				mapi_setprops($message, $newProps);
				mapi_savechanges($message);

				$result = true;
			}
			if ($result){
				$props = mapi_getprops($message, array(PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID));
				$GLOBALS["bus"]->notify(bin2hex($props[PR_PARENT_ENTRYID]), TABLE_SAVE, $props);
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
			$this->properties = $GLOBALS["properties"]->getReminderProperties($store);

			$this->sort = array();
			$this->sort[$this->properties["remindertime"]] = TABLE_SORT_DESCEND;
		}
	}
?>
