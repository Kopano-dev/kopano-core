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
* Generates Print Preview for Calendar Views
*/
PrintCalendarDayView.prototype = new PrintView;
PrintCalendarDayView.prototype.constructor = PrintCalendarDayView;
PrintCalendarDayView.superclass = PrintView.prototype;

// these functions are directly used from calendar.day.view.js file
PrintCalendarDayView.prototype.positionItems = CalendarDayView.prototype.positionItems;
PrintCalendarDayView.prototype.addItemWithoutReposition = CalendarDayView.prototype.addItemWithoutReposition;
PrintCalendarDayView.prototype.isAllDayItem = CalendarDayView.prototype.isAllDayItem;
PrintCalendarDayView.prototype.sortItems = CalendarDayView.prototype.sortItems;
PrintCalendarDayView.prototype.sortAlldayItems = CalendarDayView.prototype.sortAlldayItems;
PrintCalendarDayView.prototype.getOverLapping = CalendarDayView.prototype.getOverLapping;
PrintCalendarDayView.prototype.propagateMaxSlot = CalendarDayView.prototype.propagateMaxSlot;

/**
 * This view can be used to print a list of appointments in day view
 * @constructor
 * @param Int moduleID The PrintListModule ID
 * @param HtmlElement The html element where all elements will be appended
 * @param Object The Events object
 * @param Object The Data Passed from the main window
 */
function PrintCalendarDayView(moduleID, element, events, data, uniqueid) {
	if(arguments.length > 0) {
		this.init(moduleID, element, events, data, uniqueid);
	}

	this.days = new Array();
	for(var i = new Date(this.startdate); i.getTime() < this.duedate; i.addDays(1)) {
		this.days.push(i.getTime());
	}
}

/**
 * Function which intializes the view
 */
