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
PrintCalendarListView.prototype = new PrintView;
PrintCalendarListView.prototype.constructor = PrintCalendarListView;
PrintCalendarListView.superclass = PrintView.prototype;

/**
 * This view can be used to print a list of the appointments
 * @constructor
 * @param Int moduleID The PrintListModule ID
 * @param HtmlElement The html element where all elements will be appended
 * @param Object The Events object
 * @param Object The Data Passed from the main window
 */
function PrintCalendarListView(moduleID, element, events, data, uniqueid) {
	if(arguments.length > 0) {
		this.init(moduleID, element, events, data, uniqueid);
	}
}

/**
 * Function which intializes the view
 */ 
PrintCalendarListView.prototype.initView = function() {
	this.weekElement = dhtml.addElement(this.element, "div", "list", "list");

	// add day elments
	this.dayElements = new Array();
	this.dayElements[0] = dhtml.addElement(this.weekElement, "div", "list_view");
	this.dayElements[1] = dhtml.addElement(this.weekElement, "div", "list_view");
	this.dayElements[2] = dhtml.addElement(this.weekElement, "div", "list_view");
	this.dayElements[3] = dhtml.addElement(this.weekElement, "div", "list_view");
	this.dayElements[4] = dhtml.addElement(this.weekElement, "div", "list_view");
	this.dayElements[5] = dhtml.addElement(this.weekElement, "div", "list_view");
	this.dayElements[6] = dhtml.addElement(this.weekElement, "div", "list_view");

	// add header info
	var tmpStart = new Date(this.selecteddate).getStartDateOfWeek();
	for(var i=0;i < this.dayElements.length; i++) {
		this.dayElements[i].id = "day_" + this.moduleID + "_" + (tmpStart.getTime() / 1000);
		
		var dayTitle = dhtml.addElement(this.dayElements[i], "div", "day_header", "");
		dayTitle.innerHTML = "<b>" + tmpStart.strftime( _("%d %B %Y") )	+ "</b><br />" + 
								tmpStart.strftime( _("%A") );

		tmpStart.addDays(1);
	}
}

/**
 * Function will adds items to the view
 * @param {Object} items Object with items
 * @param {Array} properties property list
 * @param {Object} action the action tag
 * @return {Array} list of entryids
 */
PrintCalendarListView.prototype.execute = function(items, properties, action) {
	var entryids = false;

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
 * Function will resize all elements in the view
 */
PrintCalendarListView.prototype.resizeView = function() {
	var menubarHeight = dhtml.getElementById("menubar").offsetHeight + dhtml.getElementById("menubar").offsetTop;
	var titleHeight = dhtml.getElementsByClassName("title")[0].offsetHeight;
	var bodyHeight = dhtml.getBrowserInnerSize()["y"];

	var dialogContentHeight = bodyHeight - (menubarHeight + titleHeight);
	dhtml.getElementById("dialog_content").style.height = dialogContentHeight + "px";

	var printFooterHeight = dhtml.getElementById("print_footer").offsetHeight;
	
	this.element.style.height = dialogContentHeight - printFooterHeight + "px";
	this.weekElement.style.height = this.element.style.height;

	for(var i in this.dayElements) {
		if(dhtml.getElementsByClassNameInElement(this.dayElements[i], "event", "div", true) == "")
		{
			this.dayElements[i].style.display = "none";
		}
	}
}