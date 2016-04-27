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
 * --Table View--
 * @type	View
 * @classDescription	This view can be used for any
 * list module to display the items
 * 
 * NOTE THIS: for the default functions (sepcified in view.js) look at the view.js
 *            for argument description.
 * 
 * +---+---------+---------+------+
 * | ! | From    | Subject | Size |
 * +---+---------+---------+------+
 * |   | Frans   | test    | 7kb  |
 * +---+---------+---------+------+
 * |   | Michael | test2   | 7kb  |
 * +---+---------+---------+------+
 * 
 * DEPENDS ON:
 * |------> view.js
 * |----+-> *listmodule.js
 * |    |----> listmodule.js
 */

TableView.prototype = new View;
TableView.prototype.constructor = TableView;
TableView.superclass = View.prototype;

/* @param moduleID is the parent module
 * @param element is the element where the view should be placed
 * @param events are the events that should be handled for this view
 * @param data are any view-specific data
 * @param uniqueid (optional) is the column id unique id, which must be present in all data sent to addItem() and which will be used 
 *        when triggering events, etc.
 */
 
function TableView(moduleID, element, events, data, uniqueid)
{
	if(arguments.length > 0) {
		this.init(moduleID, element, events, data, uniqueid);
	}
}

TableView.prototype.init = function(moduleID, element, events, data, uniqueid)
{
	this.element = element;
	dhtml.addClassName(this.element, "tableview");
	
	this.moduleID = moduleID;
	this.events = events;
	this.data = data;
	
	this.columns = false;
	this.sortColumn = false;
	this.sortDirection = false;
	
	if(uniqueid)
		this.uniqueid = uniqueid;
	else
		this.uniqueid = "entryid";
		
	this.cursorMessage = false;
	
	this.initView();

	// quick hack for supporting next/prev page
	this.hackPaging = new Object();
}

/**
 * Function which intializes the view
 */ 
TableView.prototype.initView = function()
{
	this.columnsElement = dhtml.addElement(false, "div");

	this.inputcolumnsElement = dhtml.addElement(false, "div");

	this.divElement = dhtml.addElement(false, "div",false,"divelement");
	this.divElement.style.width = "100%";
	this.divElement.style.height = "150px";
	this.divElement.style.overflowX = "hidden";
	this.divElement.style.overflowY = "scroll";

	this.setEventsOnLayoutElement();
	
	// add keyboard event
	var module = webclient.getModule(this.moduleID);
	webclient.inputmanager.addObject(module, module.element);
	webclient.inputmanager.bindEvent(module, "keydown", eventTableViewKeyboard);
	if(typeof eventAddressCardsViewKeyboard != "undefined") {
		webclient.inputmanager.unbindEvent(module, "keydown", eventAddressCardsViewKeyboard);
	}
	if(typeof eventIconViewKeyboard != "undefined") {
		webclient.inputmanager.unbindEvent(module, "keydown", eventIconViewKeyboard);
	}
	webclient.inputmanager.bindEvent(module, "mousedown", eventTableViewMouseDown);
	webclient.inputmanager.bindKeyControlEvent(module, module.keys["select"], "keyup", eventTableViewKeyCtrlSelectAll, true);
	webclient.inputmanager.bindKeyControlEvent(module, module.keys["edit_item"], "keyup", eventListKeyCtrlEdit, true);
	webclient.inputmanager.bindKeyControlEvent(module, module.keys["quick_edit"], "keyup", eventTableViewKeyCtrlQuickEdit, true);
	
	if(typeof dragdrop != "undefined") dragdrop.addTarget(dhtml.getElementById("hierarchy"), this.element, "folder");

	this.cursorElementL = dhtml.addElement(false, "div", "cursor_left", "cursor_left");
	this.cursorElementT = dhtml.addElement(false, "div", "cursor_top", "cursor_top");
	this.cursorElementB = dhtml.addElement(false, "div", "cursor_bottom", "cursor_bottom");
	this.cursorElementR = dhtml.addElement(false, "div", "cursor_right", "cursor_right");
}

/**
 * Handle up/down events on the DIV, as IE will try to scroll the div itself. We don't want
 * this because we handle scrolling ourselves. FF doesn't seem to do this...
 */
TableView.prototype.onDivKeyDown = function(module, element, event) 
{
	moduleObject = webclient.getModule(module.moduleID);
	
	if (!moduleObject.editMessage){
		event.preventDefault();
		return false;
	}
}

/**
 * Function which resizes the view.
 */  
TableView.prototype.resizeView = function()
{
	var elementHeight = this.element.offsetHeight;

	//As an quick-add row is added in task module, height of listview must be set accordingly...
	var height = elementHeight - this.columnsElement.offsetHeight - this.inputcolumnsElement.offsetHeight;
		
	if(height < 3){
		height = 3;
	}

	var main_top = dhtml.getElementById("main_0");
	if (main_top){ 
			this.divElement.style.height = main_top.clientHeight - this.divElement.offsetTop + "px";
	} else {
			this.divElement.style.height = height + "px";
	}
	//update dragdrop targets after view has loaded, as datepicker will change position of hierarchy list.
	// and which cause the isseu of dragging and dropping of elements in wrong target.
	if(typeof(dragdrop) != "undefined") {
		dragdrop.updateTargets("folder");
	}
}

/**
 * Function which adds items to the view.
 * @param object items the items
 * @param array properties property list
 * @param object action the action tag
 * @return array list of entryids  
 */ 
TableView.prototype.execute = function(items, properties, action, groupID, inputProperties)
{
	// Get sort information
	var sort = action.getElementsByTagName("sort")[0];
	var sortColumn = false;
	var sortDirection = false;
	if(sort && sort.firstChild) {
		sortColumn = sort.firstChild.nodeValue;
		sortDirection = sort.getAttribute("direction");
	}

	this.addColumns(properties, sortColumn, sortDirection);
	if(inputProperties.length > 0){
		this.addInsertColumns(inputProperties, sortColumn, sortDirection);
	}
	var entryids = this.addRows(items);

	this.element.appendChild(this.columnsElement);

	if(inputProperties.length > 0) 
    	this.element.appendChild(this.inputcolumnsElement);

	this.element.appendChild(this.divElement);
	this.divElement.style.position = "relative";

	this.divElement.appendChild(this.cursorElementL);
	this.divElement.appendChild(this.cursorElementR);
	this.divElement.appendChild(this.cursorElementT);
	this.divElement.appendChild(this.cursorElementB);

	this.resizeView();
	
	
	//setup datepicker
	for(var i = 0; i <inputProperties.length; i++)
	{
		//set datepicker for column type = 'datepicker'
		if (inputProperties[i]["type"] == "datepicker"){			
			setcal("insertprops_module"+ this.moduleID+"_"+inputProperties[i]["id"],"cal_insertprops_module"+ this.moduleID+"_"+inputProperties[i]["id"]);
		}
	}

	// When layout is recreated registered events are lost, so set events again on layoutElement(i.e this.divElement)
	this.setEventsOnLayoutElement();

	return entryids;
}

