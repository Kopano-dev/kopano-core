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
 * ListModule
 * The list module extends from Module 
 */
ListModule.prototype = new Module;
ListModule.prototype.constructor = ListModule;
ListModule.superclass = Module.prototype;

function ListModule(id, element, title, data)
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
ListModule.prototype.init = function(id, element, title, data, uniqueid)
{
	ListModule.superclass.init.call(this, id, element, title);

	this.dblclick = false;
	
	// Storeid
	this.storeid = false;
	
	// Folder Entryid
	this.entryid = false;
	
	// List of entryids in table
	this.entryids = new Object();
	
	// Properties which are shown in the table view as columns.
	this.propertylist = new Array();
	this.properties = new Array();
	
	// Contains all the properties for every item in this module (keys are the entryids/uniqueids)
	this.itemProps = new Object();
	
	// Sort array
	this.sort = false;
	
	// Columns which should be added or deleted in the next request
	this.columns = false;

	//entry id for changing importance of item
	this.messageID = false;	
	
	//contains unixtimestamp of dates...
	this.unixtime = new Object();
	
	// Previous value of item is stored in this variable
	this.previousvalue = new Array();
	
	// Previously selected message
	this.prevselectedMessage = false;
	this.prevselMessage = false;
	
	// Message which is in the editable form
	this.editMessage = false;
	
	//stopbubble
	this.stopbubble = false;
	
	// Table
	// Start row in the table
	this.rowstart = 0;
	
	// Rowcount, which holds the number of rows queried (25, 50, ...)
	this.rowcount = 0;
	
	// Total number of rows in the folder
	this.totalrowcount = 0;
	
	// Selected messages in the table, which will be used for delete, copy or open.
	this.selectedMessages = new Array();
	
	// This is the first selected message in a range; ie it is reset when you single-select a message
	this.selectedFirstMessage = 0;
	
	// Select contextmenu message
	this.selectedContextMessage = false;

	// specifies search is currently running or not
	this.searchInProgress = false;

	// string that will be used for searching
	this.filterRestriction = false;

	// target properties on which restriction will be build for searching
	this.filterRestrictionTarget = false;

	// flag for subfolder searching
	this.searchSubfolders = false;

	// entryid of the search folder
	this.searchFolderEntryID = false;

	// specifies store supports search folder or not
	this.useSearchFolder = false;

	// Unique ID for this module (a column id that uniquely identifies a row)
	if(!uniqueid)
		this.uniqueid = "entryid";
	else
		this.uniqueid = uniqueid;

	// reference of container element of preview pane
	this.previewPane = false;

	// module object of previewreadmailitemmodule
	this.previewreadmailitemmodule = false;

	// unique ids of selected messages, used to select old selected messages when reloading WA
	this.oldSelectedMessageIds = new Array();

	// flag to indicate that when reloading WA message selection should be preserved or not,
	// if no selection is found then by default it will select the first message
	this.preserveSelectionOnReload = true;

	// View Object
	this.viewController = new ViewController();

	if (!data || !data["has_no_menu"]) {
		webclient.menu.reset();
		this.menuItems = new Array();
		this.menuItems.push(webclient.menu.createMenuItem("delete", false, _("Delete messages"), eventListDeleteMessages));
		this.menuItems.push(webclient.menu.createMenuItem("copy", false, _("Copy/Move messages"), eventListCopyMessages));
		this.menuItems.push(webclient.menu.createMenuItem("print", false, _("Print"), eventListPrintMessage));
		this.menuItems.push(webclient.menu.createMenuItem("addressbook", false, _("Addressbook"), eventListShowAddressbook));
		this.menuItems.push(webclient.menu.createMenuItem("seperator", ""));
		this.menuItems.push(webclient.menu.createMenuItem("advanced_find", _("Advanced Find"), _("Advanced Find"), eventListShowAdvancedFindDialog));
		this.menuItems.push(webclient.menu.createMenuItem("seperator", ""));
		//Add one more button to the main menu bar for performing the action of restore items
		this.menuItems.push(webclient.menu.createMenuItem("restoreitem", _("Restore Items"), _("Restore deleted Items") , eventRestoreItems));
	}
	
	var items = new Array();
	items.push(webclient.menu.createMenuItem("open", _("Open"), false, eventListContextMenuOpenMessage));
	items.push(webclient.menu.createMenuItem("print", _("Print"), false, eventListContextMenuPrintMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("categories", _("Categories"), false, eventListContextMenuCategoriesMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("delete", _("Delete"), false, eventListContextMenuDeleteMessage));
	//items.push(webclient.menu.createMenuItem("copy", _("Copy/Move Message"), false, eventListContextMenuCopyMessage));
	this.contextmenu = items;

	// Tooltip messages
	this.tooltip = new Array();
	this.tooltip["importance"] = new Array();
	this.tooltip["importance"]["value"] = new Array();
	this.tooltip["importance"]["value"][0] = _("Low");
	this.tooltip["importance"]["value"][1] = _("Normal");
	this.tooltip["importance"]["value"][2] = _("High");
	this.tooltip["importance"]["css"] = new Array();
	this.tooltip["importance"]["css"][0] = "icon_importance_low";
	this.tooltip["importance"]["css"][1] = "icon_taskarrow";
	this.tooltip["importance"]["css"][2] = "icon_importance_high";
	
	// Events which set in the table
	this.events = new Object();
	this.events["column"] = new Object();
	this.events["column"]["click"] = eventListColumnSort;
	this.events["column"]["contextmenu"] = eventListColumnContextMenu;

	this.events["menu"] = new Object();
	this.events["menu"]["importance"] = new Object();
	this.events["menu"]["importance"]["mouseover"] = eventMenuImportanceMouseOver;
	this.events["menu"]["importance"]["click"] = eventMenuImportanceClick;
	this.events["menu"]["importance"]["mousedown"] = eventListStopBubble;
	this.events["menu"]["importance"]["mouseout"]  = eventMenuImportanceMouseOut;
	
	this.events["insertrow"] = new Object();
	this.events["insertrow"]["click"] = eventInputFieldClick;
	this.events["insertrow"]["mousedown"] = eventDeselectMessages;
	this.events["insertrow"]["keydown"]= eventClickSaveMessage;
	this.events["insertrow"]["mousemove"]= eventListInsertRowMouseMove;
	this.events["insertrow"]["selectstart"]= eventListInsertRowMouseMove;
	
	this.events["insertcolumn"] = new Object();
	this.events["insertcolumn"]["importance"] = new Object();
	this.events["insertcolumn"]["importance"]["mousedown"] = eventClickChangeImportance;

	this.events["row"] = new Object();
	this.events["row"]["mousedown"] = eventListMouseDownMessage;
	this.events["row"]["mouseup"] = eventListMouseUpMessage;
	this.events["row"]["dblclick"] = eventListDblClickMessage;
	this.events["row"]["contextmenu"] = eventListContextMenuMessage;
	this.events["row"]["mousemove"]= eventListRowMouseMove;
	this.events["row"]["selectstart"]= eventListRowMouseMove;
	
	this.events["rowcolumn"] = new Object();
	this.events["rowcolumn"]["icon_index"] = new Object();
	this.events["rowcolumn"]["icon_index"]["click"] = eventListChangeReadFlag;
	// stop selection of row when changing read flag
	this.events["rowcolumn"]["icon_index"]["mousedown"] = eventStopBubbling;
	this.events["rowcolumn"]["icon_index"]["mouseup"] = eventStopBubbling;
	this.events["rowcolumn"]["flag_icon"] = new Object();
	this.events["rowcolumn"]["flag_icon"]["mouseover"] = eventListMouseOverFlag;
	this.events["rowcolumn"]["flag_icon"]["contextmenu"] = eventListContextMenuClick;
	// Linux FF opens contextmenu on rightclick mouse down event.so stop propagating mouseup event for it.
	this.events["rowcolumn"]["flag_icon"]["mouseup"] = eventListFlagIconMouseUp;
	this.events["rowcolumn"]["flag_icon"]["mouseout"] = eventListMouseOutFlag;
	this.events["rowcolumn"]["flag_icon"]["click"] = eventListChangeFlagStatus;
	this.events["rowcolumn"]["complete"] = new Object();
	this.events["rowcolumn"]["complete"]["click"] = eventListChangeCompleteStatus;
	this.events["rowcolumn"]["complete"]["mouseup"] = eventListStopBubble;
	this.events["rowcolumn"]["complete"]["mousedown"] = eventListStopBubble;
	this.events["rowcolumn"]["importance"] = new Object();
	this.events["rowcolumn"]["importance"]["mouseup"] = eventClickChangeImportance;
	this.events["rowcolumn"]["importance"]["mousedown"] = eventListStopBubble;
	
	dhtml.addEvent(this.id, document.body, "mouseup", eventListCheckSelectedContextMessage);
	
	// Set onDrop Event
	if(typeof(dragdrop) != "undefined") {
		dragdrop.setOnDropGroup("folder", this.id, eventListDragDropTarget);
	}
	
	this.addEventHandler("openitem", this, this.onOpenItem);

	this.setData(data);
	
	// Enable these features by default
	this.enableSorting = true;
	this.enableVariableColumns = true;

	this.keys["select"] = KEYS["select"];
	this.keys["edit_item"] = KEYS["edit_item"];
	this.keys["quick_edit"] = KEYS["quick_edit"];
	this.keys["refresh"] = KEYS["refresh"];
	this.keys["search"] = KEYS["search"];

	// register object for keycontrol
	webclient.inputmanager.addObject(this, this.element);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["search"], "keyup", eventListKeyCtrlSearch, false);
}

/**
 * Function which intializes the view.
 */ 
ListModule.prototype.initializeView = function()
{
	if (this.title!=false){
		this.setTitle(this.title, false, true);
	}
	this.contentElement = dhtml.addElement(this.element, "div");

	this.viewController.initView(this.id, "table", this.contentElement, this.events, null, this.uniqueid);
}

ListModule.prototype.setTitle = function()
{
	ListModule.superclass.setTitle.apply(this, arguments);
	this.initDragMessagesToDesktop();
}

/**
 * Initialize the drag messages to desktop functionality that is allowed by the ZarafaDnD Firefox
 * extension. 
 */
ListModule.prototype.initDragMessagesToDesktop = function()
{
	if(!window.ZarafaDragMessagesProxy)
		return false;

	// Find the title element 
	var elems = dhtml.getElementsByClassNameInElement(this.element, "title", "div", true);
	var titleElement = elems[0];

	if(!titleElement)
		return false;
	
	// Create an icon and make it draggable and therefor allow it to trigger the dragstart event in firefox
	this.dragMessageIcon = dhtml.addElement(titleElement, "div", "zarafaDnDMessageIcon icon icon_zarafadnd_message_icon", null, NBSP);
	this.dragMessageIcon.setAttribute("draggable","true");

	dhtml.addEvent(this, this.dragMessageIcon, "dragstart", function(moduleObject, element, event){
		if(moduleObject.selectedMessages.length == 0){
			alert(_("You have to select a message to drag it to your desktop."));
		}else if(moduleObject.selectedMessages.length > 1){
			alert(_("You cannot drag more than one message to your desktop at the same time."));
		}else{
			// Get the entryid and subject of the message
			var messageEntryId = moduleObject.entryids[moduleObject.selectedMessages[0]];
			var messageSubject = moduleObject.itemProps[ messageEntryId ]["subject"];
			// Generate filename from subject and remove invalid characters
			messageSubject = messageSubject.replace(/[\\/:*?:<>|]/g, "") + ".eml";
			// Create the URL to download the message with the correct store and message entryid.
			var action = window.location.href + "?load=download_message&fileType=eml&store=" + moduleObject.storeid + "&entryid=" + messageEntryId;
			// Use the ZarafaDragMessagesProxy implemented by the Zarafa Firefox extension
			window.ZarafaDragMessagesProxy.startDragMessages(event, Array(action), Array(messageSubject));//"http://webdev.ztest.nl/steve/gen.jpg"));//,"http://webdev.ztest.nl/simon/debug.zip"));
		}
	});
	// To trigger the dragstart event we need the mousedown event to be handled by the browser
	dhtml.addEvent(this, this.dragMessageIcon, "mousedown", forceDefaultActionEvent);
}

/**
 * Function which resizes the view.
 */ 
ListModule.prototype.resize = function()
{
	if(this.element){
		var contentElementHeight = this.element.offsetHeight - this.contentElement.offsetTop - 5;
		contentElementHeight > 0? this.contentElement.style.height = contentElementHeight + "px": "" ;
	}
	this.viewController.resizeView();
}


/**
 * Function which execute an action. This function is called by the XMLRequest object.
 * @param string type the action type
 * @param object action the action tag 
 */ 
ListModule.prototype.execute = function(type, action)
{
	webclient.menu.showMenu();
	
	switch(type)
	{
		case "list":
			this.messageList(action);
			break;
		case "item":
			this.item(action);
			break;
		case "delete":
			this.deleteItems(action);
			break;
		case "swap":
			this.swapItems(action);
			break;
		case "failed":
			this.handleActionFailure(action);
			break;
		case "search_error":
			this.handleSearchError(action);
			break;
		case "error":
			this.handleError(action);
			break;
	}
}

/**
 * Report the failure of an action
 */
ListModule.prototype.handleActionFailure = function(action)
{
	switch(dhtml.getXMLValue(action, "action_type", "none")){
		case "delete":
		case "copy":
		case "move":
			alert(_("The operation is completed, but not all items could be copied, deleted or moved."));
			break;
	}
}

/**
 * Function will show a warning to user about error occured on server side
 * it will also stop search on client side
 * @param		XMLNode		action		the action tag
 */
ListModule.prototype.handleSearchError = function(action)
{
	// refresh view
	this.clearSearchBar();

	// show error message to user
	var errorMessage = dhtml.getXMLValue(action, "error_message", false);

	if(errorMessage) {
		alert(errorMessage);
	}
}

/**
 * Swaps two rows, the entryids are in the XML action
 */
ListModule.prototype.swapItems = function(action)
{
	var entryids = action.getElementsByTagName(this.uniqueid);
	if(entryids.length != 2)
		return;

	var entryid1 = dhtml.getXMLNode(action, this.uniqueid, 0).firstChild.nodeValue;		
	var entryid2 = dhtml.getXMLNode(action, this.uniqueid, 1).firstChild.nodeValue;

	var rowid1 = this.getRowId(entryid1);
	var rowid2 = this.getRowId(entryid2);
	
	var elem1 = dhtml.getElementById(rowid1);
	var elem2 = dhtml.getElementById(rowid2);
	
	var parent = elem1.parentNode;

	// InsertBefore doesn't only insert, it also removes the element from
	// the old position. So this is a swap	
	parent.insertBefore(elem1, elem2);
}

/**
 * Function which takes care of the list action. It is responsible for
 * calling the "addItems" function in the view.
 * @param object action the action tag
 */ 
ListModule.prototype.messageList = function(action)
{
	//properties for insert column
	this.inputproperties = new Array();

	// get columns data from XML response
	this.getColumnDataFromXML(action);

	// get inputColumns data from XML response
	this.getInputColumnDataFromXML(action);

	// get sorting info from XML response
	this.getSortDataFromXML(action);

	this.viewController.deleteLoadMessage();

	this.entryids = this.viewController.addItems(action.getElementsByTagName("item"), this.properties, action, false, this.inputproperties);
	
	// check full GAB is enabled/disabled
	var disable_gab_value = dhtml.getXMLValue(action, "disable_full_gab", false);
	if (disable_gab_value) {
		this.viewController.GAB(disable_gab_value);
	}
	
	// store properties of messages
	var items = action.getElementsByTagName("item");
	this.getItemPropsFromXML(items);

	this.paging(action);

	if(this.executeOnLoad) {
		this.executeOnLoad();
	}

	if(this.preserveSelectionOnReload) {
		// Call the function to select the old selected items if there is any, after reloading the mail list with new mails.
		// if there isn't any row selected previously then we need to select first item
		this.selectOldSelectedMessages();
	}

	/**
	 * when searching in public folders we are not using search folders,
	 * so check here that search returned zero results or not
	 */
	if(!this.useSearchFolder && this.searchInProgress) {
		if(this.totalrowcount == 0 && this.filterRestriction !== false) {
			// search returned zero results
			this.viewController.viewObject.showEmptyView(_("No matches found for '%s'.").sprintf(this.filterRestriction));
		}
	}
}

/**
 * Function will get properties of items from XML and store it in itemProps,
 * attributes in properties will be truncated
 * @param Array items array of item XMLNodes
 */
ListModule.prototype.getItemPropsFromXML = function(items)
{
	// remember item properties
	this.itemProps = new Object();

	for(var index = 0; index < items.length; index++) {
		this.updateItemProps(items[index]);
	}
}

/**
 * Function will parse column data from XML
 * @param XMLNode action action XML tag
 */
ListModule.prototype.getColumnDataFromXML = function(action)
{
	// reset variables
	this.propertylist = new Array();
	this.properties = new Array();

	var columns = action.getElementsByTagName("column");

	// Columns
	for(var index = 0; index < columns.length; index++) {
		var id = dhtml.getXMLValue(columns[index], "id", false);
		if(id !== false) {
			var property = new Object();
			property["id"] = id;
			property["order"] = dhtml.getXMLValue(columns[index], "order", false);
			property["name"] = dhtml.getXMLValue(columns[index], "name", false);
			property["title"] = dhtml.getXMLValue(columns[index], "title", false);
			property["length"] = dhtml.getXMLValue(columns[index], "length", false);
			property["visible"] = dhtml.getXMLValue(columns[index], "visible", false);
			property["type"] = dhtml.getXMLValue(columns[index], "type", false);

			// some data type conversions
			if(property["order"] !== false) {
				property["order"] = parseInt(property["order"], 10);
			}
			property["visible"] = parseInt(property["visible"], 10);

			if(webclient.hasRightPane){
				/**
				 * when preview pane is on right side, we are displaying only below 4 columns
				 * so discard other columns data
				 */
				switch(property["id"]){
					case ("display_to"):
					case ("sent_representing_name"):
					case ("client_submit_time"):
					case ("message_delivery_time"):
						this.propertylist.push(property);
						if(property["visible"]) {
							this.properties.push(property);
						}
						break;
				}
			}else{
				this.propertylist.push(property);
				if(property["visible"]) {
					this.properties.push(property);
				}
			}
		}
	}
}
/**
 * Function will parse column data from XML
 * @param XMLNode action action XML tag
 */
ListModule.prototype.getInputColumnDataFromXML = function(action)
{
	// reset variables
	this.inputproperties = new Array();

	var inputColumns = action.getElementsByTagName("insertcolumn");
	
	//input columns
	for(var index = 0; index < inputColumns.length; index++)
	{
		var id = dhtml.getXMLValue(inputColumns[index], "id", false);
		if(id !== false) {
			var inputproperty = new Object();
			inputproperty["id"] = id;
			inputproperty["order"] = dhtml.getXMLValue(inputColumns[index], "order", false);
			inputproperty["name"] = dhtml.getXMLValue(inputColumns[index], "name", false);
			inputproperty["title"] = dhtml.getXMLValue(inputColumns[index], "title", false);
			inputproperty["length"] = dhtml.getXMLValue(inputColumns[index], "length", false);
			inputproperty["visible"] = dhtml.getXMLValue(inputColumns[index], "visible", 0);
			inputproperty["type"] = dhtml.getXMLValue(inputColumns[index], "type", false);
			inputproperty["readonly"] = dhtml.getXMLValue(inputColumns[index], "readonly", false);

			// some data type conversions
			if(inputproperty["order"] !== false) {
				inputproperty["order"] = parseInt(inputproperty["order"], 10);
			}
			inputproperty["visible"] = parseInt(inputproperty["visible"], 10);

			//set order of input columns according to header columns (field chooser)
			for (var j=0;j<this.properties.length;j++) {
				if (this.properties[j]["id"] == inputproperty["name"] ) {
					inputproperty["order"] = this.properties[j]["order"];
					this.inputproperties.push(inputproperty);
				}
			}
		}
	}
}

/**
 * Function will create a object of sort data that is retrived from XML
 * @param XMLNode action action XML tag
 */
ListModule.prototype.getSortDataFromXML = function(action)
{
	var sort = dhtml.getXMLNode(action, "sort");

	if(sort && sort.firstChild) {
		var sortColumn = dhtml.getXMLValue(action, "sort");
		var sortDirection = sort.getAttribute("direction");

		// old way
		var column = new Object();
		column["attributes"] = new Object();
		column["attributes"]["direction"] = sortDirection;
		column["_content"] = sortColumn;
		this.sort = new Array(column);
	}
}

/**
 * Function which takes care of the paging element, above the table view.
 * @param object action the action tag 
 */ 
ListModule.prototype.paging = function(action, noReload)
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
				var selected = this.viewController.pagingElement(this.totalrowcount, this.rowcount, this.rowstart);

				if(!selected && !noReload) {
					this.rowstart -= this.rowcount;
					this.list();
				}
			} else {
				this.viewController.removePagingElement();
				if(this.rowstart > 0 && !noReload) {
					this.rowstart = 0;
					this.list();
				}
			}
		}
	}
}

