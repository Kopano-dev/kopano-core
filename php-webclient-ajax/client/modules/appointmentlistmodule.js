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

appointmentlistmodule.prototype = new ListModule;
appointmentlistmodule.prototype.constructor = appointmentlistmodule;
appointmentlistmodule.superclass = ListModule.prototype;

function appointmentlistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
	
}

appointmentlistmodule.prototype.init = function(id, element, title, data)
{
	this.entryid = data["entryid"];
	this.selectedview = webclient.settings.get("folders/entryid_"+this.entryid+"/selected_view", "workweek");
	
	this.startdate = false;
	this.duedate = false;

	this.setDays(new Date().getTime());
	
	data["has_no_menu"] = true;
	appointmentlistmodule.superclass.init.call(this, id, element, title, data);
	
	//Generate the condition to initialize mousedown event and set readflag on double click in table view
	if(this.selectedview != "table"){
		delete this.events["row"]["mousedown"];
		this.events["row"]["dblclick"] = eventAppointmentListDblClickMessage;	
	}
	
	var items = new Array();
	items.push(webclient.menu.createMenuItem("open", _("Open"), false, eventAppointmentListContextMenuOpenMessage));
	items.push(webclient.menu.createMenuItem("print", _("Print"), false, eventListContextMenuPrintMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("categories", _("Categories"), false, eventListContextMenuCategoriesMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("delete", _("Delete"), false, eventAppointmentListContextMenuDeleteMessage));
	this.contextmenu = items;
	this.keys["view"] = KEYS["view"];

	this.initializeView();
	// Used by keycontrol, to switch between views
	this.availableViews = new Array("day", "workweek", "week", "7days", "month", "table");
}

appointmentlistmodule.prototype.initializeView = function(view)
{
	if (view){
		this.selectedview = view;
		webclient.settings.set("folders/entryid_"+this.entryid+"/selected_view", view);
	}
	this.setTitle(this.title, false, true);
	//check for the divelement's height in table view so that scroll bar is perfectly visible. 
	if(this.selectedview != "table")
		this.contentElement = dhtml.addElement(this.element, "div", false, "calendar")
	else
		this.contentElement = dhtml.addElement(this.element, "div");
	// this event allows to select a row while we click on an item in list 
		this.events["row"]["mousedown"] = eventListMouseDownMessage;
	
	var data = new Object();
	data["startdate"] = this.startdate;
	data["duedate"] = this.duedate;
	data["selecteddate"] = this.selectedDate;

	// Set onDrop Event
	if(typeof(dragdrop) != "undefined") {
		switch(this.selectedview){
			case "day":	
			case "7days":
			case "workweek":	
				dragdrop.setOnDropGroup("appointment", this.id, eventAppointmentListDragDropTarget);
				break;
			case "week":
				dragdrop.setOnDropGroup("appointment", this.id, eventCalenderWeekViewDragDropTarget);
				break;
			case "month":		
				dragdrop.setOnDropGroup("appointment", this.id, eventCalenderMonthViewDragDropTarget);
				break;	
			case "table":
				dragdrop.setOnDropGroup("folder", this.id, eventListDragDropTarget);
				break;
		}
	}

	this.viewController.initView(this.id, this.selectedview, this.contentElement, this.events, data);
	
	// Add keycontrol events
	webclient.inputmanager.addObject(this, this.element);
	if (!webclient.inputmanager.hasKeyControlEvent(this, "keyup", eventAppointmentListKeyCtrlChangeView)){
		webclient.inputmanager.bindKeyControlEvent(this, this.keys["view"], "keyup", eventAppointmentListKeyCtrlChangeView);
	}
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["refresh"], "keyup", eventListKeyCtrlRefreshFolder);
	
	// Resize the cells
	this.resize();
}

/**
 * getDragDropTargetCallback
 * 
 * This function returns the correct event function to handle the dragdrop event. 
 * In this case we use the legacy system in the dragdrop widget with the 
 * dragdrop.setOnDropGroup() method in the initializeView method.
 * 
 * @return function|boolean dragdrop even function or false if none
 */
appointmentlistmodule.prototype.getDragDropTargetCallback = function(){
	return false;
}

/**
 * Function which updates the view with the given timestamp. This function is called in
 * the date picker module. 
 */ 
appointmentlistmodule.prototype.changeDays = function(timestamp)
{
	if (timestamp) {
		this.setDays(timestamp);
	}
	
	var data = new Object();
	data["startdate"] = this.startdate;
	data["duedate"] = this.duedate;
	data["selecteddate"] = this.selectedDate; 

	this.viewController.viewObject.setData(data);

	this.list();
	
}

/**
 * Function which calculates the start and enddate.
 */ 
appointmentlistmodule.prototype.setDays = function(timestamp)
{
	var timestampdate = new Date(timestamp);
		
	var date = new Date();
	date.setTimeStamp(timestampdate.getDate(), timestampdate.getMonth()+1, timestampdate.getFullYear()); 

	this.selectedDate = date;
	
	// TODO: check DST
	switch(this.selectedview)
	{
		case "day":
			this.startdate = date.getTime();
			this.duedate = date.getTime() + ONE_DAY;
			break;
		case "workweek":
			var weekday = date.getDay();
			if (weekday==0) weekday = 7;
			this.startdate = date.getTime() - ((weekday - 1) * ONE_DAY);
			this.duedate = date.getTime() + ((6 - weekday) * ONE_DAY);
			break;
		case "7days":
			var weekday = date.getDay();
			if (weekday==0) weekday = 7;
			this.startdate = date.getTime() - ((weekday - 1) * ONE_DAY);
			this.duedate = date.getTime() + ((8 - weekday) * ONE_DAY);
			break;
		case "week":
			var weekday = date.getDay();
			if (weekday==0) weekday = 7;
			this.startdate = date.getTime() - ((weekday - 1) * ONE_DAY);
			this.duedate = date.getTime() + ((8 - weekday) * ONE_DAY);
			break;
		case "month":
			var month_start = new Date(date.getTime());
			month_start.setDate(1);
			var month_end = new Date(date.getTime());
			month_end.setDate(date.getDaysInMonth());
			this.startdate = month_start.getTime() - (6 * ONE_DAY);
			this.duedate = month_end.getTime() + (14 * ONE_DAY);
			break;
		// this will set start and end date for list view 
   		// as there is no restriction on start and end date it is set false
		case "table":
			this.startdate = false;
			this.duedate = false;
			break;	
	}
}

appointmentlistmodule.prototype.messageList = function(action)
{
	if (this.startdate && this.duedate) {
		var restriction = action.getElementsByTagName("restriction")[0];
		restriction = restriction ? dom2array(restriction) : false;

		// Check if incoming records are for selected range view
		if ((this.startdate/1000 == restriction.startdate) && (this.duedate/1000 == restriction.duedate))
			appointmentlistmodule.superclass.messageList.call(this, action);
	} else {
		// listview is selected
		appointmentlistmodule.superclass.messageList.call(this, action);
	}
}

/**
 * Function which sends a request to the server, with the action "list".
 * @param boolean useTimeOut use a time out for the request (@TODO check this parameter is not used anymore)
 * @param boolean noLoader use loader
 * @param boolean storeUniqueIds store selected items' unique ids before reloading listmodule
 */ 
appointmentlistmodule.prototype.list = function(useTimeOut, noLoader, storeUniqueIds)
{
	// As startdate and enddate are set to false for Calendar listview 
	// there is no need to send the restriction to server.
	if(this.selectedview != "table" && this.storeid && this.entryid) {
		var data = new Object();
		data["store"] = this.storeid;
		data["entryid"] = this.entryid;
		data["restriction"] = new Object();
		//this is to set restriction on start and end date if it is false
		data["restriction"]["startdate"] = (this.startdate && this.duedate) ? this.startdate/1000 : 0;
		data["restriction"]["duedate"] =  (this.startdate && this.duedate) ? this.duedate/1000 : 0 ;
		
		webclient.xmlrequest.addData(this, "list", data);
		this.viewController.loadMessage();
	}else{
	//called superclass list method to set restriction for sorting and paging
	appointmentlistmodule.superclass.list.call(this, useTimeOut, noLoader, storeUniqueIds);
	}
}

appointmentlistmodule.prototype.getAppointmentProps = function(starttime, endtime, subject, location, label, busystatus, remindertime, alldayevent)
{
	var props = new Object();
	props["store"] = this.storeid;
	props["parent_entryid"] = this.entryid;
	props["message_class"] = "IPM.Appointment";
	if(subject)
		props["subject"] = subject;

	if(location)
		props["location"] = location;

	if(label)
		props["label"] = label;
	else
		props["label"] = 0;

	// busystatus can be '0' i.e fbFree
	if (busystatus !== undefined && busystatus !== null) {
		props["busystatus"] = busystatus;
	} else {
		props["busystatus"] = fbBusy;
	}

	props["startdate"] = starttime;
	props["commonstart"] = starttime;

	props["duedate"] = endtime;	
	props["commonend"] = endtime;

	props["duration"] = (endtime-starttime)/60;
	props["alldayevent"] = (alldayevent)?1:"false";
	
	props["reminder"] = "false";
	props["reminder_minutes"] = 15;

	// If reminder time is set to zero or any other values then save it in props
	if(typeof remindertime != "undefined" && remindertime >= 0) {
		props["reminder"] = "true";
		props["reminder_minutes"] = remindertime;
	}

	props["reminder_time"] = starttime;
	props["flagdueby"] = starttime - props["reminder_minutes"];

	props["icon_index"] = 1024;
	props["importance"] = 1;
	props["sensitivity"] = 0;
	props["private"] = -1;
	props["responsestatus"] = 0;
	props["meeting"] = 0;

	props["recurring"] = "false";
	props["commonassign"] = 0;
	
	return props;
}

appointmentlistmodule.prototype.createAppointment = function(starttime, endtime, subject, location, label, busystatus, remindertime, alldayevent)
{
	// If selection is from 00:00 to 24:00 then also appointment is allday event
	var startDate = new Date(starttime*1000);
	var endDate = new Date(endtime*1000);
	if ((startDate.strftime("%H:%M:%S") == "00:00:00") &&(endDate.strftime("%H:%M:%S") == "00:00:00")) {
		alldayevent = true;
	}

	// If appointment is an allday event then set busystatus to 'Free'
	busystatus = alldayevent ? fbFree : busystatus;

	this.save(this.getAppointmentProps(starttime, endtime, subject, location, label, busystatus, remindertime, alldayevent));
}

appointmentlistmodule.prototype.destructor = function()
{
	dhtml.removeEvent(document.body, "mouseup", eventListCheckSelectedContextMessage);
	dragdrop.deleteGroup("appointment");
	appointmentlistmodule.superclass.destructor(this);
}

appointmentlistmodule.prototype.changeView = function (type,timestamp)
{
	switch (type){
		case "workweek":
			this.datepicker.changeView("workweek", false);
			this.datepicker.changeSelectedDate(timestamp, true);
			break;
		case "day":
			this.datepicker.changeView("day", false);
			this.datepicker.changeSelectedDate(timestamp, true);
			break;
		case "week":
			this.datepicker.changeView("week", false);
			this.datepicker.changeSelectedDate(timestamp, true);
			break;
		case "table":
			this.datepicker.changeView("table", false);
			break;	
	}
}

// Called when delete button in menu is pressed
appointmentlistmodule.prototype.deleteMessages = function (selecteMessages, softDelete)
{
	// for Shift + Del, passed softDelete value.
	this.deleteAppointments(this.getSelectedMessages(), true, softDelete);
}

// Actually delete a single series or an occurrence of a series
appointmentlistmodule.prototype.deleteAppointment = function(entryid, basedate, elementId)
{
	var selectedElement = dhtml.getElementById(elementId);
	var send = false;

	var appointmentItem = this.itemProps[entryid];

	if(basedate) {
		//occurrence end date
		var end = (selectedElement.end)? selectedElement.end: (selectedElement.getAttribute("end"))? selectedElement.getAttribute("end"): (selectedElement.getAttribute("duedate"))? selectedElement.getAttribute("duedate") : selectedElement.duedate;
		var endtime = new Date(end * 1000);

		/**
		 * if occurrence is later then send confirmation message for cancellation message.

		 * If MR is just saved (not send) then requestsent attribute (MeetingRequestWasSent Property) is set to '0'
		 * If MR was sent to recpients then requestsent attribute (MeetingRequestWasSent Property) is set to '1'
		 * so if request is not sent to recipients then dont ask to update them while saving MR.
		 */
		if(endtime.getTime() > (new Date().getTime()) && selectedElement.requestsent && selectedElement.requestsent == "1") {
			if(selectedElement.meetingrequest && parseInt(selectedElement.meetingrequest, 10) == 1) {
				/**
				 * with FF6 and above we have a problem of focus if a confirm box is called from parentwidow 
				 * when another dialog is opened, so to get the focus back to correct window/dialog we do this call
				 */
				window.focus();
				send = confirm(_("Would you like to send an update to the attendees regarding cancellation of this meeting?"));
				//if user does not want to cancel/delete the meeting,we want like to ignore this action
				if(send == false) return true;
			} else if (selectedElement.meetingrequest && parseInt(selectedElement.meetingrequest, 10) != 0 && !isMeetingCanceled(appointmentItem)) {
				sendConfirmationDeleteAppointmentList(this, selectedElement, basedate);
				return true;
			}
		}

		if(send) {
			// delete occurrence and send cancellation message
			var data = new Array();
			data["store"] = this.storeid;
			data["entryid"] = entryid;
			data["delete"] = true;
			data["exception"] = true;
			data["basedate"] = basedate;
			
			// The cancel call will delete the meeting appointment for us
			webclient.xmlrequest.addData(this, "cancelInvitation", data, webclient.modulePrefix);
			webclient.xmlrequest.sendRequest(true);
		} else {
			//directlly delete passed occurrence.
			var req = new Object;
			req['store'] = this.storeid;
			req['parententryid'] = this.entryid;
			req['entryid'] = entryid;
			req['delete'] = 1;
			req['props'] = new Object;
			req['props']['entryid'] = entryid;
			req['props']['basedate'] = parseInt(basedate, 10);
			webclient.xmlrequest.addData(this, 'save', req, webclient.modulePrefix);
		}
	} else {
		// Recurrence end date
		var end = this.itemProps[entryid].enddate_recurring;
		var endtime = new Date(end * 1000);

		/**
		 * If MR is just saved (not send) then requestsent attribute (MeetingRequestWasSent Property) is set to '0'
		 * If MR was sent to recpients then requestsent attribute (MeetingRequestWasSent Property) is set to '1'
		 * so if request is not sent to recipients then dont ask to update them while saving MR.
		 */
		if (endtime.getTime() > (new Date().getTime()) && selectedElement.requestsent && selectedElement.requestsent == "1") {
			// if recurrence is later then send confirmation message for cancellation message.
			if(selectedElement.meetingrequest && parseInt(selectedElement.meetingrequest, 10) == 1) {
				send = confirm(_("Would you like to send an update to the attendees regarding cancellation of this meeting?"));
				//if user does not want to cancel/delete the meeting,we want like to ignore this action
				if(send == false) return true;
			} else if (selectedElement.meetingrequest && parseInt(selectedElement.meetingrequest, 10) != 0 && !isMeetingCanceled(appointmentItem)) {
				sendConfirmationDeleteAppointmentList(this, selectedElement, false);
				return true;
			}
		}

		if(send) {
			// delete MR and send cancellation message
			var data = new Array();
			data["store"] = this.storeid;
			data["entryid"] = entryid;

			// The cancel call will delete the item for us
			webclient.xmlrequest.addData(this, "cancelInvitation", data);
			webclient.xmlrequest.sendRequest(true);
		} else {
			//directlly delete passed MRs.
			appointmentlistmodule.superclass.deleteMessage.call(this, entryid);
		}
	}
}

// This is the main DELETE function. It is called from the context menu, when pressing 'del' and when pressing the
// delete button in the menu. It asks you any special handling question before proceeding to the actual delete
// Quirk: only prompts for the first entry to be deleted, but deletes all the messages passed in 'messages'
appointmentlistmodule.prototype.deleteAppointments = function(messages, promptoccurrence, softDelete)
{
	var itemEl = false;
	/**
	 * if messages is an array (in case of multiday appointment), we need 
	 * to check that the element on which delete action is fired, is 
	 * exists in DOM or not, if yes then which element exists. ( case, 
	 * when appointment starts on sunday and ends on monday, so we can hv
	 * only one element view at a time.)
	 */
	for(var elIndex = 0; elIndex < messages.length; elIndex++){
		if(dhtml.getElementById(messages[elIndex])){
			itemEl = dhtml.getElementById(messages[elIndex]);
			break;
		}
	}

	if(!itemEl) return;

	var messageClass = false;
	var classNames = itemEl.className.split(" ");
	var entryid = this.entryids[itemEl.id];
	
	var appointmentItem = this.itemProps[entryid];

	//basedate for normal meetings is set as '0' i.e basedate doesn't exist, typecast it to integer so that later is doesn't break any IF statement
	var basedate = itemEl.attributes["basedate"] ? parseInt(itemEl.attributes["basedate"].value, 10) : false;
	var meeting = 0;
	var send = false;
	var deleteMessage = true;

	if(basedate) { // any item with a basedate is recurring
		if(promptoccurrence) {
			// open dialog for question if the series must be deleted or just the occurrence
			webclient.openModalDialog(this, "deleteoccurrence", DIALOG_URL+"entryid="+entryid+"&storeid="+this.storeid+"&task=deleteoccurrence_modal&basedate="+basedate+"&parententryid="+this.entryid+"&meeting="+meeting+"&elementid="+itemEl.id, 300, 200, null, null, {parentModule: this});
		} else {
			// shortcut directly to deleteAppointment if prompting for occurrence/series deletion is off
			this.deleteAppointment(entryid, basedate, itemEl.id);
		}
	}else{
		// do a normal delete
		// calculated end date of appointment to check whether the update to attendees is send or not, for different views of calendar. 
		var end = (itemEl.end)? itemEl.end: itemEl.getAttribute("duedate"); 
		var endtime = new Date(end * 1000);
		if( endtime.getTime()>(new Date().getTime())){
			// IF MR status is set then set its value otherwise set to olResponseNone
			var MRResponseStatusOrganizer = itemEl.meetingrequest ? Number(itemEl.meetingrequest) : olResponseNone;
			/**
			 * If MR is just saved (not send) then requestsent attribute (MeetingRequestWasSent Property) is set to '0'
			 * If MR was sent to recpients then requestsent attribute (MeetingRequestWasSent Property) is set to '1'
			 * so if request is not sent to recipients then dont ask to update them while saving MR.
			 */
			if (MRResponseStatusOrganizer == olResponseOrganized && itemEl.requestsent && itemEl.requestsent == "1") {
				// don't ask for meeting requests which are occuring in past
				send = confirm(_("Would you like to send an update to the attendees regarding changes to this meeting?"));
				deleteMessage = false;
			} else if (MRResponseStatusOrganizer != olResponseNone && MRResponseStatusOrganizer != olResponseOrganized && !isMeetingCanceled(appointmentItem)) {	//its attendee
				sendConfirmationDeleteAppointmentList(this, itemEl, false, softDelete);
				return true;
			}
		}

		if(send) {
			var data = new Array();
			data["store"] = this.storeid;
			data["entryid"] = entryid;

			// The cancel call will delete the item for us	
			webclient.xmlrequest.addData(this, "cancelInvitation", data);
		} else if (deleteMessage) {
			// Use superclass deleteMessages to actually delete the items
			appointmentlistmodule.superclass.deleteMessages.call(this, messages, softDelete);
		}
	}
}


appointmentlistmodule.prototype.handleActionFailure = function(action)
{
	var move = false;

	switch(dhtml.getXMLValue(action, "action_type", "none")){
		case "moveoccurence":
			var errorMessageString = dhtml.getXMLValue(action, "error_message");
			if(errorMessageString != false)
				alert(errorMessageString);
			break;
		case "remindertime":
			var errorMessageString = dhtml.getXMLValue(action, "error_message");
			if(errorMessageString){
				alert(errorMessageString);
				this.list();
			}
			break;
		case "bookresource":
			var name = (dhtml.getXMLValue(action, "displayname"))?dhtml.getXMLValue(action, "displayname"):'(Unknown)';
			/**
			 * Error codes:
			 * 1: No access/permissions.
			 * 2: Resource does not automatically accept meeting requests (disabled atm, mail is sent instead).
			 * 3: Resource declines recurring meeting requests.
			 * 4: Resource declines conflicting meeting requests.
			 */
			switch(Number(dhtml.getXMLValue(action, "errorcode"))){
				case 1:
					alert(_("You marked \"%s\" as a resource. You cannot schedule a meeting with \"%s\" because you do not have the appropiate permissions for that account. Either enter the name as a required or optional attendee or talk to your administrator about giving you permission to schedule \"%s\".").sprintf(name, name, name));
					break;
				case 2:
					alert(_("\"%s\" has declined your meeting because \"%s\" does not automatically accept meeting requests. ").sprintf(name, name));
					break;
				case 3:
					alert(_("\"%s\" has declined your meeting because it is recurring. You must book each meeting separetly with this recurrence.").sprintf(name));
					break;
				case 4:
					alert(_("\"%s\" is already booked for this specified time. You must use another time or find another resource.").sprintf(name));
					break;
				default:
					alert(_("Meeting was not scheduled."));
					break;
			}
			break;
	}
}

/**
 * Function to get the multiday appointments elements reference.
 * @param String elementId String which represents the Elements Id.
 * @return Array result Array of elements which should get selected.
 */
appointmentlistmodule.prototype.getMultiDayItemInObject = function(elementId){
	var elemObjEntryIds = new Array(elementId);
	if(this.viewController.viewObject.itemElementLookup && typeof this.viewController.viewObject.itemElementLookup[this.entryids[elementId]] != "undefined"){
		elemObjEntryIds = this.viewController.viewObject.itemElementLookup[this.entryids[elementId]];
	}
	return elemObjEntryIds;
}

/**
 * Function to open a dialog box to create an appointment or meeting request.
 * @param String elementId String which represents the Elements Id.
 * @return Array result Array of elements which should get selected.
 */
appointmentlistmodule.prototype.openCreateAppointmentDialog = function(type, allDayFlag, element){
	var startEndTimeFromSelection = this.viewController.viewObject.getStartEndTimeOfSelection(true, element);
	var uri = 	DIALOG_URL+
				"storeid=" + this.storeid + 
				"&parententryid=" + this.entryid;
	if(startEndTimeFromSelection){
		var starttime = startEndTimeFromSelection[0];
		var endtime = startEndTimeFromSelection[1];
		
		uri += 	"&startdate=" + starttime +
				"&enddate=" + endtime ;
		uri += (type) ?"&meeting=true":"";
	}

	if(allDayFlag){
		uri += 	"&allDayEvent=true";
	}

	uri += "&task=appointment_standard";
	webclient.openWindow(this, "appointment", uri);
}

function eventAppointmentListDblClickMessage(moduleObject, element, event)
{
	event.stopPropagation();
	var messageClass = false;
	var classNames = element.className.split(" ");
	
	var entryid = moduleObject.entryids[element.id];

	if(parseInt(element.getAttribute("basedate"), 10)) { // any item with a basedate is recurring
		basedate = element.attributes["basedate"].value;

		// please note that this url is also printed, so make it more "interesting" by first set the entryid
		webclient.openWindow(moduleObject, "occurrence", DIALOG_URL+"entryid="+entryid+"&storeid="+moduleObject.storeid+"&task=occurrence_standard&basedate="+basedate+"&parententryid="+moduleObject.entryid, 300, 200);
	} else {
		var uri = DIALOG_URL+"task=appointment_standard&storeid=" + moduleObject.storeid + "&parententryid=" + moduleObject.entryid + "&entryid=" + entryid + "&parententryid=" + moduleObject.entryid;
		// added parameter to increase width of appointment standard dialogbox to accomadate the german translation 
		webclient.openWindow(moduleObject, messageClass, uri, 860, 560);
	}
}

function eventAppointmentListContextMenuOpenMessage(moduleObject, element, event)
{
	element.parentNode.style.display = "none";

	var itemEl = dhtml.getElementById(element.parentNode.elementid);

	var messageClass = false;
	var classNames = itemEl.className.split(" ");
	var entryid = moduleObject.entryids[itemEl.id];

	if(itemEl.getAttribute("basedate")) { // any item with a basedate is recurring
		var basedate = itemEl.attributes["basedate"].value;
		// please note that this url is also printed, so make it more "interesting" by first set the entryid
		webclient.openWindow(moduleObject, "occurrence", DIALOG_URL+"entryid="+entryid+"&storeid="+moduleObject.storeid+"&task=occurrence_standard&basedate="+basedate+"&parententryid="+moduleObject.entryid, 300, 200);
	} else {
		var uri = DIALOG_URL+"task=appointment_standard&storeid=" + moduleObject.storeid + "&parententryid=" + moduleObject.entryid + "&entryid=" + entryid;
		webclient.openWindow(moduleObject, messageClass, uri);
	}

	eventListCheckSelectedContextMessage(moduleObject);
}

function eventAppointmentListContextMenuDeleteMessage(moduleObject, element, event)
{
	element.parentNode.style.display = "none";

	moduleObject.deleteAppointments(moduleObject.getSelectedMessages(element.parentNode.elementid), true); // don't prompt for occurrence

	eventListCheckSelectedContextMessage(moduleObject);
}
/**
 * Keycontrol function which switches to next/previous view
 */
function eventAppointmentListKeyCtrlChangeView(moduleObject, element, event)
{
	var newView = false;

	// Select previous view
	if (event.keyCombination == this.keys["view"]["prev"]){
		newView = array_prev(moduleObject.selectedview, moduleObject.availableViews);
	} else if (event.keyCombination == this.keys["view"]["next"]){
		// Select next view
		newView = array_next(moduleObject.selectedview, moduleObject.availableViews);
	}

	if (newView){
		moduleObject.datepicker.changeView(newView, true);
	}
}

function confirmAppointMoved(moduleObject, currentDay, targetDay)
{
	if(currentDay != targetDay){
		if(!confirm(_("You changed the date of this occurrence. If you want to change the date of all occurrences, you must first open the appointment series. Do you want to change only this occurrence?"))){
			/**
			 * Here position of the occurrence is changed,
			 * so replace it with original call list function of module.
			 */
			moduleObject.list();
			return false;
		}
	}
	return true;
}

/**
 * Event function for showing context menu for creating appointments.
 * @param object moduleObject Contains all the properties of a module object.
 * @param dom_element element The dom element's reference on which the event gets fired.
 * @param event event Event name
 */
function eventShowCreateAppointmentContextMenu(moduleObject, element, event){
	var items = new Array();
	var elem = element;
	items.push(webclient.menu.createMenuItem("appointment", _("Create Appointment"), _("Create Appointment"), function(){eventCalendarContextMenuClickCreateAppointment(moduleObject, elem, event)}));
	items.push(webclient.menu.createMenuItem("meetingrequest", _("Create Meetingrequest"), _("Create Meetingrequest"), function(){eventCalendarContextMenuClickCreateAppointment(moduleObject, elem, event, "meeting")}));
	webclient.menu.buildContextMenu(moduleObject, element.id, items, event.clientX, event.clientY);
}

/**
 * event for opening a dialog box for creating of appointments
 * @param object moduleObject Contains all the properties of a module object.
 * @param dom_element element The dom element's reference on which the event gets fired.
 * @param event event Event name
 * @param type type Event whether meeting request or empty for appointment
 */
function eventCalendarContextMenuClickCreateAppointment(moduleObject, element, event, type){
	if(dhtml.getElementById("contextmenu")){
		dhtml.getElementById("contextmenu").style.display = "none";
	}
	moduleObject.openCreateAppointmentDialog(type, true, element);
}


/**
 * Function which popups option to attendee when he/she deletes any meeting item.
 *@param object moduleObject module object
 *@param element html element of meeting
 *@param timestamp basedate if item is an occurrence then basedate is passed
 *@param boolean softdelete if shift+del
 */
function sendConfirmationDeleteAppointmentList(moduleObject, element, basedate, softdelete)
{
	var windowData = new Array();
	windowData["module"] = moduleObject;
	windowData["confirmDelete"] = true;
	windowData["elementId"] = element.id;
	windowData["subject"] = "";

	if(basedate)
		windowData["basedate"] = basedate;

	if (typeof element.subject != "undefined") {
		windowData["subject"] = element.subject;
	} else {
		if (moduleObject.itemProps[moduleObject.entryids[element.id]])
			windowData["subject"] = moduleObject.itemProps[moduleObject.entryids[element.id]].subject;
	}

	if (element.id.indexOf("_") > 0)
		windowData["entryid"] = element.id.substring(0, element.id.indexOf('_'));
	else
		windowData["entryid"] = element.id;

	if (typeof softDelete != "undefined")
		windowData["softDelete"] = softDelete;

	webclient.openModalDialog(-1, 'sendMRMailConfirmation', DIALOG_URL+'task=sendMRMailConfirmation_modal', 350, 200, appointmentListSendMRMailConfirmation_callback, moduleObject, windowData);
}
/**
 * Callback function from sendMRConfirmation dialog
 * this function decides whether to send response to organizer that attendee has deleted item.
 * 
 *@param object moduleObject module object
 *@param boolean sendResponse true if attendee wants to send response on deletion else false
 *@param integer basedate basedate if it an occurrence
 *@param object callBackData extra information about the deleted item like 
 */
function appointmentListSendMRMailConfirmation_callback(moduleObject, sendResponse, basedate, callBackData)
{
	var entryid = callBackData.elementId;

	if (entryid.indexOf('_') > 0) entryid = entryid.substring(0, entryid.indexOf('_'));

	var req = new Object;
	req['store'] = moduleObject.storeid;
	req['parententryid'] = moduleObject.entryid;
	req['props'] = new Object;
	req['props']['entryid'] = entryid;

	if (basedate)
		req['props']['basedate'] = parseInt(basedate, 10);

	if (sendResponse) {
		webclient.xmlrequest.addData(moduleObject, 'declineMeeting', req, webclient.modulePrefix);
		webclient.xmlrequest.sendRequest(true);
	} else {
		if (basedate) {
			req['delete'] = 1;
			webclient.xmlrequest.addData(moduleObject, 'save', req, webclient.modulePrefix);
			webclient.xmlrequest.sendRequest(true);
		} else {
			// Use superclass deleteMessages to actually delete the items
			appointmentlistmodule.superclass.deleteMessages.call(moduleObject, new Array(callBackData.elementId));
		}
	}
}