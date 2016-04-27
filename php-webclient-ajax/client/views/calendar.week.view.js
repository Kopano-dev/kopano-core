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
 * --Calendar Week View--
 * @type	View
 * @classDescription	This view can be used for appointement
 * list module to display the calendar items
 * 
 * +--------------------------+
 * | mon        | thu         |
 * |            |             |
 * |------------|-------------|
 * | tue        | fri         |
 * |            |             | 
 * |------------|-------------|
 * | wed        | sat         |
 * |            |-------------|
 * |            | sun         |
 * +--------------------------+
 * 
 * DEPENDS ON:
 * |------> view.js
 * |----+-> *listmodule.js
 * |    |----> listmodule.js
 */

CalendarWeekView.prototype = new View;
CalendarWeekView.prototype.constructor = CalendarWeekView;
CalendarWeekView.superclass = View.prototype;

// PUBLIC
/**
 * @constructor This view can be used for appointement list module to display the calendar items
 * @param {Int} moduleID
 * @param {HtmlElement} element
 * @param {Object} events
 * @param {XmlElement} data
 */
function CalendarWeekView(moduleID, element, events, data)
{
	this.element = element;
	this.moduleID = moduleID;
	this.events = events;
	this.data = data;
	
	this.setData(data);
	
	this.dayElements = new Array();
}

CalendarWeekView.prototype.setData = function(data)
{
	this.startdate = data["startdate"];
	this.duedate = data["duedate"];
	this.selecteddate = data["selecteddate"];
}

/**
 * Function will render the view and execute this.resizeView when done
 */
CalendarWeekView.prototype.initView = function()
{
	// clear old elements
	dhtml.deleteAllChildren(this.element);
	
	// add day elments
	this.dayElements[0] = new Object();
	this.dayElements[0].element = dhtml.addElement(this.element,"div","week_view day_monday");
	this.dayElements[3] = new Object();
	this.dayElements[3].element = dhtml.addElement(this.element,"div","week_view day_thursday");
	this.dayElements[1] = new Object();
	this.dayElements[1].element = dhtml.addElement(this.element,"div","week_view day_tuesday");
	this.dayElements[4] = new Object();
	this.dayElements[4].element = dhtml.addElement(this.element,"div","week_view day_friday");
	this.dayElements[2] = new Object();
	this.dayElements[2].element = dhtml.addElement(this.element,"div","week_view day_wednesday");
	this.dayElements[5] = new Object();
	this.dayElements[5].element = dhtml.addElement(this.element,"div","week_view day_saturday");
	this.dayElements[6] = new Object();
	this.dayElements[6].element = dhtml.addElement(this.element,"div","week_view day_sunday");
	
	// add header info
	var tmpStart = new Date(this.selecteddate).getStartDateOfWeek();
	tmpStart.clearTime();
	for(var i=0;i < this.dayElements.length;i++){
		this.dayElements[i].element.id = "day_"+this.moduleID+"_"+(tmpStart.getTime()/1000);
		this.dayElements[i].element.setAttribute("unixtime",(tmpStart.getTime()/1000));
		dragdrop.addTarget(this.element,this.dayElements[i].element,"appointment",true);
		if(i == 6){
			var dayTitle = dhtml.addElement(this.dayElements[i].element,"span","day_header","",DAYS[0]+" "+tmpStart.getDate()+" "+MONTHS[tmpStart.getMonth()]);
		} else {
			var dayTitle = dhtml.addElement(this.dayElements[i].element,"span","day_header","",DAYS[i+1]+" "+tmpStart.getDate()+" "+MONTHS[tmpStart.getMonth()]);
		}
		dhtml.addEvent(this.moduleID,dayTitle,"click",eventCalenderWeekViewChangeViewToDay);
		
		//select the current day as highlighted
		var todayCount = (new Date().getDay()== 0) ? 7 : new Date().getDay();
		if(todayCount-1 == i  && tmpStart.isSameDay(new Date()) ){
			dhtml.addClassName(this.dayElements[i].element, "selectedToday");
		}

		// attach event for context menu
		dhtml.addEvent(this.moduleID,this.dayElements[i].element,"contextmenu",eventShowCreateAppointmentContextMenu);
		dhtml.addEvent(this.moduleID,this.dayElements[i].element,"dblclick",eventCalendarContextMenuClickCreateAppointment);

		tmpStart = tmpStart.add(Date.DAY, 1);
		// This fixes the DST where it goes from 0:00 to 01:00.
		tmpStart = tmpStart.add(Date.HOUR, 12);
		tmpStart.clearTime();
	}

	// week picker
	var extraElement = dhtml.addElement("","span");
	
	// title string
	var startDateObj = new Date(this.startdate);
	var dueDateObj = new Date(this.duedate-ONE_DAY);			
	var titleString = startDateObj.getDate()+" "+MONTHS[startDateObj.getMonth()]+" - "+dueDateObj.getDate()+" "+MONTHS[dueDateObj.getMonth()];
	dhtml.addElement(extraElement,"span","","",titleString);	
	
	// previous button 
	var prevButton = dhtml.addElement(extraElement,"span","prev_button","",NBSP);
	dhtml.addEvent(this.moduleID,prevButton,"click",eventDayViewClickPrev);
	prevButton.title = _("Previous week");
	
	// next button
	var nextButton =dhtml.addElement(extraElement,"span","next_button","",NBSP);
	dhtml.addEvent(this.moduleID,nextButton,"click",eventDayViewClickNext);
	nextButton.title = _("Next week");

	// add keyboard event
	var module = webclient.getModule(this.moduleID);
	webclient.inputmanager.addObject(module, module.element);
	
	//Dont bind event if it is already binded earlier.
	if (!webclient.inputmanager.hasEvent(module, "keydown", eventCalendarWeekViewKeyboard)) {
		webclient.inputmanager.bindEvent(module, "keydown", eventCalendarWeekViewKeyboard);
	}
	module.setExtraTitle(extraElement);

	this.resizeView();
}