/**
 * Function which selects the first item in the view.
 */ 
ListModule.prototype.selectFirstItem = function()
{
	var element = dhtml.getElementById("0");

	if(element) {
		eventListMouseDownMessage(this, element, false);
		eventListMouseUpMessage(this, element, false);
	}
}

/**
 * Function which adds or updates an item in the view.
 * @param object action the action tag 
 */ 
ListModule.prototype.item = function(action)
{
	if (this.editMessage !== false){
		this.removeinputfields(this);
	}

	var items = action.getElementsByTagName("item");
	this.editMessage = false;
	// Loop through all updated items (add/modify)
	if(items && items.length > 0) {
		for(var i = 0; i < items.length; i++)
		{
			var item = items[i];
			var entryid = dhtml.getXMLValue(item, this.uniqueid);
			var parent_entryid = dhtml.getXMLValue(item, "parent_entryid", false); 
			
			// When parent_entryid exists, it must be the same as the entryid of this listmodule, else we ignore this item.
			// It is only possible to get here with an invalid parent_entryid when we are using the search, you know this by 
			// the 'searchfolder' attribute on the action element.
			if(!parent_entryid || (parent_entryid==this.entryid && entryid) || action.getAttribute("searchfolder")) {
				// Get the DOM element ID for this object
				var rowid = this.getRowId(entryid);
				var element = dhtml.getElementById(rowid);

				// If it exists, it's an update
				if(element && !item.getAttribute("instance") && !element.getAttribute("basedate")) {
					var entry = this.viewController.updateItem(element, item, this.properties);
					
					if(entry) {
						// Remember this element id -> entry id mapping of this object
						this.entryids[rowid] = undefined;
						if(typeof entry['id'] == "object"){
							this.entryids[entry['id'][0]] = entry[this.uniqueid];
							this.entryids[entry['id'][1]] = entry[this.uniqueid];
						}else{
							this.entryids[entry['id']] = entry[this.uniqueid];
						}
					} else {
						// If the view doesn't support an update, post a refresh
						this.list();
						
						// Since we're reloading, we might as well stop what we're doing ...
						return;
					}
				} else if(element && element.getAttribute("basedate")) {
					// If updated item is recurring item than need to update all occurrence, so reload view
					this.list();

					// Since we're reloading, we might as well stop what we're doing ...
					return;
				} else {
					// Otherwise it's a new item
					var entry = this.viewController.addItem(item, this.properties, action);
					
					if(entry) {
						// Remember this element id -> entry id mapping of this object
						if(typeof entry['id'] == "object"){
							this.entryids[entry['id'][0]] = entry[this.uniqueid];
							this.entryids[entry['id'][1]] = entry[this.uniqueid];
						}else{
							this.entryids[entry['id']] = entry[this.uniqueid];
						}
					} else {
						// If the view doesn't support an add, post a refresh
						this.list();
						
						// Reload, might as well stop processing
						return;
					}
				}
			}

			// update property data of items
			this.updateItemProps(item);
		}
	}
}

/**
 * Function which deletes one or more items in the view.
 * @param object action the action tag 
 * @TODO: move this to the view
 */ 
ListModule.prototype.deleteItems = function(action)
{
	var entryid = action.getElementsByTagName(this.uniqueid);
	this.totalrowcount -= entryid.length;

	// check if one of the messages were selected
	var isSelected = false;
	// array which stores all rowids which are to be deleted.
	var selectedrowids = new Array();
	for(var i = 0; i<entryid.length; i++){
		var rowid = this.getRowId(entryid[i].firstChild.nodeValue);
		selectedrowids.push(rowid);
		if (this.isMessageSelected(rowid)){
			isSelected = true;
		}
	}
	
	// Select next (or if that does not exist, the previous) message if that message is selected
	if (isSelected){
		var first_entryid = entryid[entryid.length - 1];
		if(first_entryid && first_entryid.firstChild ) {
			var rowid = this.getRowId(first_entryid.firstChild.nodeValue);
			var item = dhtml.getElementById(rowid);

			if(item) {
				var nextItem = item.nextSibling;
				if(nextItem) {
					// find next item untill we get an unselected item
					while(inArray(nextItem.id, selectedrowids) === true) {
						if(nextItem.nextSibling) {
							nextItem = nextItem.nextSibling;
							continue;
						} else {
							// this is the last item, so no next item left
							nextItem = null;
							break;
						}
					}
				}

				// If there isnt any next item which is unselected then go for previous items
				if(!nextItem) {
					nextItem = item.previousSibling;
					if(nextItem) {
						while(inArray(nextItem.id, selectedrowids) === true) {
							if(nextItem.previousSibling) {
								nextItem = nextItem.previousSibling;
								continue;
							} else {
								// probably the whole view is empty or all items are selected
								nextItem = null;
								break;
							}
						}
					}
				}

				if(nextItem) {
					dhtml.executeEvent(nextItem, "mousedown");
					dhtml.executeEvent(nextItem, "mouseup");
        		} else {
    	    	    // No more items, select nothing
	    	        this.selectedMessages = new Array();
                }
            }
		}
	}
	
	// Remove elements from the view
	for(var i = 0; i < entryid.length; i++)
	{
		if(entryid[i] && entryid[i].firstChild) {
			var entryiditem = entryid[i].firstChild.nodeValue;

			var rowid = this.getRowId(entryiditem);
			
			this.sendEvent("deleteitem", entryiditem);
				
			while(rowid) {
				var item = dhtml.getElementById(rowid);

				// Loop through all elements for an appointment(for multiday appointments)
				while(item) {
					dhtml.deleteElement(item);
					item = dhtml.getElementById(rowid);
				}
				
				// Remove item properties
				this.deleteItemProps(entryiditem);

				// Also remove the mapping in the element id -> entryid mapping
				this.entryids[rowid] = null;
				rowid = this.getRowId(entryiditem);
			}
		}
	}
	
	this.actionAfterDelete(action);
}

/**
 * Function which executes a set of action after some items are deleted
 */ 
ListModule.prototype.actionAfterDelete = function(action)
{
	// Update paging
	if(typeof moduleObject != "undefined") {
		var pagingTool = moduleObject.viewController.viewObject.pagingTool;
	}
	if(pagingTool) {
		var pageElement = dhtml.getElementById("page_"+ this.id);
		var pagingToolElement = dhtml.getElementById("pageelement"+ this.id);
		
		dhtml.deleteAllChildren(pagingToolElement);
		pageElement.style.display = "none";
		
		pagingTool.destructor();

		this.paging(action);
	}

	// fix cursor
	var curPos = this.viewController.getRowNumber(this.viewController.getCursorPosition());
	var rowCount = this.viewController.getRowCount();
	if(rowCount == 0) { 
		curPos = false; // disable cursor; no rows 
	} else if (curPos >= rowCount){ 
		curPos = rowCount -1; // select last item when cursor was beyond the end 
	}
	
	this.viewController.setCursorPosition(this.viewController.getElemIdByRowNumber(curPos));
	
	// empty the right pane after checking that list is empty or not.
	if(this.previewreadmailitemmodule) {
		dhtml.deleteAllChildren(this.previewreadmailitemmodule.element);
	}
}

/**
 * Function which sets the data for this module (storeid, entryid, ...).
 * @param object data data
 */ 
ListModule.prototype.setData = function(data)
{
	if(data) {
		for(var property in data)
		{
			this[property] = data[property];
		}
	}
}

/**
 * Function which returns the corresponding rowid for an entryid.
 * @param string entryid entryid
 * @return int row id  
 */ 
