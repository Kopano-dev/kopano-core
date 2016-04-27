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
 * --Calendar Day View--
 * @type	View
 * @classDescription	This view can be used for appointement
 * list module to display the calendar items
 *
 * for 1 hour duration
 * +-------+------------+------------+
 * | wk 51 | Mon 18 Dec | Tue 19 Dec |
 * +-------+------------+------------+
 * |  9:00 |			|			 |
 * +-------+------------+------------+
 * | 10:00 |			|			 |
 * +-------+------------+------------+
 *
 * for 30 minute duration
 * +-------+------------+------------+
 * | wk 51 | Mon 18 Dec | Tue 19 Dec |
 * +-------+------------+------------+
 * |  9:00 |------------|------------|
 * +-------+------------+------------+
 * | 10:00 |------------|------------|
 * +-------+------------+------------+
 * | 11:00 |------------|------------|
 * +-------+------------+------------+
 * 
 * for 15 minute duration
 * +-------+------------+------------+
 * | wk 51 | Mon 18 Dec | Tue 19 Dec |
 * +-------+------------+------------+
 * |       |------------|------------|
 * |  9:00 |------------|------------|
 * |       |------------|------------|
 * +-------+------------+------------+
 * |	   |------------|------------|
 * | 10:00 |------------|------------|
 * |	   |------------|------------|
 * +-------+------------+------------+
 *
 * 
 * DEPENDS ON:
 * |------> view.js
 * |----+-> *listmodule.js
 * |    |----> listmodule.js
 */
CalendarDayView.prototype = new View;
CalendarDayView.prototype.constructor = CalendarDayView;
CalendarDayView.superclass = View.prototype;


function CalendarDayView(moduleID, element, events, data)
{
	this.element = element;
	
	this.moduleID = moduleID;
	this.events = events;
	this.data = data;

	this.initView();
	this.setData(data, false);
	
}

CalendarDayView.prototype.setData = function(data)
{
	this.startdate = data["startdate"];
	this.duedate = data["duedate"];
	this.selecteddate = data["selecteddate"];

	this.days = new Array();
	for(var i = new Date(this.startdate); i.getTime() < this.duedate; i=i.add(Date.DAY, 1))
	{
		// This fixes the DST where it goes from 0:00 to 01:00.
		i = i.add(Date.HOUR, 12);
		i.clearTime();

		this.days.push(i.getTime());
	}
	
	this.setDayHeaders();
	
	// if selected date has been changed then we should reset selection
	this.clearSelection();
	this.resetSelection();
}

// Initializes the static background. This is not date-dependant
CalendarDayView.prototype.initView = function()
{
	this.headerElement = dhtml.addElement(this.element, "div", "header", this.moduleID+"_header");
	this.headerElement.style.width = 100+"%";
	this.weekNumberElement = dhtml.addElement(this.headerElement,"span","","week_number");
	this.alldayEventElement = dhtml.addElement(this.headerElement, "div", "alldayevent");
	this.alldayElement = dhtml.addElement(this.alldayEventElement, "div", "allcontainer");
	this.timelinetopElement = dhtml.addElement(this.headerElement, "div", "timelinetop");
	
	var workdayStart = webclient.settings.get("calendar/workdaystart", 9*60);
	var workdayEnd = webclient.settings.get("calendar/workdayend", 17*60);
	
	// define the number of hours per day
	this.numberOfHours = 24;
	// this defines the number of cells in one row which actually defines (60/appointment time in settings).
	this.numberOfCellsInOneHour = webclient.settings.get("calendar/appointment_time_size", 2);
	// this is time for each cell which is opposite of this.numberOfCellsInOneHour (as 60/this.numberOfCellsInOneHour)
	this.cellDefaultTime = 60/this.numberOfCellsInOneHour;

	// this defines the height of cells for different vsize of calendar
	switch(parseInt(webclient.settings.get("calendar/vsize", 2))) {
		case 1: this.hourHeight = 15; break;
		default: this.hourHeight = 25; break;
		case 3: this.hourHeight = 35; break;
	}
	//extra title
	var prevTitle, nextTitle;
	switch(parseInt(this.data["days"],10)){
		case 1:
			prevTitle = _("Previous day");
			nextTitle = _("Next day");
			break;
		default:
			prevTitle = _("Previous week");
			nextTitle = _("Next week");
			break;
	}
	var extraElement = dhtml.addElement("","span");

	// title string
	this.rangeElement = dhtml.addElement(extraElement,"span","","");
	
	// previous button 
	var prevButton = dhtml.addElement(extraElement,"span","prev_button","",NBSP);
	dhtml.addEvent(this.moduleID,prevButton,"click",eventDayViewClickPrev);
	prevButton.title = prevTitle;
	
	// next button
	var nextButton =dhtml.addElement(extraElement,"span","next_button","",NBSP);
	dhtml.addEvent(this.moduleID,nextButton,"click",eventDayViewClickNext);
	nextButton.title = nextTitle;

	webclient.getModule(this.moduleID).setExtraTitle(extraElement);
	
	for(var i = 0; i < this.data["days"]; i++)
	{
		var dayElement = dhtml.addElement(this.headerElement, "div", "date");

		//create the cells in All Day element for selection in all day timeline
		var allDayTimeLineElement = dhtml.addElement(this.alldayElement, "div", "allDayTimeLine", "day_"+i+"_time", NBSP);
		allDayTimeLineElement.setAttribute("allDayTimeLineElement", true);
		allDayTimeLineElement.offsetDay = i;

		//attach the events on those elements
		dhtml.addEvent(this.moduleID, allDayTimeLineElement, "mousedown", eventCalendarDayViewSelectionMouseDown);
		dhtml.addEvent(this, allDayTimeLineElement, "mouseup", eventCalendarDayViewAllDaySelectionMouseUp);
		dhtml.addEvent(this.moduleID, allDayTimeLineElement, "dblclick", eventCalendarDayViewDblClick);
	}
	// attach an event for creating proper selection in allday timeline event.
	dhtml.addEvent(this, this.alldayElement, "mousemove", eventCalendarDayViewAllDaySelMouseMove);
	this.daysElement = dhtml.addElement(this.element, "div", false, "days");
	
	var timelineElement = dhtml.addElement(this.daysElement, "div", "timeline");
	
	for(var i = 0; i < this.numberOfHours; i++)
	{
		var timeElement = dhtml.addElement(timelineElement, "div", "time");
		timeElement.style.height = ((this.hourHeight*this.numberOfCellsInOneHour)-1)+"px";

		// Parsing time to be compatible with US time display (ex. 10:00 AM)
		var timeString = strftime_gmt(_("%H:%M"), ( (i*60*60) ));

		var timePortions = timeString.split(":");
		var timePortions2 = timePortions[1].split(" ");
		var hour = timePortions[0];
		var mins = timePortions2[0];
		timeElement.innerHTML = hour + "<sup>"+mins+"</sup>";
	}
	this.cellHeight = (this.hourHeight-2)+"px";
	for(var i = 0; i < this.data["days"]; i++){
		var dayElement = dhtml.addElement(this.daysElement, "div", "day", "day_" + i);
		for(var j = 0; j < this.numberOfHours; j++){
			// create the elements for one hour
			for (var n=0;n<this.numberOfCellsInOneHour;n++){
				var cellOffsetStartTime = (j*60)+(n*this.cellDefaultTime);
				var cellOffsetEndTime = cellOffsetStartTime + this.cellDefaultTime;
				var brightness = "light";
				if(i > 4 || cellOffsetEndTime <= workdayStart || cellOffsetEndTime > workdayEnd) brightness = "dark";

				var classPrefix = "top";
				if(n%this.numberOfCellsInOneHour > 0) classPrefix = "bottom";

				var cellElement = dhtml.addElement(dayElement, "div", classPrefix + brightness, "day_" + i + "_time" + cellOffsetStartTime);
				cellElement.offsetTime = cellOffsetStartTime;
				cellElement.offsetDay = i;
				cellElement.style.height = this.cellHeight;

				dragdrop.addTarget(this.daysElement, cellElement, "appointment", true, true);
				dhtml.addEvent(this.moduleID, cellElement, "mousedown", eventCalendarDayViewSelectionMouseDown);
				dhtml.addEvent(this.moduleID, cellElement, "dblclick", eventCalendarDayViewDblClick);
			}
		}
		// Add an element at 24:00 which we use for positioning items that end at 00:00. You can't actually see it.
		var endElem = dhtml.addElement(dayElement, "div", "", "day_" + i + "_time1440");
		endElem.offsetDay = i;
		endElem.offsetTime = this.numberOfHours * 60;

		dhtml.addEvent(this, dayElement, "mousemove", eventCalendarDayViewSelectionMouseMove);
		dhtml.addEvent(this, dayElement, "mouseup", eventCalendarDayViewSelectionMouseUp);
	}

	// add keyboard event
	var module = webclient.getModule(this.moduleID);
	webclient.inputmanager.addObject(module, module.element);
	
	//Dont bind event if it is already binded earlier.
	if (!webclient.inputmanager.hasEvent(module, "keydown", eventCalendarDayViewSelectionKeyUp)) {
		webclient.inputmanager.bindEvent(module, "keydown", eventCalendarDayViewSelectionKeyUp);
	}

	// attach an event on body, to avoid messy selection. suppose user start selection and 
	// mouseup on somewhere else in body other than time line. the selection will go continuously 
	// to stop that we create this event
	dhtml.addEvent(this, document.body, "mouseup", eventCalendarStopSelectionMouseUp);
}

// Sets the date-dependant parts of the view
CalendarDayView.prototype.setDayHeaders = function()
{
	// Set the week number (wk XX)
	this.weekNumberElement.innerHTML = _("wk")+" "+this.selecteddate.getWeekNumber();

	var days = dhtml.getElementsByClassNameInElement(this.headerElement, "date", "div", true);
	
	// Set the day headers (mon 12 jan)
	for(var i = 0; i < days.length; i++) {
		var date = new Date(this.days[i]);
		var dayOfWeek = date.getDay();
		// Translation string shows "abbreviated day of the week name, day number in the month, abbreviated month name
		days[i].innerHTML = date.strftime(_("%a %e %h"));
		// select today in view.
		var todayCount = (new Date().getDay()== 0) ? 7 : new Date().getDay();
		if((todayCount-1 == i  && date.isSameDay(new Date())) || (this.data["days"]==1 && date.isSameDay(new Date()))){
			dhtml.addClassName(days[i], "selectedToday");
			dhtml.addClassName(this.daysElement.childNodes[i+1], "selectedToday");
		}else {
			dhtml.removeClassName(days[i], "selectedToday");
			dhtml.removeClassName(this.daysElement.childNodes[i+1], "selectedToday");
		}
	}

	// Set the range header ('1 january - 7 janary')
	var titleString = "";
	switch(parseInt(this.data["days"],10)){
		case 1:
			titleString = this.selecteddate.getDate()+" "+MONTHS[this.selecteddate.getMonth()]+" "+this.selecteddate.getFullYear();
			break;
		default:
			var startDateObj = new Date(this.startdate);
			var dueDateObj = new Date(this.duedate-ONE_DAY);			
			titleString = startDateObj.getDate()+" "+MONTHS[startDateObj.getMonth()]+" - "+dueDateObj.getDate()+" "+MONTHS[dueDateObj.getMonth()];
			break;
	}
	this.rangeElement.innerHTML = titleString;

	this.clearAppointments();

}

