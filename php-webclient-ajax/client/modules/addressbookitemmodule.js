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

addressbookitemmodule.prototype = new ItemModule;
addressbookitemmodule.prototype.constructor = addressbookitemmodule;
addressbookitemmodule.superclass = ItemModule.prototype;

function addressbookitemmodule(id)
{
	if(arguments.length > 0) {
		this.init(id);
	}
}

/**
 * Function which sets the data for this module.
 * @param string storeid store id
 */  
addressbookitemmodule.prototype.setData = function(storeid)
{
	if(storeid) {
		this.storeid = storeid;
	}
}

/** 
 * Overrides the default item function to copy values in fields that could not 
 * be set by setProperties after that function has set the values of the 
 * properties coming from the server.
 */
addressbookitemmodule.prototype.item = function(action)
{
	addressbookitemmodule.superclass.item.apply(this, arguments);

	var message = action.getElementsByTagName("item")[0];
	// We parse the object_type since it will be a string when you extract it from the XML
	var object_type = parseInt(dhtml.getXMLValue(message, "object_type" , 0),10);

	webclient.pluginManager.triggerHook("client.module.addressbookitem.item.before", {
		module: this, 
		object_type: object_type,
		message: message
	});

	switch(object_type){
		case MAPI_MAILUSER:
			/*
			 * Some data needs to be set in two fields, since the mapping of XML elements to HTML 
			 * fields through the IDs of the latter can only set one field. We need to call this 
			 * function to make it so.
			 */
			this.setDoubleFields();

			this.setManagerTable(message);
			this.setDirectReportTable(message);
			this.setMemberOfTable(message);
			this.setProxyAddressTable(message);

			this.setDataInComboboxes(message);
			break;
		case MAPI_DISTLIST:
		// Companies should also be considered a group, but are actually an AB container
		case MAPI_ABCONT:
			this.setOwnerTable(message);
			this.setMembersTable(message);
			this.setMemberOfTable(message);
			this.setProxyAddressTable(message);
			break;
	}

	this.setDialogName(message);

	webclient.pluginManager.triggerHook("client.module.addressbookitem.item.after", {
		module: this, 
		object_type: object_type,
		message: message
	});
}

/**
 * Sets the dialog title to the display name of the opened item.
 * @param message XMLNode Data contained in the <item> node
 */
addressbookitemmodule.prototype.setDialogName = function(message)
{
	var display_name = dhtml.getXMLValue(message, "display_name", false);
	setDialogTitle(display_name);
}

/**
 * Sets the fields like Phone in the UI. This field contains the value of 
 * PR_BUSINESS_TELEPHONE_NUMBER just like the Business field on the Phone/Notes 
 * tab. The setProperties function sets the values using the mapping of the 
 * HTMLElement ID to the property names coming from the server. Since you cannot
 * have two elements with the same ID the Phone field has to have a different 
 * ID. In order to set the correct value in that field we copy this value into 
 * the Phone field as well.
 */
addressbookitemmodule.prototype.setDoubleFields = function()
{
	var phoneField = dhtml.getElementById("phone");
	var businessPhoneField = dhtml.getElementById("business_telephone_number");
	if(phoneField && businessPhoneField){
		phoneField.value = businessPhoneField.value;
	}

	var assistantField = dhtml.getElementById("assistant");
	var assistantCopyField = dhtml.getElementById("assistant_copy");
	if(assistantField && assistantCopyField){
		assistantCopyField.value = assistantField.value;
	}
}

/**
 * Extract the data from the server response and set that in the manager table.
 * @param message XMLNode Data contained in the <item> node
 */
addressbookitemmodule.prototype.setManagerTable = function(message){
	var tableWidgetElem = dhtml.addElement(dhtml.getElementById("manager_table"), "div", false, "tableWidgetContainer");
	tableWidgetElem.style.height = "40px";

	this.managertable = new TableWidget(tableWidgetElem, false);
	this.managertable.addRowListener(this.eventOpenABEntryFromTblWidget, "dblclick", this);
	this.setColumnsGABEntryList(this.managertable);

	// Get manager data
	var managerData = Array();
	var managerProps = message.getElementsByTagName("ems_ab_manager")[0];
	if(managerProps){
		managerData.push(this.extractDataFromGABEntry(managerProps));
	}
	// Set manager data
	this.managertable.generateTable(managerData);
	// Fix table widget when it is empty
	this.fixEmptyTableWidget(this.managertable);
}

/**
 * Extract the data from the server response and set that in the owner table.
 * @param message XMLNode Data contained in the <item> node
 */
