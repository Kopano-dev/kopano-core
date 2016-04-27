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
 * TodayTaskView
 * @type  View
 * @classDescription  This view is use for todaytasklistmodule 
 */
TodayTaskView.prototype = new View;
TodayTaskView.prototype.constructor = TodayTaskView;
TodayTaskView.superclass = View.prototype;

/**
 * @constructor 
 * @param moduleID is the parent module
 * @param element is the element where the view should be placed
 * @param events are the events that should be handled for this view
 * @param data are any view-specific data
 */
function TodayTaskView(moduleID, element, events, data, uniqueid)
{
	if(arguments.length > 0) {
		this.init(moduleID, element, events, data, uniqueid);
	}
}

TodayTaskView.prototype.init = function(moduleID, element, events, data, uniqueid)
{
	this.element = element;
		
	this.moduleID = moduleID;
	this.events = events;
	this.data = data;

	if(uniqueid)
		this.uniqueid = uniqueid;
	else
		this.uniqueid = "entryid";

	this.initView();
}

/**
 * Function which intializes the view
 */ 
TodayTaskView.prototype.initView = function()
{
}

/**
 * Function which adds items to the view.
 * @param object items the items
 * @param array properties property list
 * @param object action the action tag
 */ 
TodayTaskView.prototype.execute = function(items, properties, action)
{
	return this.addRows(items);
}

/**
 * Function which will show the uncompleted task.  
 * @param array rows array of all uncompleted task.
 */
TodayTaskView.prototype.addRows = function(rows)
{
	var entryids = new Object();
	var table = new Array();

	table.push("<table id='task' class='align' border='0' cellpadding='0' cellspacing='0'>");
	
	for(var i = 0; i < rows.length; i++) 
	{
		var row = rows[i];

		if(row.childNodes.length > 0) {
			var entryid = dhtml.getXMLValue(row, "entryid", false);
			var importance = dhtml.getXMLValue(row, "importance", false);
			var value = NBSP;

			entryids["module"+this.moduleID+"_"+i] = entryid;
			table.push("<tr id='module"+ this.moduleID +"_"+ i +"' class='height'>");

			this.renderRowColumn(table, "complete", value, row, i);
			this.renderRowColumn(table, "importance", importance, row, i);
			this.renderRowColumn(table, "subject", value, row, i);

			table.push("</tr>");
		}
	}
	table.push("</table>");	
	this.element.innerHTML = table.join("");

	var tableElement = this.element.getElementsByTagName("table")[0];

	// Attach events
	for (var i = 0; i < tableElement.rows.length; i++) {
		var row = tableElement.rows[i];

		for (var j = 0; j < row.cells.length; j++){
			var columnName = row.cells[j].id;
			var index = columnName.indexOf("_");

			// Retrive column name
			while(index > 0){
				columnName = columnName.substr(index + 1);
				index = columnName.indexOf("_");
			}

			if (this.events["rowcolumn"][columnName]){
				dhtml.setEvents(this.moduleID, tableElement.rows[i].cells[j], this.events["rowcolumn"][columnName]);
			}
		}
	}
	
	return entryids;
}

TodayTaskView.prototype.updateItem = function(element, item, properties) 
{
	var entry = Object();

	var property = new Object();
	property["complete"] = dhtml.getXMLValue(item, "complete", 0);
	property["subject"] = dhtml.getXMLValue(item, "subject", "").htmlEntities();
	property["importance"] = dhtml.getXMLValue(item, "importance", 1);

	for(var prop in property){
		var columnElement = dhtml.getElementById("module"+ this.moduleID +"_"+ element.rowIndex +"_"+ prop);

		this.setRowColumn(columnElement, prop, property[prop], item);
	}

	entry["id"] = element.id;
	entry[this.uniqueid] = item.getElementsByTagName(this.uniqueid)[0].firstChild.nodeValue;

	return entry;
}

/**
 * todaytasklistmodule's item() is called from its parent.
 * item() call addItem() to add one item, which is not needed for task column.
 * so addItem() is overwritten.
 */
TodayTaskView.prototype.addItem = function(item, properties, action)
{
}

TodayTaskView.prototype.renderRowColumn = function(table, type, value, item, rowNumber)
{
	var settings = this.getRowColumnSettings(type, value, item);

	// Check for specific actions to be executed.
	var messageClass = dhtml.getXMLValue(item, "message_class", false);
	if(messageClass) {
		var messageSettings = this.getMessageSettings(messageClass, value, item);
		if(messageSettings["className"] && settings["value"] != NBSP) {
			settings["className"] += " " + messageSettings["className"];
		}
	}

	table.push("<td id='module"+ this.moduleID +"_"+ rowNumber +"_"+ type +"' class='" + settings["className"] + "' "+ (settings["width"]?"width='"+ settings["width"] +"'":"") +">"+ settings["value"] +"</td>");
}

TodayTaskView.prototype.setRowColumn = function(element, type, value, item)
{
	var settings = this.getRowColumnSettings(type, value, item);

	// Check for specific actions to be executed.
	var messageClass = dhtml.getXMLValue(item, "message_class", false);
	if(messageClass) {
		var messageSettings = this.getMessageSettings(messageClass, value, item);
		if(messageSettings["className"] && settings["value"] != NBSP) {
			settings["className"] += " " + messageSettings["className"];
		}
	}
	element.innerHTML = settings["value"];
	element.className = settings["className"];

	if(settings["width"]) {
		element.style.width = settings["width"] + "px";
	}
 
}

TodayTaskView.prototype.getRowColumnSettings = function(type, value, item)
{
	var width = false;
	var className = "";

	switch(type)
	{
		case "complete":
			var checked = (parseInt(value, 10)) ? "checked" : "";
			value = "<input type='checkbox' "+ checked +"/>";
			width = 25;
			break;
		case "importance":
			className += " message_icon ";

			switch(parseInt(value, 10))
			{
				case 0:
					className += "icon_importance_low";
					break;
				case 2:
					className += "icon_importance_high";
					break;		
			}
			value = NBSP;
			break;
		case "subject":
			var subject = dhtml.getXMLValue(item, "subject", "").htmlEntities();
			var duedate = dhtml.getXMLValue(item, "duedate", false);

			value = "<span>"+ subject + NBSP + (duedate? "("+ strftime('%d/%m/%Y', duedate) +")": "("+ _("None") +")") +"</span>";
			className += " data ";
	}

	var rowcolumnsettings = new Array();
	rowcolumnsettings["value"] = value;
	rowcolumnsettings["width"] = width;
	rowcolumnsettings["className"] = className;
	
	return rowcolumnsettings;
}

TodayTaskView.prototype.getMessageSettings = TableView.prototype.getMessageSettings;