/**
 * Function which adds a new item to the view.
 */ 
TableView.prototype.addItem = function(item, properties, action)
{
	return this.addRow(item);
}

TableView.prototype.deleteItems = function(items)
{
	return false;
}

/**
 * Function which updates an item.
 */ 
TableView.prototype.updateItem = function(element, item, properties)
{
	var entry = Object();
	
	// Get message flag (unread)
	var message_flags = parseInt(dhtml.getXMLValue(item,"message_flags", MSGFLAG_READ),10); // default to 'read'
	if((message_flags & MSGFLAG_READ) == MSGFLAG_READ) {
		if(element.className.indexOf("message_unread") > 0) { 
			var classNames = element.className.split(" "); 
			var className = "";
			for(var i=0; i<classNames.length; i++) { 
				if(classNames[i] != "message_unread") { 
					if(className.length>0)
						className += " ";
					className += classNames[i]; 
				} 
			}
			element.className = className;
		} 
	}else{
		element.className = element.className.substring(0, element.className.indexOf(" ")) + " message_unread " + element.className.substring(element.className.indexOf(" ") + 1);
	}

	element.setAttribute("messageflags", message_flags);

	for(var i = 0; i < properties.length; i++)
	{
		var property = properties[i];
		var itemProperty = item.getElementsByTagName(property["id"])[0];

		var value = false;
		
		if(itemProperty) {
			if(element.cells && element.cells[i]) {
				if(itemProperty.firstChild) {
					value = itemProperty.firstChild.nodeValue;
				}
			}
		}
		
		this.setRowColumn(element.cells[i], property["id"], property["type"], property["length"], value, item);
	}

	entry["id"] = element.id;
	entry[this.uniqueid] = item.getElementsByTagName(this.uniqueid)[0].firstChild.nodeValue;

	return entry;
}

TableView.prototype.addColumns = function(columns, sortColumn, sortDirection)
{
	// Sort columns
	columns.sort(this.sortColumns);

	var table = new Array();
	table.push("<div id='columnbackground'>");
	table.push("<table class='table' border='0' cellpadding='0' cellspacing='0'><tr class='columns'>");
	this.columns = columns;
	this.sortColumn = sortColumn;
	this.sortDirection = sortDirection;
	this.percentageColumn = false;

	for(var i = 0; i < columns.length; i++)
	{
		if(columns[i]["length"] == PERCENTAGE) {
			this.percentageColumn = true;
		}
		
		this.renderColumn(table, columns[i]["name"], columns[i]["title"], columns[i]["id"], columns[i]["length"], sortColumn);
	}
	
	if(!this.percentageColumn) {
		table.push("<td class='column' style='cursor:default;'><span class='column_seperator'>&nbsp;</span>&nbsp;</td>");
	}

	table.push("<td class='column' width='16' style='cursor:default;'>&nbsp;</td></tr>");
	table.push("</table></div>");

	this.columnsElement.innerHTML = table.join("");

	if(this.events["column"]) {
		var tableElement = this.columnsElement.getElementsByTagName("table")[0];
		if(tableElement) {
			var columnRow = tableElement.rows[0];
			
			if(columnRow) {
				for(var i = 0; i < columnRow.cells.length - 1; i++)
				{
					dhtml.setEvents(this.moduleID, columnRow.cells[i], this.events["column"]);
				}
			}
		}
	}
}

TableView.prototype.addInsertColumns = function(columns, sortColumn, sortDirection)
{
	// Sort columns
	columns.sort(this.sortColumns);
	
	var table = new Array();
	table.push("<div id='insertcolumn'><form id='insertmessage' class='insertform'>");
	table.push("<table width='100%' border='0' cellpadding='0' cellspacing='0'><tr id='insertrow' class=' insertcolumn'>");

    this.percentageColumn = false;
	for(var i = 0; i < columns.length; i++)
	{
		if(columns[i]["length"] == PERCENTAGE) {
			this.percentageColumn = true;
		}		
		this.renderInputColumn(false, this.moduleID, table, columns[i]["type"], columns[i]["id"], columns[i]["title"], columns[i]["length"], columns[i]["name"], columns[i]["readonly"]);
	}
	
	if(!this.percentageColumn) {
		table.push("<td class='column' style='cursor:default;'><span class='column_seperator'>&nbsp;</span>&nbsp;</td>");
	}
	
	table.push("<td class='column' width='16' style='cursor:default;'><span id='img_save' class='icon icon_save icon_norepeat '>&nbsp;</span></td></tr>");
	table.push("</table></form></div>");
	this.inputcolumnsElement.innerHTML = table.join("");
	
	var tableElement = this.inputcolumnsElement.getElementsByTagName("table")[0];
	if(tableElement) {
		var columnRow = tableElement.rows[0];
		if(columnRow) {
			for(var i = 0; i < columnRow.cells.length - 1 && i < columns.length; i++)
			{
				if (this.events["insertcolumn"][columns[i]["name"]])
				dhtml.setEvents(this.moduleID, columnRow.cells[i], this.events["insertcolumn"][columns[i]["name"]]);
			}
		}
	}

	//get an object of module
	var module = webclient.getModule(this.moduleID);
	if(this.events["insertrow"]){
		//set events for input field elements...
		var fieldElement = this.inputcolumnsElement.getElementsByTagName("input");
		if(fieldElement) {
			for(var i = 0; i < fieldElement.length; i++){
				dhtml.setEvents(this.moduleID, fieldElement[i], this.events["insertrow"]);						

				//Set event
				if (fieldElement[i].id.indexOf("categories") > 0){
					dhtml.addEvent(this.moduleID, fieldElement[i], "change", eventFilterCategories);
				}
			}
		}
	}

	//set events for image element... i.e save img
	var spanElement = this.inputcolumnsElement.getElementsByTagName("span");
	var lastspan = spanElement.length-1;
	if(spanElement) {
		dhtml.addEvent(this.moduleID,spanElement[lastspan], "click", eventClickSaveMessage);
	}
}