/**
 * Function will resize all elements in the view
 */
CalendarWeekView.prototype.resizeView = function()
{
	for(var i in this.dayElements){
		if(i == "5" || i == "6"){
			this.dayElements[i].element.style.height = (this.element.offsetHeight/3)/2 + "px";
		} else {
			this.dayElements[i].element.style.height = (this.element.offsetHeight/3) + "px";	
		}
		this.dayElements[i].element.style.width = (this.element.offsetWidth/2) - 1 + "px";
	}
	dragdrop.updateTargets("appointment");
	this.checkForMoreItems();
}

/**
 * Function will adds items to the view
 * @param {Object} items Object with items
 * @param {Array} properties property list
 * @param {Object} action the action tag
 * @return {Array} list of entryids
 */
CalendarWeekView.prototype.execute = function(items, properties, action)
{
	var entryids = false;

	for(var i=0;i<items.length;i++){
		if (!entryids) {
			entryids = new Object();
		}
		var item = this.createItem(items[i]);
		entryids[item["id"]]= item["entryid"];
	}
	this.resizeView();
	dragdrop.updateTargets("appointment");
	
	return entryids;
}

// PRIVATE
/**
 * Function will check if there are items outside the view that
 * have to be hidden
 */
CalendarWeekView.prototype.checkForMoreItems = function()
{
	for(var i in this.dayElements){
		var items = dhtml.getElementsByClassNameInElement(this.dayElements[i].element,"event","div");
		var dayHeight = this.dayElements[i].element.offsetHeight;
		var	dayCurrentHeight = (items.length*15)+15;
		var maxItems = Math.floor((dayHeight-18)/15)-1;
		
		// remove more_items icon		
		var moreItem = dhtml.getElementsByClassNameInElement(this.dayElements[i].element,"more_items","div")[0];
		if(moreItem){
			dhtml.deleteElement(moreItem);
		}

		// hide/show items
		var moreItemCount = 0;
		for(var j=0; j<items.length; j++){
			if(j < maxItems){
				items[j].style.display = "block";
			} else{
				items[j].style.display = "none";
				moreItemCount++;
			}
		}

		// show more_items icon
		if(items.length > maxItems && items.length > 0){
			var moreItem = dhtml.addElement(this.dayElements[i].element,"div","more_items");
			var unixTime = this.dayElements[i].element.getAttribute("id").split("_")[2];
			moreItem.setAttribute("unixtime",unixTime);
			if (moreItemCount==1){
				moreItem.title = _("There is one more item");
			} else {
				moreItem.title = _("There are %s more items").sprintf(moreItemCount);
			}

			dhtml.addEvent(this.moduleID,moreItem,"click",eventChangeViewToDay);
		}
		
	}
}