CalendarDayView.prototype.resizeView = function()
{
	// clears the variable which is being used for max height of allDayElement.
	this.maxPosTop = 0;
	// Position all-day items so that we know the height of the all-day container
	this.positionAllAlldayItems();
	
	// Set main viewing window size
	this.daysElement.style.width = (this.headerElement.clientWidth - 1 > 0?this.headerElement.clientWidth - 1:3) + "px";
	this.daysElement.style.height = (this.element.clientHeight - this.daysElement.offsetTop - 1 > 0?this.element.clientHeight - this.daysElement.offsetTop - 1:3) + "px";

	// Set day widths
	// Header
	var days = dhtml.getElementsByClassNameInElement(this.headerElement, "date", "div", true);
	var width = (this.headerElement.offsetWidth - 73) / this.data["days"];
	var leftPosition = 50;
	
	if(width > 0) {
		for(var i = 0; i < this.data["days"]; i++)
		{
			days[i].style.width = width + "px";
			days[i].style.left = leftPosition + "px";
			
			leftPosition += days[i].clientWidth + 1;
		}
		
		// Days
		var days = dhtml.getElementsByClassNameInElement(this.daysElement, "day", "div", true);
		leftPosition = 50;
		for(var i = 0; i < this.data["days"]; i++)
		{
			days[i].style.width = width + "px";
			days[i].style.left = leftPosition + "px";
			
			if(i == this.data["days"] - 1) {
				days[i].style.width = (width + (this.headerElement.clientWidth - leftPosition - width - 18)) + "px";
			}
			
			leftPosition += days[i].clientWidth + 1;
		}
	}
	
	var workdayStart = webclient.settings.get("calendar/workdaystart", 9*60);
	var workDayStartElem = dhtml.getElementById("day_0_time"+workdayStart, "div", this.daysElement);
	if(workDayStartElem) {
		this.daysElement.scrollTop = workDayStartElem.offsetTop;
	}

	this.positionItems();

	this.updateTargets();

}

// Updates all the targets by looking at the x,y coordinates of each day and
// updating the all the cells of those days seperately (instead of through
// dragdrop.updateTargets() )
CalendarDayView.prototype.updateTargets = function()
{
	for(var i = 0; i < this.data["days"]; i++) {
		var topleft = dhtml.getElementTopLeft(dhtml.getElementById("day_" + i + "_time0"));
		var x = topleft[0];
		var y = topleft[1];
		
		for(var j = 0; j < this.numberOfHours; j++) {
			for (var n=0;n<this.numberOfCellsInOneHour;n++){
				var dragElemId = "day_" + i + "_time" + ((j * 60)+(this.cellDefaultTime*n));
				var dragElemY = y + (j*this.numberOfCellsInOneHour*this.hourHeight) + (n * this.hourHeight);
				dragdrop.updateTarget("appointment", dragElemId, x, dragElemY);
			}
		}
	}
}

CalendarDayView.prototype.clearAppointments = function()
{
	// Remove old items
	var prevItems = dhtml.getElementsByClassNameInElement(this.element, "appointment", "div");
	for(var i = 0; i < prevItems.length; i++) {
		dhtml.deleteElement(prevItems[i]);
	}
		
	// Remove old all-day items
	var prevItems = dhtml.getElementsByClassNameInElement(this.headerElement, "allday_appointment", "div");
	for(var i = 0; i < prevItems.length; i++) {
		dhtml.deleteElement(prevItems[i]);
	}
		
}

// Open the initial view, return entryids of all objects
CalendarDayView.prototype.execute = function(items, properties, action)
{
	this.clearAppointments();
	
	var entryids = new Array();

	for(var i = 0; i < items.length; i++){
		var itemStart = parseInt(items[i].getElementsByTagName("startdate")[0].getAttribute("unixtime"),10)*1000;
		var itemDue = parseInt(items[i].getElementsByTagName("duedate")[0].getAttribute("unixtime"),10)*1000;

		/**
		 * FIRST PART : Check range criterium. 
		 * Start of appointment is before end of time period and
		 * the end of appointment is before start of time period
		 *
		 * SECOND PART : check if occurence is not a zero duration occurrence which
		 * starts at 00:00 and ends on 00:00. if it is so, then process it.
		 */
		if((this.duedate >= itemStart && itemDue >= this.startdate) || (itemDue - itemStart == 0)){
			var entry = this.addItemWithoutReposition(items[i], properties, action);
		}	
		if(entry) {
			// set two rows in lookup, for entryids for two different element of one multiday appointment
			if(typeof entry['id'] == "object"){
				entryids[entry['id'][0]] = entry['entryid'];
				entryids[entry['id'][1]] = entry['entryid'];
			}else{
				entryids[entry['id']] = entry['entryid'];
			}
		}
	}

	this.positionItems();

	this.updateTargets();

	// We must resize because there may be new allday items which change the size of the main
	// pane
	this.resizeView();

	return entryids;
}

// Add an item, only used internally (within this file)
CalendarDayView.prototype.addItemWithoutReposition = function(item, properties, action)
{
	var result = false;

	// The 'days' div
	var days = dhtml.getElementsByClassNameInElement(this.daysElement, "day", "div", true);
	if(item){
		var itemData = this.fetchDataForApptObjectFromXML(item);
		if(itemData["entryid"]){
			if(itemData["startdate"]){
				if(dhtml.getElementById(itemData["elemId"]))
					return; // we already have this item, ignore it. 
				
				if(itemData["unixtime"]) {
					// All day event (real all-day event or item which spans multiple days)
					// Calculate on which day the start date of the item is
					var day = this.getDayIndexByTimestamp(itemData["unixtime"]);
					result = new Object();
					result = this.createItem(days, day, itemData);
				}
			}
		}
	}
	return result;
}

/**
 * Get the day index of the timestamp that is passed as argument. It will search for the day that 
 * it matches in the days property. 
 * @param timestamp Number|String Timestamp to be used
 * @return Number The day index from the days property
 */
CalendarDayView.prototype.getDayIndexByTimestamp = function(timestamp)
{
	var date = new Date( parseInt(timestamp,10) * 1000);
	// Clearing the date object is part of the way to compensate for DST changes that happen during mid-night
	var clearedTimestamp = date.clearTime().getTime();
	for(var i=0,len=this.days.length;i<len;i++){
		if(clearedTimestamp == this.days[i]){
			return i;
		}
	}
	return 0;
}

// Called when a single item must be added (ie after adding a new calendar item)
CalendarDayView.prototype.addItem = function(item, properties, action)
{
	var result = true;
	var startTime = item.getElementsByTagName("startdate")[0].getAttribute("unixtime")*1000;
	var dueTime = item.getElementsByTagName("duedate")[0].getAttribute("unixtime")*1000;
	// Start of appointment is before end of time period and the end of appointment is before start of time period
	if(this.duedate > startTime && dueTime > this.startdate){
		var result = this.addItemWithoutReposition(item,properties,action);
		this.positionItems();
		dragdrop.updateTargets("appointment");
	}
	
	this.resizeView();
	
	return result;
}

// Called when a single item is deleted
CalendarDayView.prototype.deleteItems = function(items)
{
	return false; // fallback to total refresh
}

// Called when a single item is updated
CalendarDayView.prototype.updateItem = function(element, item, properties)
{
	entry = new Object();

	var days = dhtml.getElementsByClassNameInElement(this.daysElement, "day", "div", true);
	
	var isAllDayElement = element.type == "allday" ? true : false;
	var isAllDayItem = isAllDayElement;
	var itemStartdate, itemUnixTime, itemDay;
	
	if(item) {
		var itemData = this.fetchDataForApptObjectFromXML(item);
		itemStartdate = itemData["startdate"];
		itemUnixTime = itemStartdate.getAttribute("unixtime"); 
		itemDay = Math.floor((itemUnixTime - (this.days[0]/1000))/86400);
		
		if(element.getAttribute("multipleDayElementCount")){
			var elementObjs = this.itemElementLookup[itemData["entryid"]];
		}
		// Item switched to a day which is beyond the view
		// then just delete the item from that view and return
		if(itemDay >= days.length || itemDay < 0) {
			// Calculate endday of the item.
			var itemEndDay = Math.floor((itemData["duedate"].getAttribute("unixtime") - (this.days[0]/1000))/86400);

			// Check if either one of the elements of multiday appointment is coming on first day or not.
			// if not then delete and return else skip all this loop and go to deleting of elements.
			// If startday and endday, both are in past then start of view range then delete element.
			if((itemDay+1) < 0 && itemEndDay < 0){
				// if the updated item was multiday appointment
				if(element.getAttribute("multipleDayElementCount")){
					for(var x in elementObjs){
						if(dhtml.getElementById(elementObjs[x]))
							dhtml.deleteElement(dhtml.getElementById(elementObjs[x]));
					}
				}else{
					dhtml.deleteElement(element);
				}
				entry["id"] = "";
				entry["entryid"] = itemData["entryid"];
				return entry;
			}
		}

		/**
		 * Delete the previous items which checks.
		 */
		//check if appointment previously was a multiday and now its normal
		// OR
		//check if appointment previously was a multiday and now its multiday
		if((element.getAttribute("multipleDayElementCount") && !itemData["isMultiDayAppt"])
			|| (element.getAttribute("multipleDayElementCount") && itemData["isMultiDayAppt"])){
			for(var x in elementObjs){
				if(dhtml.getElementById(elementObjs[x]))
					dhtml.deleteElement(dhtml.getElementById(elementObjs[x]));
			}
		}//check if appointment previously was an normal and now its a multiday
		// OR
		//check if appointment was previously a normal appointment and now as well.
		else if((!element.getAttribute("multipleDayElementCount") && itemData["isMultiDayAppt"])
				||(!element.getAttribute("multipleDayElementCount") && !itemData["isMultiDayAppt"])) {
			dhtml.deleteElement(element);
		}

		/**
		 * re-create the updated items.
		 */
		entry = this.createItem(days, itemDay, itemData);
		
		/**
		 * reposition all items effected/newly created.
		 */
		this.positionItems();
	}
	else {
		entry = false;
	}
	return entry;
}

// Create a standard item in the appointment item list (but don't position it)
CalendarDayView.prototype.createItem = function(days, day, itemData){
	var appointmentElement;
	var itemFirstElement;
	var itemSecondElement;
	var itemDue;
	var itemStart;
	var endDateTime;
	var startDateTime;
	var result = new Object();

	if(itemData["isAllDayItem"]) {
		if(itemData["isMultiDayAppt"]){
			itemDue = itemData["duedate"].getAttribute("unixtime")*1000;
			itemStart = itemData["startdate"].getAttribute("unixtime")*1000;
			itemFirstElement = new Object(itemData);
			itemSecondElement = new Object(itemData);

			/**
			 * Set Date and time and Create First Element of Multiday Appointment
			 **/
			endDateTime = new Date(itemStart);
			// set first end time to 23:59 in the same day night.
			// to stop overflowing of the appointment in view. 
			endDateTime.setHours(23);
			endDateTime.setMinutes(59);
			endDateTime.setSeconds(0);
			// set the endDateTime to the itemFirstElement's duedate attribute which will be used by createItem 
			// as end time for creation of appointment.
			itemFirstElement["duedate"].setAttribute("unixtime", endDateTime.getTime()/1000);
			
			appointmentElement = dhtml.addElement(days[day], "div", false, itemData["elemId"]+"_1");
			this.createAppointmentElement(days[day], appointmentElement, itemData, day, 1);

			/**
			 * Set Date and time and Create second Element of Multiday Appointment
			 **/
			startDateTime = new Date(itemDue);
			// set second start time to 00:00 in the same end day.
			// to start the next day view properly in next day. 
			startDateTime.setHours(0);
			startDateTime.setMinutes(0);
			startDateTime.setSeconds(0);
			// set the startDateTime to the itemSecondElement's startdate attribute which will be used by createItem
			// as start time for creation of appointment. 
			itemSecondElement["startdate"].setAttribute("unixtime", startDateTime.getTime()/1000);
			itemSecondElement["duedate"].setAttribute("unixtime", new Date(itemDue).getTime()/1000);
			appointmentElement = dhtml.addElement(days[day+1], "div", false, itemData["elemId"]+"_2");
			this.createAppointmentElement(days[day+1], appointmentElement, itemData, day+1, 2);
			result["id"] = [itemData["elemId"]+"_1",itemData["elemId"]+"_2"];
		}else{
			this.createAllDayItem(dhtml.addElement(this.alldayElement, "div", false, "" + itemData["elemId"]), itemData);
			result["id"] = itemData["elemId"];
		}
	} else {
		this.createAppointmentElement(days[day], dhtml.addElement(days[day], "div", false, itemData["elemId"]), itemData, day);
		result["id"] = itemData["elemId"];
	}
	result["entryid"] = itemData["entryid"];
	return result;
}