/**
* Render the input columns for inserting new task
* @param string 	renderOnlyinput	-true if rendering only input type fields, false if rendering whole table column
* @param integer	moduleID		-id of module
* @param element	table			-table or column element for which to render input fields
* @param string 	type	 		-type of column
* @param string		fieldid			-id of input type fields
* @param string		title			-value of input type fields
* @param string		length			-width of column
* @param string 	name			-id of column to be rendered.
* @param string 	readonly		-value for readonly attribute of input type field.
*/
TableView.prototype.renderInputColumn = function (renderOnlyinput, moduleID, table, type, fieldid, title, length, name, readonly)
{		
	var className = "";
	var width = "";
	var elementValue = "&nbsp;";
	var sort = "";
	var value ="";
	var date ="";
	var property = "insertprops";
	var tooltip ="";
	var unixtimestamp = "";
	
	if (title && title !="&nbsp"){
		value=title.htmlEntities();
	}
	
	if (renderOnlyinput) {
		property = "editprops";
		
		if (value != "" && (name == "duedate" || name=="startdate")) {
			moduleObject = webclient.getModule(moduleID);
			rowID = table.parentNode.id;
			value = strftime("%x", moduleObject.itemProps[moduleObject.entryids[rowID]][name]);
		}
	}
	
	var moduleid = "module"+ moduleID;
	var columnid = moduleid +"_"+ name;
	var statusfieldid = property +"_"+ moduleid +"_status";
	fieldid = property +"_"+ moduleid +"_"+ fieldid;
	
	switch(type)
	{
		case "null":
			elementValue = "";
			break;
			
		case "importance":
			//add button for importance if rendering input fields for quick-add row...
			if (!renderOnlyinput){
				className +=" icon_norepeat icon_taskarrow message_icon";
			}
			tooltip = "Normal";
			break;
			
		case "checkbox":
			value = (value != 0) ? "checked" : " ";
			elementValue +="<input id='"+ fieldid +"' type='checkbox' class='status' "+ value +">";
			break;
			
		case "textbox":
			elementValue += "<input id='"+ fieldid +"' class='editfields' type='text' value='"+ value +"' "+ readonly +">";
			break;
			
		case "percent":
			//value = value*100 +"%";
			elementValue += "<input id='"+ fieldid +"' class='editfields' type='text' value='"+ value +"' "+ readonly +">";
			elementValue += "<span class='percentdiv'><div class='spinner_up' onclick='completeSpinnerUp(\""+ fieldid +"\",\""+ statusfieldid +"\");'>&nbsp;</div><div class='spinner_down' onclick='completeSpinnerDown(\""+ fieldid +"\",\""+ statusfieldid +"\");'>&nbsp;</div></span>";
			break;

		case "datepicker":	
			elementValue += "<input id='"+ fieldid +"'  class='editfields'  type='text' value='"+ value +"' "+ readonly +">";
			elementValue += "<span id='cal_"+ fieldid +"' class='icon icon_cal icon_norepeat '>&nbsp;</span>";
			elementValue +="<input id='"+ property +"_"+ moduleid +"_"+ name +"' type='hidden' value=''>";
			break;
			
		case "categories":
			elementValue += "<input id='"+ fieldid +"'  class='editfields'  type='text' value='"+ value +"' "+ readonly +">";
			elementValue += "<span id='catg_"+ fieldid +"' class='icon icon_cal icon_norepeat' onclick='eventcategoriesToWindow(\""+ fieldid +"\","+ moduleID +")' >&nbsp;</span>";
			break;
		
		case "hidden":
			className = "hidden_column";
			elementValue += this.getHiddenElements(property, moduleID);
		    break;
		    
		default:
			if(typeof(name) == "string")
				elementValue = name;
			else
				elementValue = "&nbsp;";

			if(this.sortColumn == type){
				sort = "&nbsp;<span class='sort_" + this.sortDirection + "'>&nbsp;</span>";
				id += "_sort_" + this.sortDirection;
			}

			// Default string column width is 150 px
			width = "width='150'";
			break;
	}

	if(length) {
		if(parseInt(length,10)) {
			width = "width='" + length + "'";
		} else {
			width = "";
		}
	}
	
	if (renderOnlyinput && elementValue != "&nbsp;") {
		var divElement = dhtml.addElement(false,"div");
		divElement.innerHTML = elementValue;
		dhtml.deleteAllChildren(table);
		table.appendChild(divElement);
	} else if (!renderOnlyinput) {
		table.push("<td id='"+ columnid +"' title='"+ tooltip +"'class='" + className + "'" + width + ">" + elementValue +"</td>");
	}
	
	
}

//Function which returns hidden elements for particular module...
TableView.prototype.getHiddenElements = function (property, moduleid)
{
	var moduleObject = webclient.getModule(moduleid);
	var elementValue = "";

	switch(moduleObject.getModuleName())
	{
		case "tasklistmodule":
			if (property == "insertprops") {
				elementValue  ="<input id='"+ property +"_module"+ moduleid +"_message_class' type='hidden' value='IPM.Task'>";
				elementValue +="<input id='"+ property +"_module"+ moduleid +"_icon_index' type='hidden' value='1280'>";
			}
			elementValue +="<input id='"+ property +"_module"+ moduleid +"_percent_complete' type='hidden' value='0'>";
			elementValue +="<input id='"+ property +"_module"+ moduleid +"_importance' type='hidden' value='1'>";
			break;
		
		default:
			elementValue = "&nbsp;";
			break;
	}
	return elementValue;
}


