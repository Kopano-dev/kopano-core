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
 * --Pagination Widget--  
 * @type	Widget
 * @classDescription	 This widget can be used to create paging tool
 * 
 * HOWTO BUILD:
 * HOWTO USE:
 * HOWTO REACT:
 */
Pagination.prototype = new Widget;
Pagination.prototype.constructor = Pagination;
Pagination.superclass = Widget.prototype;

/**
 * @constructor This widget can be used to create a paging tool
 * @param {Object} id
 * @param {Object} callbackfunction
 * @param {Object} moduleID
 */
function Pagination(id, callbackfunction, moduleID)
{
	this.id = id;
	this.callbackfunction = callbackfunction;
	this.moduleID = moduleID;
	this.hackPaging = new Array("prev", "next");
}

/**
 * Function which creates paging tool within specified contect element
 * @param {Object} contentElement
 * @param {Object} options
 * @param {Object} optionSelected
 * @param {Int} width
 */
Pagination.prototype.createPagingElement = function(contentElement, totalPages, currentPage)
{
	// Create paging element
	this.pagingElement = dhtml.addElement(contentElement, 'div', 'page_title', this.id);
	this.totalPages = totalPages;
	this.currentPage = currentPage;

	// create first/previous/next/last button
	var table = new Array();
	table.push('<table cellpadding="0px" cellspacing="0px" style="height:14px;"><tr>');
	table.push('<td id="page_first" class="page_button first" title="'+ _("Go to the first page") +'"></td>');
	table.push('<td id="page_prev" class="page_button prev" title="'+ _("Go to the previous page") +'"></td>');
	table.push('<td id="page_field"></td>');
	table.push('<td id="page_next" class="page_button next" title="'+ _("Go to the next page") +'"></td>');
	table.push('<td id="page_last" class="page_button last" title="'+ _("Go to the last page") +'"></td>');
	table.push('</tr></table>');

	this.pagingElement.innerHTML = table.join("");

	var firstButton = dhtml.getElementById("page_first");
	var prevButton = dhtml.getElementById("page_prev");
	var nextButton = dhtml.getElementById("page_next");
	var lastButton = dhtml.getElementById('page_last');

	// Create input field
	var pageField = dhtml.getElementById("page_field");
	dhtml.addTextNode(pageField, _("Page"));
	var inputField = dhtml.addElement(pageField, "input", "pageNavigation");
	inputField.setAttribute('value', currentPage + 1);
	dhtml.addTextNode(pageField, ' ' + _("of") + ' ' + totalPages);

	// Attach events on input field
	dhtml.addEvent(-1, inputField, 'click', eventPageFieldClick);
	dhtml.addEvent(-1, inputField, 'mousedown', eventPageFieldClick);
	dhtml.addEvent(-1, inputField, 'mouseup', eventStopBubbling);
	dhtml.addEvent(this, inputField, 'keydown', eventPageFieldKeyDown);
	dhtml.addEvent(-1, inputField, "mousemove", eventPageFieldMouseMove);
	dhtml.addEvent(-1, inputField, "selectstart", eventPageFieldMouseMove);
	dhtml.addEvent(-1, inputField, "focus", eventPageFieldFocus);
	dhtml.addEvent(-1, inputField, "blur", eventPageFieldFocus);


	// Attach events on first/prev buttons only if this is not first page
	if (currentPage != 0) {
		this.hackPaging["prev"] = prevButton;//hack
		dhtml.addEvent(this.moduleID, prevButton, "click", this.callbackfunction);
		dhtml.addEvent(this.moduleID, firstButton, "click", this.callbackfunction);
	} else {
		dhtml.addClassName(firstButton, "nobutton");
		dhtml.addClassName(prevButton, "nobutton");
	}

	// Attach events on next/last buttons only if this is not last page
	if (currentPage != (totalPages -1)) {
		this.hackPaging["next"] = nextButton;//hack
		dhtml.addEvent(this.moduleID, nextButton, "click", this.callbackfunction);
		dhtml.addEvent(this.moduleID, lastButton, "click", this.callbackfunction);
	} else {
		dhtml.addClassName(nextButton, "nobutton");
		dhtml.addClassName(lastButton, "nobutton");
	}

	// Show paging element only if total no of pages > 0
	if(totalPages > 0) {
		var pageElement = dhtml.getElementById("page_"+this.moduleID);
		pageElement.style.display = "block";
	}
}

Pagination.prototype.destructor = function()
{
	dhtml.deleteAllChildren(this.pagingElement);
	dhtml.deleteElement(this.pagingElement);
	Pagination.superclass.destructor(this);
}

function eventPageFieldClick(moduleObject, element, event)
{
	var result = false;
	event.stopPropagation();
	if (!element.hasFocus){
		element.focus();
	} else {
		result = true;
	}
	return result;
}

/**
 * Keydown event handler for page field.
 *@param Object moduleObject object of pagination widget
 */
function eventPageFieldKeyDown(widgetObject, element, event)
{
	var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghiklmnopqrstuvwxyz';
	var selectedPage = parseInt(element.value, 10);

	if (event.keyCode == 13) {
		// If entered page number is not within range then ignore it and restore previous state of input field
		if (selectedPage <= widgetObject.totalPages && selectedPage != 0)
			widgetObject.callbackfunction(widgetObject.moduleID, element, event);
		else
			element.value = widgetObject.currentPage + 1;

		event.stopPropagation();

	/**
	 * Ignore this event by returning 'false' if user enters invalid character into field.
	 * keyCode.fromCharCode() will give 'character' even if numbers are entered using NUM pan,
	 * so checking keyCode range if user is using NUM pad.
	 */
	} else if (!(event.keyCode >= 96 && event.keyCode <= 105) && (chars.indexOf(event.keyCode.fromCharCode()) != -1)) {
		event.preventDefault();
	}
}

function eventPageFieldMouseMove(moduleObject, element, event)
{
	event.stopPropagation();
	return element.hasFocus;
}

function eventPageFieldFocus(moduleObject, element, event)
{
	element.hasFocus = (event.type == "focus")
}