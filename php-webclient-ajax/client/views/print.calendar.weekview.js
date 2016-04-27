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
* Generates Print Preview for Calendar Week View
*/
PrintCalendarWeekView.prototype = new PrintView;
PrintCalendarWeekView.prototype.constructor = PrintCalendarWeekView;
PrintCalendarWeekView.superclass = PrintView.prototype;

/**
 * @constructor This view can be used to print a list of appointments in week view
 * @param Int moduleID The PrintListModule ID
 * @param HtmlElement The html element where all elements will be appended
 * @param Object The Events object
 * @param Object The Data Passed from the main window
 */
function PrintCalendarWeekView(moduleID, element, events, data, uniqueid) {
	if(arguments.length > 0) {
		this.init(moduleID, element, events, data, uniqueid);
	}
}

/**
 * Function which intializes the view
 */
PrintCalendarWeekView.prototype.initView = function() {
	// add day elments
	this.dayElements = new Array();
	this.dayElements[0] = dhtml.addElement(this.element, "div", "week_view day_monday");
	this.dayElements[3] = dhtml.addElement(this.element, "div", "week_view day_thursday");
	this.dayElements[1] = dhtml.addElement(this.element, "div", "week_view day_tuesday");
	this.dayElements[4] = dhtml.addElement(this.element, "div", "week_view day_friday");
	this.dayElements[2] = dhtml.addElement(this.element, "div", "week_view day_wednesday");
	this.dayElements[5] = dhtml.addElement(this.element, "div", "week_view day_saturday");
	this.dayElements[6] = dhtml.addElement(this.element, "div", "week_view day_sunday");

	// add header info
	var tmpStart = new Date(this.selecteddate).getStartDateOfWeek();
	
	for(var i = 0;i < this.dayElements.length; i++) {
		this.dayElements[i].id = "day_" + this.moduleID + "_" + (tmpStart.getTime() / 1000);
		var dayTitle = dhtml.addElement(this.dayElements[i], "span", "day_header", "", 
											tmpStart.strftime( _("%A %d %B") ));
		tmpStart.addDays(1);
	}
}

/**
 * Function will resize all elements in the view
 * @param	iframeWindow	iframe	iframe that is used for printing
 */
PrintCalendarWeekView.prototype.resizeView = function(iframe) {
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
		iframeElement.style.height = iframeSize["y"] - (printHeaderHeight + printFooterHeight) + "px";
		iframeElement.style.width = iframeSize["x"];

		for(var i in this.dayElements) {
			if(i == "5" || i == "6") {
				iframeDoc.getElementById(this.dayElements[i].id).style.height = (iframeElement.offsetHeight/3) / 2 + "px";
			} else {
				iframeDoc.getElementById(this.dayElements[i].id).style.height = (iframeElement.offsetHeight/3) + "px";	
			}
			iframeDoc.getElementById(this.dayElements[i].id).style.width = (iframeElement.offsetWidth/2) - 2 + "px";
		}

		// width of "print_calendar","print_header" and "print_footer" is in percentage 
		// so before printing need to convert it into pixels
		iframeElement.style.width = (iframeDoc.getElementById(this.dayElements[0].id).offsetWidth * 2) + 2 + "px";
		iframeDoc.getElementById("print_header").style.width = iframeElement.style.width;
		iframeDoc.getElementById("print_footer").style.width = iframeElement.style.width;
	} else {
		var menubarHeight = dhtml.getElementById("menubar").offsetHeight + dhtml.getElementById("menubar").offsetTop;
		var titleHeight = dhtml.getElementsByClassName("title")[0].offsetHeight;
		var bodyHeight = dhtml.getBrowserInnerSize()["y"];

		var dialogContentHeight = bodyHeight - (menubarHeight + titleHeight) + 12;
		dhtml.getElementById("dialog_content").style.height = dialogContentHeight + "px";

		var printFooterHeight = dhtml.getElementById("print_footer").offsetHeight;
		var printHeaderHeight = dhtml.getElementById("print_header").offsetHeight;

		this.element.style.height = dialogContentHeight - (printHeaderHeight + printFooterHeight) + "px";
		this.element.style.width = dhtml.getElementById("dialog_content").offsetWidth + "px";

		for(var i in this.dayElements) {
			if(i == "5" || i == "6") {
				this.dayElements[i].style.height = (this.element.offsetHeight/3) / 2 + "px";
			} else {
				this.dayElements[i].style.height = (this.element.offsetHeight/3) + "px";	
			}
			this.dayElements[i].style.width = (this.element.offsetWidth/2) - 2 + "px";
		}

		// width of "print_calendar","print_header" and "print_footer" is in percentage 
		// so before printing need to convert it into pixels
		this.element.style.width = (this.dayElements[0].offsetWidth * 2) + 2 + "px";
		dhtml.getElementById("print_header").style.width = this.element.style.width;
		dhtml.getElementById("print_footer").style.width = this.element.style.width;
	}

	this.hideOverlappingItems(iframeDoc);
}

