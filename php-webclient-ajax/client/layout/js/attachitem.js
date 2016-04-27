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

/**
 * @TODO:	There are some redundant functions which are common in 
 *			advancefind module, Which we can reduce by removing/making
 *			common functions for both modules.
 */ 


/**
 * Function to handle the action to send the selected items.
 * collect the entryids from selected rows and pass them to request
 * with action_type = attach_items to server.
 */
function addAttachmentItems(moduleObject, dialog_attachments)
{
	var selectedRowCount = moduleObject.tableWidget.getNumSelectedRows();
	var entryIds = new Array();
	if(selectedRowCount > 0){
		for (var i=0; i<selectedRowCount; i++){
			var rowId = moduleObject.tableWidget.getSelectedRowID(i);
			entryIds.push(moduleObject.tableWidget.getDataByRowID(rowId).entryid);
		}
		
		var data = new Object();
		data["store"] = moduleObject.hierarchy.selectedFolderStoreId;
		data["parententryid"] = moduleObject.hierarchy.selectedFolder;
		data["entryids"] = new Array();
		
		data["entryids"] = entryIds; 
		data["dialog_attachments"] = dialog_attachments;

		data["type"] = dhtml.getElementById('attachment').checked?"attachment":"text";
		
		webclient.xmlrequest.addData(moduleObject, "attach_items", data);
		webclient.xmlrequest.sendRequest();
	}
}

/**
 * Function will add columns to table widget
 * @param Object moduleObject advanced find module object
 * @param Array properties array of column properties
 */
function attachItemAddColumns(moduleObject, properties) {
	// add columns in table widget
	if(properties.length > 0) {
		var doNotAddProperty = false;

		// first remove previous columns before adding new one
		moduleObject.tableWidget.columns = new Array();

		for(var index = 0; index < properties.length; index++) {
			var property = properties[index];
			doNotAddProperty = false;

			// override default settings
			switch(property["id"]) {
				case "hidden_column":
					// hide column although its visibility is true
					doNotAddProperty = true;
					break;
				case "icon":
				case "icon_index":
				case "importance":
				case "attachicon":
				case "hasattach":
				case "flag":
				case "flag_icon":
				case "flag_status":
				case "complete":
				case "recurring":
					if(property["length"] == false) {
						property["length"] = 25;
					}
					property["name"] = "";		// hide title of column
					break;
				case "alldayevent":
					if(property["length"] == false) {
						property["length"] = 55;
					}
				default:
					// default value for string column width
					if(property["length"] == false) {
						property["length"] = 150;
					} else if(property["length"] == PERCENTAGE) {
						// tablewidget doesn't use PERCENTAGE for variable width columns instead it uses false for it
						property["length"] = false;
					}
					break;
			}

			if(!doNotAddProperty) {
				moduleObject.tableWidget.addColumn(property["id"], property["name"], property["length"], property["order"], property["title"], property["sort"], property["visible"]);
			}
		}
	}
}

/**
 * Returns the flag class for the red/blue/etc flag
 */
function attachItemGetFlagClass(flagStatus, flagIcon) {
	var className = "";

	switch(flagIcon) {
		case 1:
			className = "icon_flag_purple";
			break;
		case 2:
			className = "icon_flag_orange";
			break;
		case 3:
			className = "icon_flag_green";
			break;
		case 4:
			className = "icon_flag_yellow";
			break;
		case 5:
			className = "icon_flag_blue";
			break;
		case 6:
			className = "icon_flag_red";
			break;
		case 0:
			switch(flagStatus)
			{
				case 1:
					className = "icon_flag_complete";
					break;
				default:
					className = "icon_flag_none";
					break;
			}
			break;
		default:
			switch(flagStatus)
			{
				case 1:
					className = "icon_flag_complete";
					break;
				case 2:
					className = "icon_flag_none";
					break;
				default:
					className = "icon_flag";
					break;
			}
			break;
	}

	return className;
}