addressbookitemmodule.prototype.setOwnerTable = function(message){
	var tableWidgetElem = dhtml.addElement(dhtml.getElementById("owner_table"), "div", false, "tableWidgetContainer");
	tableWidgetElem.style.height = "40px";

	this.ownertable = new TableWidget(tableWidgetElem, false);
	this.ownertable.addRowListener(this.eventOpenABEntryFromTblWidget, "dblclick", this);
	this.setColumnsGABEntryList(this.ownertable);

	// Get owner data
	var ownerData = Array();
	var ownerProps = message.getElementsByTagName("ems_ab_owner")[0];
	if(ownerProps){
		ownerData.push(this.extractDataFromGABEntry(ownerProps));
	}
	// Set owner data
	this.ownertable.generateTable(ownerData);
	// Fix table widget when it is empty
	this.fixEmptyTableWidget(this.ownertable);
}

/**
 * Extract the data from the server response and set that in the directreports table.
 * @param message XMLNode Data contained in the <item> node
 */
addressbookitemmodule.prototype.setDirectReportTable = function(message){
	// Create the tableWidget
	var tableWidgetElem = dhtml.addElement(dhtml.getElementById("directreports_table"), "div", false, "tableWidgetContainer");
	tableWidgetElem.style.height = "165px";

	this.reportsTable = new TableWidget(tableWidgetElem, false);
	this.reportsTable.addRowListener(this.eventOpenABEntryFromTblWidget, "dblclick", this);
	this.setColumnsGABEntryList(this.reportsTable);

	// Get report data
	var reportsData = Array();
	/**
	 * The data is structured as followed:
	 * <ems_ab_reports>
	 *   <ems_ab_reports_entry>
	 *      <field>TEXT</field>
	 *   </ems_ab_reports_entry>
	 *   <ems_ab_reports_entry>
	 *      <field>TEXT</field>
	 *   </ems_ab_reports_entry>
	 * </ems_ab_reports>
	 */
	// Get parent node that holds the individual report nodes
	var ems_ab_reportsNode = message.getElementsByTagName("ems_ab_reports")[0];
	if(ems_ab_reportsNode){
		// Get a list of childnodes nodes that hold the individual report node
		ems_ab_reports_entries = ems_ab_reportsNode.getElementsByTagName("ems_ab_reports_entry");
		// Add the data of each individual report node to the list of data
		for(var i=0;i<ems_ab_reports_entries.length;i++){
			reportsData.push(this.extractDataFromGABEntry(ems_ab_reports_entries[i]));
		}
	}
	
	// Set proxy_addresses data
	this.reportsTable.generateTable(reportsData);
	// Fix table widget when it is empty
	this.fixEmptyTableWidget(this.reportsTable);
}

/**
 * Extract the data from the server response and set that in the memberOf table.
 * @param message XMLNode Data contained in the <item> node
 */
addressbookitemmodule.prototype.setMemberOfTable = function(message){
	// Create the tableWidget
	var tableWidgetElem = dhtml.addElement(dhtml.getElementById("memberof_table"), "div", false, "tableWidgetContainer");
	tableWidgetElem.style.height = "225px";

	this.memberOfTable = new TableWidget(tableWidgetElem, false);
	this.memberOfTable.addRowListener(this.eventOpenABEntryFromTblWidget, "dblclick", this);
	this.setColumnsGABEntryList(this.memberOfTable);

	// Get report data
	var memberOfData = Array();
	/**
	 * The data is structured as followed:
	 * <ems_ab_is_member_of_dl>
	 *   <ems_ab_is_member_of_dl_entry>
	 *      <field>TEXT</field>
	 *   </ems_ab_is_member_of_dl_entry>
	 *   <ems_ab_is_member_of_dl_entry>
	 *      <field>TEXT</field>
	 *   </ems_ab_is_member_of_dl_entry>
	 * </ems_ab_is_member_of_dl>
	 */
	// Get parent node that holds the individual report nodes
	var ems_ab_member_of_dlNode = message.getElementsByTagName("ems_ab_is_member_of_dl")[0];
	if(ems_ab_member_of_dlNode){
		// Get a list of childnodes nodes that hold the individual report node
		ems_ab_member_of_dl_entries = ems_ab_member_of_dlNode.getElementsByTagName("ems_ab_is_member_of_dl_entry");
		// Add the data of each individual report node to the list of data
		for(var i=0;i<ems_ab_member_of_dl_entries.length;i++){
			memberOfData.push(this.extractDataFromGABEntry(ems_ab_member_of_dl_entries[i]));
		}
	}
	
	// Set proxy_addresses data
	this.memberOfTable.generateTable(memberOfData);
	// Fix table widget when it is empty
	this.fixEmptyTableWidget(this.memberOfTable);
}

/**
 * Extract the data from the server response and set that in the members table.
 * @param message XMLNode Data contained in the <item> node
 */
