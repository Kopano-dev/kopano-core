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

attachitemlistmodule.prototype = new ListModule;
attachitemlistmodule.prototype.constructor = attachitemlistmodule;
attachitemlistmodule.superclass = ListModule.prototype;

function attachitemlistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

/**
 * Function will initialize this module
 */
attachitemlistmodule.prototype.init = function(id, element, title, data)
{
	attachitemlistmodule.superclass.init.call(this, id, element, title, data);

	this.attachments = new Array();
	this.tableGenerated = false;

	// hierarchy module element
	this.hierarchyElement = dhtml.getElementById("attach_item_targetfolder", "div", this.element);
	// table widget element
	this.contentElement = dhtml.getElementById("attach_item_tablewidget", "div", this.element);
	// initialize hierarchy tree
	this.initHierarchy();
	// initialize table widget
	this.initTableWidget();

	this.selectedFolders = false;
	this.containerClass = false;
}

/**
 * Function will initialize hierarchy tree in the module
 */
attachitemlistmodule.prototype.initHierarchy = function()
{
	// Load hierarchy module
	this.hierarchy = new hierarchyselectmodule();
	if(this.hierarchy) {
		var hierarchyID = webclient.addModule(this.hierarchy);
		this.hierarchy.addEventHandler("changefolder", this.hierarchy, this.hierarchyChangeFolder);
		this.hierarchy.init(hierarchyID, this.hierarchyElement, false);
		this.hierarchy.list();
	
		this.hierarchy.selectFolder(parentWebclient.hierarchy.defaultstore.defaultfolders.inbox);
	}
}

/**
 * Function will initialize table widget to show items of the selected folder
 * table widget is initialized only once, and then its reused
 */
attachitemlistmodule.prototype.initTableWidget = function()
{
	// initialize table widget
	this.tableWidget = new TableWidget(this.contentElement, true, false, true);
}

/**
 * Function which execute an action. This function is called by the XMLRequest object.
 * @param string type the action type
 * @param object action the action tag 
 */ 
attachitemlistmodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "list":
			this.tableWidget.resetWidget();
			this.messageList(action);
			break;
		case "attach_items":
			this.setAttachItemData(action);
			break;
		case "attach_items_in_body":
			this.setAttachAsBodyItemData(action);
			break;
	}
}

/**
 * Function which sends a request to the server, with the action "list".
 * which request the data items of the selected folder from hierarchy
 */ 
attachitemlistmodule.prototype.list = function()
{
	this.selectedFolders = this.hierarchy.getFolder(this.hierarchy.selectedFolder);
	this.containerClass = this.selectedFolders["container_class"];

	var data = new Object();
	data["store"] = this.hierarchy.selectedFolderStoreId;
	data["entryid"] = this.hierarchy.selectedFolder;
	data["container_class"] = this.containerClass;

	data["restriction"] = new Object();

	if(this.getRestrictionData) {
		data["restriction"] = this.getRestrictionData(); 
		if(data["restriction"] == false)
			return; // Abort list if module requires restriction but has none
	}

	data["restriction"]["start"] = this.rowstart;

	webclient.xmlrequest.addData(this, "list", data);
	webclient.xmlrequest.sendRequest();
}

/**
 * Function will add all items in the tableview, and also add columns to tableview
 * @param XMLNode action the action tag
 */
attachitemlistmodule.prototype.messageList = function(action)
{
	// get columns data from XML response
	this.getColumnDataFromXML(action);

	// get sorting info from XML response
	this.getSortDataFromXML(action);

	// add columns in table widget that is fetched from XML response
	attachItemAddColumns(this, this.properties);

	var items = action.getElementsByTagName("item");
	if(items.length > 0) {
		// store properties of messages
		this.getItemPropsFromXML(items);

		// create row column data and pass it to table widget to create layout
		this.tableGenerated = attachItemSetRowColumnData(this, items, this.properties);
	}else{
		this.tableWidget.showZeroResultMessage();
	}

	this.paging(action);
}

/**
 * Function which will add the selected items as embedded message(s) 
 *			attachments while composing the mail.
 * @param object action the action tag 
 */ 
attachitemlistmodule.prototype.setAttachItemData = function(action)
{
	window.windowData.module.messageAction = "forwardasattachment";
	window.windowData.module.setAttachItemData(action);
	window.close();
}

/**
 * Function which will add the selected items as text message attachments in body of the composing mail.
 * @param object action the action tag 
 */ 
attachitemlistmodule.prototype.setAttachAsBodyItemData = function(action)
{
	window.windowData.module.messageAction = "forwardasattachment";
	if(window.windowData.module.message_action == "reply" || window.windowData.module.message_action == "replyall"){
		window.windowData.module.deleteattachments = new Object();
		window.windowData.module.attachments = new Array();
	}

	var message = action.getElementsByTagName("item");
	for(var i=0;i<message.length;i++){
		window.windowData.module.setBody(message[i], true, false, true);
	}
	window.close();
}