/**
 * Function to create the elements of appointmen (normal and multiday).
 * @param object daysElement object contains the days element array
 * @param object appointment object contains the appointment element data
 * @param object itemData object contains the appointment data
 * @param object daynr number contains day number in view
 * @param number multipleDayElementCount number , element number of the multiday appointment element.
 */
CalendarDayView.prototype.createAppointmentElement = function(daysElement, appointment, itemData, daynr, multipleDayElementCount)
{
	if (typeof daysElement == "undefined"){
		return; // it could happen that this function is called for an item that 
				// doesn't belong on this date in that case daysElement is 
				// undefined so we ignore this item
	}

	appointment.style.display = "none";
	
	if (!itemData["is_disabled"]){
		if (this.events && this.events["row"]){
			delete(this.events["row"]["mouseup"]);
			dhtml.setEvents(this.moduleID, appointment, this.events["row"]);
		}
		dhtml.addEvent(this.moduleID, appointment, "mousedown", eventCalendarDayViewOnClick);
		dhtml.addEvent(this.moduleID, appointment, "keyup", eventCalendarDayViewKeyboard);
	}
	var className = "ipm_appointment appointment";
	className += this.convertLabelNrToString(itemData["label"]);
	
	appointment.className = className;
	if (!itemData["is_disabled"]){
		if(!itemData["isMultiDayAppt"])
		dragdrop.addDraggable(appointment, "appointment", true, APPOINTMENT_RESIZABLE);
	}
	if(itemData["privateAppointment"] && itemData["privateAppointment"].firstChild && itemData["privateSensitivity"] && itemData["privateSensitivity"].firstChild) {
		if(itemData["privateAppointment"].firstChild.nodeValue == "1") {
			dhtml.addElement(appointment, "span", "private", false, NBSP);
		} else if(itemData["privateSensitivity"].firstChild.nodeValue == "2") {
			dhtml.addElement(appointment, "span", "private", false, NBSP);
		}
	}
	if (itemData["reminderSet"] == "1"){
		dhtml.addElement(appointment, "span", "reminder", false, NBSP);
		appointment.reminderMinutes = itemData["reminderMinutes"];
		appointment.reminderSet = true;
	}
	if(itemData["requestsent"]) {
		appointment.requestsent = itemData["requestsent"];
	}
	if(itemData["meeting"] && itemData["meeting"].firstChild) {
		appointment.meetingrequest = itemData["responseStatus"]; // store responseStatus in DOM tree
		switch(itemData["meeting"].firstChild.nodeValue)
		{
			case "1":
			case "3":
			case "5":
				dhtml.addElement(appointment, "span", "meetingrequest", false, NBSP);
				break;
		}
	}

	if(itemData["recurring"] && itemData["recurring"].firstChild) {
		if(itemData["exception"] && itemData["exception"].firstChild) {
			dhtml.addElement(appointment, "span", "recurring_exception", false, NBSP);			
		} else if(itemData["recurring"].firstChild.nodeValue == "1") {
			dhtml.addElement(appointment, "span", "recurring", false, NBSP);
		}
		
		// Basedate is used for saving
		if(itemData["basedate"] && itemData["basedate"].firstChild) {
			appointment.setAttribute("basedate", itemData["basedate"].getAttribute("unixtime"));
		}
	}
	
	if(itemData["subject"] && itemData["subject"].firstChild) {
		dhtml.addElement(appointment, "span", false, false, itemData["subject"].firstChild.nodeValue);
		appointment.subject = itemData["subject"].firstChild.nodeValue;
	}else{
		appointment.innerHTML += "&nbsp;";
		appointment.subject = NBSP;
	}
	
	if(itemData["location"] && itemData["location"].firstChild) {
		dhtml.addElement(appointment, "div", false, false, "(" + itemData["location"].firstChild.nodeValue + ")");
		appointment.location = itemData["location"].firstChild.nodeValue;
	}
	
	appointment.start = itemData["startdate"].getAttribute("unixtime");
	appointment.end = itemData["duedate"].getAttribute("unixtime");
	
	if(itemData["startdate"] && itemData["startdate"].firstChild && itemData["duedate"] && itemData["duedate"].firstChild) {
		var starttime = itemData["startdate"].getAttribute("unixtime");
		var duetime = itemData["duedate"].getAttribute("unixtime");
		if (duetime==starttime) 
			duetime = (starttime*1)+60; // make duetime +1 minute when equals to starttime
		
		// Normalize reversed start/end
		if(starttime > duetime) {
			var tmp = starttime;
			starttime = duetime;
			duetime = tmp;
		}

		/* It can happen that when dealing with the Brazilian DST changes that happen at 0:00 the 
		 * starttime will be at 23:00 the past day. This check will see if the start of the 
		 * appointment is before the start of the day. If so it will set the appointment to the 
		 * start of the day. 
		 */ 
		if((starttime * 1000) < this.days[daynr]){
			starttime = this.days[daynr]/1000;
		}
		/* If the due time is set to the start of a day the appointment will last till that point. 
		 * In the calendar it should show you an appointment that will run till 0:00. 
		 * When dealing with the Brazilian DST it will shift from 0:00 till 1:00. That means that it
		 * will render an appointment that is an extra hour as the appointment lasts till 01:00. 
		 * This check sets the duetime to 23:59 on the same day as the starttime, thus preventing 
		 * that extra hour from being rendered.
		 */
		if(duetime*1000 == new Date(duetime*1000).clearTime().getTime()){
			var newDueDate = new Date(starttime*1000);
			newDueDate.setHours(23);
			newDueDate.setMinutes(59);
			duetime = newDueDate.getTime()/1000;
		}

		var start_date = new Date(starttime * 1000);
		var due_date = new Date(duetime * 1000);

		/* When the Brazilian DST changes it happens on 0:00. The timestamp of that day will then 
		 * start at 01:00 instead of 0:00. The appointment will then start at 01:00 in the view. 
		 * This check will compensate for that by setting the hours to 0 for calculating the 
		 * positioning.
		 */
		var startHours, startMinutes;
		if(starttime*1000 == this.days[daynr]){
			startHours = 0;
			startMinutes = 0;
		}else{
			startHours = start_date.getHours();
			startMinutes = start_date.getMinutes();
		}


		appointment.offsetStartTime = startHours * 60 + startMinutes;
		appointment.offsetDay = daynr;

		/**
		 * there can be such cases for startdateid.
			1. 10 minute
				a. start-> 08 minutes : id should be [dayelementid]+["_time"] + (60 * hours + parseInt(minutes/celltime) * celltime)
				b. start -> 12 minutes
			2. 15 minute
				a. start-> 12 minutes
				b. start -> 18 minutes
		 *
		 */
		var startDateId = daysElement.id + "_time" + (startHours * 60 + (parseInt(startMinutes/this.cellDefaultTime) * this.cellDefaultTime));
		
		/**
		 * there can be such cases for enddateid.
			1. 10 minute
				a. end-> 08 minutes : id should be [dayelementid]+["_time"] + (60 * hours + (parseInt(minutes/celltime)+1) * celltime)
				b. end -> 12 minutes
			2. 15 minute
				a. end-> 12 minutes
				b. end -> 18 minutes
		 *
		 */
		if(due_date.getMinutes()%this.cellDefaultTime > 0)
			var dueDateId = daysElement.id + "_time" + (due_date.getHours() * 60 + ((parseInt(due_date.getMinutes()/this.cellDefaultTime)+1) * this.cellDefaultTime));
		else
			var dueDateId = daysElement.id + "_time" + (due_date.getHours() * 60 + (parseInt(due_date.getMinutes()/this.cellDefaultTime) * this.cellDefaultTime));

		appointment.offsetEndTime = (due_date.getHours() * 60) + due_date.getMinutes();

		var startDateElem = dhtml.getElementById(startDateId);
		var endDateElem = dhtml.getElementById(dueDateId);
		appointment.style.top = startDateElem.offsetTop + "px";
		appointment.style.width = (daysElement.clientWidth - 15) + "px";
		
		var height = endDateElem.offsetTop - startDateElem.offsetTop - 6;
		if(height < 0) {
			// Height is negative, so the item ends tomorrow. Show the item until the end of today.
			height += daysElement.clientHeight;
		}
		switch(multipleDayElementCount){
			case 1:
				appointment.setAttribute("multipleDayElementCount", starttime+"_"+multipleDayElementCount);
			case 2:
				appointment.setAttribute("multipleDayElementCount", duetime+"_"+multipleDayElementCount);
		}
		appointment.style.height = height + "px";
		
		// This attribute is used for saving. 
		appointment.setAttribute("starttime", startDateId);

		var busyStatus;
		if(itemData["busystatus"] && itemData["busystatus"].firstChild) {
			switch(itemData["busystatus"].firstChild.nodeValue)
			{
				case "0":
					busyStatus = "free";
					break;
				case "1":
					busyStatus = "tentative";
					break;
				case "3":
					busyStatus = "outofoffice";
					break;
				default:
					busyStatus = "busy";
					break;
			}
		} else {
			busyStatus = " busy";
		}

		// Next we are going to set up the duration bar that will indicate how long the appointment will run.
		var durationBar = dhtml.addElement(appointment, "div", "durationbar "+busyStatus, false);

		var barStartDate = new Date(start_date.getTime());
		var barDueDate = new Date(due_date.getTime());

		/**
		 * Set the date to either 0 min or 30 min, this is because the appointment block snaps to 
		 * halfhour slots and we want the start date of that block.
		 * When the calendar has the setting to display slots of 5 minutes, it will snap at 0, 15, 
		 * 30 and 45.
		 */
		barStartDate = barStartDate.floorMinutes(this.cellDefaultTime);
		barDueDate = barDueDate.ceilMinutes(this.cellDefaultTime, true);

		// The blockTimeDiff is the number of seconds that the block covers (not the appointment)
		var blockTimeDiff = barDueDate.getTime() - barStartDate.getTime();
		// The offsetDate is the number of seconds after the start of the block.
		var offsetAppointmentStartDate = start_date.getTime() - barStartDate.getTime();
		var offsetAppointmentDueDate = due_date.getTime() - barStartDate.getTime();
		// The deltaAppointmentStart and Due will be used to convert the seconds into pixels
		var deltaAppointmentStart = offsetAppointmentStartDate / blockTimeDiff;
		var deltaAppointmentDue = offsetAppointmentDueDate / blockTimeDiff;

		// For some reason I have to recalculate the height of the block here
		var blockHeight = parseInt(endDateElem.offsetTop, 10) - parseInt(startDateElem.offsetTop, 10);
		if(blockHeight  < 0 && barDueDate.getHours() == 0 && barDueDate.getMinutes() == 0 ) {
			// blockHeight is negative, so the item ends tomorrow i.e item ends at 24 hour of current day. Show the item until the end of today.
			blockHeight = parseInt(daysElement.clientHeight,10) - parseInt(startDateElem.offsetTop, 10);
		}
		// Calculating the number of pixels where the duration bar needs to start and end
		// The height value is calculated earlier on in this function
		var durationBarStart = Math.round(blockHeight * deltaAppointmentStart);
		var durationBarEnd = Math.round(blockHeight * deltaAppointmentDue); // Using "End" now instead of "Due"

		/** 
		 * If the appointment ends at xx:59 we should make it look like it ends on the next whole 
		 * hour. Multiday appointments split up in two parts ranging from xx:xx till 23:59 and 0:00 
		 * till yy:yy. This way the duration block will stretch to the end.
		 */
		if(due_date.getMinutes() == 59){
			durationBarEnd = blockHeight;
		}

		/**
		 * The following lines are added for IE6 only
		 * IE6 does not work with setting top 0px and bottom 0px, We need to set an actual height. 
		 * The -4 is for the borders that are not included in the height.
		 */
		if(window.BROWSER_IE6){
			var durationBarHeight = (durationBarEnd-durationBarStart-4);
			// IE6 will give "invalid arguments" for negative values.
			durationBar.style.height = (durationBarHeight > 0 ? durationBarHeight : 0)+ "px";
		}

		// If the durationBar becomes too small, force the bar to show at least one pixel
		if(durationBarEnd - durationBarStart < 2){
			durationBar.style.height = "1px";
		}

		durationBar.style.top = durationBarStart + "px";
		durationBar.style.bottom = (blockHeight - durationBarEnd) + "px";
	}
	if(multipleDayElementCount){
		this.setMultiDayItemInObject(appointment, multipleDayElementCount);
		appointment.type = "multiday";
	}else{
		appointment.type = "normal";
	}
	
}

