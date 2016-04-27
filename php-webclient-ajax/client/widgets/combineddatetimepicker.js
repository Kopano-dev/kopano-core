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
 * --CombinedDateTimePicker widget--  
 * @type	Widget
 * @classDescription	This widget will create 2 pickers an start and endt picker:
 * 2*timepicker or 2*datepicker or 2*datetimepicker  
 * 
 * HOWTO BUILD:
 * - make two div with unique id's like "fbstart" and "fbend"
 * - make a object: this.dtp = new combinedDateTimePicker(fbstart,fbend);
 * HOWTO USE:
 * - to change the starttime: this.dtp.setStartValue(1159164000);//use timestamps
 * - to change the endtime: this.dtp.setEndValue(1159164000);//use timestamps
 * - to get the starttime: this.dtp.getStartValue();//will return a timestamp
 * - to get the endtime: this.dtp.getEndValue();//will return a timestamp
 * HOWTO REACT:
 * - it is possible to overwrite the onchange function
 *   this function will be exectute when the time or date
 *   are changed: this.dtp.onchange = this.onchange;
 * DEPENDS ON:
 * |------> date.js
 */
combinedDateTimePicker.prototype = new Widget;
combinedDateTimePicker.prototype.constructor = combinedDateTimePicker;
combinedDateTimePicker.superclass = Widget.prototype;

//PUBLIC
function combinedDateTimePicker(startElement,endElement)
{
	this.startPicker = startElement;
	this.endPicker = endElement;
	this.onchange = null;
	this.changed = 0;
	this.startOldValue = null;
	this.endOldValue = null;
	
	this.render();
}

/**
 * Function will change the starttime
 * @param {Int} timeStamp
 */
combinedDateTimePicker.prototype.setStartValue = function (timeStamp)
{
	timeStamp = parseInt(timeStamp,10);

	if(timeStamp != this.startPicker.getValue()){
		this.startPicker.setValue(timeStamp);
	}
	//save value to compare in update "updateStart"
	this.startOldValue = timeStamp;
}

/**
 * Function will change the endtime
 * @param {Int} timeStamp "unixtimestamp" 
 */
combinedDateTimePicker.prototype.setEndValue = function (timeStamp)
{
	if(timeStamp != this.endPicker.getValue()){
		this.endPicker.setValue(timeStamp);
	}
	
	//save value to compare in update "updateEnd"
	this.endOldValue = timeStamp;
}

/**
 * Function will return the timestamp of the starttime
 * @return {Int} timestamp 
 */ 
combinedDateTimePicker.prototype.getStartValue = function ()
{
	return this.startPicker.getValue();
}

/**
 * Function will return the timestamp of the endtime
 * @return {int} timestamp 
 */ 
combinedDateTimePicker.prototype.getEndValue = function ()
{
	return this.endPicker.getValue();
}

//PRIVATE
/**
 * Function will draw 2 pickers
 */
combinedDateTimePicker.prototype.render = function ()
{
	this.endOldValue = this.endPicker.getValue();
	this.endPicker.onchange = this.updateEnd;
	this.endPicker.parentPicker = this;

	this.startOldValue = this.startPicker.getValue();
	this.startPicker.onchange = this.updateStart;
	this.startPicker.parentPicker = this;
}

/**
 * Function will update the value of starttime
 * @param {Object} obj combinedDateTimePicker(object) 
 */ 
combinedDateTimePicker.prototype.updateStart = function(obj,oldValue)
{
	var combiDTP = obj.parentPicker;
	var newStartValue = combiDTP.getStartValue();
	var oldStartValue = combiDTP.startOldValue;
	var oldEndValue = combiDTP.endOldValue;
	var duration = oldEndValue-oldStartValue;
	//as now we can create 0 duration app
	if(duration >= 0){
		var newEndValue = newStartValue+duration;
	}
	else{
		var newEndValue = newStartValue+HALF_HOUR/1000;
	}
	
	combiDTP.changed++;

	if(newStartValue != oldStartValue){
		combiDTP.setStartValue(newStartValue);
	}
	if(newEndValue != oldEndValue){
		combiDTP.setEndValue(newEndValue);
	}

	combiDTP.changed--;

	if(!combiDTP.changed && combiDTP.onchange){
		combiDTP.onchange();
	}

}

/**
 * Function will update the value of endtime
 * @param {Object} obj combinedDateTimePicker(object) 
 */ 
combinedDateTimePicker.prototype.updateEnd = function(obj,oldValue)
{
	var combiDTP = obj.parentPicker;
	var newEndValue = combiDTP.getEndValue();
	var oldEndValue = combiDTP.endOldValue;
	var oldStartValue = combiDTP.getStartValue();

	combiDTP.changed++;

	if(newEndValue != oldEndValue){
		combiDTP.setEndValue(newEndValue);
	}
	
	if(oldStartValue > newEndValue){
		combiDTP.setStartValue(newEndValue-(HALF_HOUR/1000));
	}

	combiDTP.changed--;


	if(!combiDTP.changed && combiDTP.onchange){
		combiDTP.onchange();
	}
}