PrintCalendarDayView.prototype.initView = function() {
	this.headerElement = dhtml.addElement(this.element, "div", "header", this.moduleID + "_header");
	this.headerElement.style.width = 100 + "%";
	this.weekNumberElement = dhtml.addElement(this.headerElement, "span", "", "week_number");
	this.alldayEventElement = dhtml.addElement(this.headerElement, "div", "alldayevent");
	this.alldayElement = dhtml.addElement(this.alldayEventElement, "div", "allcontainer");
	this.timelinetopElement = dhtml.addElement(this.headerElement, "div", "timelinetop");

	// get settings for workday start and end hour
	this.workdayStart = webclient.settings.get("calendar/workdaystart");
	this.workdayEnd = webclient.settings.get("calendar/workdayend");

	if(!this.workdayStart) {
		this.workdayStart = 9*60;
	}

	if(!this.workdayEnd) {
		this.workdayEnd = 17*60;
	}

	var timeSlots = (this.workdayEnd / 60) - (this.workdayStart / 60);

	this.workdayStart = Math.floor(this.workdayStart/60) * 60;
	this.workdayEnd = Math.ceil(this.workdayEnd/60) * 60;

	for(var i = 0; i < this.data["days"]; i++) {
		var dayElement = dhtml.addElement(this.headerElement, "div", "date");
	}

	this.daysElement = dhtml.addElement(this.element, "div", false, "days");

	var timelineElement = dhtml.addElement(this.daysElement, "div", "timeline");

	// calendar/vsize ==> calendar vertical size
	// this setting indicates rowsize of half hour slot in calendar
	// it can have values as 1 - small, 2 - medium, 3 - large
	this.rowSize = parseInt(webclient.settings.get("calendar/vsize", 2));

	if(timeSlots > 12) {
		this.rowSize = 1;
	} else if(timeSlots < 8) {
		this.rowSize = 3;
	}

	for(var i = (this.workdayStart / 60); i < (this.workdayEnd / 60); i++) {
		var timeElement = dhtml.addElement(timelineElement, "div", "time");
		switch(this.rowSize) {
			case 1: timeElement.style.height = "25px"; break;   // Small
			case 3: timeElement.style.height = "65px"; break;   // Large
			default: timeElement.style.height = "45px"; break;  // Normal
		}
		timeElement.innerHTML = i + "<sup>00</sup>";
	}

	// extra elements for displaying appointments that is not in the working hours
	// if timeslots are 24 then no need to add extra slots
	if(timeSlots <= 23) {
		for(var k = 0; k < 4; k++) {
			var extraTimeElement = dhtml.addElement(timelineElement, "div", "time extra");

			switch(this.rowSize) {
				case 1: extraTimeElement.style.height = "12px"; break;   // Small 
				case 3: extraTimeElement.style.height = "32px"; break;   // Large 
				default: extraTimeElement.style.height = "22px"; break;  // Normal
			}
		}
	}

	for(var i = 0; i < this.data["days"]; i++) {
		this.dayElement = dhtml.addElement(this.daysElement, "div", "day", "day_" + i);

		for(var j = (this.workdayStart / 60); j < (this.workdayEnd / 60); j++) {
			var firstHalfElement = dhtml.addElement(this.dayElement, "div", "half_hour_cell", "day_" + i + "_time" + (j * 60));
			firstHalfElement.offsetTime = j * 60;
			firstHalfElement.offsetDay = i;

			switch(this.rowSize) {
				case 1: firstHalfElement.style.height = "13px"; break;   // Small 
				case 3: firstHalfElement.style.height = "33px"; break;   // Large 
				default: firstHalfElement.style.height = "23px"; break;  // Normal
			}

			var secondHalfElement = dhtml.addElement(this.dayElement, "div", "half_hour_cell", "day_" + i + "_time" + (j * 60 + 30));
			secondHalfElement.offsetTime = j * 60 + 30;
			secondHalfElement.offsetDay = i;

			switch(this.rowSize) {
				case 1: secondHalfElement.style.height = "12px"; break;   // Small 
				case 3: secondHalfElement.style.height = "32px"; break;   // Large 
				default: secondHalfElement.style.height = "22px"; break;  // Normal
			}
		}

		// extra elements for displaying appointments that is not in the working hours
		if(timeSlots <= 23) {
			for(var k = 0; k < 4 ; k++) {
				var extraHalfElement = dhtml.addElement(this.dayElement, "div", "half_hour_cell extra", "day_" + i + "_extra" + k);
				switch(this.rowSize) {
					case 1: extraHalfElement.style.height = "14px"; break;   // Small 
					case 3: extraHalfElement.style.height = "34px"; break;   // Large 
					default: extraHalfElement.style.height = "24px"; break;  // Normal
				}
				extraHalfElement.setAttribute("slotfree", "0");
			}
		}

		// Add an element at 24:00 which we use for positioning items that end at 00:00. You can't actually
		// see it.
		var endElem = dhtml.addElement(this.dayElement, "div", "", "day_" + i + "_time1440");
		endElem.offsetDay = i;
		endElem.offsetTime = 24 * 60;
	}

	this.setDayHeaders();
}

// Sets the date-dependant parts of the view
PrintCalendarDayView.prototype.setDayHeaders = function() {
	// Set the week number (wk XX)
	this.weekNumberElement.innerHTML = _("wk") + " " + this.selecteddate.getWeekNumber();

	var days = dhtml.getElementsByClassNameInElement(this.headerElement, "date", "div", true);

	// Set the day headers (mon 12 jan)
	for(var i = 0; i < days.length; i++) {
		var date = new Date(this.days[i]);
		days[i].innerHTML = date.strftime( _("%a %d %h") );
	}
}

/**
 * Function which is executed by Printlistmodule from addItems method
 * @param array items list of items received from the server
 * @param array properties property list
 * @param object action the XML action
 * @return array list of entryids
 */