/**
 * Function to set the multiday appointments elements as reference in object.
 * @param object appointmentElement object contains the appointment element data
 * @param number multipleDayElementCount number , element number of the multiday appointment element.
 */
CalendarDayView.prototype.setMultiDayItemInObject = function(appointmentElement, multipleDayElementCount)
{
	/**
	 * Object schema
	 * object = {
	 				entry_id : 	[
	 								0:element_1.id,
	 								1:element_2.id
	 							]
	 			}
	 *
	 *
	 */
	if(!this.itemElementLookup){
		this.itemElementLookup = new Object();
	}
	var entryid = appointmentElement.id.split("_")[0];
	if((!this.itemElementLookup[entryid] && typeof this.itemElementLookup[entryid] == "undefined") || this.itemElementLookup[entryid] == null)
		this.itemElementLookup[entryid] = new Array();
	this.itemElementLookup[entryid][multipleDayElementCount-1] = appointmentElement.id;
	
}

/**
 * Function will convert label number to a classname
 * @param value = int label number
 * @return string classname  
 */ 
CalendarDayView.prototype.convertLabelNrToString = function(value)
{
	var className = "";
	//Appointment labels
	if(value){
		switch(value){
			case "1": className = " label_important"; break;
			case "2": className = " label_work"; break;
			case "3": className = " label_personal"; break;
			case "4": className = " label_holiday"; break;
			case "5": className = " label_required"; break;
			case "6": className = " label_travel_required"; break;
			case "7": className = " label_prepare_required"; break;
			case "8": className = " label_birthday"; break;
			case "9": className = " label_special_date"; break;
			case "10": className = " label_phone_interview"; break;
			default: className = " label_none"; break;
		}
	}
	else{
		className = " label_none";
	}
	return className;
}

// Create an all day item (but don't position it)
CalendarDayView.prototype.createAllDayItem = function(appointElement, itemData)
{
	// reset events thay may still be there when converting from a 'normal' appointment
	appointElement.events = new Array();
	appointElement.style.cursor = "";
	
	var subject = itemData["subject"];
	var startUnixtime = parseInt(itemData["startdate"].getAttribute("unixtime"),10);
	var endUnixtime = parseInt(itemData["duedate"].getAttribute("unixtime"),10);
	var duration = itemData["duration"];
	var label = itemData["label"];
	var alldayevent = itemData["alldayevent"];
	var meeting = itemData["meeting"];
	var location = "";
	if(itemData["location"] && itemData["location"].childNodes.length > 0){
		location = " ("+itemData["location"].firstChild.nodeValue+")";
	}
	
	if(startUnixtime >= this.duedate/1000 || endUnixtime <= this.startdate/1000){
		// deleteing the created allday appointment element, as it does not falls in the range.
		dhtml.deleteElement(appointElement);
		return; // Item we received is not within our viewing range
	}

	//body of item
	dhtml.deleteAllChildren(appointElement);

	if(itemData["recurring"] && itemData["recurring"].firstChild) {
		if(itemData["exception"] && itemData["exception"].firstChild) {
			dhtml.addElement(appointElement, "span", "recurring_exception", false, NBSP);			
		} else if(itemData["recurring"].firstChild.nodeValue == "1") {
			dhtml.addElement(appointElement, "span", "recurring", false, NBSP);
		}
		
		// Remember basedate
		if(itemData["basedate"] && itemData["basedate"].firstChild) {
			appointElement.setAttribute("basedate", itemData["basedate"].getAttribute("unixtime"));
		}
	}

	dhtml.addTextNode(appointElement,dhtml.getTextNode(subject, NBSP)+location);
	appointElement.setAttribute("start",startUnixtime);
	appointElement.setAttribute("end",endUnixtime);
	appointElement.setAttribute("alldayevent", itemData["alldayevent"]);

	appointElement.start = startUnixtime;
	appointElement.end = endUnixtime;
	appointElement.meetingrequest = itemData["responseStatus"]; // store responseStatus in DOM tree
	appointElement.requestsent = itemData["requestsent"]; // store requestSent in DOM tree

	//style
	var className = "ipm_appointment allday_appointment";
	if(itemData["busystatus"] && itemData["busystatus"].firstChild) {
		switch(itemData["busystatus"].firstChild.nodeValue){
			case "0":
				className += " free";
				break;
			case "1":
				className += " tentative";
				break;
			case "3":
				className += " outofoffice";
				break;
			default:
				className += " busy";
				break;
		}
	} else {
		className += " busy";
	}
	dhtml.addClassName(appointElement, className);
	dhtml.addClassName(appointElement, this.convertLabelNrToString(label));

	//addevents
	appointElement.is_disabled = itemData["disabled_item"];		// for private items, no events may be added to this item 
	if(!appointElement.is_disabled) {
		if (this.events && this.events["row"]){ 
			dhtml.setEvents(this.moduleID, appointElement, this.events["row"]); 
		}
		dhtml.addEvent(this.moduleID, appointElement, "click", eventCalendarDayViewOnClick);
		dhtml.addEvent(this.moduleID, appointElement, "keyup", eventCalendarDayViewKeyboard);
	}

	appointElement.type = "allday";
}

// Position all items (called after an addition/removal/change)
CalendarDayView.prototype.positionItems = function()
{
	var days = dhtml.getElementsByClassNameInElement(this.daysElement, "day", "div", true);
	
	if(days[0]){
		for(var i = 0; i < this.data["days"]; i++)
		{
			this.positionItemsOnDay(days[i]);
		}
		this.positionAllAlldayItems();
	}
}

// Reposition all allday items
CalendarDayView.prototype.positionAllAlldayItems = function()
{	
	//position all "all day events"
	var dayList = new Object();
	var headerElement = this.headerElement;
	var alldayeventElement = this.alldayEventElement;
	var timelinetopElement = this.timelinetopElement;
	
	var headDays = dhtml.getElementsByClassNameInElement(this.headerElement,"date","div",true);
	var allDayItems = dhtml.getElementsByClassNameInElement(this.alldayElement,"allday_appointment","div",true);
	var allDayTimeLineItems = dhtml.getElementsByClassNameInElement(this.alldayElement,"allDayTimeLine","div",true);
	
	allDayItems.sort(this.sortAlldayItems);

	for(i in headDays){
		dayList[i] = new Object();
	}
	if(allDayItems.length > 0){
		for(i in allDayItems){
			var realStart = allDayItems[i].getAttribute("start");
			var realEnd = allDayItems[i].getAttribute("end");
			var isAllDayEvent = allDayItems[i].getAttribute("alldayevent");
			var startTime = timeToZero(parseInt(realStart,10));
			var endTime = timeToZero(parseInt(realEnd,10));
			if(startTime < Math.floor(this.startdate/1000)){
				startTime = Math.floor(this.startdate/1000);
			}
			if(endTime > Math.floor(this.duedate/1000)){
				endTime = Math.floor(this.duedate/1000);
			}
	
			/**
			 * If the IF-statement is true the appointment runs from 0:00 day X till
			 * 0:00 day Y and can be considered a real ALL DAY event. When this is 
			 * not the case and the appointment runs from mid-day till mid-day the 
			 * duration has to be calculated differently. The mid-day appointment 
			 * has to be placed over all days it covers.
			 */
	
			// If event is not all day event.
			if(isAllDayEvent == 0){ 
				if(realStart == startTime && realEnd == endTime){
					var durationInDays = Math.ceil((endTime-startTime)/86400);
				}else{
					//calculate the start and end time in hours and minutes, if they are 00:00 then 
					//dont add one extra day in endTime and calculate durationInDays
					var startHourMin = (new Date(realStart * 1000)).getHours() + (new Date(realStart * 1000)).getMinutes();
					var endHourMin = (new Date(realEnd * 1000)).getHours() + (new Date(realEnd * 1000)).getMinutes();
					 	
					// If the time is 00:00 and dates are diff 
					if(startHourMin == 0 && endHourMin == 0){
						var durationInDays = Math.ceil((endTime-startTime)/86400);
					}else{
						var durationInDays = Math.ceil((endTime+(this.numberOfHours*60*60)-startTime)/86400);
					}
				}
			}else{
				var durationInDays = Math.ceil((endTime-startTime)/86400);
			}
			var startDay = Math.floor((startTime - (this.days[0]/1000))/86400);
			var headerDayElement = dhtml.getElementsByClassNameInElement(this.headerElement,"date","div", true)[startDay];
			var posTop = 0;
			
			// You could have an item with 'AllDayEvent == true' but start == end.
			if(durationInDays == 0)
				durationInDays = 1;
			
			for(var j=0;j<durationInDays;j++){
				if(dayList[startDay+j]){
					while(dayList[startDay+j][posTop]=="used"){
						posTop++;
					}
				}
			}
			//flag used in dayList
			for(var j=0;j<durationInDays;j++){
				if(!dayList[startDay+j]){
					dayList[startDay+j] = new Object();
				}
				dayList[startDay+j][posTop]="used";
			}
			if(headerDayElement){
				//set style of item
				allDayItems[i].style.left = headerDayElement.offsetLeft - 50 + "px";
				allDayItems[i].style.width = (headerDayElement.offsetWidth*durationInDays) - 6 + "px";
				allDayItems[i].style.height = "14px";
				allDayItems[i].style.top = (allDayItems[i].offsetHeight+2)*posTop+"px";
			}
			//size the headers
			posTop++;
			
			// set the heighest posTop as maxPosTop, to get the proper height of allDayElement.
			if(!this.maxPosTop){
				this.maxPosTop = posTop;
			}else{
				if(this.maxPosTop <= posTop){
					this.maxPosTop = posTop;
				}
			}
			
			if(alldayeventElement.offsetHeight > posTop*19){
				posTop = 1;
			}
			
			// set posTop to available MaxPosTop.
			if(this.maxPosTop){
				posTop = this.maxPosTop;
			}
			
			headerElement.style.height = (posTop*19+17+15)+"px";
			timelinetopElement.style.height = (posTop*19+16+15)+"px";
			alldayeventElement.style.height = (posTop*19+15)+"px";
		}
	}else{
		// handle the case if allDayItems is not present
		if(alldayeventElement.offsetHeight >= 19){
			alldayeventElement.style.height = 19+"px";
			headerElement.style.height = 19+17+"px";
			timelinetopElement.style.height = 19+16+"px";
		}
	}
	var headerDayElement = dhtml.getElementsByClassNameInElement(this.headerElement,"date","div", true);
	for(i in allDayTimeLineItems){
		allDayTimeLineItems[i].style.width = headerDayElement[i].style.width;
		allDayTimeLineItems[i].style.height = this.alldayEventElement.style.height;
		if(headerDayElement[i].style.left != "")
			allDayTimeLineItems[i].style.left = (parseInt(headerDayElement[i].style.left)-50) + "px";
	}
}

