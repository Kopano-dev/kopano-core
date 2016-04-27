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
 * ViewController
 * Function which loads a new view object and it passes the the calls
 * to this loaded view object.   
 */ 
function ViewController()
{
	// The view
	this.view = false;
	
	// The view object
	this.viewObject = false;
}

ViewController.prototype.destructor = function()
{
	if(this.viewObject.destructor){
		this.viewObject.destructor();
		this.viewObject = false;
	}
}

/**
 * Function which loads a new view object.
 * @param integer moduleID the module id
 * @param string view the view
 * @param object element html element
 * @param object events all the events
 * @param object data additional data which will be passed to the view object
 */ 
ViewController.prototype.initView = function(moduleID, view, element, events, data, uniqueid)
{
	this.view = view;
	this.moduleID = moduleID;
	
	var oldHasRightPane = webclient.hasRightPane;
	
	webclient.hasRightPane = false;
	
	if(typeof(uniqueid) == "undefined")
		uniqueid = "entryid";

	switch(view)
	{
		// Contacts
		case "contact_cards":
			this.viewObject = new AddressCardsView(moduleID, element, events, data, uniqueid);
			break;
		case "detailed_address_cards":
			break;
		
		// Stickynote icon
		case "icon":
			this.viewObject = new IconView(moduleID, element, events, data, uniqueid);
			break;
			
		// Calendar
		case "day":
			data["days"] = 1;
			this.viewObject = new CalendarDayView(moduleID, element, events, data, uniqueid);
			break;
		case "workweek":
			data["days"] = 5;
			this.viewObject = new CalendarDayView(moduleID, element, events, data, uniqueid);
			break;
		case "7days":
			data["days"] = 7;
			this.viewObject = new CalendarDayView(moduleID, element, events, data, uniqueid);
			break;
		case "week":
			this.viewObject = new CalendarWeekView(moduleID, element, events, data, uniqueid);
			break;
		case "month":
			this.viewObject = new CalendarMonthView(moduleID, element, events, data, uniqueid);
			break;
		case "datepicker":
			this.viewObject = new CalendarDatePickerView(moduleID, element, events, data, uniqueid);
			break;
		case "multiusercalendar":
			this.viewObject = new MultiUserCalendarView(moduleID, element, events, data);
			break;
		case "multiusercalendarappointmentlist":
			this.viewObject = new MultiUserCalendarAppointmentListView(moduleID, element, events, data);
			break;
		case "today":
			this.viewObject = new TodayView(moduleID, element, events, data);
			break;
		case "todayAppointment":
			this.viewObject = new TodayCalendarView(moduleID, element, events, data);
			break;
		case "todayTask":
			this.viewObject = new TodayTaskView(moduleID, element, events, data);
			break;
		case "todayFolder":
			this.viewObject = new TodayFolderView(moduleID, element, events, data);
			break;
								
		case "email":
			var preview = webclient.settings.get("global/previewpane", "right");
			var entryid = webclient.getModule(this.moduleID).entryid;
			
			// If we're loading the initial view of the inbox, then use that setting
			if(!entryid) entryid = "inbox";
			
			preview = webclient.settings.get("folders/entryid_"+entryid+"/previewpane",preview);

			
			this.view = "table";
			
			if(preview == "right") {
				this.viewObject = new TableCompactView(moduleID, element, events, data, uniqueid);
				webclient.hasRightPane = true;
			} else {
				this.viewObject = new TableView(moduleID, element, events, data, uniqueid);
			}
			
			break;

		// Default: table view
		case "contact_list":
		default:
			if(view){
				webclient.pluginManager.triggerHook('client.core.viewcontroller.initview.addcustomview', {
					view: view,
					viewcontroller: this,
					moduleID: moduleID,
					element: element,
					events: events,
					data: data,
					uniqueid: uniqueid
				});
			}
			if(!this.viewObject){
				this.view = "table";
				
				this.viewObject = new TableView(moduleID, element, events, data, uniqueid);
				break;
			}
	}
	
	if(webclient.hasRightPane != oldHasRightPane) {
		eventWebClientResize();
	}
}

/**
 * Function which resizes the view.
 */ 
ViewController.prototype.resizeView = function()
{
	if(this.viewObject) {
		this.viewObject.resizeView();
	}

	this.setCursorPosition(this.getCursorPosition());
}

/**
 * Function which shows a load message in the view
 */ 
ViewController.prototype.loadMessage = function()
{
	if(this.viewObject) {
		this.viewObject.loadMessage();
	}
}

/**
 * Function which deletes the load message in the view
 */ 
ViewController.prototype.deleteLoadMessage = function()
{
	if(this.viewObject) {
		this.viewObject.deleteLoadMessage();
	}
}

/**
 * Function which adds items in the view. This function is executed
 * after the complete list response is received from the server.
 * @param array items list of items received from the server
 * @param array properties property list
 * @param object action the XML action
 * @param array inputProperties property list
 * @return array list of entryids  
 */ 