addressbookitemmodule.prototype.setMembersTable = function(message){
	// Create the tableWidget
	var tableWidgetElem = dhtml.addElement(dhtml.getElementById("members_table"), "div", false, "tableWidgetContainer");
	tableWidgetElem.style.height = "225px";

	this.membersTable = new TableWidget(tableWidgetElem, false);
	this.membersTable.addRowListener(this.eventOpenABEntryFromTblWidget, "dblclick", this);
	this.setColumnsGABEntryList(this.membersTable);

	// Get report data
	var membersData = Array();
	/**
	 * The data is structured as followed:
	 * <members>
	 *   <member>
	 *      <field>TEXT</field>
	 *   </member>
	 *   <member>
	 *      <field>TEXT</field>
	 *   </member>
	 * </members>
	 */
	// Get parent node that holds the individual report nodes
	var membersNode = message.getElementsByTagName("members")[0];
	if(membersNode){
		// Get a list of childnodes nodes that hold the individual report node
		memberEntries = membersNode.getElementsByTagName("member");
		// Add the data of each individual report node to the list of data
		for(var i=0;i<memberEntries.length;i++){
			membersData.push(this.extractDataFromGABEntry(memberEntries[i]));
		}
	}
	
	// Set proxy_addresses data
	this.membersTable.generateTable(membersData);
	// Fix table widget when it is empty
	this.fixEmptyTableWidget(this.membersTable);
}

/**
 * Extract the data from the server response and set that in the proxy_addresses table.
 * @param message XMLNode Data contained in the <item> node
 */
addressbookitemmodule.prototype.setProxyAddressTable = function(message){
	// Create the tableWidget
	var tableWidgetElem = dhtml.addElement(dhtml.getElementById("proxy_addresses_table"), "div", false, "tableWidgetContainer");
	tableWidgetElem.style.height = "250px";

	this.proxyAddressesTable = new TableWidget(tableWidgetElem, false);
	this.proxyAddressesTable.addRowListener(this.eventOpenABEntryFromTblWidget, "dblclick", this);
	this.proxyAddressesTable.addColumn("emailaddress", _("E-mail Addresses"), false, 1);

	// Get proxy_addresses data
	var addressesData = Array();
	/**
	 * The data is structured as followed:
	 * <ems_ab_proxy_addresses>
	 *   <ems_ab_proxy_address>ADDRESS1</ems_ab_proxy_address>
	 *   <ems_ab_proxy_address>ADDRESS2</ems_ab_proxy_address>
	 * </ems_ab_proxy_addresses>
	 */
	// Get parent node that holds the individual address nodes
	var ems_ab_proxy_addressesNode = message.getElementsByTagName("ems_ab_proxy_addresses")[0];
	if(ems_ab_proxy_addressesNode){
		// Get a list of childnodes nodes that hold the individual address node
		ems_ab_proxy_addresses = ems_ab_proxy_addressesNode.getElementsByTagName("ems_ab_proxy_address");
		// Add each individual address nodes text node value to the list of data
		for(var i=0;i<ems_ab_proxy_addresses.length;i++){
			addressesData.push({
				emailaddress: {
					innerHTML: dhtml.getTextNode(ems_ab_proxy_addresses[i], "")
				}
			});
		}
	}
	
	// Set proxy_addresses data
	this.proxyAddressesTable.generateTable(addressesData);
	// Fix table widget when it is empty
	this.fixEmptyTableWidget(this.proxyAddressesTable);
}

/**
 * Setting the columns for a list of GAB entries. The columns shown in all the 
 * tables are the same.
 * @param tableWidget Object TableWidget reference
 */
addressbookitemmodule.prototype.setColumnsGABEntryList = function(tableWidget){
	tableWidget.addColumn("display_type", '', 22, 1);
	tableWidget.addColumn("display_name", _("Display Name"), false, 2);
	tableWidget.addColumn("smtp_address", _("Address"), 180, 3);
	tableWidget.addColumn("account", _("Account"), 180, 4);
}

/**
 * Extracts the values from a GAB entry and puts them in an object that can be 
 * used by the table widgets. The columns shown in all the tables are the same.
 * @param entry XMLElement Element containing the properties for one entry.
 * @return Object Data in an tablewidget usable object.
 */
addressbookitemmodule.prototype.extractDataFromGABEntry = function(entry){
	var display_type = dhtml.getXMLValue(entry, "display_type");
	var data = {
		display_type: {
			css: 'message_icon ' + displayTypeToClassName(display_type)
		},
		display_name: {
			innerHTML: dhtml.getXMLValue(entry, "display_name")
		},
		smtp_address: {
			innerHTML: dhtml.getXMLValue(entry, "smtp_address")
		},
		account: {
			innerHTML: dhtml.getXMLValue(entry, "account")
		},
		entryid: dhtml.getXMLValue(entry, "entryid"),
		object_type: dhtml.getXMLValue(entry, "object_type")
	};
	return data;
}