ListModule.prototype.getRowId = function(entryid)
{
	var rowid = false;
	
	for(var i in this.entryids)
	{
		if(this.entryids[i] == entryid) {
			/**
			 * Check the element if that element is present in WA or not.
			 * this is added because of multiday appointment has 2 elements for single entryid 
			 * and if in the view there is only one element is present (1st or 2nd) then we need
			 * to know which element is present in html dom and need to return that to call updateItem
			 * otherwise it will call createItem function of view.
			 */
			if(dhtml.getElementById(i))
				rowid = i;
		}
	}
	
	return rowid;
}

/**
 * Function which sends a request to the server, with the action "list".
 * @param boolean useTimeOut use a time out for the request (@TODO check this parameter is not used anymore)
 * @param boolean noLoader use loader
 * @param boolean storeUniqueIds store selected items' unique ids before reloading listmodule
 * @param boolean reposition the cursor based on the selection. This does nothing if storeUniqueIds is undefined or false
 */ 
ListModule.prototype.list = function(useTimeOut, noLoader, storeUniqueIds, reposition)
{
	var data = new Object();
    data["store"] = this.storeid;
    data["entryid"] = this.entryid;

	this.sort = this.loadSortSettings();
    if(this.sort) {
        data["sort"] = new Object();
        data["sort"]["column"] = this.sort;
	}
    this.columns = this.loadFieldSettings();
    
    if(this.columns) {
        var tablecolumns = new Array();
        for(var i = 0; i < this.columns.length; i++)
        {
            var column = new Object();
            column["attributes"] = new Object();
            column["attributes"]["action"] = this.columns[i]["action"];
            
            if(this.columns[i]["order"] !== false) {
                column["attributes"]["order"] = this.columns[i]["order"];
            }
            
            column["_content"] = this.columns[i]["id"];
            tablecolumns.push(column);	
        }
        
        data["columns"] = new Object();
        data["columns"]["column"] = tablecolumns;
        
        this.columns = new Array();
    }
    
    data["restriction"] = new Object();
    
    if(this.getRestrictionData) {
        data["restriction"] = this.getRestrictionData(); 
        if(data["restriction"] == false)
            return; // Abort list if module requires restriction but has none
    }
    
    data["restriction"]["start"] = this.rowstart;

	if(this.searchSubfolders !== false) {
		// if search folder then also check for subfolders option
		data["subfolders"] = this.searchSubfolders;
	}

	if(this.useSearchFolder !== false) {
		data["use_searchfolder"] = this.useSearchFolder;
	}

	// Retrieve data in extended (address cards view, calendar view, etc...) way 
	// or the normal (table) way.
	data["data_retrieval"] = (this.viewController.view=="table"?"normal":"extended");

	if(typeof storeUniqueIds != "undefined" && storeUniqueIds) {
		// store unique ids of items that are currently selected
		this.storeSelectedMessageIds();
		if(typeof reposition != "undefined" && reposition && typeof this.oldSelectedMessageIds == 'object') {
			// special case when we select a message while sorting the list,So we need to send the entryid of message.
			data["restriction"]["selectedmessageid"] = this.oldSelectedMessageIds[0];
			delete data["restriction"]["start"];
		}
	}

    webclient.xmlrequest.addData(this, "list", data);
    
    if (!noLoader)
        this.viewController.loadMessage();
}

/**
 * Function which saves a set of properties for a message.
 * @param object props the properties which should be saved 
 */ 
ListModule.prototype.save = function(props, send, drag)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.entryid;
	data["props"] = props;
	//if an  recurring appointment item is drag to some other position then true.
	if(drag){
		data["drag"] = true;
	}

	if (send){
		data["send"] = true;
	}

	webclient.xmlrequest.addData(this, "save", data);
}

/**
 * Function which sets the PR_MESSAGE_FLAGS (read/unread).
 * @param string messageEntryid the entryid of the message
 * @param string flag the flags comma seperated list of flags "read","receipt","noreceipt" or "unread"
 */ 
ListModule.prototype.setReadFlag = function(messageEntryid, flag)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["entryid"] = messageEntryid;
	
	if(flag) {
		data["flag"] = flag;
	}
	
	webclient.xmlrequest.addData(this, "read_flag", data);
}

ListModule.prototype.sendReadReceipt = function(entryID)
{
	var result = false;
	if (entryID){
		var message_flags = this.itemProps[entryID]["message_flags"];
		if (message_flags!=null && ((message_flags & MSGFLAG_READ) != MSGFLAG_READ) && (message_flags & MSGFLAG_RN_PENDING) == MSGFLAG_RN_PENDING){
			switch(webclient.settings.get("global/readreceipt_handling", "ask")){
				case "ask":
					result = confirm(_("The sender of this message has asked to be notified when you read this message.")+"\n"+_("Do you wish to notify the sender?"));
					break;
				case "never":
					result = false;
					break;
				case "always":
					result = true;
					break;
			}
		}
	}
	return result;
}

/**
 * Function which deletes a list of selected messages
 * @param boolean softDelete message should be soft deleted or not (for Shift + Del)
 */ 
ListModule.prototype.deleteMessages = function(messages, softDelete)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.entryid;
	var cancelInviteData = new Object();
	cancelInviteData = data;

	var folder = webclient.hierarchy.getFolder(this.entryid);
	
	if(folder && (folder.rights["deleteowned"] > 0 || folder.rights["deleteany"] > 0)){

		if (webclient.hierarchy.isSpecialFolder("wastebasket", this.entryid)){
			if (!confirm(_("Are you sure that you want to permanently delete the selected item(s)?"))){
				return;
			}
		} else if(softDelete) {
			if (!confirm(_("Are you sure that you want to permanently delete the selected item(s)?"))){
				return;
			}
			// pass softdelete variable value to the server
			data["softdelete"] = true;
		}
		
		data["entryid"] = new Array();
		if(messages.length > 0) {
			for(var i = 0; i < messages.length; i++) {

				if (messages[i] == "undefined")
					continue;

				var send = false;			
				// check row is not selected for editing...
				if (this.editMessage !== this.selectedMessages[i]){
					
					// check for appointment in list is meeting request or not, so that we could update the attendees about meeting 				
					if(typeof folder.container_class != "undefined"){
					
						var foldertype = folder.container_class.substring(folder.container_class.indexOf(".")+1, folder.container_class.length);
						if ( foldertype == "Appointment" && this.itemProps[this.entryids[messages[i]]]["meeting"] == "1") {
							
							var endtime = new Date(this.itemProps[this.entryids[messages[i]]]["duedate"] * 1000);
							var msgElement = dhtml.getElementById(messages[0]);
							/**
							 * If MR is just saved (not send) then requestsent attribute (MeetingRequestWasSent Property) is set to '0'
							 * If MR was sent to recpients then requestsent attribute (MeetingRequestWasSent Property) is set to '1'
							 * so if request is not sent to recipients then dont ask to update them while saving MR.
							 */
							if( endtime.getTime() > new Date().getTime() && msgElement.requestsent && msgElement.requestsent == "1") {
								send = confirm(_("This will delete meeting \'%s\'. \nWould you like to send an update to the attendees regarding changes to this meeting?").sprintf(this.itemProps[this.entryids[messages[i]]]["subject"]));
								if(send) {
									cancelInviteData["entryid"] = this.entryids[messages[i]];
									// This call deletes the meeting request item	
									webclient.xmlrequest.addData(this, "cancelInvitation", cancelInviteData);
								}
							}
						}
					}
					// check for all items other then meeting request
					if (!send)
						data["entryid"].push(this.entryids[messages[i]]);
				}
			}
		}

		data["start"] = this.rowstart;
		data["rowcount"] = this.rowcount;

		if(this.sort) {
			data["sort"] = new Object();
			data["sort"]["column"] = this.sort;
		}

		if(data["entryid"].length > 0) {
			// don't send request if entryid is empty
			webclient.xmlrequest.addData(this, "delete", data);
		}
	}
}

/**
 * Function which deletes one message.
 * @param string messageEntryid entryid of the message 
 */ 
ListModule.prototype.deleteMessage = function(messageEntryid)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.entryid;
	data["entryid"] = messageEntryid;

	if(typeof parentWebclient != "undefined") {
		var folder = parentWebclient.hierarchy.getFolder(this.entryid);
	} else {
		var folder = webclient.hierarchy.getFolder(this.entryid);
	}

	// FIXME: better checking for rights needed
	if(folder && (folder.rights["deleteany"] > 0 || folder.rights["deleteowned"] > 0)){
		webclient.xmlrequest.addData(this, "delete", data);
	}
}

/**
 * Function which copies or moves messages.
 * @param string destinationFolder entryid of destination folder
 * @param string type "copy" or "move"
 * @param boolean dragdrop true - action is after drag drop event (optional)
 * @param string messageEntryid the drag/dropped message entryid
 * @param string destStore the drag/dropped message entryid
 */ 
ListModule.prototype.copyMessages = function(destStore, destinationFolder, type, dragdrop, messageEntryid)
{
	/**
	 * If parentfolder of the message and destination folder are same
	 * and action is 'move' then no need to move messages.
	 * and if action is 'copy' then create identical copy in folder.
	 */
	if(destinationFolder && this.entryid && compareEntryIds(destinationFolder, this.entryid) && type == 'move')
		return;

	messageEntryids = new Array();
	if(typeof messageEntryid != "object"){
		messageEntryids[0] = messageEntryid;
	}else{
		messageEntryids = messageEntryid;
	}

	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.entryid;
	data["destinationfolder"] = destinationFolder;
	data["destinationstore"] = destStore;
	if(!destStore){
		data["destinationstore"] = this.storeid;
	}

	data["entryid"] = new Array();

	if(type == "move") {
		data["movemessages"] = "1";	
	} 
	
	if(typeof messageEntryid == "object"){
		data["entryid"] = messageEntryid;
		webclient.xmlrequest.addData(this, "copy", data);
	}else{
		for(var j=0;j<messageEntryids.length;j++){
			messageEntryid = messageEntryids[j];

			var messages = this.getSelectedMessages(messageEntryid);

			if(!dragdrop) {
				for(var i in messages) {
					data["entryid"].push(this.entryids[messages[i]]);
				}
			} else {
				if(typeof messageEntryid != "undefined" && messageEntryid) {
					data["entryid"].push(messageEntryid);
				}
			}
			
			if(data["entryid"].length > 0) {
				// don't send request if entryid is empty
				webclient.xmlrequest.addData(this, "copy", data);
			}
		}
	}

	if(type == "copy" && this.entryid == destinationFolder) {
		this.list(true);
	}
}

ListModule.prototype.showCopyMessagesDialog = function()
{
	if(typeof this.selectedMessages != "undefined" && this.selectedMessages.length > 0) {
		// added parent_entryid in URL so that we can set inbox as default folder while opening the Dialog
		webclient.openModalDialog(this, "copymessages", DIALOG_URL+"task=copymessages_modal&storeid="+this.storeid+"&parent_entryid="+webclient.hierarchy.defaultstore.defaultfolders.inbox, 300, 300, null, null, {parentModule: this});
	} else {
		alert(_("Please select a message to copy/move."));
	}
}

/**
 * Function which set the FLAG.
 * @param string messageEntryid the entryid of the message
 * @parm integer flagStatus the flag status
 * @param integer flagIcon the icon of the flag   
 */ 
ListModule.prototype.flagStatus = function(messageEntryid, flagStatus, flagIcon)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.entryid;
	data["props"] = new Object();
	data["props"]["entryid"] = messageEntryid;
	data["props"]["flag_status"] = flagStatus;
	data["props"]["flag_icon"] = flagIcon;
	data["props"]["flag_request"] = _("Red");
	data["props"]["reply_requested"] = true;
	data["props"]["response_requested"] = true;
	data["props"]["flag_complete_time"] = "";

	if (flagStatus == 1){ // completed
		data["props"]["reminder_set"] = "false";
		data["props"]["reply_requested"] = "false";
		data["props"]["response_requested"] = "false";
		data["props"]["flag_complete_time"] = parseInt((new Date()).getTime()/1000);
	}
	
	if (flagStatus == 0 && flagIcon == 0){
		data["props"]["reminder_set"] = "false";
		data["props"]["reply_requested"] = "false";
		data["props"]["response_requested"] = "false";
		data["props"]["flag_request"] = "";
	}

	webclient.xmlrequest.addData(this, "save", data);
}

/**
 * Function which is used for setting a message complete (task)
 * @param string messageEntryid entryid of the message
 * @param boolean complete true - set message complete  
 */ 
ListModule.prototype.completeStatus = function(messageEntryid, complete)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.entryid;
	data["props"] = new Object();
	data["props"]["entryid"] = messageEntryid;
	data["props"]["complete"] = (complete?"1":"-1");
	// if task is completed then remove the reminder for it.
	if(complete) {
		data["props"]["reminder"] = "0";
		data["props"]["datecompleted"] = parseInt((new Date()).getTime()/1000);
	} else {
		data["props"]["reminder"] = "1";
		data["props"]["datecompleted"] = "";
	}
		
	data["props"]["status"] = (complete?2:0);
	data["props"]["percent_complete"] = (complete?1:0);
	
	webclient.xmlrequest.addData(this, "save", data);
}

/**
 * Function which set the categories on a message
 * @param string messageEntryid entryid of the message
 * @param string categories list of categories to be set, divided by ; (semi-colon)  
 */ 
ListModule.prototype.setCategories = function(messageEntryid, categories)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.entryid;
	data["props"] = new Object();
	data["props"]["entryid"] = messageEntryid;
	data["props"]["categories"] = categories;

	webclient.xmlrequest.addData(this, "save", data);
}

/**
 * Destructor
 */ 
ListModule.prototype.destructor = function(moduleObject)
{
	dhtml.removeEvent(document.body, "mouseup", eventListCheckSelectedContextMessage);

	if(moduleObject){
		moduleObject.element.innerHTML = "";
		moduleObject.viewController.destructor();
	}

	webclient.inputmanager.removeObject(this);
}

/**
* Function to change the current page (when more then one pages exists)
*
*@param int page The page number to switch to.
*/
ListModule.prototype.changePage = function(page)
{
	this.rowstart = page * this.rowcount;
	this.list();
}

/**
* Function to open a print dialog
*
*@param string entryid The entryid for the item
*/
ListModule.prototype.printItem = function(entryid)
{
	// please note that this url is also printed, so make it more "interesting" by first set the entryid
	webclient.openModalDialog(this, "printing", DIALOG_URL+"entryid="+entryid+"&storeid="+this.storeid+"&task=printitem_modal", 600, 600);
}