CalendarWeekView.prototype.addItem = function()
{
	return false;
}

/**
 * Function will add one "item" tot the view or replace "element" 
 * @param {Object} item
 * @param {HtmlElement} element changed item
 * @return {Object} entry item for entryID list
 */
CalendarWeekView.prototype.createItem = function(item, element)
{
	var entry = Object();
	// get properties
	var startDate = new Date(item.getElementsByTagName("startdate")[0].getAttribute("unixtime")*1000);
	var dueDate = new Date(item.getElementsByTagName("duedate")[0].getAttribute("unixtime")*1000);
	var unixTimeStart = item.getElementsByTagName("startdate")[0].getAttribute("unixtime");
	var unixTimeDue = item.getElementsByTagName("duedate")[0].getAttribute("unixtime");	
	var subject = dhtml.getXMLValue(item, "subject", NBSP);
	var location = dhtml.getXMLValue(item, "location", NBSP);
	var duration = dhtml.getXMLValue(item, "duration", 0);
	var parent_entryid = dhtml.getXMLValue(item, "parent_entryid", ""); 
	var entryid = dhtml.getXMLValue(item, "entryid", "");
	var label =  dhtml.getXMLValue(item, "label", "");
	var recurring = dhtml.getXMLValue(item, "recurring", "");
	var basedate = item.getElementsByTagName("basedate")[0]?item.getElementsByTagName("basedate")[0].getAttribute("unixtime"):false;	
	var exception = item.getElementsByTagName("exception")[0];
	var privateAppointment = dhtml.getXMLValue(item, "private", 0);
	var privateSensitivity = dhtml.getXMLValue(item, "sensitivity", 0);

	var AppointmentElement;
	
	var today = new Date(startDate);
	today.clearTime();
	var endday = new Date(dueDate);
	endday.clearTime();
	
	var alldayevent = today.getTime() != endday.getTime();

	while(today <= dueDate){
		// check if there is a zero duration appointment.
		if(today < dueDate || (dueDate - startDate == 0)){
			var unixTimeId = startDate.getTime()/1000;
			var parent = dhtml.getElementById("day_"+this.moduleID+"_"+today.getTime()/1000);
			if(parent){
				if(element){
					var elemId = element.id;
					dhtml.deleteElement(element);
					// for multiday appointments, check if there is any other element remaining with same ID, then delete it
					if(dhtml.getElementById(elemId)) dhtml.deleteElement(dhtml.getElementById(elemId));
				}
				AppointmentElement = dhtml.addElement(parent,"div","event");
				AppointmentElement.is_disabled = dhtml.getTextNode(item.getElementsByTagName("disabled_item")[0],0) != 0; // for private items, no events may be added to this item

				// set some properties
				AppointmentElement.id = entryid+"_"+unixTimeId;
				AppointmentElement.startdate = startDate.getTime()/1000;
				AppointmentElement.duedate = dueDate.getTime()/1000;
				
				// allday event
				if(alldayevent){
					dhtml.addClassName(AppointmentElement, "event_day_item");
				} else {
					dhtml.addElement(AppointmentElement,"span","event_time","",startDate.toTime()+" "+dueDate.toTime());
				}
		
				// private item
				if(privateAppointment == "1") {
					dhtml.addElement(AppointmentElement, "span", "private", false, NBSP);
				} else if(privateSensitivity == "2") {
					dhtml.addElement(AppointmentElement, "span", "private", false, NBSP);
				}
		
				
				// Recurring items
				if(recurring.length > 0 && basedate.length > 0) {
					AppointmentElement.setAttribute("basedate", basedate);
					if(exception && exception.firstChild) {
						dhtml.addElement(AppointmentElement, "span", "recurring_exception", false, NBSP);			
					} else if(recurring == "1") {
						dhtml.addElement(AppointmentElement, "span", "recurring", false, NBSP);
					}
				}
				
				// add subject
				if(location.length > 0){
					dhtml.addElement(AppointmentElement,"span","event_subject","",subject+" ("+location+")");
				} else {
					dhtml.addElement(AppointmentElement,"span","event_subject","",subject);
				}			

				// MeetingRequestWasSent
				if(item.getElementsByTagName("requestsent") && item.getElementsByTagName("requestsent")[0]) {
					AppointmentElement.requestsent = dhtml.getTextNode(item.getElementsByTagName("requestsent")[0]);;
				}
				// meeting
				var meeting = item.getElementsByTagName("meeting")[0];
				if(meeting && meeting.firstChild) {
					var responseStatus = dhtml.getTextNode(item.getElementsByTagName("responsestatus")[0], 0);
					AppointmentElement.meetingrequest = responseStatus; // store responseStatus in DOM tree
					switch(meeting.firstChild.nodeValue)
					{
						case "1":
						case "3":
						case "5":
							dhtml.addElement(AppointmentElement, "span", "meetingrequest", false, NBSP);
							break;
					}
				}

				// Appointment labels
				switch(label){
					case "1": dhtml.addClassName(AppointmentElement,"label_important"); break;
					case "2": dhtml.addClassName(AppointmentElement,"label_work"); break;
					case "3": dhtml.addClassName(AppointmentElement,"label_personal"); break;
					case "4": dhtml.addClassName(AppointmentElement,"label_holiday"); break;
					case "5": dhtml.addClassName(AppointmentElement,"label_required"); break;
					case "6": dhtml.addClassName(AppointmentElement,"label_travel_required"); break;
					case "7": dhtml.addClassName(AppointmentElement,"label_prepare_required"); break;
					case "8": dhtml.addClassName(AppointmentElement,"label_birthday"); break;
					case "9": dhtml.addClassName(AppointmentElement,"label_special_date"); break;
					case "10": dhtml.addClassName(AppointmentElement,"label_phone_interview"); break;
				}

				if (!AppointmentElement.is_disabled){
				
					// add events
					if (this.events && this.events["row"]){
						dhtml.setEvents(this.moduleID, AppointmentElement, this.events["row"]);
					}
					
					// single click   TODO: check DST
					dragdrop.addDraggable(AppointmentElement,"appointment",true,APPOINTMENT_NOT_RESIZABLE);
				}
				
				entry["id"] = entryid+"_"+unixTimeId;
				entry["entryid"] = entryid;
			}
		}
		
		// add one_day for more day event
		today = today.add(Date.DAY, 1);
		// This fixes the DST where it goes from 0:00 to 01:00.
		today = today.add(Date.HOUR, 12);
		today.clearTime();
	}
	return entry;
}

