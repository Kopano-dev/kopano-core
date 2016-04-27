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
	 * ResolveNames Module
	 */
	class ResolveNamesModule extends Module
	{
		/**
		 * Constructor
		 */
		function ResolveNamesModule($id, $data)
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
				
					switch($action["attributes"]["type"])
					{
						case "checknames":
							$result = $this->checkNames($store, $action);
							break;
					}
				}
			}
		}
		
		/**
		 * Function which checks the names, sent by the client. This function is used
		 * when a user wants to sent an email and want to check the names filled in
		 * by the user in the to, cc and bcc field. This function uses the global
		 * user list of Zarafa to check if the names are correct.		 		 		 
		 * @param object $store MAPI Message Store Object
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function checkNames($store, $action)
		{
			$result = false;
			
			if(isset($action["name"])) {
				$data = array();
				$data["attributes"] = array("type" => "checknames");
				$data["name"] = array();
				$excludeusercontacts= $action["excludeusercontacts"];
				
				$names = $action["name"];
				if(!is_array($names)) {
					$names = array($names);
				} 
				// if excludeusercontacts is set to true, user Contact's folder will also be checked to resolve the given name
				if(!$excludeusercontacts){
					// get all contact folders from own store, exclude subfolders
					$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
					$subtreeEntryid = mapi_getprops($store, array(PR_IPM_SUBTREE_ENTRYID));
					$ipmSubtree = mapi_msgstore_openentry($store, $subtreeEntryid[PR_IPM_SUBTREE_ENTRYID]);
					$hierarchyTable = mapi_folder_gethierarchytable($ipmSubtree, MAPI_DEFERRED_ERRORS);

					$restriction = array(
										RES_CONTENT,
											array(
												FUZZYLEVEL => FL_FULLSTRING,
												ULPROPTAG => PR_CONTAINER_CLASS,
												VALUE => array(
															PR_CONTAINER_CLASS => "IPF.Contact"
														)
											)
									);

					mapi_table_restrict($hierarchyTable, $restriction, TBL_BATCH);
					$contactFolderEntryIds = mapi_table_queryrows($hierarchyTable, array(PR_ENTRYID), 0, 0x7fffffff);

					$contactFolders = array();
					for($index = 0; $index < count($contactFolderEntryIds); $index++) {
						array_push($contactFolders, mapi_msgstore_openentry($store, $contactFolderEntryIds[$index][PR_ENTRYID]));
					}
				}

				// open addressbook
				$ab = $GLOBALS["mapisession"]->getAddressbook();
				$ab_entryid = mapi_ab_getdefaultdir($ab);
				$ab_dir = mapi_ab_openentry($ab,$ab_entryid);

				// check names
				foreach($names as $query){
					if (is_array($query) && isset($query["id"]) && !empty($query["id"]) && isset($query["type"]) && !empty($query["type"])){
						$id = $query["id"];
						$type = $query["type"];

						$result = $this->searchAddressBook($ab, $ab_dir, $id, $type);

						if($excludeusercontacts){
							foreach($result as $name){
								// check only for users not for groups
								if($name["objecttype"] == 6)
									array_push($data["name"],$name);
							}
						}else{// will search user contact folder to resolve the give name
							$data["name"] = array_merge($data["name"], $result);
						
							for($index = 0; $index < count($contactFolders); $index++) {
								$result = $this->searchContactFolder($contactFolders[$index], $id, $type, $store);
								$data["name"] = array_merge($data["name"], $result);
							}
						}
					}
				}

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
			}
			
			return $result;
		}

		/**
		* This function searches the addressbook specified for users and returns an array with data
		* Please note that the returning array must be UTF8
		*
		*@param $ab_dir The addressbook
		*@param $query The search query, case is ignored
		*@param $type The type of search, this is returned in the result array
		*/
		function searchAddressBook($ab, $ab_dir, $query, $type)
		{
		    // First, try an addressbook lookup
		    $rows = mapi_ab_resolvename($ab, array ( array(PR_DISPLAY_NAME => u2w($query)) ) , 0 );
		    
		    if(!$rows && mapi_last_hresult() == MAPI_E_AMBIGUOUS_RECIP) {
		        // Ambiguous, show possiblities:
		    
                $table = mapi_folder_getcontentstable($ab_dir);

                // only return users from who the displayName or the username starts with $name
                // TODO: use PR_ANR for this restriction instead of PR_DISPLAY_NAME and PR_ACCOUNT
                $restriction = array(RES_AND, 
                                    array(
                                        array(RES_OR, 
                                            array(
                                                array(
                                                    RES_CONTENT,
                                                        array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
                                                            ULPROPTAG=>PR_DISPLAY_NAME,
                                                            VALUE=>utf8_to_windows1252($query)
                                                        )
                                                    ),
                                                array(
                                                    RES_CONTENT,
                                                        array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
                                                            ULPROPTAG=>PR_ACCOUNT,
                                                            VALUE=>utf8_to_windows1252($query)
                                                        )
                                                    ),
                                            ), // RES_OR
                                        ),
										array(RES_OR,
											array(
												array(
													RES_PROPERTY,
														array(RELOP=>RELOP_EQ,
															ULPROPTAG=>PR_OBJECT_TYPE,
															VALUE=>MAPI_MAILUSER
														)
													),
												array(
													RES_PROPERTY,
														array(RELOP=>RELOP_EQ,
															ULPROPTAG=>PR_OBJECT_TYPE,
															VALUE=>MAPI_DISTLIST
													)
												)
											)
										)
                                    ) // RES_AND
                                );
                mapi_table_restrict($table, $restriction);
                mapi_table_sort($table, array(PR_DISPLAY_NAME => TABLE_SORT_ASCEND));
                
				$tableItems = mapi_table_queryrows($table, array(PR_ACCOUNT, PR_DISPLAY_NAME, PR_ENTRYID, PR_OBJECT_TYPE, PR_SMTP_ADDRESS, PR_OBJECT_TYPE), 0, 0x7fffffff);

				$items = array();
				foreach($tableItems as $user_data) {
					$item = array();

					$item["username"] = w2u($user_data[PR_ACCOUNT]);
					$item["fullname"] = w2u($user_data[PR_DISPLAY_NAME]);
					$item["emailaddress"] = w2u($user_data[PR_SMTP_ADDRESS]);
					$item["objecttype"] = $user_data[PR_OBJECT_TYPE];
					$item["nameid"] = $query;
					$item["nametype"] = $type;

					array_push($items, $item);
				}
			} else if(!$rows) {
				$items = array(); // Nothing found
			} else {
				// Item found, get details from AB
				$abitem = mapi_ab_openentry($ab, $rows[0][PR_ENTRYID]);
				$user_data = mapi_getprops($abitem, array(PR_ACCOUNT, PR_DISPLAY_NAME, PR_ENTRYID, PR_EMAIL_ADDRESS, PR_SMTP_ADDRESS, PR_OBJECT_TYPE));
				
				$item = array();
				$item["username"] = w2u($user_data[PR_ACCOUNT]);
				$item["fullname"] = w2u($user_data[PR_DISPLAY_NAME]);
				$item["emailaddress"] = w2u($user_data[PR_SMTP_ADDRESS]);
				$item["objecttype"] = $user_data[PR_OBJECT_TYPE];
				$item["nameid"] = $query;
				$item["nametype"] = $type;

				$items = array($item);
			}
			
			return $items;
		}

		/**
		* This function searches the contact folder specified for users and returns an array with data
		* Please note that the returning array must be UTF8
		*
		*@param $folder The opened folder to search, normaly this is a contactsfolder
		*@param $query The search query, case is ignored
		*@param $type The type of search, this is returned in the result array
		*/
		function searchContactFolder($folder, $query, $type, $store=false)
		{
			$table = mapi_folder_getcontentstable($folder);
			
			$properties = $GLOBALS["properties"]->getContactProperties();

			// only return users from who the displayName or the username starts with $name
			// TODO: use PR_ANR for this restriction instead of PR_DISPLAY_NAME and PR_ACCOUNT
			$restriction = array(RES_AND, 
								array(
									array(RES_OR, 
										array(
											array(
												RES_CONTENT,
													array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
														ULPROPTAG=>$properties["display_name"],
														VALUE=>utf8_to_windows1252($query)
													)
												),
											array(
												RES_CONTENT,
													array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
														ULPROPTAG=>$properties["fileas"],
														VALUE=>utf8_to_windows1252($query)
													)
												),
											array(
												RES_CONTENT,
													array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
														ULPROPTAG=>$properties["email_address_display_name_1"],
														VALUE=>utf8_to_windows1252($query)
													)
												),
											array(
												RES_CONTENT,
													array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
														ULPROPTAG=>$properties["email_address_display_name_2"],
														VALUE=>utf8_to_windows1252($query)
													)
												),
											array(
												RES_CONTENT,
													array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
														ULPROPTAG=>$properties["email_address_display_name_3"],
														VALUE=>utf8_to_windows1252($query)
													)
												),
										), // RES_OR
									),
									array(RES_OR,
										array(
											array(
												RES_PROPERTY,
													array(RELOP=>RELOP_EQ,
														ULPROPTAG=>$properties["message_class"],
														VALUE=>"IPM.Contact"
													)
												),
											array(
												RES_PROPERTY,
													array(RELOP=>RELOP_EQ,
														ULPROPTAG=>$properties["message_class"],
														VALUE=>"IPM.DistList"
													)
												)
											)
										)
								) // RES_AND
							);
			mapi_table_restrict($table, $restriction);
			mapi_table_sort($table, array($properties["fileas"] => TABLE_SORT_ASCEND));

			// get all matching contacts
			$rows =  mapi_table_queryrows($table, $properties, 0, 0x7fffffff);

			$items = array();
			foreach ($rows as $row) {
				if($row[$properties["message_class"]] == "IPM.DistList"){
					$item = array();
					$item["username"] = w2u($row[$properties["fileas"]]);
					$item["fullname"] = w2u($row[$properties["display_name"]]);
					$item["message_class"] = $row[$properties["message_class"]];
					$members = $this->getMembersFromDistributionList($store, $row[$properties["entryid"]], array(), $properties);
					$item["members"] = array("member"=>$members);
					$item["nameid"] = $query;
					$item["nametype"] = $type;
					array_push($items, $item);
				}else{
					for ($email=1; $email<=3; $email++){
						if (isset($row[$properties["email_address_".$email]]) && !empty($row[$properties["email_address_".$email]])){
							$item = array();

							$item["username"] = w2u($row[$properties["fileas"]]);
							if (isset($row["email_address_display_name_".$email])){
								$item["fullname"] = w2u($row[$properties["email_address_display_name_".$email]]);
							}else{
								$item["fullname"] = w2u($row[$properties["display_name"]]);
							}
							$item["emailaddress"] = w2u($row[$properties["email_address_".$email]]);
							$item["nameid"] = $query;
							$item["nametype"] = $type;
							$item["objecttype"] = MAPI_MAILUSER;

							array_push($items, $item);
						}
					}
				}
			}
			return $items;
		}

		/**
		 * Function which fetches all members of a distribution list recursively.
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the distribution list
		 * @return object $items all members of a distlist.
		 */
		function getMembersFromDistributionList($store, $entryid, $listEntryIDs = array(), $properties)
		{
			if (in_array($entryid, $listEntryIDs)){ // don't expand a distlist that is already expanded
				return array();
			}

			$listEntryIDs[] = $entryid;

			$message = mapi_msgstore_openentry($store, $entryid);
			$items = array();
			if($message) {
				$props = mapi_getprops($message, array($properties["oneoff_members"], $properties["members"]));
				if(isset($props[$properties["oneoff_members"]])) {
					$members = $props[$properties["members"]];
				}

				// parse oneoff members
				$oneoffmembers = array();
				foreach($props[$properties["oneoff_members"]] as $key=>$item){
					$oneoffmembers[$key] = mapi_parseoneoff($item);
				}
				
				foreach($members as $key=>$item){
					$parts = unpack("Vnull/A16guid/Ctype/A*entryid", $item);
					if ($parts["guid"]==hex2bin("812b1fa4bea310199d6e00dd010f5402")){ // custom e-mail address (no user or contact)
						// $parts can not be used for this guid because it is a one off entryid
						$oneoff = mapi_parseoneoff($item);
						$item = array();
						$item["fileas"] = w2u($oneoff["name"]);
						$item["display_name"] = $item["fileas"];
						$item["type"] = w2u($oneoff["type"]);
						$item["emailaddress"] = w2u($oneoff["address"]);
						$items[] = $item;
					}else{
						// $parts can be another personal distribution list, an electronic address contained in a contact, a GAL member, a distribution list, or a one-off e-mail address.
						// see msdn documentation [OXOCNTC] WrappedEntryId structure
						switch($parts["type"]){
							case 0: //one off
								$oneoff = mapi_parseoneoff($parts["entryid"]);
								$item = array();
								$item["fileas"] = w2u($oneoff["name"]);
								$item["display_name"] = $item["fileas"];
								$item["type"] = w2u($oneoff["type"]);
								$item["emailaddress"] = w2u($oneoff["address"]);
								$items[] = $item;
								break;
							case DL_USER: // contact
								$msg = mapi_msgstore_openentry($store, $parts["entryid"]);
								if (mapi_last_hresult()!=NOERROR) // contact could be deleted, skip item
									continue;
								$msgProps = mapi_getprops($msg, $properties);
								$item = array();
								$item["entryid"] = bin2hex($parts["entryid"]);
								$item["display_name"] = w2u($msgProps[$properties["display_name"]]);
								$item["emailaddress"] = w2u($oneoffmembers[$key]["address"]);

								// use the email adress from the OneOff entry	
								$item["type"] = w2u($oneoffmembers[$key]["type"]);
								$items[] = $item;	
								break;
							case DL_USER_AB: // ab user
								$ab = $GLOBALS["mapisession"]->getAddressbook();
								$msg = mapi_ab_openentry($ab,$parts["entryid"]);
								if (mapi_last_hresult()!=NOERROR) // user could be deleted, skip item
									continue;
								$properties = array_merge($properties, array("smtp_address" => PR_SMTP_ADDRESS));
								$msgProps = mapi_getprops($msg, $properties);
								$item = array();
								$item["entryid"] = bin2hex($parts["entryid"]);
								$item["display_name"] = w2u($msgProps[$properties["display_name"]]);

								// don't use the email adress from the OneOff entry because 
								// it will give a ZARAFA mail address, and in the webaccess we currently only support SMTP addresses
								$item["type"] = "SMTP";
								$item["emailaddress"] = w2u($msgProps[$properties["smtp_address"]]);

								$items[] = $item;
								break;

							case DL_DIST: // dist list
								$items = array_merge($items, $this->getMembersFromDistributionList($store, $parts["entryid"], $listEntryIDs, $properties));
								break;

							case DL_DIST_AB: // group
								$group = mapi_ab_openentry($GLOBALS["mapisession"]->getAddressbook(), $parts["entryid"]);
								$groupProps = mapi_getprops($group, $properties);
								$item = array();
								$item["entryid"] = bin2hex($parts["entryid"]);
								$item["display_name"] = w2u($groupProps[$properties["display_name"]]);
								$item["type"] = "ZARAFA";
								if(isset($groupProps[PR_EMAIL_ADDRESS])) 
									$item["emailaddress"] = w2u($groupProps[PR_EMAIL_ADDRESS]);
								$items[] = $item;
								break;
						}
					}
				}
			}
			return $items;
		}
	}
?>
