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
	 * Contact ItemModule
	 * Module which openes, creates, saves and deletes an item. It 
	 * extends the Module class.
	 */
	class ContactItemModule extends ItemModule
	{
		/**
		 * @var Array properties of contact item that will be used to get data
		 */
		var $properties = null;

		var $plaintext;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function ContactItemModule($id, $data)
		{
			$this->plaintext = true;

			parent::ItemModule($id, $data);
		}
		
		/**
		 * Function which saves an item. It sets the right properties for a contact
		 * item (address book properties).		 
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid parent entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure		 		 
		 */		 		
		function save($store, $parententryid, $action)
		{
			$result = false;
			$properiesToDelete = array();		// create an array of properties which should be deleted 
												// this array is passed to $GLOBALS["operations"]->saveMessage() function
			
			if(!$store && !$parententryid) {
				if(isset($action["props"]["message_class"])) {
					$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
					$parententryid = $this->getDefaultFolderEntryID($store, $action["props"]["message_class"]);
				}
			}

			if($store && $parententryid && isset($action["props"])) {
				if(isset($action["props"]["fileas"])) {
					$action["props"]["subject"] = $action["props"]["fileas"];
				}

				/**
				 * type of email address		address_book_mv			address_book_long
				 *	email1						0						1 (0x00000001)
				 *	email2						1						2 (0x00000002)
				 *	email3						2						4 (0x00000004)
				 *	fax2(business fax)			3						8 (0x00000008)
				 *	fax3(home fax)				4						16 (0x00000010)
				 *	fax1(primary fax)			5						32 (0x00000020)
				 *
				 *	address_book_mv is a multivalued property so all the values are passed in array
				 *	address_book_long stores sum of the flags
				 *	these both properties should be in sync always
				 */
				$action["props"]["address_book_mv"] = array();
				$action["props"]["address_book_long"] =  0;

				// set email address properties
				for($index = 1; $index < 4; $index++)
				{
					if(!empty($action["props"]["email_address_$index"]) && !empty($action["props"]["email_address_display_name_$index"])) {
						$action["props"]["email_address_entryid_$index"] = bin2hex(mapi_createoneoff($action["props"]["email_address_display_name_$index"], "SMTP", $action["props"]["email_address_$index"]));

						switch($index) {
							case 1:
								$action["props"]["address_book_long"] += 1;
								array_push($action["props"]["address_book_mv"], 0);
								break;
							case 2:
								$action["props"]["address_book_long"] += 2;
								array_push($action["props"]["address_book_mv"], 1);
								break;
							case 3:
								$action["props"]["address_book_long"] += 4;
								array_push($action["props"]["address_book_mv"], 2);
								break;
						}

						$action["props"]["email_address_type_$index"] = "SMTP";
						$action["props"]["email_address_display_name_email_$index"] = $action["props"]["email_address_$index"];
					}
				}

				// set properties for primary fax number
				if(isset($action['props']['primary_fax_number']) && !empty($action['props']['primary_fax_number'])) {
					$action['props']['fax_1_address_type'] = 'FAX';
					$action['props']['fax_1_original_display_name'] = $action["props"]["subject"]; // same as PR_NORMALIZED_SUBJECT
					$action['props']['fax_1_email_address'] = $action['props']['fax_1_original_display_name'] . '@' . $action['props']['primary_fax_number'];
					$action['props']['fax_1_original_entryid'] = bin2hex(mapi_createoneoff($action['props']['fax_1_original_display_name'], $action['props']['fax_1_address_type'], $action['props']['fax_1_email_address'], MAPI_UNICODE));

					$action["props"]["address_book_long"] += 32;
					array_push($action["props"]["address_book_mv"], 5);
				} else {
					// delete properties to remove previous values
					$properiesToDelete[] = $this->properties['fax_1_address_type'];
					$properiesToDelete[] = $this->properties['fax_1_original_display_name'];
					$properiesToDelete[] = $this->properties['fax_1_email_address'];
					$properiesToDelete[] = $this->properties['fax_1_original_entryid'];
				}

				// set properties for business fax number
				if(isset($action['props']['business_fax_number']) && !empty($action['props']['business_fax_number'])) {
					$action['props']['fax_2_address_type'] = 'FAX';
					$action['props']['fax_2_original_display_name'] = $action["props"]["subject"]; // same as PR_NORMALIZED_SUBJECT
					$action['props']['fax_2_email_address'] = $action['props']['fax_2_original_display_name'] . '@' . $action['props']['business_fax_number'];
					$action['props']['fax_2_original_entryid'] = bin2hex(mapi_createoneoff($action['props']['fax_2_original_display_name'], $action['props']['fax_2_address_type'], $action['props']['fax_2_email_address'], MAPI_UNICODE));

					$action["props"]["address_book_long"] += 8;
					array_push($action["props"]["address_book_mv"], 3);
				} else {
					$properiesToDelete[] = $this->properties['fax_2_address_type'];
					$properiesToDelete[] = $this->properties['fax_2_original_display_name'];
					$properiesToDelete[] = $this->properties['fax_2_email_address'];
					$properiesToDelete[] = $this->properties['fax_2_original_entryid'];
				}

				// set properties for home fax number
				if(isset($action['props']['home_fax_number']) && !empty($action['props']['home_fax_number'])) {
					$action['props']['fax_3_address_type'] = 'FAX';
					$action['props']['fax_3_original_display_name'] = $action["props"]["subject"]; // same as PR_NORMALIZED_SUBJECT
					$action['props']['fax_3_email_address'] = $action['props']['fax_3_original_display_name'] . '@' . $action['props']['home_fax_number'];
					$action['props']['fax_3_original_entryid'] = bin2hex(mapi_createoneoff($action['props']['fax_3_original_display_name'], $action['props']['fax_3_address_type'], $action['props']['fax_3_email_address'], MAPI_UNICODE));

					$action["props"]["address_book_long"] += 16;
					array_push($action["props"]["address_book_mv"], 4);
				} else {
					$properiesToDelete[] = $this->properties['fax_3_address_type'];
					$properiesToDelete[] = $this->properties['fax_3_original_display_name'];
					$properiesToDelete[] = $this->properties['fax_3_email_address'];
					$properiesToDelete[] = $this->properties['fax_3_original_entryid'];
				}

				sort($action["props"]["address_book_mv"]);

				// check for properties which should be deleted
				if(isset($action["props"]["entryid"]) && !empty($action["props"]["entryid"])) {
					// check for empty email address properties
					for($i = 1; $i < 4; $i++)
					{
						if(!isset($action["props"]["email_address_entryid_" . $i])) {
							array_push($properiesToDelete, $this->properties["email_address_entryid_" . $i]);
							array_push($properiesToDelete, $this->properties["email_address_" . $i]);
							array_push($properiesToDelete, $this->properties["email_address_display_name_" . $i]);
							array_push($properiesToDelete, $this->properties["email_address_display_name_email_" . $i]);
							array_push($properiesToDelete, $this->properties["email_address_type_" . $i]);
						}
					}

					// check for empty address_book_mv and address_book_long properties
					if($action["props"]["address_book_long"] == 0) {
						$properiesToDelete[] = $this->properties['address_book_mv'];
						$properiesToDelete[] = $this->properties['address_book_long'];
					}

					// Check if the birthday and anniversary properties are empty. If so delete them.
					if(!isset($action['props']['birthday']) || empty($action['props']['birthday'])){
						array_push($properiesToDelete, $this->properties['birthday']);
						array_push($properiesToDelete, $this->properties['birthday_eventid']);
						if(!empty($action["props"]["birthday_eventid"])) {
							$this->deleteSpecialDateAppointment($store, $action["props"]["birthday_eventid"]);
						}
					}

					if(!isset($action['props']['wedding_anniversary']) || empty($action['props']['wedding_anniversary'])){
						array_push($properiesToDelete, $this->properties['wedding_anniversary']);
						array_push($properiesToDelete, $this->properties['anniversary_eventid']);
						if(!empty($action["props"]["anniversary_eventid"])) {
							$this->deleteSpecialDateAppointment($store, $action["props"]["anniversary_eventid"]);
						}
					}
				}

				/** 
				 * convert all line endings(LF) into CRLF
				 * XML parser will normalize all CR, LF and CRLF into LF
				 * but outlook(windows) uses CRLF as line ending
				 */
				if(isset($action["props"]["business_address"])) {
					$action["props"]["business_address"] = str_replace("\n", "\r\n", $action["props"]["business_address"]);
				}

				if(isset($action["props"]["home_address"])) {
					$action["props"]["home_address"] = str_replace("\n", "\r\n", $action["props"]["home_address"]);
				}

				if(isset($action["props"]["other_address"])) {
					$action["props"]["other_address"] = str_replace("\n", "\r\n", $action["props"]["other_address"]);
				}

				/**
				 * check if webpage property is set if it is set then 
				 * also set PR_BUSINESS_HOME_PAGE property
				 */
				if(isset($action["props"]["webpage"])) {
					$action["props"]["business_home_page"] = $action["props"]["webpage"];
				}
				
				// check birthday props to make an appoitmentnet  
				if(isset($action["props"]["birthday"]) && !empty($action["props"]["birthday"])){
					$data = array();
					$data["appointment_unixtime"] = $action['props']['birthday'];
					$data["appointment_texttime"] = $action['props']['text_birthday'];
					$data["timezone"] = $action['props']['timezone'];
					$data["dialog_attachments"] = $action["dialog_attachments"];
					
					if(isset($action['props']['display_name_prefix']) && !empty($action['props']['display_name_prefix'])){
						$displayname = trim(str_replace($action['props']['display_name_prefix'], "", $action['props']['display_name']));
					}else{
						$displayname = trim($action['props']['display_name']);
					}
					$data["subject"] = sprintf(_("%s's Birthday"), $displayname);

					// check wheather the birthday date is modified or not,to update the Appointment in calendar 
					if(isset($action["props"]["birthday_eventid"]) && empty($action["props"]["birthday_eventid"])){
						$action["props"]["birthday_eventid"] = bin2hex($this->createSpecialDateAppointment($store, $data));
					}else{
						$data["entryid"] = $action["props"]["birthday_eventid"];
						$this->createSpecialDateAppointment($store, $data);
					}
				}

				// check anniversary props to make an appoitmentnet  
				if(isset($action["props"]["wedding_anniversary"]) && !empty($action["props"]["wedding_anniversary"])){
					$data = array();
					$data["appointment_unixtime"] = $action['props']['wedding_anniversary'];
					$data["appointment_texttime"] = $action['props']['text_wedding_anniversary'];
					$data["timezone"] = $action['props']['timezone'];
					$data["dialog_attachments"] = $action["dialog_attachments"];
					
					if(isset($action['props']['display_name_prefix']) && !empty($action['props']['display_name_prefix'])){
						$displayname = trim(str_replace($action['props']['display_name_prefix'], "", $action['props']['display_name']));
					}else{
						$displayname = trim($action['props']['display_name']);
					}
					$data["subject"] = sprintf(_("%s's Anniversary"), $displayname);

					// check wheather the wedding_anniversary date is modified or not,to update the Appointment in calendar 
					if(isset($action["props"]["anniversary_eventid"]) && empty($action["props"]["anniversary_eventid"])){
						$action["props"]["anniversary_eventid"] = bin2hex($this->createSpecialDateAppointment($store, $data));
					}else{
						$data["entryid"] = $action["props"]["anniversary_eventid"];
						$this->createSpecialDateAppointment($store, $data);
					}
				}

				$messageProps = array();
				$result = $GLOBALS["operations"]->saveMessage($store, $parententryid, Conversion::mapXML2MAPI($this->properties, $action["props"]), false, $action["dialog_attachments"], $messageProps, false, false, $properiesToDelete);

				if($result) {
					$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_SAVE, $messageProps);
				}
			}
			
			return $result;
		}

		/**
		 * Function will create a yearly recurring appointment on the respective date of birthday or anniversary in user's calendar.	 
		 * @param object $store MAPI Message Store Object
		 * @param array $action the action data, sent by the client
		 * @return entryid of the newly created appointment.
		 */	
		function createSpecialDateAppointment($store, $action)
		{
			$result = false;
			
			$inbox = mapi_msgstore_getreceivefolder($store);
			if(mapi_last_hresult() == NOERROR) {
				$inboxprops = mapi_getprops($inbox, Array(PR_IPM_APPOINTMENT_ENTRYID, PR_STORE_ENTRYID));
				$parententryid = bin2hex($inboxprops[PR_IPM_APPOINTMENT_ENTRYID]);
				$storeentryid = bin2hex($inboxprops[PR_STORE_ENTRYID]);
			}

			// here the start of appointment is always set in localtime, so the unixtime is added with timezone value. 
			// (-1) is used for the correct direction ie. as we move eastward from GMT the timezone value is in positive and vicaversa when
			// moving westward.
			$recur = new Recurrence(null, null);
			$startDate_localtime = $action["appointment_unixtime"] + ($recur->getTimezone($action["timezone"], $action["appointment_unixtime"]) * 60 * (-1));
			
			// comment it properly why
			switch(strftime("%m", $startDate_localtime)){
				case 1:	$month = "0";
				break;
				case 2:	$month = "44640";
				break;
				case 3:	$month = "84960";
				break;
				case 4:	$month = "129600";
				break;
				case 5:	$month = "172800";
				break;
				case 6:	$month = "217440";
				break;
				case 7:	$month = "260640";
				break;
				case 8:	$month = "305280";
				break;
				case 9:	$month = "348480";
				break;
				case 10: $month = "393120";
				break;
				case 11: $month = "437760";
				break;
				case 12: $month = "480960";
				break;
			}
				
			$props = array ('entryid'	=> $action["entryid"],
							'message_class' => 'IPM.Appointment',
							'icon_index' => '1025',
							'busystatus' => '0',
							'startdate' => $action["appointment_unixtime"],				
							'duedate' => $action["appointment_unixtime"] + (24 * 60 * 60),	//ONE DAY is added to set duedate of item.
							'alldayevent' => '1',
							'reminder' => '1', 
							'reminder_minutes' => 18*60,										// all day events default to 18 hours reminder
							'commonstart' => $action["appointment_unixtime"],
							'commonend' => $action["appointment_unixtime"] + (24 * 60 * 60),	//ONE DAY is added to set enddate of item.
							'meeting' => '0',
							'recurring' => '1',
							'recurring_reset' => '1',
							'startocc' => '0',
							'endocc' => '1440',
							'start' => $startDate_localtime,	//start is set as local time.
							'term' => '35',
							'everyn' => '1',
							'subtype' => '2',
							'type' => '13',
							'month' => $month,
							'monthday' => strftime("%e", $startDate_localtime),
							'timezone' => $action["timezone"]["timezone"],
							'timezonedst' => $action["timezone"]["timezonedst"],
							'dststartmonth' => $action["timezone"]["dststartmonth"],
							'dststartweek' => $action["timezone"]["dststartweek"],
							'dststartday' => $action["timezone"]["dststartday"],
							'dststarthour' => $action["timezone"]["dststarthour"],
							'dstendmonth' => $action["timezone"]["dstendmonth"],
							'dstendweek' => $action["timezone"]["dstendweek"],
							'dstendday' => $action["timezone"]["dstendday"],
							'dstendhour' => $action["timezone"]["dstendhour"],
							'responsestatus' => '0',
							'subject' => $action["subject"],
							'appoint_start_date' => $action["appointment_texttime"],
							'checkbox_alldayevent' => 'true',
							'appoint_end_date' => $action["appointment_texttime"],
							);
			
			$data = array();
			$data["attributes"] = array();
			$data["attributes"]["type"] = 'save';
			$data["store"] = $storeentryid;
			$data["parententryid"] = $parententryid;
			$data["props"] = $props;
			$data["dialog_attachments"] = $action["dialog_attachments"];
			
			// Save appointment (saveAppointment takes care of creating/modifying exceptions to recurring
			// items if necessary)
			$messageProps = $GLOBALS["operations"]->saveAppointment($store, hex2bin($parententryid), $data);

			// Notify the bus if the save was OK
			if($messageProps && !(is_array($messageProps) && isset($messageProps['error'])) ){
				$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_SAVE, $messageProps);
				$result = $messageProps[PR_ENTRYID];
			}
			return $result;
		}
		
		/**
		 * Function will delete the appointment on the respective date of birthday or anniversary in user's calendar.	 
		 * @param object $store MAPI Message Store Object
		 * @param $entryid of the message with will be deleted,sent by the client
		 */	
		function deleteSpecialDateAppointment($store, $entryid)
		{
			$inbox = mapi_msgstore_getreceivefolder($store);
			if(mapi_last_hresult() == NOERROR) {
				$inboxprops = mapi_getprops($inbox, Array(PR_IPM_APPOINTMENT_ENTRYID, PR_STORE_ENTRYID));
				$parententryid = $inboxprops[PR_IPM_APPOINTMENT_ENTRYID];
				$storeentryid = $inboxprops[PR_STORE_ENTRYID];
			}

			$props = array();
			$props[PR_PARENT_ENTRYID] = $parententryid;
			$props[PR_ENTRYID] = hex2bin($entryid);
			$props[PR_STORE_ENTRYID] = $storeentryid;
			
			$result = $GLOBALS["operations"]->deleteMessages($store, $parententryid, hex2bin($entryid));
			
			if($result) {
				$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_DELETE, $props);
			}
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
			$this->properties = $GLOBALS["properties"]->getContactProperties($store);
		}
	}
?>