// Gets all overlapping items and returns them as an array
CalendarDayView.prototype.getOverLapping = function(slots, start, end)
{
	var overlapping = new Object();
	
	for(elementid in slots) {
		var slot = slots[elementid];
		//there can be a case when duration is '0'min then slot's start will equalto end of the appointment so,we also check this condition
		if(slot.start < end && slot.end > start || slot.start == end) 
			overlapping[elementid] = slot;
	}
	
	return overlapping;
}

// Propagates the 'maxdepth' value of slot with id 'elementid' to all its overlappers and their overlappers
CalendarDayView.prototype.propagateMaxSlot = function(slots, elementid, maxslot)
{
	// Loop protection
	if(slots[elementid].maxslot == maxslot)
		return;
		
	slots[elementid].maxslot = maxslot;
	
	var overlapping = this.getOverLapping(slots, slots[elementid].start, slots[elementid].end);
	
	for(elementid in overlapping) {
		this.propagateMaxSlot(slots, elementid, maxslot);
	}
}

/**
 * @todo implement multi dayevents in this function
 */ 
CalendarDayView.prototype.positionItemsOnDay = function(dayElement)
{
	var items = dhtml.getElementsByClassNameInElement(dayElement, "appointment", "div", true);

	// This is the array of all items, which will be increasingly built up
	// by this algorithm to calculate in which slot the items must go
	var slots = new Object();


	// Sort items by startdate, and length (longest first)
	items.sort(this.sortItems);

	/* Loop through all the items, doing the following:
	 * For each item, get all overlapping slots alread placed and try to place the
	 * new item in slot0 .. slotN (with slotN being the maxslot of overlapper 0)
	 * If there are no slots, set slot to N+1, and propagate a new maxslot (N+1)
	 * to all overlappers and their overlappers.
	 */
	for(var i = 0; i < items.length; i++)
	{
		/**
		 * Calculate the position times. To prevent 15min appointments from 
		 * overlapping the positioning time is rounded to the fill the complete 
		 * half hour slot.
		 */
		items[i].positionStartTime = (new Date(items[i].start*1000)).floorMinutes(this.cellDefaultTime).getTime()/1000;
		items[i].positionEndTime = (new Date(items[i].end*1000)).ceilMinutes(this.cellDefaultTime, true).getTime()/1000;

		var overlapping = this.getOverLapping(slots, items[i].positionStartTime, items[i].positionEndTime);
		var maxdepth = 0;
		var placed = false;
		var elementid = items[i].id;
		var slot = new Object;
			
		// Remember this info so we can do overlap checking later
		slot.start = items[i].positionStartTime;
		slot.end = items[i].positionEndTime;
		
		for(var overlapid in overlapping) { // this is more like an if(overlapping.length > 0)
			maxdepth = overlapping[overlapid].maxslot; // maxslot should be the same for all overlappers, so take the first
			
			// Try to put the item in the leftmost slot
			for(var slotnr = 0; slotnr < maxdepth; slotnr++) {
				var slotfree = true;
				for(var overlapper in overlapping) {
					if(overlapping[overlapper].slot == slotnr) {
						slotfree = false;
						break; 
					}
				}
				
				// This slot is still free, so use that
				if(slotfree) {
					placed = true;
					slot.slot = slotnr;
					slot.maxslot = maxdepth;
					slots[elementid] = slot;
					break;
				}
			}
			break; // Yep, only go through this once
		}

		if(!placed) {
			// No free slots, add a new slot on the right
			slot.slot = maxdepth;
			slot.maxslot = 0; // will be updated by propagateMaxSlot 

			slots[elementid] = slot;
				
			// Propagate new 'maxslot' to all other overlapping slots (and their overlappers etc)
			this.propagateMaxSlot(slots, elementid, maxdepth + 1);
		}
	}		
	 
	/* After running through this algorithm for all items, the 'slots' array
	 * will contain both the slot number and the max. slot number for each item
	 * which we now use to position and resize the items
	 */

	// Position items
	for(i = 0; i < items.length; i++)
	{
		var item = items[i];
		var slot = slots[items[i].id];
		
		var width = (item.parentNode.clientWidth / slot.maxslot) - 15;
		item.style.width = width + "px";
		item.style.left = (slot.slot * (width + 15)) + "px";
		item.style.display = "";
	}
}

// Sort on starttime, if time is equal, sort on duration (longest first)
CalendarDayView.prototype.sortItems = function(itemA, itemB)
{
	if(itemA.start == itemB.start)
		return (itemB.end - itemB.start) - (itemA.end - itemA.start);
	if(itemA.start > itemB.start) return 1;
	if(itemA.start < itemB.start) return -1;
	
	return 0;
}

CalendarDayView.prototype.sortAlldayItems = function(itemA, itemB)
{
	var result = 0;
	var startA = parseInt(itemA.getAttribute("start"),10);
	var startB = parseInt(itemB.getAttribute("start"),10);
	var endA = parseInt(itemA.getAttribute("end"),10);
	var endB = parseInt(itemB.getAttribute("end"),10);
	var durationA = endA-startA;
	var durationB = endB-startB;
		
	//biggest duration on top
	if(durationA > durationB)	result = -1;
	if(durationA < durationB)	result = 1;
	
	//smallest starttime on top
	if(startA > startB)	result = 1;
	if(startA < startB)	result = -1;
	
	return result;
}

CalendarDayView.prototype.isAllDayItem = function(allDayEvent, startdate, duedate)
{
	var itemStart = startdate.getAttribute("unixtime")*1000;
	var itemDue = duedate.getAttribute("unixtime")*1000;
	allDayEvent = parseInt(allDayEvent);

	if((allDayEvent == 1) ||
		(timeToZero(itemStart/1000) != timeToZero(itemDue/1000))){
		
		var tempStartTime = new Date(itemStart);
		tempStartTime.addDays(1);
		// Check whether the start date does not start at the start of a day and the due Date does.
		if(itemStart !== (new Date(itemStart)).clearTime().getTime() &&
			itemDue === (new Date(itemDue)).clearTime().getTime() && 
			(timeToZero(tempStartTime.getTime()/1000) == timeToZero(itemDue/1000)) ) {
			return false;
		}else{
			return true;
		}
	}else{
		return false;
	}
}

CalendarDayView.prototype.loadMessage = function()
{
	document.body.style.cursor = "wait";
}


CalendarDayView.prototype.deleteLoadMessage = function()
{
	document.body.style.cursor = "default";
}


/**
 * Function to check whether the appointment is a multiday appointment or not
 * @param object item object contains the appointment data
 * in this function,  we check 2 major things
 *  1. the appointment's total time is less than 24 hrs but greater than 0 hrs.
 *  2. the appointment is spreaded to 2 different days.
 *  3. the appointment's end date is greater than next day's 00:00
 *  If we find all these conditions then we return true otherwise false.
 */
CalendarDayView.prototype.isMultiDayAppointment = function(item){
	// find the start and end time of that appointment to check the total time span is not more than 24 hrs.
	var itemStart = item.getElementsByTagName("startdate")[0].getAttribute("unixtime")*1000;
	var itemDue = item.getElementsByTagName("duedate")[0].getAttribute("unixtime")*1000;
	var isAllDayEvent = (dhtml.getTextNode(item.getElementsByTagName("alldayevent")[0])) ? dhtml.getTextNode(item.getElementsByTagName("alldayevent")[0]) : 0 ;

	// check whether item has more than 24 hrs time span or not.
	var deltaTimeInHours = (new Date(itemDue).getTime() - new Date(itemStart).getTime())/(1000 * 60);
	if(deltaTimeInHours > 0 && deltaTimeInHours < (this.numberOfHours * 60) && (new Date(itemStart).getDate() != new Date(itemDue).getDate()) &&	isAllDayEvent != 1){
		
		/* Check by clearing the time whether the due date ends on the start of a day. We need to 
		 * check it like this to work around DST changes like the Brazilian DST that switches at 0:00.
		 */
		if(itemDue === (new Date(itemDue)).clearTime().getTime()){
			return false;
			
		}else{
			return true;
		}
	} else {
		return false;
	}
}

/**
 * Function to fetch all data from XML once into a JS object.
 * @param object item object contains the XML data
 * @return object dataObj object contains the appointment data in JS format
 */
CalendarDayView.prototype.fetchDataForApptObjectFromXML = function(item){
	if(item){
		dataObj = new Object();
		dataObj["entryid"] = dhtml.getXMLValue(item,"entryid");
		if(dataObj["entryid"]){
			dataObj["startdate"] = item.getElementsByTagName("startdate")[0];
			dataObj["duedate"] = item.getElementsByTagName("duedate")[0];
			dataObj["unixtime"] = dataObj["startdate"].getAttribute("unixtime"); 
			
			dataObj["elemId"] = dataObj["entryid"] + "_" + dataObj["unixtime"];
			dataObj["alldayevent"] = dhtml.getTextNode(item.getElementsByTagName("alldayevent")[0]);
			dataObj["isAllDayItem"] = this.isAllDayItem(dataObj["alldayevent"], dataObj["startdate"], dataObj["duedate"]);
			dataObj["isMultiDayAppt"] = this.isMultiDayAppointment(item);
			
			// for private items, no events may be added to this item
			dataObj["is_disabled"] = dhtml.getTextNode(item.getElementsByTagName("disabled_item")[0],0) != 0;
			
			dataObj["label"] = dhtml.getTextNode(item.getElementsByTagName("label")[0]);
			dataObj["busystatus"] = item.getElementsByTagName("busystatus")[0];
			dataObj["privateAppointment"] = item.getElementsByTagName("private")[0];
			dataObj["privateSensitivity"] = item.getElementsByTagName("sensitivity")[0];
			
			dataObj["reminderSet"] = dhtml.getXMLValue(item, "reminder");
			dataObj["reminderMinutes"] = dhtml.getXMLValue(item, "reminder_minutes", "");
			// get MeetingRequestWasSent property and check that is MR really sent or not.
			dataObj["requestsent"] = dhtml.getTextNode(item.getElementsByTagName("requestsent")[0]);
			dataObj["meeting"] = item.getElementsByTagName("meeting")[0];
			if(dataObj["meeting"]) {
				dataObj["responseStatus"] = dhtml.getTextNode(item.getElementsByTagName("responsestatus")[0], 0);
			}
			
			dataObj["recurring"] = item.getElementsByTagName("recurring")[0];
			if(dataObj["recurring"]) {
				dataObj["exception"] = item.getElementsByTagName("exception")[0];
				// Basedate is used for saving
				dataObj["basedate"] = item.getElementsByTagName("basedate")[0];
			}
			dataObj["subject"] = item.getElementsByTagName("subject")[0];
			dataObj["location"] = item.getElementsByTagName("location")[0];
			
			dataObj["duration"] = dhtml.getTextNode(item.getElementsByTagName("duration")[0]);
			dataObj["disabled_item"] = dhtml.getTextNode(item.getElementsByTagName("disabled_item")[0],0);
		}
		return dataObj;
	}
}

// Function for quick appointment goes here.

/**
 * Function to create contextmenu on section's right click.
 * @param element object HTML element
 * @param clientX number MouseX position
 * @param clientY number MouseY position
 */
CalendarDayView.prototype.showSelectionContextMenu = function(element, clientX, clientY){
	var items = new Array();
	items.push(webclient.menu.createMenuItem("appointment", _("Create Appointment"), _("Create Appointment"), eventCalendarDayContextMenuClickCreateAppointment));
	items.push(webclient.menu.createMenuItem("meetingrequest", _("Create Meetingrequest"), _("Create Meetingrequest"), eventCalendarDayContextMenuCreateMeetingRequestClick));
	webclient.menu.buildContextMenu(this.moduleID, element.id, items, clientX, clientY);
}

