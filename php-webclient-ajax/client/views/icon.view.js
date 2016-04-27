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
 * --Icon View--
 * @type	View
 * @classDescription	This view is a icon view and is meant to be used for the stickynote list module
 * 
 *   +-----+       +-----+
 *   |     |       |     |
 *   |     |       |     |
 *   +-----+       +-----+
 * hello world   hello world
 * 
 * DEPENDS ON:
 * |------> view.js
 * |----+-> *listmodule.js
 * |    |----> listmodule.js
 */
 
IconView.prototype = new View;
IconView.prototype.constructor = IconView;
IconView.superclass = View.prototype;

// PUBLIC
/**
 * @constructor This view can be used for stickynote list module to display the sticknote items
 * @param {Int} moduleID
 * @param {HtmlElement} element
 * @param {Object} events
 * @param {XmlElement} data
 */
function IconView(moduleID, element, events, data)
{
	this.element = element;
	this.moduleID = moduleID;
	this.events = events;
	this.data = data;
}

/**
 * Function will render the view and execute this.resizeView when done
 */
IconView.prototype.initView = function()
{
	// clear old elements
	dhtml.deleteAllChildren(this.element);
	this.element.style.overflow = "auto";
	this.element.className = "iconview";
		
	// add keyboard event
	var module = webclient.getModule(this.moduleID);
	webclient.inputmanager.addObject(module, module.element);
	webclient.inputmanager.bindEvent(module, "keydown", eventIconViewKeyboard);
	if(typeof eventTableViewKeyboard != "undefined") {
		webclient.inputmanager.unbindEvent(module, "keydown", eventTableViewKeyboard);
	}
	webclient.inputmanager.bindKeyControlEvent(module, module.keys["select"], "keyup", eventIconViewKeyCtrlSelectAll, true);

	if(typeof dragdrop != "undefined") dragdrop.addTarget(dragdrop.targets["folder"]["scrollelement"], this.element, "folder");
}

/**
 * Function will resize all elements in the view
 */
IconView.prototype.resizeView = function()
{
	// mousedown event will generate focusid which is used to 
	// execute every other events on icons
	dhtml.executeEvent(this.element, "mousedown");
}

/**
 * Function will adds items to the view
 * @param {Object} items Object with items
 * @param {Array} properties property list
 * @param {Object} action the action tag
 * @return {Array} list of entryids
 */
IconView.prototype.execute = function(items, properties, action)
{
	var entryids = false;

	for(var i=0;i<items.length;i++){
		if (!entryids) {
			entryids = new Object();
		}
		var item = this.createItem(items[i]);
		entryids[item["id"]]= item["entryid"];
	}
	this.resizeView();
	
	return entryids;
}

IconView.prototype.addItem = function()
{
	return false;
}

/**
 * Function will add one "item" tot the view or replace "element" 
 * @param {Object} item
 * @param {HtmlElement} element changed item
 * @return {Object} entry item for entryID list
 */
IconView.prototype.createItem = function(item, element)
{
	// get properties
	var entryid = dhtml.getTextNode(item.getElementsByTagName("entryid")[0],"");
	var subject =  dhtml.getTextNode(item.getElementsByTagName("subject")[0],"");
	var icon_index = dhtml.getTextNode(item.getElementsByTagName("icon_index")[0],"");
	var message_class = dhtml.getTextNode(item.getElementsByTagName("message_class")[0],"");
	
	var id = this.moduleID+"_"+entryid;
	var className = "large_view "+iconIndexToClassName(icon_index,message_class)+"_large";
	className += " "+message_class.toLowerCase().replace(".","_");

	if(element){
		// update item
		newItem = element;
		newItem.className = className;
	}else{
		// create new item
		var newItem = dhtml.addElement(this.element,"div",className,id);
	}
	dhtml.addElement(newItem,"span","","",subject);
	
	// add context menu
	if (this.events && this.events["row"]){
		dhtml.setEvents(this.moduleID, newItem, this.events["row"]);
	}
	
	// add dragdrop
	if(typeof(dragdrop) != "undefined") {
		dragdrop.addDraggable(newItem, "folder");
	}
	
	var entry = Object();
	var entry = new Object();
	entry["id"] = id;
	entry["entryid"] = entryid;

	return entry;
}

/**
 * Function will update a item
 * @param {Object} element
 * @param {Object} item
 * @param {Object} properties
 * @return {Object} entry item for entryID list
 */
IconView.prototype.updateItem = function(element, item, properties)
{
	if(item) {
		dhtml.deleteAllChildren(element);
		return this.createItem(item,element);
	}
	return undefined;
}

/**
 * Function will show Loading text in view
 */
IconView.prototype.loadMessage = function()
{
	dhtml.removeEvents(this.element);
	dhtml.deleteAllChildren(this.element);

	this.element.innerHTML = "<center>" + _("Loading") + "...</center>";
	document.body.style.cursor = "wait";
}

/**
 * Function will delete load text in view
 */
IconView.prototype.deleteLoadMessage = function()
{
	dhtml.deleteAllChildren(this.element);
	this.initView();
	document.body.style.cursor = "default";
}


// FIXME FIXME this is called with 'mobuleObject' referring not to this view oject, but to our parent module!
function eventIconViewKeyboard(moduleObject, element, event)
{
	// Set to TRUE if we have really selected the item, like pressing on an item with the mouse
	var openItem = false;
	
	if (typeof moduleObject != "undefined"){

		if (event.type == "keydown"){

			// get the right element
			if (moduleObject && moduleObject instanceof ListModule && typeof moduleObject.selectedMessages[0] != "undefined"){
				switch (event.keyCode){
					case 46: // DELETE
						// event.shiftKey for soft delete if Shift+Del
						moduleObject.deleteMessages(moduleObject.selectedMessages, event.shiftKey);
						break;
				}
			}
		}
	}
}
/**
 * Function which selects all items.
 */
function eventIconViewKeyCtrlSelectAll(moduleObject, element, event)
{
	var elements = new Array();
	for (var item in moduleObject.itemProps){
		var element = dhtml.getElementById(moduleObject.id +"_"+ item);
		if (element){
			elements.push(element);
		}
	}
	moduleObject.selectMessages(elements, "large_viewselected");
}