/**
 * Function generates a string which will be used to show the recipients in list view's attendees coloumn.
 * @param number recipienttype number which contains the recipient type
 * @param object item object xml data of each item
 * @return string result string which contains the recipients name and email
 */
function attachItemCreateRecipientString(recipienttype, item) {
	var result = "";
	var recipients = dhtml.getXMLNode(item, "recipients");

	if(recipients) {
		var recipient = recipients.getElementsByTagName("recipient");

		for (var index = 0; index < recipient.length; index++){
			var type = dhtml.getXMLValue(recipient[index], "type");
			var name = dhtml.getXMLValue(recipient[index], "display_name");
			var email = dhtml.getXMLValue(recipient[index], "email_address");
			var objecttype = dhtml.getXMLValue(recipient[index], "objecttype");

			if(parseInt(type,10) == recipienttype){
				result += (result=="") ? nameAndEmailToString(name, email, objecttype, true) + ";": nameAndEmailToString(name, email, objecttype, true);
			}
		}
	}

	return result;
}

/**
 * Function will render XML data and create object of row column data
 * that is compatible with table widget
 * @param Object moduleObject advanced find module object
 * @param XMLNode itemData XMLNode of an item
 * @param Array properties column data
 */
function attachItemRenderRowColumnData(moduleObject, itemData, properties) {
	// render row data to pass to tablewidget
	var item = new Object();

	for(var propIndex = 0; propIndex < properties.length; propIndex++) {
		var value = NBSP;
		var colId = properties[propIndex]["id"];
		var xmlValue = dhtml.getXMLValue(itemData, colId);

		switch(colId) {
			case "icon":
			case "icon_index":
				var messageFlags = parseInt(dhtml.getXMLValue(itemData, "message_flags", MSGFLAG_READ), 10);
				var readFlag = false;
				if((messageFlags & MSGFLAG_READ) == MSGFLAG_READ) {
					readFlag = true;
				}
				value = "<div class='rowcolumn message_icon " + iconIndexToClassName(xmlValue, dhtml.getXMLValue(itemData, "message_class"), readFlag) + "'>" + NBSP + "</div>";
				break;
			case "attachicon":
			case "hasattach":
				if(parseInt(xmlValue, 10) == 1) {
					value = "<div class='rowcolumn message_icon icon_hasattach'>" + NBSP + "</div>";
				} else {
					value = NBSP;
				}
				break;
			case "flag":
			case "flag_icon":
			case "flag_status":
				value = "<div class='rowcolumn message_icon " + attachItemGetFlagClass(parseInt(dhtml.getXMLValue(itemData, "flag_status"), 10), parseInt(dhtml.getXMLValue(itemData, "flag_icon"), 10)) + "'>" + NBSP + "</div>";
				break;
			case "importance":
				switch(parseInt(xmlValue, 10)) {
					case IMPORTANCE_LOW:
						value = "<div class='rowcolumn message_icon icon_importance_low'>" + NBSP + "</div>";
						break;
					case IMPORTANCE_HIGH:
						value = "<div class='rowcolumn message_icon icon_importance_high'>" + NBSP + "</div>";
						break;
					case IMPORTANCE_NORMAL:
					default:
						value = NBSP;
						break;
				}
				break;
			case "percentage":
			case "percent_complete":
				value = "<div class='rowcolumn message_icon'>" + (value * 100) + "%</div>";
				break;
			case "checkbox":
			case "complete":
				var checked = "";
				if(parseInt(xmlValue, 10) == 1) {
					checked = "checked = 'checked'";
				}
				value = "<input type='checkbox' " + checked + " disabled>";
				break;
			case "parent_entryid":
				if(typeof parentWebclient != "undefined" && parentWebclient.hierarchy) {
					// if table view is loaded in dialogs then use parentWebclient
					var folder = parentWebclient.hierarchy.getFolder(xmlValue);
				} else {
					var folder = webclient.hierarchy.getFolder(xmlValue);
				}

				if(folder && typeof folder["display_name"] == "string") {
					var folderName = folder["display_name"].htmlEntities();
				} else {
					var folderName = NBSP;
				}
				value = folderName;
				break;
			case "alldayevent":
				var checked = "";
				if(parseInt(xmlValue, 10) == 1) {
					checked = "checked = 'checked'";
				}
				value = "<input type='checkbox' " + checked + " disabled>";
				break;
			case "recurring":
				if(parseInt(xmlValue, 10) == 1) {
					value = "<div class='rowcolumn message_icon icon_recurring'>" + NBSP + "</div>";
				} else {
					value = NBSP;
				}
				break;
			case "reminder":
				if(parseInt(xmlValue, 10) == 1) {
					value = "<div class='rowcolumn message_icon icon_reminder'>" + NBSP + "</div>";
				} else {
					value = NBSP;
				}
				break;
			case "meeting":
				// first check that we are really dealing with a meeting and not an appointment
				var meetingStatus = parseInt(xmlValue, 10);
				if(!isNaN(meetingStatus) && meetingStatus !== olNonMeeting) {
					var responseStatus = parseInt(dhtml.getXMLValue(itemData, "responsestatus", olResponseNone), 10);
					switch(responseStatus)
					{
						case olResponseNone:
							value = _("No Response");
							break;
						case olResponseOrganized:
							value = _("Meeting Organizer");
							break;
						case olResponseTentative:
							value = _("Tentative");
							break;
						case olResponseAccepted:
							value = _("Accepted");
							break;
						case olResponseDeclined:
							value = _("Declined");
							break;
						case olResponseNotResponded:
							value = _("Not Yet Responded");
							break;
						default:
							value = NBSP;
					}
				} else {
					value = NBSP;
				}
				break;
			case "busystatus":
				switch(parseInt(xmlValue, 10))
				{
					case fbFree:
						value = _("Free");
						break;
					case fbTentative:
						value = _("Tentative");
						break;
					case fbBusy:
						value = _("Busy");
						break;
					case fbOutOfOffice:
						value = _("Out of Office");
						break;
					default:
						value = NBSP;
				}
				break;
			case "sensitivity":
				switch(parseInt(xmlValue, 10))
				{
					case SENSITIVITY_NONE:
						value = _("Normal");
						break;
					case SENSITIVITY_PERSONAL:
						value = _("Personal");
						break;
					case SENSITIVITY_PRIVATE:
						value = _("Private");
						break;
					case SENSITIVITY_COMPANY_CONFIDENTIAL:
						value = _("Confidential");
						break;
					default:
						value = NBSP;
				}
				break;
			case "label":
				switch(parseInt(xmlValue, 10))
				{
					case 0:
						value = _("None");
						break;
					case 1:
						value = _("Important");
						break;
					case 2:
						value = _("Business");
						break;
					case 3:
						value = _("Personal");
						break;
					case 4:
						value = _("Vacation");
						break;
					case 5:
						value = _("Must Attend");
						break;
					case 6:
						value = _("Travel Required");
						break;
					case 7:
						value = _("Needs Preparation");
						break;
					case 5:
						value = _("Birthday");
						break;
					case 8:
						value = _("Anniversary");
						break;
					case 9:
						value = _("Phone Call");
						break;
					default:
						value = NBSP;
				}
				break;
			case "duration":
				value = simpleDurationString(parseInt(xmlValue, 10));
				break;
			case "required":
				value = attachItemCreateRecipientString(MAPI_TO, itemData);
				break;
			case "optional":
				value = attachItemCreateRecipientString(MAPI_CC, itemData);
				break;
			case "resource":
				value = attachItemCreateRecipientString(MAPI_BCC, itemData);
				break;
			default:
				var xmlNode = dhtml.getXMLNode(itemData, colId);
				if(xmlNode && xmlNode.getAttribute("type") == "timestamp") {
					value = strftime(_("%a %x %X"), xmlValue);
				} else if(xmlNode && xmlNode.getAttribute("type") == "timestamp_date") {
					value = strftime_gmt(_("%a %x %X"), xmlValue);
				} else if(typeof xmlValue == 'string') {
					value = xmlValue.htmlEntities();
				} else {
					value = NBSP;
				}
		}

		/**
		 * hack to show messages as unread
		 * tablewidget doesn't support passing class name to table row
		 */
		var message_flags = parseInt(dhtml.getXMLValue(itemData, "message_flags", -1), 10);
		if((message_flags != -1) && (message_flags & MSGFLAG_READ) != MSGFLAG_READ) {
			if(value != null) {
				value = "<b>" + value + "</b>";
			}
		}

		item[colId] = {innerHTML: value};
	}
	item["entryid"] = dhtml.getXMLValue(itemData, "entryid");
	item["rowID"] = item[moduleObject.uniqueid];		// entryid will be used as uniqueID in table widget

	return item;
}