/**
 * Function will update a item
 * @param {Object} element
 * @param {Object} item
 * @param {Object} properties
 * @return {Object} entry item for entryID list
 */
CalendarWeekView.prototype.updateItem = function(element, item, properties)
{
	if(item) {
		dhtml.deleteAllChildren(element);
		var result = this.createItem(item,element);
		this.checkForMoreItems()
		return result;
	}
	return undefined;
}

/**
 * Function will show Loading text in view
 */
CalendarWeekView.prototype.loadMessage = function()
{
	dhtml.removeEvents(this.element);
	dhtml.deleteAllChildren(this.element);

	this.element.innerHTML = "<center>" + _("Loading") + "...</center>";
	document.body.style.cursor = "wait";
}

/**
 * Function will delete load text in view
 */
CalendarWeekView.prototype.deleteLoadMessage = function()
{
	dhtml.deleteAllChildren(this.element);
	this.initView();
	document.body.style.cursor = "default";
}

CalendarWeekView.prototype.getStartEndTimeOfSelection = function(flag, element){
	if(typeof element != 'undefined') {
		var starttime = element.getAttribute("unixtime");
		return [starttime, starttime];
	} else {
		var hourCount = webclient.settings.get("calendar/workdaystart",9 * 60) / 60;
		var startTime = addHoursToUnixTimeStamp(this.selecteddate.getTime() / 1000, hourCount);

		return [startTime, startTime + 1800];
	}
}