/**
 * Extract the list-data from the server response and set that in the respective combo boxes. This 
 * is the case for the home2_telephone_number and business2_telephone_number property.
 * @param message XMLNode Data contained in the <item> node
 */
addressbookitemmodule.prototype.setDataInComboboxes = function(message)
{
	// Get the SELECT HTMLElements
	var homeNumField = dhtml.getElementById("home2_telephone_number");
	var businessNumField = dhtml.getElementById("business2_telephone_number");

	/*
	 * Get the nodes in the XML that hold the list of telephone numbers and the 
	 * node that holds the selection. The structure of this XML is as follows:
	 * 
	 * <home2_telephone_number>456</home2_telephone_number>
	 * <home2_telephone_numbers>
	 *   <home2_telephone_numbers_entry>123</home2_telephone_numbers_entry>
	 *   <home2_telephone_numbers_entry>456</home2_telephone_numbers_entry>
	 * </home2_telephone_numbers>
	 * 
	 * The first line holds the selectio and the lines after that the list. This
	 * is the same for the home2 as the business2 telephone numbers.
	 */
	// Get the selection nodes
	var selectionHomeNumNode = message.getElementsByTagName("home2_telephone_number")[0];
	var selectionBusinessNumNode = message.getElementsByTagName("business2_telephone_number")[0];
	// Get the lists of telephone number entries 
	var homeNumbersNode = message.getElementsByTagName("home2_telephone_numbers_entry");
	var businessNumbersNode = message.getElementsByTagName("business2_telephone_numbers_entry");

	// Adding the home2_telephone_number options to the combobox
	if(homeNumbersNode.length > 0){
		for(var i=0;i<homeNumbersNode.length;i++){
			// Extract the value that needs to be displayed
			var optionValue = dhtml.getTextNode(homeNumbersNode[i], "");
			// Compare this option's value with the selection text to see if it needs to be selected
			var selected = (optionValue == dhtml.getTextNode(selectionHomeNumNode, null))?true:false;
			// Create and add the option to the selection field
			var option = new Option(optionValue, optionValue, selected);
			homeNumField.add(option,null);
		}
	}

	// Adding the business2_telephone_number options to the combobox
	if(businessNumbersNode.length > 0){
		for(var i=0;i<businessNumbersNode.length;i++){
			// Extract the value that needs to be displayed
			var optionValue = dhtml.getTextNode(businessNumbersNode[i], "");
			// Compare this option's value with the selection text to see if it needs to be selected
			var selected = (optionValue == dhtml.getTextNode(selectionBusinessNumNode, null))?true:false;
			// Create and add the option to the selection field
			var option = new Option(optionValue, optionValue, selected);
			businessNumField.add(option,null);
		}
	}
}

/**
 * This function fixes the empty table widget issue. When no data is in the table widget the header 
 * grows bigger than it should be. By adding an empty row that when clicked clears that selection 
 * the header keeps the normal height. This is a dirty fix for this issue until that is fixed.
 * @param tblWidget Object Reference to the table widget
 */
addressbookitemmodule.prototype.fixEmptyTableWidget = function(tblWidget){
	if(tblWidget.getRowCount() == 0){
		// Clearing the selection when clicked
		tblWidget.addRowListener(function(){this.clearSelection();},"all");
		// Adding an empty row
		tblWidget.addRow({display_name: "", object_type: 0});
	}
}

/**
 * Event function fired by the table widget. This function is commonly used for the double click 
 * event on a row. This function will open the correct type of addressbookitem details dialog based
 * on the object type of the row.
 * @param tblWidget Object Reference to the table widget
 * @param type Object Event type
 * @param rowId Number Row ID
 * @param event Object Event object
 */
addressbookitemmodule.prototype.eventOpenABEntryFromTblWidget = function(tblWidget, type, rowId, event){
	var data = tblWidget.getDataByRowID(rowId);
	switch(parseInt(data.object_type,10)){
		case MAPI_MAILUSER:
			var uri = DIALOG_URL+"task=gab_detail_mailuser_standard&storeid=" + this.storeid + "&entryid=" + data.entryid;
			webclient.openWindow(this, "gab_detail", uri, FIXEDSETTINGS.ABITEM_DIALOG_MAILUSER_WIDTH, FIXEDSETTINGS.ABITEM_DIALOG_MAILUSER_HEIGHT);
			break;
		case MAPI_DISTLIST:
		case MAPI_ABCONT:
			var uri = DIALOG_URL+"task=gab_detail_distlist_standard&storeid=" + this.storeid + "&entryid=" + data.entryid;
			webclient.openWindow(this, "gab_detail", uri, FIXEDSETTINGS.ABITEM_DIALOG_DISTLIST_WIDTH, FIXEDSETTINGS.ABITEM_DIALOG_DISTLIST_WIDTH);
			break;
	}
}
