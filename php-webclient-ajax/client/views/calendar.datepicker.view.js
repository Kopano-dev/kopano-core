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
 * --Calendar Datepicker View--
 * @type	View
 * @classDescription	This view can be used for appointement
 * list module to display the calendar items
 * 
 * +-----------------------------+
 * |    <   December 2006   >    |
 * +-----------------------------+
 * | Mon Tue Wed Thu Fri Sat Sun |
 * |                  1   2   3  |
 * |  4   5   6   7   8   9  10  |
 * | 11  12  13  14  15  16  17  |
 * | 18  19  20  21  22  23  24  | 
 * | 25  26  27  28  29  30  31  |
 * +-----------------------------+
 * 
 * DEPENDS ON:
 * |------> view.js
 * |----+-> *listmodule.js
 * |    |----> listmodule.js
 */

/**
 * TODO: 
 * - implement 'onmouseover' on day with events.
 * - implement add item
 * - implement delete item
 * - implement dhtml addClassName 
 */  
CalendarDatePickerView.prototype = new View;
CalendarDatePickerView.prototype.constructor = CalendarDatePickerView;
CalendarDatePickerView.superclass = View.prototype;

function CalendarDatePickerView(moduleID, element, events, data)
{
	this.element = element;
	this.element.id = "datepicker";
	
	this.moduleID = moduleID;
	this.events = events;
	this.data = data;
	this.dayList = Array();
	this.entryList = Array();
	this.month = data["month"];
	this.year = data["year"];
	
	this.initView();
}

CalendarDatePickerView.prototype.initView = function()
{
	var monthElement = dhtml.addElement(this.element, "div", "month");
	
	var table = dhtml.addElement(monthElement, "table");
	table.width = "100%";
	table.height = "100%";
	table.cellPadding = 0;
	table.cellSpacing = 0;
	table.border = 0;
	
	if(MONTHS[this.month]) {
		var tr = table.insertRow(-1);
		tr.className = "monthselected";

		var previoustd = tr.insertCell(-1);
		dhtml.addClassName(previoustd, "previousmonth month_" + this.month + "_" + this.year);
		previoustd.innerHTML = "&nbsp;";
		dhtml.setEvents(this.moduleID, previoustd, this.events["previousmonth"]);
		
		var monthtd = tr.insertCell(-1);
		monthtd.colSpan = 5;
		monthtd.innerHTML = MONTHS[this.month] + " " + this.year;
		
		var nexttd = tr.insertCell(-1);
		dhtml.addClassName(nexttd, "nextmonth month_" + this.month + "_" + this.year);
		nexttd.innerHTML = "&nbsp;";
		dhtml.setEvents(this.moduleID, nexttd, this.events["nextmonth"]);
	}
	
	// add week header
	// FIXME: before we can have an user setting for the startday, we need support in the displaying of the items, so for now it is fixed on monday
//	var startDay = webclient.settings.get("global/calendar/weekstart",1);
	var startDay = 1;
	var tr = table.insertRow(-1);
	for(var i=startDay; i<7; i++){
		var weekday = tr.insertCell(-1);
		dhtml.addClassName(weekday, "dayname");
		weekday.innerHTML = DAYS_SHORT[i]; 
	}
	for(var i=0;i<startDay;i++){		
		var weekday = tr.insertCell(-1);
		dhtml.addClassName(weekday, "dayname");
		weekday.innerHTML = DAYS_SHORT[i]; 
	}

	// Today	
	var today = new Date();

	// Month Date
	var date = new Date();
	date.setTimeStamp(1, this.month+1, this.year);

	var startday = date.getDay();
	var daycount = 1;
	for(var week = 0; week < 6; week++)
	{
		var tr = table.insertRow(-1);
		
		for(var day = 0; day < 7; day++)
		{
			if (startday==0) startday = 7;
			if(!(day+1 < startday && week == 0) && !(daycount > date.getDaysInMonth())) {
				date.setDate(daycount);
				
				var className = "calendarday";
				if(today.getDate() == daycount && today.getMonth() == this.month && today.getFullYear() == this.year) {
					className += " calendartoday";
				} 
				
				var monthday = tr.insertCell(-1);
				dhtml.addClassName(monthday, className);
				monthday.id = date.getTime(); 
				this.dayList[date.getTime()] = Array();
				monthday.innerHTML = daycount;
				dhtml.setEvents(this.moduleID, monthday, this.events["day"]);
				daycount++;
			} else {
				var monthday = tr.insertCell(-1);
				monthday.innerHTML = "&nbsp;";
			}
		}
	}
}

CalendarDatePickerView.prototype.resizeView = function()
{
	
}

CalendarDatePickerView.prototype.execute = function(items, properties, action)
{
	switch(action.getAttribute("type"))
	{
		case "list":
			this.addList(items);
			break;
		case "item":
			//if item exist unbold
			//bold on new spot
			break;
		case "delete":
			//unbold one item
			break;
	}
}