/**
 * Function to start the selection in both timeline.
 * @param element object HTML element
 */
CalendarDayView.prototype.startSelection = function(element){
	this.selectionStart = element.id;
	this.selectionEnd = element.id;
	this.selectionInAllDay = false;
	this.selectingTimerange = true;
	this.clearSelection();

	//select the current element to show that selection is started.
	this.markSelection(element.id, element.id);
}

/**
 * Function to check whether selection is in progress or not.
 *   means mouse is pressed and user is dragging it over timeline.
 * @return boolean boolean true - if selection is in progress / false when it is not.
 */
CalendarDayView.prototype.isSelecting = function(){
	if(this.selectingTimerange){
		return true;
	}else{
		return false;
	}
}

/**
 * Function which first get the selection range and then mark it selected
 * @param startID String starting HTML element ID of selection 
 * @param endID String ending HTML element ID of selection
 */
CalendarDayView.prototype.markSelection = function(startID, endID){
	// if selectionRange is not avaialble then initialize it.
	if(!this.selectionRange) this.selectionRange = new Array();

	// get the selection range here according to selection area;
	if(this.selectionInAllDay)
		this.selectionRange = this.getAllDaySelectionRange(startID, endID);
	else
		this.selectionRange = this.getSelectionRange(startID, endID);

	for(var i=0; i< this.selectionRange.length; i++){
		dhtml.addClassName(this.selectionRange[i], "selection");
	}
}

/**
 * Function which gets called on mousemove event, it 
 * @param startID String starting HTML element ID of selection 
 * @param endID String ending HTML element ID of selection
 */
CalendarDayView.prototype.moveSelection = function(currentElement){
	if(currentElement.parentNode == this.alldayElement){
		this.selectionInAllDay = true;
	}else{
		this.selectionInAllDay = false;
	}
	// mark all the element selected
	this.markSelection(this.selectionStart, currentElement.id);
}

/**
 * Function to end the selection in both timeline.
 * @param element object HTML element
 */
CalendarDayView.prototype.endSelection = function(element){
	this.selectionEnd = element.id;
	this.selectingTimerange = false;
}

/**
 * Function to reset the selection.
 * It will help to stop populating the quick element when there is no selection.
 * case : You create a selection and clear it still if you type something the 
 *        quick appointment element will get created. to avoid that we use this.
 * @param element object HTML element
 */
CalendarDayView.prototype.resetSelection = function(element){
	this.selectionStart = null;
	this.selectionEnd = null;
	this.selectionRange = new Array();
	this.selectingTimerange = false;
}

/**
 * Function to clear the selection in both timeline.
 */
CalendarDayView.prototype.clearSelection = function(){
 	// clear the selection of selected cells to clear the selection. 
	if(this.selectionRange){
		for(var i=0;i<this.selectionRange.length;i++){ 
			dhtml.removeClassName(this.selectionRange[i],"selection"); 
		}
	}
	if(this.alldayElement){
		//var headerDayEventElement = dhtml.getElementsByClassNameInElement(this.alldayElement, "allDayTimeLine", "div", true);
		for(var i=0;i<this.alldayElement.childNodes.length;i++){ 
			dhtml.removeClassName(this.alldayElement.childNodes[i],"selection");
		}
	}
}

/**
 * Function to get the selection range of Allday time line's selected items.
 * @param startID String starting HTML element ID of selection 
 * @param endID String ending HTML element ID of selection
 */
CalendarDayView.prototype.getAllDaySelectionRange = function(startId, endId){
	// create the selection range here from start point to end point / current point.
	var selectionRange = new Array();
	var startElIndex = dhtml.getElementById(startId).offsetDay;
	var endElIndex = dhtml.getElementById(endId).offsetDay;
	if(startElIndex>endElIndex){
		var temp = startElIndex;
		startElIndex = endElIndex;
		endElIndex = temp;
	}
	var element = this.alldayElement.childNodes;
	for (var i = startElIndex; i<=endElIndex; i++){
		selectionRange.push(element[i]);
	}
	return selectionRange;
}

/**
 * Function to get the selection range of normal calendar time line's selected items.
 * @param startID String starting HTML element ID of selection 
 * @param endID String ending HTML element ID of selection
 */
CalendarDayView.prototype.getSelectionRange = function(startId, endId){
	// create the selection range here from start point to end point / current point.
	var selectionRange = new Array();
	if(!this.alldayElement) return selectionRange;

	var startElement = dhtml.getElementById(startId);
	var endElement = dhtml.getElementById(endId);
	// check case of start selecting at allday and moved pointer to normal cell
	if(startElement.parentNode.id == this.alldayElement.id && endElement.parentNode.id != this.alldayElement.id){
		// set the start cell as start day's 00:00 cell and hold a starting pointer of actual start cell
		this.selectionStart = "day_"+startElement.offsetDay+"_time0";
		startElement = dhtml.getElementById(this.selectionStart);
	}
	if(endElement && endElement.id && endElement.id.split("_").length <=2){
		if(typeof this.selectionEnd != "undefined")
			endElement = dhtml.getElementById(this.selectionEnd);
		else
			return selectionRange;
	}

	if((startElement.offsetDay > endElement.offsetDay) || 
		((startElement.offsetTime > endElement.offsetTime) && startElement.offsetDay == endElement.offsetDay)){
		// selection is in reverse direction		
		var temp = startElement;
		startElement = endElement; 
		endElement = temp;
	}

	var startoffsetDay = startElement.offsetDay;
	var endoffsetDay = endElement.offsetDay;

	var startOffsetTime = startElement.offsetTime;
	var endOffsetTime = endElement.offsetTime;
	
	//If the selection is on the same day and not expanding to multiple days
	if(startoffsetDay == endoffsetDay){
		for(var i = startOffsetTime/this.cellDefaultTime; i<=(endOffsetTime/this.cellDefaultTime); i++){
			var el = startElement.parentNode.childNodes[i];
			selectionRange.push(el);
		}
	}else{
		/******* create the selection range which is spread over multiple days. *******/

		// step1. first select the days which are  starting from starting point of selection to end of the day
		for(var j = startOffsetTime/this.cellDefaultTime; j<(1440/this.cellDefaultTime); j++){
			var el = startElement.parentNode.childNodes[j];
			selectionRange.push(el);
		}

		// step2. then if the number of days are more than 1 then select the complete 
		var dayDiff = parseInt(endoffsetDay - startoffsetDay);
		if(dayDiff > 1){
			for(var x = 1; x < dayDiff; x++){
				var nextDayElement = dhtml.getElementById("day_"+(startoffsetDay+x));
				if(nextDayElement)
				for(var i = 0;i<nextDayElement.childNodes.length;i++){
					var el = nextDayElement.childNodes[i];
					selectionRange.push(el);
				}
			}
		}
		//step 3. add the remaing cells of ending 
		if(endoffsetDay == startoffsetDay)
			return selectionRange;

		for(var i = 0; i<=(endOffsetTime/this.cellDefaultTime); i++){
			var el = endElement.parentNode.childNodes[i];
			selectionRange.push(el);
		}
	}
	return selectionRange;
}

/**
 * Function which will return the cell Id on current mouseY position.
 *		get the cell location and match it with the actual mouse location on day element, 
 *		if found in criteria return the element id of cell.
 * @param element object HTML element
 * @param clientY number MouseY position
 * @return currElement object HTML element
 */
CalendarDayView.prototype.translateYCoordToTimelineCell = function(element, clientY){
	var currElement = false;
	var mouseY = clientY - dhtml.getElementTopLeft(this.daysElement)[1] ;
	var appointmentStartCell = dhtml.getElementById(this.selectionStart);
	var elScrollTop = dhtml.getElementById("days").scrollTop;
	var elLen = element.childNodes.length;
	var elTop = 0;

	for(var i=0;i<elLen;i++){
		var el = element.childNodes[i];
		if(el.id.split("_").length > 2){
			if((elTop - elScrollTop) <= mouseY && (elTop + el.offsetHeight + 1) >= (mouseY + elScrollTop)) 
				return el;
		}else{
			return appointmentStartCell;
		}
		elTop += this.hourHeight;
	}
	return currElement;
}

/**
 * Function which will return the cell Id on current mouseX position.
 *		get the cell location and match it with the actual mouse location on Allday timeline element, 
 *		if found in criteria return the element id of cell.
 * @param element object HTML element
 * @param clientY number MouseY position
 * @return currElement object HTML element
 */
CalendarDayView.prototype.translateXCoordToAllDayTimelineCell = function(element, clientX){
	var currElement = false;
	// calculate the current cell acccording to mouseX position.
	var mouseX = clientX - dhtml.getElementTopLeft(this.alldayElement)[0] ;
	var appointmentStartCell = dhtml.getElementById(this.selectionStart);
	var elLen = element.childNodes.length;
	for(var i=0;i<elLen;i++){
		var el = element.childNodes[i];
		if(el.id.split("_").length > 2){
			if(el.offsetLeft <= mouseX && mouseX <= (el.offsetLeft + el.offsetWidth)){
				return el;
			}
		}else{
			return appointmentStartCell;
		}
	}
	return currElement;
}

/**
 * Function to get the start and end time of the selection area.
 * @param contextMenuFlag boolean boolean value  (used for sending proper timing for all day event creation.)
 * @return array array ArrayObject which contains start and end time. 
 * 		if no selection then returns false.
 */
CalendarDayView.prototype.getStartEndTimeOfSelection = function(contextMenuFlag){
	var elements = this.selectionRange;
	if(typeof elements != 'undefined' && elements.length > 0){
		//if selection is on normal calendar timeline
		if(!this.selectionInAllDay){
			var startOffsetDay = elements[0].offsetDay;
			var startOffsetTime = elements[0].offsetTime;

			var endOffsetDay = elements[elements.length-1].offsetDay;
			var endOffsetTime = elements[elements.length-1].offsetTime + this.cellDefaultTime;
		}//check if selection on all day timeline then find the required data
		else{
			var startOffsetDay = elements[0].offsetDay;
			var startOffsetTime = 0;
			
			var endOffsetDay = parseInt(elements[elements.length-1].offsetDay);
			//as this item is selected from allday timeline so add 1440 minutes to the endTime timestamp 
			var endOffsetTime = (contextMenuFlag) ? this.cellDefaultTime : 1440;
		}
		var starttime = addHoursToUnixTimeStamp(this.days[startOffsetDay]/1000 , (startOffsetTime / 60));
		var endtime = addHoursToUnixTimeStamp(this.days[endOffsetDay]/1000 , (endOffsetTime / 60));

		return [starttime, endtime];
	}
	return false;
}

/**
 * Function which creates a quick element for creating quick appointment. 
 *	1.	single line quick element in timeline. (input)
 *	2.	multiple line quick element (textarea)
 *	3.	single line quick element in all day slot (input)
 *	4.	single line quick element in all day slot (but text area and for all day appointment
 * @param moduleObject object Module Object
 * @param element object HTML element object
 */
