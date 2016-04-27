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
 * todayfolderlistmodule extend from the TodayListModule.
 */
todayfolderlistmodule.prototype = new TodayListModule;
todayfolderlistmodule.prototype.constructor = todayfolderlistmodule;
todayfolderlistmodule.superclass = TodayListModule.prototype;

function todayfolderlistmodule(id, element, title, data)
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
todayfolderlistmodule.prototype.init = function(id, element, title, data)
{
	todayfolderlistmodule.superclass.init.call(this, id, element, title, data);

	this.events = new Object();
	this.events["click"] = eventFolderClick;
	this.events["mouseover"] = eventTodayMessageMouseOver;
	this.events["mouseout"] = eventTodayMessageMouseOut;
	
	this.initializeView();
}

/**
 * Function which intializes the view.
 */
todayfolderlistmodule.prototype.initializeView = function()
{		
	if (this.title) {
		this.setTitleInTodayView(this.title);
	}	
	this.contentElement = dhtml.addElement(this.element, "div", "main");
	this.viewController.initView(this.id, "todayFolder", this.contentElement, this.events);
}

/**
 * Function which sends a request to the server, with the action "list".
 */ 
todayfolderlistmodule.prototype.list = function()
{
	var data = new Object();
	data["entryid"] = this.entryid;
	data["store"] = this.storeid;
	var folders = webclient.hierarchy.defaultstore.defaultfolders;
	data["selectedfolders"] = new Object();
	data["selectedfolders"]["entryids"] = new Array();
	data["selectedfolders"]["entryids"].push(folders["inbox"]);
	data["selectedfolders"]["entryids"].push(folders["drafts"]);
	data["selectedfolders"]["entryids"].push(folders["sent"]);
	webclient.xmlrequest.addData(this, "list", data);
}

/**
 * Function which execute an action. This function is called by the XMLRequest object.
 * @param string type the action type
 * @param object action the action tag 
 */ 
todayfolderlistmodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "list":
			this.messageList(action);
			break;
		case "folder":
			this.updateFolder(action);
			break;
	}
}

/**
 * Function which takes care of the list action. 
 * @param object action the action tag
 */ 
todayfolderlistmodule.prototype.messageList = function(action)
{
	this.viewController.addItems(action.getElementsByTagName("folder"), false, action);
	this.resize();
}

todayfolderlistmodule.prototype.updateFolder = function(action)
{
	var folder = dhtml.getXMLNode(action, "folder", 0);

	if (folder){
		var entryid = dhtml.getXMLValue(folder, "entryid", false);
		var folderElement = dhtml.getElementById("module"+ this.id +"_"+ entryid);

		if (folderElement){
			this.viewController.updateItem(folderElement, folder);
		} else {
			
		}
	}
}

todayfolderlistmodule.prototype.resize = function()
{
	this.todaymodule.resize();
}

/**
 * Function will call when mousedown on message header or any folder(inbox,draft or ourbox).
 * This will open the foloder view.
 */
function eventFolderClick(moduleObject, element, event) 
{
	var folderId = element.id.substr(element.id.indexOf("_") + 1);
	webclient.hierarchy.selectFolder(true, folderId);
}

