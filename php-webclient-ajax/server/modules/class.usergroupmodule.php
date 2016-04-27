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
	 * UserGroupModule Module
	*/
	
	class UserGroupModule extends Module 
	{
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function UserGroupModule($id, $data)
		{
			$this->properties = Array();
			$this->startdate = false;
			$this->enddate = false;

			$this->sort = array();

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
				
					switch($action["attributes"]["type"])
					{
						case "getaddressbookusers":
							$result = $this->GABUsers($action);
							break;
						case "getgroups":
							$result = $this->getGroups($action);
							break;
						case "getgroupusers":
							$result = $this->getGroupUsers($action);
							break;
						case "savegroup":
							$result = $this->saveGroup($action);
							break;
						case "removegroup":
							$result = $this->removeGroup($action);
							break;
					}
				}
			}

/*

// Get StoreEntryID by username  (this => $GLOBALS['mapisession'])   [check hresult==0]
$user_entryid = mapi_msgstore_createentryid($this->getDefaultMessageStore(), $username);
// Open store (this => $GLOBALS['mapisession'])
$store = $this->openMessageStore($user_entryid);

// Open root folder
$root = mapi_msgstore_openentry($store, null);
// Get calendar entryID
$rootProps = mapi_getprops($root, array(PR_IPM_APPOINTMENT_ENTRYID));

// Open Calendar folder   [check hresult==0]
$calFolder = mapi_msgstore_openentry($store, $rootProps[PR_IPM_APPOINTMENT_ENTRYID]);




*/


			
			return $result;
		}



		/**
		 * Function which retrieves the list of system users in Zarafa.
		 * @param object $store MAPI Message Store Object
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function GABUsers($action)
		{
			$name = "";
			if(isset($action["restriction"])) {
				if(isset($action["restriction"]["name"])) {
					$name = $action["restriction"]["name"];
				}
			}

			$data = array();
			$data["attributes"] = array("type" => "users");
			$data["column"] = $this->tablecolumns;

			$firstSortColumn = array_shift(array_keys($this->sort));
			$data["sort"] = array();
			$data["sort"]["attributes"] = array();
			$data["sort"]["attributes"]["direction"] = "asc";
			$data["sort"]["_content"] = array_search($firstSortColumn, $this->properties);


			$ab = $GLOBALS["mapisession"]->getAddressbook();
			$entryid = mapi_ab_getdefaultdir($ab);
			$dir = mapi_ab_openentry($ab,$entryid);

			$table = mapi_folder_getcontentstable($dir);

			// only return users from who the displayName or the username starts with $name
			// TODO: use PR_ANR for this restriction instead of PR_DISPLAY_NAME and PR_ACCOUNT
			$restriction = array(RES_AND, 
								array(
									array(RES_OR, 
										array(
											array(
												RES_CONTENT,
													array(FUZZYLEVEL => FL_PREFIX|FL_IGNORECASE,
														ULPROPTAG=>PR_DISPLAY_NAME,
														VALUE=>$name
													)
												),
											array(
												RES_CONTENT,
													array(FUZZYLEVEL => FL_PREFIX|FL_IGNORECASE,
														ULPROPTAG=>PR_ACCOUNT,
														VALUE=>$name
													)
												),
										), // RES_OR
									),
									array(
										RES_PROPERTY,
											array(RELOP=>RELOP_EQ,
												ULPROPTAG=>PR_OBJECT_TYPE,
												VALUE=>MAPI_MAILUSER // TODO: MAPI_DISTLIST: for now, ignore the groups
											)
										)
								) // RES_AND
							);

			// todo: fix paging stuff
			$data["page"] = array();
			$data["page"]["start"] = 0;
			$data["page"]["rowcount"] = mapi_table_getrowcount($table);
			$data["page"]["totalrowcount"] = mapi_table_getrowcount($table);

			mapi_table_restrict($table, $restriction);
			mapi_table_sort($table, array(PR_DISPLAY_NAME => TABLE_SORT_ASCEND));
			$items = array();
			for ($i = 0; $i < mapi_table_getrowcount($table); $i++) {
				$user_data = mapi_table_queryrows($table, array(PR_ACCOUNT, PR_DISPLAY_NAME, PR_ENTRYID, PR_OBJECT_TYPE, PR_SMTP_ADDRESS), $i, 1);


				// Get StoreEntryID by username  (this => $GLOBALS['mapisession'])   [check hresult==0]
				$user_entryid = mapi_msgstore_createentryid($GLOBALS['mapisession']->getDefaultMessageStore(), $user_data[0][PR_ACCOUNT]);	//973078558 => username?
				// Open store (this => $GLOBALS['mapisession'])
				$store = $GLOBALS['mapisession']->openMessageStore($user_entryid);

				// Open root folder
				$root = mapi_msgstore_openentry($store, null);
				// Get calendar entryID
				$rootProps = mapi_getprops($root, array(PR_STORE_ENTRYID, PR_IPM_APPOINTMENT_ENTRYID));

				// Check to filter out accounts without stores like SYSTEM
				if(isset($rootProps[PR_IPM_APPOINTMENT_ENTRYID])){

					$item = array();
					$item["type"] = 'user';
					$item["entryid"] = bin2hex($user_data[0][PR_ENTRYID]);
					$item["icon_index"] = 512; // FIXME
					$item["message_class"] = "IPM.Contact"; // FIXME
					$item["display_name"] = windows1252_to_utf8($user_data[0][PR_DISPLAY_NAME]);
					$item["fileas"] = windows1252_to_utf8($user_data[0][PR_ACCOUNT]);
					$item["email_address"] = windows1252_to_utf8($user_data[0][PR_SMTP_ADDRESS]);
					$item["message_flags"] = MSGFLAG_READ; // in addressbook we don't have message flags, but we want to show them as "read" 

					// Open Calendar folder   [check hresult==0]
					$calFolder = mapi_msgstore_openentry($store, $rootProps[PR_IPM_APPOINTMENT_ENTRYID]);

					$item["storeid"] = bin2hex($rootProps[PR_STORE_ENTRYID]);
					$item["calentryid"] = bin2hex($rootProps[PR_IPM_APPOINTMENT_ENTRYID]);

					array_push($items, $item);
				}

			}
			$data = array_merge($data, array("item"=>$items));

			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
			return true;
		}


		function getGroups($action){
			$restriction = Array(
				RES_AND,
				Array(
					Array(	// Check for PR_MESSAGE_CLASS == IPM.appointment
						RES_CONTENT,
						Array(
							FUZZYLEVEL => FL_FULLSTRING,
							ULPROPTAG => PR_MESSAGE_CLASS,
							VALUE => Array(
								PR_MESSAGE_CLASS => 'IPM.Appointment'
							)
						)
					),
					Array(	// Check if subject starts with "{B911D251-1842-4720-A131-F164B6C99078} - "
						RES_CONTENT,
						Array(
							FUZZYLEVEL => FL_PREFIX,
							ULPROPTAG => PR_SUBJECT,
							VALUE => Array(
								PR_SUBJECT => '{B911D251-1842-4720-A131-F164B6C99078} - '
							)
						)
					)
				)
			);

			$root = mapi_msgstore_openentry($GLOBALS['mapisession']->getDefaultMessageStore(),  null);
			$rootProps = mapi_getProps($root, Array(PR_IPM_APPOINTMENT_ENTRYID));

			$folder = mapi_msgstore_openentry($GLOBALS['mapisession']->getDefaultMessageStore(), $rootProps[PR_IPM_APPOINTMENT_ENTRYID]);
			$table = mapi_folder_getcontentstable($folder, MAPI_ASSOCIATED);
			$calendaritems = mapi_table_queryallrows($table, Array(PR_ENTRYID, PR_SUBJECT), $restriction);

			$items = Array();
			for($i=0;$i<count($calendaritems);$i++){
				$items[] = Array(
					'type' => 'usergroup',
					'entryid' => bin2hex($calendaritems[$i][PR_ENTRYID]),
					'subject' => str_replace('{B911D251-1842-4720-A131-F164B6C99078} - ', '', $calendaritems[$i][PR_SUBJECT])
				);
			}


			$data = array();
			$data["attributes"] = array("type" => "groups");
			$data = array_merge($data, array("item"=>$items));

			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
			return true;
		}

		function getGroupUsers($action){
			$recipientRestriction = Array(
				// Check for PR_ADDRTYPE == ZARAFA
				RES_CONTENT,
				Array(
					FUZZYLEVEL => FL_FULLSTRING,
					ULPROPTAG => PR_ADDRTYPE,
					VALUE => Array(
						PR_ADDRTYPE => 'ZARAFA'
					)
				)
			);

			$items = Array();

			$message = mapi_msgstore_openentry($GLOBALS['mapisession']->getDefaultMessageStore(), hex2bin($action['group_entry_id']));

			$msgRecipientsTable = mapi_message_getrecipienttable($message);

			$msgRecipientsTableItems = mapi_table_queryallrows($msgRecipientsTable, Array(PR_DISPLAY_NAME, PR_ADDRTYPE, PR_ENTRYID, PR_EMAIL_ADDRESS, PR_SMTP_ADDRESS), $recipientRestriction);

			for($i=0;$i<count($msgRecipientsTableItems);$i++){
				$item = Array(
					'type' => 'user',
					'userentryid' => bin2hex($msgRecipientsTableItems[$i][PR_ENTRYID]),
					'display_name' => $msgRecipientsTableItems[$i][PR_DISPLAY_NAME],
					'username' => $msgRecipientsTableItems[$i][PR_EMAIL_ADDRESS],
					'emailaddress' => $msgRecipientsTableItems[$i][PR_SMTP_ADDRESS]
				);


				// Get StoreEntryID by username  (this => $GLOBALS['mapisession'])   [check hresult==0]
				$user_entryid = mapi_msgstore_createentryid($GLOBALS['mapisession']->getDefaultMessageStore(), $item['username']);

				// Open store (this => $GLOBALS['mapisession'])
				$userStore = $GLOBALS['mapisession']->openMessageStore($user_entryid);

				// Open root folder
				$userRoot = mapi_msgstore_openentry($userStore, null);

				// Get calendar entryID
				$userRootProps = mapi_getprops($userRoot, array(PR_STORE_ENTRYID, PR_IPM_APPOINTMENT_ENTRYID));

				// Open Calendar folder   [check hresult==0]
				$calFolder = mapi_msgstore_openentry($userStore, $userRootProps[PR_IPM_APPOINTMENT_ENTRYID]);

				if($calFolder){
					$calFolderProps = mapi_getProps($calFolder, Array(PR_ACCESS));
					$item['access'] = $calFolderProps[PR_ACCESS];

					$item["storeid"] = bin2hex($userRootProps[PR_STORE_ENTRYID]);
					$item["calentryid"] = bin2hex($userRootProps[PR_IPM_APPOINTMENT_ENTRYID]);

				}else{
					$item['access'] = 0;
				}

				$items[] = $item;
			}

			$data = array();
			$data["attributes"] = array("type" => "users");
			$data = array_merge($data, array("item"=>$items));

			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
			return true;
		}


		function saveGroup($action){
			$root = mapi_msgstore_openentry($GLOBALS['mapisession']->getDefaultMessageStore(),  null);
			$rootProps = mapi_getProps($root, Array(PR_IPM_APPOINTMENT_ENTRYID));

			$folder = mapi_msgstore_openentry($GLOBALS['mapisession']->getDefaultMessageStore(), $rootProps[PR_IPM_APPOINTMENT_ENTRYID]);
			if(isset($action['group_entry_id'])){
				$newmsg = mapi_msgstore_openentry($GLOBALS['mapisession']->getDefaultMessageStore(), hex2bin($action['group_entry_id']));
			}else{
				$newmsg = mapi_folder_createmessage($folder, MAPI_ASSOCIATED);
			}
			$recipTbl = mapi_message_getrecipienttable($newmsg);

			// Delete recipients
			if($recipTbl){
				$numOfRecipients = mapi_table_getrowcount($recipTbl);
				if($numOfRecipients > 0){
					$userRowIDs = Array();
					for($i=0;$i<$numOfRecipients;$i++){
						$userRowIDs[] = Array(
							PR_ROWID => $i
						);
					}
					mapi_message_modifyrecipients($newmsg, MODRECIP_REMOVE, $userRowIDs);
				}
			}

			if(!isset($action['group_entryid']) && isset($action['groupname'])){
				mapi_setProps($newmsg, Array(
					PR_SUBJECT => '{B911D251-1842-4720-A131-F164B6C99078} - ' . $action['groupname'],
					PR_MESSAGE_CLASS => 'IPM.Appointment'
				));
			}
			$users = Array();
			if(is_array($action['users'])){
				for($i=0;$i<count($action['users']);$i++){
					if(isset($action['users'][$i]['userentryid'])){
						$users[] = Array(
							PR_ENTRYID => hex2bin($action['users'][$i]['userentryid']),
							PR_DISPLAY_NAME => $action['users'][$i]['display_name'],
							PR_EMAIL_ADDRESS => $action['users'][$i]['username'],
							PR_SMTP_ADDRESS => $action['users'][$i]['emailaddress'],
							PR_ADDRTYPE => 'ZARAFA',
							PR_RECIPIENT_TYPE => MAPI_TO
						);
					}
				}
				mapi_message_modifyrecipients($newmsg, MODRECIP_ADD, $users);
			}
			mapi_savechanges($newmsg);

			return true;
		}

		function removeGroup($action){
			$root = mapi_msgstore_openentry($GLOBALS['mapisession']->getDefaultMessageStore(),  null);
			$rootProps = mapi_getProps($root, Array(PR_IPM_APPOINTMENT_ENTRYID));
			if(is_array($action['group_entry_id'])){
				for($i=0;$i<count($action['group_entry_id']);$i++){
					$action['group_entry_id'][$i] = hex2bin($action['group_entry_id'][$i]);
				}
			}else{
				$action['group_entry_id'] = hex2bin($action['group_entry_id']);
			}
			$result = $GLOBALS["operations"]->deleteMessages($GLOBALS['mapisession']->getDefaultMessageStore(), $rootProps[PR_IPM_APPOINTMENT_ENTRYID], $action['group_entry_id']);

			return true;
		}


	}
	
?>