TableView.prototype.addRows = function(rows)
{
	var entryids = new Object();
	
	var table = new Array();
	table.push("<table id='items' class='table' border='0' cellpadding='0' cellspacing='0'>");

	for(var i = 0; i < rows.length; i++)
	{
		var row = rows[i];
		
		if(row.childNodes.length > 0) {
			var entryid = row.getElementsByTagName(this.uniqueid)[0];
			var message_class = row.getElementsByTagName("message_class")[0];

			if(entryid && entryid.firstChild) {
				entryids[i] = entryid.firstChild.nodeValue;
				
				// Get message class (double click => open window)
				var messageClass = "";
				if(message_class && message_class.firstChild) {
					messageClass = message_class.firstChild.nodeValue.replace(/\./g, "_").toLowerCase();
					
					switch(messageClass) {
						case "ipm_note":
						case "ipm_post":
						case "report_ipm_note_ndr":
						case "ipm_schedule_meeting_request":
						case "ipm_schedule_meeting_resp_pos":
						case "ipm_schedule_meeting_resp_tent":
						case "ipm_schedule_meeting_resp_neg":
						case "ipm_schedule_meeting_canceled":
						case "report_ipm_note_ipnnrn":
						case "report_ipm_note_ipnrn":
							messageClass = "ipm_readmail read_unread";
							
							var messageUnsent = row.getElementsByTagName("message_unsent")[0];
							if(messageUnsent && messageUnsent.firstChild) {
								messageClass = "ipm_createmail read_unread";
							}
							break;
						case "ipm_taskrequest":
						case "ipm_taskrequest_accept":
						case "ipm_taskrequest_decline":
						case "ipm_taskrequest_update":
							messageClass = "ipm_task read_unread";
							break;
					}
				}

				// Get message flag (unread)
				var messageUnread = "message_unread";
				var message_flags = parseInt(dhtml.getXMLValue(row,"message_flags", -1),10);		//returns message_flags value

				if(message_flags == -1 || (message_flags & MSGFLAG_READ) == MSGFLAG_READ) {
					messageUnread = "";
				}
				
				if(message_flags == -1) {
					messageflaghtml = "";
				} else {
					messageflaghtml = 'messageflags="' + message_flags + '"';
				}
				
				// Create row
				table.push("<tr id='" + i + "' class='row " + messageUnread + " " + messageClass + "' " + messageflaghtml + ">");
				
				// Create cells
				for(var j = 0; j < this.columns.length; j++)
				{
					var column = this.columns[j];
					var property = row.getElementsByTagName(column["id"])[0];
				
					var value = "&nbsp;";
					if(property && property.firstChild) {
						value = property.firstChild.nodeValue;
					}
					// Hide attachment icon if message has only inline attachments
					if (column["id"] == "hasattach"){
						var hideattachments = row.getElementsByTagName("hideattachments")[0];
						if (hideattachments && hideattachments.firstChild) {
							hideattachments = hideattachments.firstChild.nodeValue;
							
							if (value) {
								if (hideattachments){
									value = 0;
								}
							}
						}
					}
					this.renderRowColumn(table, column["id"], column["type"], column["length"], value, row,i);
				}
				
				if(!this.percentageColumn) {
					table.push("<td class='rowcolumn'>&nbsp;</td>");
				}

				table.push("</tr>");
			}
		}
	}

	table.push("</table>");	
	this.divElement.innerHTML = table.join("");

	// Set Events.
	var tableElement = this.divElement.getElementsByTagName("table")[0];
	
	// Resize table width. Only in IE.
	if(window.BROWSER_IE && !window.BROWSER_IE8) {
		tableElement.style.width = this.divElement.clientWidth + "px";
	}
	
	for(var i = 0; i < tableElement.rows.length; i++)
	{
		if(typeof(dragdrop) != "undefined") {
			dragdrop.addDraggable(tableElement.rows[i], "folder",null,null,this.moduleID);
		}
		
		// Here we are getting private property from "disabled_item" from rows(XML items) and dont add events for private items.
		if(this.events["row"] && dhtml.getXMLValue(rows[i], "disabled_item", "0") != "1") {
			dhtml.setEvents(this.moduleID, tableElement.rows[i], this.events["row"]);
		}

		if(this.events["rowcolumn"]) {
			for(var j = 0; j < this.columns.length; j++)
			{
				var column = this.columns[j];
				if(this.events["rowcolumn"][column["id"]] && tableElement.rows[i].cells[j]) {
					dhtml.setEvents(this.moduleID, tableElement.rows[i].cells[j], this.events["rowcolumn"][column["id"]]);
				}
			}
		}
	}

	return entryids;
}

TableView.prototype.renderColumn = function(table, name, title, type, length)
{
	var id = type;
	var className = "column";
	var width = "";
	var elementValue = "&nbsp;";
	var sort = "";
	
	switch(type)
	{
		case "hasattach":
		case "icon_index":
		case "display_type":
		case "flag_status":
		case "flag_icon":
		case "importance":
		case "complete":
		case "recurring":
			className += " message_icon icon_" + type;

			if(this.sortColumn == type) {
				id += "_sort_" + this.sortDirection;
			}
			break;

		//set column class of columns which display icons and type at column title bar 
		case "alldayevent":
		case "reminder":
			className += " message_title ";	
			elementValue = name;
			break;		
		
		case "hidden_column":
			className = "hidden_column";
			break;

		default:
			if(typeof(name) == "string")
				elementValue = name;
			else
				elementValue = "&nbsp;";

			if(this.sortColumn == type) {
				sort = "&nbsp;<span class='sort_" + this.sortDirection + "'>&nbsp;</span>";
				id += "_sort_" + this.sortDirection;
			}
			
			// Default string column width is 150 px
			width = "width='150'";
			break;
	}
	
	if(length) {
		if(parseInt(length,10)) {
			width = "width='" + length + "'";
		} else {
			width = "";
		}
	}

	table.push("<td id='property_" + id + "' class='" + className + "' title='" + title + "' " + width + "><span class='column_seperator'>&nbsp;</span>" + elementValue + sort + "</td>");
}

TableView.prototype.renderRowColumn = function(table, fieldname, type, length, value, item,rowNumber)
{
	var settings = this.getRowColumnSettings(fieldname, type, length, value, item);
	
	// Check for specific actions to be executed.
	var messageClass = item.getElementsByTagName("message_class")[0];
	if(messageClass && messageClass.firstChild) {
		var messageSettings = this.getMessageSettings(messageClass.firstChild.nodeValue, value, item);
		if(messageSettings["className"] && settings["value"] != "&nbsp;") {
			settings["className"] += " " + messageSettings["className"];
		}
	}
	
	//add moduleID and row no. to the id of column
	table.push("<td id='property_module"+ this.moduleID + "_" + rowNumber +"_"+ fieldname + "' title='"+ settings["title"] +"' class='" + settings["className"] + "' " + (settings["width"]?"width='" + settings["width"] + "'":"") + ">" + settings["value"] + "</td>");
}

TableView.prototype.setRowColumn = function(element, fieldname, type, length, value, item)
{
	var settings = this.getRowColumnSettings(fieldname, type, length, value, item);

	// Check for specific actions to be executed.
	var messageClass = item.getElementsByTagName("message_class")[0];
	if(messageClass && messageClass.firstChild) {
		var messageSettings = this.getMessageSettings(messageClass.firstChild.nodeValue, value, item);
		if(messageSettings["className"] && settings["value"] != "&nbsp;") {
			settings["className"] += " " + messageSettings["className"];
		}
	}

	element.className = settings["className"];
	element.id = "property_module"+ this.moduleID +"_"+ element.parentNode.id +"_"+ fieldname;

	if(settings["width"]) {
		element.style.width = settings["width"] + "px";
	}

	element.innerHTML = settings["value"];
}

