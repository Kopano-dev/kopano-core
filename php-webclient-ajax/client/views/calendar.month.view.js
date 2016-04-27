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
 * --Calendar Month View--
 * @type View
 * @classDescription	This view can be used for appointement
 * list module to display the calendar items
 * 
 * +----+-----+-----+-----+-----+-----+-----+-----+
 * |    | mon | tue | wed | thu | fri | sat | sun |
 * +----+-----+-----+-----+-----+-----+-----+-----+
 * | 48 |     |     |     |     |     |     |     |
 * +----+-----+-----+-----+-----+-----+-----+-----+
 * | 49 |     |     |     |     |     |     |     |
 * +----+-----+-----+-----+-----+-----+-----+-----+
 * | 50 |     |     |     |     |     |     |     |
 * +----+-----+-----+-----+-----+-----+-----+-----+
 * | 51 |     |     |     |     |     |     |     |
 * +----+-----+-----+-----+-----+-----+-----+-----+
 * | 52 |     |     |     |     |     |     |     |
 * +----+-----+-----+-----+-----+-----+-----+-----+
 * 
 * DEPENDS ON:
 * |------> dhtml.js
 * |------> view.js
 * |----+-> *listmodule.js
 * |    |----> listmodule.js
 */
CalendarMonthView.prototype = new View;
CalendarMonthView.prototype.constructor = CalendarMonthView;
CalendarMonthView.superclass = View.prototype;

function CalendarMonthView(moduleID, element, events, data)
{
	this.element = element;
	
	this.moduleID = moduleID;
	this.events = events;
	this.data = data;

	this.setData(data);	
	
	this.days = new Array();

	for(var i = new Date(this.startdate); i.getTime() < this.duedate; i=i.add(Date.DAY, 1))
	{
		// This fixes the DST where it goes from 0:00 to 01:00.
		i = i.add(Date.HOUR, 12);
		i.clearTime();

		this.days.push(i.getTime());
	}
}

CalendarMonthView.prototype.setData= function(data)
{
	this.startdate = data["startdate"];
	this.duedate = data["duedate"];
	this.selecteddate = data["selecteddate"];
}

