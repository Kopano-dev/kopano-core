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

stickynotelistmodule.prototype = new ListModule;
stickynotelistmodule.prototype.constructor = stickynotelistmodule;
stickynotelistmodule.superclass = ListModule.prototype;

function stickynotelistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

stickynotelistmodule.prototype.init = function(id, element, title, data)
{
	this.entryid = data["entryid"];
	this.defaultview = webclient.settings.get("folders/entryid_"+this.entryid+"/selected_view", "icon");
	
	stickynotelistmodule.superclass.init.call(this, id, element, title, data);
	this.keys["view"] = KEYS["view"];
	this.initializeView();

	this.menuItems.push(webclient.menu.createMenuItem("seperator", ""));
	this.menuItems.push(webclient.menu.createMenuItem("stickynote_icon", _("Icon"), _("Icon View"), eventStickynotelistSwitchView));
	this.menuItems.push(webclient.menu.createMenuItem("stickynote_list", _("List"), _("List View"), eventStickynotelistSwitchView));
	webclient.menu.buildTopMenu(this.id, "stickynote", this.menuItems, eventListNewMessage);

	var items = new Array();
	items.push(webclient.menu.createMenuItem("open", _("Open"), false, eventListContextMenuOpenMessage));
	items.push(webclient.menu.createMenuItem("print", _("Print"), false, eventListContextMenuPrintMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("categories", _("Categories"), false, eventListContextMenuCategoriesMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("delete", _("Delete"), false, eventListContextMenuDeleteMessage));
	this.contextmenu = items;

	// Used by keycontrol, to switch between views
	this.availableViews = new Array("icon", "list");
}

/**
 * Function will cleare selectedMessages and then call messagelist of superclass.
 * When user adds one note whole view is loaded again but selectedMessage variable is not cleared.
 * Note: If addItem function is added for icon view then this function is not needed.
 */
stickynotelistmodule.prototype.messageList = function(action)
{
	this.selectedMessages = new Array();
	stickynotelistmodule.superclass.messageList.call(this, action);
}

stickynotelistmodule.prototype.initializeView = function(view)
{
	if (view){
		webclient.settings.set("folders/entryid_"+this.entryid+"/selected_view", view);
	}
	this.setTitle(this.title, NBSP, true);
	this.contentElement = dhtml.addElement(this.element, "div");
	this.selectedview = view;
	
	this.viewController.initView(this.id, (view?view:this.defaultview), this.contentElement, this.events, false);

	webclient.inputmanager.addObject(this, this.element);
	if (!webclient.inputmanager.hasKeyControlEvent(this, "keyup", eventNoteListKeyCtrlChangeView)){
		webclient.inputmanager.bindKeyControlEvent(this, this.keys["view"], "keyup", eventNoteListKeyCtrlChangeView);
	}
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["refresh"], "keyup", eventListKeyCtrlRefreshFolder);
}

function eventStickynotelistSwitchView(moduleObject, element, event)
{
	var newView = element.id.split("_")[1];
	moduleObject.destructor(moduleObject);
	moduleObject.initializeView(newView);
	moduleObject.list();
	moduleObject.resize();
}
/**
 * Function will change view when keys are presses
 */
function eventNoteListKeyCtrlChangeView(moduleObject, element, event, keys)
{
	var newView = false;

	// Get next View
	if (event.keyCombination == this.keys["view"]["prev"]){
		newView = array_prev(moduleObject.selectedview, moduleObject.availableViews);
	} else if (event.keyCombination == this.keys["view"]["next"]){
		newView = array_next(moduleObject.selectedview, moduleObject.availableViews);
	}

	if (newView){
		moduleObject.destructor(moduleObject);
		moduleObject.initializeView(newView);
		moduleObject.list();
		moduleObject.resize();
	}
}