TableView.prototype.addRow = function(row)
{
	var entry = false;
	var tableElement = this.divElement.getElementsByTagName("table")[0];

	if(tableElement && row.childNodes.length > 0) {
		var entryid = row.getElementsByTagName(this.uniqueid)[0];
		var message_class = row.getElementsByTagName("message_class")[0];
		var message_flags = parseInt(dhtml.getXMLValue(row,"message_flags", 0),10);
		
		if(entryid && entryid.firstChild) {
			var id = 0;
			if (tableElement.rows.length>0){
				id = parseInt(tableElement.rows[tableElement.rows.length-1].id,10) + 1;
			}
			entry = new Object();
			entry["id"] = id;
			entry[this.uniqueid] = entryid.firstChild.nodeValue;

			// Get message class (double click => open window)
			var messageClass = "";
			if(message_class && message_class.firstChild) {
				messageClass = message_class.firstChild.nodeValue.replace(/\./g, "_").toLowerCase();
				
				switch(messageClass) {
					case "ipm_note":
					case "ipm_post":
					case "report_ipm_note_ndr":
					case "ipm_schedule_meeting_request":
					case "ipm_schedule_meeting_resp_pos":
					case "ipm_schedule_meeting_resp_tent":
					case "ipm_schedule_meeting_resp_neg":
					case "ipm_schedule_meeting_canceled":
					case "report_ipm_note_ipnnrn":
					case "report_ipm_note_ipnrn":
						messageClass = "ipm_readmail read_unread";
						
						var messageUnsent = row.getElementsByTagName("message_unsent")[0];
						if(messageUnsent && messageUnsent.firstChild) {
							messageClass = "ipm_createmail read_unread";
						}
						break;
				}
			}

			var rowElement = tableElement.insertRow(-1);
			rowElement.id = id;
			rowElement.className = "row " + messageClass;
			rowElement.setAttribute("messageflags", message_flags);

			if(typeof(dragdrop) != "undefined") {
				dragdrop.addDraggable(rowElement, "folder",null,null,this.moduleID);
			}
			if(this.events["row"]) {
				dhtml.setEvents(this.moduleID, rowElement, this.events["row"]);
			}
			
			for(var j = 0; j < this.columns.length; j++)
			{
				var column = this.columns[j];
				var property = row.getElementsByTagName(column["id"])[0];
				
				var cellElement = rowElement.insertCell(j);
			
				var value = "&nbsp;";
				if(property && property.firstChild) {
					value = property.firstChild.nodeValue;
				}
				
				this.setRowColumn(cellElement, column["id"], column["type"], column["length"], value, row);
				
				if(this.events["rowcolumn"] && this.events["rowcolumn"][column["id"]]) {
					dhtml.setEvents(this.moduleID, cellElement, this.events["rowcolumn"][column["id"]]);
				}
			}
		}
	}
	
	return entry;
}

TableView.prototype.showEmptyView = function(message)
{
	var tableElement = this.divElement.getElementsByTagName("table")[0];
	var rowElement = dhtml.getElementById("empty_view_message", "tr", tableElement);
	if(typeof message == "undefined") {
		message = _("There are no items to show in this view") + ".";
	}

	if(rowElement) {
		dhtml.deleteElement(rowElement);
	}

	if(tableElement) {
		var rowElement = tableElement.insertRow(-1);
		rowElement.id = "empty_view_message";
		var cell = rowElement.insertCell(-1);
		cell.innerHTML = message;
		cell.style.textAlign = "center";
	}
}