// EVENTS
/**
 * Function will change the view to day view
 * @param {Object} moduleObject
 * @param {Object} element
 * @param {Object} event
 */
function eventCalenderWeekViewChangeViewToDay(moduleObject, element, event)
{
	moduleObject.changeView("day",element.parentNode.getAttribute("id").split("_")[2]*1000);
}

/**
 * Function will modify that item in the view and server(php)
 * @param {Object} moduleObject
 * @param {Object} targetElement
 * @param {Object} element
 * @param {Object} event
 */
function eventCalenderWeekViewDragDropTarget(moduleObject, targetElement, element, event)
{	
	var draggableStartdate = element.startdate;
	var draggableDuedate = element.duedate;
	var draggableBasedate = element.getAttribute("basedate");
	var draggableEntryID = element.getAttribute("id").split("_")[0];
	var targetDate = new Date(targetElement.getAttribute("id").split("_")[2]*1000);
	var duration = draggableDuedate-draggableStartdate;		

	var newDraggableStartdate = new Date(draggableStartdate*1000);
	newDraggableStartdate.setDate(targetDate.getDate());
	newDraggableStartdate.setMonth(targetDate.getMonth());
	newDraggableStartdate.setFullYear(targetDate.getFullYear());
	newDraggableStartdate = newDraggableStartdate/1000;//convert to unixtimestamp
	
	var newDraggableDuedate = newDraggableStartdate+duration;
	
	// move item
	targetElement.appendChild(element);
	element.setAttribute("resizable",APPOINTMENT_NOT_RESIZABLE);// TODO fixed this in dragdrop.js
																															// this workaround is not working in ie6
	// send update to server
	var props = new Object();
	props["entryid"] = draggableEntryID;
	props["startdate"] = newDraggableStartdate;
	props["duedate"] = newDraggableDuedate;
	props["commonstart"] = newDraggableStartdate;
	props["commonend"] = newDraggableDuedate;

	// Set flagdueby property to show reminder on correcttime.
	if(props["startdate"] && props["entryid"] && moduleObject.itemProps[props["entryid"]])
		props["flagdueby"] = props["startdate"] - (moduleObject.itemProps[props["entryid"]].reminder_minutes*60);

	if(draggableBasedate){
		props["basedate"] = draggableBasedate;
		var drag = true;
	}
	props["duration"] = duration/60; 

	var send = false;
	var moveOccAllowFlag = true;
	if (element.meetingrequest && element.meetingrequest == olResponseOrganized) {
		// Check whether there are any recepients in MR or not.
		if( newDraggableDuedate>(new Date().getTime()/1000) && element.requestsent && element.requestsent == "1") {
			send = confirm(_("Would you like to send an update to the attendees regarding changes to this meeting?"));
		}
	}

	if(draggableBasedate){
		moveOccAllowFlag = confirmAppointMoved(moduleObject, draggableBasedate, targetElement.getAttribute("id").split("_")[2]);
	}
	if(moveOccAllowFlag){
		moduleObject.save(props, send);
	}
}

function eventCalendarWeekViewKeyboard(moduleObject, element, event)
{
	var viewObject = moduleObject.viewController.viewObject;
	switch (event.keyCode){
		case 37: // navigations keys
			eventDayViewClickPrev(moduleObject);
			break;
		case 39: // navigations keys
			eventDayViewClickNext(moduleObject);
			break;
	}
}