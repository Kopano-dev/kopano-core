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
	* Server-wide properties class
	*
	* This class is instantiated only once and therefore initialises all its data structures only once. It 
	* is then used throughout the code to retrieve property tags for various purposes.
	*
	* The main idea is that each returned array contains a mapping
	*
	* human_readable_string => numeric_mapi_property_tag
	*
	* This not only makes various parts of the code more readable, we also use these mappings as direct mappings
	* between the XML tag names and the property tags within MAPI. So, when receiving or sending XML data, the
	* mappings here are used as a conversion between the XML tag and the MAPI property tag.
	*
	* For example, the following XML:
	* <code>
    * <subject>hello, world!</subject>
    * </code>
    *
    * is mapped to MAPI property tag PR_SUBJECT (symbolic for 0x0037001e) with value 'hello, world!'
    *
    * Please note that not all MAPI properties have a symbolic name like PR_SUBJECT, as many properties are MAPI
    * named properties which are allocated by getPropIdsFromStrings()
    *
    * Please also note that removing or adding properties to these lists have a profound effect on the rest of the code;
    * If a property is listed here, the code will read that property from the item, and send it via XML. If that property
    * contains megabytes of data, this will mean that you'll be sending megabytes of redundant data over the wire each time
    * one of the objects (or, wores, an entire table) is retrieved by the client.
	*
	* @todo Each of the functions in this class keep re-creating the properties array, but since it is static, it really
	*       only needs to do this once.
	*
	* @package core
	*/

	class Properties
	{
		/**
		 * @var object MAPI Message Store object
		 */		 		
		var $store;
		
		/**
		 * @var TRUE if we have init'ed, FALSE if not
		 */
		var $init = false;

		/**
		 * @var array GUIDS that are used for named properties
		 */
		var $guids;
		
		/**
		 * Constructor		 
		 */		 		
		function Properties()
		{
		} 
		
		/**
		* Initialize the class by opening the default message store. This is done only once.
		*/
		function Init()
		{
			if($this->init)
				return;
			else {
				$this->store = $GLOBALS["mapisession"]->getDefaultMessageStore();
				$this->init = true;
				$this->guids = array();
			}
		}
		
		/**
		 * Returns the properties for Recipients in a message
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for Recipient.
		 */
		function getRecipientProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["display_name"] = PR_DISPLAY_NAME;
			$properties["email_address"] = PR_EMAIL_ADDRESS;
			$properties["recipient_flags"] = PR_RECIPIENT_FLAGS;
			$properties["type"] = PR_RECIPIENT_TYPE;
			$properties["recipient_attendees"] =PR_RECIPIENT_FLAGS;
			$properties["recipient_status"] = PR_RECIPIENT_TRACKSTATUS;
			$properties["recipient_status_num"] = PR_RECIPIENT_TRACKSTATUS_TIME;
			$properties["objecttype"] = PR_OBJECT_TYPE;

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}
		
		/**
		 * Returns the properties for an appointment.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for an appointment.
		 */
		function getAppointmentProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
	  		$properties["entryid"] = PR_ENTRYID;
	  		$properties["parent_entryid"] = PR_PARENT_ENTRYID;
	  		$properties["message_class"] = PR_MESSAGE_CLASS;
			$properties["message_flags"] = PR_MESSAGE_FLAGS;
	  		$properties["icon_index"] = PR_ICON_INDEX;
	  		$properties["subject"] = PR_SUBJECT;
	  		$properties["display_to"] = PR_DISPLAY_TO;
	  		$properties["display_cc"] = PR_DISPLAY_CC;
	  		$properties["display_bcc"] = PR_DISPLAY_BCC;
	  		$properties["importance"] = PR_IMPORTANCE;
	  		$properties["sensitivity"] = PR_SENSITIVITY;
			$properties["sent_representing_entryid"] = PR_SENT_REPRESENTING_ENTRYID;
			$properties["sent_representing_name"] = PR_SENT_REPRESENTING_NAME;
			$properties["sent_representing_addrtype"] = PR_SENT_REPRESENTING_ADDRTYPE;
			$properties["sent_representing_email_address"] = PR_SENT_REPRESENTING_EMAIL_ADDRESS;
			$properties["sent_representing_search_key"] = PR_SENT_REPRESENTING_SEARCH_KEY;
			$properties["startdate"] = "PT_SYSTIME:PSETID_Appointment:0x820d";
			$properties["duedate"] = "PT_SYSTIME:PSETID_Appointment:0x820e";
			$properties["recurring"] = "PT_BOOLEAN:PSETID_Appointment:0x8223";
			$properties["recurring_data"] = "PT_BINARY:PSETID_Appointment:0x8216";
			$properties["recurring_pattern"] = "PT_STRING8:PSETID_Appointment:0x8232";
			$properties["busystatus"] = "PT_LONG:PSETID_Appointment:0x8205";
			$properties["label"] = "PT_LONG:PSETID_Appointment:0x8214";
			$properties["alldayevent"] = "PT_BOOLEAN:PSETID_Appointment:0x8215";
			$properties["private"] = "PT_BOOLEAN:PSETID_Common:0x8506";
			$properties["meeting"] = "PT_LONG:PSETID_Appointment:0x8217";
			$properties["startdate_recurring"] = "PT_SYSTIME:PSETID_Appointment:0x8235";
			$properties["enddate_recurring"] = "PT_SYSTIME:PSETID_Appointment:0x8236";
			$properties["location"] = "PT_STRING8:PSETID_Appointment:0x8208";
			$properties["duration"] = "PT_LONG:PSETID_Appointment:0x8213";
			$properties["responsestatus"] = "PT_LONG:PSETID_Appointment:0x8218";
			$properties["reminder"] = "PT_BOOLEAN:PSETID_Common:0x8503";
			$properties["reminder_minutes"] = "PT_LONG:PSETID_Common:0x8501";
			$properties["contacts"] = "PT_MV_STRING8:PSETID_Common:0x853a";
			$properties["contacts_string"] = "PT_STRING8:PSETID_Common:0x8586";
			$properties["categories"] = "PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords";
			$properties["reminder_time"] = "PT_SYSTIME:PSETID_Common:0x8502";
			$properties["flagdueby"] = "PT_SYSTIME:PSETID_Common:0x8560";
			$properties["commonstart"] = "PT_SYSTIME:PSETID_Common:0x8516";
			$properties["commonend"] = "PT_SYSTIME:PSETID_Common:0x8517";
			$properties["basedate"] = "PT_SYSTIME:PSETID_Appointment:0x8228";
			$properties["timezone_data"] = "PT_BINARY:PSETID_Appointment:0x8233";
			$properties["commonassign"] = "PT_LONG:PSETID_Common:0x8518";
			$properties["last_modification_time"] = PR_LAST_MODIFICATION_TIME;
			$properties["creation_time"] = PR_CREATION_TIME;
			$properties["remindertime"] = "PT_SYSTIME:PSETID_Common:0x8502";
			$properties["requestsent"] = "PT_BOOLEAN:PSETID_Appointment:0x8229";
			$properties["toattendeesstring"] = "PT_STRING8:PSETID_Appointment:0x823B";
			$properties["ccattendeesstring"] = "PT_STRING8:PSETID_Appointment:0x823C";
			$properties["body"] = PR_BODY;
			// Conflict properties
			$properties["conflictitems"] = PR_CONFLICT_ITEMS;

			// Propose new time properties
			$properties["proposed_start_whole"] = "PT_SYSTIME:PSETID_Appointment:0x8250";
			$properties["proposed_end_whole"] = "PT_SYSTIME:PSETID_Appointment:0x8251";
			$properties["proposed_duration"] = "PT_LONG:PSETID_Appointment:0x8256";
			$properties["counter_proposal"] = "PT_BOOLEAN:PSETID_Appointment:0x8257";
			//$properties["proposal_number"] = "PT_BOOLEAN:PSETID_Appointment:0x8257";

			$properties["deleted_on"] = PR_DELETED_ON;
			$properties["updatecounter"] = "PT_LONG:PSETID_Appointment:0x8201";
			$properties["recur_type"] = "PT_LONG:PSETID_Appointment:0x8231";

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}
		
		/**
		 * Returns the properties for a contact.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for a contact.
		 */
		function getContactProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["subject"] = PR_SUBJECT;
			$properties["parent_entryid"] = PR_PARENT_ENTRYID;
			$properties["icon_index"] = PR_ICON_INDEX;
			$properties["message_class"] = PR_MESSAGE_CLASS;
			$properties["message_flags"] = PR_MESSAGE_FLAGS;
			$properties["display_name"] = PR_DISPLAY_NAME;
			$properties["given_name"] = PR_GIVEN_NAME;
			$properties["middle_name"] = PR_MIDDLE_NAME;
			$properties["surname"] = PR_SURNAME;
			$properties["home_telephone_number"] = PR_HOME_TELEPHONE_NUMBER;
			$properties["cellular_telephone_number"] = PR_CELLULAR_TELEPHONE_NUMBER;
			$properties["office_telephone_number"] = PR_OFFICE_TELEPHONE_NUMBER;
			$properties["business_fax_number"] = PR_BUSINESS_FAX_NUMBER;
			$properties["company_name"] = PR_COMPANY_NAME;
			$properties["title"] = PR_TITLE;
			$properties["department_name"] = PR_DEPARTMENT_NAME;
			$properties["office_location"] = PR_OFFICE_LOCATION;
			$properties["profession"] = PR_PROFESSION;
			$properties["manager_name"] = PR_MANAGER_NAME;
			$properties["assistant"] = PR_ASSISTANT;
			$properties["nickname"] = PR_NICKNAME;
			$properties["display_name_prefix"] = PR_DISPLAY_NAME_PREFIX;
			$properties["spouse_name"] = PR_SPOUSE_NAME;
			$properties["generation"] = PR_GENERATION;
			$properties["birthday"] = PR_BIRTHDAY;
			$properties["wedding_anniversary"] = PR_WEDDING_ANNIVERSARY;
			$properties["sensitivity"] = PR_SENSITIVITY;
			$properties["fileas"] = "PT_STRING8:PSETID_Address:0x8005";
			$properties["fileas_selection"] = "PT_LONG:PSETID_Address:0x8006";
			$properties["business_address"] = "PT_STRING8:PSETID_Address:0x801b";
			$properties["email_address_1"] = "PT_STRING8:PSETID_Address:0x8083";
			$properties["email_address_display_name_1"] = "PT_STRING8:PSETID_Address:0x8080";
			$properties["email_address_display_name_email_1"] = "PT_STRING8:PSETID_Address:0x8084";
			$properties["email_address_type_1"] = "PT_STRING8:PSETID_Address:0x8082";
			$properties["email_address_2"] = "PT_STRING8:PSETID_Address:0x8093";
			$properties["email_address_display_name_2"] = "PT_STRING8:PSETID_Address:0x8090";
			$properties["email_address_display_name_email_2"] = "PT_STRING8:PSETID_Address:0x8094";
			$properties["email_address_type_2"] = "PT_STRING8:PSETID_Address:0x8092";
			$properties["email_address_3"] = "PT_STRING8:PSETID_Address:0x80a3";
			$properties["email_address_display_name_3"] = "PT_STRING8:PSETID_Address:0x80a0";
			$properties["email_address_display_name_email_3"] = "PT_STRING8:PSETID_Address:0x80a4";
			$properties["email_address_type_3"] = "PT_STRING8:PSETID_Address:0x80a2";
			$properties["home_address"] = "PT_STRING8:PSETID_Address:0x801a";
			$properties["other_address"] = "PT_STRING8:PSETID_Address:0x801c";
			$properties["mailing_address"] = "PT_LONG:PSETID_Address:0x8022";
			$properties["im"] = "PT_STRING8:PSETID_Address:0x8062";
			$properties["webpage"] = "PT_STRING8:PSETID_Address:0x802b";
			$properties["business_home_page"] = PR_BUSINESS_HOME_PAGE;
			$properties["email_address_entryid_1"] = "PT_BINARY:PSETID_Address:0x8085";
			$properties["email_address_entryid_2"] = "PT_BINARY:PSETID_Address:0x8095";
			$properties["email_address_entryid_3"] = "PT_BINARY:PSETID_Address:0x80a5";
			$properties["address_book_mv"] = "PT_MV_LONG:PSETID_Address:0x8028";
			$properties["address_book_long"] = "PT_LONG:PSETID_Address:0x8029";
			$properties["oneoff_members"] = "PT_MV_BINARY:PSETID_Address:0x8054";
			$properties["members"] = "PT_MV_BINARY:PSETID_Address:0x8055";
			$properties["private"] = "PT_BOOLEAN:PSETID_Common:0x8506";
			$properties["contacts"] = "PT_MV_STRING8:PSETID_Common:0x853a";
			$properties["contacts_string"] = "PT_STRING8:PSETID_Common:0x8586";
			$properties["categories"] = "PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords";
			$properties["last_modification_time"] = PR_LAST_MODIFICATION_TIME;
			// Conflict properties
			$properties["conflictitems"] = PR_CONFLICT_ITEMS;
			$properties["deleted_on"] = PR_DELETED_ON;

			// Detailed contacts properties
			// Properties for phone numbers
			$properties["assistant_telephone_number"] = PR_ASSISTANT_TELEPHONE_NUMBER;
			$properties["business_telephone_number"] = PR_BUSINESS_TELEPHONE_NUMBER;
			$properties["business2_telephone_number"] = PR_BUSINESS2_TELEPHONE_NUMBER;
			$properties["callback_telephone_number"] = PR_CALLBACK_TELEPHONE_NUMBER;
			$properties["car_telephone_number"] = PR_CAR_TELEPHONE_NUMBER;
			$properties["company_telephone_number"] = PR_COMPANY_MAIN_PHONE_NUMBER;
			$properties["home2_telephone_number"] = PR_HOME2_TELEPHONE_NUMBER;
			$properties["home_fax_number"] = PR_HOME_FAX_NUMBER;
			$properties["isdn_number"] = PR_ISDN_NUMBER;
			$properties["other_telephone_number"] = PR_OTHER_TELEPHONE_NUMBER;
			$properties["pager_telephone_number"] = PR_PAGER_TELEPHONE_NUMBER;
			$properties["primary_fax_number"] = PR_PRIMARY_FAX_NUMBER;
			$properties["primary_telephone_number"] = PR_PRIMARY_TELEPHONE_NUMBER;
			$properties["radio_telephone_number"] = PR_RADIO_TELEPHONE_NUMBER;
			$properties["telex_telephone_number"] = PR_TELEX_NUMBER;
			$properties["ttytdd_telephone_number"] = PR_TTYTDD_PHONE_NUMBER;
			// Additional fax properties
			$properties["fax_1_address_type"] = "PT_STRING8:PSETID_Address:0x80B2";
			$properties["fax_1_email_address"] = "PT_STRING8:PSETID_Address:0x80B3";
			$properties["fax_1_original_display_name"] = "PT_STRING8:PSETID_Address:0x80B4";
			$properties["fax_1_original_entryid"] = "PT_BINARY:PSETID_Address:0x80B5";
			$properties["fax_2_address_type"] = "PT_STRING8:PSETID_Address:0x80C2";
			$properties["fax_2_email_address"] = "PT_STRING8:PSETID_Address:0x80C3";
			$properties["fax_2_original_display_name"] = "PT_STRING8:PSETID_Address:0x80C4";
			$properties["fax_2_original_entryid"] = "PT_BINARY:PSETID_Address:0x80C5";
			$properties["fax_3_address_type"] = "PT_STRING8:PSETID_Address:0x80D2";
			$properties["fax_3_email_address"] = "PT_STRING8:PSETID_Address:0x80D3";
			$properties["fax_3_original_display_name"] = "PT_STRING8:PSETID_Address:0x80D4";
			$properties["fax_3_original_entryid"] = "PT_BINARY:PSETID_Address:0x80D5";

			// Properties for addresses
			// Home address
			$properties["home_address_street"] = PR_HOME_ADDRESS_STREET;
			$properties["home_address_city"] = PR_HOME_ADDRESS_CITY;
			$properties["home_address_state"] = PR_HOME_ADDRESS_STATE_OR_PROVINCE;
			$properties["home_address_postal_code"] = PR_HOME_ADDRESS_POSTAL_CODE;
			$properties["home_address_country"] = PR_HOME_ADDRESS_COUNTRY;
			// Other address
			$properties["other_address_street"] = PR_OTHER_ADDRESS_STREET;
			$properties["other_address_city"] = PR_OTHER_ADDRESS_CITY;
			$properties["other_address_state"] = PR_OTHER_ADDRESS_STATE_OR_PROVINCE;
			$properties["other_address_postal_code"] = PR_OTHER_ADDRESS_POSTAL_CODE;
			$properties["other_address_country"] = PR_OTHER_ADDRESS_COUNTRY;
			// Business address
			$properties["business_address_street"] = "PT_STRING8:PSETID_Address:0x8045";
			$properties["business_address_city"] = "PT_STRING8:PSETID_Address:0x8046";
			$properties["business_address_state"] = "PT_STRING8:PSETID_Address:0x8047";
			$properties["business_address_postal_code"] = "PT_STRING8:PSETID_Address:0x8048";
			$properties["business_address_country"] = "PT_STRING8:PSETID_Address:0x8049";
			// Mailing address
			$properties["country"] = PR_COUNTRY;
			$properties["city"] = PR_LOCALITY;
			$properties["postal_address"] = PR_POSTAL_ADDRESS;
			$properties["postal_code"] = PR_POSTAL_CODE;
			$properties["state"] = PR_STATE_OR_PROVINCE;
			$properties["street"] = PR_STREET_ADDRESS;
			// Special Date such as birthday n anniversary appoitment's entryid is store
			$properties["birthday_eventid"] = "PT_BINARY:PSETID_Address:0x804D";
			$properties["anniversary_eventid"] = "PT_BINARY:PSETID_Address:0x804E";

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}

		/**
		 * Returns the properties for a contact in the addressbook.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for a contact.
		 */
		function getContactABProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["parent_entryid"] = PR_PARENT_ENTRYID;
			$properties["icon_index"] = PR_ICON_INDEX;
			$properties["message_class"] = PR_MESSAGE_CLASS;
			$properties["message_flags"] = PR_MESSAGE_FLAGS;
			$properties["display_name"] = PR_DISPLAY_NAME;
			$properties["smtp_address"] = PR_SMTP_ADDRESS;
			$properties["objecttype"] = PR_OBJECT_TYPE;
			$properties["fileas"] = "PT_STRING8:PSETID_Address:0x8005";
			$properties["email_address_1"] = "PT_STRING8:PSETID_Address:0x8083";
			$properties["email_address_display_name_1"] = "PT_STRING8:PSETID_Address:0x8080";
			$properties["email_address_2"] = "PT_STRING8:PSETID_Address:0x8093";
			$properties["email_address_display_name_2"] = "PT_STRING8:PSETID_Address:0x8090";
			$properties["email_address_3"] = "PT_STRING8:PSETID_Address:0x80A3";
			$properties["email_address_display_name_3"] = "PT_STRING8:PSETID_Address:0x80A0";
			$properties["email_address_entryid_1"] = "PT_BINARY:PSETID_Address:0x8085";
			$properties["email_address_entryid_2"] = "PT_BINARY:PSETID_Address:0x8095";
			$properties["email_address_entryid_3"] = "PT_BINARY:PSETID_Address:0x80A5";
			$properties["email_address_type_1"] = "PT_STRING8:PSETID_Address:0x8082";
			$properties["email_address_type_2"] = "PT_STRING8:PSETID_Address:0x8092";
			$properties["email_address_type_3"] = "PT_STRING8:PSETID_Address:0x80a2";
			$properties["address_book_mv"] = "PT_MV_LONG:PSETID_Address:0x8028";
			$properties["address_book_long"] = "PT_LONG:PSETID_Address:0x8029";

			// distlist items
			$properties["subject"] = PR_SUBJECT;
			$properties["addrtype"] = PR_ADDRTYPE;
			$properties["fileas_selection"] = "PT_LONG:PSETID_Address:0x8006";
			$properties["dl_name"] = "PT_STRING8:PSETID_Address:0x8053";
			$properties["oneoff_members"] = "PT_MV_BINARY:PSETID_Address:0x8054";
			$properties["members"] = "PT_MV_BINARY:PSETID_Address:0x8055";
			$properties["categories"] = "PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords";
	  		$properties["sensitivity"] = PR_SENSITIVITY;
			$properties["private"] = "PT_BOOLEAN:PSETID_Common:0x8506";
			
			$properties["deleted_on"] = PR_DELETED_ON;

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}

		/**
		 * Returns the properties for a distribution list.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for a distribution list.
		 */
		function getDistListProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["parent_entryid"] = PR_PARENT_ENTRYID;
			$properties["icon_index"] = PR_ICON_INDEX;
			$properties["message_class"] = PR_MESSAGE_CLASS;
			$properties["message_flags"] = PR_MESSAGE_FLAGS;
			$properties["display_name"] = PR_DISPLAY_NAME;
			$properties["subject"] = PR_SUBJECT;
			$properties["fileas"] = "PT_STRING8:PSETID_Address:0x8005";
			$properties["fileas_selection"] = "PT_LONG:PSETID_Address:0x8006";
			$properties["dl_name"] = "PT_STRING8:PSETID_Address:0x8053";
			$properties["oneoff_members"] = "PT_MV_BINARY:PSETID_Address:0x8054";
			$properties["members"] = "PT_MV_BINARY:PSETID_Address:0x8055";
			$properties["categories"] = "PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords";
			$properties["sensitivity"] = PR_SENSITIVITY;
			$properties["private"] = "PT_BOOLEAN:PSETID_Common:0x8506";
			// Conflict properties
			$properties["conflictitems"] = PR_CONFLICT_ITEMS;

			$properties["deleted_on"] = PR_DELETED_ON;

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}

		/**
		 * Returns the properties for a contact in the addressbook.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for a contact.
		 */
		function getAddressBookProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["smtp_address"] = PR_SMTP_ADDRESS;
			$properties["email_address"] = PR_EMAIL_ADDRESS;
			$properties["display_name"] = PR_DISPLAY_NAME;
			$properties["display_type"] = PR_DISPLAY_TYPE;
			$properties["display_type_ex"] = PR_DISPLAY_TYPE_EX;
			$properties["account"] = PR_ACCOUNT;
			$properties["addrtype"] = PR_ADDRTYPE;
			$properties["objecttype"] = PR_OBJECT_TYPE;
			$properties["search_key"] = PR_SEARCH_KEY;
			$properties["office_phone"] = PR_OFFICE_TELEPHONE_NUMBER;
			$properties["location"] = PR_OFFICE_LOCATION;
			$properties["fax"] = PR_PRIMARY_FAX_NUMBER;
			$properties["department"] = PR_DEPARTMENT_NAME;

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}

		/**
		 * Returns the details properties for a MAPI_MAILUSER in the addressbook.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for an AB entry.
		 */
		function getAddressBookItemMailuserProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["object_type"] = PR_OBJECT_TYPE;
			$properties["given_name"] = PR_GIVEN_NAME;
			$properties["initials"] = PR_INITIALS;
			$properties["surname"] = PR_SURNAME;
			$properties["display_name"] = PR_DISPLAY_NAME;
			$properties["account"] = PR_ACCOUNT;
			$properties["street_address"] = PR_STREET_ADDRESS;
			$properties["locality"] = PR_LOCALITY;
			$properties["state_or_province"] = PR_STATE_OR_PROVINCE;
			$properties["postal_code"] = PR_POSTAL_CODE;
			$properties["country"] = PR_COUNTRY;
			$properties["title"] = PR_TITLE;
			$properties["company_name"] = PR_COMPANY_NAME;
			$properties["department_name"] = PR_DEPARTMENT_NAME;
			$properties["office_location"] = PR_OFFICE_LOCATION;
			$properties["assistant"] = PR_ASSISTANT;
			$properties["business_telephone_number"] = PR_BUSINESS_TELEPHONE_NUMBER;
			$properties["business2_telephone_number"] = PR_BUSINESS2_TELEPHONE_NUMBER;
			$properties["business2_telephone_number_mv"] = PR_BUSINESS2_TELEPHONE_NUMBER_MV;
			$properties["primary_fax_number"] = PR_PRIMARY_FAX_NUMBER;
			$properties["home_telephone_number"] = PR_HOME_TELEPHONE_NUMBER;
			$properties["home2_telephone_number"] = PR_HOME2_TELEPHONE_NUMBER;
			$properties["home2_telephone_number_mv"] = PR_HOME2_TELEPHONE_NUMBER_MV;
			$properties["mobile_telephone_number"] = PR_MOBILE_TELEPHONE_NUMBER;
			$properties["pager_telephone_number"] = PR_PAGER_TELEPHONE_NUMBER;
			$properties["comment"] = PR_COMMENT;
			$properties["ems_ab_manager"] = PR_EMS_AB_MANAGER;
			$properties["ems_ab_reports"] = PR_EMS_AB_REPORTS;
			$properties["ems_ab_reports_mv"] = PR_EMS_AB_REPORTS_MV;
			$properties["ems_ab_is_member_of_dl"] = PR_EMS_AB_IS_MEMBER_OF_DL;
			$properties["ems_ab_proxy_addresses"] = PR_EMS_AB_PROXY_ADDRESSES;
			$properties["ems_ab_proxy_addresses_mv"] = PR_EMS_AB_PROXY_ADDRESSES_MV;

			// Allowing to hook in and add more properties
			$GLOBALS['PluginManager']->triggerHook("server.core.properties.addressbookitem.mailuser", array(
				'properties' =>& $properties,
				'store' => $store !== null ? $store : $this->store
			));

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}

		/**
		 * Returns the details properties for a MAPI_DISTLIST in the addressbook.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for an AB entry.
		 */
		function getAddressBookItemDistlistProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["object_type"] = PR_OBJECT_TYPE;
			$properties["display_name"] = PR_DISPLAY_NAME;
			$properties["account"] = PR_ACCOUNT;
			$properties["comment"] = PR_COMMENT;
			$properties["ems_ab_owner"] = PR_EMS_AB_OWNER;
			$properties["ems_ab_is_member_of_dl"] = PR_EMS_AB_IS_MEMBER_OF_DL;
			$properties["ems_ab_proxy_addresses"] = PR_EMS_AB_PROXY_ADDRESSES;
			$properties["ems_ab_proxy_addresses_mv"] = PR_EMS_AB_PROXY_ADDRESSES_MV;

			// Allowing to hook in and add more properties
			$GLOBALS['PluginManager']->triggerHook("server.core.properties.addressbookitem.distlist", array(
				'properties' =>& $properties,
				'store' => $store !== null ? $store : $this->store
			));

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}

		/**
		 * Returns the details properties for the manager AB entry in the addressbook.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for an AB entry.
		 */
		function getAddressBookItemABObjectProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["display_name"] = PR_DISPLAY_NAME;
			$properties["entryid"] = PR_ENTRYID;
			$properties["object_type"] = PR_OBJECT_TYPE;
			$properties["instance_key"] = PR_INSTANCE_KEY;
			$properties["display_type"] = PR_DISPLAY_TYPE;
			$properties["smtp_address"] = PR_SMTP_ADDRESS;
			$properties["account"] = PR_ACCOUNT;
			$properties["rowid"] = PR_ROWID;

			// Allowing to hook in and add more properties
			$GLOBALS['PluginManager']->triggerHook("server.core.properties.addressbookitem.abobject", array(
				'properties' =>& $properties,
				'store' => $store !== null ? $store : $this->store
			));

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}

		/**
		 * Returns the properties for an email.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for an email.
		 */
		function getMailProperties($store = null)
		{
			$this->Init();

			$properties = array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["store_entryid"] = PR_STORE_ENTRYID;
			$properties["parent_entryid"] = PR_PARENT_ENTRYID;
			$properties["message_class"] = PR_MESSAGE_CLASS;
			$properties["icon_index"] = PR_ICON_INDEX;
			$properties["display_to"] = PR_DISPLAY_TO;
			$properties["display_cc"] = PR_DISPLAY_CC;
			$properties["display_bcc"] = PR_DISPLAY_BCC;
			$properties["subject"] = PR_SUBJECT;
			$properties["normalized_subject"] = PR_NORMALIZED_SUBJECT;
			$properties["importance"] = PR_IMPORTANCE;
			$properties["sent_representing_name"] = PR_SENT_REPRESENTING_NAME;
			$properties["sent_representing_email_address"] = PR_SENT_REPRESENTING_EMAIL_ADDRESS;
			$properties["sent_representing_addrtype"] = PR_SENT_REPRESENTING_ADDRTYPE;
			$properties["sent_representing_entryid"] = PR_SENT_REPRESENTING_ENTRYID;
			$properties["sender_email_address"] = PR_SENDER_EMAIL_ADDRESS;
			$properties["sender_name"] = PR_SENDER_NAME;
			$properties["received_by_name"] = PR_RECEIVED_BY_NAME;
			$properties["received_by_email_address"] = PR_RECEIVED_BY_EMAIL_ADDRESS;
			$properties["message_delivery_time"] = PR_MESSAGE_DELIVERY_TIME;
			$properties["last_modification_time"] = PR_LAST_MODIFICATION_TIME;
			$properties["last_verb_executed"] = PR_LAST_VERB_EXECUTED;
			$properties["last_verb_execution_time"] = PR_LAST_VERB_EXECUTION_TIME;
			$properties["hasattach"] = PR_HASATTACH;
			$properties["message_size"] = PR_MESSAGE_SIZE;
			$properties["message_flags"] = PR_MESSAGE_FLAGS;
			$properties["flag_status"] = PR_FLAG_STATUS;
			$properties["flag_complete_time"] = PR_FLAG_COMPLETE_TIME;
			$properties["flag_icon"] = PR_FLAG_ICON;
			$properties["reminder_time"] = "PT_SYSTIME:PSETID_Common:0x8502";
			$properties["reminder_set"] = "PT_BOOLEAN:PSETID_Common:0x8503";
			$properties["flag_request"] = "PT_STRING8:PSETID_Common:0x8530";
			$properties["flag_due_by"] = "PT_SYSTIME:PSETID_Common:0x8560";
			$properties["reply_requested"] = PR_REPLY_REQUESTED;
			$properties["reply_time"] = PR_REPLY_TIME;
			$properties["response_requested"] = PR_RESPONSE_REQUESTED;
			$properties["client_submit_time"] = PR_CLIENT_SUBMIT_TIME;
			$properties["sensitivity"] = PR_SENSITIVITY;
			$properties["read_receipt_requested"] = PR_READ_RECEIPT_REQUESTED;
			$properties["location"] = "PT_STRING8:PSETID_Appointment:0x8208";
			$properties["categories"] = "PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords";
			$properties["transport_message_headers"] = PR_TRANSPORT_MESSAGE_HEADERS;
			$properties["contacts"] = "PT_MV_STRING8:PSETID_Common:0x853A";
			$properties["contacts_string"] = "PT_STRING8:PSETID_Common:0x8586";
			// Meeting request properties
			$properties["startdate"] = "PT_SYSTIME:PSETID_Appointment:0x820d";
			$properties["duedate"] = "PT_SYSTIME:PSETID_Appointment:0x820e";
			$properties["location"] = "PT_STRING8:PSETID_Appointment:0x8208";
			$properties["recurring_pattern"] = "PT_STRING8:PSETID_Appointment:0x8232";
			$properties["recurring"] = "PT_BOOLEAN:PSETID_Appointment:0x8223";
			// Conflict properties
			$properties["conflictitems"] = PR_CONFLICT_ITEMS;
			
			// Propose new time properties
			$properties["proposed_start_whole"] = "PT_SYSTIME:PSETID_Appointment:0x8250";
			$properties["proposed_end_whole"] = "PT_SYSTIME:PSETID_Appointment:0x8251";
			$properties["proposed_duration"] = "PT_LONG:PSETID_Appointment:0x8256";
			$properties["counter_proposal"] = "PT_BOOLEAN:PSETID_Appointment:0x8257";

			$properties["deleted_on"] = PR_DELETED_ON;
			$properties["updatecounter"] = "PT_LONG:PSETID_Appointment:0x8201";
			$properties["hideattachments"] = "PT_BOOLEAN:PSETID_Common:0x8514";

			// meeting request properties, which will give the response status
			$properties["responsestatus"] = "PT_LONG:PSETID_Appointment:0x8218";

			// meeting request properties, which will be set when mr is auto forwarded using delegate meeting request rule
			$properties["rcvd_representing_name"] = PR_RCVD_REPRESENTING_NAME;
			$properties["delegated_by_rule"] = PR_DELEGATED_BY_RULE;
			$properties["goid"] = "PT_BINARY:PSETID_Meeting:0x3";
			$properties["goid2"] = "PT_BINARY:PSETID_Meeting:0x23";

			// Archiver property
			$properties["stubbed"] = "PT_BOOLEAN:PSETID_Archive:stubbed";

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}
		
		/**
		 * Returns the properties for a sticky note.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for a sticky note.
		 */
		function getStickyNoteProperties($store = null)
		{
			$this->Init();
			
			$properties = Array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["parent_entryid"] = PR_PARENT_ENTRYID;
			$properties["icon_index"] = PR_ICON_INDEX;
			$properties["message_class"] = PR_MESSAGE_CLASS;
			$properties["message_flags"] = PR_MESSAGE_FLAGS;
			$properties["last_modification_time"] = PR_LAST_MODIFICATION_TIME;
			$properties["subject"] = PR_SUBJECT;
			$properties["body"] = PR_BODY;
			$properties["html"] = PR_HTML;
			$properties["rtf_compressed"] = PR_RTF_COMPRESSED;
			$properties["creation_time"] = PR_CREATION_TIME;
			$properties["color"] = "PT_LONG:PSETID_Note:0x8B00";
			$properties["categories"] = "PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords";
			// Conflict properties
			$properties["conflictitems"] = PR_CONFLICT_ITEMS;

			$properties["deleted_on"] = PR_DELETED_ON;
			
			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}
		
		/**
		 * Returns the properties for a task.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties for a task.
		 */
		function getTaskProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["parent_entryid"] = PR_PARENT_ENTRYID;
			$properties["store_entryid"] = PR_STORE_ENTRYID;
			$properties["icon_index"] = PR_ICON_INDEX;
			$properties["message_class"] = PR_MESSAGE_CLASS;
			$properties["message_flags"] = PR_MESSAGE_FLAGS;
			$properties["subject"] = PR_SUBJECT;
			$properties["importance"] = PR_IMPORTANCE;
			$properties["sensitivity"] = PR_SENSITIVITY;
			$properties["last_modification_time"] = PR_LAST_MODIFICATION_TIME;
			$properties["complete"] = "PT_BOOLEAN:PSETID_Task:0x811c";
			$properties["duedate"] = "PT_SYSTIME:PSETID_Task:0x8105";
			$properties["status"] = "PT_LONG:PSETID_Task:0x8101";
			$properties["percent_complete"] = "PT_DOUBLE:PSETID_Task:0x8102";
			$properties["startdate"] = "PT_SYSTIME:PSETID_Task:0x8104";
			$properties["owner"] = "PT_STRING8:PSETID_Task:0x811f";
			$properties["reminder"] = "PT_BOOLEAN:PSETID_Common:0x8503";
			$properties["reminderdate"] = "PT_SYSTIME:PSETID_Common:0x8502";
			$properties["datecompleted"] = "PT_SYSTIME:PSETID_Task:0x810f";
			$properties["totalwork"] = "PT_LONG:PSETID_Task:0x8111";
			$properties["actualwork"] = "PT_LONG:PSETID_Task:0x8110";
			$properties["companies"] = "PT_MV_STRING8:PSETID_Common:0x8539";
			$properties["mileage"] = "PT_STRING8:PSETID_Common:0x8534";
			$properties["billinginformation"] = "PT_STRING8:PSETID_Common:0x8535";
			$properties["private"] = "PT_BOOLEAN:PSETID_Common:0x8506";
			$properties["contacts"] = "PT_MV_STRING8:PSETID_Common:0x853a";
			$properties["contacts_string"] = "PT_STRING8:PSETID_Common:0x8586";
			$properties["categories"] = "PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords";
			$properties["reminder_time"] = "PT_SYSTIME:PSETID_Common:0x8502";
			$properties["commonstart"] = "PT_SYSTIME:PSETID_Common:0x8516";
			$properties["commonend"] = "PT_SYSTIME:PSETID_Common:0x8517";
			$properties["commonassign"] = "PT_LONG:PSETID_Common:0x8518";
			$properties["flagdueby"] = "PT_SYSTIME:PSETID_Common:0x8560";
			$properties["recurring"] = "PT_BOOLEAN:PSETID_Task:0x8126";
			$properties["dead_occurrence"] = "PT_BOOLEAN:PSETID_Task:0x8109";
			// Conflict properties
			$properties["conflictitems"] = PR_CONFLICT_ITEMS;

			$properties["deleted_on"] = PR_DELETED_ON;
			
			$properties["sent_representing_entryid"] = PR_SENT_REPRESENTING_ENTRYID;
			$properties["sent_representing_name"] = PR_SENT_REPRESENTING_NAME;
			$properties["sent_representing_addrtype"] = PR_SENT_REPRESENTING_ADDRTYPE;
			$properties["sent_representing_email_address"] = PR_SENT_REPRESENTING_EMAIL_ADDRESS;
			$properties["sent_representing_search_key"] = PR_SENT_REPRESENTING_SEARCH_KEY;
			$properties["sender_email_address"] = PR_SENDER_EMAIL_ADDRESS;
			$properties["sender_name"] = PR_SENDER_NAME;
			$properties["sender_addrtype"] = PR_SENDER_ADDRTYPE;
			$properties["sender_entryid"] = PR_SENDER_ENTRYID;
			$properties["sender_search_key"] = PR_SENDER_SEARCH_KEY;

			$properties["updatecount"] = "PT_LONG:PSETID_Task:0x8112";
			$properties["taskstate"] = "PT_LONG:PSETID_Task:0x8113";
			$properties["taskmultrecips"] = "PT_LONG:PSETID_Task:0x8120";
			$properties["taskupdates"] = "PT_BOOLEAN:PSETID_Task:0x811b";
			$properties["tasksoc"] = "PT_BOOLEAN:PSETID_Task:0x8119";
			$properties["taskhistory"] = "PT_LONG:PSETID_Task:0x811a";
			$properties["taskmode"] = "PT_LONG:PSETID_Common:0x8518";
			$properties["taskglobalobjid"] = "PT_BINARY:PSETID_Common:0x8519";
			$properties["assignedtime"] = "PT_SYSTIME:PSETID_Task:0x8115";
			$properties["taskfcreator"] = "PT_BOOLEAN:PSETID_Task:0x0x811e";
			$properties["tasklastuser"] = "PT_STRING8:PSETID_Task:0x8122";
			$properties["tasklastdelegate"] = "PT_STRING8:PSETID_Task:0x8125";
			$properties["taskaccepted"] = "PT_BOOLEAN:PSETID_Task:0x8108";
			$properties["ownership"] = "PT_LONG:PSETID_Task:0x8129";
			$properties["delegationstate"] = "PT_LONG:PSETID_Task:0x812a";

			$properties["display_cc"] = PR_DISPLAY_CC;

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}

		/**
		* Returns the properties used for categories dialog
		* @param MAPIStore $store store to use to get property tags
		* @return array properties
		*/
		function getCategoryProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["parent_entryid"] = PR_PARENT_ENTRYID;
			$properties["categories"] = "PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords";

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}
	
		/**
		 * Returns the properties used by the "properties" dialog.
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties.
		 */
		function getFolderProperties($store = null)
		{
			$this->Init();
			
			$properties = Array();
			$properties["entryid"] = PR_ENTRYID;
			$properties["parent_entryid"] = PR_PARENT_ENTRYID;
			$properties["store_entryid"] = PR_STORE_ENTRYID;
			$properties["display_name"] = PR_DISPLAY_NAME;
			$properties["creation_time"] = PR_CREATION_TIME;
			$properties["content_count"] = PR_CONTENT_COUNT;
			$properties["content_unread"] = PR_CONTENT_UNREAD;
			$properties["comment"] = PR_COMMENT;
			$properties["container_class"] = PR_CONTAINER_CLASS;
			$properties["subfolders"] = PR_SUBFOLDERS;

//			$properties["parent_display_name"] = PR_DISPLAY_NAME; // will be filled in by module
			$properties["message_size"] = PR_MESSAGE_SIZE; // will be filled in by module if it does not exist
			
			$properties["deleted_on"] = PR_DELETED_ON;

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}
		
		/**
		 * Returns the properties used for the reminders dialog
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties
		 */
		function getReminderProperties($store = null)
		{
			$this->Init();
			
			$properties = Array();
	  		$properties["entryid"] = PR_ENTRYID;
	  		$properties["parent_entryid"] = PR_PARENT_ENTRYID;
	  		$properties["message_class"] = PR_MESSAGE_CLASS;
			$properties["message_flags"] = PR_MESSAGE_FLAGS;
	  		$properties["icon_index"] = PR_ICON_INDEX;
	  		$properties["subject"] = PR_SUBJECT;

			$properties["reminder"] = "PT_BOOLEAN:PSETID_Common:0x8503";				// PidLidReminderSet
			$properties["reminder_minutes"] = "PT_LONG:PSETID_Common:0x8501";			// PidLidReminderDelta
			$properties["remindertime"] = "PT_SYSTIME:PSETID_Common:0x8502";			// PidLidReminderTime
			$properties["flagdueby"] = "PT_SYSTIME:PSETID_Common:0x8560";				// PidLidReminderSignalTime

			$properties["task_duedate"] = "PT_SYSTIME:PSETID_Task:0x8105";				// PidLidTaskDueDate
			$properties["task_resetreminder"] = "PT_BOOLEAN:PSETID_Task:0x8107";		// PidLidTaskResetReminder
			$properties["task_recurring"] = "PT_BOOLEAN:PSETID_Task:0x8126";			// PidLidTaskFRecurring

			$properties["appointment_recurring"] = "PT_BOOLEAN:PSETID_Appointment:0x8223";			// PidLidRecurring
			$properties["appointment_startdate"] = "PT_SYSTIME:PSETID_Appointment:0x820d";
			$properties["appointment_enddate"] = "PT_SYSTIME:PSETID_Appointment:0x820e";
			$properties["appointment_recurring_data"] = "PT_BINARY:PSETID_Appointment:0x8216";
			$properties["appointment_timezone_data"] = "PT_BINARY:PSETID_Appointment:0x8233";
			$properties["appointment_startdate_recurring"] = "PT_SYSTIME:PSETID_Appointment:0x8235";
			$properties["appointment_enddate_recurring"] = "PT_SYSTIME:PSETID_Appointment:0x8236";

			$properties["commonend"] = "PT_SYSTIME:PSETID_Common:0x8517";
			
			$properties["deleted_on"] = PR_DELETED_ON;

			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}


		/**
		 * Return properties used in rules
		 * @param MAPIStore $store store to use to get property tags
		 * @return array properties
		 */
		function getRulesProperties($store = null)
		{
			$this->Init();
			
			$properties = array();
			$properties["rule_id"] = PR_RULE_ID;
			$properties["rule_name"] = PR_RULE_NAME;
			$properties["rule_sequence"] = PR_RULE_SEQUENCE;
			$properties["rule_state"] = PR_RULE_STATE;
			$properties["rule_condition"] = PR_RULE_CONDITION;
			$properties["rule_actions"] = PR_RULE_ACTIONS;
			$properties["rule_provider"] = PR_RULE_PROVIDER;
			$properties["rule_provider_data"] = PR_RULE_PROVIDER_DATA;
			$properties["rule_level"] = PR_RULE_LEVEL;
			
			return getPropIdsFromStrings($store !== null ? $store : $this->store, $properties);
		}
}
?>