TableView.prototype.getRowColumnSettings = function(fieldname, type, length, value, item)
{
	var className = "rowcolumn";
	var width = false;
	var elementValue = "&nbsp;";
	var title = "";

	// Get message flag (unread)
	var message_flags = parseInt(dhtml.getXMLValue(item,"message_flags", 0),10);
	var messageUnread = true;
	if((message_flags & MSGFLAG_READ) == MSGFLAG_READ) {
		messageUnread = false;
	}
	
	/* Backward-compatibility hack:
	 *
	 * If the type of the field was sent by the server in its XML response, use that
	 * otherwise, fall back to backward-compatible mode of checking fieldname for formatting
	 */
	 
	var fieldname_or_type = fieldname;
	if(typeof(type) == "string") {
		// split "type" to have the posibility to add extra type data
		fieldname_or_type = type.split("|")[0];
	}
	
	switch(fieldname_or_type)
	{
		case "hidden_column":
			className = "hidden_column";
			break;
		
		case "attachicon":
		case "hasattach":
			className += " message_icon ";
			
			if(parseInt(value,10) == 1) {
				className += " icon_hasattach";
			}
			break;
		case "icon":
		case "icon_index":
			className += " message_icon ";
			var messageClass = dhtml.getTextNode(item.getElementsByTagName("message_class")[0],"IPM.Note");
			var isStub = dhtml.getXMLValue(item, "stubbed", false);
			var iconIndex = parseInt(value,10);

			className += iconIndexToClassName(iconIndex, messageClass, !messageUnread, isStub);
			break;
		case "display_type":
			className += " message_icon ";
			className += displayTypeToClassName(value);
			break;
		case "flag":
		case "flag_status":
		case "flag_icon":
			className += " message_icon ";
			
			className += this.getFlagClass(parseInt(dhtml.getXMLValue(item, "flag_status")), parseInt(dhtml.getXMLValue(item, "flag_icon")));
			break;

		case "importance":
			className += " message_icon ";

			switch(parseInt(value,10))
			{
				case IMPORTANCE_LOW:
					className += "icon_importance_low";
					title = _("Low");
					break;
					
				case IMPORTANCE_NORMAL:
					/**
					 * We want dropdown arrow for Normal Priority
					 * only in those list which has Quickitem ability.
					 */
					if (this.inputcolumnsElement.hasChildNodes()) {
						className += "icon_taskarrow";
						title = _("Normal");
					}
					break;
					
				case IMPORTANCE_HIGH:
					className += "icon_importance_high";
					title = _("High");
					break;		
			}
			break;
		case "checkbox":
		case "complete":
			var checked = "";
			if (typeof(type)=="string" && type.split("|").length>1){
				if (value.match(type.split("|")[1])){
					checked = "checked";
				}
			}else{
				if(parseInt(value,10) == 1) {
					checked = "checked";
				}
			}
			elementValue = "<input type='checkbox' " + checked + ">";
			break;
		case "percentage":
		case "percent_complete":
			if (value == "&nbsp;") value=0;
			elementValue = "<div class='rowcolumntext'>" + (value*100) + "%</div>";
			break;

		case "folder_name":
			var folder = webclient.hierarchy.getFolder(value);

			if (folder && typeof(folder["display_name"])=="string"){
				value = folder["display_name"].htmlEntities();
			}else{
				value = "&nbsp;";
			}
			elementValue = "<div class='rowcolumntext'>"+value+"</div>";
			width = 150;
			break;
			
		//recurring	icon	
		case "recurring":
			className += " message_icon ";
			if(parseInt(value,10) == 1) {
				className += "icon_recurring";
			}
			break;
			
		//reminder icon	
		case "reminder":
			className += " message_title ";
			if(parseInt(value,10) == 1) {
				className += "icon_reminder";
			}
			break;
			
		// to display duration with minutes at postfix
		case "duration":
			elementValue = "<div class='rowcolumntext'>" + simpleDurationString(parseInt(value, 10))+ "</div>";
			break;
				
		//all day as chechbox
		case "alldayevent":
			var checked = "";
			className += " message_title ";
			if(parseInt(value,10) == 1) {
					checked = "checked='checked'";
			}
			elementValue = "<input type='checkbox' disabled='disabled' " + checked + ">";	
			break;

		// to display meeting status as a string
		case "meeting":
			var responsestatus = dhtml.getXMLValue(item, "responsestatus");
			var meeting = dhtml.getXMLValue(item, "meeting");
				if(parseInt(meeting,10) == 0){
					elementValue = "<div class='rowcolumntext'></div>";		
				}else{ 
					switch(parseInt(responsestatus,10))
					{
						case olResponseNone:
							elementValue = "<div class='rowcolumntext'>"+ _("No Response")+"</div>";
							break;

						case olResponseOrganized:
							elementValue = "<div class='rowcolumntext'>"+ _("Meeting Organizer")+"</div>";
							break;

						case olResponseTentative:
							elementValue = "<div class='rowcolumntext'>"+ _("Tentatively Accepted")+"</div>";
							break;	
							
						case olResponseAccepted:
							elementValue = "<div class='rowcolumntext'>"+ _("Accepted")+"</div>";
							break;	
		
						case olResponseDeclined:
							elementValue = "<div class='rowcolumntext'>"+ _("Declined")+"</div>";
							break;	
					
						case olResponseNotResponded:
							elementValue = "<div class='rowcolumntext'>"+ _("Not Yet Responded")+"</div>";
							break;	
						
						default:
							elementValue = "<div class='rowcolumntext'></div>";
							break;
					}
				}	
				width = 150;
				break;
		
			//busystatus
			case "busystatus":
				switch(parseInt(value,10))
				{
					case fbFree:
						elementValue = "<div class='rowcolumntext'>"+ _("Free")+"</div>";
						break; 
					case fbTentative:
						elementValue = "<div class='rowcolumntext'>"+ _("Tentative")+"</div>"; 
						break;
					case fbBusy:
						elementValue = "<div class='rowcolumntext'>"+ _("Busy")+"</div>"; 
						break;
					case fbOutOfOffice:
						elementValue = "<div class='rowcolumntext'>"+ _("Out of Office")+"</div>";
						break
				}
				width = 150; 
				break;

		//label
		case "label":
			switch(parseInt(value,10))
			{
				case 0:
				elementValue = "<div class='rowcolumntext'>"+ _("None")+"</div>";		
				break;
					
				case 1:
				elementValue = "<div class='rowcolumntext'>"+ _("Important")+"</div>";
				break;	
					
				case 2:
				elementValue = "<div class='rowcolumntext'>"+ _("Business")+"</div>";
				break;	
					
				case 3:
				elementValue = "<div class='rowcolumntext'>"+ _("Personal")+"</div>";
				break;	
				case 4:
				elementValue = "<div class='rowcolumntext'>"+ _("Vacation")+"</div>";		
				break;
					
				case 5:
				elementValue = "<div class='rowcolumntext'>"+ _("Must Attend")+"</div>";
				break;	
					
				case 6:
				elementValue = "<div class='rowcolumntext'>"+ _("Travel Required")+"</div>";	
				break;	
					
				case 7:
				elementValue = "<div class='rowcolumntext'>"+ _("Needs Preparation")+"</div>";
				break;	
				
				case 5:
				elementValue = "<div class='rowcolumntext'>"+ _("Birthday")+"</div>";
				break;	
					
				case 8:
				elementValue = "<div class='rowcolumntext'>"+ _("Anniversary")+"</div>";
				break;	
					
				case 9:
				elementValue = "<div class='rowcolumntext'>"+ _("Phone Call")+"</div>";
				break;
				
				default:
				elementValue = "<div class='rowcolumntext'></div>";
				break;
			}
			width = 150;
			break;
		
		//sensitivity	
		case "sensitivity":
			switch(parseInt(value,10))
			{
				case SENSITIVITY_NONE:
					elementValue = "<div class='rowcolumntext'>"+ _("Normal") +"</div>";
					break;

				case SENSITIVITY_PERSONAL:
					elementValue = "<div class='rowcolumntext'>"+ _("Personal")+"</div>";
					break;

				case SENSITIVITY_PRIVATE:
					elementValue = "<div class='rowcolumntext'>"+ _("Private")+"</div>";
					break;

				case SENSITIVITY_COMPANY_CONFIDENTIAL:
					elementValue = "<div class='rowcolumntext'>"+ _("Confidential")+"</div>";
					break;

				default:
					elementValue = "<div class='rowcolumntext'></div>";
					break;
			}
			width = 150;
			break;

		default:
			// fix any HTML characters, because value will be added directly to innerHTML
			// known bug: if a subject only contains "&nbsp;" it wouldn't display
			if (value && value != "&nbsp;"){
				// check for other types in the 'type' attribute
				var fieldType = dhtml.getXMLNode(item, fieldname).getAttribute("type");
				switch(fieldType) {
					case "timestamp":
						value = strftime(_("%a %x %X"),value);
						break;
					case "timestamp_date":
						value = strftime_gmt(_("%a %x"),value);
						break;
					default:
						value = value.htmlEntities();
						break;
				}
			}

			if(!value) {
				value = "&nbsp;";
			}

			elementValue = "<div class='rowcolumntext'>" + value + "</div>";
			
			// Default width for all string columns is 150px
			width = 150;
			break;
	}

	// Override with width sent from server	
	if(length) {
		if(parseInt(length,10)) {
			width = length;
		} else {
			width = false; // Width specified as 'percentage'
		}
	}

	var rowColumnSettings = new Object();
	rowColumnSettings["className"] = className;
	rowColumnSettings["width"] = width;
	rowColumnSettings["value"] = elementValue;	
	rowColumnSettings["title"] = title;

	return rowColumnSettings;
}

TableView.prototype.getMessageSettings = function(messageClass, value, message)
{
	var messageSettings = new Object();
	messageSettings["className"] = "";
	
	switch(messageClass)
	{
		case "IPM.DistList":
			messageSettings["className"] += "distlist";
			break;
		case "IPM.Task":
			// Check Duedate (red color)
			var duedate = parseInt(dhtml.getXMLValue(message, "duedate", 0),10);
			if(duedate>0) {
				var currentDate = new Date();
				var messageDate = new Date((duedate*1000)+ONE_DAY);
					
				if(currentDate.getTime() > messageDate.getTime()) {
					messageSettings["className"] += "pastduedate";
				}
			}

			// Set Complete (line-through)
			var complete = message.getElementsByTagName("complete")[0];
			if(complete && complete.firstChild) {
				if(complete.firstChild.nodeValue == "1" && value != "&nbsp;") {
					messageSettings["className"] += " complete";
				}
			}
			break;
	}
	
	return messageSettings;
}

