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

Menu.prototype = new Widget;
Menu.prototype.constructor = Menu;
Menu.superclass = Widget.prototype;

function Menu()
{
	this.defaultMenuItems = new Array();
	this.defaultMenuItems.push(this.createMenuItem("folder", _("New"), _("Folder")));
	this.defaultMenuItems.push(this.createMenuItem("seperator", ""));
	this.defaultMenuItems.push(this.createMenuItem("createmail", _("New"), _("Email Message")));
	this.defaultMenuItems.push(this.createMenuItem("appointment", _("New"), _("Appointment")));
	this.defaultMenuItems.push(this.createMenuItem("meetingrequest", _("New"), _("Meeting Request")));
	this.defaultMenuItems.push(this.createMenuItem("contact", _("New"), _("Contact")));
	this.defaultMenuItems.push(this.createMenuItem("distlist", _("New"), _("Distribution List")));
	this.defaultMenuItems.push(this.createMenuItem("task", _("New"), _("Task")));
	this.defaultMenuItems.push(this.createMenuItem("assigntask", _("New"), _("Task Request")));
	this.defaultMenuItems.push(this.createMenuItem("stickynote", _("New"), _("Note")));

	this.menuBar = dhtml.getElementById("menubar");
	this.menuBarLeft = dhtml.getElementById("menubar_left");
	this.menuBarRight = dhtml.getElementById("menubar_right");
	
	this.defaultstoreid = false;
}

Menu.prototype.createMenuItem = function(id, name, title, callbackfunction, shortcut, toggle, data)
{
	var menuitem = new Object();
	menuitem["id"] = id;
	menuitem["name"] = name;
	menuitem["title"] = title;
	
	if(callbackfunction) {
		menuitem["callbackfunction"] = callbackfunction;
	}
	if(shortcut){
		menuitem["shortcut"] = shortcut;
	}
	if(toggle){
		menuitem["toggle"] = toggle;
	}
	if(data){
		menuitem["data"] = data;
	}

	return menuitem;
}

// The menu is built invisible by default, you must call 'showMenu()' when you want to show it
Menu.prototype.buildMenu = function(moduleID, items)
{
	this.menuBarLeft.style.display = "none";
	for(var i in items)
	{
		var menuitem = items[i];
		if(menuitem["id"].indexOf("seperator") == 0) {
			if(menuitem["id"] == "seperator")
				this.menuBarLeft.appendChild(this.buildSeperator());
			else
				this.menuBarLeft.appendChild(this.buildSeperator(menuitem["id"]));
		} else {
			this.menuBarLeft.appendChild(this.buildMenuItem(moduleID, menuitem["id"], menuitem));
		}
	}
	
	this.menuBarLeft.appendChild(this.buildSeperator());
}

Menu.prototype.showMenu = function()
{
	if (this.menuBarLeft)
		this.menuBarLeft.style.display = "block";
}

Menu.prototype.buildTopMenu = function(moduleID, mainItem, items, createNewItemFunction)
{
	this.buildDefaultMenu(moduleID, mainItem, createNewItemFunction);
	var defaultMenuItem = this.getDefaultMenuItem(mainItem);
	
	if(defaultMenuItem) {
		if(!defaultMenuItem["callbackfunction"])
            defaultMenuItem["callbackfunction"] = createNewItemFunction;

		defaultMenuItem["shortcut"] = "N";
		this.menuBarLeft.appendChild(this.buildMenuItem(moduleID, mainItem, defaultMenuItem));
		
		var arrowElement = dhtml.addElement(this.menuBarLeft, "span", "menubutton icon_arrow", "defaultmenu_arrow");
		arrowElement.innerHTML = "&nbsp;";
		
		dhtml.addEvent(-1, arrowElement, "mouseover", eventMenuMouseOverTopMenuItem);
		dhtml.addEvent(-1, arrowElement, "mouseout", eventMenuMouseOutTopMenuItem);
		dhtml.addEvent(-1, arrowElement, "click", eventMenuChangeDefaultMenuState);
		
		this.menuBarLeft.appendChild(this.buildSeperator());
		this.buildMenu(moduleID, items);
	}
}

