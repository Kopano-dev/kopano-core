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
 * --TimePicker widget--  
 * @type	Widget
 * @classDescription	This widget will create 1 time picker
 *
 * HOWTO BUILD:
 * - make one div with unique id like "startpick" without undersquare
 * - make a object: this.tp = new DateTimePicker(dhtml.getElementById("startpick"),"Start time");
 * HOWTO USE:
 * - to change the time: this.tp.setValue(1159164000);//use timestamps
 * - to get the time: this.tp.getValue();//will return a timestamp
 * HOWTO REACT:
 * - it is possible to overwrite the onchange function
 *   this function will be exectute when the time or date
 *   are changed: this.tp.onchange = tpOnchange;
 * DEPENDS ON:
 * |------> timepicker.js
 * |------> dhtml.js
 * |------> date.js
 */

timePicker.prototype = new Widget;
timePicker.prototype.constructor = timePicker;
timePicker.superclass = Widget.prototype;
 
//PUBLIC
/**
 * @constructor	This widget will create 1 time picker
 * @param {HtmlElement} element
 * @param {String} picTitle
 * @param {Int} timeInSec
 */
function timePicker(element,picTitle,timeInSec)
{
	this.element = element;
	this.changed = false;
	this.visible = true;
	if(picTitle){
		this.picTitle = picTitle;
	}
	else{
		this.picTitle = "";
	}
	if(timeInSec){
		this.value = timeInSec;
	}
	else{
		this.value = 32400;//32400 = 9:00
	}
	this.timeInput = null;
	this.onchange = null;

	this.render();
	this.setValue(this.value);
}

/**
 * Function will resturn the value "timestamp" of "this.value"
 * @return {int}
 */ 
timePicker.prototype.getValue = function()
{
	var result = 0;
	if(this.visible == true){
		result = timeToSeconds(this.timeInput.value);
	}
	return parseInt(result,10);
}

/**
 * Function will set "this.value" time
 * @param {Int} unixtime timestamp 
 */ 
timePicker.prototype.setValue = function(timeInSec)
{
	var newValue = parseInt(timeInSec,10);
	var oldValue = this.value;
	var dayValue = 0;
	
	if(newValue>=86400){
		newValue=newValue-86400;//86400 = ONE_DAY
		dayValue = 1;
	}
	if(newValue<0){
		newValue=86400+newValue;//86400 = ONE_DAY
		dayValue = -1; 
	}
	if(oldValue != newValue){
		this.value = newValue;
	}

	this.dayValue = dayValue;
	this.timeInput.value = secondsToTime(this.value);
}

timePicker.prototype.hide = function()
{
	this.timeInput.parentNode.parentNode.parentNode.parentNode.style.visibility = "hidden";
	this.visible = false;
}

timePicker.prototype.show = function()
{
	this.timeInput.parentNode.parentNode.parentNode.parentNode.style.visibility = "visible";
	this.visible = true;
}

//PRIVATE
timePicker.prototype.render = function()
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
	
	var col4 = dhtml.addElement(row1,"td");
	this.timeInput = dhtml.addElement(null,"input","fieldsize");
	this.timeInput.setAttribute("type","text");
	col4.appendChild(this.timeInput);
	
	// If time is in am/pm format then set more fieldsize for timepicker field
	if(_("%H:%M") != "%H:%M")
		this.timeInput.setAttribute("size","6");
	else
		this.timeInput.setAttribute("size","4");
	dhtml.addEvent(-1,this.timeInput,"change",eventTimePickerInputChange);
	this.timeInput.timePickerObject = this;
	
	var col5 = dhtml.addElement(row1,"td");
	col5.style.width = 40+"px";
	var col5spinUp = dhtml.addElement(col5,"div","spinner_up","","\u00a0");
	dhtml.addEvent(-1,col5spinUp,"click",eventTimePickerSpinnerUp);
	col5spinUp.timePickerObject = this;
	var col5spinDown = dhtml.addElement(col5,"div","spinner_down","","\u00a0");
	dhtml.addEvent(-1,col5spinDown,"click",eventTimePickerSpinnerDown);
	col5spinDown.timePickerObject = this;
}