PrintCalendarDayView.prototype.execute = function(items, properties, action) {
	var entryids = new Array();

	for(var i = 0; i < items.length; i++) {
		var itemStart = parseInt(items[i].getElementsByTagName("startdate")[0].getAttribute("unixtime"), 10) * 1000;
		var itemDue = parseInt(items[i].getElementsByTagName("duedate")[0].getAttribute("unixtime"), 10) * 1000;

		if((itemStart >= this.startdate && itemDue <= this.duedate) || 
			(itemStart < this.duedate && itemDue > this.duedate) ||
			(itemStart < this.startdate && itemDue > this.startdate)) {
			var entry = this.addItemWithoutReposition(items[i], properties, action);
		}
		if(entry) {
			entryids[entry['id']] = entry['entryid'];
		}
	}

	this.positionItems();

	// We must resize because there may be new allday items which change the size of the main
	// pane
	this.resizeView();

	this.createIFrame();

	return entryids;
}

// Create a standard item in the appointment item list (but don't position it)
PrintCalendarDayView.prototype.createItem = function(daysElement, appointment, item, daynr) {
	if (typeof daysElement == "undefined") {
		return; // it could happen that this function is called for an item that doesn't belong on this date
				// in that case daysElement is undefined so we ignore this item
	}

	appointment.style.display = "none";
	var className = "ipm_appointment appointment";
	var label = dhtml.getTextNode(item.getElementsByTagName("label")[0]);

	// background colors are removed at printing time
	// so used images instead of it
	switch(label) {
		case "1": imageName = "label_important.gif"; break;
		case "2": imageName = "label_work.gif"; break;
		case "3": imageName = "label_personal.gif"; break;
		case "4": imageName = "label_holiday.gif"; break;
		case "5": imageName = "label_required.gif"; break;
		case "6": imageName = "label_travel_required.gif"; break;
		case "7": imageName = "label_prepare_required.gif"; break;
		case "8": imageName = "label_birthday.gif"; break;
		case "9": imageName = "label_special_date.gif"; break;
		case "10": imageName = "label_phone_interview.gif"; break;
		default: imageName = "label_default.gif"; break;
	}

	var busystatus = item.getElementsByTagName("busystatus")[0];
	if(busystatus && busystatus.firstChild) {
		switch(busystatus.firstChild.nodeValue) {
			case "0": className += " free"; break;
			case "1": className += " tentative"; break;
			case "3": className += " outofoffice"; break;
			default: className += " busy"; break;
		}
	} else {
		className += " busy";
	}

	appointment.className = className;

	var privateAppointment = item.getElementsByTagName("private")[0];
	var privateSensitivity = item.getElementsByTagName("sensitivity")[0];
	if(privateAppointment && privateAppointment.firstChild && privateSensitivity && privateSensitivity.firstChild) {
		if(privateAppointment.firstChild.nodeValue == "1") {
			appointment.innerHTML += "<img src='client/layout/img/icon_private.gif' />";
		} else if(privateSensitivity.firstChild.nodeValue == "2") {
			appointment.innerHTML += "<img src='client/layout/img/icon_private.gif' />";
		}
	}

	var reminderSet = item.getElementsByTagName("reminder")[0];
	if (reminderSet && reminderSet.firstChild && reminderSet.firstChild.nodeValue == "1"){
		appointment.innerHTML += "<img src='client/layout/img/icon_reminder.gif' />";
	}

	var meeting = item.getElementsByTagName("meeting")[0];
	if(meeting && meeting.firstChild) {
		var responseStatus = dhtml.getTextNode(item.getElementsByTagName("responsestatus")[0], 0);
		appointment.meetingrequest = responseStatus; // store responseStatus in DOM tree
		switch(meeting.firstChild.nodeValue)
		{
			case "1":
			case "3":
			case "5":
				appointment.innerHTML += "<img src='client/layout/img/icon_group.gif' />";
				break;
		}
	}

	var recurring = item.getElementsByTagName("recurring")[0];
	if(recurring && recurring.firstChild) {
		var exception = item.getElementsByTagName("exception")[0];

		if(exception && exception.firstChild) {
			appointment.innerHTML += "<img src='client/layout/img/icon_recurring_changed.gif' />";
		} else if(recurring.firstChild.nodeValue == "1") {
			appointment.innerHTML += "<img src='client/layout/img/icon_recurring.gif' />";
		}

		// Basedate is used for saving
		var basedate = item.getElementsByTagName("basedate")[0];
		if(basedate && basedate.firstChild) {
			appointment.setAttribute("basedate", basedate.getAttribute("unixtime"));
		}
	}

	var subject = item.getElementsByTagName("subject")[0];
	if(subject && subject.firstChild) {
		appointment.innerHTML += subject.firstChild.nodeValue;
		appointment.subject = subject.firstChild.nodeValue;
	} else {
		appointment.innerHTML += "&nbsp;";
		appointment.subject = NBSP;
	}

	var location = item.getElementsByTagName("location")[0];
	if(location && location.firstChild) {
		appointment.innerHTML += " (" + location.firstChild.nodeValue + ")";
		appointment.location = location.firstChild.nodeValue;
	}

	var startdate = item.getElementsByTagName("startdate")[0];
	var duedate = item.getElementsByTagName("duedate")[0];

	appointment.start = startdate.getAttribute("unixtime");
	appointment.end = duedate.getAttribute("unixtime");

	if(startdate && startdate.firstChild && duedate && duedate.firstChild) {
		var starttime = startdate.getAttribute("unixtime");
		var duetime = duedate.getAttribute("unixtime");
		if (duetime == starttime) {
			duetime = (starttime * 1) + 60; // make duetime +1 minute when equals to starttime
		}

		var start_date = new Date(starttime * 1000);
		var due_date = new Date(duetime * 1000);

		appointment.offsetStartTime = start_date.getHours() * 60 + start_date.getMinutes();
		appointment.offsetDay = daynr;

		var startDateId = daysElement.id + "_time" + (start_date.getHours() * 60);
		if(start_date.getMinutes() >= 30) {
			startDateId = daysElement.id + "_time" + (start_date.getHours() * 60 + 30);
		}

		appointment.offsetEndTime = due_date.getHours() * 60 + due_date.getMinutes();

		var dueDateId = daysElement.id + "_time" + (due_date.getHours() * 60);
		if(due_date.getMinutes() > 0 && due_date.getMinutes() <= 30) {
			dueDateId = daysElement.id + "_time" + (due_date.getHours() * 60 + 30);
		} else if(due_date.getMinutes() > 30) {
			dueDateId = daysElement.id + "_time" + ((due_date.getHours() + 1) * 60);
		}

		var startDateElem = dhtml.getElementById(startDateId);
		var endDateElem = dhtml.getElementById(dueDateId);

		// if appointment ends at work day end hour then it will not get end date element
		// we have to use first extra element as end date element
		if(endDateElem == null) {
			endDateElem = dhtml.getElementById("day_" + daynr + "_extra" + 0);
		}

		if(appointment.offsetStartTime >= this.workdayStart && appointment.offsetEndTime <= this.workdayEnd && appointment.offsetEndTime != 0) {
			appointment.style.top = startDateElem.offsetTop + "px";
			appointment.style.width = (daysElement.clientWidth - 26) + "px";

			var height = endDateElem.offsetTop - startDateElem.offsetTop - 6;
		} else {
			for(var i = 0; i < 4; i++) {
				var extraElement = dhtml.getElementById("day_" + daynr + "_extra" + i);
				if(extraElement.getAttribute("slotfree") == "0") {
					appointment.style.top = extraElement.offsetTop + "px";
					appointment.style.width = (daysElement.clientWidth - 26) + "px";
					extraElement.setAttribute("slotfree", "1");
					break;
				}

				if(i == 3) {
					// if appointments outside working hours is more than 4
					var extraTimeElement = dhtml.addElement(dhtml.getElementsByClassName("timeline")[0], "div", "time extra");
					switch(this.rowSize) {
					    case 1: extraTimeElement.style.height = "12px"; break;   // Small 
					    case 3: extraTimeElement.style.height = "32px"; break;   // Large 
					    default: extraTimeElement.style.height = "22px"; break;  // Normal
					}
					extraTimeElement.style.position = "absolute";

					var extraHalfElement = dhtml.addElement(dhtml.getElementById("day_" + daynr), "div", "half_hour_cell extra");
					switch(this.rowSize) {
						case 1: extraHalfElement.style.height = "13px"; break;   // Small 
						case 3: extraHalfElement.style.height = "33px"; break;   // Large 
						default: extraHalfElement.style.height = "23px"; break;  // Normal
					}

					appointment.style.top = extraHalfElement.offsetTop + "px";
					appointment.style.width = (daysElement.clientWidth - 26) + "px";
				}
			}

			var height = extraElement.clientHeight - 4;

			// also show time in the appointment subject if not in working hours
			var content = appointment.innerHTML;
			appointment.innerHTML = start_date.toTime() + " - " + due_date.toTime() + " " + content;
		}

		appointment.style.height = height + "px";

		// This attribute is used for saving. 
		appointment.setAttribute("starttime", startDateId);
	}

	appointment.type = "normal";

	// extra element is added in every appointment to use image as a background color
	backElement = dhtml.addElement(daysElement, "div", "appointment_back", appointment.id + "_back");
	backElement.style.top = appointment.style.top;
	backElement.style.height = height + 4 + "px"; // 4 px for the padding
	backElement.style.width = appointment.style.width;
	backElement.style.left = appointment.style.left;
	backElement.innerHTML = "<img src='client/layout/img/" + imageName + "' width='100%' height='100%' />";
}

