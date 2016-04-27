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
 * --Combo Box Widget--  
 * @type	Widget
 * @classDescription	 This widget can be used to make a combo box
 * 
 * HOWTO BUILD:
 * HOWTO USE:
 * HOWTO REACT:
 */
ComboBox.prototype = new Widget;
ComboBox.prototype.constructor = ComboBox;
ComboBox.superclass = Widget.prototype;

/**
 * @constructor This widget can be used to make a combo box
 * @param {Object} id
 * @param {Object} callbackfunction
 * @param {Object} moduleID
 */
function ComboBox(id, callbackfunction, moduleID)
{
	this.id = id;
	this.callbackfunction = callbackfunction;
	this.moduleID = moduleID;
}

/**
 * @param {Object} contentElement
 * @param {Object} options
 * @param {Object} optionSelected
 * @param {Int} width
 */
ComboBox.prototype.createComboBox = function(contentElement, options, optionSelected, width)
{
	var combobox = dhtml.addElement(contentElement, "div", "combobox");

	if (width){
		combobox.style.width = width + "px";
	} else {
		combobox.style.width = width + "100px";
	}
	
	var selected = dhtml.addElement(combobox, "div", "selected", "selected_" + this.id, optionSelected);
	dhtml.addEvent(-1, selected, "click", eventComboBoxClickArrow);
	
	var arrow = dhtml.addElement(combobox, "div", "arrow icon_arrow", "arrow_" + this.id);
	dhtml.addEvent(-1, arrow, "mouseover", eventComboBoxMouseOverArrow);
	dhtml.addEvent(-1, arrow, "mouseout", eventComboBoxMouseOutArrow);
	dhtml.addEvent(-1, arrow, "click", eventComboBoxClickArrow);
	
	var optionsElement = dhtml.addElement(document.body, "div", "combobox_options", "options_" + this.id);
	
	for(var i in options)
	{
		var option = options[i];
		var optionElement = dhtml.addElement(optionsElement, "div", "option value_" + option["id"], false, option["value"]);

		dhtml.addEvent(-1, optionElement, "mouseover", eventComboBoxMouseOverOption);
		dhtml.addEvent(-1, optionElement, "mouseout", eventComboBoxMouseOutOption);
		dhtml.addEvent(-1, optionElement, "click", eventComboBoxClickOption);
		dhtml.addEvent(this.moduleID, optionElement, "click", this.callbackfunction);
	}

	this.combobox = combobox;
}

ComboBox.prototype.destructor = function()
{
	dhtml.deleteAllChildren(dhtml.getElementById("options_"+this.id));
	dhtml.deleteElement(dhtml.getElementById("options_"+this.id));
	
	dhtml.deleteAllChildren(this.combobox);
	dhtml.deleteElement(this.combobox);
	ComboBox.superclass.destructor(this);
}

/**
 * @param {Object} moduleObject
 * @param {HtmlElement} element
 * @param {Object} event
 */
function eventComboBoxMouseOverArrow(moduleObject, element, event)
{
	element.className += " arrowover";
}

/**
 * @param {Object} moduleObject
 * @param {HtmlElement} element
 * @param {Object} event
 */
function eventComboBoxMouseOutArrow(moduleObject, element, event)
{
	if(element.className.indexOf("arrowover") > 0) {
		element.className = element.className.substring(0, element.className.indexOf("arrowover"));
	}
}

/**
 * @param {Object} moduleObject
 * @param {HtmlElement} element
 * @param {Object} event
 */
function eventComboBoxClickArrow(moduleObject, element, event)
{
	var options = dhtml.getElementById("options_"+element.id.substring(element.id.indexOf("_") + 1));
	
	if(options) {
		if(options.style.display == "none" || options.style.display == "") {
			var combobox = element.parentNode;
			options.style.width = (combobox.clientWidth - 2) + "px";
			options.style.top = (dhtml.getElementTop(combobox) + combobox.clientHeight) + "px";
			options.style.left = (dhtml.getElementLeft(combobox) + (document.all?1:2)) + "px";
			
			options.style.display = "block";
		} else {
			options.style.display = "none";
		}
	}
}

/**
 * @param {Object} moduleObject
 * @param {HtmlElement} element
 * @param {Object} event
 */
function eventComboBoxMouseOverOption(moduleObject, element, event)
{
	element.className += " optionover";
}

/**
 * @param {Object} moduleObject
 * @param {HtmlElement} element
 * @param {Object} event
 */
function eventComboBoxMouseOutOption(moduleObject, element, event)
{
	if(element.className.indexOf("optionover") > 0) {
		element.className = element.className.substring(0, element.className.indexOf("optionover"));
	}
}

/**
 * @param {Object} moduleObject
 * @param {HtmlElement} element
 * @param {Object} event
 */
function eventComboBoxClickOption(moduleObject, element, event)
{
	var selected = dhtml.getElementById("selected_" + element.parentNode.id);
	
	if(selected) {
		dhtml.deleteAllChildren(selected);
		dhtml.addTextNode(selected, element.firstChild.nodeValue);
	}	
	
	element.parentNode.style.display = "none";
}