/**
 * Event Function which will get fired when user changes any data in time inputfield
 * this also checks follwoing time formats entered by the user and parse them as required,thus making time edit more User-friendly
 * for example  0700 => 07:00
 *			     700 => 07:00
 *				  07 => 07:00
 *                 7 => 07:00
 *               4pm => 04:00 pm 
 * @param {Object} moduleObject
 * @param {HtmlElement} element
 * @param {Object} event
 */
function eventTimePickerInputChange(moduleObject, element, event)
{
	var dtpObj = element.timePickerObject;
	var oldValue = dtpObj.value;
	var newValue = element.value.trim();
	var regTime = /^[0-2]?[0-9][:][0-6]?[0-9]$/; //time
	// This will seprate the text part from input time format i.e. am/pm.
	var formatType;

	// If time is in am/pm format then change the regular expression
	if(_("%H:%M") != "%H:%M"){
		regTime = /^[0-2]?[0-9][:][0-6]?[0-9]([ ]?[apAP][.]?[Mm][.]?)?$/;
		formatType = newValue.match(/([apAP][.]?[Mm][.]?)/g);
		if(formatType)	
			newValue = newValue.substring(0, newValue.indexOf(formatType)).trim();
		else
			newValue = newValue.trim();
	}

	if(newValue.length < 5 && newValue.match(/\D/g) == null){
		switch(newValue.length){
			case 4:
			var hh = newValue.substring(0,2);
			var mm = newValue.substring(2,4);
			newValue = hh +':'+ mm;
			break;
			
			case 3:
			var hh = newValue.substring(0,1);
			var mm = newValue.substring(1,3);
			newValue = hh +':'+ mm;
			break;

			case 2:
			var hh = newValue.substring(0,2);
			newValue = hh +':'+ 00;
			break;

			case 1:
			var hh = newValue.substring(0,1);
			newValue = hh +':'+ 00;
			break;
			
			default:
			alert(_("You must specify a valid date and/or time. Check your entries in this dialog box to make sure they represent a valid date and/or time.")); 		
			break;
		}
		if(_("%H:%M") != "%H:%M" && formatType)
			newValue = newValue + " " + formatType;
	}

	if(!regTime.test(newValue)){
		if(newValue.length >= 0){
			alert(_("You must specify a valid date and/or time. Check your entries in this dialog box to make sure they represent a valid date and/or time."));
			dtpObj.setValue(oldValue);//as we need to restore the previous save time of the appointment.
			if(dtpObj.onchange)
				dtpObj.onchange(dtpObj, oldValue);
		}
	}
	else{
		dtpObj.setValue(timeToSeconds(newValue),dtpObj);
		// here we check if the value to time is hours in greater the 24hrs [one day]
		// if it is we have to add one Day in datepicker as well. 
		if(parseInt(newValue.substring(0,2), 10) >= 24)
			dtpObj.dayValue = 1;
		if(dtpObj.onchange)
			dtpObj.onchange(dtpObj, oldValue, dtpObj.dayValue);
	}
}

/**
 * @param {Object} moduleObject
 * @param {HtmlElement} element
 * @param {Object} event
 */
function eventTimePickerSpinnerUp(moduleObject, element, event)
{
	var dtpObj = element.timePickerObject;
	var oldValue = dtpObj.value;
	dtpObj.setValue(oldValue+HALF_HOUR/1000);
	if(dtpObj.onchange)
		dtpObj.onchange(dtpObj, oldValue, dtpObj.dayValue);
}

/**
 * @param {Object} moduleObject
 * @param {HtmlElement} element
 * @param {Object} event
 */
function eventTimePickerSpinnerDown(moduleObject, element, event)
{
	var dtpObj = element.timePickerObject;
	var oldValue = dtpObj.value;
	dtpObj.setValue(oldValue-HALF_HOUR/1000);
	if(dtpObj.onchange)
		dtpObj.onchange(dtpObj, oldValue, dtpObj.dayValue);
}
