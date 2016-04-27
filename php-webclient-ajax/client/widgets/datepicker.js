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
 * --datePicker widget--   
 * @type	Widget
 * @classDescription	 This widget will create 1 datetime picker
 *
 * HOWTO BUILD:
 * - make one div with unique id like "startpick" without undersquare
 * - make a object: this.dp = new datePicker("startpick", dhtml.getElementById("startpick"),"Start date", new Date());
 *   for "new Date()" you can also use the constant DATE_NONE, to start with an empty date field
 * HOWTO USE:
 * - to change the date: this.dp.setValue(1159164000);//use timestamps
 * - to get the date: this.dp.getValue();//will return a timestamp
 *   if the date field is empty, getValue will return NaN -> check with JS function isNaN()
 * HOWTO REACT:
 * - it is possible to overwrite the onchange function
 *   this function will be exectute when the time or date
 *   are changed: this.dp.onchange = dpOnchange;
 * DEPENDS ON:
 * |----+-> datepicker.js
 * |    |----> date-picker.js
 * |    |----> date-picker-language.js
 * |    |----> date-picker-setup.js
 * |------> dhtml.js
 * |------> date.js
 */

datePicker.prototype = new Widget;
datePicker.prototype.constructor = datePicker;
datePicker.superclass = Widget.prototype;

var DATE_NONE = "nodate"; 

//PUBLIC
/**
 * @constructor This widget will create 1 datetime picker
 * @param {Int} id
 * @param {HtmlElement} element
 * @param {String} picTitle
 * @param {Date} dateobj
 */
function datePicker(id,element,picTitle,dateobj)
{
	this.element = element;
	this.id = id;
	this.changed = false
	if(picTitle){
		this.picTitle = picTitle;
	}
	else{
		this.picTitle = "";
	}
	if(dateobj){
		if (dateobj == DATE_NONE){
			this.value = new Date(NaN);
		}else{
			this.value = dateobj;
		}
	}
	else{
		this.value = new Date();
	}
	this.dateInput = null;
	this.onchange = null;

	this.render();
	this.setValue(Math.floor(this.value.getTime()/1000));
}

/**
 * Function will resturn the value "timestamp" of "this.value"
 * @param (bool) TRUE if returned timestamp should be 00:00:00 UTC instead of 00:00:00 local
 * @return {int}
 */ 
datePicker.prototype.getValue = function(utc)
{
	var result = Date.parseDate(this.dateInput.value,_("%d-%m-%Y"), utc);
	return result.getTime() / 1000;
}

/**
 * Function will set "this.value" date
 * @param {Int} unixtime timestamp 
 */ 
datePicker.prototype.setValue = function(unixtime)
{
	var newValue = parseInt(unixtime,10);
	var oldValue = parseInt(Math.floor(this.value.getTime()/1000),10);

	if(oldValue != newValue){
		this.value = new Date(newValue*1000);
	}

	if (isNaN(unixtime)) {
		this.dateInput.value = "";
	}else{
		this.dateInput.value = this.value.print(_("%d-%m-%Y"));
	}
}

//PRIVATE
datePicker.prototype.render = function()
{
	//drawing elements
	var container = dhtml.addElement(this.element,"table");
	container.setAttribute("border","0");
	container.setAttribute("cellpadding","0");
	container.setAttribute("cellspacing","0");

	var container = dhtml.addElement(container, "tbody");

	var row1 = dhtml.addElement(container,"tr");
	if(this.picTitle.length > 0){
		var col1 = dhtml.addElement(row1,"td","propertynormal propertywidth",null,this.picTitle+":");
	}
	
	var col2 = dhtml.addElement(row1,"td");
	this.dateInput = dhtml.addElement(null,"input","fieldsize",this.id+"_date");
	this.dateInput.setAttribute("type","text");
	col2.appendChild(this.dateInput); 
	this.dateInput.datePickerObj = this;
	dhtml.addEvent(-1,this.dateInput,"change",eventDatePickerInputChange);
	
	var col3 = dhtml.addElement(row1,"td");
	col3.style.width = 30+"px";
	var col3datepicker = dhtml.addElement(col3,"div","datepicker",this.id+"_date_button","\u00a0");
	
	//create calendar item
	Calendar.setup({
		inputField	:	this.id+"_date",			// id of the input field
		ifFormat	:	_("%d-%m-%Y"),				// format of the input field
		button		:	this.id+"_date_button",		// trigger for the calendar (button ID)
		step		:	1,							// show all years in drop-down boxes (instead of every other year as default)
		weekNumbers	:	false,
		onUpdate : eventDatePickerCalendarChange,
		datePickerObj : this						// this obj for the callback function
	});
}

/**
 * @param {Object} moduleObject
 * @param {HtmlElement} element
 * @param {Object} event
 */
function eventDatePickerInputChange(moduleObject, element, event)
{
	var obj = element.datePickerObj;
	/**
	 * As parseDate function works with Date value and create current datetime string if the Date value is not a valid date.
	 * Thus here we check if the newValue inserted is valid date or any string or invalid date and assign its Value to blank if invalid.
	 * 
	 * Split date string in to sub parts to get value
	 * German language  ->		dd.mm.yyyy
	 * English US language ->	dd/mm/yyyy
	 * Other languages  ->		dd-mm-yyyy
	 */
	var valueArr = element.value.trim().split(/[-.//]/);
	var newValue = (new Date(valueArr[2], valueArr[1], valueArr[0]) != "Invalid Date")?Date.parseDate(element.value,_("%d-%m-%Y")):"" ;
	var oldValue = obj.value;

	if(!newValue){
		alert(_("You must specify a valid date and/or time. Check your entries in this dialog box to make sure they represent a valid date and/or time."));
		obj.setValue(Math.floor(oldValue.getTime()/1000));
		if(obj.onchange)
			obj.onchange(obj, oldValue);
	}
	else{
		obj.setValue(Math.floor(newValue.getTime()/1000));
		if(obj.onchange)
			obj.onchange(obj, oldValue);
	}
}

/**
 * @param {Object} obj
 */
function eventDatePickerCalendarChange(obj)
{
	var oldValue = obj.value;
	
	if (obj && obj.params && obj.params.datePickerObj)
	{
		obj = obj.params.datePickerObj;
		obj.setValue(Date.parseDate(obj.dateInput.value, _("%d-%m-%Y")).getTime()/1000);
		if(obj.onchange)
			obj.onchange(obj, oldValue);
	}
	return true;
}
