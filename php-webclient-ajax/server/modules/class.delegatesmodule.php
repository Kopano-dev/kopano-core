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
	 * DelegatesModule
	 * It extends the Module class.
	 * @author Anand Patel <a.maganbhai@zarafa.com>
	 */
	class DelegatesModule extends Module
	{
		/**
		 * @var array contains entryid's of all folders.
		 */
		var $folders;
		
		/**
		 * @var array contains delegate properties from special message(LocalFreebusy).
		 */
		var $messageProps;
		
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function DelegatesModule($id, $data)
		{
			$this->folders = array();
			$this->messageProps = array();
			$this->entryIdObj = new EntryId();
			parent::Module($id, $data);
		}
		
		/**
		 * Executes all the actions in the $data variable.
		 * @return boolean true on success of false on failure.
		 */
		function execute()
		{
			$result = false;
			foreach($this->data as $action)
			{
				if(isset($action["attributes"]) && isset($action["attributes"]["type"])) {
					$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
					// Set folder entryids
					$this->getFolderEntryIDS($store);
					switch($action["attributes"]["type"])
					{
						case "list":
							$result = $this->delegateList($store);
							if($result != NOERROR) {
								// return error
								$data = array();
								$data["attributes"] = array("type" => "error");
								$data["error"] = array();
								$data["error"]["message"] = _("Unable to get Delegates information.");

								array_push($this->responseData["action"], $data);
								$GLOBALS["bus"]->addData($this->responseData);
								$result = true;
							}
							break;
						case "save":
							$result = $this->save($store, $action);
							if($result != NOERROR) {
								// return error
								$data = array();
								$data["attributes"] = array("type" => "error");
								$data["error"] = array();
								$data["error"]["message"] = _("Unable to set Delegates permissions.");
	
								array_push($this->responseData["action"], $data);
								$GLOBALS["bus"]->addData($this->responseData);
								$result = true;
							} else {
								// Acknowledge client that data has been saved.
								$data = array();
								$data["attributes"] = array("type" => "saved");
								array_push($this->responseData["action"], $data);
								$GLOBALS["bus"]->addData($this->responseData);
							}
							break;
						case "newuserpermissions":
							$data = array();
							$data["attributes"] = array("type" => "newuserpermissions");
							$data["delegate"] = array();
							/**
							 * new user will not have delegate meeting rule flag set,
							 * but being cautious and checking it also
							 */
							// get delegate meeting rule
							$delegateMeetingRule = $this->getDelegateMeetingRule($store);

							if(is_array($delegateMeetingRule) && count($delegateMeetingRule) > 0) {
								array_push($data["delegate"], $this->getDelegatePermissions($store, hex2bin($action["entryid"]), $delegateMeetingRule));
							} else {
								array_push($data["delegate"], $this->getDelegatePermissions($store, hex2bin($action["entryid"])));
							}

							array_push($this->responseData["action"], $data);
							$GLOBALS["bus"]->addData($this->responseData);
							$result = true;
							break;
					}
				}
			}
			return $result;
		}
		
		/**
		 * Function which retrieves a list of delegates
		 * @param object $store MAPI Message Store Object
		 * @return boolean true on success or false on failure		 		 
		 */
		function delegateList($store)
		{
			$result = false;
			
			$message = $this->getLocalFreebusyMessage($store);
			if ($message) {
				$this->messageProps = mapi_getprops($message, array(PR_DELEGATES_SEE_PRIVATE, PR_SCHDINFO_DELEGATE_ENTRYIDS, PR_SCHDINFO_DELEGATE_NAMES));
				$data = array();
				$data["attributes"] = array("type" => "list");
				$data["delegate"] = array();

				// get delegate meeting rule
				$delegateMeetingRule = $this->getDelegateMeetingRule($store);

				// Get permissions of all delegates.
				if(isset($this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS])) {
					for($i = 0; $i < count($this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS]); $i++){
						if (is_array($delegateMeetingRule) && count($delegateMeetingRule) > 0)
							array_push($data["delegate"], $this->getDelegatePermissions($store, $this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS][$i], $delegateMeetingRule));
						else
							array_push($data["delegate"], $this->getDelegatePermissions($store, $this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS][$i]));
					}
				}

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
			}
			return $result;
		}
		
		/**
		 * Function which saves an delegates properties.
		 * @param object $store MAPI Message Store Object
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function save($store, $action)
		{
			$result = false;
			$message = $this->getLocalFreebusyMessage($store);

			if ($message) {
				$this->messageProps = mapi_getprops($message, array(PR_DELEGATES_SEE_PRIVATE, PR_SCHDINFO_DELEGATE_ENTRYIDS, PR_SCHDINFO_DELEGATE_NAMES));

				/**
				 * With each call of saving delegate list we are deleting previous delegate information
				 * and writing all delegate information from scratch, so to make this procedure
				 * synchronous with delegate meeting rule, we have to do same with delegate meeting rule,
				 * so every save call will delete delegate meeting rule and re-create it
				 */
				// Delete all previous delegates and their permissions.
				$this->deleteDelegate($store, $message);
				$this->deleteDelegateMeetingRule($store);

				// Initialize delegate properties
				$this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS] = array();
				$this->messageProps[PR_DELEGATES_SEE_PRIVATE] = array();
				$this->messageProps[PR_SCHDINFO_DELEGATE_NAMES] = array();

				if (isset($action["delegate"]) && is_array($action["delegate"])){
					$delegates = $action["delegate"];
					
					// when there is just one delegate in the permission, wrap it in an array
					if (!is_array($delegates[key($delegates)])){
						$delegates = array($delegates);
					}

					foreach ($delegates as $key => $delegate){
						$this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS][$key] = hex2bin($delegate["entryid"]);
						$this->messageProps[PR_DELEGATES_SEE_PRIVATE][$key] = (int)$delegate["see_private"];
						$this->messageProps[PR_SCHDINFO_DELEGATE_NAMES][$key] = $delegate["fullname"];

						// Now set delegate's permissions.
						$result = $this->setDelegatePermissions($store, $delegate);
						if ($result != NOERROR)
							return $result;
					}

					// set delegate meeting rule
					$result = $this->setDelegateMeetingRule($store, $delegates);
					if($result != NOERROR)
						return $result;
				}
				mapi_setprops($message, $this->messageProps);
				mapi_savechanges($message);
			}
			return $result;
		}
		
		/**
		 * Function which deletes all delegates information.
		 * @param object $store MAPI Message Store Object
		 * @param object $message MAPI Message Object of LocalFreebusy
		 * @param array $action the action data, sent by the client
		 */
		function deleteDelegate($store, $message)
		{
			$root = mapi_msgstore_openentry($store);
			$rootProps = mapi_getprops($root, array(PR_FREEBUSY_ENTRYIDS));
			$freebusy = mapi_msgstore_openentry($store, $rootProps[PR_FREEBUSY_ENTRYIDS][3]);

			if ($message && isset($this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS])) {
				for ($i = 0; $i < count($this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS]); $i++){
					// First delete permissons from all folders.					
					foreach ($this->folders as $folderName => $folderEntryID){
						$folder = mapi_msgstore_openentry($store, $folderEntryID);
						$folderProps = mapi_getprops($folder, array(PR_DISPLAY_NAME, PR_STORE_ENTRYID, PR_ENTRYID));
						$store = $GLOBALS["mapisession"]->openMessageStore($folderProps[PR_STORE_ENTRYID]);

						// delete current acl's
						$acls = array(
									array(
										"type" => ACCESS_TYPE_GRANT,
										"userid" => $this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS][$i],
										"rights" => 0,
										"state" => RIGHT_DELETED|RIGHT_AUTOUPDATE_DENIED
									)
								);
						mapi_zarafa_setpermissionrules($folder, $acls);

						if ($folderName == 'calendar' && isset($freebusy)) {
							mapi_zarafa_setpermissionrules($freebusy, $acls);
							mapi_savechanges($freebusy);
						}
						mapi_savechanges($folder);
					}
				}
				// Delete delegate properties.
				mapi_deleteprops($message, array(PR_DELEGATES_SEE_PRIVATE, PR_SCHDINFO_DELEGATE_ENTRYIDS, PR_SCHDINFO_DELEGATE_NAMES));
				mapi_savechanges($message);
			}
		}
		
		/**
		 * Function which retrieves permissions of specified user.
		 * @param object $store MAPI Message Store Object
		 * @param binary $userEntryId user entryid
		 * @return array permissions of user.
		 */
		function getDelegatePermissions($store, $userEntryId, $delegateMeetingRule = false)
		{	
			$delegateIndex = $this->getDelegateIndex($userEntryId);
			$userinfo = $this->getUserInfo($userEntryId); 
			// open the addressbook
			$ab = $GLOBALS["mapisession"]->getAddressbook();

			$delegate = array();
			$delegate["entryid"] = bin2hex($userEntryId);
			$delegate["fullname"] = $userinfo["fullname"];
			$delegate["see_private"] = $delegateIndex === false ? 0:$this->messageProps[PR_DELEGATES_SEE_PRIVATE][$delegateIndex];

			// set delegate meeting rule flag
			if($this->getDelegateMeetingRuleFlag($delegateMeetingRule, $userEntryId) === true) {
				$delegate["delegate_meeting_rule"] = "1";
			} else {
				$delegate["delegate_meeting_rule"] = "0";
			}

			$delegate["permissions"] = array("rights" => array());
			
			// Get delegate permissions from all folders
			foreach ($this->folders as $folderName => $folderEntryId){
				$folder = mapi_msgstore_openentry($store, $folderEntryId);
				// check if folder is rootFolder, then we need the permissions from the store
				$folderProps = mapi_getprops($folder, array(PR_DISPLAY_NAME, PR_STORE_ENTRYID));
				// Get all users who has permissions
				$grants = mapi_zarafa_getpermissionrules($folder, ACCESS_TYPE_GRANT);
				$rights = array("attributes" => array("foldername" => $folderName));
				// Find current delegate and get permission.

				foreach($grants as $id => $grant){
					$user = mapi_ab_openentry($ab, $grant["userid"]);
					if (mapi_last_hresult() == NOERROR){
						if ($this->entryIdObj->compareABEntryIds(bin2hex($userEntryId), bin2hex($grant["userid"]))) {
							$rights["_content"] = $grant["rights"];
						}
					}
				}
				// If couldn't find rights, then set default rights i.e 0
				if (empty($rights["_content"]))
					$rights["_content"] = 0;
				array_push($delegate["permissions"]["rights"], $rights);
			}
			return $delegate;
		}
		
		/**
		 * Function which sets object 'folders' which contains 
		 * entryIDs of all default folders
		 * @param object $store MAPI Message Store Object
		 */
		function getFolderEntryIDS($store)
		{
			// Get root store
			$root = mapi_msgstore_openentry($store, null);
			// Get Inbox folder
			$inbox = mapi_msgstore_getreceivefolder($store);
			$inboxprops = mapi_getprops($inbox, Array(PR_ENTRYID));
			// Get entryids of default folders.
			$rootStoreProps = mapi_getprops($root, array(PR_IPM_APPOINTMENT_ENTRYID, PR_IPM_TASK_ENTRYID, $inboxprops[PR_ENTRYID], PR_IPM_CONTACT_ENTRYID, PR_IPM_NOTE_ENTRYID, PR_IPM_JOURNAL_ENTRYID));
			
			$this->folders = array("calendar" 	=> $rootStoreProps[PR_IPM_APPOINTMENT_ENTRYID],
								   "tasks" 		=> $rootStoreProps[PR_IPM_TASK_ENTRYID],
								   "inbox" 		=> $inboxprops[PR_ENTRYID],
								   "contacts"	=> $rootStoreProps[PR_IPM_CONTACT_ENTRYID],
								   "notes"		=> $rootStoreProps[PR_IPM_NOTE_ENTRYID],
								   "journal"	=> $rootStoreProps[PR_IPM_JOURNAL_ENTRYID]);
		}
		
		/**
		 * Function which returns delegate Index. Delegates properties in LocalFreeBusy 
		 * are multivalued properties.
		 * @param binary $entryid
		 * @return integer index of delegate or false if not found
		 */
		function getDelegateIndex($entryid)
		{
			// Check if user is existing delegate.
			if(isset($this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS])) {
				$eidstr = bin2hex($entryid);
				for($i=0; $i<count($this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS]); $i++) {
					if ($this->entryIdObj->compareABEntryIds($eidstr, bin2hex($this->messageProps[PR_SCHDINFO_DELEGATE_ENTRYIDS][$i]))) {
						return $i;
					}
				}
			}
			return false;
		}
		
		/**
		 * Function which sets permissions of one delegate.
		 * @param object $store MAPI Message Store Object
		 * @param array $delegate delegate's permissions
		 * @return boolean true on success or false on failure
		 */
		function setDelegatePermissions($store, $delegate)
		{
			$root = mapi_msgstore_openentry($store);
			$rootProps = mapi_getprops($root, array(PR_FREEBUSY_ENTRYIDS));
			$freebusy = mapi_msgstore_openentry($store, $rootProps[PR_FREEBUSY_ENTRYIDS][3]);

			// Get all folders and set permissions.
			foreach ($this->folders as $folderName => $folderEntryID){
				$folder = mapi_msgstore_openentry($store, $folderEntryID);

				// Set new permissions.
				$acls = array(
							array(
								"type" => (int)ACCESS_TYPE_GRANT,
								"userid" => hex2bin($delegate["entryid"]),
								"rights" => (int)$delegate["permissions"][$folderName],
								"state" => RIGHT_NEW | RIGHT_AUTOUPDATE_DENIED
							)
						);
				mapi_zarafa_setpermissionrules($folder, $acls);
				if(mapi_last_hresult() != NOERROR) {
					// unable to set permissions
					return mapi_last_hresult();
				}
				mapi_savechanges($folder);

				if ($folderName == 'calendar'){
					if(isset($freebusy)){
						// set permissions on free/busy message
						$acls[0]["rights"] |= ecRightsReadAny | ecRightsFolderVisible;
						mapi_zarafa_setpermissionrules($freebusy, $acls);
						if(mapi_last_hresult() != NOERROR) {
							// unable to set permissions
							return mapi_last_hresult();
						}
						mapi_savechanges($freebusy);
					}
				}
			}
		}
		
		/**
		 * Function which retrieves 'LocalFreebusy' message
		 * which contains delegate properties.
		 * @param object $store MAPI message store
		 * @return resource localfreebusy message
		 */
		function getLocalFreebusyMessage($store)
		{
			// Get 'LocalFreeBusy' message from FreeBusy Store
			$root = mapi_msgstore_openentry($store, null);							 
			$storeProps = mapi_getprops($root, array(PR_FREEBUSY_ENTRYIDS));
			$message = mapi_msgstore_openentry($store, $storeProps[PR_FREEBUSY_ENTRYIDS][1]);
			if (mapi_last_hresult() == NOERROR){
				return $message;
			}
			return false;
		}
		
		/** 
         * Function which retrieves information of specified user. 
         * @param binary $userentryid 
         * @return array user information 
         */ 
        function getUserInfo($userentryid) 
        { 
            // default return stuff 
            $result = array("fullname"=>_("Unknown user/group"), 
                            "entryid"=>null, 
                            ); 
 
            // open the addressbook 
            $ab = $GLOBALS["mapisession"]->getAddressbook(); 
            // try userid as normal user 
            $user = mapi_ab_openentry($ab, $userentryid); 
 
            if ($user){ 
                $props = mapi_getprops($user, array(PR_DISPLAY_NAME)); 
                $result["fullname"] = windows1252_to_utf8($props[PR_DISPLAY_NAME]); 
                $result["entryid"] = bin2hex($userentryid); 
            }
            return $result; 
        }

		/** 
		 * Function which creates/modifies delegate meeting rule in user store
		 * to send meeting request mails to delegates also
		 * @param		resource		$store		user's store
		 * @param		array			$delegates	all delegate information
		 * @return									on success NOERROR and on failing mapiError
		 */
		function setDelegateMeetingRule($store, $delegates)
		{
			$usersInfo = Array();

			// open addressbook to get information of all users
			$addrBook = $GLOBALS["mapisession"]->getAddressbook(); 

			// get all users which has set delegate_meeting_rule flag
			foreach($delegates as $key => $delegate) {
				if(isset($delegate["delegate_meeting_rule"]) && $delegate["delegate_meeting_rule"] == "1") {
					// get user info, using entryid
					$user = mapi_ab_openentry($addrBook, hex2bin($delegate["entryid"]));
					$userProps = mapi_getprops($user, Array(PR_ENTRYID, PR_ADDRTYPE, PR_EMAIL_ADDRESS, PR_DISPLAY_NAME, PR_SEARCH_KEY, PR_SMTP_ADDRESS, PR_OBJECT_TYPE, PR_DISPLAY_TYPE));

					// add recipient type prop, to specify type of recipient in mail
					$userProps[PR_RECIPIENT_TYPE] = MAPI_TO;

					if(mapi_last_hresult() == NOERROR && is_array($userProps)) {
						$usersInfo[] = $userProps;
					}
				}
			}

			// only create delegate meeting rule if any delegate has set the flag
			if(is_array($usersInfo) && count($usersInfo) > 0) {
				// create new meeting rule for delegates
				return $this->createDelegateMeetingRule($store, $usersInfo);
			}

			return mapi_last_hresult();
		}

		/**
		 * Function will get delegate meeting rule if it is present
		 * @param		resource		$store		user's store
		 * @return		array						properties of delegate meeting rule
		 */
		function getDelegateMeetingRule($store)
		{
			$inbox = mapi_msgstore_getreceivefolder($store);

			if(mapi_last_hresult() != NOERROR)
				return false;

			// get IExchangeModifyTable intergface
			$rulesTable = mapi_folder_openmodifytable($inbox);

			// get delegate meeting rule
			$rulesTable = mapi_rules_gettable($rulesTable);
			mapi_table_restrict($rulesTable,
											Array(RES_CONTENT,
												Array(
														FUZZYLEVEL	=>	FL_FULLSTRING | FL_IGNORECASE,
														ULPROPTAG	=>	PR_RULE_PROVIDER,
														VALUE		=>	Array(
																			PR_RULE_PROVIDER	=>	"Schedule+ EMS Interface"
																		)
												)
											)
								);

			// get all properties of rule
			$properties = Array(PR_RULE_ACTIONS, PR_RULE_CONDITION, PR_RULE_ID, PR_RULE_LEVEL, PR_RULE_NAME, PR_RULE_PROVIDER, PR_RULE_PROVIDER_DATA, PR_RULE_SEQUENCE, PR_RULE_STATE, PR_RULE_USER_FLAGS);
			// there will be only one rule, so fetch that only
			$delegateMeetingRule = mapi_table_queryrows($rulesTable, $properties, 0, 1);

			return is_array($delegateMeetingRule) && count($delegateMeetingRule) > 0 ? $delegateMeetingRule[0] : false;
		}

		/**
		 * Function will get delegate meeting rule for a user
		 * @param		array		$delegateMeetingRule		delegate meeting rule properties
		 * @param		HexString	$userEntryID				entryid of user to check for flag
		 * @return		boolean									true if flag is set otherwise false
		 */
		function getDelegateMeetingRuleFlag($delegateMeetingRule, $userEntryId)
		{
			if($delegateMeetingRule) {
				$ruleAction = $delegateMeetingRule[PR_RULE_ACTIONS][0];
				if($ruleAction) {
					$adrlist = $ruleAction["adrlist"];

					// check if user exists in addresslist for rule
					foreach($adrlist as $key => $user) {
						if($this->entryIdObj->compareABEntryIds(bin2hex($user[PR_ENTRYID]), bin2hex($userEntryId))) {
							return true;
						}
					}
				}
			}

			return false;
		}

		/**
		 * Function will create a new delegate meeting rule if it is not present
		 * @param		resource		$store		user's store
		 * @param		array			$usersInfo	user properties which should be added in PR_RULE_ACTIONS
		 * @return									on success NOERROR and on failure mapi error
		 */
		function createDelegateMeetingRule($store, $usersInfo)
		{
			$inbox = mapi_msgstore_getreceivefolder($store);

			if(mapi_last_hresult() != NOERROR)
				return mapi_last_hresult();

			// get IExchangeModifyTable intergface
			$rulesTable = mapi_folder_openmodifytable($inbox);

			if(mapi_last_hresult() != NOERROR)
				return mapi_last_hresult();

			// create new rule
			$rule = Array();
			// no need to pass rule_id when creating new rule
			$rule[PR_RULE_ACTIONS] = Array(
										Array(
											"action" => OP_DELEGATE,
											"flavor" => 0,
											"flags" => 0,
											"adrlist" => $usersInfo
										)
									);
			$rule[PR_RULE_CONDITION] = Array(RES_AND,
											Array(
												Array(RES_CONTENT,
													Array(
														FUZZYLEVEL => FL_PREFIX,
														ULPROPTAG => PR_MESSAGE_CLASS,
														VALUE => Array(PR_MESSAGE_CLASS => "IPM.Schedule.Meeting")
													)
												),
												Array(RES_NOT,
													Array(
														Array(RES_EXIST,
															Array(
																ULPROPTAG => PR_DELEGATED_BY_RULE
															)
														)
													)
												),
												Array(RES_OR,
													Array(
														Array(RES_NOT,
															Array(
																Array(RES_EXIST,
																	Array(
																		ULPROPTAG => PR_SENSITIVITY
																	)
																)
															)
														),
														Array(RES_PROPERTY,
															Array(
																RELOP => RELOP_NE,
																ULPROPTAG => PR_SENSITIVITY,
																VALUE => Array(PR_SENSITIVITY => 2)
															)
														)
													)
												),
											)
										);
			$rule[PR_RULE_NAME] = "Delegate Meetingrequest service";
			$rule[PR_RULE_PROVIDER_DATA] = "";		// 0 byte binary string
			$rule[PR_RULE_STATE] = ST_ENABLED;
			$rule[PR_RULE_LEVEL] = 0;
			$rule[PR_RULE_SEQUENCE] = 0;
			$rule[PR_RULE_PROVIDER] = "Schedule+ EMS Interface";
			$rule[PR_RULE_USER_FLAGS] = 0;

			$rows = Array(
						0 => Array(
								"rowflags" => ROW_ADD,
								"properties" => $rule
							)
					);

			$result = mapi_rules_modifytable($rulesTable, $rows);

			return mapi_last_hresult();
		}

		/**
		 * Function will delete existing delegate meeting rule
		 * @param		resource		$store		user's store
		 * @return		integer						mapi error or 0
		 */
		function deleteDelegateMeetingRule($store)
		{
			$delegateMeetingRule = $this->getDelegateMeetingRule($store);

			if(is_array($delegateMeetingRule) && count($delegateMeetingRule) > 0) {
				$inbox = mapi_msgstore_getreceivefolder($store);

				if(mapi_last_hresult() != NOERROR)
					return mapi_last_hresult();

				// get IExchangeModifyTable intergface
				$rulesTable = mapi_folder_openmodifytable($inbox);

				if(mapi_last_hresult() != NOERROR)
					return mapi_last_hresult();

				$rows = Array(
							0 => Array(
									"rowflags" => ROW_REMOVE,
									"properties" => $delegateMeetingRule
								)
						);

				$result = mapi_rules_modifytable($rulesTable, $rows);
			}

			return mapi_last_hresult();
		}
	}