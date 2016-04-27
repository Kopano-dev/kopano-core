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
* Generates Print Preview for Calendar Appointment List
*/
PrintView.prototype = new View;
PrintView.prototype.constructor = PrintView;
PrintView.superclass = View.prototype;

/**
 * This view can be used to print a list of the appointments
 * @constructor
 * @param Int moduleID The PrintListModule ID
 * @param HtmlElement The html element where all elements will be appended
 * @param Object The Events object
 * @param Object The Data Passed from the main window
 */
function PrintView(moduleID, element, events, data, uniqueid) {
	if(arguments.length > 0) {
		this.init(moduleID, element, events, data, uniqueid);
	}
}

/**
 * this function does all the creating stuff
 * @param Int moduleID The PrintListModule ID
 * @param HtmlElement The html element where all elements will be appended
 * @param Object The Events object
 * @param Object The Data Passed from the main window
 */
PrintView.prototype.init = function(moduleID, element, events, data, uniqueid) {
	// initialize data
	this.element = element;
	this.module = webclient.getModule(moduleID);
	this.data = data;
	
	this.entryid = data["entryid"];
	this.moduleID = data["moduleID"];
	this.view = data["view"];
	this.startdate = data["restriction"]["startdate"];
	this.duedate = data["restriction"]["duedate"];
	this.selecteddate = data["restriction"]["selecteddate"];
}

/**
 * Function which intializes the view
 */ 
PrintView.prototype.initView = function() {

}

/**
 * Function will add items to the view
 * @param {Object} items Object with items
 * @param {Array} properties property list
 * @param {Object} action the action tag
 * @return {Array} list of entryids
 */
PrintView.prototype.execute = function(items, properties, action) {

}

/**
 * Function will add/update an item of the month view
 * there are two options 	1: createItem(item) for new items
 * 							2: createItem(item,element) for moved items
 * @param item calendar item
 * @param element existing div element
 * @return object[id],[entryid]
 */