Menu.prototype.buildDefaultMenu = function(moduleID, mainItem, createNewItemFunction)
{
	var defaultMenuItem = this.getDefaultMenuItem(mainItem);
	
	if(defaultMenuItem) {
		var defaultmenu = dhtml.addElement(document.body, "div", false, "defaultmenu");
		
		var mainElement = dhtml.addElement(defaultmenu, "div", "menuitem icon_" + mainItem, false, defaultMenuItem["title"]);
		dhtml.addEvent(-1, mainElement, "mouseover", eventMenuMouseOverMenuItem);
		dhtml.addEvent(-1, mainElement, "mouseout", eventMenuMouseOutMenuItem);
        /*
         * For the click event we should check whether a certain callback function is registered.
         * If it is we should call that one, but by default all the menu items do not use a special
         * callback function, these use the eventMenuNewDefaultMessage callback function instead.
         */
		dhtml.addEvent(moduleID, mainElement, "click", defaultMenuItem["callbackfunction"] ? defaultMenuItem["callbackfunction"] : createNewItemFunction);

		dhtml.addElement(defaultmenu, "div", "icon_menuitemseperator");
		
		for(var i in this.defaultMenuItems)
		{
			var menuitem = this.defaultMenuItems[i];
			
			if(menuitem["id"] != mainItem) {
				if(menuitem["id"].indexOf("seperator") == 0) {
					dhtml.addElement(defaultmenu, "div", "icon_menuitemseperator", menuitem["id"] );
				} else {
					var element = dhtml.addElement(defaultmenu, "div", "menuitem icon_" + menuitem["id"], false, menuitem["title"]);
					dhtml.addEvent(-1, element, "mouseover", eventMenuMouseOverMenuItem);
					dhtml.addEvent(-1, element, "mouseout", eventMenuMouseOutMenuItem);
                    /*
                     * For the click event we should check whether a certain callback function is registered.
                     * If it is we should call that one, but by default all the menu items do not use a special
                     * callback function, these use the eventMenuNewDefaultMessage callback function instead.
                     */
					dhtml.addEvent(-1, element, "click", menuitem["callbackfunction"] ? menuitem["callbackfunction"] : eventMenuNewDefaultMessage);
				}
			}
		}
	}
}

Menu.prototype.buildMenuItem = function(moduleID, name, menuItem, data)
{
	var item = dhtml.addElement(false, "a", "menubutton icon icon_" + name, name);
	item.title = menuItem["title"];
	item.innerHTML = "&nbsp;";

	if(menuItem["name"]) {
		item.innerHTML += menuItem["name"];
	} else {
		item.style.backgroundPosition = "center center";
	}
	
	if(menuItem["shortcut"]) {
		item.setAttribute("accesskey",menuItem["shortcut"]);
	}

	if(menuItem["toggle"]) {
		item.toggle = true;
		item.toggleState = false;
	}
	if(menuItem["data"]) {
		item.data = menuItem["data"];
	}

	dhtml.addEvent(-1, item, "mouseover", eventMenuMouseOverTopMenuItem);
	dhtml.addEvent(-1, item, "mouseout", eventMenuMouseOutTopMenuItem);
	dhtml.addEvent(-1, item, "mousedown", eventMenuMouseDown);

	if(menuItem["callbackfunction"]) {
		dhtml.addEvent(moduleID, item, "click", menuItem["callbackfunction"]);
	}

	return item;
}

