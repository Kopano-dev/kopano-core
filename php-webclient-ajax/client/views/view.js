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
 * --View--
 * @classDescription	View base class
 *  
 * DEPENDS ON:
 * |------> view.js
 * |----+-> *listmodule.js
 * |    |----> listmodule.js
 */

/**
 * TODO: implement default functions
 */

View.prototype.constructor = View;

/**
 * @constructor
 * @param {Int} moduleID
 * @param {HtmlElement} element
 * @param {Object} events
 * @param {XmlElement} data
 */
function View(moduleID, element, events, data)
{
}

/**
 * Function will render the view and execute this.resizeView when done
 */
View.prototype.initView = function()
{
}

View.prototype.destructor = function()
{
	// Unregister module from InputManager
	webclient.inputmanager.removeObject(webclient.getModule(this.moduleID));
}

View.prototype.execute = function()
{
}

View.prototype.showEmptyView = function()
{
}

/**
 * Function will resize all elements in the view
 */
View.prototype.resizeView = function()
{
}

/**
 * Function will show Loading text in view
 */
View.prototype.loadMessage = function()
{
	if (this.element){
		dhtml.removeEvents(this.element);
		dhtml.deleteAllChildren(this.element);
	
		this.element.innerHTML = "<center>" + _("Loading") + "...</center>";
		document.body.style.cursor = "wait";
	}
}

/**
 * Function will delete load text in view
 */
View.prototype.deleteLoadMessage = function()
{
	if (this.element){
		dhtml.deleteAllChildren(this.element);
		document.body.style.cursor = "default";
	}
}

/**
 * Get/Set cursor position, default is to ignore
 */
 
View.prototype.setCursorPosition = function(id)
{
}

View.prototype.getCursorPosition = function()
{
}

/*
 * Modify the data set in the constructor
 */
View.prototype.setData = function(data)
{
}

View.prototype.getRowNumber = function(elemid)
{
}

View.prototype.getElemIdByRowNumber = function(rownum)
{
}

View.prototype.getRowCount = function()
{
}

