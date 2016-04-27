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
 * TodayCalendarView
 * @type  View
 * @classDescription  This view is use for todaycalendarlistmodule 
 */
TodayCalendarView.prototype = new View;
TodayCalendarView.prototype.constructor = TodayCalendarView;
TodayCalendarView.superclass = View.prototype;

function TodayCalendarView(moduleID, element, events, data, uniqueid)
{	
	if(arguments.length > 0) {
		this.init(moduleID, element, events, data, uniqueid);
	}
}

/**
 * @constructor 
 * @param moduleID is the parent module
 * @param element is the element where the view should be placed
 * @param events are the events that should be handled for this view
 * @param data are any view-specific data
 */
TodayCalendarView.prototype.init = function(moduleID, element, events, data)
{
	this.element = element;
		
	this.moduleID = moduleID;
	this.events = events;
	this.data = data;
	
	this.initView();
}

/**
 * Function which intializes the view
 */ 
TodayCalendarView.prototype.initView = function()
{
}

/**
 * Function which adds items to the view.
 * @param object items the items
 * @param array properties property list
 * @param object action the action tag
 */ 
TodayCalendarView.prototype.execute = function(items, properties, action)
{	
	//convert object in to array to use shift() function.
	var rows = new Array();
	for (var i = 0; i < items.length; i++) {
		rows[i] = items[i];
	}
	// Empty the Element
	this.element.innerHTML = "";

	//create the all day elements.
	this.addDays();

	//fetch all data from XML into a JS array
	var data = this.fetchDataFromXML(rows);

	//call the function which creates the data into table.
	this.addAppointmentData(data);
}

/**
 * Function which create days elements to the view.
 */
TodayCalendarView.prototype.addDays = function(){
	
	var date = new Date();
	var today = date.getDay();
	/**
	 * create an array of days which will start from today and will end up 7 days later.
	 * i.e. -> if today is Wednesday, then we need to show a week "Wednesday - Tuesday".
	 */
	var showDays = new Array();
	for( var i in DAYS){
		showDays[i] = DAYS[i];
	}
	var tempStartingDays = showDays.splice(today,showDays.length+1);
	var tempEndingDays = showDays.slice(0,today);
	showDays = tempStartingDays.concat(tempEndingDays);

	for (var i in showDays ){
		var dayElement = dhtml.addElement(this.element, "div", "align hide", "day_"+showDays[i].toLowerCase());
		var dayElementHeader = dhtml.addElement(dayElement, "div", "header");
		dhtml.addTextNode(dayElementHeader, (i == 0) ? _("Today") : showDays[i]);
		var dayElementData = dhtml.addElement(dayElement, "div", "container", "container_"+showDays[i].toLowerCase());
	}
}

/**
 * Function which fetch all data from XML object into a JS Object.
 * @param array rows XML Data array of objects
 * @return array dataObj JS object
 */
TodayCalendarView.prototype.fetchDataFromXML = function(rows){
	var dataObj = new Array();
	for (var i in rows)
	{
		var row = rows[i];
		var jsObj = new Object();
		jsObj["entryid"] = dhtml.getXMLValue(row, "entryid", false);
		jsObj["subject"] = dhtml.getXMLValue(row, "subject", "");
		jsObj["location"] = dhtml.getXMLValue(row, "location", "");
		jsObj["alldayevent"] = dhtml.getXMLValue(row, "alldayevent", false);
		jsObj["startdate"] = dhtml.getXMLValue(row, "startdate", false);
		jsObj["duedate"] = dhtml.getXMLValue(row, "duedate", false);
		dataObj.push(jsObj);
	}
	return dataObj;
}

/**
 * Function which creates the view and add data to it.
 * @param array rows the JS object
 */