// Create an all day item (but don't position it)
PrintCalendarDayView.prototype.createAllDayItem = function(appointElement, item) {
	// reset events thay may still be there when converting from a 'normal' appointment
	appointElement.style.cursor = "";

	var subject = item.getElementsByTagName("subject")[0];
	var startUnixtime = parseInt(item.getElementsByTagName("startdate")[0].getAttribute("unixtime"),10);
	var endUnixtime = parseInt(item.getElementsByTagName("duedate")[0].getAttribute("unixtime"),10);
	var duration = dhtml.getTextNode(item.getElementsByTagName("duration")[0]);
	var label = dhtml.getTextNode(item.getElementsByTagName("label")[0]);
	var location = dhtml.getTextNode(item.getElementsByTagName("location")[0],"");
	if(location){
		location = " ("+location+")";
	}

	if(startUnixtime >= this.duedate/1000 || endUnixtime <= this.startdate/1000) {
		return; // Item we received is not within our viewing range
	}

	// body of item
	dhtml.deleteAllChildren(appointElement);
	dhtml.addTextNode(appointElement,dhtml.getTextNode(subject, NBSP)+location);
	appointElement.setAttribute("start", startUnixtime);
	appointElement.setAttribute("end", endUnixtime);

	// style
	appointElement.className = "";
	dhtml.addClassName(appointElement, "ipm_appointment allday_appointment");

	// background colors are removed at printing time
	// so used images instead of it
	switch(label) {
		case "1": imageName = "label_important.gif"; break;
		case "2": imageName = "label_work.gif"; break;
		case "3": imageName = "label_personal.gif"; break;
		case "4": imageName = "label_holiday.gif"; break;
		case "5": imageName = "label_required.gif"; break;
		case "6": imageName = "label_travel_required.gif"; break;
		case "7": imageName = "label_prepare_required.gif"; break;
		case "8": imageName = "label_birthday.gif"; break;
		case "9": imageName = "label_special_date.gif"; break;
		case "10": imageName = "label_phone_interview.gif"; break;
		default: imageName = "label_default.gif"; break;
	}

	// Remember basedate
	var basedate = item.getElementsByTagName("basedate")[0];
	if(basedate && basedate.firstChild) {
		appointElement.setAttribute("basedate", basedate.getAttribute("unixtime"));
	}

	appointElement.type = "allday";

	// extra element is added in every appointment to use image as a background color
	backElement = dhtml.addElement(this.alldayElement, "div", "allday_appointment_back", appointElement.id + "_back");
	backElement.innerHTML = "<img src='client/layout/img/" + imageName + "' width='100%' height='100%' />";
}