Menu.prototype.buildContextMenu = function(moduleID, elementid, items, posX, posY)
{
	if(dhtml.getElementById("contextmenu")) { // FIXME: close any other context menu if exists
		dhtml.executeEvent(document.body, "mouseup");
	}
	
	// fix for mouse cursor position with Firefox/Iseweasel under Linux
	posX -= 1;
	posY -= 1;

	var contextmenu = dhtml.addElement(false, "div", false, "contextmenu");
	contextmenu.elementid = elementid;
	contextmenu.style.top = posY + "px";

	if(posX + 150 > document.documentElement.clientWidth) {
		posX = document.documentElement.clientWidth - 152;
	}
	contextmenu.style.left = posX + "px";
	
	// Div element containing Iframe should be the top element in menu.
	this.createIframeInMenu(contextmenu);

	for(var i = 0; i < items.length; i++)
	{
		if(items[i]["id"] == "seperator") {
			var seperator = dhtml.addElement(contextmenu, "div", "icon_menuitemseperator");
			seperator.style.height = "1px";
		} else {
			var menuitem = dhtml.addElement(contextmenu, "div", "menuitem icon_" + items[i]["id"], items[i].id, items[i]["name"]);
			if(items[i]["name"]){
				menuitem.data = items[i]["data"];
			}

			dhtml.addEvent(-1, menuitem, "mouseover", eventMenuMouseOverMenuItem);
			dhtml.addEvent(-1, menuitem, "mouseout", eventMenuMouseOutMenuItem);
			dhtml.addEvent(-1, menuitem, "mousedown", eventMenuMouseDown);

			if(items[i]["callbackfunction"]) {
				dhtml.addEvent(moduleID, menuitem, "click", items[i]["callbackfunction"]);
			}
		}
	}

	document.body.appendChild(contextmenu);
	
	// Check Y position contextmenu (Y + contextmenu height > body height)
	if((posY + contextmenu.clientHeight) > document.documentElement.clientHeight) {
		if((document.documentElement.clientHeight - contextmenu.clientHeight) > 0) {
			contextmenu.style.top = (document.documentElement.clientHeight - contextmenu.clientHeight) + "px";
		}
	}
	
	// Set size of iframe contained by contextmenu to fix 'select tag' problem in IE6.
	this.setMenuIframeSize(contextmenu);
}

Menu.prototype.buildSeperator = function(id)
{
	var seperator = dhtml.addElement(false, "div", "icon_seperator", id);
	seperator.innerHTML = "&nbsp;";
	return seperator;
}

Menu.prototype.getDefaultMenuItem = function(id)
{
	var menuitem = false;
	
	for(var i in this.defaultMenuItems)
	{
		if(this.defaultMenuItems[i]["id"] == id) {
			menuitem = this.defaultMenuItems[i];
		}
	}
	
	return menuitem;
}

Menu.prototype.reset = function()
{
	this.items = new Object();
	dhtml.deleteElement(this.menuBarLeft);
	
	if(dhtml.getElementById("defaultmenu")) {
		dhtml.deleteElement(dhtml.getElementById("defaultmenu"));
	}
	
	this.menuBarLeft = dhtml.addElement(this.menuBar, "div", false, "menubar_left");
}

Menu.prototype.toggleItem = function(element, state)
{
	if (element.toggle){
		element.toggleState = state;
		if (element.toggleState){
			dhtml.addClassName(element, "menubuttonover");
		}else{
			dhtml.removeClassName(element, "menubuttonover");
		}
	}
}

function eventMenuMouseOverTopMenuItem(moduleObject, element, event)
{
	if (!element.toggle || !element.toggleState){
		dhtml.addClassName(element, "menubuttonover");
	}
}

function eventMenuMouseOutTopMenuItem(moduleObject, element, event)
{
	if (!element.toggle || !element.toggleState){
		dhtml.removeClassName(element, "menubuttonover");
	}
}

function eventMenuMouseOverMenuItem(moduleObject, element, event)
{
	if (!element.toggle || !element.toggleState){
		dhtml.addClassName(element, "menuitemover");
	}
}

function eventMenuMouseDown(moduleObject, element, event)
{
	event.stopPropagation();
}

function eventMenuMouseOutMenuItem(moduleObject, element, event)
{
	if (!element.toggle || !element.toggleState){
		dhtml.removeClassName(element, "menuitemover");
	}
}