ListModule.prototype.saveFieldSettings = function(data)
{
	var path = "folders/entryid_"+this.entryid+"/fields";
	var sendData = Object();
	for(var i=0;i<data.length;i++){
		var item = new Object();
		if(data[i]["order"]){
			item["order"] = data[i]["order"];
		}
		item["action"] = data[i]["action"];
		item["id"] = data[i]["id"];
		sendData[data[i]["id"]] = item;
	}
	webclient.settings.deleteSetting(path);
	webclient.settings.setArray(path,sendData);
}

ListModule.prototype.loadFieldSettings = function()
{
	var path = "folders/entryid_"+this.entryid+"/fields";
	data = webclient.settings.get(path);
	var result = new Array();
	for(var i in data){
		result.push(data[i]);
	}
	return result;
}

ListModule.prototype.saveSortSettings = function (data)
{
	var path = "folders/entryid_"+this.entryid+"/sort";
	webclient.settings.deleteSetting(path);
	webclient.settings.setArray(path,data);
}

ListModule.prototype.loadSortSettings = function ()
{
	var path = "folders/entryid_"+this.entryid+"/sort";
	
	data = webclient.settings.get(path);

	// Save the rows which are to be sorted in an array.
	var sortColumns = new Array();
	for(var i in data){
		if(i != "prototype"){//workarround		
			var column = new Object();
			column["attributes"] = new Object();
			column["attributes"]["direction"] = data[i];
			column["_content"] = i;
			// push column in a sortColumns array
			sortColumns.push(column);
		}
	}

	// If sortColumns is not available then return false otherwise return sortColumns.
	if(sortColumns.length <= 0){
		var result = false;
	}else{
		var result = sortColumns;
	}
	return result;
}
/**
 * This method is called when a dragdrop event has been started on an element 
 * that has this moduleObject associated with it.
 */ 
ListModule.prototype.dragEventDown = function(){
	this.dragSelectedMessages = this.getSelectedMessages();
}
/**
 * This method is called when a dragdrop event has ended on an element that has 
 * this moduleObject associated with it.
 */ 
ListModule.prototype.dragEventUp = function(){
	this.dragSelectedMessages = false;
}

ListModule.prototype.isMessageSelected = function(id){
	for(var i=0;i<this.selectedMessages.length;i++){
		if(this.selectedMessages[i] == id){
			return true;
		}
	}
	return false;
}

/**
 * Returns the row numbers of all selected messages.
 *
 * If 'selectedid' is specified, then the function returns the rows that
 * should be acted upon during a context menu operation. The selectedid must
 * be specified because when the user right-clicks a previously selected
 * message, the operation should be done on all selected messages, but if
 * the right-click was done on a non-selected message, then THAT message
 * should be acted upon.
 */ 
ListModule.prototype.getSelectedMessages = function(selectedid)
{
	var result = new Array;
	
	if (typeof this.selectedContextMessage != "undefined" && !selectedid){
		selectedid = this.selectedContextMessage;
	}
	if (this.selectedMessages.length == 0){
		if (selectedid)
			result[0] = selectedid;
	} else {
		
		if (selectedid){
			// check if selectedid is within selectedMessages, then we will return the selectedMessages
			var selectedItemFound = false;

			for(var i=0; i<this.selectedMessages.length;i++){
				if(this.selectedMessages[i] == selectedid){
					selectedItemFound = true;
				}
			}

			if(!selectedItemFound){
				// return selectedContextMessage
				result[0] = selectedid;
			} else {
				// return selectedMessages
				result = this.selectedMessages;
			}
		} else {
			// return selectedMessages
			result = this.selectedMessages;
		}
	}
	return result;
}

/**
 * Returns the row number of the topmost selected item
 */
ListModule.prototype.getSelectedRowNumber = function() {
	var selected = this.getSelectedMessages();	
	var top = selected[0];

	return this.viewController.getRowNumber(top);
}

/**
 * Returns the rowid of the given row number
 */
ListModule.prototype.getMessageByRowNumber = function(rownum)
{
	return this.viewController.getElemIdByRowNumber(rownum);
}

/**
 * Returns the number of rows in the table
 */
ListModule.prototype.getRowCount = function()
{
	return this.viewController.getRowCount();
}

/**
 * Returns a list of unique IDs of all the selected rows
 */
ListModule.prototype.getSelectedIDs = function()
{
	var result = new Array;
	for(var i=0; i<this.selectedMessages.length; i++) {
		result.push(this.entryids[this.selectedMessages[i]]);
	}
	
	return result;
}

/**
* Standard event handler for opening items
*
* message_type is the type of message "appointment", "task", "contact" etc (usally a part of the message_class)
*/
ListModule.prototype.onOpenItem = function(entryid, message_type)
{
	this.setReadFlag(entryid, "read,"+(this.sendReadReceipt(entryid)?"receipt":"noreceipt"));
	var uri = DIALOG_URL+"task=" + message_type + "_standard&storeid=" + this.storeid + "&parententryid=" + this.entryid + "&entryid=" + entryid;
	webclient.openWindow(this, message_type, uri);
}

ListModule.prototype.updateItemProps = function(item)
{
	var entryid = dhtml.getXMLValue(item, this.uniqueid, null)
	if (entryid){
		this.itemProps[entryid] = new Object();
		for(var j=0;j<item.childNodes.length;j++){
			if (item.childNodes[j].nodeType == 1){
				var prop_name = item.childNodes[j].tagName;
				var prop_val = dom2array(item.childNodes[j]);
				if (prop_val!==null){
					this.itemProps[entryid][prop_name] = prop_val;
				}
			}
		}
	}
}

ListModule.prototype.deleteItemProps = function(entryid)
{
	delete this.itemProps[entryid];
}

/********* SEARCH FUNCTIONS ***********/

/**
 * Function will show search indicator when search is in progress
 */
ListModule.prototype.enableSearchIndicator = function()
{
	var searchButton = dhtml.getElementById("searchfilterbutton");
	searchButton.className = "searchindicator";
}

/**
 * Function will remove search indicator when search is completed/stopped
 */
ListModule.prototype.disableSearchIndicator = function()
{
	var searchButton = dhtml.getElementById("searchfilterbutton");
	searchButton.className = "searchfilterbutton";
}

/**
 * Function will create restriction array based on search data given
 * @param object data restriction array
 * @return object data search restriction array
 */
ListModule.prototype.getSearchRestrictionData = function(data)
{
	if(typeof data == "undefined") {
		var data = new Object();
	}

	if(this.filterRestriction) {
		data["search"] = new Array();
		var filterTargets = this.filterRestrictionTarget.split(" ");
		for(var i=0; i < filterTargets.length; i++) {
			var target = new Object();
			target["property"] = filterTargets[i];
			target["value"] = this.filterRestriction;
			data["search"].push(target);
		}
	}

	return data;
}

/**
 * Function will send a request to the server, with the action "search"
 * to start search process on server
 */
ListModule.prototype.search = function()
{
	if(this.storeid && this.entryid) {
		var data = new Object();
		data["store"] = this.storeid;
		data["entryid"] = this.entryid;

		this.sort = this.loadSortSettings();
		if(this.sort) {
			data["sort"] = new Object();
			data["sort"]["column"] = this.sort;
		}

		this.columns = this.loadFieldSettings();
		if(this.columns) {
			var tablecolumns = new Array();
			for(var i = 0; i < this.columns.length; i++) {
				var column = new Object();
				column["attributes"] = new Object();
				column["attributes"]["action"] = this.columns[i]["action"];

				if(this.columns[i]["order"]) {
					column["attributes"]["order"] = this.columns[i]["order"];
				}

				column["_content"] = this.columns[i]["id"];
				tablecolumns.push(column);	
			}

			data["columns"] = new Object();
			data["columns"]["column"] = tablecolumns;

			this.columns = new Array();
		}

		data["restriction"] = new Object();
		if(this.getRestrictionData) {
			data["restriction"] = this.getRestrictionData();
			if(data["restriction"] == false) {
				return; // Abort list if module requires restriction but has none
			}
		}

		data["restriction"]["start"] = this.rowstart;

		data["subfolders"] = this.searchSubfolders;

		// search folder should be used or not
		data["use_searchfolder"] = this.useSearchFolder;

		// Retrieve data in extended (address cards view) way 
		// or the normal (table) way.
		data["data_retrieval"] = (this.viewController.view == "table" ? "normal" : "extended");

		webclient.xmlrequest.addData(this, "search", data);
		/** 
		 *	no need to call webclient.xmlrequest.sendRequest() here because
		 *	this function is called from event function and handleEvent() of dhtml.js file
		 *	will automatically call sendRequest() function.
		 */

		// show loading message
		this.viewController.loadMessage();
	}
}

/**
 * Function is used to check status of the search and send request
 * for further data if search is running
 * @param object action the action tag 
 */
ListModule.prototype.updateSearch = function(action)
{
	var searchFolderEntryID = dhtml.getXMLValue(action, "searchfolderentryid", false);
	this.searchFolderEntryID = searchFolderEntryID;		// add reference to DOM
	var searchState = dhtml.getXMLValue(action, "searchstate", 0);

	if(searchState == 0) {
		// search is not running and we are using search folder which 
		// has already finished the searching
		this.searchInProgress = false;
	}

	if(this.searchInProgress || this.finalSearchRequest) {
		this.paging(action); // update paging
		this.finalSearchRequest = false;
	}

	if(this.searchInProgress) {
		/**
		 * search state values
		 * search is completed ==> [SEARCH_RUNNING : 1]
		 * search is running ==> [SEARCH_RUNNING : 1] | [SEARCH_REBUILD : 2]
		 * search is aborted by user ==> [no flag will be set : 0]
		 */
		if((searchState & SEARCH_REBUILD) == SEARCH_REBUILD) {
			// add some time between update requests
			var module = this;	// Fix loss-of-scope in setTimeout function
			setTimeout(function() {
				module.getSearchUpdate();
			}, 500);
		} else {
			// request for the last time an update, because it could happen the search is stopped
			// at the same time this request was made.
			this.getSearchUpdate();
			this.searchInProgress = false;
			// This check is used to fetch the last paging result comes back from
			// the server when the this.searchInProgress is already set to false.
			this.finalSearchRequest = true;
		}
	} else {
		if(this.useSearchFolder) {
			this.disableSearchIndicator();
		
			if(this.totalrowcount == 0) {
				// search returned zero results
				this.viewController.viewObject.showEmptyView(_("No matches found for '%s'.").sprintf(this.filterRestriction));
			}
		}
	}
}

/**
 * Function will send XML data to get search status from server
 */
ListModule.prototype.getSearchUpdate = function()
{
	// only send request for update when search is actually running
	if(this.searchInProgress !== false) {
		var data = new Object();
		data["store"] = this.storeid;
		data["entryid"] = this.searchFolderEntryID;

		webclient.xmlrequest.addData(this, "updatesearch", data);
		webclient.xmlrequest.sendRequest();
	}
}

/**
 * Function will send XML data to stop search on server
 */
ListModule.prototype.stopSearch = function()
{
	/**
	 * we always need search folder entryid to stop search on server side
	 * but because of server side optimizations we didn't get it immediately
	 * so instead of it we can send folder's entryid, this will not help in
	 * stopping actual search but will reset the flag
	 */
	var data = new Object();
	data["store"] = this.storeid;
	data["entryid"] = this.searchFolderEntryID || this.entryid;

	webclient.xmlrequest.addData(this, "stopsearch", data);
	webclient.xmlrequest.sendRequest();
}

/**
 * Function will clear search bar and if search is running then it will stop it.
 */
ListModule.prototype.clearSearchBar = function()
{
	// disable search indicator
	if(this.useSearchFolder) {
		this.disableSearchIndicator();
	}

	// stop search on server
	this.stopSearch();

	var input = this.searchBarContainer.searchFilterInputBox;
	input.value = "";
	input.blur();

	this.filterRestriction = false;
	this.filterRestrictionTarget = false;
	this.rowstart = 0;
	this.searchInProgress = false;
	this.searchFolderEntryID = false;

	// disable clearsearch button
	this.searchBarContainer.searchClearButton.disabled = true;

	/**
	 * request reload of listmodule after some time as search response can come
	 * after some interval so it should not overwrite this response
	 */
	var module = this;
	setTimeout(function() {
		module.list();
	}, 500);
}

/********* END OF SEARCH FUNCTIONS ***********/

// Function which creates input fields for editing tasks.
ListModule.prototype.CreateEditFields = function (moduleObject, element, event)
{
	var value = '';	
	var rowValues = moduleObject.itemProps[moduleObject.entryids[element.id]];
	
	//set flag for removing css class 'complete' from each column
	var check = moduleObject.itemProps[moduleObject.entryids[element.id]]["complete"];
	
	//insertcolumn properties
	var inputprops = moduleObject.inputproperties;		

	setTimeout( function() { 
		if (moduleObject.dblclick == true) {
			moduleObject.dblclick = false;
			return;
		}
	
	
		//create input type fields for each column properties... only if module has quick-add functionality
		if (element.getElementsByTagName("input").length <= 1 && inputprops.length > 0) {
			
			//remove drag event on item row...
			dhtml.removeEvent(element, "mousedown", eventDragDropMouseDownDraggable);
			
			//remove click event on status checkbox
			if (dhtml.getElementById("property_module"+ moduleObject.id +"_"+ element.id +"_complete")) {
				dhtml.removeEvent(dhtml.getElementById("property_module"+ moduleObject.id +"_"+ element.id +"_complete"), "click", eventListChangeCompleteStatus);
			}
			
			//get tableview object...
			tableview = new TableView();
			
			for (var i=0; i<inputprops.length; i++) {
				value = '';
				
				//get column element for editing task...
				colelement = dhtml.getElementById("property_module"+ moduleObject.id +"_"+ element.id +"_"+ inputprops[i].name);			
				moduleObject.previousvalue[colelement.id] = colelement.innerHTML;
				
				if (rowValues[inputprops[i].name]) {
					if (inputprops[i].name == "percent_complete") {
						value = rowValues[inputprops[i].name] * 100 +"%";
						if (rowValues[inputprops[i].name] == 1) check = 1;
					} else {
						value = rowValues[inputprops[i].name];
					}
				}
				
				//render input type fields for column...
				tableview.renderInputColumn(true, moduleObject.id, colelement, inputprops[i].type, inputprops[i].id, value, false, inputprops[i].name, inputprops[i].readonly);
			
				//if task was marked as complete, then remove css class 'complete' from every column
				if (check == 1) {
					dhtml.removeClassName(colelement,"complete");
				}
				
				var inputElement = colelement.getElementsByTagName("input")[0];
				if (inputElement) {
					dhtml.addEvent(moduleObject.id, inputElement , "click", eventInputFieldClick);			
				}
				
				//setup datepicker for dates 
				if (inputprops[i]["type"] == "datepicker") { 				
					setcal("editprops_module"+ moduleObject.id +"_"+ inputprops[i]["id"], "cal_editprops_module"+ moduleObject.id +"_"+ inputprops[i]["id"]);
				}
			}
			
			//A message is put into editable form, so set 'editMessage' variable...
			moduleObject.editMessage = element.id;
		}
	}, 500);
}