TableView.prototype.setComplete = function(row, className)
{
	if(row.cells) {
		for(var i = 0; i < row.cells.length; i++)
		{
			if(className.length > 0) {
				if(row.cells[i].innerHTML != "&nbsp;") {
					row.cells[i].className += " " + className;
				}
			} else {
				if(row.cells[i].className.indexOf("complete") > 0) {
					row.cells[i].className = row.cells[i].className.substring(0, row.cells[i].className.indexOf("complete"));
				}
			}
		}
	}
}

TableView.prototype.sortColumns = function(columnA, columnB)
{
	if(columnA["order"] > columnB["order"]) return 1;
	if(columnA["order"] < columnB["order"]) return -1;
	return 0;
}

TableView.prototype.destructor = function()
{
	webclient.inputmanager.removeObject(webclient.getModule(this.moduleID));

	if(this.pagingTool) {
		this.pagingTool.destructor();
		dhtml.deleteAllChildren(dhtml.getElementById("pageelement_"+this.moduleID));
	}

	this.element.innerHTML = "";
}

/**
 * Function which creates the paging element
 */ 
TableView.prototype.pagingElement = function(totalrowcount, rowcount, rowstart)
{
	var pagingToolElement = dhtml.getElementById("pageelement_"+this.moduleID);
	dhtml.deleteAllChildren(pagingToolElement);

	if(this.pagingTool) {
		var pageElement = dhtml.getElementById("page_"+this.moduleID);
		pageElement.style.display = "none";

		this.pagingTool.destructor();
	}

	// Number of pages
	var pages = Math.floor(totalrowcount / rowcount);
	if((totalrowcount % rowcount) > 0) {
		pages += 1;
	}

	// current page
	var currentPage = Math.floor(rowstart / rowcount);

	// create paging element
	this.pagingTool = new Pagination("paging", eventListChangePage, this.moduleID);
	
	if(pages > 0) {
		this.pagingTool.createPagingElement(pagingToolElement, pages, currentPage);
		var pageElement = dhtml.getElementById("page_"+this.moduleID);
		pageElement.style.display = "block";
	}

	return true;
}

TableView.prototype.removePagingElement = function()
{
	var pagingToolElement = dhtml.getElementById("pageelement_"+this.moduleID);
	dhtml.deleteAllChildren(pagingToolElement);

	if(this.pagingTool) {
		var pageElement = dhtml.getElementById("page_"+this.moduleID);
		
		pageElement.style.display = "none";
		
		this.pagingTool.destructor();
	}
}

/**
 * Returns the row number of an element
 */
TableView.prototype.getRowNumber = function(elemid)
{
	var elem = dhtml.getElementById(elemid);
	var rowNum = 0;
	
	while(elem) {
		elem = elem.previousSibling;
		if(!elem)
			break;
		rowNum++;
	}
	
	return rowNum;
}

/**
 * Returns the total number of rows
 */
TableView.prototype.getRowCount = function()
{
	var elems = this.divElement.getElementsByTagName("tr");
	return elems.length;
}

/**
 * Returns the element ID of a specific row number
 */
TableView.prototype.getElemIdByRowNumber = function(rownum)
{
	var elems = this.divElement.getElementsByTagName("tr");
	if (typeof elems[rownum] != "undefined"){
		return elems[rownum].id;
	}else{
		return;
	}	
}

/**
 * Returns the flag class for the red/blue/etc flag
 */