CalendarDayView.prototype.createQuickAppointmentElement = function(moduleObject, character){
	
	var elements = this.selectionRange;
	if(elements.length>0){
		var element = elements[0];
	}else{
		return;
	}
	
	if(typeof this.keyPressed == "undefined" || this.keyPressed == null){
		this.keyPressed = new Array();
	}
	if(character != 13)
		this.keyPressed.push(character);

	var days = dhtml.getElementById("days");

	// delete quickElem only if it exists and isn't the same time
	if (moduleObject.quickElem && moduleObject.quickElem.getAttribute("timestamp") != element.id){
		event = new Object();
		event.keyCode = 13;
		eventQuickAppointmentKey(moduleObject, moduleObject.quickElem, event);
		if(typeof moduleObject.quickElem != "undefined" && moduleObject.quickElem && typeof moduleObject.quickElem.secondPart != "undefined" && moduleObject.quickElem.secondPart)
			dhtml.deleteElement(moduleObject.quickElem.secondPart);
		dhtml.deleteElement(moduleObject.quickElem);
		moduleObject.quickElem = null;
	}

	// when quickElem still exists, exit this function, nothing to do here
	if (!moduleObject.quickElem){
		if(elements.length>0){
			var startElement = elements[0];
			var endElement = elements[elements.length - 1];
		}
		if(this.selectionInAllDay){
			moduleObject.quickElem = dhtml.addElement(null, "input", null, "quickappointment");
			moduleObject.quickElem.style.height = "15px";
			moduleObject.quickElem.style.width = (((this.selectionRange.length) * element.offsetWidth)-8)+"px";
			moduleObject.quickElem.style.top = (element.offsetHeight - 23) + "px";
			moduleObject.quickElem.style.left = (element.offsetLeft) +"px";
			moduleObject.quickElem.setAttribute("type", "text");
			element.parentNode.appendChild(moduleObject.quickElem);
		}else{
			if(startElement && endElement){
				var startday = startElement.parentNode.id.split("_")[1];
				var endday = endElement.parentNode.id.split("_")[1]
			}

			// check if selection is spreaded over more than one day.
			if(startElement && endElement && (startday != endday)){
				var stTimeIndex = parseInt(startElement.id.split("_")[2].replace("time",""));
				var enTimeIndex = parseInt(endElement.id.split("_")[2].replace("time",""));
				var totalDays = (endday - startday) + 1;

				// check if the appointment is below 24 hrs and more than 1 day. 
				if(stTimeIndex > (enTimeIndex+this.cellDefaultTime) && (totalDays == 2)){
					//creation of first element for quick multiday appointment cretion
					var startDayElements = dhtml.getElementsByClassNameInElement(dhtml.getElementById("day_"+startday),"selection", "div");
					moduleObject.quickElem = dhtml.addElement(startElement, "textarea", null, "quickappointment");
					moduleObject.quickElem.style.width = (element.offsetWidth-6)+"px";
					moduleObject.quickElem.style.height = ((startDayElements.length * element.offsetHeight)-10)+"px";

					//creation of second (div) element for quick multiday appointment cretion
					var endDayElements = dhtml.getElementsByClassNameInElement(dhtml.getElementById("day_"+endday),"selection", "div");
					moduleObject.quickElem.secondPart = dhtml.addElement(endDayElements[0], "div", null, "quickappointment");
					moduleObject.quickElem.secondPart.style.width = (element.offsetWidth-6)+"px";
					moduleObject.quickElem.secondPart.style.height = ((endDayElements.length * element.offsetHeight)-6)+"px";
					moduleObject.quickElem.secondPart.innerHTML = NBSP;
					moduleObject.quickElem.secondPart.style.display = "block";
				}else{
					// selection is spreaded over multiple days, add the quickElem in allDay slot
					moduleObject.quickElem = dhtml.addElement(null, "input", null, "quickappointment");
					moduleObject.quickElem.style.height = "14px";
					moduleObject.quickElem.style.width = ((totalDays * element.offsetWidth))+"px";
					moduleObject.quickElem.style.left = (startday * element.offsetWidth) +"px";
					moduleObject.quickElem.setAttribute("type", "text");
					moduleObject.quickElem.style.top = (this.alldayElement.offsetHeight - 23) + "px";
					this.alldayEventElement.appendChild(moduleObject.quickElem);
				}
			}else{
				moduleObject.quickElem = dhtml.addElement(element, "textarea", null, "quickappointment");
				moduleObject.quickElem.style.height = ((this.selectionRange.length * element.offsetHeight)-10)+"px";
				moduleObject.quickElem.style.width = (element.offsetWidth-6)+"px";
			}
		}
		
		moduleObject.quickElem.offsetDay =  element.offsetDay;
		moduleObject.quickElem.offsetTime = element.offsetTime;
		dhtml.addEvent(moduleObject, moduleObject.quickElem, "keyup", eventQuickAppointmentKey);
		dhtml.addEvent(moduleObject, moduleObject.quickElem, "mousedown", eventQuickAppointmentMouse);
		dhtml.addEvent(moduleObject, moduleObject.quickElem, "mouseup", eventQuickAppointmentMouse);
		
		// workaround for Firefox 1.5 bug with autocomplete and emtpy string: "'Permission denied to get property XULElement.selectedIndex' when calling method: [nsIAutoCompletePopup::selectedIndex]"
		moduleObject.quickElem.setAttribute("autocomplete","off");
		moduleObject.quickElem.style.zIndex = 99;
	} 

	if (moduleObject.quickElem){
		
		/**
		 * Note: In IE, giving focus manually to an element changes 'scrollTop' of whichever parentElement is having scroll. 
		 * So,it looks like scroll jumps when an element is selected.
		 */
		if (days){
			//Get scrollTop into temporary variable and assign it back to scrolling element, after giving focus.
			var daysscrollTop = days.scrollTop;
			moduleObject.quickElem.focus();
			days.scrollTop = daysscrollTop;
		}
	}
	
	if(typeof this.keyPressed != "undefined" && (this.quickElem && this.quickElem.value != "")){
		if(this.keyPressed.length >1)
			moduleObject.quickElem.value=this.keyPressed.join("");
		else
			moduleObject.quickElem.value=this.keyPressed[0];
		this.keyPressed = null;
	}
	
}

function getTimeOffsetByPixelOffset(firstElement, offset)
{
	// Loop through the target elements and check if the endTarget is between
	// the element height.
	while(firstElement.offsetTop < offset) 
	{
		firstElement = firstElement.nextSibling;
	}

	endTargetTime = firstElement.offsetTime;
	
	return endTargetTime;
}

/**
 * Opening a create appointment dialog with startdate filled in.
 */
function eventCalendarDayViewDblClick(moduleObject, element, event){

	//deselect selected item
	eventCalendarDayViewOnClick(moduleObject, false, event);

	// delete quickElem only if it exists and isn't the same time
	if (moduleObject.quickElem && moduleObject.quickElem.getAttribute("timestamp")!=element.id){
	    event = new Object();
	    
		event.keyCode = 13;
		eventQuickAppointmentKey(moduleObject, moduleObject.quickElem, event);

		if(typeof moduleObject.quickElem != "undefined" && moduleObject.quickElem && typeof moduleObject.quickElem.secondPart != "undefined" && moduleObject.quickElem.secondPart)
			dhtml.deleteElement(moduleObject.quickElem.secondPart);

		dhtml.deleteElement(moduleObject.quickElem);
		moduleObject.quickElem = null;
	}
	var viewObject = moduleObject.viewController.viewObject;
	// open the window for either creating or editing the appointment.
	moduleObject.openCreateAppointmentDialog(false, viewObject.selectionInAllDay);
	viewObject.clearSelection();
	viewObject.resetSelection();
}



function eventQuickAppointmentKey(moduleObject, element, event)
{
	if (moduleObject.quickElem){
		switch (event.keyCode) {
			case 13: // ENTER Key
				var subject = moduleObject.quickElem.value;
				var viewObject = moduleObject.viewController.viewObject;
				var startEndTime = viewObject.getStartEndTimeOfSelection();
				var reminderTime;
				if(webclient.settings.get("calendar/reminder","true") == "true"){
					// while creating an quick allday appointment, default reminder is set then we need to set reminder time to 18hrs or else to default value.
					reminderTime = viewObject.selectionInAllDay? "1080" : webclient.settings.get("calendar/reminder_minutes", 15);
				}
				if (subject.trim()!=""){
					moduleObject.createAppointment(startEndTime[0], startEndTime[1], subject, null, null, null, reminderTime, (viewObject.selectionInAllDay)?1:0);
				}

				if(typeof moduleObject.quickElem != "undefined" && moduleObject.quickElem && typeof moduleObject.quickElem.secondPart != "undefined" && moduleObject.quickElem.secondPart)
					dhtml.deleteElement(moduleObject.quickElem.secondPart);

				dhtml.deleteElement(moduleObject.quickElem);
				moduleObject.quickElem = null;
				viewObject.clearSelection();
				viewObject.resetSelection();
				break;
			case 27: // ESCAPE Key
				if(typeof moduleObject.quickElem != "undefined" && moduleObject.quickElem && typeof moduleObject.quickElem.secondPart != "undefined" && moduleObject.quickElem.secondPart)
					dhtml.deleteElement(moduleObject.quickElem.secondPart);

				dhtml.deleteElement(moduleObject.quickElem);
				moduleObject.quickElem = null;
				break;
			
		}
	}
}

/**
 * Event Function which will get fired on mousedown/mouseup on quick appointment.
 *		 It allows user to use scroll bar properly, and stop the bubbling of parent element's events.
 * @param moduleObject object Module Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventQuickAppointmentMouse(moduleObject, element, event){
	event.stopPropagation();
}

// Called with moduleObject == module, NOT this view object.
function eventAppointmentListDragDropTarget(moduleObject, targetElement, element, event)
{
	/**
	 * If MR appointment is moved by attandee than show user an alert message.
	 * If MR is just saved (not send) then requestsent attribute (MeetingRequestWasSent Property) is set to '0'
	 * If MR was sent to recpients then requestsent attribute (MeetingRequestWasSent Property) is set to '1'
	 * so if request is not sent to recipients then dont ask to update them while saving MR.
	 */
	if(element && element.meetingrequest && element.meetingrequest == "3" && element.requestsent && element.requestsent == "1") { // MR attendee
		if (!confirm(_("You are changing details for a meeting on your calendar while you are not the meeting organizer. If the organizer sends a meeting update, your changes will be lost. Do you want to continue?"))) {
			moduleObject.list();
			return;
		}
	}
	// We only use the targetElement to see which day we are in, for the start/end we
	// just look at the dragged element's height and location.
	var offsetDay = targetElement.offsetDay;
	
	var starttime = false;
	var endtime = false;
	
	// Date (day-month-year 00:00 hours)
	var dayStart = moduleObject.viewController.viewObject.days[offsetDay];
	
	// Parse start hour and start minute		
	var subject = element.subject;
	var location = element.location;
	
	// Get start/endtime just by looking at the pixel height of the top and bottom of
	// the element we are dropping
	var topOffset = element.offsetTop;
	var bottomOffset = element.offsetTop + element.clientHeight;
	var firstTargetElement = dhtml.getElementById("day_" + offsetDay + "_time0");

	var startTimeOffset = getTimeOffsetByPixelOffset(firstTargetElement, topOffset);
	var endTimeOffset = getTimeOffsetByPixelOffset(firstTargetElement, bottomOffset);

	var actionType = element.getAttribute("action");
	var starttime = false;
	var endtime = false;

	// Set stattime and endtime for the appointment.
	if(actionType != null && actionType == "resize-bottom") { 
		endtime = new Date(dayStart);
		endtime.addUnixTimeStampToDate(endTimeOffset * 60);
		starttime = new Date(element.start * 1000);
	} else if(actionType != null && actionType == "resize-top") {
		starttime = new Date(dayStart);
		starttime.addUnixTimeStampToDate(startTimeOffset * 60);
		endtime = new Date(element.end * 1000);
	} else {
		// when moving an appointment, its duration time should not be changed
		starttime = new Date(dayStart);
		starttime.addUnixTimeStampToDate(startTimeOffset * 60);

		endtime = new Date(dayStart);
		var duration = parseInt(element.end, 10) - parseInt(element.start, 10);
		if(!isNaN(duration)) {
			endtime.addUnixTimeStampToDate(duration + (startTimeOffset * 60));
		} else {
			// fallback to old way (not needed)
			endtime.addUnixTimeStampToDate(endTimeOffset * 60);
		}
	}

	var props = new Object();
	props["store"] = moduleObject.storeid;
	props["parent_entryid"] = moduleObject.entryid;
	props["entryid"] = moduleObject.entryids[element.id];

	if(!props["entryid"])
		props["entryid"] = element.id.substr(0,element.id.indexOf("_"));

	if(starttime) {
		props["startdate"] = parseInt(starttime.getTime()/1000, 10);
		props["commonstart"] = props["startdate"];
		props["remindertime"] = props["startdate"];
		if (element.reminderSet) props["reminder_minutes"] = element.reminderMinutes;
	} else if(element && element.positionStartTime) {
		starttime = new Date(element.positionStartTime * 1000);
		//starttime.setTime(element.positionStartTime * 1000)
	}

	// Set flagdueby property to show reminder on correcttime.
	if(props["startdate"] && props["entryid"] && moduleObject.itemProps[props["entryid"]])
		props["flagdueby"] = props["startdate"] - (moduleObject.itemProps[props["entryid"]].reminder_minutes*60);

	/**
	 * if user stretches the appointment to previous day or before the startdate
	 * then it should become zero minute appointment.
	 */
	if(endtime && starttime && endtime <= starttime) {
		endtime = starttime;
	}
	if(endtime) {
		props["duedate"] = parseInt(endtime.getTime()/1000, 10);
		props["commonend"] = props["duedate"];
	}

	if(starttime && endtime) {
		props["duration"] = (props["duedate"] - props["startdate"]) / 60;
	}
	

	var basedate = (element.getAttribute("basedate") == "undefined" || !element.getAttribute("basedate")?false:element.getAttribute("basedate"));
	if(basedate) {
		props["basedate"] = basedate;
	}

	var occurrenceBaseDate = element.id.substring(element.id.indexOf("_")+1);
	var send = false;
	var moveOccAllowFlag = true;
	if (element.meetingrequest && parseInt(element.meetingrequest, 10) == 1) { // 1 = olResponseOrganized
		// Check whether there are any recepients in MR or not.
		if( endtime.getTime()>(new Date().getTime()) && element.requestsent && element.requestsent == "1") {
			send = confirm(_("Would you like to send an update to the attendees regarding changes to this meeting?"));
		}
	}else if(basedate){
		moveOccAllowFlag = confirmAppointMoved(moduleObject, element.offsetDay, targetElement.offsetDay);
	}
	if(moveOccAllowFlag){
		moduleObject.save(props, send);
		moduleObject.viewController.updateItem(element);
	}
}

