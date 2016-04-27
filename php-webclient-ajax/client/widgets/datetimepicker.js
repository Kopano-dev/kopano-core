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
 * --DateTimePicker widget--   
 * @type	Widget
 * @classDescription	 This widget will create 1 datetime picker
 *
 * HOWTO BUILD:
 * - make one div with unique id like "startpick" without undersquare
 * - make a object: this.dtp = new DateTimePicker(dhtml.getElementById("startpick"),"Start time");
 * HOWTO USE:
 * - to change the time/date: this.dtp.setValue(1159164000);//use timestamps
 * - to get the time/date: this.dtp.getValue();//will return a timestamp
 * HOWTO REACT:
 * - it is possible to overwrite the onchange function
 *   this function will be exectute when the time or date
 *   are changed: this.dtp.onchange = dtpOnchange;
 * DEPENDS ON:
 * |----+-> datepicker.js
 * |    |----> date-picker.js
 * |    |----> date-picker-language.js
 * |    |----> date-picker-setup.js
 * |------> timepicker.js
 * |------> dhtml.js
 * |------> date.js
 */

DateTimePicker.prototype = new Widget;
DateTimePicker.prototype.constructor = DateTimePicker;
DateTimePicker.superclass = Widget.prototype;

//PUBLIC
/**
 * @constructor	This widget will create 1 datetime picker
 * @param {HtmlElement} element with a unique id without undersquare
 * @param {String} picTitle (optional) title that will be placed before the pickers
 * @param {Date} dateobj (optional) start date/time of the picker  
 */ 
function DateTimePicker(element,picTitle,dateobj)
{
	this.element = element;
	this.picTitle = picTitle;
	this.changed = 0;
	this.id = this.element.id;
	if(dateobj){
		this.value = dateobj;
	}
	else{
		this.value = new Date();
	}
	this.timeElement = null;
	this.dateElement = null;
	this.onchange = null;

	this.render();
}

/**
 * Function will return the value "timestamp" of the pickers
 * @return {Int} date/time in as timestamp 
 */
DateTimePicker.prototype.getValue = function()
{
	var result = 0;

	result = addUnixTimestampToUnixTimeStamp(this.dateElement.getValue(), this.timeElement.getValue());

	return result;
}

/**
 * Function will set "this.value" time/date and the value of the pickers
 * @param {Int} unixtime timestamp
 */
DateTimePicker.prototype.setValue = function(unixtime)
{
	var oldValue = parseInt(Math.floor(this.value/1000));
	var newValue = parseInt(unixtime);
	if(oldValue != newValue){
		this.value = new Date(newValue*1000);
		this.changed++;
		this.timeElement.setValue(timeToSeconds(this.value.getHours()+":"+this.value.getMinutes()));
		this.dateElement.setValue(newValue);
		this.changed--;
	}
}

//PRIVATE
/**
 * Function will build the date and the time picker in "this.element"
 */
DateTimePicker.prototype.render = function()
{
	//this.element.datetimepickerobject = this;
	var tableElement = dhtml.addElement(this.element,"table");
	tableElement = dhtml.addElement(tableElement,"tbody");
	tableElement = dhtml.addElement(tableElement,"tr");
	
	var r1Element = dhtml.addElement(tableElement,"td");
	this.dateElement = new datePicker(this.id,r1Element,this.picTitle,this.value);
	this.dateElement.dateTimePickerObj = this;
	this.dateElement.onchange = dateTimePickerOnchange;
	
	var r2Element = dhtml.addElement(tableElement,"td");
	this.timeElement = new timePicker(r2Element,"",(timeToSeconds(this.value.getHours()+":"+this.value.getMinutes())));
	this.timeElement.dateTimePickerObj = this;
	this.timeElement.onchange = dateTimePickerOnchange;
}

/**
 * Function will check if the value of the picker have been changed and will call
 * "this.onchange(this)" if there is a change
 * @param {Int} dayValue
 */
DateTimePicker.prototype.updateValue = function(dayValue)
{
	var oldValue = Math.floor(this.value.getTime()/1000);
	var newValue = this.getValue();

	this.changed++;

	if(dayValue == undefined){
		dayValue =0;
	}
	if(dayValue == 1){
		this.dateElement.setValue(newValue+(ONE_DAY/1000));
	}
	if(dayValue == -1){
		this.dateElement.setValue(newValue-(ONE_DAY/1000));
	}
	
	if(newValue != oldValue && dayValue == 0){
		this.setValue(newValue);
	}
	this.changed--;

	if(!this.changed && this.onchange){
		this.onchange(this,oldValue);
	}
}

/**
 * Function will call the "DateTimePicker.updateValue()" function
 * @param {HtmlElement} obj
 * @param {Int} oldValue
 * @param {Int} dayValue
 */
function dateTimePickerOnchange(obj,oldValue,dayValue)
{
	obj.dateTimePickerObj.updateValue(dayValue);
}
