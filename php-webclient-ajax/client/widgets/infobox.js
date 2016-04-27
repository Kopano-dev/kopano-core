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
* InfoBox widget - shows a message in the webclient
*/

InfoBox.prototype = new Widget;
InfoBox.prototype.constructor = InfoBox;
InfoBox.superclass = Widget.prototype;

function InfoBox(message, timeout, className)
{
	this.init(message, timeout, className);
}

InfoBox.prototype.init = function(message, timeout, className)
{
	InfoBox.superclass.init(this);
	

	this.message = "";
	this.timeout = 10000; // timeout in msec
	this.timer = null;
	this.className = "infobox";

	this.element = dhtml.addElement(document.body, "div", this.className, "infobox_"+this.widgetID);
	this.element.widgetObject = this;
	dhtml.addEvent(-1, this.element, "click", eventInfoBoxClick);

	if (arguments.length > 0 && message != null){
		this.show(message, timeout, className);
	}
}

InfoBox.prototype.destructor = function(widgetObject)
{
	// stop timer
	window.clearTimeout(this.timer);

	// if visible hide the infobox
	this.hide();

	// remove element
	dhtml.deleteAllChildren(this.element);
	dhtml.removeElement(this.element);

	InfoBox.superclass.destructor(this);
}

InfoBox.prototype.show = function(message, timeout, className)
{
	if (typeof message != "undefined"){
		this.message = message;
	}
	if (typeof timeout != "undefined"){
		this.timeout = timeout;
	}
	if (typeof className != "undefined"){
		this.className = "infobox "+className;
	}

	if (this.element){
		this.element.className = this.className;
		dhtml.deleteAllChildren(this.element);

		this.element.textBox = dhtml.addElement(this.element, "span", false, false);
		this.element.textBox.innerHTML = this.message;

		this.visible = true;		
		this.element.style.display = "block";
		
		if (this.timeout>0){
			// hiding of the info box is initial called from a static function
			this.timer = window.setTimeout('eventInfoBoxTimeout("'+this.element.id+'");', this.timeout);
		}
	}
}

InfoBox.prototype.hide = function()
{
	this.element.style.display = "none";
	this.visible = false;
	// make sure the timer is stopped (in case we call this function ourself)
	window.clearTimeout(this.timer);
}

InfoBox.prototype.click = function(event)
{
	this.hide();
}

function eventInfoBoxTimeout(infoBoxID)
{
	var element = dhtml.getElementById(infoBoxID);
	if (element && element.widgetObject){
		element.widgetObject.hide();
	}
}

function eventInfoBoxClick(moduleObject, element, event)
{
	if (typeof element.widgetObject != "undefined" && element.widgetObject instanceof InfoBox){
		element.widgetObject.click(event);
	}
}
