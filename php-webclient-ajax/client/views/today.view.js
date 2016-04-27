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
 * Today View
 * @type  View
 * @classDescription  This view is use for todaylistmodule 
 */
TodayView.prototype = new View;
TodayView.prototype.constructor = TodayView;
TodayView.superclass = View.prototype;

/**
 * @constructor 
 * @param moduleID is the parent module
 * @param element is the element where the view should be placed
 * @param events are the events that should be handled for this view
 * @param data are any view-specific data
 */
function TodayView(moduleID, element, events, data)
{
	if(arguments.length > 0) {
		this.init(moduleID, element, events, data);
	}
}

TodayView.prototype.init = function(moduleID, element, events, data)
{
	this.element = element;

	this.moduleID = moduleID;
	this.events = events;
	this.data = data;
	
	this.module = new Object();
	
	this.layout = new Object();
	this.layout["first"] = new Object();
	this.layout["first"]["elements"] = new Object();
	this.layout["second"] = new Object();
	this.layout["second"]["elements"] = new Object();
	this.layout["third"] = new Object();
	this.layout["third"]["elements"] = new Object();
	
	this.initView();
}

/**
 * Function which intializes the view.
 */ 
TodayView.prototype.initView = function()
{
	//create columns for module.
	this.firstColumn = dhtml.addElement(this.element, "div", "floatdiv", "first");
	this.secondColumn = dhtml.addElement(this.element, "div", "floatdiv", "second");
	this.thirdColumn = dhtml.addElement(this.element, "div", "message", "third");
}

/**
 * function add different module to the todayview.
 * @param integer moduleID moduleID.
 * @param string position position of module where it should be loaded
 * @param integer INSERT_ELEMENT
 * @return object the new element 
 */ 
TodayView.prototype.addModule = function(moduleID, position, INSERT_ELEMENT)
{
	var layoutElement = dhtml.getElementById(position);
	if(layoutElement){
		var element = dhtml.addElement(layoutElement, "div", "todaylayoutelement");
	}
	switch(INSERT_ELEMENT)
	{
		case INSERT_ELEMENT_AT_TOP:
			if(layoutElement.firstChild) {
				layoutElement.insertBefore(element, layoutElement.firstChild);
			}
			break;
		case INSERT_ELEMENT_BETWEEN:
			var childNodes = layoutElement.childNodes.length;
			var elementPosition = Math.round(childNodes.length / (length + 1));

			if(layoutElement.childNodes[elementPosition]) {
				layoutElement.insertBefore(element, layoutElement.childNodes[elementPosition]);
			}
			break;
		case INSERT_ELEMENT_AT_BOTTOM:
			// is default
			break;
	}
	this.layout[position]["elements"][moduleID] = element;
	
	return element;
}

/**
 * function call when todayview is resize.
 * @param integer style style of the layout.
 */
TodayView.prototype.resizeView = function(style)
{		
	switch(style)
	{
		case 2:
			this.firstColumn.style.width = "48%";  
			this.secondColumn.style.display = "none";  
			this.thirdColumn.style.width = "48%";
			break;
		case 3:
			this.firstColumn.style.display = "none"; 
			this.secondColumn.style.display = "none";  
			this.thirdColumn.style.width = "100%";
			
			if(this.thirdColumn.lastChild && this.thirdColumn.lastChild.lastChild && this.thirdColumn.lastChild.lastChild.lastChild) {
				this.thirdColumn.lastChild.lastChild.lastChild.style.width = "10%";
			}
			break;
		case 1:
		default:
			this.firstColumn.style.width = "41%";   
			this.secondColumn.style.width = "41%";
			this.thirdColumn.style.width = "15%";
		break;
	}
	
	if ((this.element.parentNode.offsetHeight - 52) > 0)
		this.element.style.height = (this.element.parentNode.offsetHeight - 52) + "px";

	var maxHeight = 0;
	for (var position in this.layout) {
		var positionHeight = 0;
		for (var moduleID in this.layout[position]["elements"]){
			var element = this.layout[position]["elements"][moduleID];
			positionHeight += element.offsetHeight;
		}
		
		if (positionHeight && positionHeight > maxHeight) maxHeight = positionHeight;
	}
	
	if (maxHeight != 0) {
		for (var position in this.layout){
			var positionElement = dhtml.getElementById(position);
			if (positionElement) positionElement.style.height = maxHeight + "px";
		}
	}
}