/**
 * getDragDropTargetCallback
 * 
 * This function returns the correct event function to handle the dragdrop event. 
 * 
 * @return function dragdrop even function
 */
ListModule.prototype.getDragDropTargetCallback = function(){
	if(eventListDragDropTarget){
		return eventListDragDropTarget;
	}else{
		return false;
	}
}

/**
 * Function which selects messages that are passed as agrument
 *
 * @param array elements list of all row elements each containing an item.
 * @param string classname classname when item is selected.
 */
ListModule.prototype.selectMessages = function (elements, className)
{
	if (elements && elements.length > 0){
		// Set to empty array if elements found, so that array doesn't have duplicate values.
		this.selectedMessages = new Array();
	
		// Select items
		for (var i = 0; i < elements.length; i++){
			this.selectedMessages.push(elements[i].id);
			dhtml.selectElement(elements[i].id, className);
		}
	}
}

ListModule.prototype.refresh = function()
{
	this.list();
}
/**
 * Receives callback from the categories window
 */
 
function categoriesCallBack(categories, userdata)
{
	var entryid = userdata.entryid;
	var module = userdata.module;
	
	module.setCategories(entryid, categories);		
}

/**
 * All the events for this module are defined below. In the "init" function of this 
 * module are most of these events set. Every event receives three parameters.
 * @param obejct moduleObject the module object
 * @param object element the element on which the event fired
 * @param object event the event object 
 */ 
 
function eventListMouseDownMessage(moduleObject, element, event)
{
	if(!event){
		event = new Object();
	}

	// Check if contextmenu is opened. If so, remove the class on the element.
	// This check is build in, because if the same element is clicked as the contextmenu 
	// element, the style brakes. With this check the style is correct and this message
	// is selected.
	eventListCheckSelectedContextMessage(moduleObject);

	if (event.button == 0 || event.button == 1 || !event.button) {
		/**
		 * This lines seems to get the first classname and append it with "selected". So for normal 
		 * table with rows it appends "row" to "rowselected". For notes it appends "large_view" to 
		 * "large_viewselected". For contact card view it appends "contact" to "contactselected".
		 */
		var className = (element.className.indexOf(" ") > 0?element.className.substring(0, element.className.indexOf(" ")):element.className) + "selected";

		if (event.ctrlKey) {
				// Deselect previously selected item
			if (element.className.indexOf(className) > 0) {
				// Deselect previously selected item
				dhtml.deselectElement(element.id, className);
				
				// Search for current element in list of selected items.
				var elementIndex = -1;
				for(var i = 0; i < moduleObject.selectedMessages.length; i++)
				{
					if(moduleObject.selectedMessages[i] == element.id) {
						elementIndex = i;
					}
				}
				
				// Remove focussed item from list of selected items
				if(elementIndex >= 0) {
					moduleObject.selectedMessages.splice(elementIndex, 1);
				}
			// Current element not selected
			} else {
				// Select previously unselected item
				dhtml.selectElement(element.id, className);
				moduleObject.selectedMessages.push(element.id);
				moduleObject.selectedFirstMessage = element.id;
			}
			// Set cursor position to focussed element
			moduleObject.viewController.setCursorPosition(element.id);
		} else if(event.shiftKey) {
			// Deselect all messages, set cursor position and select from first item till focussed item.
			dhtml.deselectElements(moduleObject.selectedMessages, className);
			moduleObject.viewController.setCursorPosition(element.id);
			moduleObject.selectedMessages = dhtml.selectRange(moduleObject.selectedFirstMessage, moduleObject.viewController.getCursorPosition(), className);
		// No shift or control used. Only mousedown or keyboard up/down
		} else {
			if (!moduleObject.isMessageSelected(element.id)) {
				dhtml.deselectElements(moduleObject.selectedMessages, className);
				dhtml.selectElement(element.id, className);
				//Check if it is appointmentlistmodule and getMultiDayItemInObject is exists, then add elements accodingly.
				if(typeof moduleObject.getMultiDayItemInObject != "undefined"){
					var elementArr = moduleObject.getMultiDayItemInObject(element.id);
					for(var elCnt = 0;elCnt<elementArr.length;elCnt++)
						moduleObject.selectedMessages.push(elementArr[elCnt]);
				}else{
					moduleObject.selectedMessages = new Array(element.id);
				}
			}
			moduleObject.selectedFirstMessage = element.id;
			moduleObject.viewController.setCursorPosition(element.id);
		}

		// clear the text selection on document.
		dhtml.clearBodySelection();
	}
	
	// save changes and dactivate editmode, if 'mousedown' on other item...
	if (moduleObject.editMessage !== false && moduleObject.editMessage !== element.id) {
		// here we have to pass element id of message which is in edit mode
		// not the current element
		if (moduleObject.SubmitMessage(moduleObject, dhtml.getElementById(moduleObject.editMessage), event)){
			moduleObject.removeinputfields(moduleObject, element, event);
		}
	}

	// As 'mousedown' event is also registered for divelement we don't want the event to bubble to divelement.
	// only cancel bubbling when quick edit mode is active
	if(typeof moduleObject.editMessage != "undefined" && moduleObject.editMessage !== false) {
		event.stopPropagation();
	}
}


function eventListMouseUpMessage(moduleObject, element, event)
{
	if(!event) {
		event = new Object();
	}
	if (event.button == 0 || event.button == 1 || !event.button) {
		var className = (element.className.indexOf(" ") > 0?element.className.substring(0, element.className.indexOf(" ")):element.className) + "selected";

		if ((typeof(dragdrop) == "undefined" || !dragdrop.targetover) && !event.ctrlKey && !event.shiftKey) {

			moduleObject.sendEvent("selectitem", moduleObject.entryids[element.id]);
				
			if (moduleObject.isMessageSelected(element.id) && moduleObject.selectedMessages.length != 1){
				dhtml.deselectElements(moduleObject.selectedMessages, className);
				dhtml.selectElement(element.id, className);

				moduleObject.selectedMessages = new Array(element.id);
			}
		}
		
		/**
		 * Dont put item into editmode,
		 * when special keyes are pressed.
		 * e.g PgDn, PgUp etc.
		 */
		if (event.type != "keydown") {
			if (moduleObject.selectedMessages.length == 1 && !moduleObject.editMessage) {
				/**
				 * Check to see if the the message we have clicked on has been selected
				 * by the previous click and that we are not dragging anything.
				 */
				if (moduleObject.selectedMessages[0] === moduleObject.prevselectedMessage && !(typeof dragdrop != "undefined" && dragdrop.attached === true)) {
					moduleObject.CreateEditFields(moduleObject, element, event);
				}
			}
		}
		
		//assign newly selected item id to 'preveselectedMessage' variable... 
		moduleObject.prevselectedMessage = element.id;
	}
}

function eventListDblClickMessage(moduleObject, element, event)
{	

	moduleObject.dblclick = true;
	
	if (moduleObject.editMessage && event.keyCode == 13) {
		//set drag and drop event for tasks.
		dhtml.addEvent(moduleObject.id, element, "mousedown", eventDragDropMouseDownDraggable);

		//set click event so that task is saved as complete when user selects the complete flag...
		if (dhtml.getElementById("property_module"+ moduleObject.id +"_"+ element.id +"_complete")) {
			dhtml.addEvent(moduleObject.id, dhtml.getElementById("property_module"+moduleObject.id +"_"+ element.id +"_complete"), "click", eventListChangeCompleteStatus);
		}
		
		if (moduleObject.SubmitMessage(moduleObject, element, event)){
			moduleObject.dblclick = false;
			moduleObject.editMessage = false;
		}
	} else if(!moduleObject.editMessage) {
		var messageClass = false;
		var classNames = element.className.split(" ");
	
		for(var index in classNames)
		{
			if(classNames[index].indexOf("ipm_") >= 0 && messageClass==false) {
				messageClass = classNames[index].substring(classNames[index].indexOf("_") + 1);
			}
		}
		moduleObject.sendEvent("openitem", moduleObject.entryids[element.id], messageClass);
	}
}

function eventListContextMenuMessage(moduleObject, element, event)
{
	event.stopPropagation();
	// Stop opening default context menu in IE.
	event.preventDefault();
	var className = (element.className.indexOf(" ") > 0?element.className.substring(0, element.className.indexOf(" ")):element.className);
	var classNameContextMenu = className + "contextmenu";

	if(moduleObject.selectedContextMessage) {
		var message = dhtml.getElementById(moduleObject.selectedContextMessage);
		
		if(message) {
			if(message.className.indexOf(classNameContextMenu) > 0) {
				dhtml.removeClassName(message, classNameContextMenu);
			}
		}
	}
	
	if(element.className.indexOf(className + "selected") < 0) {
		element.className += " " + classNameContextMenu;
	}

	moduleObject.selectedContextMessage = element.id;
	
	var items = new Array();
	var removeMenuItems = new Array();

	if (moduleObject.itemProps[moduleObject.entryids[moduleObject.selectedContextMessage]]){
		var props = moduleObject.itemProps[moduleObject.entryids[moduleObject.selectedContextMessage]];
		if(props.message_class){
			if(props.message_class.indexOf('IPM.Note') == -1 && props.message_class.indexOf('IPM.Schedule.Meeting') == -1 ) {
				removeMenuItems.push("save_email");
			}
			if (props.message_class && props.message_class == "IPM.DistList"){
				removeMenuItems.push("createmail");
			}
		}
	}

	var selection = moduleObject.getSelectedMessages();
	var selectionHasUnread = false;
	var selectionHasRead = false;
	for(i in selection){
		var el = dhtml.getElementById(selection[i]);
		if(el == null)
			continue;
		// Check for read/unread (only for mail and tasks)
		if(el.className.indexOf("mail") > 0 || el.className.indexOf("task") > 0 ) {
			if(el.className.indexOf("message_unread") > 0) {
				selectionHasUnread = true;
			} else {
				selectionHasRead = true;
			}
		}
		if (selectionHasRead && selectionHasUnread)
			break; // no need to check the other messages
	}
	// show forwarditems option only when more than one item is seleceted from list
	if(selection && selection.length < 2) {
		removeMenuItems.push("forward_items");
	}
	// do not display forward, reply, replyall options in context menu when two or more items are seleceted
	if(selection && selection.length > 1) {
		removeMenuItems.push("forward");
		removeMenuItems.push("reply");
		removeMenuItems.push("replyall");
	}

	if (selectionHasRead && !selectionHasUnread) {
		removeMenuItems.push("markread");
	}

	if (selectionHasUnread && !selectionHasRead) {
		removeMenuItems.push("markunread");
	}

	if (webclient.hierarchy && webclient.hierarchy.isSpecialFolder("junk", moduleObject.entryid)){
		removeMenuItems.push("junk");
	}

	if (webclient.hierarchy && !webclient.hierarchy.isSpecialFolder("sent", moduleObject.entryid)){
		removeMenuItems.push("edit");
	}

	for(var i in moduleObject.contextmenu) {
		var item = moduleObject.contextmenu[i];
		for(var j=0; j<removeMenuItems.length; j++){
			if(item["id"] == removeMenuItems[j]){
				item = false;
				break;
			}
		}
		if (item){
			items.push(item);
		}
	}

	webclient.menu.buildContextMenu(moduleObject.id, element.id, items, event.clientX, event.clientY);
}

function eventListCheckSelectedContextMessage(moduleObject, element, event)
{
	// only react on click events and no events, this is a workaround, we get events here from body (mouseup)
	if((!event || event.type == "click") && moduleObject.selectedContextMessage) {
		var message = dhtml.getElementById(moduleObject.selectedContextMessage);
		
		if(message) {
			var className = (message.className.indexOf(" ") > 0?message.className.substring(0, message.className.indexOf(" ")):message.className) + "contextmenu";
			dhtml.removeClassName(message, className);
		}

		/** 
		 * selectedContextMessage contains the row id of selected row(s), on which the action 
		 * has been performed and item function has been called for an update of that item(s).
		 * so now we need to clear the selectedContextMessage variable, else it will keep the 
		 * reference of the row(s) and action can be performed on that, which should not be 
		 * done now.
		 */
		moduleObject.selectedContextMessage = false;
	}
}

function eventListNewMessage(moduleObject, element, event)
{
	if(element.parentNode.id == "defaultmenu") {
		element.parentNode.style.display = "none";
	}
	
	var messageClass = false;
	var classNames = element.className.split(" ");
	
	for(var index in classNames)
	{
		if(classNames[index].indexOf("icon_") >= 0) {
			messageClass = classNames[index].substring(classNames[index].indexOf("_") + 1);
		}
	}

	var parententryID = moduleObject.entryid;
	var storeID = moduleObject.storeid;

	if(messageClass) {
		var extraParams = "";
		if (messageClass == "appointment"){
			// Check implemented mainly for the multiusercalendar
			if(!parententryID){
				parententryID = webclient.hierarchy.defaultstore.defaultfolders.calendar;
			}

			// if any slot is selected in calendar then use its time
			if(typeof moduleObject.appointmentmodule != 'undefined') {
				var viewObj = moduleObject.appointmentmodule.viewController.viewObject;
				var getStartEndTimeOfSelection = viewObj.getStartEndTimeOfSelection;
				if(typeof getStartEndTimeOfSelection != 'undefined') {
					var startEndTimeFromSelection = getStartEndTimeOfSelection.call(viewObj, false);
					if(startEndTimeFromSelection) {
						extraParams = "&startdate=" + startEndTimeFromSelection[0] + "&enddate=" + (startEndTimeFromSelection[1]);
					}
				}
			}

			// use date from datepicker
			if(extraParams.trim().length == 0) {
				var dtmodule = webclient.getModulesByName("datepickerlistmodule");
				if (dtmodule[0] && dtmodule[0].selectedDate){
					if (!new Date(dtmodule[0].selectedDate).isSameDay(new Date())){
						var hourCount = webclient.settings.get("calendar/workdaystart",9 * 60) / 60;
						var newAppDateTimeStamp = addHoursToUnixTimeStamp(dtmodule[0].selectedDate / 1000, hourCount);
						// set extraParams to startdate and enddate value so appointment dailog opens with the seleceted date
						// a default 30 minutes(1800 sec) is added to endtime.
						extraParams = "&startdate=" + newAppDateTimeStamp + "&enddate=" + (newAppDateTimeStamp + 1800);
					}
				}
			}
		} else if (messageClass == "createmail"){
			// Select default store when creating new mail while other's Inbox is opened
			storeID = webclient.hierarchy.defaultstore.id;
			parententryID = webclient.hierarchy.defaultstore.defaultfolders.outbox;
		}

		webclient.openWindow(moduleObject, messageClass, DIALOG_URL+"task=" + messageClass + "_standard&storeid=" + storeID + "&parententryid=" + parententryID + extraParams);
	}
}