function eventDayViewClickPrev(moduleObject, element, event)
{
	var datepickerlistmodule = webclient.getModulesByName("datepickerlistmodule")[0];
	var numberOfDays = (moduleObject.duedate-moduleObject.startdate)/ONE_DAY;
	var startdate;

	if(numberOfDays == 1){
		startdate = new Date(moduleObject.startdate);
		startdate.addDays(-1);
	} else {
		startdate = new Date(moduleObject.startdate);
		startdate.addDays(-7);
	}
	datepickerlistmodule.changeMonth(startdate.getMonth() + 1, startdate.getFullYear(), true);
	datepickerlistmodule.changeSelectedDate(startdate.getTime(),true);
}

function eventDayViewClickNext(moduleObject, element, event)
{
	var datepickerlistmodule = webclient.getModulesByName("datepickerlistmodule")[0];
	var numberOfDays = (moduleObject.duedate-moduleObject.startdate)/ONE_DAY;
	var startdate;
	
	if(numberOfDays == 1){
		startdate = new Date(moduleObject.startdate);
		startdate.addDays(1);
	} else {
		startdate = new Date(moduleObject.startdate);
		startdate.addDays(7);
	}
	
	datepickerlistmodule.changeMonth(startdate.getMonth() + 1, startdate.getFullYear(), true);
	datepickerlistmodule.changeSelectedDate(startdate.getTime(),true);
}

function eventCalendarDayViewOnClick(moduleObject, element, event)
{
	//deselect quick appointment
	if(moduleObject.quickElem){
		// submit new appointment when it has some text, the keyhandler will do this when keyCode == 13
		event = new Object();
		
		event.keyCode = 13;
		eventQuickAppointmentKey(moduleObject, moduleObject.quickElem, event);
	}
	
	//deselect the selection on timeline if any	
	if(event.type != "dblclick"){
		moduleObject.viewController.viewObject.clearSelection();
		moduleObject.viewController.viewObject.resetSelection();
	}

	//deselect old appointments
	if(moduleObject.selectedMessages){
		while(moduleObject.selectedMessages.length > 0){
			var item = dhtml.getElementById(moduleObject.selectedMessages.pop());
			dhtml.removeClassName(item,"selected");
		}
	}
	
	//select appointment
	if(element && element.getAttribute("multipledayelementcount") && typeof element.getAttribute("multipledayelementcount") != "undefined"){
		if(moduleObject.viewController.viewObject.itemElementLookup && typeof moduleObject.viewController.viewObject.itemElementLookup[moduleObject.entryids[element.id]] != "undefined"){
			var elemObjEntryIds = moduleObject.viewController.viewObject.itemElementLookup[moduleObject.entryids[element.id]];
			for(var elCnt = 0;elCnt<elemObjEntryIds.length;elCnt++){
				moduleObject.selectedMessages.push(elemObjEntryIds[elCnt]);
				dhtml.addClassName(dhtml.getElementById(elemObjEntryIds[elCnt]),"selected");
			}
		}
	}else{
		if(element && typeof moduleObject.entryids[element.id] != "undefined"){
			moduleObject.selectedMessages.push(element.id);
			dhtml.addClassName(element,"selected");
		}
	}
}

function eventCalendarDayViewKeyboard(moduleObject, element, event)
{
	if (typeof moduleObject != "undefined"){

		if (event.type == "keydown"){

			// get the right element
			if (moduleObject && moduleObject instanceof ListModule && typeof moduleObject.selectedMessages[0] != "undefined"){
				element = dhtml.getElementById(moduleObject.selectedMessages[0]);
			
				if (element){
					switch (event.keyCode){
						case 13: // ENTER
							if(!moduleObject.quickElem){
								viewObject.createQuickAppointmentElement(moduleObject, event.keyCode);
							}
							break;
						case 46: // DELETE
							moduleObject.deleteAppointments(moduleObject.getSelectedMessages(), true); // prompt for occurrence
							break;
						default :
							return false;
							break;
					}
				}
			}
		}
	}
}



//Event Functions for quick appointment goes here

/**
 * Event Function which will get fired on mousedown on any cell on timeline.
 * @param moduleObject object Module Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventCalendarDayViewSelectionMouseDown(moduleObject, element, event){
	if(!event)
		event = window.event;
	var viewObject = moduleObject.viewController.viewObject;
	var button;
	if(event.button) //check FF
		button = event.button;
	else if(event.which) //check IE
		button = event.which;
	switch (button){
		case 0: // left mouse click
		case 1:
			// clear the previous selected messages.
			eventCalendarDayViewOnClick(moduleObject, element, event);
			// clear the selection of selected cells on timeline
			viewObject.clearSelection();
			viewObject.resetSelection();
			// set the starting point of selection
			viewObject.startSelection(element);
			break;
		case 2: // right mouse click
			// check if right click is on selected area or not, if cell is not selected, 
			// than clear the selected selection and select that particular cell and show the menu.
			if(!dhtml.hasClassName(element, "selection")){
				viewObject.startSelection(element);
				viewObject.moveSelection(element);
			}
			if(viewObject.selectionInAllDay)
				eventCalendarDayViewAllDaySelectionMouseUp(viewObject, element, event);
			else
				eventCalendarDayViewSelectionMouseUp(viewObject, element.parentNode, event);
			//show menu
			viewObject.showSelectionContextMenu(element, event.clientX, event.clientY);
			break;
	}
}

/**
 * Event Function which will get fired on mousemove on day elements.
 * @param viewObject object View Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventCalendarDayViewSelectionMouseMove(viewObject, element, event){
	if(viewObject.isSelecting()){
		viewObject.clearSelection();
		var currElement = viewObject.translateYCoordToTimelineCell(element, event.clientY);
		if(currElement)
			viewObject.moveSelection(currElement);
	}
}

/**
 * Event Function which will get fired on mouseup on day element on timeline.
 * @param viewObject object View Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventCalendarDayViewSelectionMouseUp(viewObject, element, event){
	if(viewObject.isSelecting()){
		var currElement = viewObject.translateYCoordToTimelineCell(element, event.clientY);
		viewObject.endSelection(currElement);
		viewObject.moveSelection(currElement);
	}
}

/**
 * Event Function which will get fired on mouseup on allday cells on all day timeline.
 * @param viewObject object View Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventCalendarDayViewAllDaySelectionMouseUp(viewObject, element, event){
	if(viewObject.isSelecting()){
		viewObject.endSelection(element);
		viewObject.moveSelection(element);
	}
}

/**
 * Event Function which will get fired on mousemove on allday timeline elements.
 * @param viewObject object View Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventCalendarDayViewAllDaySelMouseMove(viewObject, element, event){
	if(viewObject.isSelecting()){
		viewObject.clearSelection();
		var currElement = viewObject.translateXCoordToAllDayTimelineCell(element, event.clientX);
		viewObject.moveSelection(currElement);
	}
}

/**
 * Event Function which will get fired on mouseup on body.
 * @param viewObject object View Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventCalendarStopSelectionMouseUp(viewObject, element, event){
	this.selectingTimerange = false;
}

/**
 * Event Function which will get fired on choosing "create appointment" option from context menu on selection.
 * @param moduleObject object Module Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventCalendarDayContextMenuClickCreateAppointment(moduleObject, element, event){
	var viewObject = moduleObject.viewController.viewObject;
	// hide the context menu if it is present
	if(dhtml.getElementById("contextmenu")){
		dhtml.getElementById("contextmenu").style.display = "none";
	}
	moduleObject.openCreateAppointmentDialog(false, viewObject.selectionInAllDay);
	viewObject.clearSelection();
	viewObject.resetSelection();
}

/**
 * Event Function which will get fired on choosing "create meetingrequest" option from context menu on selection.
 * @param moduleObject object Module Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventCalendarDayContextMenuCreateMeetingRequestClick(moduleObject, element, event){
	var viewObject = moduleObject.viewController.viewObject;
	// hide the context menu if it is present
	if(dhtml.getElementById("contextmenu")){
		dhtml.getElementById("contextmenu").style.display = "none";
	}
	moduleObject.openCreateAppointmentDialog("meeting", viewObject.selectionInAllDay);
	viewObject.clearSelection();
	viewObject.resetSelection();
}

/**
 * Function handles the keyboard events on time line's cell elements
 * @param moduleObject object Module Object
 * @param element object HTML element object
 * @param event object Event Pbject
 */
function eventCalendarDayViewSelectionKeyUp(moduleObject, element, event){
	var viewObject = moduleObject.viewController.viewObject;
	switch (event.keyCode){
		case 27: // Escape key
			viewObject.clearSelection();
			viewObject.resetSelection();
			break;
		case 46: // delete key
			if(moduleObject.getSelectedMessages() != "")
				moduleObject.deleteAppointments(moduleObject.getSelectedMessages(), true); // prompt for occurrence
			break;
		case  9: // tab key
		case 16: // Alt
		case 17: // ctr key
		case 18: // alt key
			break;
		case 37: // navigations keys
			eventDayViewClickPrev(moduleObject);
			break;
		case 39: // navigations keys
			eventDayViewClickNext(moduleObject);
			break;
		case 38: // navigations keys
		case 40: // navigations keys
			break;
		case 13: // ENTER
			if(!moduleObject.quickElem && viewObject.selectionStart)
				moduleObject.openCreateAppointmentDialog(false);
			viewObject.clearSelection();
			break;
		default:
			if(!moduleObject.quickElem){
				if(viewObject.createQuickAppointmentElement)
					viewObject.createQuickAppointmentElement(moduleObject, event.keyCode);
			}
			break;
	}
}
