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
* Module for making the printlist and printing
*/
printlistmodule.prototype = new ListModule;
printlistmodule.prototype.constructor = printlistmodule;
printlistmodule.superclass = ListModule.prototype;

function printlistmodule(id, element, title, data) {
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

/**
 * Function which intializes the module.
 * @param integer id the module id
 * @param HtmlElement element the element in which all elements will be appended
 * @param string title the title of the module
 * @param object data the data (storeid, entryid, ...)
 */
printlistmodule.prototype.init = function(id, element, title, data) {
	this.id = id;
	this.viewObject = false;

	this.data = data;
	this.element = element;
	this.keys = new Array();
	this.keys["print"] = KEYS["edit_item"]["print"];

	this.initializeView(this.data["view"]);
	
	// Add keycontrol event
	webclient.inputmanager.addObject(this);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys, "keyup", eventPrintListKeyCtrlPrint);
}

/**
 * Function which intializes the view.
 */
printlistmodule.prototype.initializeView = function(view) {
	if(typeof(uniqueid) == "undefined") {
		uniqueid = "entryid";
	}

	switch(view) {
		case "day":
			this.data["days"] = 1;
			this.viewObject = new PrintCalendarDayView(this.id, this.element, false, this.data, uniqueid);
			break;
		case "workweek":
			this.data["days"] = 5;
			this.viewObject = new PrintCalendarDayView(this.id, this.element, false, this.data, uniqueid);
			break;
		case "week":
			this.viewObject = new PrintCalendarWeekView(this.id, this.element, false, this.data, uniqueid);
			break;
		case "7days":
			this.data["days"] = 7;
			this.viewObject = new PrintCalendarDayView(this.id, this.element, false, this.data, uniqueid);
			break;
		case "month":
			this.viewObject = new PrintCalendarMonthView(this.id, this.element, false, this.data, uniqueid);
			break;
		case "list":
			this.viewObject = new PrintCalendarListView(this.id, this.element, false, this.data, uniqueid);
			break;
		default:
			this.viewObject = new PrintView(this.id, this.element, false, this.data, uniqueid);	
	}

	// to get the data from the server
	this.list();
}

/**
 * Function which takes care of the list action. It is responsible for
 * calling the "addItems" function in the view.
 * @param object action the action tag
 */
printlistmodule.prototype.messageList = function(action) {
	this.propertylist = new Array();
	this.properties = new Array();
	var properties = action.getElementsByTagName("column");

	// Columns
	for(var i = 0; i < properties.length; i++) {
		var id = properties[i].getElementsByTagName("id")[0];

		if(id && id.firstChild) {
			var order = properties[i].getElementsByTagName("order")[0];
			var name = properties[i].getElementsByTagName("name")[0];
			var title = properties[i].getElementsByTagName("title")[0];
			var length = properties[i].getElementsByTagName("length")[0];
			var visible = properties[i].getElementsByTagName("visible")[0];
			var type = properties[i].getElementsByTagName("type")[0];

			var property = new Object();
			property["id"] = id.firstChild.nodeValue;
			property["order"] = (order && order.firstChild?order.firstChild.nodeValue:false);
			property["name"] = (name && name.firstChild?name.firstChild.nodeValue:false);
			property["title"] = (title && title.firstChild?title.firstChild.nodeValue:false);
			property["length"] = (length && length.firstChild?length.firstChild.nodeValue:false);
			property["visible"] = (visible && visible.firstChild?visible.firstChild.nodeValue:false);
			property["type"] = (type && type.firstChild?type.firstChild.nodeValue:false);

			this.propertylist.push(property);

			if(property["visible"]) {
				this.properties.push(property);
			}
		}
	}

	// Sort
	var sort = action.getElementsByTagName("sort")[0];
	if(sort && sort.firstChild) {
		var sortColumn = sort.firstChild.nodeValue;
		var sortDirection = sort.getAttribute("direction");

		var column = new Object();
		column["attributes"] = new Object();
		column["attributes"]["direction"] = sortDirection;
		column["_content"] = sortColumn;
		this.sort = new Array(column);
	}

	this.viewObject.deleteLoadMessage();
	this.entryids = this.addItems(action.getElementsByTagName("item"), this.properties, action);
}

/**
 * Function which adds items in the view. This function is executed
 * after the complete list response is received from the server.
 * @param array items list of items received from the server
 * @param array properties property list
 * @param object action the XML action
 * @return array list of entryids      
 */ 
printlistmodule.prototype.addItems = function(items, properties, action, groupID) {
	var entryids = new Array();
	if(typeof groupID == "undefined"){
		groupID = null;
	}

	if(this.viewObject) {
		entryids = this.viewObject.execute(items, properties, action, groupID);
	}

	return entryids;
}

/** 
 * Function which is used to create xml object 
 * and send the request to server
 */
printlistmodule.prototype.list = function() {
	if(this.data["store"] && this.data["entryid"]) {
		// created a local xmldata object to pass to server becuase all data passed in this.data object is
		// not needed
		var xmldata = new Object();
		xmldata["moduleID"] = this.data["moduleID"];
		xmldata["entryid"] = this.data["entryid"];
		xmldata["store"] = this.data["store"];
		xmldata["restriction"] = new Object();
		xmldata["restriction"]["startdate"] = this.data["restriction"]["startdate"] / 1000;
		xmldata["restriction"]["duedate"] = this.data["restriction"]["duedate"] / 1000;
		xmldata["restriction"]["selecteddate"] = this.data["restriction"]["selecteddate"] / 1000;

		webclient.xmlrequest.addData(this, "list", xmldata, webclient.modulePrefix);
		this.viewObject.loadMessage();
		webclient.xmlrequest.sendRequest();
	}
}

/**
 * Function which resizes the view.
 */
printlistmodule.prototype.resize = function()
{
	this.viewObject.resizeView();
}

/**
 * this function is called on clicking on print button
 * this function will copy all printing contents to iframe and then print the iframe
 */
printlistmodule.prototype.printIFrame = function() {
	switch(this.data["view"]) {
		case "day":
		case "workweek":
		case "7days":
			window.frames["printing_frame"].document.getElementById("print_calendar").style.height = (dhtml.getElementById("days").scrollHeight + dhtml.getElementById("1_header").offsetHeight) + "px";
			window.frames["printing_frame"].document.getElementById("days").style.height = dhtml.getElementById("days").scrollHeight + "px";
			window.frames["printing_frame"].document.getElementById("days").style.overflow = "hidden";
			break;
		case "week":
			break;
		case "month":
			break;
		case "list":
			// print list view
			window.frames["printing_frame"].document.getElementById("print_calendar").style.height = dhtml.getElementById("list").scrollHeight + "px";
			window.frames["printing_frame"].document.getElementById("list").style.height = dhtml.getElementById("list").scrollHeight + "px";
			break;
	}

	dhtml.getElementById("printing_frame").style.visibility = "visible";

	if (window.frames["printing_frame"]) {
		window.frames["printing_frame"].focus();
		window.frames["printing_frame"].print();
	}
}

function eventPrintListKeyCtrlPrint(moduleObject, element, event)
{
	moduleObject.printIFrame();
}