function eventListDeleteMessages(moduleObject, element, event)
{
	var selectedMessages = moduleObject.getSelectedMessages();
	if(selectedMessages.length > 0) {
		moduleObject.deleteMessages(selectedMessages);
	} else {
		alert(_("Please select a message to delete."));
	}
}

/**
 * Event called when multiple items are seleceted from maillistmodule
 * This will open the createmail dialog with selected items added as attachment.
 */ 
function eventListContextMenuForwardMultipleItems(moduleObject, element, event) 
{
	element.parentNode.style.display = "none";
	var forwardItems = new Array();
	for(var i=0; i < moduleObject.selectedMessages.length; i++){
		forwardItems[i] = moduleObject.entryids[moduleObject.selectedMessages[i]];
	}
	//here selected messages are added through the windowdata as attachments
	webclient.openWindow(moduleObject, "createmail", DIALOG_URL+"task=createmail_standard&message_action=forwardasattachment" + "&storeid=" + moduleObject.storeid + "&parententryid=" + moduleObject.entryid ,720, 580, true,null,null, forwardItems);
}

function eventListCopyMessages(moduleObject, element, event)
{
	moduleObject.showCopyMessagesDialog();
}

function eventListPrintMessage(moduleObject, element, event)
{
	var printSuccess = false;
	/**
	 * in calendar list view, moduleObject is of datepickerlistmodule so can't use datepickerlistmodule.selectedMessages
	 * for that we have to use appointmentlistmodule
	 */
	// for calendar
	if(typeof this.appointmentmodule != "undefined" && this.appointmentmodule) {
		if(this.appointmentmodule.selectedview == "table") {
			// print selected item
			if(this.appointmentmodule.entryids && this.appointmentmodule.selectedMessages[0]) {
				var item = this.appointmentmodule.entryids[this.appointmentmodule.selectedMessages[0]];
				moduleObject.printItem(item);
				printSuccess = true;
			}
		} else {
			// print whole view
			moduleObject.printList(moduleObject.entryid);
			printSuccess = true;
		}
	} else if(moduleObject.entryids && moduleObject.selectedMessages[0]) {
		// for all other folders, print selected item
		var item = moduleObject.entryids[moduleObject.selectedMessages[0]];
		moduleObject.printItem(item);
		printSuccess = true;
	}

	if(!printSuccess) {
		alert(_("Please select a message to print") + ".");
	}
}

function eventListShowAddressbook(moduleObject, element, event)
{
	// Although this window is not modal, we're using the modal addressbook dialog anyway
	webclient.openWindow(moduleObject, 'addressbook', DIALOG_URL+'task=addressbook_modal&showsendmail=true&detailsonopen=true&storeid=' + moduleObject.storeid, 800, 500, true);
}

function eventListMouseOverFlag(moduleObject, element, event)
{
	element.className += "_over";
}

function eventListMouseOutFlag(moduleObject, element, event)
{
	if(element.className.indexOf("_over") > 0) {
		element.className = element.className.substring(0, element.className.indexOf("_over"));
	}
}

function eventListChangeFlagStatus(moduleObject, element, event)
{
	var flagStatus = olFlagComplete;
	var flagIcon = olNoFlagIcon;

	if(element.className.indexOf("complete") > 0 || element.className.indexOf("flag_over") > 0) {
		flagStatus = olFlagMarked;
		flagIcon = olRedFlagIcon;
	}

	moduleObject.flagStatus(moduleObject.entryids[element.parentNode.id], flagStatus, flagIcon);
}

function eventListChangeReadFlag(moduleObject, element, event)
{
	var tableRow = element.parentNode;
	var entryid = moduleObject.entryids[tableRow.id];

	if(dhtml.hasClassName(tableRow, "read_unread")) {
		var flag = "unread";
		if(dhtml.hasClassName(tableRow, "message_unread")) {
			flag = "read," + (moduleObject.sendReadReceipt(entryid) ? "receipt" : "noreceipt");
		}

		moduleObject.setReadFlag(entryid, flag);
	}
}

function eventListChangeCompleteStatus(moduleObject, element, event)
{
	var checkbox = element.getElementsByTagName("input")[0];

	if(checkbox) {
		moduleObject.completeStatus(moduleObject.entryids[element.parentNode.id], checkbox.checked);
	}
}

function eventListChangePage(moduleObject, element, event)
{
	if (element.className.indexOf("first") > 0) {
		moduleObject.changePage(0);
	} else if (element.className.indexOf("prev") > 0) {
		var page = Math.floor(moduleObject.rowstart / moduleObject.rowcount) - 1;
		if (page>=0) {
			moduleObject.changePage(page);
		}
	} else if (element.className.indexOf("next") > 0) {
		var page = Math.floor(moduleObject.rowstart / moduleObject.rowcount) + 1;
		if (page<=Math.floor(moduleObject.totalrowcount/moduleObject.rowcount)) {
			moduleObject.changePage(page);
		}
	} else if (element.className.indexOf("last") > 0) {
		var page = Math.floor(moduleObject.totalrowcount / moduleObject.rowcount);

		if((moduleObject.totalrowcount % moduleObject.rowcount) == 0)
			page -= 1;

		moduleObject.changePage(page);
	} else if (element.tagName.toLowerCase() == 'input') {
		moduleObject = webclient.getModule(moduleObject);
		if (moduleObject) moduleObject.changePage(parseInt(element.value, 10) - 1);
	}
}

function eventListChangeView(moduleObject, element, event)
{
	if(element.className.indexOf("value_") > 0) {
		var classNames = element.className.split(" ");

		var value = moduleObject.defaultview;
		for(var i in classNames)
		{
			if(classNames[i].indexOf("value_") >= 0) {
				value = classNames[i].substring(classNames[i].indexOf("_") + 1);
			}
		}

		moduleObject.destructor();
		moduleObject.initializeView(value);
		moduleObject.list();
		moduleObject.resize();
	}
}

function eventListColumnSort(moduleObject, element, event)
{
	var old_sortColumn = moduleObject.viewController.viewObject.sortColumn;
	var old_sortDirection = moduleObject.viewController.viewObject.sortDirection;

	var sortColumn = element.id.substring(element.id.indexOf("_") + 1);
	if(element.id.indexOf("sort") > 0) {
		sortColumn = element.id.substring(element.id.indexOf("_") + 1, element.id.indexOf("sort") - 1);
	}
	
	var sortDirection = "";
	// change direction only through menu or when you click the already sorted column
	if (dhtml.hasClassName(event.target,"menuitem") || sortColumn==old_sortColumn){
		if(element.id.indexOf("sort") > 0) {
			if(element.id.indexOf("sort_asc") > 0) {
				sortDirection = "desc";
			}
		}
	}else{
		sortDirection = old_sortDirection;
	}
	
	if(!sortDirection)
		sortDirection = "asc";
	
	var sortSaveList = Object();
	sortSaveList[sortColumn] = sortDirection
	
	/**
	 * flag status column value depends on values of flag_status and flag_icon properties,
	 * so do sorting on both properties.
	 */
	if(sortColumn == "flag_icon")
		sortSaveList["flag_status"] = sortDirection;

	moduleObject.saveSortSettings(sortSaveList);

	//Check if there is any restriction on sorting
	var name = dhtml.getElementById("name");
	if(name && name.value.trim() != "") {
		var searchData = new Object();
		searchData["searchstring"] = name.value;
		moduleObject.list(false, false, searchData);
		var character = dhtml.getElementById("character_" + this.selectedCharacter);
		if(character) {
			dhtml.removeClassName(character, "characterselect");
		}
	} else if (typeof(this.enableGABPagination) != "undefined" && this.enableGABPagination) {
		moduleObject.list(false, false, getPaginationRestriction(), true);
	} else {
		moduleObject.list(false, false, moduleObject.selectedMessages, true);
	}
}

function eventListColumnContextMenu(moduleObject, element, event)
{
	var items = new Array();
	if(moduleObject.enableSorting) {
		items.push(webclient.menu.createMenuItem("sort_ascending", _("Sort Ascending"), false, eventListColumnContextSortAsc));
		items.push(webclient.menu.createMenuItem("sort_descending", _("Sort Descending"), false, eventListColumnContextSortDesc));
	}
	if(moduleObject.enableVariableColumns) {
		items.push(webclient.menu.createMenuItem("seperator", ""));
		items.push(webclient.menu.createMenuItem("columndelete", _("Delete this column"), false, eventListColumnDelete));
		items.push(webclient.menu.createMenuItem("seperator", ""));
		items.push(webclient.menu.createMenuItem("fieldchooser", _("Field Chooser"), false, eventListFieldChooser));
	}

	if(moduleObject.enableSorting || moduleObject.enableVariableColumns)
		webclient.menu.buildContextMenu(moduleObject.id, element.id, items, event.clientX, event.clientY);
}

function eventListColumnContextSortAsc(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	
	var column = dhtml.getElementById(element.parentNode.elementid);
	if(column.id.indexOf("sort") > 0) {
		column.id = column.id.substring(0, column.id.indexOf("sort") - 1);
	}
	
	eventListColumnSort(moduleObject, column, event);
}

function eventListColumnContextSortDesc(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	
	var column = dhtml.getElementById(element.parentNode.elementid);
	if(column.id.indexOf("sort_asc") <= 0) {
		column.id = column.id + "_sort_asc";
	}

	eventListColumnSort(moduleObject, column, event);
}

function eventListColumnDelete(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	
	if(!moduleObject.columns) {
		moduleObject.columns = new Array();
	}
	
	var columnElement = dhtml.getElementById(element.parentNode.elementid);
	var sortColumn = columnElement.id.substring(columnElement.id.indexOf("_") + 1);
	if(columnElement.id.indexOf("sort") > 0) {
		sortColumn = columnElement.id.substring(columnElement.id.indexOf("_") + 1, columnElement.id.indexOf("sort") - 1);
	}
	
	var column = new Object();
	column["id"] = sortColumn;
	column["action"] = "delete";
	//get other columns
	var columnList = moduleObject.loadFieldSettings();
	columnList.push(column);
	//save columns
	moduleObject.saveFieldSettings(columnList);
	//display comlumns
	moduleObject.list();
}

function eventListFieldChooser(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	/**
	 * If the field chooser is opened by the main window, then take webclient as its parent window.
	 * If the field chooser is opened from some dialog box, then take its parentwebclient object to open.
	 */
	if (typeof parentWebclient != "undefined")
		parentWebclient.openModalDialog(moduleObject, "fieldchooser", DIALOG_URL+"task=fieldchooser_modal", 550, 470, fieldChooserCallBack, moduleObject, {parentModule: moduleObject});
	else
		webclient.openModalDialog(moduleObject, "fieldchooser", DIALOG_URL+"task=fieldchooser_modal", 550, 470, fieldChooserCallBack, moduleObject, {parentModule: moduleObject});
}

