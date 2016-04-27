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
 * todaylistmodule extend from the Listmodule.
 */
TodayListModule.prototype = new ListModule;
TodayListModule.prototype.constructor = TodayListModule;
TodayListModule.superclass = ListModule.prototype;

function TodayListModule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}	
}

/**
 * Function which intializes the module.
 * @param integer id id
 * @param object element the element for the module
 * @param string title the title of the module
 * @param object data the data (storeid, entryid, ...)  
 */ 
TodayListModule.prototype.init = function(id, element, title, data)
{
	TodayListModule.superclass.init.call(this, id, element, title, data);
}

/**
 * Function which sets the title for the todaymodule's sub module
 * @param string title the title
 * @param object events events
 */ 
TodayListModule.prototype.setTitleInTodayView = function(title)
{
	var titleElement = dhtml.addElement(this.element, "div", "heading");
	var header = dhtml.addElement(titleElement, "a", false, false, title);
    header.href ="#";
	dhtml.addEvent(this, titleElement, "click", eventTodayTitleClick);
}



// function open the view when click on the header. 
function eventTodayTitleClick(moduleObject, element, event)
{
	webclient.hierarchy.selectFolder(true, moduleObject.entryid);
}

function eventTodayMessageMouseOver(moduleObject, element, event){
		dhtml.addClassName(element, "trtextover");
}

function eventTodayMessageMouseOut(moduleObject, element, event){
		dhtml.removeClassName(element, "trtextover");
}