TableView.prototype.getFlagClass = function(flagStatus, flagIcon)
{
	var className = "";

	switch(flagIcon)
	{
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
					className = "icon_flag";
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

// Sets the cursor position
TableView.prototype.setCursorPosition = function(id)
{
	var element = dhtml.getElementById(id);

	if (element){
		this.cursorElementL.style.top = (element.offsetTop) + "px";	
		if(element.offsetHeight != 0) this.cursorElementL.style.height = (element.offsetHeight - 3) + "px";
		this.cursorElementL.style.visibility = "visible";	
		this.cursorElementT.style.top = (element.offsetTop) + "px";	
		if(element.offsetWidth != 0) this.cursorElementT.style.width = (element.offsetWidth - 2) + "px";
		this.cursorElementT.style.visibility = "visible";	
		this.cursorElementR.style.top = (element.offsetTop) + "px";	
		if(element.offsetWidth != 0) this.cursorElementR.style.left = (element.offsetWidth - 2) + "px";
		if(element.offsetHeight != 0) this.cursorElementR.style.height = (element.offsetHeight - 3) + "px";
		this.cursorElementR.style.visibility = "visible";	
		this.cursorElementB.style.top = (element.offsetTop + element.offsetHeight - 2) + "px";
		if(element.offsetWidth != 0) this.cursorElementB.style.width = (element.offsetWidth - 2 ) + "px";
		this.cursorElementB.style.visibility = "visible";	

		this.cursorMessage = id;
	}else{
		this.cursorElementL.style.visibility = "hidden";	
		this.cursorElementT.style.visibility = "hidden";	
		this.cursorElementR.style.visibility = "hidden";	
		this.cursorElementB.style.visibility = "hidden";	
	}
}

TableView.prototype.getCursorPosition = function(id)
{
	return this.cursorMessage;
}
/**
 * Function which returns all row elements
 * @return array all row elements
 */
TableView.prototype.getAllRowElements = function()
{
	// Get row count
	var rowCount = this.getRowCount();
	var elements = new Array();
	for (var i = 0; i < rowCount; i++){
		// Get row id
		var elementID = this.getElemIdByRowNumber(i);
		var element = dhtml.getElementById(elementID);
		if (element){
			elements.push(element);
		}
	}
	return elements;
}

TableView.prototype.setEventsOnLayoutElement = function()
{
	// Prevent IE scrolling the pane when user does up/down/pgup/etc	
	dhtml.addEvent(this, this.divElement, "keydown", this.onDivKeyDown);
}

// FIXME FIXME this is called with 'mobuleObject' referring not to this view oject, but to our parent module!
function eventTableViewKeyboard(moduleObject, element, event)
{
	// Set to TRUE if we have really selected the item, like pressing on an item with the mouse
	var openItem = false;
	
	if (typeof moduleObject != "undefined"){

		if (event.type == "keydown"){

			// get the right element
			if (moduleObject && moduleObject instanceof ListModule && typeof moduleObject.selectedMessages[0] != "undefined"){
				// WARNING element shadows parameter 'element'
				element = dhtml.getElementById(moduleObject.viewController.viewObject.cursorMessage);
			
				if (element){
					var selectElement = null;
					var divElement = moduleObject.viewController.viewObject.divElement;
		
					switch (event.keyCode){
						case 36: // HOME
								if(!moduleObject.editMessage){		// check if the message is in the editable form
									selectElement = element.parentNode.firstChild;
									if(!event.ctrlKey)
										openItem = true;
								}
							break;
						case 33: // PAGE UP
							var itemsPerPage = Math.floor(divElement.offsetHeight/element.offsetHeight);
							var i = 1;
							selectElement = element.previousSibling;
							while(i<itemsPerPage && selectElement){
								selectElement = selectElement.previousSibling;
								i++;
							}
							if (!selectElement){ // select first item if we don't have a correct ite
								selectElement = element.parentNode.firstChild;
							}
							if(!event.ctrlKey)
								openItem = true;
							break;
						case 38: // KEY_UP
							selectElement = element.previousSibling;
							if(!event.ctrlKey)
								openItem = true;
							break;
						case 40: // KEY_DOWN
							selectElement = element.nextSibling;
							if(!event.ctrlKey)
								openItem = true;
							break;
						case 32: // SPACEBAR
							if (typeof moduleObject.editMessage == "undefined" || moduleObject.editMessage === false) {
								selectElement = element;
								openItem = true;
							}
							break;
						case 34: // PAGE DOWN
							var itemsPerPage = Math.floor(divElement.offsetHeight/element.offsetHeight);
							var i = 1;
							selectElement = element.nextSibling;
							while(i<itemsPerPage && selectElement){
								selectElement = selectElement.nextSibling;
								i++;
							}
							if (!selectElement){ // select last item if we don't have a correct item
								selectElement = element.parentNode.childNodes[element.parentNode.childNodes.length-1];
							}
							if(!event.ctrlKey)
								openItem = true;
							break;
						case 35: // END
							if(!moduleObject.editMessage){		// check if the message is in the editable form
								selectElement = element.parentNode.childNodes[element.parentNode.childNodes.length-1];
								if(!event.ctrlKey)
									openItem = true;
							}
							break;
						case 13: // ENTER
							eventListDblClickMessage(moduleObject, element, event);
							break;
						case 46: // DELETE
							// event.shiftKey for soft delete if Shift+Del
							moduleObject.deleteMessages(moduleObject.getSelectedMessages(), event.shiftKey);
							break;
		
						case 37: // LEFT
							var pagingTool = moduleObject.viewController.viewObject.pagingTool;
							if (pagingTool.hackPaging["prev"]){
								dhtml.executeEvent(pagingTool.hackPaging["prev"], "click");
							}
							break;
						case 39: // RIGHT
							var pagingTool = moduleObject.viewController.viewObject.pagingTool;
							if (pagingTool.hackPaging["next"]){
								dhtml.executeEvent(pagingTool.hackPaging["next"], "click");
							}
							break;
					}
		
					// when we select an element, we want to scroll the view and set the cursor position
					if (selectElement){
						moduleObject.viewController.setCursorPosition(selectElement.id);
						tableViewScroll(moduleObject, selectElement, event);
						if(openItem) {
							// Really select the item as if we clicked on it
							selectItem(moduleObject, selectElement, event);
						}
					}					
				}
			}
		}
	}
}

function selectItem(moduleObject, selectedElement, event) 
{
	if(selectedElement) {
		eventListMouseDownMessage(moduleObject, selectedElement, event);
		if (moduleObject.previewTimer){
				window.clearTimeout(moduleObject.previewTimer);
			}
		/**
		 * This delay is used when the user is using the up and down arrow keys
		 * to browse between the items in the list. When the presses the down key several times
		 * this delay keeps the WA from loading each item.
		 * Instead only the last item that is selected will be loaded.
		 */
		moduleObject.previewTimer = window.setTimeout(function() {
											eventListMouseUpMessage(moduleObject, selectedElement, event);
										}, 250);
	}
}

function tableViewScroll(moduleObject, selectedElement, event)
{
	if(selectedElement) {
		var scrollElem = moduleObject.viewController.viewObject.divElement;
		if (selectedElement.offsetTop < scrollElem.scrollTop){
			scrollElem.scrollTop = selectedElement.offsetTop;
		}else if (selectedElement.offsetTop+selectedElement.offsetHeight > scrollElem.scrollTop+scrollElem.offsetHeight){
			scrollElem.scrollTop = (selectedElement.offsetTop+selectedElement.offsetHeight) - scrollElem.offsetHeight;
		}
	}
}

/**
* Sets up datepicker
* @param	string inputfield -id of duedate input field
* @param	string showbutton -id of trigger button 
*/
function setcal(inputfield,showbutton) {	
	Calendar.setup({
		inputField	:	inputfield,			    	// id of the input field
		ifFormat	:	_('%d-%m-%Y'),				// format of the input field
		button		:	showbutton,					// trigger for the calendar (button ID)
		step		:	1,							// show all years in drop-down boxes (instead of every other year as default)
		weekNumbers	:	false
	});
}

//deselects and removes input fields if any message is in edit form...
function eventTableViewMouseDown(moduleObject, element, event)
{		
	// first check that any message is in edit form...
	if (moduleObject && moduleObject.editMessage !== false && moduleObject.stopbubble == false){
		// here we have to pass element id of message which is in edit mode
		// not the current element
		moduleObject.SubmitMessage(moduleObject, dhtml.getElementById(moduleObject.editMessage), event);
		moduleObject.removeinputfields(moduleObject, false, event);
	}
	
	moduleObject.stopbubble = false;
	//check is importance menu is selected and remove it if exists...
	var importancemenu = dhtml.getElementById("importancemenu");
	if (importancemenu){
		dhtml.showHideElement(importancemenu, event.clientX, event.clientY, true);
	}
}
/**
 * Function which selects all items.
 */
function eventTableViewKeyCtrlSelectAll(moduleObject, element, event, keys)
{
	// Retrive all row elements
	var elements = moduleObject.viewController.viewObject.getAllRowElements();
	moduleObject.selectMessages(elements, "rowselected");
}
/**
 * Function which activates/deactivates quick edit mode in list.
 */
function eventTableViewKeyCtrlQuickEdit(moduleObject, element, event, keys)
{
	if (moduleObject.inputproperties && moduleObject.inputproperties.length > 0
		&& moduleObject.selectedMessages && moduleObject.selectedMessages.length > 0){
		switch(event.keyCombination)
		{
			case keys["activate"]:
				var element = dhtml.getElementById(moduleObject.selectedMessages[0]);

				if (element)
					moduleObject.CreateEditFields(moduleObject, element, event);
				break;
			case keys["deactivate"]:
				if (moduleObject.editMessage)
					moduleObject.removeinputfields(moduleObject, false, event);
				break;
		}
	}
}