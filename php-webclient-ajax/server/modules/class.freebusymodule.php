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
	 * FreeBusyModule Module
	 */
	class FreeBusyModule extends Module
	{

		function FreeBusyModule($id, $data)
		{
			parent::Module($id, $data);
		}
		
		function execute()
		{	
			//add user
			foreach($this->data as $selUser)
			{
				//get info from webclient
				$type = $selUser["attributes"]["type"];
				
				switch($type){
					case("add"):
						//get info from MAPI
						//search the user info on the emailaddress bases 
						$query = $selUser["emailaddress"] ? $selUser["emailaddress"] : $selUser["username"];
						$userData = $this->getUserData(u2w($query), $selUser["clientuser"] == "c_user" ? true : false);

						if($userData){
							$data = array();
							$data["attributes"] = array("type" => "add");
							$data["start"] = $selUser["start"];
							$data["end"] = $selUser["end"];
							
							$user = array();
							$user["attributes"] = array("clientuser" => $selUser["clientuser"]);
							$user["username"] = $selUser["username"];
							$user["entryid"] = bin2hex($userData["entryid"]);
							$user["fullname"] = $userData["fullname"];
							$user["email"] = $userData["emailaddress"];
							$user["recipienttype"] = $userData["recipienttype"];
							$user["objecttype"] = $userData["objecttype"];

							$busyArray = $this->getFreeBusyInfo($userData["entryid"],$selUser["start"],$selUser["end"]);

							if ($busyArray == NULL) {
								// No freebusy data available 
								$busy["status"] = array();
								$busy["status"]["_content"] = "-1";
								$busy["start"] = array();
								$busy["start"]["_content"] = "";
								$busy["start"]["attributes"] = array ("unixtime" => $selUser["start"]);
								$busy["end"] = array();
								$busy["end"]["_content"] = "";
								$busy["end"]["attributes"] = array ("unixtime" => $selUser["end"]);
								$user["item"][] = $busy;
							} else {
								foreach($busyArray as $busyItem){
									//add busy time
									$busy = array();
									$busy["status"] = array();
									$busy["status"]["_content"] = $busyItem["status"];
									$busy["start"] = array();
									$busy["start"]["_content"] = "";
									$busy["start"]["attributes"] = array ("unixtime" => $busyItem["start"]);
									$busy["end"] = array();
									$busy["end"]["_content"] = "";
									$busy["end"]["attributes"] = array ("unixtime" => $busyItem["end"]);
									$user["item"][] = $busy;
								}
							}
							
							//add to array
							$data["user"][] = $user;
						}
						else{
							$data = array();
							$data["attributes"] = array("type" => "add");
							$data["start"] = $selUser["start"];
							$data["end"] = $selUser["end"];
							
							$user = array();
							$user["attributes"] = array("clientuser" => $selUser["clientuser"]);
							$user["username"] = $selUser["username"];
							$user["entryid"] = $selUser["username"];
							$user["fullname"] = $selUser["username"];
							$user["email"] = $selUser["emailaddress"] ? w2u($selUser["emailaddress"]) : $selUser["username"];

							//add busy time
							$busy = array();
							$busy["status"] = array();
							$busy["status"]["_content"] = "-1";
							$busy["start"] = array();
							$busy["start"]["_content"] = "";
							$busy["start"]["attributes"] = array ("unixtime" => $selUser["start"]);
							$busy["end"] = array();
							$busy["end"]["_content"] = "";
							$busy["end"]["attributes"] = array ("unixtime" => $selUser["end"]);
							$user["item"][] = $busy;
							
							//add to array
							$data["user"][] = $user;
						}
						break;
				}
			}
			
			array_push($this->responseData["action"], $data);
			$GLOBALS["bus"]->addData($this->responseData);
	
			return true;
		}
		
		/**
		* This function searches the addressbook specified for users and return the first match
		* Please note that the returning array must be UTF8
		*
		* This function must be replaced by the resolve user module
		*
		*@param $query The search query, case is ignored
		*@param $organizer true/false
		*/
		function getUserData($query, $organizer)
		{
			$result = false;
			
			// open addressbook
			$ab = $GLOBALS["mapisession"]->getAddressbook();
			
			// First, try an exact-name lookup of the user
			$rows = mapi_ab_resolvename($ab, array ( array(PR_DISPLAY_NAME => $query) ) , EMS_AB_ADDRESS_LOOKUP );

			if(!$rows) {
				// Next, try a loose lookup of the user
				$rows = mapi_ab_resolvename($ab, array ( array(PR_DISPLAY_NAME => $query) ), 0 );
				if(!$rows && mapi_last_hresult() == MAPI_E_AMBIGUOUS_RECIP) {
					// The user was ambiguous, so get the first match

					$ab_entryid = mapi_ab_getdefaultdir($ab);
					$ab_dir = mapi_ab_openentry($ab,$ab_entryid);
					$table = mapi_folder_getcontentstable($ab_dir);	

					$restriction = array(RES_AND, 
										array(
											array(RES_OR, 
												array(
													array(
														RES_CONTENT,
															array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
																ULPROPTAG=>PR_DISPLAY_NAME,
																VALUE=>$query
															)
														),
													array(
														RES_CONTENT,
															array(FUZZYLEVEL => FL_SUBSTRING|FL_IGNORECASE,
																ULPROPTAG=>PR_ACCOUNT,
																VALUE=>$query
															)
														),
												), // RES_OR
											),
											array(
												RES_PROPERTY,
													array(RELOP=>RELOP_EQ,
														ULPROPTAG=>PR_OBJECT_TYPE,
														VALUE=>MAPI_MAILUSER
													)
												),
											array(
												RES_PROPERTY,
													array(RELOP=>RELOP_NE,
														ULPROPTAG=>PR_ACCOUNT,
														VALUE=>"SYSTEM"
													)
												)
										) // RES_AND
									);

					mapi_table_restrict($table, $restriction);
					mapi_table_sort($table, array(PR_DISPLAY_NAME => TABLE_SORT_ASCEND));

					$rows = mapi_table_queryrows($table, array(PR_ENTRYID), 0 ,1);
				}
			}

			if (is_array($rows) && count($rows)>0) {
				$abitem = mapi_ab_openentry($ab, $rows[0][PR_ENTRYID]);
				$user_data = mapi_getprops($abitem, array(PR_ACCOUNT, PR_DISPLAY_NAME, PR_DISPLAY_TYPE_EX, PR_ENTRYID, PR_EMAIL_ADDRESS, PR_SMTP_ADDRESS, PR_OBJECT_TYPE));
				$result = array();
				$result["entryid"] = $user_data[PR_ENTRYID];
				$result["username"] = windows1252_to_utf8($user_data[PR_ACCOUNT]);
				$result["fullname"] = windows1252_to_utf8($user_data[PR_DISPLAY_NAME]);
				$result["emailaddress"] = $user_data[PR_SMTP_ADDRESS] ? $user_data[PR_SMTP_ADDRESS] : w2u($user_data[PR_EMAIL_ADDRESS]);
				$result["objecttype"] = $user_data[PR_OBJECT_TYPE];
				/**
				 * user type --> PR_DISPLAY_TYPE_EX value
				 * active user --> DT_MAILUSER | DTE_FLAG_ACL_CAPABLE
				 * non-active user --> DT_MAILUSER
				 * room --> DT_ROOM
				 * equipment --> DT_EQUIPMENT
				 * contact --> DT_REMOTE_MAILUSER
				 */
				if(DTE_IS_ACL_CAPABLE($user_data[PR_DISPLAY_TYPE_EX]) === false){
					switch (DTE_LOCAL($user_data[PR_DISPLAY_TYPE_EX])){
						case DT_EQUIPMENT:
						case DT_ROOM:
							$result["recipienttype"] = MAPI_BCC; // resource
							break;
						case DT_MAILUSER:
							$result["recipienttype"] = MAPI_TO; // required attendee
							break;
					}
				} else {
					if($organizer) {
						$result["recipienttype"] = MAPI_ORIG;			// organizer
					} else {
						$result["recipienttype"] = MAPI_TO;				// required attendee
					}
				}
				$result["nameid"] = $query;
			}
			return $result;
		}
		
		function getFreeBusyInfo($entryID,$start,$end)
		{
			$result = array();			
			$fbsupport = mapi_freebusysupport_open($GLOBALS["mapisession"]->getSession());			

			if(mapi_last_hresult() != NOERROR) {
				dump("Error in opening freebusysupport object.");
				return $result;
			}

			$fbDataArray = mapi_freebusysupport_loaddata($fbsupport, array($entryID));
			
			if($fbDataArray[0] != NULL){
				foreach($fbDataArray as $fbDataUser){
					$rangeuser1 = mapi_freebusydata_getpublishrange($fbDataUser);
					if($rangeuser1 == NULL){
						return $result;
					}
					
					$enumblock = mapi_freebusydata_enumblocks($fbDataUser, $start, $end);
					mapi_freebusyenumblock_reset($enumblock);
	
					while(true){
						$blocks = mapi_freebusyenumblock_next($enumblock, 100);
						if(!$blocks){
							break;
						}
						foreach($blocks as $blockItem){
							$result[] = $blockItem;
						}
					}
				}
			}
			
			mapi_freebusysupport_close($fbsupport);
			return $result;
		}
	}
?>