/**
 * Function which takes care of the paging element
 * @param object action the action tag 
 */ 
attachitemlistmodule.prototype.paging = function(action, noReload)
{
	if(action) {
		var page = action.getElementsByTagName("page")[0];

		if(page) {
			var rowstart = page.getElementsByTagName("start")[0];
			if(rowstart && rowstart.firstChild) {
				this.rowstart = rowstart.firstChild.nodeValue * 1;
			}

			var rowcount = page.getElementsByTagName("rowcount")[0];
			if(rowcount && rowcount.firstChild) {
				this.rowcount = rowcount.firstChild.nodeValue * 1;
			}

			var totalrowcount = page.getElementsByTagName("totalrowcount")[0];
			if(totalrowcount && totalrowcount.firstChild) {
				this.totalrowcount = totalrowcount.firstChild.nodeValue * 1;
			}

			if(this.totalrowcount > this.rowcount) {
				var selected = attachItemPagingElement(this, this.totalrowcount, this.rowcount, this.rowstart);

				if(!selected && !noReload) {
					this.rowstart -= this.rowcount;
					this.list();
				}
			} else {
				attachItemRemovePagingElement(this);
				if(this.rowstart > 0 && !noReload) {
					this.rowstart = 0;
					this.list();
				}
			}
		}
	}
}

/**
 * @destructor 
 */
attachitemlistmodule.prototype.destructor = function()
{
	// remove paging elements
	if(this.pagingTool) {
		this.pagingTool.destructor();
		dhtml.deleteAllChildren(dhtml.getElementById("pageelement_"+ this.id, "div", this.element).parentNode);
	}

	// call destructor of widgets
	this.tableWidget.destructor();

	// remove registered events
	dhtml.removeEvents(this.element);

	attachitemlistmodule.superclass.destructor(this);
}

/**
 * Function called when hierarchy module has selected a folder
 */
attachitemlistmodule.prototype.hierarchyChangeFolder = function()
{
	// Simply call the list function to show the data in list.
	module.list();
}

/**
 * Function will be called when user double clicks on a message in table widget
 * @param Object tblWidget table widget object
 * @param String type type of event
 * @param HexString rowId rowid of element row
 * @param EventObject event event object
 */
attachitemlistmodule.prototype.eventAttachItemRowDblClick = function(tblWidget, type, rowId, event) {
	if(typeof rowId != "undefined" && rowId) {
		addAttachmentItems(this, window.dialog_attachment);
	}
}

/**
 * Function which resizes the view.
 */ 
attachitemlistmodule.prototype.resize = function()
{
	var dialogDimensions = dhtml.getBrowserInnerSize();
	var dialogHeight = dialogDimensions['y'];
	var dialogWidth = dialogDimensions['x'];

	var tableWidgetElement = dhtml.getElementById("attach_item_tablewidget", "div", this.element);

	// Calcuate how much space there is from the top of the tableWidgetElement till the bottom of 
	// dialog. That space, minus 10 pixels to have an edge at the bottom, will be the new height of
	// the tableWidget.
	var tableWidgetTopOffset = dhtml.getElementTop(module.contentElement);
	tableWidgetElement.style.height = (dialogHeight - tableWidgetTopOffset - 10) + "px";

	// Size the width of the widget element 12 pixels smaller than the inner size of the dialog to 
	// not let the scrollbar disappear under the edge of the dialog.
	tableWidgetElement.style.width = (dialogWidth-12)+'px';
	// resize table widget
	this.tableWidget.resize();
}

/**
 * Event which is called when any folder in hierarchy tree is been selected
 * @param Object moduleObject
 * @param Html Element the element on which event occured
 * @param EventObject event event object
 */
function eventHierarchyChangeFolder(moduleObject, element, event)
{
	if(!event) {
		event = new Object();
	}
	
	if(event.button == 0 || event.button == 1 || !event.button) {
		var storeid = false;
		if(moduleObject.defaultstore) {
			storeid = moduleObject.defaultstore["id"];
		}
	
		var folder = moduleObject.getFolder(element.parentNode.id);
	
		if(folder) {
			var data = new Object();
			var storeid = moduleObject.folderstoreid;

			// Opened folder of another user will contain id and foldertype followed by '_', so remove foldertype
			if(storeid.indexOf("_") > 0)
				storeid = storeid.substr(0,storeid.indexOf("_"));
			
			moduleObject.setNumberItems(folder["content_count"], folder["content_unread"]);
			moduleObject.sendEvent("changefolder", storeid, folder);
			moduleObject.setDocumentTitle(folder);
		}
	}
}