CalendarDatePickerView.prototype.addList = function(items)
{
	this.clearEntryAndDayList();

	// Set a date object that will start at the start of the shown month
	var currMonthStart = (new Date())
	currMonthStart.setTimeStamp(1, this.month+1, this.year);
	currMonthStart = new Date(timeToZero(currMonthStart.getTime()/1000)*1000);

	for(var i = 0; i < items.length; i++)
	{
		var startdate = items[i].getElementsByTagName("startdate")[0];
		var duedate = items[i].getElementsByTagName("duedate")[0];
		var entryID = items[i].getElementsByTagName("entryid")[0].firstChild.nodeValue;
		var subject = dhtml.getXMLValue(items[i], "subject", NBSP);
		var busystatus = dhtml.getXMLValue(items[i], "busystatus", 0);

		//here appointments busystatus are checked to highlight the date in datepicker
		if(parseInt(busystatus, 10) && startdate && startdate.firstChild && duedate && duedate.firstChild) {
			var startunixtime = startdate.getAttribute("unixtime");
			var dueunixtime = duedate.getAttribute("unixtime");
			
			if(startunixtime && dueunixtime) {
				var start_date = new Date(startunixtime * 1000);
				var due_date = new Date(dueunixtime * 1000);

				// set hour,minute & second part to zero in timestamp
				var date = new Date(timeToZero(startunixtime) * 1000);

				var itemObject = new Object ()
				itemObject["startdate"] = startdate;
				itemObject["duedate"] = duedate;
				itemObject["entryid"] = entryID;
				itemObject["subject"] = subject;

				if(date.getTime() < currMonthStart.getTime()){
					date = new Date(currMonthStart.getTime());
				}
				while(date){
					this.addItemToDay(itemObject,date.getTime());
					//switch to next day
					date.addDays(1);
					// Set date to false when the appointment ends to stop marking the days in the datepicker.
					// When the appointments lasts for the whole month, it stops as soon as the next month is reached.
					if(date.getTime() >= (dueunixtime * 1000) || date.getMonth() != currMonthStart.getMonth())
						date = false;
				}
			}			
		}
	}	
}

CalendarDatePickerView.prototype.clearEntryAndDayList = function()
{
	//clear entryList
	this.entryList = Array();

	//clear dayList
	for(var i in this.dayList){
		this.dayList[i] = Array();
		var element = dhtml.getElementById(i);
		if(element){
			dhtml.removeClassName(element, "hasappointments");
		}
	}	
}

CalendarDatePickerView.prototype.addItemToDay = function(itemObject,timeStampOfDay)
{
	var element = dhtml.getElementById(timeStampOfDay);
	var oldTimeStampOfDay = this.entryList[itemObject["entryid"]];
	
	//when the item have bean moved
	if((this.entryList[itemObject["entryid"]]) > 0 && this.dayList[timeStampOfDay] && (this.dayList[timeStampOfDay].length == 1)){
		if(element){
			dhtml.removeClassName(element, "hasappointments");
		}
	}

	if(element){
		dhtml.addClassName(element, "hasappointments");
	}
	
	//modify entryList
	this.entryList[itemObject["entryid"]] = timeStampOfDay;
	
	//modify dayList
	for(var i in this.dayList[oldTimeStampOfDay]){
		if(this.dayList[oldTimeStampOfDay][i] && (this.dayList[oldTimeStampOfDay][i]["entryid"] == itemObject["entryid"])){
			this.dayList[oldTimeStampOfDay][i] = undefined;
		}
	}	
	if(!this.dayList[timeStampOfDay]){
		this.dayList[timeStampOfDay] = Array()
	}
	var sizeOfDay = this.dayList[timeStampOfDay].length;
	this.dayList[timeStampOfDay][sizeOfDay] = itemObject;
}

CalendarDatePickerView.prototype.removeItemOfDay = function(entryID)
{
	var timeStampOfDay = this.entryList[entryID];
	var element = dhtml.getElementById(timeStampOfDay);
	//when the item have bean moved
	if(timeStampOfDay && this.dayList[timeStampOfDay].length == 1){
		if(element){
			dhtml.removeClassName(element, "hasappointments");
		}
	}
	
	//modify entryList
	this.entryList[entryID] = undefined;
	
	//modify dayList
	for(var i in this.dayList[timeStampOfDay]){
		if(this.dayList[timeStampOfDay][i] && this.dayList[timeStampOfDay][i]["entryid"] == entryID){
			this.dayList[timeStampOfDay][i] = undefined;
		}
	}
}

CalendarDatePickerView.prototype.addItem = function()
{
	return false;
}

CalendarDatePickerView.prototype.deleteItems = function(items)
{
	return false;
}

CalendarDatePickerView.prototype.updateItem = function()
{

}

CalendarDatePickerView.prototype.loadMessage = function()
{
}

CalendarDatePickerView.prototype.deleteLoadMessage = function()
{
}