function eventListDragDropTarget(moduleObject, targetElement, element, event)
{
	var targetFolder = false;
	// Destination entryid
	var folder = targetElement.parentNode.parentNode.id;

	if (folder == "main"){
		// this is the maillist, ignore the drop
		return;
	}

	if(typeof moduleObject.dragSelectedMessages == "object" && moduleObject.dragSelectedMessages[0] != false && moduleObject.dragSelectedMessages.length > 1){
		var selectedMessages = moduleObject.dragSelectedMessages;
	}else{
		var selectedMessages = moduleObject.getSelectedMessages();
	}
	if(typeof selectedMessages != "object"){
		selectedMessagesArr = new Array();
		selectedMessagesArr[0] = selectedMessages;
		selectedMessages = selectedMessagesArr;
	}

	// Flag for checking whether target is waste basket (Deleted items folder) or not.
	var targetIsWasteBasket = false;
	// Set the root folder flag to false, as we cannot move any items to root folder.
	var targetIsRootFolder = false;

	var folderObject = webclient.hierarchy.getFolder(folder);

	// get target folder entryid
	var targetStoreID = folderObject.storeid;
	var targetEntryID = folderObject.entryid;

	// get target folder properties
	var targetFolderProperties = webclient.hierarchy.getFolder(targetEntryID);
	var targetFolderContainerClass = targetFolderProperties.container_class;
	var targetFolderContainerClassParts = targetFolderContainerClass.split('.') ;

	if(targetFolderProperties["entryid"] == webclient.hierarchy.defaultstore.root.entryid)
		targetIsRootFolder = true;

	if(targetFolderProperties["entryid"] == webclient.hierarchy.defaultstore.defaultfolders.wastebasket) {
		// allow copy/move operation for all items if destination folder is deleted items
		targetIsWasteBasket = true;
	}

	// Message entryids
	var entryid = new Array;
	for(var i=0;i<selectedMessages.length;i++){
		if (moduleObject.entryids[selectedMessages[i]] && !targetIsRootFolder) {
			if(targetIsWasteBasket) {
				// If target is waste basket (Deleted items folder) then move all items to entryid array
				entryid.push(moduleObject.entryids[selectedMessages[i]]);
			} else {
				var selectedItemMessageClassParts = moduleObject.itemProps[moduleObject.entryids[selectedMessages[i]]].message_class.split(".");
				if(targetFolderContainerClassParts[1] == selectedItemMessageClassParts[1]) {
					// If target folder is of same message class as selected item's message class then move item to entryid array
					entryid.push(moduleObject.entryids[selectedMessages[i]]);
				} else if (selectedItemMessageClassParts[1] == "Schedule" && targetFolderContainerClassParts[1] == "Note") { 
					/** 
					 * If selected item's message class is IPM.Schedule.Meeting.Request  
					 * then allow it to move in Note folder. 
					 */ 
					entryid.push(moduleObject.entryids[selectedMessages[i]]); 
				} else if (selectedItemMessageClassParts[0] == "REPORT" && targetFolderContainerClassParts[1] == "Note") { 
					/** 
					 * If selected item's message class is of report type 
					 * REPORT.IPM.Note.NDR -> undelivered mail. 
					 * REPORT.IPM.Note.IPNNRN -> unread notification mail. 
					 * REPORT.IPM.Note.IPNRN -> read notification mail. 
					 * then allow it to move in Note folder. 
					 */ 
					entryid.push(moduleObject.entryids[selectedMessages[i]]); 
				} else if (selectedItemMessageClassParts[1] == "DistList" && targetFolderContainerClassParts[1] == "Contact") { 
					/** 
					 * If selected item's message class is IPM.DistList  
					 * then allow it to move in Contact folder. 
					 */ 
					entryid.push(moduleObject.entryids[selectedMessages[i]]); 
				} else {
					targetFolder = true;
					// As message is dragged to diffrent IPF Folder we build the 
					// message object with entryid and type to send to server
					var message = new Object();
					message.id = moduleObject.entryids[selectedMessages[i]];
					message.type = selectedItemMessageClassParts[1];
					entryid.push(message);
				}
			}
		}
	}
	
	if(folder != moduleObject.entryid && folderObject && entryid.length > 0) {
		if(!targetFolder){
			/**
			 * As target folder is of same message class as selected item's 
			 * message class then we can move the message
			 */
			moduleObject.copyMessages(folderObject.storeid, folder, "move", true, entryid);
		}else{
			/**
			 * Here we open particular dialog of target folder type which contains
			 * the selected message added to the body of newly opened Dailog
			 */
			var sourceFolderContainerClass = webclient.hierarchy.getFolder(moduleObject.entryid).container_class;

			moduleObject.createNewMessageFromItems(sourceFolderContainerClass, targetFolderContainerClass, targetEntryID, true, entryid);
		}
		
	} else if(entryid.length == 0 && targetIsRootFolder) {
		/**
		 * whenever any item is moved to the root folder of user 
		 * store an alert message is displyed. 
		 */
		alert(_("Cannot move the items. The destination folder cannot contain messages/forms."));
	} else{
		/**
		 * TODO: When there multiple messages of different classes are dragged/dropped
		 * from deleted items, It just allows to transfer messages that are type of destination messages.
		 * for other it is not showing any error messages.
		 */
		alert(_("Can not move selected items to destination folder."));
	}
}

/**
 * Function which opens new dialog of target folder type for the selected items
 * @param string sourceFolderType type of source folder
 * @param string targetFolderType type of target folder
 * @param string targetEntryID entryid of target folder
 * @param boolean dragdrop true - action is after drag drop event
 * @param object messageEntryid the drag/dropped message entryid and type
 */ 
ListModule.prototype.createNewMessageFromItems = function(sourceFolderType, targetFolderType , targetEntryID, dragdrop, messageEntryids)
{
	switch(targetFolderType){
		case "IPF.Appointment":
			targetFolder = "appointment";
			break;
		case "IPF.Contact":
			targetFolder = "contact";
			break;
		case "IPF.StickyNote":
			targetFolder = "stickynote";
			break;
		case "IPF.Task":
			targetFolder = "task";
			break;
		case "IPF.Note":
			targetFolder = "createmail";
			break;
	}

	// special case when selected message[item being dragged] is from contacts folder.
	if(sourceFolderType == "IPF.Contact" && targetFolderType != "IPF.StickyNote"){
		var data = new Object();
		data["store"] = this.storeid;
		data["parententryid"] = targetEntryID;
		data["messages"] = new Object();
		data["messages"]["message"]= new Object();
		data["messages"]["message"]= messageEntryids;
		data["targetfolder"] = targetFolder;
		
		webclient.xmlrequest.addData(this, "convert_contact", data); 
		webclient.xmlrequest.sendRequest();

	}else if(sourceFolderType == "IPF.Note" && targetFolderType == "IPF.Appointment" && messageEntryids.length < 2){
		// special case when selected message[item being dragged] is from inbox folder but is a scheduled meeting request.
		var data = new Object();
		data["store"] = this.storeid;
		data["parententryid"] = targetEntryID;
		data["messages"] = new Object();
		data["messages"]["message"]= new Object();
		data["messages"]["message"]= messageEntryids;
		data["targetfolder"] = targetFolder;

		webclient.xmlrequest.addData(this, "convert_meeting", data); 
		webclient.xmlrequest.sendRequest();

	}else{
		// open the target folder's new item dialog window
		var uri = DIALOG_URL+"task=" + targetFolder + "_standard&storeid=" + this.storeid + "&parententryid=" + targetEntryID;
		webclient.openWindow(this, 
							targetFolder, 
							uri,
							720, 580, 
							true, null, null, 
							{
								"entryids" : messageEntryids, 
								"action" : "convert_item"
							});
	}
}


