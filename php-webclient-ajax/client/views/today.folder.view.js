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
 * TodayFolderView
 * @type  View
 * @classDescription  This view is use for todayfolderlistmodule 
 */
TodayFolderView.prototype = new View;
TodayFolderView.prototype.constructor = TodayFolderView;
TodayFolderView.superclass = View.prototype;

function TodayFolderView(moduleID, element, events, data, uniqueid)
{	
	if(arguments.length > 0) {
		this.init(moduleID, element, events, data, uniqueid);
	}
}

/**
 * @constructor 
 * @param moduleID is the parent module
 * @param element is the element where the view should be placed
 * @param events are the events that should be handled for this view
 * @param data are any view-specific data
 */
TodayFolderView.prototype.init = function(moduleID, element, events, data)
{
	this.element = element;
		
	this.moduleID = moduleID;
	this.events = events;
	this.data = data;
	
	this.initView();
}

/**
 * Function which intializes the view
 */ 
TodayFolderView.prototype.initView = function()
{
}

/**
 * Function which adds items to the view.
 * @param object items the items
 * @param array properties property list
 * @param object action the action tag
 */ 
TodayFolderView.prototype.execute = function(items, properties, action)
{
	var rows = new Array();
	//convert object in to array to use shift() function.
	for (var i = 0; i < items.length; i++) {
		rows[i] = items[i];
	}
	// call the function to create view and add data into it.
	this.addFolders(rows);
}

/**
 * Function which will show folder and their unread messages.  
 */
TodayFolderView.prototype.addFolders = function(itemData)
{
	var items = this.fetchDataFromXML(itemData);
	var message_count;
	//create table
	var folderTable = dhtml.addElement(this.element, "table", "align", "folder_table");
	// to avoide a small break in table cells.
	folderTable.setAttribute("cellspacing",0);
	
	var tbody = dhtml.addElement(folderTable, "tbody", "", "");

	for (var i = 0; i < items.length; i++ )
	{
		var item = items[i];
		message_count = (webclient.hierarchy.defaultstore.defaultfolders["drafts"] == item["entryid"]) ? item["content_count"] : item["content_unread"];

		var message_class = (message_count == 0) ? "messagecountzero" : "messagecount";
		// create tr
		var folderTr = dhtml.addElement(tbody, "tr", "", "module"+ this.moduleID +"_"+ item["entryid"]);

		// create cell for name
		var nameTd = dhtml.addElement(folderTr, "td", "todayfolderdisplayname", item["entryid"] +"_display_name", item["display_name"]);

		// create cell for count
		var content_column_id = item["entryid"] +"_"+ ((webclient.hierarchy.defaultstore.defaultfolders["drafts"] == item["entryid"]) ? "content_count" : "content_unread");
		var countTd = dhtml.addElement(folderTr, "td", message_class, content_column_id, message_count);

		// attach events
		dhtml.setEvents(this.moduleID, folderTr, this.events);
	}	
}
/**
 * Function which fetch all data from XML object into a JS Object.
 * @param array rows XML Data array of objects
 * @return array dataObj JS object
 */
TodayFolderView.prototype.fetchDataFromXML = function(items){
	var dataObj = new Array();
	for (var i in items)
	{
		var item = items[i];
		var jsObj = new Object();
		jsObj["entryid"] = dhtml.getXMLValue(item, "entryid", false);
		jsObj["display_name"] = dhtml.getXMLValue(item, "display_name", false);
		jsObj["content_count"] = dhtml.getXMLValue(item, "content_count", false);
		jsObj["content_unread"] = dhtml.getXMLValue(item, "content_unread", false);
		jsObj["extended_folder_flags"] = dhtml.getXMLValue(item, "extended_folder_flags", false);
		dataObj.push(jsObj);
	}
	return dataObj;
}

TodayFolderView.prototype.updateItem = function(element, folder)
{
	for (var i = 0; i < element.cells.length; i++){
		var column = element.cells[i];
		var id = column.id.substr(column.id.indexOf("_") + 1);
		var value = dhtml.getXMLValue(folder, id);

		this.setRowColumn(column, id, value);
	}
}

TodayFolderView.prototype.setRowColumn = function(element, type, value)
{
	var innerHTML = NBSP;
	var className = "";

	switch(type)
	{
		case "display_name":
			className += " todayfolderdisplayname";
			innerHTML = value;
			break;
		case "content_count":
		case "content_unread":
			className += (parseInt(value, 10) == 0) ? "messagecountzero" : "messagecount";
			innerHTML = value;
			break;
	}

	element.innerHTML = innerHTML;
	element.className = className;
}