CalendarMonthView.prototype.initView = function()
{
	this.folder_entryid = webclient.getModule(this.moduleID).entryid;

	var weekElement, dayElement, spanElement,currentDay;

	// extratitle
	var extraElement = dhtml.addElement("","span");

	var extraTitleString = MONTHS[this.selecteddate.getMonth()]+" "+this.selecteddate.getFullYear();
	dhtml.addElement(extraElement,"span","",""," "+extraTitleString+" ");

	// previous button 
	var prevButton = dhtml.addElement(extraElement,"span","prev_button","",NBSP);
	dhtml.addEvent(this.moduleID,prevButton,"click",eventMonthViewClickPrev);
	prevButton.title = MONTHS[(this.selecteddate.getMonth()==0)?11:this.selecteddate.getMonth()-1];

	// next button
	var nextButton =dhtml.addElement(extraElement,"span","next_button","",NBSP);
	dhtml.addEvent(this.moduleID,nextButton,"click",eventMonthViewClickNext);	
	nextButton.title = MONTHS[(this.selecteddate.getMonth()==11)?0:this.selecteddate.getMonth()+1];
	
	// add keyboard event
	var module = webclient.getModule(this.moduleID);
	webclient.inputmanager.addObject(module, module.element);
	
	//Dont bind event if it is already binded earlier.
	if (!webclient.inputmanager.hasEvent(module, "keydown", eventCalendarMonthViewKeyboard)) {
		webclient.inputmanager.bindEvent(module, "keydown", eventCalendarMonthViewKeyboard);
	}
	module.setExtraTitle(extraElement);
	
	// get the date of the first day in the view
	firstDayOfTheView = new Date(this.selecteddate);
	firstDayOfTheView.setDate(1);
	firstDayOfTheView = firstDayOfTheView.getStartDateOfWeek();
	
	currentDay = firstDayOfTheView;
	currentDay.clearTime();

	
	this.tableElement = dhtml.addElement(false, "div","month","");
	
	weekElement = dhtml.addElement(this.tableElement, "div", "month_header");
	//week# col
	dayElement = dhtml.addElement(weekElement, "div","month_header_week_nr");
	dhtml.addElement(dayElement, "span","","",NBSP);	
	//end week# col	
	
	// add week header
	// FIXME: before we can have an user setting for the startday, we need support in the displaying of the items, so for now it is fixed on monday
	// var startDay = webclient.settings.get("global/calendar/weekstart",1);
	var startDay = 1;
	for(var i=startDay; i<7; i++){
		dayElement = dhtml.addElement(weekElement, "div", "month_header_day");
		dhtml.addElement(dayElement, "span", "","", DAYS_SHORT[i]);
	}
	for(var i=0;i<startDay;i++){		
		dayElement = dhtml.addElement(weekElement, "div", "month_header_day");
		dhtml.addElement(dayElement, "span", "","", DAYS_SHORT[i]);
	}

	// begin month view
	var today = new Date();

	for(var i=0;i<6;i++){
		//week row
		weekElement = dhtml.addElement(this.tableElement, "div", "week");
		
		//week# col
		dayElement = dhtml.addElement(weekElement, "div","month_week_nr");
		dayElement.setAttribute("unixtime",(currentDay.getTime()/1000));
		spanElement = dhtml.addElement(dayElement, "span", "","",currentDay.getWeekNumber());
		dhtml.addEvent(this.moduleID,dayElement,"click",eventChangeViewToWorkWeek);
		//end week# col
		
		for (var j=0;j<7;j++){
			var dayId = this.folder_entryid+"_"+(currentDay.getTime()/1000);

			if(currentDay.getMonth()==this.selecteddate.getMonth()){
				if(currentDay.isSameDay(new Date())){
					dayElement = dhtml.addElement(weekElement, "div", "month_day today",dayId);
				}
				else{
					dayElement = dhtml.addElement(weekElement, "div", "month_day",dayId);
				}
			}
			else{
				dayElement = dhtml.addElement(weekElement, "div", "month_day month_other",dayId);
			}
			dayElement.setAttribute("unixtime",(currentDay.getTime()/1000));
			dragdrop.addTarget(this.tableElement,dayElement,"appointment",true);
			
			//day number	
			spanElement = dhtml.addElement(dayElement, "span", "day_number","", currentDay.getDate());
			spanElement.setAttribute("unixtime",(currentDay.getTime()/1000));
			dhtml.addEvent(this.moduleID,spanElement,"click",eventChangeViewToDay);

			dhtml.addEvent(this.moduleID,dayElement,"contextmenu",eventShowCreateAppointmentContextMenu);
			dhtml.addEvent(this.moduleID,dayElement,"dblclick",eventCalendarContextMenuClickCreateAppointment);
			//end day number
			
			currentDay = currentDay.add(Date.DAY, 1);
			// This fixes the DST where it goes from 0:00 to 01:00.
			currentDay = currentDay.add(Date.HOUR, 12);
			currentDay.clearTime();
		}
	}
}

CalendarMonthView.prototype.getStartEndTimeOfSelection = function(flag, element){
	if(typeof element != 'undefined') {
		var starttime = element.getAttribute("unixtime");
		return [starttime, starttime];
	} else {
		var hourCount = webclient.settings.get("calendar/workdaystart",9 * 60) / 60;
		var startTime = addHoursToUnixTimeStamp(this.selecteddate.getTime() / 1000, hourCount);

		return [startTime, startTime + 1800];
	}
}

