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
PrintCalendarMonthView.prototype = new PrintView;
PrintCalendarMonthView.prototype.constructor = PrintCalendarMonthView;
PrintCalendarMonthView.superclass = PrintView.prototype;

/**
 * This view can be used to print a list of appointments in month view
 * @constructor
 * @param Int moduleID The PrintListModule ID
 * @param HtmlElement The html element where all elements will be appended
 * @param Object The Events object
 * @param Object The Data Passed from the main window
 */
function PrintCalendarMonthView(moduleID, element, events, data, uniqueid) {
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
PrintCalendarMonthView.prototype.initView = function() {
	this.folder_entryid = this.entryid;

	var weekElement, dayElement, spanElement, currentDay;

	// get the date of the first day in the view
	firstDayOfTheView = new Date(this.selecteddate);
	firstDayOfTheView.setDate(1);
	firstDayOfTheView = firstDayOfTheView.getStartDateOfWeek();
	currentDay = firstDayOfTheView;

	this.tableElement = dhtml.addElement(this.element, "div", "month", "");

	weekElement = dhtml.addElement(this.tableElement, "div", "month_header");
	//week# col
	dayElement = dhtml.addElement(weekElement, "div", "month_header_week_nr");
	dhtml.addElement(dayElement, "span", "", "", NBSP);
	//end week# col

	// add week header
	// FIXME: before we can have an user setting for the startday, 
	// we need support in the displaying of the items, so for now it is fixed on monday
	// var startDay = webclient.settings.get("global/calendar/weekstart",1);
	var startDay = 1;
	for(var i = startDay; i < 7; i++) {
		dayElement = dhtml.addElement(weekElement, "div", "month_header_day");
		dhtml.addElement(dayElement, "span", "", "", DAYS_SHORT[i]);
	}
	for(var i = 0; i < startDay; i++) {
		dayElement = dhtml.addElement(weekElement, "div", "month_header_day");
		dhtml.addElement(dayElement, "span", "", "", DAYS_SHORT[i]);
	}

	// begin month view
	var today = new Date();

	for(var i = 0; i < 6; i++) {
		//week row
		weekElement = dhtml.addElement(this.tableElement, "div", "week");

		//week# col
		dayElement = dhtml.addElement(weekElement, "div", "month_week_nr");
		dayElement.setAttribute("unixtime", (currentDay.getTime() / 1000));
		spanElement = dhtml.addElement(dayElement, "span", "", "", currentDay.getWeekNumber());
		//end week# col

		for (var j = 0; j < 7; j++) {
			var dayId = this.folder_entryid + "_" + (currentDay.getTime() / 1000);

			if(currentDay.getMonth() == this.selecteddate.getMonth()) {
				if(currentDay.isSameDay(new Date())) {
					dayElement = dhtml.addElement(weekElement, "div", "month_day today", dayId);
				} else {
					dayElement = dhtml.addElement(weekElement, "div", "month_day", dayId);
				}
			} else {
				dayElement = dhtml.addElement(weekElement, "div", "month_day month_other", dayId);
				dayElementBack = dhtml.addElement(weekElement, "div", "month_other_back", dayId + "_back");
				dayElementBack.innerHTML = "<img src='client/layout/img/grey_background.gif' height='100%' width='100%' />";
			}
			dayElement.setAttribute("unixtime", (currentDay.getTime() / 1000));

			//day number
			spanElement = dhtml.addElement(dayElement, "span", "day_number", "", currentDay.getDate());
			spanElement.setAttribute("unixtime", (currentDay.getTime() / 1000));
			//end day number

			currentDay.addDays(1);
		}
	}
}

/**
 * Function which is executed by Printlistmodule from addItems method
 * @param array items list of items received from the server
 * @param array properties property list
 * @param object action the XML action
 * @return array list of entryids
 */
PrintCalendarMonthView.prototype.execute = function(items, properties, action) {
	var entryids = false;

	this.element.appendChild(this.tableElement);

	for(var i = 0; i < items.length; i++) {
		if (!entryids) {
			entryids = new Object();
		}
		var item = this.createItem(items[i]);
		entryids[item["id"]] = item["entryid"];
	}

	this.resizeView();

	this.createIFrame();

	return entryids;
}

/**
 * Function will add/update an item of the print month view
 * @param item calendar item
 * @return object[id],[entryid]
 */
PrintCalendarMonthView.prototype.createItem = function(item) {
	var entry = Object();

	var unixTimeStart = item.getElementsByTagName("startdate")[0].getAttribute("unixtime");
	var unixTimeDue = item.getElementsByTagName("duedate")[0].getAttribute("unixtime");
	var startDate = new Date(unixTimeStart * 1000);
	var dueDate = new Date(unixTimeDue * 1000);
	var subject = dhtml.getTextNode(item.getElementsByTagName("subject")[0], NBSP);
	var parent_entryid = item.getElementsByTagName("parent_entryid")[0].firstChild.nodeValue;
	var alldayevent = dhtml.getTextNode(item.getElementsByTagName("alldayevent")[0], 0);
	var entryid = item.getElementsByTagName("entryid")[0].firstChild.nodeValue;
	var startTime = new Date(startDate);
	var privateAppointment = dhtml.getTextNode(item.getElementsByTagName("private")[0], 0);
	var privateSensitivity = dhtml.getTextNode(item.getElementsByTagName("sensitivity")[0], 0);

	var appointmentElement, appointmentElementBack;

	startTime.setHours(0);
	startTime.setMinutes(0);
	startTime.setSeconds(0);

	// Check how many days this event takes TODO: check DST
	var numberOfItems = Math.ceil((dueDate - startDate) / ONE_DAY);
	if(numberOfItems < 1) {
		numberOfItems = 1;
	}
	
	for(var i = 0; i < numberOfItems; i++) {
		var unixTimeId = startDate.getTime() / 1000;
		var selDay = dhtml.getElementById(parent_entryid + "_" + (startTime.getTime() / 1000));

		if(selDay) {
			appointmentElement = dhtml.addElement(selDay, "div");
			appointmentElementBack = dhtml.addElement(selDay, "div");

			appointmentElement.setAttribute("id", entryid + "_" + unixTimeId);
			appointmentElement.setAttribute("startdate", unixTimeId);
			appointmentElement.setAttribute("duedate", unixTimeDue);

			appointmentElementBack.setAttribute("id", entryid + "_" + unixTimeId + "_back");

			if(alldayevent ==  "1") {
				// allday event
				appointmentElement.className = "event_item ipm_appointment event_day_item";
				appointmentElement.setAttribute("title", subject);

				appointmentElementBack.className = "event_item_back event_day_item_back";
			} else {
				// normal event
				appointmentElement.className = "event_item ipm_appointment";
				appointmentElement.setAttribute("title", "[" + startDate.strftime(_("%H:%M")) + " - " + dueDate.strftime(_("%H:%M")) + "] " + subject);
				dhtml.addElement(appointmentElement, "span", "event_time", "", startDate.strftime(_("%H:%M")) + NBSP);

				appointmentElementBack.className = "event_item_back";
			}

			// private item
			if(privateAppointment == "1") {
				appointmentElement.innerHTML += "<img src='client/layout/img/icon_private.gif' />";
			} else if(privateSensitivity == "2") {
				appointmentElement.innerHTML += "<img src='client/layout/img/icon_private.gif' />";
			}

			// subject
			dhtml.addElement(appointmentElement, "span", "event_subject", "", subject);

			// Appointment labels
			var imageName = "label_default.gif";
			if(item.getElementsByTagName("label")[0]) {
				switch(item.getElementsByTagName("label")[0].firstChild.nodeValue) {
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
				}
			}

			appointmentElementBack.innerHTML = "<img src='client/layout/img/" + imageName + "' width='100%' height='100%' />";

			var recurring = item.getElementsByTagName("recurring")[0];
			if(recurring && recurring.firstChild) {
				// Basedate is used for saving
				var basedate = item.getElementsByTagName("basedate")[0];
				if(basedate && basedate.firstChild) {
					appointmentElement.setAttribute("basedate", basedate.getAttribute("unixtime"));
				}
			}

			entry["id"] = entryid + "_" + unixTimeId;
			entry["entryid"] = entryid;
		}

		// add one_day for more day event
		startTime.addDays(1);
	}

	return entry;
}

/**
 * Function will resize all elements in the view
 * @param	iframeWindow	iframe	iframe that is used for printing
 */
PrintCalendarMonthView.prototype.resizeView = function(iframe) {
	if(typeof iframe != "undefined") {
		var iframeDoc = iframe.document;
		var printFooterHeight = iframeDoc.getElementById("print_footer").offsetHeight;
		var printHeaderHeight = iframeDoc.getElementById("print_header").offsetHeight;
		var iframeSize = dhtml.getBrowserInnerSize(iframe);

		// change width of header & footer
		iframeDoc.getElementById("print_header").style.width = iframeSize["x"];
		iframeDoc.getElementById("print_footer").style.width = iframeSize["x"];

		// change width / height of container element
		iframeElement = iframeDoc.getElementById("print_calendar");
		var contentAreaHeight = iframeSize["y"] - (printHeaderHeight + printFooterHeight) + "px";
		iframeElement.style.height = contentAreaHeight;
		iframeElement.style.width = iframeSize["x"];
		
		var tableElement = iframeElement.firstChild;
		tableElement.style.height = contentAreaHeight;
		tableElement.style.width = iframeSize["x"];

		if(tableElement) {
			var fixedHeight = 20;
			var fixedWidth = 20;
			var flexHeight = Math.floor((tableElement.offsetHeight - fixedHeight) / 6);
			var flexWidth = Math.floor((tableElement.offsetWidth - fixedWidth) / 7);
			var divElement;

			divElement = dhtml.getElementsByClassNameInElement(tableElement, "month_header", "div");
			divElement[0].style.height = fixedHeight + "px";
			divElement[0].style.width = tableElement.offsetWidth + "px";

			divElement = dhtml.getElementsByClassNameInElement(tableElement, "month_header_week_nr", "div");
			divElement[0].style.height = fixedHeight + "px";
			divElement[0].style.width = fixedWidth + "px";

			divElement = dhtml.getElementsByClassNameInElement(tableElement, "month_header_day", "div");
			for(var i = 0; i < divElement.length - 1; i++) {
				divElement[i].style.height = fixedHeight + "px";
				divElement[i].style.width = flexWidth + "px";
			}
			divElement[divElement.length - 1].style.height = fixedHeight + "px";
			divElement[divElement.length - 1].style.width = (tableElement.offsetWidth - (divElement[divElement.length - 2].offsetLeft + flexWidth) - 2) + "px";

			divElement = dhtml.getElementsByClassNameInElement(tableElement, "month_week_nr", "div");
			for(var i = 0; i < divElement.length; i++) {
				divElement[i].style.height = flexHeight + "px";
				divElement[i].style.width = fixedWidth + "px";
			}
			
			divElement = dhtml.getElementsByClassNameInElement(tableElement, "week", "div");
			for(var i = 0; i < divElement.length; i++) {
				divElement[i].style.height = flexHeight + "px";
				divElement[i].style.width = tableElement.offsetWidth + "px";

				dayElement = dhtml.getElementsByClassNameInElement(divElement[i], "month_day", "div");
				for(var j = 0; j < dayElement.length - 1; j++) {
					dayElement[j].style.height = flexHeight + "px";
					dayElement[j].style.width = flexWidth + "px";

					if(dayElement[j].className == "month_day month_other") {
						dayElementBack = dhtml.getElementById(dayElement[j].id + "_back", "div", divElement[i]);
						dayElementBack.style.height = dayElement[j].clientHeight + "px";
						dayElementBack.style.width = dayElement[j].clientWidth + "px";
						dayElementBack.style.top = dayElement[j].offsetTop + "px";
						dayElementBack.style.left = dayElement[j].offsetLeft + "px";
					}

					// position divs that will give colors to appointment using images
					var appointmentElements = dhtml.getElementsByClassNameInElement(dayElement[j], "ipm_appointment", "div");
					for(var k = 0; k < appointmentElements.length; k++) {
						var appointmentElement = appointmentElements[k];
						var appointmentElementBack = dhtml.getElementById(appointmentElement.id + "_back", "div", dayElement[j]);

						appointmentElement.style.height = "14px";
						appointmentElementBack.style.top = appointmentElement.offsetTop + "px";
						appointmentElementBack.style.height = appointmentElement.clientHeight + "px";
						appointmentElementBack.style.width = appointmentElement.clientWidth + "px";
					}
				}

				var lastElementInRow = dayElement[dayElement.length - 1];
				lastElementInRow.style.height = flexHeight + "px";
				lastElementInRow.style.width = (flexWidth - 8) + "px";

				if(lastElementInRow.className == "month_day month_other") {
					dayElementBack = dhtml.getElementById(lastElementInRow.id + "_back", "div", divElement[i]);
					dayElementBack.style.height = lastElementInRow.offsetHeight + "px";
					dayElementBack.style.width = lastElementInRow.offsetWidth + "px";
					dayElementBack.style.top = lastElementInRow.offsetTop + "px";
					dayElementBack.style.left = lastElementInRow.offsetLeft + "px";
				}

				var appointmentElements = dhtml.getElementsByClassNameInElement(lastElementInRow, "ipm_appointment", "div");
				for(var k = 0; k < appointmentElements.length; k++) {
					var appointmentElement = appointmentElements[k];
					var appointmentElementBack = dhtml.getElementById(appointmentElement.id + "_back", "div", lastElementInRow);

					appointmentElement.style.height = "14px";
					appointmentElementBack.style.top = appointmentElement.offsetTop + "px";
					appointmentElementBack.style.height = appointmentElement.clientHeight + "px";
					appointmentElementBack.style.width = appointmentElement.clientWidth + "px";
				}
			}
		}

		// convert percentage width into pixels for printing
		var width = dhtml.getElementsByClassName("month_header", "div")[0].style.width;
		dhtml.getElementById("print_footer").style.width = width;
		dhtml.getElementById("print_header").style.width = width;
		this.element.style.width = width;
	} else {
		var menubarHeight = dhtml.getElementById("menubar").offsetHeight + dhtml.getElementById("menubar").offsetTop;
		var titleHeight = dhtml.getElementsByClassName("title")[0].offsetHeight;
		var bodyHeight = dhtml.getBrowserInnerSize()["y"];
		var bodyWidth = dhtml.getBrowserInnerSize()["x"];

		var dialogContentHeight = bodyHeight - (menubarHeight + titleHeight);
		dhtml.getElementById("dialog_content").style.height = dialogContentHeight + "px";

		var printFooterHeight = dhtml.getElementById("print_footer").offsetHeight;
		var printHeaderHeight = dhtml.getElementById("print_header").offsetHeight;

		var printCalendarHeight = dialogContentHeight - (printHeaderHeight + printFooterHeight) - 7;
		this.element.style.height = printCalendarHeight + "px";
		this.element.style.width = dhtml.getElementById("dialog_content").offsetWidth + "px";

		this.tableElement.style.height = printCalendarHeight + "px";
		this.tableElement.style.width = this.element.offsetWidth;

		if(this.tableElement) {
			var fixedHeight = 20;
			var fixedWidth = 20;
			var flexHeight = Math.floor((this.tableElement.offsetHeight - fixedHeight) / 6);
			var flexWidth = Math.floor((this.tableElement.offsetWidth - fixedWidth) / 7);
			var divElement;

			divElement = dhtml.getElementsByClassNameInElement(this.tableElement, "month_header", "div");
			divElement[0].style.height = fixedHeight + "px";
			divElement[0].style.width = this.tableElement.offsetWidth + "px";

			divElement = dhtml.getElementsByClassNameInElement(this.tableElement, "month_header_week_nr", "div");
			divElement[0].style.height = fixedHeight + "px";
			divElement[0].style.width = fixedWidth + "px";

			divElement = dhtml.getElementsByClassNameInElement(this.tableElement, "month_header_day", "div");
			for(var i = 0; i < divElement.length - 1; i++) {
				divElement[i].style.height = fixedHeight + "px";
				divElement[i].style.width = flexWidth + "px";
			}
			divElement[divElement.length - 1].style.height = fixedHeight + "px";
			divElement[divElement.length - 1].style.width = (this.tableElement.offsetWidth - (divElement[divElement.length - 2].offsetLeft + flexWidth) - 2) + "px";

			divElement = dhtml.getElementsByClassNameInElement(this.tableElement, "month_week_nr", "div");
			for(var i = 0; i < divElement.length; i++) {
				divElement[i].style.height = flexHeight + "px";
				divElement[i].style.width = fixedWidth + "px";
			}
			
			divElement = dhtml.getElementsByClassNameInElement(this.tableElement, "week", "div");
			for(var i = 0; i < divElement.length; i++) {
				divElement[i].style.height = flexHeight + "px";
				divElement[i].style.width = this.tableElement.offsetWidth + "px";

				dayElement = dhtml.getElementsByClassNameInElement(divElement[i], "month_day", "div");
				for(var j = 0; j < dayElement.length - 1; j++) {
					dayElement[j].style.height = flexHeight + "px";
					dayElement[j].style.width = flexWidth + "px";

					if(dayElement[j].className == "month_day month_other") {
						dayElementBack = dhtml.getElementById(dayElement[j].id + "_back", "div", divElement[i]);
						dayElementBack.style.height = dayElement[j].clientHeight + "px";
						dayElementBack.style.width = dayElement[j].clientWidth + "px";
						dayElementBack.style.top = dayElement[j].offsetTop + "px";
						dayElementBack.style.left = dayElement[j].offsetLeft + "px";
					}

					// position divs that will give colors to appointment using images
					var appointmentElements = dhtml.getElementsByClassNameInElement(dayElement[j], "ipm_appointment", "div");
					for(var k = 0; k < appointmentElements.length; k++) {
						var appointmentElement = appointmentElements[k];
						var appointmentElementBack = dhtml.getElementById(appointmentElement.id + "_back", "div", dayElement[j]);

						appointmentElement.style.height = "14px";
						appointmentElementBack.style.top = appointmentElement.offsetTop + "px";
						appointmentElementBack.style.height = appointmentElement.clientHeight + "px";
						appointmentElementBack.style.width = appointmentElement.clientWidth + "px";
					}
				}

				var lastElementInRow = dayElement[dayElement.length - 1];
				lastElementInRow.style.height = flexHeight + "px";
				lastElementInRow.style.width = (flexWidth - 8) + "px";

				if(lastElementInRow.className == "month_day month_other") {
					dayElementBack = dhtml.getElementById(lastElementInRow.id + "_back", "div", divElement[i]);
					dayElementBack.style.height = lastElementInRow.offsetHeight + "px";
					dayElementBack.style.width = lastElementInRow.offsetWidth + "px";
					dayElementBack.style.top = lastElementInRow.offsetTop + "px";
					dayElementBack.style.left = lastElementInRow.offsetLeft + "px";
				}

				var appointmentElements = dhtml.getElementsByClassNameInElement(lastElementInRow, "ipm_appointment", "div");
				for(var k = 0; k < appointmentElements.length; k++) {
					var appointmentElement = appointmentElements[k];
					var appointmentElementBack = dhtml.getElementById(appointmentElement.id + "_back", "div", lastElementInRow);

					appointmentElement.style.height = "14px";
					appointmentElementBack.style.top = appointmentElement.offsetTop + "px";
					appointmentElementBack.style.height = appointmentElement.clientHeight + "px";
					appointmentElementBack.style.width = appointmentElement.clientWidth + "px";
				}
			}
		}

		// convert percentage width into pixels for printing
		var width = dhtml.getElementsByClassName("month_header", "div")[0].style.width;
		dhtml.getElementById("print_footer").style.width = width;
		dhtml.getElementById("print_header").style.width = width;
		this.element.style.width = width;
	}

	this.hideOverlappingItems(iframeDoc);
}

/** 
 * Function is used to add a label in month view if all items are 
 * not visible in particular cell of month view
 * @param	iframeDocument	iframeDoc	iframe that is used for printing
 */
PrintCalendarMonthView.prototype.hideOverlappingItems = function(iframeDoc) {
	var containerElement = this.element;
	if(typeof iframeDoc != "undefined") {
		containerElement = iframeDoc.getElementById("print_calendar");
	}

	var dayElement = dhtml.getElementsByClassNameInElement(containerElement, "month_day", "div");

	for(var i = 0; i < dayElement.length; i++) {
		var items = dhtml.getElementsByClassNameInElement(dayElement[i], "event_item", "div");
		var dayHeight = dayElement[i].offsetHeight;
		var dayCurrentHeight = (items.length * 14) + 18;
		var maxItems = Math.floor((dayHeight - 18) / 14) - 1;

		// remove more_items label
		var moreItem = dhtml.getElementsByClassNameInElement(dayElement[i], "more_items", "div")[0];
		if(moreItem){
			dhtml.deleteElement(moreItem);
		}

		// hide/show items
		var moreItemCount = 0;
		for(var j = 0; j < items.length; j++) {
			if(j < maxItems) {
				items[j].style.display = "block";
			} else {
				items[j].style.display = "none";
				moreItemCount++;
			}

			var item_back = dhtml.getElementById(items[j].id + "_back", "div", dayElement[i]);
			if(j < maxItems && item_back != null) {
				item_back.style.display = "block";
			} else {
				item_back.style.display = "none";
			}
		}

		// show more_items label
		if(items.length > maxItems && items.length > 0) {
			var moreItem = dhtml.addElement(dayElement[i], "div", "more_items");
			moreItem.setAttribute("unixtime", dayElement[i].getAttribute("unixtime"));
			moreItem.innerHTML = _("More items...");
		}
	}
}