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
	 * Addressbook Module
	 */
	class AddressbookListModule extends ListModule
	{
		/**
		 * @var Array properties used to get data from addressbook contact folders
		 */
		var $properties = null;

		/**
		 * @var Array properties used to get data from global address book
		 */
		var $ab_properties = null;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function AddressbookListModule($id, $data)
		{
			// Default Columns
			$this->tablecolumns = $GLOBALS["TableColumns"]->getContactABListTableColumns();
			$this->ab_tablecolumns = $GLOBALS["TableColumns"]->getAddressBookListTableColumns();

			parent::ListModule($id, $data, array());
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

					switch($action["attributes"]["type"])
					{
						case "hierarchy":
							$result = $this->getHierarchy($action);
							break;
						case "contacts":
							$result = $this->messageList($store, $entryid, $action);
							break;
						case "globaladdressbook":
							$result = $this->GABUsers($action);
							break;
					}
				}
			}
			
			return $result;
		}
		
		/**
		 * Function which retrieves a list of contacts in a contact folder
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the folder
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */
		function messageList($store, $entryid, $action)
		{
			$result = false;
			$searchPerformed = false;				// flag to check the request is for search or not
			$disableFullContactlist = false;		// flag for disabling the contact list

			if(!$entryid) {
				$rootcontainer = mapi_msgstore_openentry($store);
				if($rootcontainer) {
					$props = mapi_getprops($rootcontainer, array(PR_IPM_CONTACT_ENTRYID));
					
					if(isset($props[PR_IPM_CONTACT_ENTRYID])) {
						$entryid = $props[PR_IPM_CONTACT_ENTRYID];
					}
				}
			}

			if($store && $entryid) {
				$restriction = array(RES_AND, // get contacts when having one or more email adress
									array(
										array(RES_PROPERTY,
											array(	RELOP=>RELOP_EQ,
													ULPROPTAG=>PR_MESSAGE_CLASS,
													VALUE => array(PR_MESSAGE_CLASS=>"IPM.Contact")
											)
										),
										array(RES_PROPERTY,
											array(	RELOP => RELOP_GT,
													ULPROPTAG => $this->properties["address_book_long"],
													VALUE => array($this->properties["address_book_long"] => 0)
											)
										)
									)
								);

				// include distribution lists when requested
				if (isset($action["groups"]) && $action["groups"] != "no"){
					$restriction = 	array(RES_OR,
										array(
											array(RES_PROPERTY, // get distribution lists
												array(	RELOP=>RELOP_EQ,
														ULPROPTAG=>PR_MESSAGE_CLASS,
														VALUE => array(PR_MESSAGE_CLASS=>"IPM.DistList")
												)
											),
											$restriction,
										)
									);
				}

				if(isset($action["restriction"])) {
					if(isset($action["restriction"]["searchstring"])) {
						$searchPerformed = true;
						$restrictions = array();
						$props = array($this->properties["fileas"], 
									   $this->properties["display_name"],
									   $this->properties["email_address_1"],
									   $this->properties["email_address_display_name_1"],
									   $this->properties["email_address_2"],
									   $this->properties["email_address_display_name_2"],
									   $this->properties["email_address_3"],
									   $this->properties["email_address_display_name_3"]);
						
						foreach($props as $property)
						{
							array_push($restrictions, array(RES_CONTENT,
															array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
																ULPROPTAG => $property,
																VALUE => u2w($action["restriction"]["searchstring"]))));
						}

						$restrictions = array(RES_OR, $restrictions);
						$restriction = array(RES_AND, array($restriction, $restrictions));
					} else if (isset($action["restriction"]["pagination_character"]) && $action["restriction"]["pagination_character"] != "...") {
						// If request is for gab pagination then set pagination restriction.
						$searchPerformed = true;

						// Get pagination restriction and add it in restriction array.
						$paginationRestriction = $this->getPaginationRestriction($action);
						if($paginationRestriction)
							$restriction = array(RES_AND, array($restriction, $paginationRestriction));
 					}
				}
				
				if(isset($action["sort"]) && isset($action["sort"]["column"])) {
					$this->sort = array();
					
					foreach($action["sort"]["column"] as $column)
					{
						if(isset($column["attributes"]) && isset($column["attributes"]["direction"])) {
							switch(strtolower($column["attributes"]["direction"]))
							{
								case "asc":
									if(isset($this->properties[$column["_content"]])) {
										$this->sort[$this->properties[$column["_content"]]] = TABLE_SORT_ASCEND;
									}
									break;
								case "desc":
									if(isset($this->properties[$column["_content"]])) {
										$this->sort[$this->properties[$column["_content"]]] = TABLE_SORT_DESCEND;
									}
									break;
							}
						}
					}
				}
				
				// We map the 'email_address' column to 'email_address_1' for sorting. This is a bit of a hack
				// because we are showing either email_address_1 or _2 or _3 ...
				
				$map = array();
				$map["email_address"] = $this->properties["email_address_1"];
				
				$this->parseSortOrder($action, $map, true);

				$data = array();
				$data["attributes"] = array("type" => "list");
				$data["column"] = $this->tablecolumns;
				$data["sort"] = $this->generateSortOrder($map);

				// open contacts folder
				$folder = mapi_msgstore_openentry($store, $entryid);
				if(mapi_last_hresult() == NOERROR) {
					$contentsTable = mapi_folder_getcontentstable($folder, MAPI_DEFERRED_ERRORS);

					/**
					 * Check whether we have to disable the full contactlist listing using the config: 
					 * DISABLE_FULL_CONTACTLIST_THRESHOLD. we are checking here if in case 
					 * mapi_table_getrowcount returns number greater then this config value then we
					 * don't have to do any processing, so it will lessen processing time
					 */
					if(DISABLE_FULL_CONTACTLIST_THRESHOLD != -1 && !$searchPerformed) {
						if(DISABLE_FULL_CONTACTLIST_THRESHOLD >= 0 && mapi_table_getrowcount($contentsTable) > DISABLE_FULL_CONTACTLIST_THRESHOLD) {
							$disableFullContactlist = true;
						}
					}

					// apply restriction & sorting
					mapi_table_restrict($contentsTable, $restriction, TBL_BATCH);
					mapi_table_sort($contentsTable, $this->sort, TBL_BATCH);

					if(!$disableFullContactlist) {
						// Get all rows
						/**
						 * we can also use mapi_table_queryallrows but it internally calls queryrows with 
						 * start as 0 and end as 0x7fffffff, so we are directly calling mapi_table_queryrows
						 */
						$rows = mapi_table_queryrows($contentsTable, $this->properties, 0, 0x7fffffff);

						// Search for distribution lists and get all email adresses in the distribution lists.
						$items = array();
						foreach($rows as $row)
						{
							$item = Conversion::mapMAPI2XML($this->properties, $row);

							if (is_array($item["entryid"])){
								$item["entryid"] = $item["entryid"]["_content"];
							}

							$item["message_flags"] = MSGFLAG_READ; // in the addressbook we don't use message flags, but we want to show them as "read"
							if(isset($item["message_class"])) {
								switch($item["message_class"])
								{
									case "IPM.Contact":
										if(isset($item["address_book_mv"]) && is_array($item["address_book_mv"])){
											$email_addresses = $item["address_book_mv"];
											$entryid = $item["entryid"];

											foreach($email_addresses as $email_address)
											{
												$email_address += 1; // address_book_mv starts with 0, so we add '1' here

												if(isset($item["email_address_" . $email_address])) {
													$item["entryid"] = $entryid . "_" . $email_address;
													$item["email_address"] = $item["email_address_" . $email_address];
													$item["display_name"] = $item["email_address_display_name_" . $email_address];
													$item["addrtype"] = $item["email_address_type_" . $email_address];
													$item["email_address_number"] = $email_address;
													array_push($items, $item);
												}
											}
										}
										break;
									case "IPM.DistList":
										$entryid = hex2bin($item["entryid"]); // need real entryid here
										$item["email_address"] = $item["display_name"];
										$item["addrtype"] = "ZARAFA";
										$item["members"] = array("member"=>$this->expandDistributionList($store, $entryid));

										array_push($items, $item);
										break;
								}
							}
						}

						/**
						 * Check whether we have to disable the full contactlist listing using the config: 
						 * DISABLE_FULL_CONTACTLIST_THRESHOLD. We check this after we have looped through all the
						 * items (code above) becuase it could happen then after expanding the groups the number
						 * of rows crosses the limit. This is because we need to know how many rows the contactlist
						 * will contain. Also note that when the user conducts a search the list should be displayed 
						 * regardless of the number of items returned.
						 */
						if(DISABLE_FULL_CONTACTLIST_THRESHOLD != -1 && !$searchPerformed){
							if(DISABLE_FULL_CONTACTLIST_THRESHOLD >= 0 && count($items) > DISABLE_FULL_CONTACTLIST_THRESHOLD){
								$disableFullContactlist = true;
							}
						}
					}

					if(!$disableFullContactlist){
						$data = array_merge($data, array("item" => $items));
					}else{
						// Provide clue that full contactlist is disabled.
						$data = array_merge($data, array("disable_full_gab" => true));
					}

					array_push($this->responseData["action"], $data);
					$GLOBALS["bus"]->addData($this->responseData);

					$result = true;
				}
			}

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
			$searchstring = "";
			$paginationCharacter = "";
			if(isset($action["restriction"])) {
				if(isset($action["restriction"]["searchstring"])) {
					// Get search string for searching in AB.
					$searchstring = $action["restriction"]["searchstring"];
				} else if (isset($action["restriction"]["pagination_character"])) {
					// Get pagination character for AB.
					$paginationCharacter = $action["restriction"]["pagination_character"];
				}
			}

			$hide_users = isset($action["hide_users"]) ? $action["hide_users"] : false;
			$hide_groups = isset($action["hide_groups"]) ? $action["hide_groups"] : false;
			$hide_companies = isset($action["hide_companies"]) && $action["hide_companies"] == "true" ? true : false;

			$items = array();

			$data["page"] = array();
			$data["page"]["start"] = 0;
			$data["page"]["rowcount"] = 0;
			$data["page"]["totalrowcount"] = 0;

			$data = array();
			$data["attributes"] = array("type" => "list");
			$data["column"] = $this->ab_tablecolumns;

			$this->sort = array();
			$this->sort[$this->ab_properties['display_name']] = TABLE_SORT_ASCEND;
			
			$firstSortColumn = array_shift(array_keys($this->sort));

			$map = array();
			$map["fileas"] = $this->ab_properties['account'];

			// Parse incoming sort order
			$this->parseSortOrder($action, $map, true, $this->ab_properties);

			// Generate output sort
			$data["sort"] = array();
			$data["sort"] = $this->generateSortOrder($map, $this->ab_properties);

			if($paginationCharacter == "...")
				$paginationCharacter = "";

			if(!DISABLE_FULL_GAB || strlen($searchstring) > 0 || strlen($paginationCharacter) > 0) {
				$ab = $GLOBALS["mapisession"]->getAddressbook();

				if (isset($action["entryid"]) && $action["entryid"] != "") {
					$entryid = hex2bin($action["entryid"]);
				}else{
					$entryid = mapi_ab_getdefaultdir($ab);
				}

				$dir = mapi_ab_openentry($ab,$entryid);
				if (mapi_last_hresult()){
					return false;
				}

				$table = mapi_folder_getcontentstable($dir);

				$restriction = false;
				$tempRestriction = false;
				$userGroupRestriction = false;

				if($hide_users || $hide_groups || $hide_companies) {
					$usersRestriction = $this->createUsersRestriction($hide_users);
					$groupsRestriction = $this->createGroupsRestriction($hide_groups);
					$companyRestriction = $this->createCompanyRestriction($hide_companies);

					if($companyRestriction) {
						$userGroupRestriction = Array(RES_OR, Array($usersRestriction, $groupsRestriction, $companyRestriction));
					} else {
						$userGroupRestriction = Array(RES_OR, Array($usersRestriction, $groupsRestriction));
					}
				}

				if(strlen($searchstring) > 0){
					// create restriction for search
					// only return users from who the displayName or the username starts with $searchstring
					// TODO: use PR_ANR for this restriction instead of PR_DISPLAY_NAME and PR_ACCOUNT
					$tempRestriction = 	array(RES_OR, 
										array(
											array(
												RES_CONTENT,
													array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
														ULPROPTAG => PR_DISPLAY_NAME,
														VALUE => u2w($searchstring)
													)
												),
											array(
												RES_CONTENT,
													array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
														ULPROPTAG => PR_ACCOUNT,
														VALUE => u2w($searchstring)
													)
												),
										),
									);
				} else if(strlen($paginationCharacter) > 0) {
					// create restriction for alphabet bar
					$tempRestriction = $this->getPaginationRestriction($action);
 				}

				if($tempRestriction && $userGroupRestriction) {
					$restriction = Array(
										RES_AND,
											Array(
												$tempRestriction,				// restriction for search/alphabet bar
												$userGroupRestriction			// restriction for hiding users/groups
											)
									);
				} else if($tempRestriction) {
					$restriction = $tempRestriction;							// restriction for search/alphabet bar
				} else {
					$restriction = $userGroupRestriction;						// restriction for hiding users/groups
				}

				// Only add restriction when it is used
				if($restriction) {
					mapi_table_restrict($table, $restriction);
				}
				mapi_table_sort($table, $this->sort);

				// todo: fix paging stuff

				$data["page"]["start"] = 0;
				$data["page"]["rowcount"] = mapi_table_getrowcount($table);
				$data["page"]["totalrowcount"] = $data["page"]["rowcount"];

				$rows = mapi_table_queryrows($table, $this->ab_properties, 0, 0x7fffffff);
				
                for ($i = 0; $i < count($rows); $i++) {
                    $user_data = $rows[$i];
                    $item = array();
                    $item["entryid"] = bin2hex($user_data[$this->ab_properties["entryid"]]);
                    $item["display_name"] = w2u($user_data[$this->ab_properties["display_name"]]);
                    $item["fileas"] = w2u($user_data[$this->ab_properties["account"]]);
                    $item["objecttype"] = $user_data[$this->ab_properties["objecttype"]];

					switch($user_data[PR_DISPLAY_TYPE]){
                        case DT_ORGANIZATION:
							$item["display_type"] = $user_data[PR_DISPLAY_TYPE];
                            $item["email_address"] = w2u($user_data[$this->ab_properties["account"]]);
                            $item["message_flags"] = 0; // FIXME: we want companies to show "bold", so in fact this is some kind of a hack ;)
							$item["addrtype"] = "ZARAFA";
							$item["search_key"] = bin2hex($user_data[$this->ab_properties["search_key"]]);
                            break;

                        case DT_DISTLIST:
							// remove DTE_FLAG_ACL_CAPABLE flag from display_type_ex property
							$item["display_type"] = DTE_IS_ACL_CAPABLE($user_data[PR_DISPLAY_TYPE_EX]) ? $user_data[PR_DISPLAY_TYPE_EX] & ~DTE_FLAG_ACL_CAPABLE : $user_data[PR_DISPLAY_TYPE_EX];		// DT_AGENT or DT_DISTLIST or DT_SEC_DISTLIST
							$item["email_address"] = w2u($user_data[$this->ab_properties["account"]]);
							$item["smtp_address"] = isset($user_data[$this->ab_properties["smtp_address"]]) ? $user_data[$this->ab_properties["smtp_address"]] : "";
							$item["message_flags"] = 0; // FIXME: we want dist lists to show "bold", so in fact this is some kind of a hack ;)
							$item["addrtype"] = "ZARAFA";
							$item["search_key"] = bin2hex($user_data[$this->ab_properties["search_key"]]);
							break;

                        case DT_MAILUSER:
                        default:
							// remove DTE_FLAG_ACL_CAPABLE flag from display_type_ex property
							$item["display_type"] = DTE_IS_ACL_CAPABLE($user_data[PR_DISPLAY_TYPE_EX]) ? $user_data[PR_DISPLAY_TYPE_EX] & ~DTE_FLAG_ACL_CAPABLE : $user_data[PR_DISPLAY_TYPE_EX];		// DT_ROOM or DT_EQUIPMENT or DT_MAILUSER
							$item["email_address"] = w2u($user_data[$this->ab_properties["email_address"]]);
							$item["smtp_address"] = w2u($user_data[$this->ab_properties["smtp_address"]]);
							$item["search_key"] = bin2hex($user_data[$this->ab_properties["search_key"]]);
							$item["addrtype"] = w2u($user_data[$this->ab_properties["addrtype"]]);
							$item["message_flags"] = MSGFLAG_READ;	// FIXME: setting message_flags to read, to fix the view
							$item["department"] = isset($user_data[$this->ab_properties["department"]]) ? w2u($user_data[$this->ab_properties["department"]]) : "";
							$item["office_phone"] = isset($user_data[$this->ab_properties["office_phone"]]) ? $user_data[$this->ab_properties["office_phone"]] : "";
							$item["location"] = isset($user_data[$this->ab_properties["location"]]) ? w2u($user_data[$this->ab_properties["location"]]) : "";
							$item["fax"] = isset($user_data[$this->ab_properties["fax"]]) ? $user_data[$this->ab_properties["fax"]] : "";
							$item["addrtype"] = "SMTP";
							$item["search_key"] = bin2hex($user_data[$this->ab_properties["search_key"]]);

							// Contacts from the GAL have email-address saved in smtp_address property.
							if(trim($item["email_address"]) == "") {
								$item["email_address"] = $item["smtp_address"];
							}
                            break;
                    }

                    array_push($items, $item);
                    
                }
            } else {
	           	// Provide clue that full GAB is disabled.
            	$data = array_merge($data, array("disable_full_gab" => DISABLE_FULL_GAB));
            }
			$data = array_merge($data, array("item"=>$items));

			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
			return true;
		}

		/**
		 *	Function will create a restriction based on parameters passed for hiding users
		 *	@param		Array				$hide_users		list of users that should not be shown
		 *	@return		restrictionObject					restriction for hiding provided users
		 */
		function createUsersRestriction($hide_users) {
			$tempRestrictions = Array();

			// check we should hide users or not
			if($hide_users) {
				// wrap parameters in an array
				if(!is_array($hide_users)) {
					$hide_users = Array($hide_users);
				}

				if(in_array("room", $hide_users)) {
					array_push($tempRestrictions, Array(
													RES_PROPERTY,
														Array(
															RELOP => RELOP_NE,
															ULPROPTAG => PR_DISPLAY_TYPE_EX,
															VALUE => Array(
																	PR_DISPLAY_TYPE_EX => DT_ROOM
															)
														)
													)
							);
				}

				if(in_array("equipment", $hide_users)) {
					array_push($tempRestrictions, Array(
													RES_PROPERTY,
														Array(
															RELOP => RELOP_NE,
															ULPROPTAG => PR_DISPLAY_TYPE_EX,
															VALUE => Array(
																	PR_DISPLAY_TYPE_EX => DT_EQUIPMENT
															)
														)
													)
							);
				}

				if(in_array("active", $hide_users)) {
					array_push($tempRestrictions, Array(
													RES_PROPERTY,
														Array(
															RELOP => RELOP_NE,
															ULPROPTAG => PR_DISPLAY_TYPE_EX,
															VALUE => Array(
																	PR_DISPLAY_TYPE_EX => DTE_FLAG_ACL_CAPABLE
															)
														)
													)
							);
				}

				if(in_array("non_active", $hide_users)) {
					array_push($tempRestrictions, Array(
													RES_PROPERTY,
														Array(
															RELOP => RELOP_NE,
															ULPROPTAG => PR_DISPLAY_TYPE_EX,
															VALUE => Array(
																	PR_DISPLAY_TYPE_EX => DT_MAILUSER
															)
														)
													)
							);
				}

				if(in_array("contact", $hide_users)) {
					array_push($tempRestrictions, Array(
													RES_PROPERTY,
														Array(
															RELOP => RELOP_NE,
															ULPROPTAG => PR_DISPLAY_TYPE_EX,
															VALUE => Array(
																	PR_DISPLAY_TYPE_EX => DT_REMOTE_MAILUSER
															)
														)
													)
							);
				}

				if(in_array("system", $hide_users)) {
					array_push($tempRestrictions, Array(
													RES_NOT,
														Array(
															Array(
																RES_CONTENT,
																	Array(
																		FUZZYLEVEL => FL_FULLSTRING,
																		ULPROPTAG => PR_ACCOUNT,
																		VALUE => Array(
																				PR_ACCOUNT => "SYSTEM"
																		)
																	)
															)
														)
													)
							);
				}
			}

			if(count($tempRestrictions) > 0) {
				$usersRestriction = Array(
										RES_AND,
											Array(
												Array(
													RES_OR,
														Array(
															Array(
																RES_PROPERTY,
																	Array(
																		RELOP => RELOP_EQ,
																		ULPROPTAG => PR_DISPLAY_TYPE,
																		VALUE => Array(
																				PR_DISPLAY_TYPE => DT_MAILUSER
																		)
																	)
															),
															Array(
																RES_PROPERTY,
																	Array(
																		RELOP => RELOP_EQ,
																		ULPROPTAG => PR_DISPLAY_TYPE,
																		VALUE => Array(
																				PR_DISPLAY_TYPE => DT_REMOTE_MAILUSER
																		)
																	)
															)
														)
												),
												Array(
													RES_AND,
													$tempRestrictions					// all user restrictions
												)
											)
									);
			} else {
				$usersRestriction = Array(
										RES_AND,
											Array(
												Array(
													RES_OR,
														Array(
															Array(
																RES_PROPERTY,
																	Array(
																		RELOP => RELOP_EQ,
																		ULPROPTAG => PR_DISPLAY_TYPE,
																		VALUE => Array(
																				PR_DISPLAY_TYPE => DT_MAILUSER
																		)
																	)
															),
															Array(
																RES_PROPERTY,
																	Array(
																		RELOP => RELOP_EQ,
																		ULPROPTAG => PR_DISPLAY_TYPE,
																		VALUE => Array(
																				PR_DISPLAY_TYPE => DT_REMOTE_MAILUSER
																		)
																	)
															)
														)
												)
											)
									);
			}

			return $usersRestriction;
		}

		/**
		 *	Function will create a restriction based on parameters passed for hiding groups
		 *	@param		Array				$hide_groups	list of groups that should not be shown
		 *	@return		restrictionObject					restriction for hiding provided users
		 */
		function createGroupsRestriction($hide_groups) {
			$tempRestrictions = Array();

			// check we should hide groups or not
			if($hide_groups) {
				// wrap parameters in an array
				if(!is_array($hide_groups)) {
					$hide_groups = Array($hide_groups);
				}

				if(in_array("normal", $hide_groups)) {
					array_push($tempRestrictions, Array(
													RES_PROPERTY,
														Array(
															RELOP => RELOP_NE,
															ULPROPTAG => PR_DISPLAY_TYPE_EX,
															VALUE => Array(
																	PR_DISPLAY_TYPE_EX => DT_DISTLIST
															)
														)
													)
							);
				}

				if(in_array("security", $hide_groups)) {
					array_push($tempRestrictions, Array(
													RES_PROPERTY,
														Array(
															RELOP => RELOP_NE,
															ULPROPTAG => PR_DISPLAY_TYPE_EX,
															VALUE => Array(
																	PR_DISPLAY_TYPE_EX => (DT_SEC_DISTLIST | DTE_FLAG_ACL_CAPABLE)
															)
														)
													)
							);
				}

				if(in_array("dynamic", $hide_groups)) {
					array_push($tempRestrictions, Array(
													RES_PROPERTY,
														Array(
															RELOP => RELOP_NE,
															ULPROPTAG => PR_DISPLAY_TYPE_EX,
															VALUE => Array(
																	PR_DISPLAY_TYPE_EX => DT_AGENT
															)
														)
													)
							);
				}

				if(in_array("everyone", $hide_groups)) {
					array_push($tempRestrictions, Array(
													RES_NOT,
														Array(
															Array(
																RES_CONTENT,
																	Array(
																		FUZZYLEVEL => FL_FULLSTRING,
																		ULPROPTAG => PR_ACCOUNT,
																		VALUE => Array(
																				PR_ACCOUNT => "Everyone"
																		)
																	)
															)
														)
													)
							);
				}
			}

			if(count($tempRestrictions) > 0) {
				$groupsRestriction = Array(
										RES_AND,
											Array(
												Array(
													RES_PROPERTY,
														Array(
															RELOP => RELOP_EQ,
															ULPROPTAG => PR_DISPLAY_TYPE,
															VALUE => Array(
																	PR_DISPLAY_TYPE => DT_DISTLIST
															)
														)
												),
												Array(
													RES_AND,
													$tempRestrictions					// all group restrictions
												)
											)
									);
			} else {
				$groupsRestriction = Array(
										RES_AND,
											Array(
												Array(
													RES_PROPERTY,
														Array(
															RELOP => RELOP_EQ,
															ULPROPTAG => PR_DISPLAY_TYPE,
															VALUE => Array(
																	PR_DISPLAY_TYPE => DT_DISTLIST
															)
														)
													)
											)
									);
			}

			return $groupsRestriction;
		}

		/**
		 *	Function will create a restriction to get company information
		 *	@param		Boolean				$hide_companies	true/false
		 *	@return		restrictionObject					restriction for getting company info
		 */
		function createCompanyRestriction($hide_companies) {
			$companyRestriction = false;

			if(!$hide_companies) {
				$companyRestriction =	Array(
											RES_PROPERTY,
												Array(
													RELOP => RELOP_EQ,
													ULPROPTAG => PR_DISPLAY_TYPE,
													VALUE => Array(
															PR_DISPLAY_TYPE => DT_ORGANIZATION
													)
												)
										);
			}

			return $companyRestriction;
		}

		function getHierarchy($action)
		{
			$data = array();
			$data["attributes"] = array("type" => "hierarchy");

			$storeslist = false;
			if (isset($action["contacts"])){
				if (isset($action["contacts"]["stores"]["store"]) && !is_array($action["contacts"]["stores"]["store"])){
					$action["contacts"]["stores"]["store"] = array($action["contacts"]["stores"]["store"]);
				}
				if (isset($action["contacts"]["stores"]["folder"]) && !is_array($action["contacts"]["stores"]["folder"])){
					$action["contacts"]["stores"]["folder"] = array($action["contacts"]["stores"]["folder"]);
				}
				$storeslist = $action["contacts"]["stores"];
			}
			
			$folders = $GLOBALS["operations"]->getAddressbookHierarchy($storeslist);

			$data = array_merge($data, array("folder"=>$folders));
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
			return true;
		}


		function expandGroup($entryid)
		{
			$result = array();
			$group = mapi_ab_openentry($GLOBALS["mapisession"]->getAddressbook(), $entryid);
			$table = mapi_folder_getcontentstable($group);
			$items = mapi_table_queryallrows($table, $this->ab_properties);
			foreach($items as $item){
				if($item[$this->ab_properties["display_type"]] == DT_MAILUSER && isset($item[$this->ab_properties["smtp_address"]])) {
					$result[] = array(
							"entryid"=>array("attributes"=>array("type"=>"binary"),"_content"=>bin2hex($item[$this->ab_properties["entryid"]])),
							"fileas"=>w2u($item[$this->ab_properties["account"]]),
							"display_name"=>w2u($item[$this->ab_properties["display_name"]]),
							"addrtype"=>w2u($item[$this->ab_properties["addrtype"]]),
							"email_address"=>w2u($item[$this->ab_properties["smtp_address"]]),
						);
				}
			}
			return $result;
		}


		/**
		 * Function which expands a distribution list
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the distribution list
		 * @param array $listEntryid used to prevent looping with multiple dist.lists
		 */
		function expandDistributionList($store, $entryid, $listEntryIDs = array())
		{
			if (in_array($entryid, $listEntryIDs)){ // don't expand a distlist that is already expanded
				return array();
			}

			$listEntryIDs[] = $entryid;

			$message = mapi_msgstore_openentry($store, $entryid);

			if($message) {
				$props = mapi_getprops($message, array($this->properties["oneoff_members"], $this->properties["members"]));
	
				if(isset($props[$this->properties["oneoff_members"]])) {
					$members = $props[$this->properties["members"]];
		
					// parse oneoff members
					$oneoffmembers = array();
					foreach($props[$this->properties["oneoff_members"]] as $key=>$item){
						$oneoffmembers[$key] = mapi_parseoneoff($item);
					}

					$items = array();

					foreach($members as $key=>$member){
						$parts = unpack("Vnull/A16guid/Ctype/A*entryid", $member);
						
						if ($parts["guid"]==hex2bin("812b1fa4bea310199d6e00dd010f5402")){ // custom e-mail address (no user or contact)
							// $parts can not be used for this guid because it is a one off entryid
							$oneoff = mapi_parseoneoff($member);
							$item = array();
							$item["fileas"] = w2u($oneoff["name"]);
							$item["display_name"] = $item["fileas"];
							$item["addrtype"] = w2u($oneoff["type"]);
							$item["email_address"] = w2u($oneoff["address"]);
							$items[] = $item;
						}else{
							// $parts can be another personal distribution list, an electronic address contained in a contact, a GAL member, a distribution list, or a one-off e-mail address.
							// see msdn documentation [OXOCNTC] WrappedEntryId structure
							$item = array();
							switch($parts["type"]){
								case 0: //one off
									$oneoff = mapi_parseoneoff($parts["entryid"]);
									$item["fileas"] = w2u($oneoff["name"]);
									$item["display_name"] = $item["fileas"];
									$item["addrtype"] = w2u($oneoff["type"]);
									$item["email_address"] = w2u($oneoff["address"]);
									$items[] = $item;
									break;
								case DL_USER: // contact
									$msg = mapi_msgstore_openentry($store, $parts["entryid"]);
									if (mapi_last_hresult()!=NOERROR) // contact could be deleted, skip item
										continue;
									$msgProps = mapi_getprops($msg, $this->properties);
									$item = array();
									$item["entryid"] = bin2hex($parts["entryid"]);
									$item["fileas"] = w2u($msgProps[$this->properties["fileas"]]);
									$item["display_name"] = w2u($msgProps[$this->properties["display_name"]]);
									$item["email_address"] = w2u($msgProps[$this->properties["email_address_1"]]);
									$item["objecttype"] = $msgProps[$this->ab_properties["objecttype"]];

									// use the email adress from the OneOff entry	
									$item["addrtype"] = w2u($oneoffmembers[$key]["type"]);
									$items[] = $item;		
									break;
								case DL_USER_AB: // ab user
									$ab = $GLOBALS["mapisession"]->getAddressbook();
									$msg = mapi_ab_openentry($ab,$parts["entryid"]);
									if (mapi_last_hresult()!=NOERROR) // user could be deleted, skip item
										continue;
									$msgProps = mapi_getprops($msg, $this->ab_properties);
									$item = array();
									$item["entryid"] = bin2hex($parts["entryid"]);
									$item["fileas"] = w2u($msgProps[$this->ab_properties["account"]]);
									$item["display_name"] = w2u($msgProps[$this->ab_properties["display_name"]]);

									// don't use the email adress from the OneOff entry because it will give a ZARAFA mail address, and in the webaccess we currently only support SMTP addresses
									$item["addrtype"] = "SMTP";
									$item["email_address"] = w2u($msgProps[$this->ab_properties["smtp_address"]]);
									$item["objecttype"] = $msgProps[$this->ab_properties["objecttype"]];

									$items[] = $item;
									break;

								case DL_DIST: // dist list
									$items = array_merge($items, $this->expandDistributionList($store, $parts["entryid"], $listEntryIDs));
									break;

								case DL_DIST_AB: // group
									$group = mapi_ab_openentry($GLOBALS["mapisession"]->getAddressbook(), $parts["entryid"]);
									$groupProps = mapi_getprops($group, $this->ab_properties);
									
									$item = array();
									$item["entryid"] = bin2hex($parts["entryid"]);
									$item["fileas"] = w2u($groupProps[$this->ab_properties["account"]]);
									$item["display_name"] = w2u($groupProps[$this->ab_properties["display_name"]]);
									$item["addrtype"] = "ZARAFA";
									$item["email_address"] = w2u($groupProps[$this->ab_properties["email_address"]]);
									$item["objecttype"] = $groupProps[$this->ab_properties["objecttype"]];
									$items[] = $item;
									break;
							}
						}
					}
					return $items;
				}
			}
			return array();
		}

		/**
		 * Function will return restriction used in GAB pagination.
		 * @param array $action the action data, sent by the client
		 * @return array paginationRestriction returns restriction used in pagination.
		 */
		function getPaginationRestriction($action)
		{
			if(isset($action["restriction"]["pagination_character"])) {
				// Get sorting column to provide pagination on it.
				if(isset($action["sort"]) && isset($action["sort"]["column"]))
					$sortColumn = $action["sort"]["column"][0]["_content"];

				$paginationColumnProperties = array();
				// Get Pagination column Properties.
				if($action["attributes"]["type"] == "contacts") {
					array_push($paginationColumnProperties, $this->properties["fileas"]);
					array_push($paginationColumnProperties, $this->properties["display_name"]);
					array_push($paginationColumnProperties, $this->properties["email_address_1"]);
					array_push($paginationColumnProperties, $this->properties["email_address_display_name_1"]);
					array_push($paginationColumnProperties, $this->properties["email_address_2"]);
					array_push($paginationColumnProperties, $this->properties["email_address_display_name_2"]);
					array_push($paginationColumnProperties, $this->properties["email_address_3"]);
					array_push($paginationColumnProperties, $this->properties["email_address_display_name_3"]);
				} else if ($action["attributes"]["type"] == "globaladdressbook") {
					array_push($paginationColumnProperties, $this->ab_properties["account"]);
					array_push($paginationColumnProperties, $this->ab_properties["display_name"]);
					array_push($paginationColumnProperties, $this->ab_properties["smtp_address"]);
					array_push($paginationColumnProperties, $this->ab_properties["department"]);
					array_push($paginationColumnProperties, $this->ab_properties["location"]);
				}

				// Get Pagination character.
				$paginationCharacter = $action["restriction"]["pagination_character"];
				
				// Create restriction according to paginationColumn
				$restrictions = array();
				foreach ($paginationColumnProperties as $paginationColumnProperty)
				{
					switch ($paginationCharacter){
						case "123":
							array_push($restrictions,
												array(
														RES_AND,
														array(
															array(
																RES_PROPERTY,
																array(
																	RELOP => RELOP_GE,
																	ULPROPTAG =>  $paginationColumnProperty,
																	VALUE => array(
																		$paginationColumnProperty => "0"
																	)
																)
															),
															array(
																RES_PROPERTY,
																array(
																	RELOP => RELOP_LE,
																	ULPROPTAG => $paginationColumnProperty,
																	VALUE => array(
																		$paginationColumnProperty => "9"
																	)
																)
															)
														)
													)
												);
						break;

						case "z":
							array_push($restrictions,
											array(
												RES_PROPERTY,
												array(RELOP => RELOP_GE,
													ULPROPTAG => $paginationColumnProperty,
													VALUE => array(
														$paginationColumnProperty => "z" 
														)
													)
												)
											);
						break;

						default:
							array_push($restrictions,
													array(
															RES_AND,
															array(
																array(
																	RES_PROPERTY,
																	array(
																		RELOP => RELOP_GE,
																		ULPROPTAG =>  $paginationColumnProperty,
																		VALUE => array(
																			$paginationColumnProperty => $paginationCharacter
																		)
																	)
																),
																array(
																	RES_PROPERTY,
																	array(
																		RELOP => RELOP_LT,
																		ULPROPTAG => $paginationColumnProperty,
																		VALUE => array(
																			$paginationColumnProperty => chr(ord($paginationCharacter) + 1)
																		)
																	)
																)
															)
														)
													);
					}
				}
				$paginationRestriction = array(RES_OR, $restrictions);
			}

			if($paginationRestriction)
				return $paginationRestriction;
			else
				return false;
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
			$this->properties = $GLOBALS["properties"]->getContactABProperties($store);
			$this->ab_properties = $GLOBALS["properties"]->getAddressBookProperties($store);

			if(isset($this->properties["fileas"])) {
				$this->sort = array();
				$this->sort[$this->properties["fileas"]] = TABLE_SORT_ASCEND;
			}
		}
	}

?>