PrintView.prototype.createItem = function(item,element) {
	var entry = Object();
	// get properties
	var startDate = new Date(item.getElementsByTagName("startdate")[0].getAttribute("unixtime")*1000);
	var dueDate = new Date(item.getElementsByTagName("duedate")[0].getAttribute("unixtime")*1000);
	var unixTimeStart = item.getElementsByTagName("startdate")[0].getAttribute("unixtime");
	var unixTimeDue = item.getElementsByTagName("duedate")[0].getAttribute("unixtime");	
	var subject = item.getElementsByTagName("subject")[0].hasChildNodes()?item.getElementsByTagName("subject")[0].firstChild.nodeValue:NBSP;
	var location = dhtml.getTextNode(item.getElementsByTagName("location")[0],"");
	var parent_entryid = dhtml.getTextNode(item.getElementsByTagName("parent_entryid")[0],"");
	var alldayevent = dhtml.getTextNode(item.getElementsByTagName("alldayevent")[0],"");
	var entryid = dhtml.getTextNode(item.getElementsByTagName("entryid")[0],"");
	var label =  dhtml.getTextNode(item.getElementsByTagName("label")[0],"");
	var recurring = dhtml.getTextNode(item.getElementsByTagName("recurring")[0],"");
	var basedate = item.getElementsByTagName("basedate")[0]?item.getElementsByTagName("basedate")[0].getAttribute("unixtime"):false;	
	var exception = item.getElementsByTagName("exception")[0];
	var privateAppointment = dhtml.getTextNode(item.getElementsByTagName("private")[0], 0);
	var privateSensitivity = dhtml.getTextNode(item.getElementsByTagName("sensitivity")[0], 0);

	var startTime = new Date(startDate);
	var appointmentElement;

	startTime.setHours(0);
	startTime.setMinutes(0);
	startTime.setSeconds(0);

	// Check how many days this event takes TODO: check DST
	var numberOfItems = Math.ceil((dueDate-startDate)/ONE_DAY);
	if(numberOfItems < 1) {
		numberOfItems = 1;
	}
	for(var i = 0; i < numberOfItems; i++) {
		var unixTimeId = startDate.getTime()/1000;
		var parent = dhtml.getElementById("day_" + this.moduleID + "_" + startTime.getTime()/1000);
		if(parent) {
			if(element) {
				dhtml.deleteElement(element);
			}
			appointmentElement = dhtml.addElement(parent, "div", "event");

			// set some properties
			appointmentElement.id = entryid + "_" + unixTimeId;
			appointmentElement.startdate = startDate.getTime()/1000;
			appointmentElement.duedate = dueDate.getTime()/1000;
			appointmentElement.style.height = 14 + "px";

			// allday event
			if(alldayevent ==  "1") {
				if(this.view == "week") {
					dhtml.addClassName(appointmentElement, "event_day_item");
				} else {
					dhtml.addElement(appointmentElement, "span", "event_time", "", _("All Day"));
					dhtml.addElement(appointmentElement, "span", "spacer_list", "", NBSP);
					dhtml.addElement(appointmentElement, "span", "spacer_list", "", NBSP);
				}
			} else {
				if(this.view == "week") {
					dhtml.addElement(appointmentElement, "span", "event_time", "", startDate.strftime(_("%H:%M"))+" "+dueDate.strftime(_("%H:%M")));
				} else {				
					dhtml.addElement(appointmentElement, "span", "event_time", "", startDate.strftime(_("%H:%M"))+" - "+dueDate.strftime(_("%H:%M")));
					dhtml.addElement(appointmentElement, "span", "spacer_list", "", NBSP);
				}
			}

			// private item
			if(privateAppointment == "1") {
				appointmentElement.innerHTML += "<img src='client/layout/img/icon_private.gif' />";
			} else if(privateSensitivity == "2") {
				appointmentElement.innerHTML += "<img src='client/layout/img/icon_private.gif' />";
			}

			// Recurring items
			if(recurring.length > 0 && basedate.length > 0) {
				appointmentElement.setAttribute("basedate", basedate);
				if(exception && exception.firstChild) {
					appointmentElement.innerHTML += "<img src='client/layout/img/icon_recurring_changed.gif' />";
				} else if(recurring == "1") {
					appointmentElement.innerHTML += "<img src='client/layout/img/icon_recurring.gif' />";
				}
			}

			// meeting
			var meeting = item.getElementsByTagName("meeting")[0];
			if(meeting && meeting.firstChild) {
				var responseStatus = dhtml.getTextNode(item.getElementsByTagName("responsestatus")[0], 0);
				appointmentElement.meetingrequest = responseStatus; // store responseStatus in DOM tree
				switch(meeting.firstChild.nodeValue) {
					case "1":
					case "3":
					case "5":
						appointmentElement.innerHTML += "<img src='client/layout/img/icon_group.gif' />";
						break;
				}
			}

			// add subject
			if(location.length > 0) {
				dhtml.addElement(appointmentElement, "span", "event_subject", "", subject + " (" + location + ")");
			} else {
				dhtml.addElement(appointmentElement, "span", "event_subject", "", subject);
			}

			// only view label colors in week view not in list view
			if(this.view == "week") {
				// Appointment labels
				var imageName = "label_default.gif";
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
				}

				// add div with image, behind appointment div to show it as background color
				// imageName != "label_default.gif" ==> because we don't need to show white background
				// in week view unlike day view
				if(alldayevent == "1" || (alldayevent != "1" && imageName != "label_default.gif")) {
					AppointmentElementBack = dhtml.addElement(parent, "div", "event_back", appointmentElement.id + "_back");
					AppointmentElementBack.style.height = appointmentElement.clientHeight + "px";
					AppointmentElementBack.style.top = appointmentElement.offsetTop - 2 + "px";
					AppointmentElementBack.style.left = appointmentElement.offsetLeft + "px";
					AppointmentElementBack.innerHTML = "<img src='client/layout/img/" + imageName + "' width='100%' height='100%' />";
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
 */
PrintView.prototype.resizeView = function(iframe) {

}

/**
* this function will create the header & footer for all the printing views
*/
PrintView.prototype.createHeaderFooter = function() {

	this.header = dhtml.getElementById("print_header");
	this.footer = dhtml.getElementById("print_footer");

	if(this.view != "list") {
		// header table
		this.tbl = dhtml.addElement(this.header, "table", "headertbl", "");
		this.tbl.setAttribute("cellspacing", "0px");
		this.tbl.setAttribute("style", "position: relative;");
		this.tbl.setAttribute("width", "100%");

		this.tblBody = dhtml.addElement(this.tbl, "tbody");

		this.tblRow = dhtml.addElement(this.tblBody, "tr", "headerrow", "");

		this.tblCellLeft = dhtml.addElement(this.tblRow, "td", "left_cell", "");
		this.tblCellMiddle = dhtml.addElement(this.tblRow, "td", "middle_cell", "");
		this.tblCellRight = dhtml.addElement(this.tblRow, "td", "right_cell", "");

		// create view dependant header
		var startDate = new Date(this.startdate);
		var dueDate = new Date(this.duedate);
		var selectedDate = new Date(this.selecteddate);

		switch(this.view) {
			case "day":
				this.tblCellLeft.innerHTML = "<b>" + selectedDate.strftime( _("%d %B %Y") ) 
											+ "</b><br />" + selectedDate.strftime( _("%A") );
				break;
			case "7days":
				this.tblCellLeft.innerHTML = "<b>" + startDate.strftime( _("%d %B") ) + " - " +
											dueDate.strftime( _("%d %B") ) + "</b>";
				break;
			case "workweek":
				this.tblCellLeft.innerHTML = "<b>" + startDate.strftime( _("%d %B") ) + " - " +
											dueDate.strftime( _("%d %B") ) + "</b>";
				break;
			case "week":
				this.tblCellLeft.innerHTML = "<b>" + startDate.strftime( _("%d %B") ) + " - " +
											dueDate.strftime( _("%d %B") ) + "</b>";
				break;
			case "month":
				this.tblCellLeft.innerHTML = "<b>" + selectedDate.strftime( _("%B %Y") ) + "</b>";
				break;
			default:
				this.tblCellLeft.innerHTML = "";
		}

		// create datepicker objects for previous and next month
		events = new Object();
		events["previousmonth"] = function() {};
		events["nextmonth"] = function() {};
		events["day"] = function() {};
		
		this.dpPrev = new CalendarDatePickerView(-1, this.tblCellMiddle, events, {month:selectedDate.getMonth(), year:selectedDate.getFullYear()});
		// get next month
		selectedDate.setMonth(selectedDate.getMonth() + 1);
		this.dpNext = new CalendarDatePickerView(-1, this.tblCellRight, events, {month:selectedDate.getMonth(), year:selectedDate.getFullYear()});
	} else {
		this.header.style.display = "none";
	}
	
	// footer table	
	this.tbl = dhtml.addElement(this.footer, "table", "footertbl", "");
	this.tblBody = dhtml.addElement(this.tbl, "tbody");

	this.tblRow = dhtml.addElement(this.tblBody, "tr", "footerrow", "");

	this.tblCellLeft = dhtml.addElement(this.tblRow, "td", "left_footer_cell", "", NBSP);
	this.tblCellMiddle = dhtml.addElement(this.tblRow, "td", "middle_footer_cell", "", NBSP);
	this.tblCellRight = dhtml.addElement(this.tblRow, "td", "right_footer_cell", "", NBSP);

	// current date for footer
	var currentDate = new Date();

	this.tblCellLeft.innerHTML = webclient.fullname;
	this.tblCellMiddle.innerHTML = "1";
	this.tblCellRight.innerHTML = currentDate.strftime( _("%d/%m/%Y %H:%M") );
}

/**
 * Function which shows a load message in the view
 */ 
PrintView.prototype.loadMessage = function() {
	dhtml.deleteAllChildren(this.element);

	this.element.innerHTML = "<center>" + _("Loading") + "...</center>";
	document.body.style.cursor = "wait";
}

/**
 * Function which deletes the load message in the view
 */
PrintView.prototype.deleteLoadMessage = function() {
	dhtml.deleteAllChildren(this.element);

	// initialize view dependant layout
	this.initView();

	// create header & footer for all views
	this.createHeaderFooter();

	document.body.style.cursor = "default";
}

// can not print directly from page because contents are bigger then window size
// so create a iframe copied all contents to it and printed that iframe only
PrintView.prototype.createIFrame = function() {
	if (document.createElement && (iframe =	document.createElement('iframe'))) {
		iframe.id = iframe.name = "printing_frame";
		iframe.scrolling = "no";
		// hardcoded value for A4/Letter size paper (210 x 297 mm / 216 × 279 mm)
		iframe.style.height = "259mm";
		iframe.style.width = "210mm";
		document.body.appendChild(iframe);
	}

	if (iframe) {
		var iframeDoc;
		iframeDoc = window.frames[iframe.name].document;

		if (iframeDoc) {
			iframeDoc.open();
			iframeDoc.write('<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">');
			iframeDoc.write('<html>');
			iframeDoc.write('	<head>');
			iframeDoc.write('		<title>' + _('Zarafa WebAccess') + ' - ' + webclient.fullname + '</title>');
			// need to inlclude css again because iframe can not access parent document's css
			iframeDoc.write('		<link rel="stylesheet" type="text/css" href="client/layout/css/calendar-print.css" />');
			iframeDoc.write('	</head>');
			iframeDoc.write('	<body>');

			iframeDoc.write(dhtml.getElementById("dialog_content").innerHTML);

			iframeDoc.write('	</body>');
			iframeDoc.write('</html>');
			iframeDoc.close();
		}

		var viewObject = this;		// Fix loss-of-scope in setTimeout function
		setTimeout(function() {
			// resize frame that is displayed to user
			viewObject.resizeView();
			// resize frame that is used for printing
			viewObject.resizeView(window.frames["printing_frame"]);
		}, 1000);
	}
}