ViewController.prototype.addItems = function(items, properties, action, groupID, inputProperties)
{
	var entryids = new Array();
	if(typeof groupID == "undefined"){
		groupID = null;
	}
	
	if(this.viewObject) {
		entryids = this.viewObject.execute(items, properties, action, groupID, inputProperties);
	}
	
	var firstElemId = this.getElemIdByRowNumber(0);
	if (firstElemId!==false){
		this.setCursorPosition(firstElemId);
	}

	return entryids;
}

/**
 * Function which adds an item in the view.
 * @param array item the item
 * @param array properties property list
 * @param object action the XML action
 * @return object entry of the item or false if refresh is needed
 */ 
ViewController.prototype.addItem = function(item, properties, action, groupID)
{
	// return false to force a reload when we are in the tasklistmodule and we add a new item
	if (webclient.getModule(this.moduleID).getModuleName()=="tasklistmodule" && action.getElementsByTagName("delete").length == 0){
		return false;
	}

	var entry = false;
	if(typeof groupID == "undefined"){
		groupID = null;
	}
	
	if(this.viewObject) {
		entry = this.viewObject.addItem(item, properties, action, groupID);
	}
	
	return entry;
}

/**
 * Function which updates an item in the view.
 * @param object element html element 
 * @param array item the item
 * @param array properties property list
 */ 
ViewController.prototype.updateItem = function(element, item, properties, groupID)
{
	var entry = false;
	if(typeof groupID == "undefined"){
		groupID = null;
	}
	
	if(this.viewObject) {
		entry = this.viewObject.updateItem(element, item, properties, groupID);
	}
	
	return entry;
}

/**
 * Function which deletes items in the view.
 * @param array items the items which should be deleted
 * @todo
 * - implement this function in every view object and call this function
 *   in the list module js file.   
 */
ViewController.prototype.deleteItems = function(items, groupID)
{
	var result = false;
	if(typeof groupID == "undefined"){
		groupID = null;
	}

	if(this.viewObject) {
		result = this.viewObject.deleteItems(items, groupID);
	}
	return result;
}

/**
 * Function which adds the paging element to the view.
 * @param integer totalrowcount total row count in a folder
 * @param integer rowcount the visible rows (for example: 10, 20, ..., 50, etc.)
 * @param integer rowstart the start row (for example: 0, 50, 150, etc)
 * @return boolean this boolean indicates if a total refresh is needed, if no option
 *                 is selected in the combobox  
 */ 
ViewController.prototype.pagingElement = function(totalrowcount, rowcount, rowstart)
{
	var selected = false;
	
	if(this.viewObject) {
		if(this.viewObject.pagingElement) {
			selected = this.viewObject.pagingElement(totalrowcount, rowcount, rowstart);
		} else {
			selected = true;
		}
	}
	return selected;
}

ViewController.prototype.removePagingElement = function()
{
	if(this.viewObject && this.viewObject.removePagingElement) {
		this.viewObject.removePagingElement();
	}
}

ViewController.prototype.getRowNumber = function(elemid)
{
	if(this.viewObject)
		return this.viewObject.getRowNumber(elemid);
	else
		return;
}

ViewController.prototype.getElemIdByRowNumber = function(rownum)
{
	if(this.viewObject && this.viewObject.getElemIdByRowNumber)
		return this.viewObject.getElemIdByRowNumber(rownum);
	else
		return;
}

ViewController.prototype.getRowCount = function()
{
	if(this.viewObject)
		return this.viewObject.getRowCount();
	else
		return;
}

ViewController.prototype.setCursorPosition = function(id)
{
	if(this.viewObject)
		return this.viewObject.setCursorPosition(id);
	else
		return;
}

ViewController.prototype.getCursorPosition = function()
{
	if(this.viewObject)
		return this.viewObject.getCursorPosition();
	else
		return;
}






ViewController.prototype.addGroup = function(groupData){
	if(this.viewObject && typeof this.viewObject.addGroup == "function"){
		var groupID = this.viewObject.addGroup(groupData);
	}else{
		var groupID = false;
	}
	return groupID;
}
ViewController.prototype.updateGroup = function(groupID, groupData){
	if(this.viewObject && typeof this.viewObject.updateGroup == "function"){
		var returnID = this.viewObject.updateGroup(groupID, groupData);
	}else{
		var returnID = false;
	}
	return returnID;
}
ViewController.prototype.deleteGroup = function(groupID, data){
	if(this.viewObject && typeof this.viewObject.deleteGroup == "function"){
		var returnID = this.viewObject.deleteGroup(groupID, data);
	}else{
		var returnID = false;
	}
	return returnID;
}

/**
 * Function which displays clue, that 
 * tells user whether full GAB is enabled/disabled.
 */
ViewController.prototype.GAB = function(disable_full_gab)
{
	if (disable_full_gab){
		if (this.viewObject && (typeof this.viewObject.divElement != "undefined")) {
			var divelement = this.viewObject.divElement;
			var gab_status = dhtml.addElement(false, "div", "gab_status", "gab_status", _("Please search for a name"));
			divelement.insertBefore(gab_status, divelement.firstChild);
		}
	}
}