function eventListContextMenuOpenMessage(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var message = dhtml.getElementById(element.parentNode.elementid);

	if(message) {
		eventListDblClickMessage(moduleObject, message, event);
	}
	
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventListContextMenuPrintMessage(moduleObject, element, event)
{
	element.parentNode.style.display = "none";

	moduleObject.printItem(moduleObject.entryids[element.parentNode.elementid]);

	eventListCheckSelectedContextMessage(moduleObject);
}

function eventListContextMenuCategoriesMessage(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var callbackdata = new Object();
	callbackdata.module = moduleObject;
	callbackdata.entryid = moduleObject.entryids[element.parentNode.elementid];
	webclient.openModalDialog(moduleObject, "categories", DIALOG_URL+"task=categories_modal&storeid=" + moduleObject.storeid + "&parententryid=" + moduleObject.entryid + "&entryid=" + moduleObject.entryids[element.parentNode.elementid], 350, 370, categoriesCallBack, callbackdata);
}

function eventListContextMenuMessageOptions(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	webclient.openModalDialog(moduleObject, "options", DIALOG_URL+"task=messageoptions_modal&storeid=" + moduleObject.storeid + "&parententryid=" + moduleObject.entryid + "&entryid=" + moduleObject.entryids[element.parentNode.elementid], 460, 420);
}

function eventListContextMenuDeleteMessage(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var items = moduleObject.getSelectedMessages(element.parentNode.elementid);
	moduleObject.deleteMessages(items);
	
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventListContextMenuMoveJunkMessage(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var hierarchy =  webclient.hierarchy;
	var destinationFolder = hierarchy.getStore(moduleObject.storeid).defaultfolders["junk"];

	// change flag always to read after move to junk
	var items = moduleObject.getSelectedMessages(element.parentNode.elementid);
	for(i in items){
		moduleObject.setReadFlag(moduleObject.entryids[items[i]], "read,noreceipt");
	}

	moduleObject.copyMessages(moduleObject.storeid, destinationFolder, "move")
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventListContextMenuCopyMessage(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	webclient.openModalDialog(moduleObject, "copymessages", DIALOG_URL+"task=copymessages_modal&storeid="+moduleObject.storeid, 300, 300, null, null, {parentModule: moduleObject});
	eventListCheckSelectedContextMessage(moduleObject);
}

/**
 * Event Function which will get fired on choosing "edit as new message" option from context menu;this will open message as editable in sent folder
 * @param moduleObject object Module Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventListContextMenuEditMessage(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var message = dhtml.getElementById(element.parentNode.elementid);
	webclient.openWindow(moduleObject, "createmail" , DIALOG_URL+"task=createmail_standard&message_action=edit&storeid=" + moduleObject.storeid + "&parententryid=" + moduleObject.entryid + "&entryid=" + moduleObject.entryids[message.id]);
}

/**
 * Event Function which will be fired on "Convert mail to Task" option from context menu;this will create a task out of a mail.
 * @param moduleObject object Module Object
 * @param element object HTML element object
 * @param event object event type object
 */
function eventListContextMenuTask(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var items = moduleObject.getSelectedMessages(element.parentNode.elementid);
	for(i in items){
		var	messageEntryid = moduleObject.entryids[items[i]];

		var data = new Object();
		data["store"] = moduleObject.storeid;
		data["entryid"] = messageEntryid;
		
		webclient.xmlrequest.addData(moduleObject, "createTask", data);
	}
}

function fieldChooserCallBack(result, module)
{
	var properties = result;
	
	module.saveFieldSettings(properties);
	module.columns = properties;	
	module.list(true);
	
	//If the user has selected 'open inbox at startup',then to save column settings while login we 
	//set inbox storid and entryid in  folders settings.
	if(module.title == "Inbox"){
		webclient.settings.set("folders/entryid_inbox/entryid",module.entryid);
		webclient.settings.set("folders/entryid_inbox/storeid",module.storeid);
	}
}

function eventInputFieldClick(moduleObject, element, event)
{
	if (element.type == "checkbox") {
		percentchange(moduleObject.id, element.id.substring(0,element.id.indexOf("_")));
	}	
	
	if(element.id == "insertprops_module"+ moduleObject.id +"_subject" && element.value == _("Click here to add a new item")) {
		element.value = "";
	}

	if (!element.readOnly) {
		element.focus();
		//this will clear the owner field of quickbar in task's public folder
		if(element.id == "insertprops_module"+ moduleObject.id +"_owner"){
			element.value = "";
		}
		// also maintain focus on input field using inputmanager
		webclient.inputmanager.handleFocus({"type":"blur"});
		webclient.inputmanager.handleFocus({"type":"focus"});
	}
}

//Function which saves the task
function eventClickSaveMessage (moduleObject, element, event)
{	
	event.stopPropagation();
	var subject = dhtml.getElementById("insertprops_module"+moduleObject.id+"_subject");
	
	//save message only if enter key is pressed or save button is clicked.
	if (event.keyCode == 13 || event.type == "click") {
		
		if (subject) {
			//check subject field is filled properly	
			switch(subject.value)
			{
				case "":
				case _("Click here to add a new item"):
					alert(_("Fill in the Subject"));
					return false;
				default:
					if (subject.value.trim().length == 0) {
						alert(_("Fill in the Subject"));
						return false;
					}
			}
		}
	
		//find parent tr node to pass it to SubmitMessage() function
		while (element = element.parentNode) {
			if (element.tagName == "TR") {
				break;
			}
		}
		if(moduleObject.SubmitMessage(moduleObject, element)){
			var form = dhtml.getElementById("insertmessage");

			if (form) {
				form.reset();
				form.blur();
			}
			return false;
		}
	}
	
	//Reset form when ESC button is pressed...
	if (event.keyCode == 27) {		
		var form = dhtml.getElementById("insertmessage");
		if (form) {
			form.reset();
			form.blur();
		}
		
		var showimp = dhtml.getElementById("module"+ moduleObject.id +"_importance");
		if (showimp) {		
			showimp.className = "message_icon icon_norepeat icon_taskarrow";
		}
	}	
}

//Function which removes input fields
ListModule.prototype.removeinputfields = function(moduleObject, element, event)
{		
	var row = dhtml.getElementById(moduleObject.editMessage);
	var checked="";

	if (row && row != element) {		
		var importance = moduleObject.itemProps[moduleObject.entryids[row.id]]["importance"];
		var column = dhtml.getElementById("property_module"+ moduleObject.id +"_"+ row.id +"_importance");
		column.className = "rowcolumn message_icon " + moduleObject.tooltip["importance"]["css"][importance];
		
		for (var j = 0; j < row.cells.length; j++) {
			row.cells[j].innerHTML = moduleObject.previousvalue[row.cells[j].id];
		}
		
		//set drag and drop event for tasks.
		dhtml.addEvent(moduleObject.id, row, "mousedown", eventDragDropMouseDownDraggable);

		//set click event so that task is saved as complete when user selects the complete flag...
		if (dhtml.getElementById("property_module"+moduleObject.id +"_"+ row.id +"_complete")) {
			dhtml.addEvent(moduleObject.id, dhtml.getElementById("property_module"+moduleObject.id +"_"+ row.id +"_complete"), "click", eventListChangeCompleteStatus);	
		}
		
		moduleObject.editMessage = false;		
		
		//add css class 'complete' if task was set as complete...		
		if (moduleObject.itemProps[moduleObject.entryids[row.id]]["complete"] == 1) {
			for (var i = 0; i < row.cells.length; i++) {
				if (!dhtml.hasClassName(row.cells[i],"message_icon")){				
					dhtml.addClassName(row.cells[i],"complete");
				}
			}
		}
	}
}

//Function which deselects the messages...
function eventDeselectMessages(moduleObject, element, event)
{
	var deselect = new Array();

	//deselect messages visually i.e change class of selected messages.
	for (var i = 0; i < moduleObject.selectedMessages.length; i++) {
		dhtml.deselectElement(moduleObject.selectedMessages[i],"rowselected");
		deselect[i]=moduleObject.selectedMessages[i];
	}

	//delete or remove selected message ids from 'selectedMessages' variable...
	for (var i = 0; i < deselect.length; i++) {
		for (var j = 0; j < moduleObject.selectedMessages.length; j++) {
			if (deselect[i] == moduleObject.selectedMessages[j]) {
				moduleObject.selectedMessages.splice(j,1);
			}
		}
	}
	moduleObject.prevselectedMessage = false;
	if(event)
		event.stopPropagation();
}

//Function which creates importance menu
function eventClickChangeImportance(moduleObject, element, event)
{
	event.stopPropagation();
	
	
	//show importance menu only on left click mouseevent on importance column...
	if (event.button == 0 || event.button == 1) {
		
		//check importance menu is already selected...
		importancemenu = dhtml.getElementById("importancemenu");
		if (importancemenu) {
			//remove menu if selected...
			dhtml.showHideElement(importancemenu, event.clientX, event.clientY, true);
		}
		
		//create menu for importance and append it to document body...
		importancemenu = dhtml.addElement(document.body,"div","importancemenu","importancemenu");
		divimportance = "<table cellspacing='0' cellpadding='0' width='65' style='visibility: visible;'><tr class='menurow'><td class='icon icon_norepeat icon_importance_low'/><td>Low</td></tr>";
		divimportance += "<tr class='menurow'><td class='icon icon_norepeat ";

		/**
		 * We want dropdown arrow for Normal Priority
		 * only in those list which has Quickitem ability.
		 */
		if (this.inputproperties.length > 0) {
			divimportance += "icon_taskarrow'/><td>Normal</td></tr>";
		} else {
			divimportance += "'/><td>Normal</td></tr>";
		}
		divimportance += "<tr class='menurow'><td class='icon icon_norepeat icon_importance_high'/><td>High</td></tr></table>";	
		importancemenu.innerHTML = divimportance;
		importancemenu.style.position = "absolute";
		importancemenu.style.display = "block";
		importancemenu.style.left = (event.clientX)- 5 + "px";
		importancemenu.style.top = (event.clientY)- 5 + "px";
		
		rows = importancemenu.getElementsByTagName("tr");
		if (rows) {
			for (var i =0; i < rows.length; i++) {
				dhtml.setEvents(moduleObject.id, rows[i], moduleObject.events["menu"]["importance"]);
			}
		}
		
		//store row id in messageID
		moduleObject.messageID = element.parentNode.id;
	}
}

/**
 * Fucntion stops propagating event for quicktime insert row which is text field in tasks folder.
 * So selection in text field becomes possible
 */
function eventListInsertRowMouseMove(moduleObject, element, event)
{
	event.stopPropagation();
}

/**
 * Fucntion stops propagating event for quicktime row in tasks folder in edit time mode.
 * So selection in text field becomes possible
 */
function eventListRowMouseMove(moduleObject, element, event)
{
	if(this.editMessage !== false)
		event.stopPropagation();
}

/**
* Function which set the IMPORTANCE STATUS...
* @param string messageEntryid	-the entryid of the message
* @param object priorityStatus		-properties of item
*/
ListModule.prototype.importanceStatus = function(messageEntryid, priorityStatus)
{	
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.entryid;
	data["props"] = new Object();
	data["props"]["entryid"] = messageEntryid;
	data["props"]["importance"] = priorityStatus;

	webclient.xmlrequest.addData(this, "save", data);
}

function eventMenuImportanceMouseOver (moduleObject, element, event)
{
	dhtml.addClassName(element,"menuitemover");
}

function eventMenuImportanceMouseOut (moduleObject, element, event)
{
	dhtml.removeClassName(element,"menuitemover");
}

function eventMenuImportanceClick (moduleObject, element, event)
{
	event.stopPropagation();
	importancemenu = dhtml.getElementById("importancemenu");
	
	if (importancemenu) {
		importancemenu.style.display = "none";

		//check if importancemenu is selected for editing task or entering new task...
		if (moduleObject.itemProps[moduleObject.entryids[moduleObject.messageID]]) {
			var columnElement = dhtml.getElementById("property_module"+ moduleObject.id +"_"+ moduleObject.messageID +"_importance");
			columnElement.title = moduleObject.tooltip["importance"]["value"][element.rowIndex];
			moduleObject.previousvalue["editprops_module"+moduleObject.id+"_importance"] = moduleObject.itemProps[moduleObject.entryids[moduleObject.messageID]]["importance"];
			moduleObject.itemProps[moduleObject.entryids[moduleObject.messageID]]["importance"] = element.rowIndex;
			
			/*if message is in edit form, save value in hidden importance field and return from this function bcouz, we want to save
			  priority when user presses ENTER key for saving all fields*/
			if (moduleObject.editMessage) {
				for (var i=0; i<3;i++) {
					if (dhtml.hasClassName(columnElement, moduleObject.tooltip["importance"]["css"][i])) {
						dhtml.removeClassName(columnElement, moduleObject.tooltip["importance"]["css"][i]);
					}
				}			
				dhtml.addClassName(columnElement, moduleObject.tooltip["importance"]["css"][element.rowIndex]);		
				
				//hidden importance input field element
				var importanceElement = dhtml.getElementById("editprops_module"+ moduleObject.id +"_importance");
				importanceElement.value = element.rowIndex;
			} else {
				moduleObject.importanceStatus(moduleObject.itemProps[moduleObject.entryids[moduleObject.messageID]]["entryid"], element.rowIndex);
			}
		} else {
			//Just change css class and importance value if entering new task...
			var importanceElement = dhtml.getElementById("insertprops_module"+ moduleObject.id +"_importance");
			var priority = parseInt(importanceElement.value);
			var className = moduleObject.tooltip["importance"]["css"][priority];
			var columnElement = dhtml.getElementById("module"+ moduleObject.id +"_importance");
			columnElement.title = moduleObject.tooltip["importance"]["value"][element.rowIndex];
			
			dhtml.removeClassName(columnElement, className);		
			importanceElement.value = element.rowIndex;
			dhtml.addClassName(columnElement, moduleObject.tooltip["importance"]["css"][element.rowIndex]);
		}
		
		//After saving importance remove importancemenu from DOM...
		dhtml.showHideElement(importancemenu, importancemenu.Left, importancemenu.Top, true);
		dhtml.executeEvent(dhtml.getElementById("divelement"),"mousedown");
	}
}

function eventListStopBubble(moduleObject, element, event)
{
	if (event.keyCode != 13 && event.keyCode != 27) {
		event.stopPropagation();
		moduleObject.stopbubble = true;
	}
}

/**
 * Function to select the old selected email again. If there isn't any message selected
 * previously then we will select the first item.
 * clears the variable of old selected email's entryid.
 */
ListModule.prototype.selectOldSelectedMessages = function()
{
	var messageObjs = new Array();

	if(this.oldSelectedMessageIds.length > 0) {
		for(var idIndex = 0; idIndex < this.oldSelectedMessageIds.length; idIndex++) {
			// check if the message exists in the list, can happen that it is on a different page after sorting
			var rowId = this.getRowId(this.oldSelectedMessageIds[idIndex]);

			// get html element of rows that should be selected
			if(rowId) {
				messageObjs.push(dhtml.getElementById(rowId));
			}
		}
	} else {
		// get element object of first message
		var rowId = this.getMessageByRowNumber(0);

		if(rowId) {
			messageObjs.push(dhtml.getElementById(rowId));
		}
	}

	if (messageObjs.length > 0) {
		// only select first message as we clicked on it
		// when we select an element, we want to scroll the view and set the cursor position
		this.viewController.setCursorPosition(messageObjs[0].id);
		tableViewScroll(this, messageObjs[0], false);
		
		// explicitly call event listener for loading item in preview pane
		this.sendEvent("selectitem", this.entryids[messageObjs[0].id]);

		// select all other messages
		this.selectMessages(messageObjs, "rowselected");

		// after selecting messages change the focus to table view element,
		// so further keystrokes can be handled by inputmanager
		webclient.inputmanager.changeFocus(this.element);
	}

	// clear the variable to stop calling this function each time.
	this.oldSelectedMessageIds = new Array();
}

/**
 * Defining a global function to store old selected email's entryid 
 * which is needed to select the same email after reload of list.
 */
ListModule.prototype.storeSelectedMessageIds = function()
{
	// overwrite old value and store new values
	this.oldSelectedMessageIds = this.getSelectedIDs();
}

/**
 * Function which handles Edit menu(OL) options
 */
function eventListKeyCtrlEdit(moduleObject, element, event)
{
	if (moduleObject.selectedMessages && moduleObject.selectedMessages.length > 0){
		switch(event.keyCombination)
		{
			case this.keys["edit_item"]["copy"]:
			case this.keys["edit_item"]["move"]:
				moduleObject.showCopyMessagesDialog();
				break;
			case this.keys["edit_item"]["toggle_read"]:
				var selection = moduleObject.getSelectedMessages();
				// Change message flags for each selected message.
				for(i in selection){
					var nextFlag = "unread";
					var messageElement = dhtml.getElementById(selection[0]);
					
					// Mark only mail and task items
					if (messageElement.className.indexOf("mail") > 0 || messageElement.className.indexOf("task") > 0){
						if (messageElement.className.indexOf("message_unread") > 0){
							nextFlag = "read";
						}
						if (nextFlag == "read"){
							nextFlag = "read,"+ (moduleObject.sendReadReceipt(moduleObject.entryids[moduleObject.selectedMessages[i]])?"receipt":"noreceipt");
						}
						moduleObject.setReadFlag(moduleObject.entryids[moduleObject.selectedMessages[i]], nextFlag);
					}
				}
				break;
			case this.keys["edit_item"]["categorize"]:
				var callbackdata = new Object();
				callbackdata.module = moduleObject;
				callbackdata.entryid = moduleObject.entryids[moduleObject.selectedMessages[0]];
				webclient.openModalDialog(moduleObject, "categories", DIALOG_URL+"task=categories_modal&storeid=" + moduleObject.storeid + "&parententryid=" + moduleObject.entryid + "&entryid=" + moduleObject.entryids[moduleObject.selectedMessages[0]], 350, 370, categoriesCallBack, callbackdata);
				break;
			case this.keys["edit_item"]["print"]:
				eventListPrintMessage(moduleObject);
				break;
			case this.keys["edit_item"]["toggle_flag"]:
				for (var menu in moduleObject.contextmenu){
					if (moduleObject.contextmenu[menu].id.indexOf("flag") != -1){
						var items = moduleObject.getSelectedMessages();
						for(i in items){
							var item = moduleObject.itemProps[moduleObject.entryids[items[i]]];
							if (item)
								moduleObject.flagStatus(moduleObject.entryids[items[i]], item.flag_status == olFlagComplete ? olFlagMarked : olFlagComplete, item.flag_status == olPurpleFlagIcon ? olNoFlagIcon : olRedFlagIcon);
						}
						break;
					}
				}
				break;
		}
	}
}

function eventListKeyCtrlRefreshFolder(moduleObject, element, event)
{
	moduleObject.refresh();
}

/**
 * Keycontrol function which enables search options.
 */
function eventListKeyCtrlSearch(moduleObject, element, event)
{
	switch(event.keyCombination)
	{
		case this.keys["search"]["normal"]:
			var searchElement = dhtml.getElementById("search");
			if (searchElement) dhtml.executeEvent(searchElement, "click");
			break;
		case this.keys["search"]["advanced"]:
			var searchElement = dhtml.getElementById("advanced_find");
			if (searchElement) dhtml.executeEvent(searchElement, "click");
			break;
	}
}

/**
 * Global Event Function
 * This function will open advanced find dialog
 */
function eventListShowAdvancedFindDialog(moduleObject, element, event) {
	var uri = DIALOG_URL + "task=advancedfind_standard&storeid=" + moduleObject.storeid + "&entryid=" + moduleObject.entryid;
	var browserSize = dhtml.getBrowserInnerSize();
	webclient.openWindow(this, "advancedfind", uri, browserSize['x'] - 100, browserSize['y'] - 50);
}

/**
 * function for handle the restore button on main menu bar of folders.
 * @param object moduleObject Contains all the properties of a module object.
 * @param dom_element element The dom element's reference on which the event gets fired.
 * @param event event Event name
 * return - none, opens a dialog window.
 */
function eventRestoreItems(moduleObject, element, event){
	//task name should be "window.name _ standard" // as we are opening a standard dialog box.
	webclient.openWindow(
		moduleObject, 
		"restoreitems", 
		DIALOG_URL+"task=restoreitems_standard&storeid=" + this.storeid + "&parententryid=" + this.entryid,
		700, 500, false);
}

// for Linux-FF dont hide contextmenu on mouseup event of right mousebutton, so set flag on event.
function eventListFlagIconMouseUp(moduleObject, element, event)
{
	if (event.button == 2) {
		event.hideContextMenu = false;
	}
}
function eventListContextMenuClick(moduleObject, element, event)
{
	event.stopPropagation();
	// Get table rowelement from tabledata.
	parentElement = element.parentNode;
	// Here it assumes that the first classname is type of the element e.g. type -> 'row' for 'rowcontextmenu'
	var className = (parentElement.className.indexOf(" ") > 0?parentElement.className.substring(0, parentElement.className.indexOf(" ")):parentElement.className);
	var classNameContextMenu = className + "contextmenu";

	if(moduleObject.selectedContextMessage) {
		var message = dhtml.getElementById(moduleObject.selectedContextMessage);
		
		if(message) {
			dhtml.removeClassName(message, classNameContextMenu);
		}
	}

	if(!dhtml.hasClassName(parentElement, className + "selected")) {
		dhtml.addClassName(parentElement, classNameContextMenu);
	}

	moduleObject.selectedContextMessage = parentElement.id;

	var items = new Array();
	items.push(webclient.menu.createMenuItem("flag_icon_red", _("Red Flag"), false, eventMailListContextMenuMessageFlagChanged));
	items.push(webclient.menu.createMenuItem("flag_icon_blue", _("Blue Flag"), false, eventMailListContextMenuMessageFlagChanged));
	items.push(webclient.menu.createMenuItem("flag_icon_yellow", _("Yellow Flag"), false, eventMailListContextMenuMessageFlagChanged));
	items.push(webclient.menu.createMenuItem("flag_icon_green", _("Green Flag"), false, eventMailListContextMenuMessageFlagChanged));
	items.push(webclient.menu.createMenuItem("flag_icon_orange", _("Orange Flag"), false, eventMailListContextMenuMessageFlagChanged));
	items.push(webclient.menu.createMenuItem("flag_icon_purple", _("Purple Flag"), false, eventMailListContextMenuMessageFlagChanged));

	items.push(webclient.menu.createMenuItem("seperator", ""));

	items.push(webclient.menu.createMenuItem("flag_status_complete", _("Flag Complete"), false, eventMailListContextMenuMessageFlagChanged));
	items.push(webclient.menu.createMenuItem("flagno", _("Delete Flag"), false, eventMailListContextMenuMessageFlagChanged));

	webclient.menu.buildContextMenu(moduleObject.id, element.parentNode.id, items, event.clientX, event.clientY);
	return false;
}

/**
 * Function to stop propagation/bubbling of events.
 */
function eventStopBubbling(moduleObject, element, event){
	event.stopPropagation();

	// stopPropagation will stop execution of our event functions
	// so the browser default action will be executed, so also stop that
	if(event.type == "mouseup" || event.type == "mousedown") {
		event.preventDefault();
	}
}