/**
 * Function will resize all elements in the view
 */
PrintCalendarDayView.prototype.resizeView = function() {
	var menubarHeight = dhtml.getElementById("menubar").offsetHeight + dhtml.getElementById("menubar").offsetTop;
	var titleHeight = dhtml.getElementsByClassName("title")[0].offsetHeight;
	var bodyHeight = dhtml.getBrowserInnerSize()["y"];

	var dialogContentHeight = bodyHeight - (menubarHeight + titleHeight) + 12;
	dhtml.getElementById("dialog_content").style.height = dialogContentHeight + "px";

	var printFooterHeight = dhtml.getElementById("print_footer").offsetHeight;
	var printHeaderHeight = dhtml.getElementById("print_header").offsetHeight;
	var dayHeaderHeight = dhtml.getElementById("1_header").offsetHeight;

	var printCalendarHeight = dialogContentHeight - (printHeaderHeight + printFooterHeight);
	this.element.style.height = printCalendarHeight + "px";

	// Set main viewing window size
	this.daysElement.style.width = (this.headerElement.offsetWidth - 1 > 0?this.headerElement.offsetWidth : 3) + "px";
	this.daysElement.style.height = (printCalendarHeight - dayHeaderHeight - 1 > 0 ? printCalendarHeight - dayHeaderHeight - 1:3) + "px";

	// to set width of header, footer and main table
	this.element.style.width = this.daysElement.style.width;
	dhtml.getElementById("print_header").style.width = this.daysElement.style.width;
	dhtml.getElementById("print_footer").style.width = this.daysElement.style.width;
	
	// Set day widths
	// Header
	var days = dhtml.getElementsByClassNameInElement(this.headerElement, "date", "div", true);
	var width = (this.headerElement.offsetWidth - 50) / this.data["days"];
	var leftPosition = 50;

	if(width > 0) {
		for(var i = 0; i < this.data["days"]; i++) {
			days[i].style.width = width + "px";
			days[i].style.left = leftPosition + "px";

			leftPosition += days[i].clientWidth + 1;
		}

		// Days
		var days = dhtml.getElementsByClassNameInElement(this.daysElement, "day", "div", true);
		leftPosition = 50;
		for(var i = 0; i < this.data["days"]; i++) {
			days[i].style.width = width + "px";
			days[i].style.left = leftPosition + "px";

			if(i == this.data["days"] - 1) {
				days[i].style.width = (width + (this.headerElement.clientWidth - leftPosition - (width))) + "px";
			}

			leftPosition += days[i].clientWidth + 1;
		}
	}

	this.positionItems();
}