/**
 * Function will add all data to table widget and generate table
 * @param Object moduleObject advanced find module object
 * @param Array items item XMLNodes to add in table widget
 * @param Array properties column data
 */
function attachItemSetRowColumnData(moduleObject, items, properties) {
	if(items.length == 0) {
		return false;
	}

	// create table data for table widget
	var tableData = new Array();

	// create an object to pass as data object in tablewidget
	for(var itemIndex = 0; itemIndex < items.length; itemIndex++) {
		var itemData = items[itemIndex];

		// add rendered row data to table data
		var item = attachItemRenderRowColumnData(moduleObject, itemData, properties);
		tableData.push(item);
	}

	if(tableData.length > 0) {
		// register row event listeners
		moduleObject.tableWidget.addRowListener(moduleObject.eventAttachItemRowDblClick, "dblclick", moduleObject);

		// hide loading message in table widget
		moduleObject.tableWidget.hideLoader();
		moduleObject.tableWidget.generateTable(tableData);

		return true;
	}
	return false;
}

/**
 * Function which creates container for paging element
 * @param Integer moduleId id of module
 * @param HTMLElement element main element of module
 */
function attachItemCreatePagingElement(moduleId, element) {
	var parentElem = dhtml.getElementById("attach_item_paging", "div", element);

	var pageElement = dhtml.addElement(parentElem, "div", "page", "page_" + moduleId);
	dhtml.addElement(pageElement, "div", false, "pageelement_"+ moduleId);
}


