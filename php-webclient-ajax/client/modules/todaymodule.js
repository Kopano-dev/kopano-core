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
 * todayModule extend from the module.
 */
todaymodule.prototype = new Module;
todaymodule.prototype.constructor = todaymodule;
todaymodule.superclass = Module.prototype;

function todaymodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

/**
 * Function which intializes the module.
 * @param integer id id
 * @param object element the element for the module
 * @param string title the title of the module
 * @param object data the data (storeid, entryid, ...)  
 */ 
todaymodule.prototype.init = function(id, element, title, data)
{
	todaymodule.superclass.init.call(this, id, element, title);
	
	// Array that contains list of all modules loaded within Today View.
	this.modules = new Object();
	
	this.viewController = new ViewController();
	
	if (data){
		for(var property in data){
			this[property] = data[property];
		}
	}
	
	//for the topmenu (display new and print button).
	webclient.menu.reset();
	this.menuItems = new Array();
	this.menuItems.push(webclient.menu.createMenuItem("print", false, _("Print"), eventListPrintMessage));
	webclient.menu.buildTopMenu(this.id, "createmail", this.menuItems, eventListNewMessage);
	webclient.menu.showMenu();
	
	this.layout = new Object();
	this.layout["calendar"] = new Object();
	this.layout["tasks"] = new Object();
	this.layout["messages"] = new Object();
	
	//get style value from the customize webaccess today settings dialog box.
	this.style = parseInt(webclient.settings.get("today/style/styletype","1"));
	this.setLayoutAsStyle(this.style);
	
	this.initializeView();
}

/**
 * Function which intializes the view.
 */ 
todaymodule.prototype.initializeView = function()
{
	var date = new Date();
	if (this.title!=false){
		this.setTitle(this.title, date.strftime("%A, %B %d, %Y"), true);
	}
	this.customizeTodayViewButton();	

	this.contentElement = dhtml.addElement(this.element, "div", "todayview", "todayview");
	this.viewController.initView(this.id, "today", this.contentElement, false, false);

	this.modules["todayappointmentlistmodule"] = this.addModule(this.storeid, webclient.hierarchy.defaultstore.defaultfolders.calendar, "todayappointmentlistmodule", _("Calendar"), this.layout["calendar"]["position"], this.layout["calendar"]["insert_element_at"]);
    this.modules["todaytasklistmodule"] = this.addModule(this.storeid, webclient.hierarchy.defaultstore.defaultfolders.task, "todaytasklistmodule", _("Tasks"), this.layout["tasks"]["position"], this.layout["tasks"]["insert_element_at"]);
    this.modules["todayfolderlistmodule"] = this.addModule(this.storeid, webclient.hierarchy.defaultstore.defaultfolders.inbox, "todayfolderlistmodule", _("Messages"), this.layout["messages"]["position"], this.layout["messages"]["insert_element_at"]);
    this.resize();
}

/**
 * Function which add the different modules to the todaymodule.
 * @param string name modulename
 * @param string title title of module 
 * @param string storeid storeid
 * @param string entryid entryid
 * @param string position position of module where it should be loaded
 * @param integer insert_element_at
 * @return object module
 */ 
todaymodule.prototype.addModule = function(storeid, entryid, name, title, position, insert_element_at)
{
	var module = webclient.dispatcher.loadModule(name);
	if (module){
		var moduleID = webclient.addModule(module);
		var data = new Object();
		data["storeid"] = storeid; 
		data["entryid"] = entryid;
		data["has_no_menu"] = true;
		
		var element = this.viewController.viewObject.addModule(moduleID, position, insert_element_at);
	
		module.todaymodule = this;
		module.init(moduleID, element, title, data);
		module.list();
	}
	return module;
}

/**
 * As today module has no any request to send, list() is overwritten.
 */ 
todaymodule.prototype.list = function(useTimeOut)
{
}

/**
 * function will create "customize webaccess today" button.
 */ 
todaymodule.prototype.customizeTodayViewButton = function()
{
	var settingButton = dhtml.addElement(false, "span", "button", "button");
	dhtml.addEvent(this, settingButton, "mousedown", eventCustomizedSettingButtonMouseDown);
	var cutomizebutton = dhtml.addElement(settingButton, "a", "buttonborder", false, _("Customize Webaccess Today..."));
	cutomizebutton.href = "#";
	
	//get the subtitle element and append the "customize today view" button.
	var elements = dhtml.getElementsByClassNameInElement(this.element, "subtitle", "div")[0];
	elements.appendChild(settingButton);
}

/**
 * function gives position and place to the module in today view.
 * @param integer style style of the layout.
 */ 
todaymodule.prototype.setLayoutAsStyle = function(style)
{
	switch(style)
	{
		case 2:
			this.layout["calendar"]["position"] = "first";
			this.layout["calendar"]["insert_element_at"] = ""; 
			this.layout["tasks"]["position"] = "third";
			this.layout["tasks"]["insert_element_at"] = ""; 
			this.layout["messages"]["position"] = "third";
			this.layout["messages"]["insert_element_at"] = INSERT_ELEMENT_AT_TOP; 
			break;
		case 3:
			this.layout["calendar"]["position"] = "third";
			this.layout["calendar"]["insert_element_at"] = ""; 
			this.layout["tasks"]["position"] = "third";
			this.layout["tasks"]["insert_element_at"] = ""; 
			this.layout["messages"]["position"] = "third";
			this.layout["messages"]["insert_element_at"] = ""; 
			break;
		case 1:	
		default:
			this.layout["calendar"]["position"] = "first";
			this.layout["calendar"]["insert_element_at"] = "";
			this.layout["tasks"]["position"] = "second"; 
			this.layout["tasks"]["insert_element_at"] = "";
			this.layout["messages"]["position"] = "third";
			this.layout["messages"]["insert_element_at"] = ""; 
			break;
	}
}

/**
 * function call when todayview is resize.
 */ 
todaymodule.prototype.resize = function()
{
	this.viewController.viewObject.resizeView(this.style);
}

// Fuction will call when mousedown on the customize button.
function eventCustomizedSettingButtonMouseDown(moduleObject, element, event) 
{
	//pass the list of task folder to the dialog box for the task settings combo box.
	var data = new Object();
	data = moduleObject;

	webclient.openModalDialog(this, "customizetoday", DIALOG_URL + "task=customizetoday_modal", 400, 400, false, null, data);
}