/**
 * @todo implement multi dayevents in this function
 */ 
PrintCalendarDayView.prototype.positionItemsOnDay = function(dayElement) {
	var items = dhtml.getElementsByClassNameInElement(dayElement, "appointment", "div", true);
	var items_back = dhtml.getElementsByClassNameInElement(dayElement, "appointment_back", "div", true);

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
	for(var i = 0; i < items.length; i++) {
		/**
		 * Calculate the position times. To prevent 15min appointments from 
		 * overlapping the positioning time is rounded to the fill the complete 
		 * half hour slot.
		 */
		
		items[i].positionStartTime = (new Date(items[i].start * 1000)).floorHalfhour() / 1000;
		items[i].positionEndTime = (new Date(items[i].end * 1000)).ceilHalfhour() / 1000;

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
	for(i = 0; i < items.length; i++) {
		var item = items[i];
		var item_back = items_back[i];
		var slot = slots[items[i].id];

		var width = (item.parentNode.clientWidth / slot.maxslot) - 20;
		item.style.width = width + "px";
		item.style.left = (slot.slot * (width + 20)) + "px";
		item.style.display = "";

		item_back.style.width = width + 11 + "px"; // 11 px for padding
		item_back.style.left = item.style.left;
	}
}

// Reposition all allday items
PrintCalendarDayView.prototype.positionAllAlldayItems = function() {
	// position all "all day events"
	var dayList = new Object();
	var headerElement = this.headerElement;
	var alldayeventElement = this.alldayEventElement;
	var timelinetopElement = this.timelinetopElement;

	var headDays = dhtml.getElementsByClassNameInElement(this.headerElement, "date", "div", true);
	var allDayItems = dhtml.getElementsByClassNameInElement(this.alldayElement, "allday_appointment", "div", true);
	var allDayItems_back = dhtml.getElementsByClassNameInElement(this.alldayElement, "allday_appointment_back", "div", true);

	allDayItems.sort(this.sortAlldayItems);

	for(i in headDays) {
		dayList[i] = new Object();
	}

	for(i in allDayItems) {
		var realStart = allDayItems[i].getAttribute("start");
		var realEnd = allDayItems[i].getAttribute("end");
		var startTime = timeToZero(parseInt(realStart,10));
		var endTime = timeToZero(parseInt(realEnd,10));
		if(startTime < Math.floor(this.startdate/1000)){
			startTime = Math.floor(this.startdate/1000);
		}
		if(endTime > Math.floor(this.duedate/1000)) {
			endTime = Math.floor(this.duedate/1000);
		}
		/**
		 * If the IF-statement is true the appointment runs from 0:00 day X till
		 * 0:00 day Y and can be considered a real ALL DAY event. When this is 
		 * not the case and the appointment runs from mid-day till mid-day the 
		 * duration has to be calculated differently. The mid-day appointment 
		 * has to be placed over all days it covers.
		 */
		if(realStart == startTime && realEnd == endTime) {
			var durationInDays = Math.ceil((endTime-startTime)/86400);
		} else {
			var durationInDays = Math.ceil((endTime+(24*60*60)-startTime)/86400);
		}

		var startDay = Math.floor((startTime - (this.days[0]/1000))/86400);

		var headerDayElement = dhtml.getElementsByClassNameInElement(this.headerElement, "date", "div", true)[startDay];
		var posTop = 0;

		// You could have an item with 'AllDayEvent == true' but start == end.
		if(durationInDays == 0) {
			durationInDays = 1;
		}

		for(var j = 0; j < durationInDays; j++) {
			if(dayList[startDay + j]) {
				while(dayList[startDay + j][posTop] == "used") {
					posTop++;
				}
			}
		}
		// flag used in dayList
		for(var j = 0;j < durationInDays; j++) {
			if(!dayList[startDay + j]) {
				dayList[startDay + j] = new Object();
			}
			dayList[startDay + j][posTop] = "used";
		}
		
		if(headerDayElement) {
			// set style of item
			allDayItems[i].style.left = headerDayElement.offsetLeft - 50 + "px";
			allDayItems[i].style.width = (headerDayElement.offsetWidth * durationInDays) - 2 + "px";
			allDayItems[i].style.height = "14px";
			allDayItems[i].style.top = (allDayItems[i].offsetHeight + 2) * posTop + "px";

			// set back elements' style
			allDayItems_back[i].style.left = allDayItems[i].style.left;
			allDayItems_back[i].style.width = allDayItems[i].style.width;
			allDayItems_back[i].style.height = "14px";
			allDayItems_back[i].style.top = allDayItems[i].style.top;
		}
		// size the headers
		posTop++;
		if(alldayeventElement.offsetHeight < posTop * 19) {
			headerElement.style.height = posTop * 19 + 17 + "px";
			timelinetopElement.style.height = posTop * 19 + 16 + "px";
			alldayeventElement.style.height = posTop * 19 + "px";
		}
	}
}