/**
 * Function which creates the paging element
 * @param Object moduleObject object of module
 * @param Number totalrowcount total number of results fetched
 * @param Number rowcount number of results shown currently
 * @return String selected title of currently selected option
 */
function attachItemPagingElement(moduleObject, totalrowcount, rowcount, rowstart) {

	var pagingToolElement = dhtml.getElementById("pageelement_"+ moduleObject.id, "div", moduleObject.element);
	dhtml.deleteAllChildren(pagingToolElement);

	if(moduleObject.pagingTool) {
		var pageElement = dhtml.getElementById("page_"+ moduleObject.id);
		pageElement.style.display = "none";

		moduleObject.pagingTool.destructor();
	}

	// Number of pages
	var pages = Math.floor(totalrowcount / rowcount);
	if((totalrowcount % rowcount) > 0) {
		pages += 1;
	}

	// current page
	var currentPage = Math.floor(rowstart / rowcount);

	// create paging element
	moduleObject.pagingTool = new Pagination("paging", eventListChangePage, module.id);
	
	if(pages > 0) {
		moduleObject.pagingTool.createPagingElement(pagingToolElement, pages, currentPage);
		var pageElement = dhtml.getElementById("page_"+ module.id);
		pageElement.style.display = "block";
	}

	return true;
}

/**
 * Function which removes paging combobox options
 * @param		Object			moduleObject		object of module
 */
function attachItemRemovePagingElement(moduleObject) {
	var pagingToolElement = dhtml.getElementById("pageelement_"+ moduleObject.id, "div", moduleObject.element);
	dhtml.deleteAllChildren(pagingToolElement);

	if(moduleObject.pagingTool) {
		var pageElement = dhtml.getElementById("page_" + moduleObject.id, "div", moduleObject.element);
		pageElement.style.display = "none";

		moduleObject.pagingTool.destructor();
	}
}