CalendarMonthView.prototype.resizeView = function()
{
	if(this.tableElement){
		var fixedHeight = 20;
		var fixedWidth = 20;
		var flexHeight = Math.floor((this.tableElement.offsetHeight-fixedHeight)/6);
		var flexWidth = Math.floor((this.tableElement.offsetWidth-fixedWidth)/7);
		var divElement;
		
		divElement = dhtml.getElementsByClassNameInElement(this.tableElement,"month_header","div");
		divElement[0].style.height = fixedHeight+"px";
		divElement[0].style.width = this.tableElement.offsetWidth+"px";
		
		divElement = dhtml.getElementsByClassNameInElement(this.tableElement,"month_header_week_nr","div");
		divElement[0].style.height = fixedHeight+"px";
		divElement[0].style.width = fixedWidth+"px";
		
		divElement = dhtml.getElementsByClassNameInElement(this.tableElement,"month_header_day","div");
		for(var i=0;i<divElement.length-1;i++){
			divElement[i].style.height = fixedHeight+"px";
			divElement[i].style.width = flexWidth+"px";
		}
		divElement[divElement.length-1].style.height = fixedHeight+"px";
		divElement[divElement.length-1].style.width = (this.tableElement.offsetWidth-(divElement[divElement.length-2].offsetLeft+flexWidth)-2)+"px";

		divElement = dhtml.getElementsByClassNameInElement(this.tableElement,"week","div");
		for(var i=0;i<divElement.length;i++){
			divElement[i].style.height = flexHeight+"px";
			divElement[i].style.width = this.tableElement.offsetWidth+"px";

			dayElement = dhtml.getElementsByClassNameInElement(divElement[i],"month_day","div");
			for(var j=0;j<dayElement.length-1;j++){
				dayElement[j].style.height = flexHeight+"px";
				dayElement[j].style.width = flexWidth+"px";
			}
			dayElement[dayElement.length-1].style.height = flexHeight+"px";
			dayElement[dayElement.length-1].style.width = (flexWidth-8)+"px";
//			dayElement[dayElement.length-1].style.width = (this.tableElement.offsetWidth-(dayElement[dayElement.length-2].offsetLeft+flexWidth)-2)+"px";
		}

		divElement = dhtml.getElementsByClassNameInElement(this.tableElement,"month_week_nr","div");
		for(var i=0;i<divElement.length;i++){
			divElement[i].style.height = flexHeight+"px";
			divElement[i].style.width = fixedWidth+"px";
		}
		
	}
	dragdrop.updateTargets("appointment");
	this.checkForMoreItems();
}

CalendarMonthView.prototype.checkForMoreItems = function()
{
	var dayElement = dhtml.getElementsByClassNameInElement(this.element,"month_day","div");
	for(var i=0;i < dayElement.length;i++){
		items = dhtml.getElementsByClassNameInElement(dayElement[i],"event_item","div");
		dayHeight = dayElement[i].offsetHeight;
		dayCurrentHeight = (items.length*15)+18;

		// remove more_items icon		
		var moreItem = dhtml.getElementsByClassNameInElement(dayElement[i],"more_items","div")[0];
		if(moreItem){
			dhtml.deleteElement(moreItem);
		}

		// hide/show items
		var maxItems = Math.floor((dayHeight-18)/15)-1;
		var moreItemCount = 0;
		for(var j=0;j<items.length;j++){
			if(j < maxItems){
				items[j].style.display = "block";
			}
			else{
				items[j].style.display = "none";
				moreItemCount++;
			}
		}

		// show more_items icon
		if(items.length > maxItems && items.length > 0){
			var moreItem = dhtml.addElement(dayElement[i],"div","more_items");
			moreItem.setAttribute("unixtime",dayElement[i].getAttribute("unixtime"));
			if (moreItemCount==1){
				moreItem.title = _("There is one more item");
			} else {
				moreItem.title = _("There are %s more items").sprintf(moreItemCount);
			}

			// singel click
			dhtml.addEvent(this.moduleID,moreItem,"click",eventChangeViewToDay);
		}
		
	}
}

