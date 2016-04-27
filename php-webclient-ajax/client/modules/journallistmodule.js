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

journallistmodule.prototype = new ListModule;
journallistmodule.prototype.constructor = journallistmodule;
journallistmodule.superclass = ListModule.prototype;

function journallistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

journallistmodule.prototype.init = function(id, element, title, data)
{
	journallistmodule.superclass.init.call(this, id, element, title, data);
	this.initializeView();

	this.menuItems.push(webclient.menu.createMenuItem("seperator", ""));
	this.menuItems.push(webclient.menu.createMenuItem("reply", _("Reply"), _("Reply"), eventMailListReplyMessage));
	this.menuItems.push(webclient.menu.createMenuItem("replyall", _("Reply All"), _("Reply All"), eventMailListReplyAll));
	this.menuItems.push(webclient.menu.createMenuItem("forward", _("Forward"), _("Forward"), eventMailListForwardMessage));
	webclient.menu.buildTopMenu(this.id, "createmail", this.menuItems, eventListNewMessage);

	var items = new Array();
	// depending on maillistmodule for these event handlers
	items.push(webclient.menu.createMenuItem("open", _("Open"), false, eventListContextMenuOpenMessage));
	items.push(webclient.menu.createMenuItem("print", _("Print"), false, eventListContextMenuPrintMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("reply", _("Reply"), false, eventMailListContextMenuReply));
	items.push(webclient.menu.createMenuItem("replyall", _("Reply All"), false, eventMailListContextMenuReplyAll));
	items.push(webclient.menu.createMenuItem("forward", _("Forward"), false, eventMailListContextMenuForward));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("markread", _("Mark Read"), false, eventMailListContextMenuMessageFlag));
	items.push(webclient.menu.createMenuItem("markunread", _("Mark Unread"), false, eventMailListContextMenuMessageFlag));
	items.push(webclient.menu.createMenuItem("categories", _("Categories"), false, eventListContextMenuCategoriesMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("delete", _("Delete"), false, eventListContextMenuDeleteMessage));
	items.push(webclient.menu.createMenuItem("copy", _("Copy/Move Message"), false, eventListContextMenuCopyMessage));
	this.contextmenu = items;
}