TodayCalendarView.prototype.addAppointmentData = function(rows){
	var currentUnixTime = parseInt(new Date().getTime() / 1000);
	var nextappointmentFlag = 0;
	for(var i in rows){
		var row = rows[i]
		// get the data of the Element and check the parent Element for table.
		var startDateUnixtime = row["startdate"];
		var startDate = strftime('%d-%m-%Y',startDateUnixtime);

		var dueDateUnixtime = row["duedate"];
		var dueDate = strftime('%d-%m-%Y',dueDateUnixtime);
		
		//check if the appointment's starting time is older then now.
		var timeForDay = (currentUnixTime > startDateUnixtime) ? currentUnixTime : startDateUnixtime;
		var dataDay = DAYS[new Date(timeForDay*1000).getDay()].toLowerCase();

		var dayElement = dhtml.getElementById("container_" + dataDay);
		// create table
		var table = dhtml.getElementById("table_"+dataDay);
		if (!table){
			var table = dhtml.addElement(dayElement, "table", "align", "table_"+dataDay);
		}
		var tbody = dhtml.addElement(table, "tbody", "", "");

		// fetch the proper time to show in proper format
		var timeData = this.getDateTimeOfAppointment(row);

		// create row
		var elementRow = dhtml.addElement(tbody, "tr", timeData[1], row["entryid"]);
		
		if(startDate == dueDate && dueDateUnixtime > currentUnixTime && nextappointmentFlag == 0) {
			//for Next appointment icon
			var tdIcon = dhtml.addElement(elementRow, "td", "nextappointment");
			tdIcon.title = _("Next Appointment");
			nextappointmentFlag = 1;		
		}else if(nextappointmentFlag == 1 && startDateUnixtime <= currentUnixTime && dueDateUnixtime >= currentUnixTime){
			if(startDateUnixtime <= currentUnixTime && dueDateUnixtime >= currentUnixTime){
				var nextIndex = parseInt(i) + 1;
				if(parseInt(rows[nextIndex].startdate) != parseInt(startDateUnixtime))
					nextappointmentFlag = 2;
			}else{
				nextappointmentFlag = 2;
			}
			var tdIcon = dhtml.addElement(elementRow, "td", "nextappointment");
			tdIcon.title = _("Next Appointment");
		} else {
			var tdIcon = dhtml.addElement(elementRow, "td", "appointment");			
		}
		// create cell for time
		var tdTime = dhtml.addElement(elementRow, "td", "time","", timeData[0]);

		// create cell for subject
		var subject = row["subject"];
		if(row["location"] != ""){
			subject += " (" + row["location"] + ")";
		}
		var tdData = dhtml.addElement(elementRow, "td", "data","", subject);

		//add events
		dhtml.setEvents(this.moduleID, elementRow, this.events);

		// show the div for data;
		dhtml.removeClassName(dhtml.getElementById("day_"+dataDay),"hide");
	}
}

/**
 * Function check whether the appointment is MultiDay / AllDay or notmal appointment.  
 *     it return an array which consists.
 *        arr[0] = manipulated string to show as multiday /allday/ normal event.
 *		  arr[1] = class name, if multiday/allday appointment.
 * @param array rows array of all appointmet item of next 7 days.
 * @return array rows array of all appointmet item of next 7 days.
 */
TodayCalendarView.prototype.getDateTimeOfAppointment = function(data){
	
	var allday = (data["alldayevent"]=="0") ? false : true;
	var startDateUnixtime = data["startdate"];
	var startTime = strftime(_('%H:%M'),startDateUnixtime).toUpperCase();
	var startDate = strftime('%d-%m-%Y',startDateUnixtime);
	var days = strftime('%A',startDateUnixtime);
	
	var dueDateUnixtime = data["duedate"];
	var dueTime = strftime(_('%H:%M'),dueDateUnixtime).toUpperCase();
	var dueDate = strftime('%d-%m-%Y',dueDateUnixtime);

	// To avoid showing single day appoitments which completes 24:00hrs as a "Multi day appointments"
	var preferredDueDate = strftime('%d-%m-%Y',dueDateUnixtime - 1);

	var currentUnixTime = parseInt(new Date().getTime() / 1000);
	if(dueDateUnixtime < currentUnixTime && startDate == dueDate) {
		//for expire time 
		return [startTime + " - " + dueTime + " ", "normalevent"];
	} else if(allday) {
		//for all day event
		return [_("All day event"), "alldayevent"];
	} else if(startDate != preferredDueDate) { 
		//for multi day event
		return [_("Multi-day event"), "alldayevent"];
	} else {
		//default
		return [startTime + " - " + dueTime + " ", "normalevent"];
	}
}



/**
 * In todaycalendarlistmodule, item() is called from its parent.
 * item() call updateItem() to update only one row, which is not needed for calendar column.
 * so updateItem() is overwritten.
 */ 
TodayCalendarView.prototype.updateItem = function(element, item, properties)
{
}

/**
 * todaycalendarlistmodule's item() is called from its parent.
 * item() call addItem() to add one item, which is not needed for calendar column.
 * so addItem() is overwritten.
 */ 
TodayCalendarView.prototype.addItem = function(item, properties, action) 
{
}