/**
 * Function will add items to the view
 * @param Object items Object with items
 * @param Array properties property list
 * @param Object action the action tag
 * @return Array list of entryids
 */
PrintCalendarWeekView.prototype.execute = function(items, properties, action) {
	var entryids = false;

	for(var i=0;i<items.length;i++){
		if (!entryids) {
			entryids = new Object();
		}
		// there is not much difference in createItem function 
		// of PrintView and PrintCalendarWeekView class
		// so it is not redeclared here used common function
		var item = this.createItem(items[i]);
		entryids[item["id"]]= item["entryid"];
	}

	this.resizeView();

	this.createIFrame();

	return entryids;
}

/**
 * Function will check if there are items outside the view that
 * have to be hidden
 * @param	iframeDocument	iframeDoc	iframe that is used for printing
 */
PrintCalendarWeekView.prototype.hideOverlappingItems = function(iframeDoc) {
	if(typeof iframeDoc != "undefined") {
		for(var i in this.dayElements) {
			var dayElement = iframeDoc.getElementById(this.dayElements[i].id);
			var items = dhtml.getElementsByClassNameInElement(dayElement, "event", "div");
			var dayHeight = dayElement.offsetHeight;
			var	dayCurrentHeight = (items.length * 14) + 14;
			var maxItems = Math.floor((dayHeight - 18) / 14) - 1;

			// remove more_items label
			var moreItem = dhtml.getElementsByClassNameInElement(dayElement, "more_items", "div")[0];
			if(moreItem) {
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

				var item_back = dhtml.getElementById(items[j].id + "_back", "div", dayElement);
				if(item_back != null) {
					if(j < maxItems) {
						item_back.style.display = "block";
					} else {
						item_back.style.display = "none";
					}
				}
			}

			// show more_items label
			if(items.length > maxItems && items.length > 0) {
				var moreItem = dhtml.addElement(dayElement, "div", "more_items");
				var unixTime = dayElement.getAttribute("id").split("_")[2];
				moreItem.setAttribute("unixtime", unixTime);
				moreItem.innerHTML = _("More items...");
			}
		}
	} else {
		for(var i in this.dayElements) {
			var items = dhtml.getElementsByClassNameInElement(this.dayElements[i], "event", "div");
			var dayHeight = this.dayElements[i].offsetHeight;
			var	dayCurrentHeight = (items.length * 14) + 14;
			var maxItems = Math.floor((dayHeight - 18) / 14) - 1;

			// remove more_items label
			var moreItem = dhtml.getElementsByClassNameInElement(this.dayElements[i], "more_items", "div")[0];
			if(moreItem) {
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

				var item_back = dhtml.getElementById(items[j].id + "_back", "div", this.dayElements[i]);
				if(item_back != null) {
					if(j < maxItems) {
						item_back.style.display = "block";
					} else {
						item_back.style.display = "none";
					}
				}
			}

			// show more_items label
			if(items.length > maxItems && items.length > 0) {
				var moreItem = dhtml.addElement(this.dayElements[i], "div", "more_items");
				var unixTime = this.dayElements[i].getAttribute("id").split("_")[2];
				moreItem.setAttribute("unixtime", unixTime);
				moreItem.innerHTML = _("More items...");
			}
		}
	}
}