CalendarMonthView.prototype.execute = function(items, properties, action)
{
	var entryids = false;

	this.element.appendChild(this.tableElement);
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

CalendarMonthView.prototype.addItem = function()
{
	return false;
}

/**
 * Function will add/update an item of the month view
 * there are two options 	1: createItem(item) for niewe items
 * 							2: createItem(item,element) for moved items 
 * @param item calendar item
 * @param element existing div element 
 * @return object[id],[entryid]  
 */ 
CalendarMonthView.prototype.createItem = function(item,element)
{
	var entry = Object();
	
	var unixTimeStart = item.getElementsByTagName("startdate")[0].getAttribute("unixtime");
	var unixTimeDue = item.getElementsByTagName("duedate")[0].getAttribute("unixtime");	
	var startDate = new Date(unixTimeStart*1000);
	var dueDate = new Date(unixTimeDue*1000);
	var subject = dhtml.getXMLValue(item, "subject", NBSP);
	var duration = dhtml.getXMLValue(item, "duration", 0);
	var parent_entryid = dhtml.getXMLValue(item, "parent_entryid", "");
	var entryid = dhtml.getXMLValue(item, "entryid", "");
	var today = new Date(startDate);
	var endday = new Date(dueDate);
	var privateAppointment = dhtml.getXMLValue(item, "private", NBSP);
	var privateSensitivity = dhtml.getXMLValue(item, "sensitivity", NBSP);
	
	var AppointmentElement;
	
	var today = new Date(startDate);
	today.clearTime();
	var endday = new Date(dueDate);
	endday.clearTime();
	
	// Treat the item as an 'allday' event if it starts on a different day than it ends
	var alldayevent = today.getTime() != endday.getTime();

	while(today <= dueDate){
		// check if there is a zero duration appointment.
		if(today < dueDate || (dueDate - startDate == 0)){
			var unixTimeId = startDate.getTime()/1000;
			var selDay = dhtml.getElementById(parent_entryid+"_"+(today.getTime()/1000));
		
			if(selDay){
				if(element){
					dhtml.deleteElement(element);
				}
				AppointmentElement = dhtml.addElement(selDay,"div");
				AppointmentElement.is_disabled = dhtml.getTextNode(item.getElementsByTagName("disabled_item")[0],0) != 0; // for private items, no events may be added to this item

				AppointmentElement.setAttribute("id",entryid+"_"+unixTimeId)
				AppointmentElement.setAttribute("startdate",unixTimeId);
				AppointmentElement.setAttribute("duedate",unixTimeDue);
		
				if(alldayevent){
					// allday event
					AppointmentElement.className = "event_item ipm_appointment event_day_item";			
					AppointmentElement.setAttribute("title",subject);
				} else{				
					// normal event
					AppointmentElement.className = "event_item ipm_appointment";
					//strftime func calculates starttime and endtime and support both 24/12Hrs Time format according to language settings
					AppointmentElement.setAttribute("title","["+strftime(_('%H:%M'),unixTimeId)+" - "+strftime(_('%H:%M'),unixTimeDue)+"] "+subject);
					dhtml.addElement(AppointmentElement,"span","event_time","",strftime(_('%H:%M'),unixTimeId)+NBSP);
				}	
				
				// private item
				if(privateAppointment == "1") {
					dhtml.addElement(AppointmentElement, "span", "private", false, NBSP);
				} else if(privateSensitivity == "2") {
					dhtml.addElement(AppointmentElement, "span", "private", false, NBSP);
				}

				// subject
				dhtml.addElement(AppointmentElement,"span","event_subject","",subject);

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
				if(item.getElementsByTagName("label")[0]){
					switch(item.getElementsByTagName("label")[0].firstChild.nodeValue){
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
				}
				
				var recurring = item.getElementsByTagName("recurring")[0];
				if(recurring && recurring.firstChild) {
					// Basedate is used for saving
					var basedate = item.getElementsByTagName("basedate")[0];
					if(basedate && basedate.firstChild) {
						AppointmentElement.setAttribute("basedate", basedate.getAttribute("unixtime"));
					}
				}

				if (!AppointmentElement.is_disabled){
					// add events
					if (this.events && this.events["row"]){
						dhtml.setEvents(this.moduleID, AppointmentElement, this.events["row"]);
					}

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

CalendarMonthView.prototype.updateItem = function(element, item, properties)
{
	if(item) {
		// delete all occurences of the multiday appointment.
		while(dhtml.getElementById(element.id)){
			dhtml.deleteElement(dhtml.getElementById(element.id));
		}
		var result = this.createItem(item,element);
		this.checkForMoreItems();
		return result;
	}
	return undefined;
}

CalendarMonthView.prototype.sortItems = function(itemA, itemB)
{
	if(itemA.offsetTop > itemB.offsetTop) return 1;
	if(itemA.offsetTop < itemB.offsetTop) return -1;
	
	return 0;
}

CalendarMonthView.prototype.loadMessage = function()
{

	dhtml.removeEvents(this.element);
	dhtml.deleteAllChildren(this.element);

	this.element.innerHTML = "<center>" + _("Loading") + "...</center>";
	document.body.style.cursor = "wait";
}

CalendarMonthView.prototype.deleteLoadMessage = function()
{
	dhtml.deleteAllChildren(this.element);
	this.initView();
	document.body.style.cursor = "default";
}

function eventChangeViewToWorkWeek(moduleObject, element, event)
{
	var timestamp = element.getAttribute("unixtime");
	moduleObject.changeView("week",timestamp*1000);
}

function eventChangeViewToDay(moduleObject, element, event)
{
	event.stopPropagation();
	var timestamp = element.getAttribute("unixtime");
	moduleObject.changeView("day",timestamp*1000);
}

function eventCalenderMonthViewDragDropTarget(moduleObject, targetElement, element, event)
{	
	var draggableStartdate = parseInt(element.getAttribute("startdate") || 0);
	var draggableDuedate = parseInt(element.getAttribute("duedate") || 0);
	var draggableBasedate = element.getAttribute("basedate");
	var draggableEntryID = element.getAttribute("id").split("_")[0];
	var targetDate = new Date(parseInt(targetElement.getAttribute("unixtime") || 0)*1000);
	var duration = draggableDuedate-draggableStartdate;		

	var newDraggableStartdate = new Date(draggableStartdate*1000);
	// inspite of adding date/month/year to draggableSratDate, we will add time to targetDate.
	// to avoid the moving an appointment from higher days month to lesser days month.
	var newDragHours = newDraggableStartdate.getHours();
	var newDragMinutes = newDraggableStartdate.getMinutes();
	newDraggableStartdate = timeToZero(targetDate.getTime() / 1000);
	newDraggableStartdate = addHoursToUnixTimeStamp(newDraggableStartdate, newDragHours + (newDragMinutes/60));

	var newDraggableDuedate = addHoursToUnixTimeStamp(newDraggableStartdate, (duration/3600));
	
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
		if(newDraggableDuedate>(new Date().getTime()/1000) && element.requestsent && element.requestsent == "1") {
			send = confirm(_("Would you like to send an update to the attendees regarding changes to this meeting?"));
		}
	}

	if(draggableBasedate){
		moveOccAllowFlag = confirmAppointMoved(moduleObject, draggableBasedate, targetElement.getAttribute("unixtime"));
	}
	if(moveOccAllowFlag){
		moduleObject.save(props, send);
	}

	// moduleObject.viewController.updateItem(element);//not necessary
}

function eventMonthViewClickPrev(moduleObject, element, event)
{
	var datepickerlistmodule = webclient.getModulesByName("datepickerlistmodule")[0];
	var month = moduleObject.selectedDate.getMonth();
	var year = moduleObject.selectedDate.getFullYear();
	
	if(month == 0){
		month = 11;
		year--;
	} else {
		month--;
	}
	
	var timestamp = new Date(year, month, 1, 0, 0, 0);
	datepickerlistmodule.changeMonth(timestamp.getMonth() + 1, timestamp.getFullYear(), true);
	datepickerlistmodule.changeSelectedDate(timestamp.getTime(),true);
}

function eventMonthViewClickNext(moduleObject, element, event)
{
	var datepickerlistmodule = webclient.getModulesByName("datepickerlistmodule")[0];
	var month = moduleObject.selectedDate.getMonth();
	var year = moduleObject.selectedDate.getFullYear();
	
	if(month == 11){
		month = 0;
		year++;
	} else {
		month++;
	}
	
	var timestamp = new Date(year, month, 1, 0, 0, 0);
	datepickerlistmodule.changeMonth(timestamp.getMonth() + 1, timestamp.getFullYear(), true);
	datepickerlistmodule.changeSelectedDate(timestamp.getTime(),true);
}


function eventCalendarMonthViewKeyboard(moduleObject, element, event)
{
	var viewObject = moduleObject.viewController.viewObject;
	switch (event.keyCode){
		case 37: // navigations keys
			eventMonthViewClickPrev(moduleObject);
			break;
		case 39: // navigations keys
			eventMonthViewClickNext(moduleObject);
			break;
	}
}