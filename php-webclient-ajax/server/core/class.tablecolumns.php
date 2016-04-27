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
class TableColumns {
	/**
	 * Constructor
	 */
	function TableColumns() {
		
	}

	/**
	 * Creates tablecolumns array for appointmentlist.
	 * @return		array		$tableColumns		tablecolumns for appointmentlist.
	 */
	function getAppointmentListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "icon_index", true, 0, _("Icon Index"), _("Sort on Icon Index"));
		$this->addColumn($tableColumns, "alldayevent", true, 0, _("All Day"), _("Sort on All Day Event"));		
		$this->addColumn($tableColumns, "subject", true, 2, _("Subject"), _("Sort on Subject"),PERCENTAGE);
		$this->addColumn($tableColumns, "startdate", true, 3, _("Start date"), _("Sort on Startdate"));
		$this->addColumn($tableColumns, "duedate", true, 4, _("End date"), _("Sort on Enddate"));
		$this->addColumn($tableColumns, "location", true, 5, _("Location"), _("Sort on location"));
		$this->addColumn($tableColumns, "meeting", true, 7, _("Meeting Status"), _("Sort on meeting status"));
		$this->addColumn($tableColumns, "duration", true, 6, _("Duration"), _("Sort on Duration"), 60);
		$this->addColumn($tableColumns, "body", false, 8, _("Message"), _("Sort on Message"), PERCENTAGE);
		$this->addColumn($tableColumns, "creation_time", false, 5, _("Created on"), _("Sort on Created Date"));

		// Columns for Reminder
		$this->addColumn($tableColumns, "reminder", false, 3, _("Reminder"), _("Sort on Reminder"));
		$this->addColumn($tableColumns, "flagdueby", false, 9, _("Due By"), _("Sort on Due By"));

		// Columns for Recurring Events
		$this->addColumn($tableColumns, "recurring", true, 1, _("Recurring"), _("Sort on Recurring"));
		$this->addColumn($tableColumns, "recurring_pattern", false, 4, _("Recurring Pattern"), _("Sort on Recurring"), 450);
		$this->addColumn($tableColumns, "startdate_recurring", false, 5, _("Recurring Startdate"), _("Sort on Recurring"));
		$this->addColumn($tableColumns, "enddate_recurring", false, 6, _("Recurring Enddate"), _("Sort on Recurring"));
	
		$this->addColumn($tableColumns, "categories", false, 9, _("Categories"), _("Sort on Categories"));
		$this->addColumn($tableColumns, "sensitivity", false, 10, _("Sensitivity"), _("Sort on Sensitivity"), 80);
		$this->addColumn($tableColumns, "label", false, 9, _("Label"), _("Sort on Label"), 90);
		$this->addColumn($tableColumns, "contacts", false, 10, _("Contacts"), _("Sort on Contact"));

		// Columns for Attendees
		$this->addColumn($tableColumns, "display_to", false, 11, _("Required Attendee"), _("Sort on Required Attendee"));
		$this->addColumn($tableColumns, "display_cc", false, 12, _("Optional Attendee"), _("Sort on Optional Attendee"));
		$this->addColumn($tableColumns, "display_bcc", false, 13, _("Resource"), _("Sort on Resource"));

		$this->addColumn($tableColumns, "busystatus", false, 5, _("Busy Status"), _("Sort on Busy Status"));

		return $tableColumns;
	}

	/**
	 * Creates tablecolumns array for contactlist.
	 * @return		array		$tableColumns		tablecolumns for contactlist.
	 */
	function getContactListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "icon_index", true, 0, _("Icon"), _("Sort on Icon"));
		$this->addColumn($tableColumns, "fileas", true, 1, _("File As"), _("Sort on File As"));
		$this->addColumn($tableColumns, "display_name", true, 2, _("Full Name"), _("Sort on Display Name"), PERCENTAGE);
		$this->addColumn($tableColumns, "email_address_1", true, 3, _("Email Address 1"), _("Sort on Email Address 1"));
		$this->addColumn($tableColumns, "email_address_2", false, 3, _("Email Address 2"), _("Sort on Email Address 2"));
		$this->addColumn($tableColumns, "email_address_3", false, 3, _("Email Address 3"), _("Sort on Email Address 3"));
		$this->addColumn($tableColumns, "home_telephone_number", true, 4, _("Home Telephone Number"), _("Sort on Home Telephone Number"));
		$this->addColumn($tableColumns, "cellular_telephone_number", true, 5, _("Cellular Telephone Number"), _("Sort on Cellular Telephone Number"));
		$this->addColumn($tableColumns, "categories", false, 4, _("Categories"), _("Sort on Categories"));

		$this->addColumn($tableColumns, "business_telephone_number", false, 99, _("Business Telephone Number"), _("Sort on Business Telephone Number"));
		$this->addColumn($tableColumns, "office_telephone_number", false, 99, _("Office Telephone Number"), _("Sort on Office Telephone Number"));
		$this->addColumn($tableColumns, "business_fax_number", false, 99, _("Business Fax Number"), _("Sort on Business Fax Number"));
		$this->addColumn($tableColumns, "title", false, 99, _("Function"), _("Sort on Function Name"));
		$this->addColumn($tableColumns, "company_name", false, 99, _("Company Name"), _("Sort on Company Name"));
		$this->addColumn($tableColumns, "department_name", false, 99, _("Department Name"), _("Sort on Department Name"));
		$this->addColumn($tableColumns, "office_location", false, 99, _("Office Location"), _("Sort on Office Location"));
		$this->addColumn($tableColumns, "profession", false, 99, _("Profession"), _("Sort on Profession"));
		$this->addColumn($tableColumns, "manager_name", false, 99, _("Manager name"), _("Sort on Manager Name"));
		$this->addColumn($tableColumns, "assistant", false, 99, _("Assistant"), _("Sort on Assistant"));
		$this->addColumn($tableColumns, "nickname", false, 99, _("Nickname"), _("Sort on Nickname"));
		$this->addColumn($tableColumns, "spouse_name", false, 99, _("Spouse Name"), _("Sort on Spouse Name"));
		$this->addColumn($tableColumns, "birthday", false, 99, _("Birthday"), _("Sort on Birthday"));
		$this->addColumn($tableColumns, "business_address", false, 99, _("Business Address"), _("Sort on Business Address"));
		$this->addColumn($tableColumns, "home_address", false, 99, _("Home Address"), _("Sort on Home Address"));
		$this->addColumn($tableColumns, "other_address", false, 99, _("Office Address"), _("Sort on Other Address"));
		$this->addColumn($tableColumns, "mailing_address", false, 99, _("Mailing Address"), _("Sort on Mailing Address"));
		$this->addColumn($tableColumns, "im", false, 99, _("IM"), _("Sort on IM"));
		$this->addColumn($tableColumns, "webpage", false, 99, _("Webpage"), _("Sort on Webpage"));
		$this->addColumn($tableColumns, "assistant_telephone_number", false, 99, _("Assistant Telephone Number"), _("Sort On Assistant Telephone Number"));
		$this->addColumn($tableColumns, "business2_telephone_number", false, 99, _("Business Telephone Number 2"), _("Sort On Business Telephone Number 2"));
		$this->addColumn($tableColumns, "callback_telephone_number", false, 99, _("Callback Telephone Number"), _("Sort On Callback Telephone Number"));
		$this->addColumn($tableColumns, "car_telephone_number", false, 99, _("Car Telephone Number"), _("Sort On Car Telephone Number"));
		$this->addColumn($tableColumns, "company_telephone_number", false, 99, _("Company Telephone Number"), _("Sort On Company Telephone Number"));
		$this->addColumn($tableColumns, "home2_telephone_number", false, 99, _("Home Telephone Number 2"), _("Sort On Home Telephone Number 2"));
		$this->addColumn($tableColumns, "home_fax_number", false, 99, _("Home Fax Number"), _("Sort On Home Fax Number"));
		$this->addColumn($tableColumns, "isdn_number", false, 99, _("ISDN Number"), _("Sort On ISDN number"));
		$this->addColumn($tableColumns, "other_telephone_number", false, 99, _("Other Telephone Number"), _("Sort On Other Telephone Number"));
		$this->addColumn($tableColumns, "pager_telephone_number", false, 99, _("Pager Number"), _("Sort On Pager Number"));

		return $tableColumns;
	}

	/**
	 * Creates tablecolumns array for contacts folder of addressbooklist.
	 * @return		array		$tableColumns		tablecolumns for addressbooklist.
	 */
	function getContactABListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "icon_index", true, 0, _("Icon"), _("Sort on Icon"), 25);
		$this->addColumn($tableColumns, "display_name", true, 1, _("Display Name"), _("Sort on Display Name"), "35%");
		$this->addColumn($tableColumns, "email_address", true, 2, _("Email Address"), _("Sort on Email Address"), PERCENTAGE);
		$this->addColumn($tableColumns, "fileas", true, 3, _("File as"), _("Sort on File As"), "30%");
		$this->addColumn($tableColumns, "entryid", false);
		$this->addColumn($tableColumns, "email_address_display_name_1", false);
		$this->addColumn($tableColumns, "email_address_2", false);
		$this->addColumn($tableColumns, "email_address_display_name_2", false);
		$this->addColumn($tableColumns, "email_address_3", false);
		$this->addColumn($tableColumns, "email_address_display_name_3", false);
		$this->addColumn($tableColumns, "message_class", false);

		return $tableColumns;
	}

	/**
	 * Creates tablecolumns array for addressbooklist.
	 * @return		array		$tableColumns		tablecolumns for addressbooklist.
	 */
	function getAddressBookListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "display_type", true, 0, _("Icon"), _("Sort on Icon"), 25);
		$this->addColumn($tableColumns, "display_name", true, 1, _("Display Name"), _("Sort on Display Name"), "15%");
		$this->addColumn($tableColumns, "smtp_address", true, 2, _("Email Address"), _("Sort on Email Address"), PERCENTAGE);
		$this->addColumn($tableColumns, "fileas", true, 3, _("File as"), _("Sort on File As"), "15%");
		$this->addColumn($tableColumns, "department", true, 4, _("Department"), _("Sort on Department"), "10%");
		$this->addColumn($tableColumns, "office_phone", true, 5, _("Business Phone"), _("Sort on Business Phone"), "13%");
		$this->addColumn($tableColumns, "location", true, 6, _("Location"), _("Sort on Location"), "10%");
		$this->addColumn($tableColumns, "fax", true, 7, _("Fax"), _("Sort on Fax"), "10%");
		$this->addColumn($tableColumns, "entryid", false);
		$this->addColumn($tableColumns, "message_class", false);

		return $tableColumns;
	}

	/**
	 * Creates tablecolumns array for reminderlist.
	 * @return		array		$tableColumns		tablecolumns for reminderlist.
	 */
	function getReminderListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "icon_index", true, 0, _("Icon"), _("Sort on Icon"));
		$this->addColumn($tableColumns, "subject", true, 1, _("Subject"), _("Sort on subject"));
		$this->addColumn($tableColumns, "remindertime", true, 2, _("Due in"), _("Sort on Due In"));

		return $tableColumns;
	}

	/**
	 * Creates tablecolumns array for journallist.
	 * @return		array		$tableColumns		tablecolumns for journallist.
	 */
	function getJournalListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "importance", false, 0, _("Priority"), _("Sort on Priority"));
		$this->addColumn($tableColumns, "icon_index", true, 1, _("Icon"), _("Sort on Icon"));
		$this->addColumn($tableColumns, "hasattach", true, 2, _("Attachments"), _("Sort on Attachments"));
		$this->addColumn($tableColumns, "subject", true, 4, _("Subject"), _("Sort on Subject"), PERCENTAGE);
		$this->addColumn($tableColumns, "message_size", false, 6, _("Size"), _("Sort on Size"), 80);
		$this->addColumn($tableColumns, "flag_status", false, 7, _("Flag Status"), _("Sort on Flag Status"));
		$this->addColumn($tableColumns, "categories", false, 8, _("Categories"), _("Sort on Categories"));
		$this->addColumn($tableColumns, "sent_representing_name", true, 3, _("From"), _("Sort on Sender"));
		$this->addColumn($tableColumns, "message_delivery_time", true, 5, _("Received"), _("Sort on Received Date"));
		$this->addColumn($tableColumns, "display_to", false, 3, _("To"), _("Sort on Recipient"));
		$this->addColumn($tableColumns, "client_submit_time", false, 5, _("Sent"), _("Sort on Sent Date"));

		return $tableColumns;
	}

	/**
	 * Creates tablecolumns array for maillist.
	 * @return		array		$tableColumns		tablecolumns for maillist.
	 */
	function getMailListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "importance", true, 0, _("Priority"), _("Sort on Priority"));
		$this->addColumn($tableColumns, "icon_index", true, 1, _("Icon"), _("Sort on Icon"));
		$this->addColumn($tableColumns, "hasattach", true, 2, _("Attachments"), _("Sort on Attachments"));
		$this->addColumn($tableColumns, "subject", true, 4, _("Subject"), _("Sort on Subject"), PERCENTAGE);
		$this->addColumn($tableColumns, "message_size", true, 6, _("Size"), _("Sort on Size"), 80);
		$this->addColumn($tableColumns, "flag_icon", true, 7, _("Flag Status"), _("Sort on Flag Status"));
		$this->addColumn($tableColumns, "categories", false, 8, _("Categories"), _("Sort on Categories"));

		// Columns & Sort: Mail Folders
		$this->addColumn($tableColumns, "sent_representing_name", false, 3, _("From"), _("Sort on Sender"));
		$this->addColumn($tableColumns, "message_delivery_time", false, 5, _("Received"), _("Sort on Received Date"));
		
		// Columns & Sort: Outbox & Sent Items
		$this->addColumn($tableColumns, "display_to", false, 3, _("To"), _("Sort on Recipient"));
		$this->addColumn($tableColumns, "client_submit_time", false, 5, _("Sent"), _("Sort on Sent Date"));
		$this->addColumn($tableColumns, "last_modification_time", false, 5, _("Modification"), _("Sort on Modification Date"));

		return $tableColumns;
	}

	/**
	 * Creates tablecolumns array for tasklist.
	 * @return		array		$tableColumns		tablecolumns for tasklist.
	 */
	function getTaskListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "icon_index", true, 0, _("Icon"), _("Sort on Icon"), 25);
		$this->addColumn($tableColumns, "complete", true, 1, _("Complete"), _("Sort on Complete"), 25);
		$this->addColumn($tableColumns, "importance", true, 2, _("Priority"), _("Sort on Priority"));
		$this->addColumn($tableColumns, "subject", true, 3, _("Subject"), _("Sort on Subject"), PERCENTAGE);
		$this->addColumn($tableColumns, "duedate", true, 4, _("Due Date"), _("Sort on Due Date"));
		$this->addColumn($tableColumns, "owner", true, 5, _("Owner"), _("Sort on Owner"));
		$this->addColumn($tableColumns, "categories", false, 6, _("Categories"), _("Sort on Categories"));
		$this->addColumn($tableColumns, "percent_complete", false, 7, _("% Completed"), _("Sort on Percent Completed"), 100);
		$this->addColumn($tableColumns, "hidden_column", true, 8);
		$this->addColumn($tableColumns, "startdate", true, 9, _("Start Date"), _("Sort on Start Date"));
		$this->addColumn($tableColumns, "recurring", false, 10, _("Recurring"), _("Sort on Recurring"));

		return $tableColumns;
	}

	/**
	 * Creates inputcolumns array for tasklist.
	 * @param		array		$columnData			column data
	 * @return		array		$tableColumns		inputcolumns for tasklist.
	 */
	function getTaskListInputColumns($columnData) {
		$tableColumns = array();

		$this->addInputColumn($tableColumns, "icon", "null", true, "", 0, "icon_index",false,  23);
		$this->addInputColumn($tableColumns, "complete", "checkbox", true, "", 1, "complete",false,  23);
		$this->addInputColumn($tableColumns, "importance", "importance", true, "", 2, "importance",false);
		$this->addInputColumn($tableColumns, "subject", "textbox", true, "", 3, "subject",_("Click here to add a new item"),PERCENTAGE);
		$this->addInputColumn($tableColumns, "text_duedate", "datepicker", true, "readonly", 4,"duedate",false,150);
		$this->addInputColumn($tableColumns, "owner", "textbox", true, "", 5, "owner", "", 150);
		$this->addInputColumn($tableColumns, "categories", "categories", false, "", 6, "categories",false, 150);
		$this->addInputColumn($tableColumns, "text_percent_complete", "percent", false, "readonly", 7,"percent_complete",_("0%"), 100);
		$this->addInputColumn($tableColumns, "hiddencolumn", "hidden", false, "", 8,"hidden_column");
		$this->addInputColumn($tableColumns, "text_startdate", "datepicker", true, "readonly", 9, "startdate", false, 150);
		$this->addInputColumn($tableColumns, "recurring", "null", false, "", 10, "recurring", false, 23);

		/** 
		 * structure of $columnData must be
		 * $columnData[column_id][property] = value
		 */
		// set property values that are passed in $columnData
		foreach($columnData as $columnId => $columnProperty) {
			if(is_array($columnProperty) && count($columnProperty) > 0) {
				foreach($columnProperty as $prop => $val) {
					$this->changeColumnPropertyValue($tableColumns, $columnId, $prop, $val);
				}
			}
		}

		return $tableColumns;
	}

	/**
	 * Creates tablecolumns array for stickynotelist.
	 * @return		array		$tableColumns		tablecolumns for stickynotelist.
	 */
	function getStickyNoteListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "icon_index", true, 0, _("Icon"), _("Sort on Icon"));
		$this->addColumn($tableColumns, "subject", false, 1, _("Subject"), _("Sort on Subject"), PERCENTAGE);
		$this->addColumn($tableColumns, "body", true, 2, _("Body"), _("Sort on Body"), PERCENTAGE);
		$this->addColumn($tableColumns, "creation_time", true, 3, _("Created"), _("Sort on Created"));
		$this->addColumn($tableColumns, "categories", false, 4, _("Categories"), _("Sort on Categories"));

		return $tableColumns;
	}

	/**
	 * Creates tablecolumns array for rulelist.
	 * @return		array		$tableColumns		tablecolumns for rulelist.
	 */
	function getRuleListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "rule_id", false, 0, _("Id"), _("Sort on Id"));
		$this->addColumn($tableColumns, "rule_state", true, 0, " ", _("Sort on Enabled"), 22, "checkbox|ST_ENABLED");
		$this->addColumn($tableColumns, "rule_name", true, 0, _("Name"), _("Sort on Name"), PERCENTAGE);

		return $tableColumns;
	}

	/**
	 * Creates tablecolumns array for distlist.
	 * @return		array		$tableColumns		tablecolumns for distlist.
	 */
	function getDistListTableColumns() {
		$tableColumns = array();

		$this->addColumn($tableColumns, "icon_index", true, 0, _("Icon"));
		$this->addColumn($tableColumns, "name", true, 1, _("Full Name"), _("Name"), PERCENTAGE);
		$this->addColumn($tableColumns, "address", true, 2, _("E-mail"), _("E-mail"), 400);

		return $tableColumns;
	}

	/**
	 * Function which sets the column properties. This function is used for setting
	 * column information, like name and if it is visible in the client view.
	 * @param		array		$tableColumns	reference to an array, which the column will be added
	 * @param		string		$id				id of the column
	 * @param		boolean		$visible		true - column is visible,
	 * 											false - column is not visible
	 * @param		integer		$order			the order in which the columns are visible
	 * @param		string		$name			name of the column
	 * @param		string		$title			title of the column
	 * @param		string		$type			type of column
	 * @param		integer		$length			length of the column (pixels)
	 */
	function addColumn(&$tableColumns, $id, $visible, $order = false, $name = false, $title = false, $length = false, $type = false) {
		$column = array();

		$column["id"] = $id;
		$column["visible"] = $visible;

		if($order !== false && $name && $title) {
			$column["name"] = $name;
			$column["title"] = $title;
			$column["order"] = $order;

			if($length) {
				$column["length"] = $length;
			}
		}

		if($type) {
			$column["type"] = $type;
		}

		array_push($tableColumns, $column);
	}

	/**
	 * Function which sets the inputcolumn properties. This function is used for setting
	 * column information, like name and if it is visible in the client view.
	 * @param		array 		$tableColumns 	reference to an array, which the column will be added
	 * @param		string 		$id 			id of the input type fields
	 * @param		string 		$type 			the type of column
	 * @param		boolean 	$visible 		true - column is visible,
	 *											false - column is not visible		 
	 * @param		boolean 	$readonly		true - column is readonly,
	 *											false - column is not editable
	 * @param		integer 	$order 			order in which the columns are visible
	 * @param		string 		$name 			id` of the corresponding column in the header row
	 * @param		string 		$title 			title of the column
	 * @param		integer 	$length 		length of the column (pixels)
	 */
	function addInputColumn(&$tableColumns, $id, $type, $visible, $readonly, $order = false, $name = false, $title = false, $length = false) {
		$column = array();

		$column["id"] = $id;
		$column["type"] = $type;
		$column["visible"] = $visible;
		$column["readonly"] = $readonly;
		$column["name"] = $name;

		if($order !== false) {
			$column["title"] = $title;
			$column["order"] = $order;
		}

		if($length) {
			$column["length"] = $length;
		}

		array_push($tableColumns, $column);
	}

	/**
	 * Function will check if column exists and if it exists then will remove it.
	 * @param		array		$tableColumns	reference to an array, which the column will be added
	 * @param		string		$id				id of the column
	 */
	function removeColumn(&$tableColumns, $id) {
		$arrayIndex = $this->getColumn($tableColumns, $id);

		if($arrayIndex !== false) {
			// column exists, remove it
			unset($tableColumns[$arrayIndex]);
		}
	}

	/**
	 * Function which returns the column array key (0, 1, 2, ...) of a column.
	 * @param		array		$tableColumns	array of columns
	 * @param		string		$id				id of the column
	 * @return		integer		$key			key in the $tableColumns array (0, 1, 2, ...)
	 */
	function getColumn($tableColumns, $id) {
		$key = false;

		foreach($tableColumns as $columnkey => $column) {
			if($column["id"] == $id) {
				$key = $columnkey;
			}
		}

		return $key;
	}

	/**
	 * Function which sets property value of a column
	 * @param		array		$tableColumns	array of columns
	 * @param		string		$id				id of the column
	 * @param		string		$property		property of the column
	 * @param		string		$value			value of the property
	 */
	function changeColumnPropertyValue(&$tableColumns, $id, $property, $value) {
		$tableColumnKey = $this->getColumn($tableColumns, $id);
		if($tableColumnKey !== false && $tableColumns[$tableColumnKey][$property] !== $value) {
			$tableColumns[$tableColumnKey][$property] = $value;
		}
	}

	/**
	 * Function which changes visibility of table columns based on data
	 * passed by client
	 * @param		array		$tableColumns	array of columns
	 * @param		array		$action			action data
	 */
	function parseVisibleColumns(&$tableColumns, $action) {
		if(isset($action["columns"]) && isset($action["columns"]["column"])) {
			// Loop through the columns
			foreach($action["columns"]["column"] as $column) {
				if(isset($column["attributes"]) && isset($column["attributes"]["action"])) {
					$tablecolumnkey = $this->getColumn($tableColumns, $column["_content"]);

					// Add or delete the column
					if($tablecolumnkey !== false) {
						switch(strtolower($column["attributes"]["action"])) {
							case "add":
								$tableColumns[$tablecolumnkey]["visible"] = true;

								if(isset($column["attributes"]["order"])) {
									$tableColumns[$tablecolumnkey]["order"] = (int) $column["attributes"]["order"];
								}
								break;
							case "delete":
								$tableColumns[$tablecolumnkey]["visible"] = false;
								break;
						}
					}
				}
			}
		}
	}

	/**
	 * Function which changes visibility of table columns based on
	 * user settings
	 * @param		array			$tableColumns		array of columns
	 * @param		HexString		$entryId			entryid of folder
	 */
	function parseVisibleColumnsFromSettings(&$tableColumns, $entryId) {
		// get settings for particular folder
		$fields = $GLOBALS['settings']->get("folders/entryid_" . $entryId . "/fields", false);

		if(isset($fields) && is_array($fields)) {
			// Loop through the columns
			foreach($fields as $columnKey => $columnData) {
				if(isset($columnData["action"]) && $columnData["action"]) {
					$tableColumnKey = $this->getColumn($tableColumns, $columnKey);

					// Add or delete the column
					if($tableColumnKey !== false) {
						switch(strtolower($columnData["action"])) {
							case "add":
								$tableColumns[$tableColumnKey]["visible"] = true;

								if(isset($columnData["order"])) {
									$tableColumns[$tableColumnKey]["order"] = (int) $columnData["order"];
								}
								break;
							case "delete":
								$tableColumns[$tableColumnKey]["visible"] = false;
								break;
						}
					}
				}
			}
		}
	}
}
?>