function eventMenuChangeDefaultMenuState(moduleObject, element, event)
{
	var defaultMenu = dhtml.getElementById("defaultmenu");

	if(defaultMenu) {
		if(defaultMenu.style.display == "" || defaultMenu.style.display == "none") {
			defaultMenu.style.display = "block";
		} else {
			defaultMenu.style.display = "none";
		}
	}
}

function eventMenuNewDefaultMessage(moduleObject, element, event)
{
	// This function is only called when selecting an option from the "New"-pulldown menu
	// for the button, see eventListNewMessage in listmodule.js

	element.parentNode.style.display = "none";
	
	var messageClass = false;
	var classNames = element.className.split(" ");
	var hierarchymodule = webclient.hierarchy;
	var extraParams = false;
	
	var selectedFolder = hierarchymodule.getFolder(hierarchymodule.selectedFolder);
	var containerClass = "IPF.Note";
	if (selectedFolder && selectedFolder.container_class){
		containerClass = selectedFolder.container_class;
	}

	for(var index in classNames)
	{
		if(classNames[index].indexOf("icon_") >= 0) {
			messageClass = classNames[index].substring(classNames[index].indexOf("_") + 1);
		}
	}
	
	var storeID = hierarchymodule.defaultstore.id;
	var folderEntryID = hierarchymodule.defaultstore.defaultfolders.drafts;

	if(messageClass){
		switch(messageClass){
			case "meetingrequest":
				messageClass = "appointment";
				extraParams = "meeting=true";
			case "appointment":
				folderEntryID = hierarchymodule.defaultstore.defaultfolders.calendar;
				var dtmodule = webclient.getModulesByName("datepickerlistmodule");
				if (dtmodule[0] && dtmodule[0].selectedDate){
					var newappDate = new Date(addHoursToUnixTimeStamp(dtmodule[0].selectedDate, webclient.settings.get("calendar/workdaystart",9*60)));
					extraParams = (extraParams?extraParams+"&":"")+("date="+parseInt(newappDate.getTime()/1000));
				}
				break;
			case "distlist":
				// check if selected folder can contain contacts, this check is needed when we want to add distlists to other contact folders
				folderEntryID = hierarchymodule.defaultstore.defaultfolders.contact;
				if (containerClass == "IPF.Contact"){
					folderEntryID = selectedFolder.entryid;
					storeID = selectedFolder.storeid;
				}
				break;
			case "contact":
				folderEntryID = hierarchymodule.defaultstore.defaultfolders.contact;
				break;
			case "assigntask":
				messageClass = "task";
				extraParams = "taskrequest=true";
			case "task":
				folderEntryID = hierarchymodule.defaultstore.defaultfolders.task;
				break;
			case "stickynote":
				folderEntryID = hierarchymodule.defaultstore.defaultfolders.note;
				break;			
		}
	}
	if(messageClass && messageClass != "folder") {
		webclient.openWindow(moduleObject, messageClass, DIALOG_URL+"task=" + messageClass + "_standard&storeid=" + storeID +"&parententryid="+folderEntryID+(extraParams?"&"+extraParams:""));
	}
	else{
		webclient.openModalDialog(hierarchymodule, messageClass, DIALOG_URL+"task=createfolder_modal&parent_entryid=" + hierarchymodule.selectedFolder+(extraParams?"&"+extraParams:""), 300, 400, null, null, {parentModule: hierarchymodule});
	}
}
/**
 * Function which places iframe behind the menu,
 * so that menu can come over the 'select' tag in IE6.
 * @param element contextmenu menu
 */
Menu.prototype.createIframeInMenu = function (contextmenu)
{
	var div = dhtml.addElement(contextmenu, "div", "menutopdiv");
	var iframe = dhtml.addElement(div, "iframe");
}
/**
 * Function which sets size of iframe within menu.
 */
Menu.prototype.setMenuIframeSize = function (contextmenu)
{
	var menuiframe = contextmenu.getElementsByTagName("iframe")[0];
	if (menuiframe){
		menuiframe.style.width = parseInt(contextmenu.clientWidth+2) +"px";  //width + border of contentmenu
		menuiframe.style.height = contextmenu.clientHeight +"px";
	}
}
