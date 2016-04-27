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

AdvancedFindListModule.prototype = new ListModule;
AdvancedFindListModule.prototype.constructor = AdvancedFindListModule;
AdvancedFindListModule.superclass = ListModule.prototype;

function AdvancedFindListModule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
	
}

/**
 * Function will initialize this module
 */
AdvancedFindListModule.prototype.init = function(id, element, title, data)
{
	AdvancedFindListModule.superclass.init.call(this, id, element, title, data);

	// maintain a flag to check table of tableWidget is generated or not
	this.tableGenerated = false;

	// quick hack for supporting next/prev page
	this.hackPaging = new Object();

	this.selectedFolders = new Object();

	// id of setTimeout function to refresh search results at every X seconds
	this.timeoutId = false;

	// view element which will have the table widget
	this.contentElement = dhtml.getElementById("advanced_find_view", "div", this.element);

	/**
	 * here we are owerwriting default methods of contexte menu
	 * because default methods are for table view and we are dealing with 
	 * table widget
	 */
	var items = new Array();
	items.push(webclient.menu.createMenuItem("open", _("Open"), false, eventAdvFindContextMenuOpenMessage));
	items.push(webclient.menu.createMenuItem("print", _("Print"), false, eventAdvFindContextMenuPrintMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("categories", _("Categories"), false, eventAdvFindContextMenuCategoriesMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("delete", _("Delete"), false, eventAdvFindContextMenuDeleteMessage));
	this.contextmenu = items;

	// initialize advanced find dialog
	this.initAdvancedFind(this.storeid, this.entryid);

	// initialize search criteria widget
	this.initSearchCriteriaWidget();

	// initialize table widget
	this.initTableWidget();

	// register key events
	dhtml.addEvent(this, dhtml.getElementById("advanced_find_searchwidget", "div", this.element), "keydown", eventAdvFindKeyEvents);
}

/**
 * Function will be called when initializing advanced find dialog
 * it will get message type based on selected folder and set it in dialog
 * @param		HexString		storeid			selected store id
 * @param		HexString		entryid			selected folder entryid
 */
AdvancedFindListModule.prototype.initAdvancedFind = function(storeid, entryid)
{
	// add selected folder's display name to search location box
	var search_location_box = dhtml.getElementById("search_location", "div", this.element);

	var selected_folder = parentWebclient.hierarchy.getFolder(entryid);

	if(selected_folder !== false) {
		dhtml.addElement(search_location_box, "span", false, false, selected_folder["display_name"] + ";");

		// select message type based on folder type
		var message_type_selector = dhtml.getElementById("message_type_selector", "select", this.element);
		switch(selected_folder["container_class"]) {
			case "IPF.Appointment":
				message_type_selector.selectedIndex = 1;
				message_type_selector.options[1].defaultSelected = true;
				break;
			case "IPF.Contact":
				message_type_selector.selectedIndex = 2;
				message_type_selector.options[2].defaultSelected = true;
				break;
			case "IPF.StickyNote":
				message_type_selector.selectedIndex = 4;
				message_type_selector.options[4].defaultSelected = true;
				break;
			case "IPF.Task":
				message_type_selector.selectedIndex = 5;
				message_type_selector.options[5].defaultSelected = true;
				break;
			case "IPF.Note":
			default:
				message_type_selector.selectedIndex = 3;
				message_type_selector.options[3].defaultSelected = true;
				break;
		}

		
		// set container class to use in module
		this.container_class = selected_folder["container_class"];

		// set selected folder entryid
		this.selectedFolders[0] = new Object();
		this.selectedFolders[0]["folderentryid"] = selected_folder["entryid"];
		this.selectedFolders[0]["foldername"] = selected_folder["display_name"];
		this.selectedFolders[0]["storeentryid"] = selected_folder["storeid"];

		// check current store supports search folder or not
		if(selected_folder && (selected_folder["store_support_mask"] & STORE_SEARCH_OK) == STORE_SEARCH_OK) {
			this.useSearchFolder = true;
		}
	}
}

/**
 * Function will initialize search criteria widget
 * this widget will create layout for all search options
 */
AdvancedFindListModule.prototype.initSearchCriteriaWidget = function()
{
	// change basic tab title from selected message type
	var message_type_selector = dhtml.getElementById("message_type_selector", "select", this.element);
	var selected_message_type = message_type_selector.options[message_type_selector.selectedIndex];

	this.searchCriteriaWidget = new SearchCriteria(this, dhtml.getElementById("advanced_find_searchwidget", "div", this.element));
	this.searchCriteriaWidget.initSearchCriteria(selected_message_type.value, selected_message_type.firstChild.nodeValue);
}

/**
 * Function will initialize table widget to show search results
 * table widget is initialized only once, and then its reused
 */
AdvancedFindListModule.prototype.initTableWidget = function()
{
	// initialize table widget to show search results
	var viewContentElement = dhtml.getElementById("advanced_find_tablewidget", "div", this.contentElement);
	this.advFindTableWidget = new TableWidget(viewContentElement, true, false, true);
}

/**
 * Function which execute an action. This function is called by the XMLRequest object.
 * @param		string		type		the action type
 * @param		object		action		the action tag 
 */ 
AdvancedFindListModule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "list":
			this.messageList(action);
			break;
		case "item":
			this.item(action);
			break;
		case "failed": // If any action (copy/move) has failed 
			this.handleActionFailure(action);
			break;
		case "search":
			this.updateSearch(action);
			break;
		case "search_error": // if any error occurred while searching 
			this.handleSearchError(action);
			break;
		case "delete":
			this.deleteItems(action);
			break;
	}
}

/**
 * Function will show a warning to user about error occured on server side
 * @param		XMLNode		action		the action tag
 */
AdvancedFindListModule.prototype.handleSearchError = function(action)
{
	this.clearSearch();

	// show error message to user
	var errorMessage = dhtml.getXMLValue(action, "error_message", false);

	if(errorMessage) {
		alert(errorMessage);
	}
}

/**
 * Function will add all items in the view, and also add columns to view
 * @param		XMLNode		action		the action tag
 */
AdvancedFindListModule.prototype.messageList = function(action)
{
	// get columns data from XML response
	this.getColumnDataFromXML(action);

	// get sorting info from XML response
	this.getSortDataFromXML(action);

	// add columns in table widget that is fetched from XML response
	advFindAddColumns(this, this.properties);

	//flag to check if user is explicity sorting or not
	var sortingFlag = dhtml.getXMLValue(action, "has_sorted_results", false);
	
	var items = action.getElementsByTagName("item");
	if(items.length > 0) {
		// store properties of messages
		this.getItemPropsFromXML(items);
		
		// create row column data and pass it to table widget to create layout
		this.tableGenerated = advFindSetRowColumnData(this, items, this.properties, sortingFlag);
	}

	this.paging(action);

	if(!this.useSearchFolder) {
		/**
		 * if we are not using search folder then we will get all data
		 * in a single request so after data has arrived change buttons' visibility
		 */
		 this.toggleActionButtons("search_stop");
	}
}

/**
 * Function will add a single item in the view
 * @param		XMLNode		action		the action tag
 */
AdvancedFindListModule.prototype.item = function(action)
{
	var items = action.getElementsByTagName("item");

	/**
	 * check if table has been generated, if its not generated
	 * then we have to generate it first then we can add rows
	 */
	if(items.length > 0) {
		if(this.tableGenerated === false) {
			// store properties of messages
			this.getItemPropsFromXML(items);

			// create row column data and pass it to table widget to create layout
			this.tableGenerated = advFindSetRowColumnData(this, items, this.properties);
		} else {
			// create an object to pass as data object in tablewidget
			for(var itemIndex = 0; itemIndex < items.length; itemIndex++) {
				var itemData = items[itemIndex];

				this.updateItemProps(itemData);
				var rowId = dhtml.getXMLValue(itemData, this.uniqueid);

				// check if row is already present in table widget
				if(this.advFindTableWidget.getRowByRowID(rowId)) {
					this.advFindTableWidget.updateRow(rowId, advFindRenderRowColumnData(this, itemData, this.properties));
				} else {
					this.advFindTableWidget.addRow(advFindRenderRowColumnData(this, itemData, this.properties));
				}
			}
		}
	}
}

/**
 * Function will delete items from the view
 * @param		XMLNode		action		the action tag
 */
AdvancedFindListModule.prototype.deleteItems = function(action)
{
	var entryids = action.getElementsByTagName("entryid");

	if(entryids.length > 0 && this.tableGenerated) {
		for(var idIndex = 0; idIndex < entryids.length; idIndex++) {
			var rowId = dhtml.getTextNode(entryids[idIndex], false);

			// check if row is already present in table widget
			if(this.advFindTableWidget.getRowByRowID(rowId)) {
				this.deleteItemProps(rowId);
				this.advFindTableWidget.deleteRow(rowId);
			}
		}
	}

	// server will send new items to replace with deleted items, so add it to table widget
	this.item(action);
}

/**
 * Function which takes care of the paging element
 * @param		object		action		the action tag 
 */ 
AdvancedFindListModule.prototype.paging = function(action, noReload)
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
				var selected = advFindPagingElement(this, this.totalrowcount, this.rowcount, this.rowstart);

				if(!selected && !noReload) {
					this.rowstart -= this.rowcount;
					this.search();
				}
			} else {
				advFindRemovePagingElement(this);
				if(this.rowstart > 0 && !noReload) {
					this.rowstart = 0;
					this.search();
				}
			}

			// also update item count when paging is updated/removed
			advFindUpdateItemCount(this.totalrowcount, false);
		}
	}
}

/**
* Function to change the current page (when more then one pages exists)
* @param		int			page		The page number to switch to
*/
AdvancedFindListModule.prototype.changePage = function(page)
{
	// reset table widget, so new data will not be appended to old data
	this.advFindTableWidget.resetWidget();

	// and show loader
	this.advFindTableWidget.showLoader();

	this.rowstart = page * this.rowcount;
	this.search();
}

/**
 * Function will create restriction array based on search data given
 * @return		object		restriction		search restriction array
 */
AdvancedFindListModule.prototype.getSearchRestrictionData = function()
{
	var fullRestriction = this.searchCriteriaWidget.createSearchRestrictionObject();

	if(fullRestriction !== false) {
		// convert restriction part to json string
		fullRestriction = JSON.stringify(fullRestriction);

		var restriction = new Object();
		restriction["attributes"] = new Object();
		restriction["attributes"]["type"] = "json";
		restriction["_content"] = fullRestriction;

		return new Array(restriction);
	} else {
		return false;
	}
}

/**
 * Function will create restriction array based on search data given
 * @param object data restriction array
 * @return object data search restriction array
 */
AdvancedFindListModule.prototype.search = function()
{
	if(this.selectedFolders && this.container_class) {
		var data = new Object();
		data["store"] = this.storeid;
		data["container_class"] = this.container_class;

		data["entryid"] = new Array();
		for(var key in this.selectedFolders) {
			data["entryid"].push(this.selectedFolders[key]["folderentryid"]);
		}

		data["restriction"] = new Object();
		if(this.getSearchRestrictionData) {
			data["restriction"]["search"] = this.getSearchRestrictionData();
			if(data["restriction"]["search"] == false) {
				return false; // Abort request if restriction is not created
			}
		}

		data["restriction"]["start"] = this.rowstart;
		
		data["subfolders"] = this.searchSubfolders;

		// search folder should be used or not
		data["use_searchfolder"] = this.useSearchFolder;

		if(this.sort) {
			data["sort"] = new Object();
			data["sort"]["column"] = this.sort;
			data["sort_result"] = true;
		}else{
			//by default set it to sort on subject in asc order
			this.sort = new Array();
			var column = new Object();
			column["attributes"] = new Object();
			column["attributes"]["direction"] = "asc";
			column["_content"] = "subject";
			// push column in a sortColumns array
			this.sort.push(column);

			data["sort"] = new Object();
			data["sort"]["column"] = this.sort;
			data["sort_result"] = false;
		}

		webclient.xmlrequest.addData(this, "search", data);
		webclient.xmlrequest.sendRequest();

		return true;
	}

	return false;
}

/**
 * Function is used to check status of the search and send request for further data if search is running.
 *
 * NOTE:	When we fire search request the results/response comes in multiple bunch of responses,
 *			so that we need to check, whether search is still going or final response has came.
 * @param		object		action			the action tag 
 */
AdvancedFindListModule.prototype.updateSearch = function(action)
{
	var searchFolderEntryID = dhtml.getXMLValue(action, "searchfolderentryid", false);
	this.searchFolderEntryID = searchFolderEntryID;		// add reference to DOM
	var searchState = dhtml.getXMLValue(action, "searchstate", 0);

	if(searchState == 0) {
		// search is not running and we are using search folder which 
		// has already finished the searching
		this.searchInProgress = false;
	}

	/**
	 * always update paging because it could happen that after finishing
	 * the search an item is added in search results
	 */
	this.paging(action);

	if(this.searchInProgress || this.finalSearchRequest) {
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
			var module = this;		// Fix loss-of-scope in setTimeout function
			setTimeout(function() {
				module.getSearchUpdate();
			}, 500);
		} else {
			// request for the last time an update, because it could happen the search is stopped
			// at the same time this request was made.
			this.getSearchUpdate();
			this.searchInProgress = false;
			// This check is used to fetch the last paging result comes back from
			// the server when this.searchInProgress is already set to false.
			this.finalSearchRequest = true;
		}
	} else {
		if(this.useSearchFolder) {
			this.disableSearchIndicator();
		}

		// disable stop button and enable other buttons
		if(typeof this.toggleActionButtons != "undefined") {
			this.toggleActionButtons("search_stop");
		}

		/**
		 * search is completed now so we have to refresh results
		 * at interval of some seconds 
		 * and seconds will be fetched from settings
		 */
		this.refreshSearchResult();
	}
}

/**
 * Function will be called after search has been finished,
 * and it will refresh search results after some interval
 * set by user in settings
 */
AdvancedFindListModule.prototype.refreshSearchResult = function()
{
	// get setting
	var refreshTime = webclient.settings.get("advancedfind/refresh_time", 0);

	if(refreshTime != 0) {
		var module = this;		// Fix loss-of-scope in setTimeout function
		this.timeoutId = setTimeout(function() {
			module.getSearchUpdate();
		}, refreshTime * 1000);
	}
}

/**
 * Function will send XML data to get search status from server
 */
AdvancedFindListModule.prototype.getSearchUpdate = function()
{
	var data = new Object();
	data["store"] = this.storeid;
	data["entryid"] = this.searchFolderEntryID;

	webclient.xmlrequest.addData(this, "updatesearch", data);
	webclient.xmlrequest.sendRequest();
}

/**
 * Function will reset search options and also stop search if its running
 * it will also reset table widget data so table widget object can be reused
 */
AdvancedFindListModule.prototype.resetSearch = function()
{
	// check user has changed something
	if(!this.checkForChangedValues()) {
		return false;
	}

	// stop search if its running
	if(this.searchInProgress !== false) {
		// send request to stop search
		this.stopSearch();
	}

	this.clearSearch();

	// reset fields in searchcriteria widget
	this.searchCriteriaWidget.resetFieldValues();

	return true;
}

/**
 * Function will clear all search results and variables
 * this will be called when any error occurs in searching
 */
AdvancedFindListModule.prototype.clearSearch = function()
{
	// disable search indicator
	if(this.useSearchFolder) {
		this.disableSearchIndicator();
	}

	if(this.searchFolderEntryID) {
		// delete search folder on server
		this.deleteSearchFolder();
	}

	// disable stop button and enable other buttons
	this.toggleActionButtons("search_stop");

	// reset table widget
	this.advFindTableWidget.resetWidget();

	// remove paging stuff
	advFindRemovePagingElement(this);

	// remove item count
	advFindUpdateItemCount(false, true);

	if(this.timeoutId) {
		// clear timer
		clearTimeout(this.timeoutId);
		this.timeoutId = false;
	}
	this.sort = false;
	this.searchInProgress = false;
	this.searchFolderEntryID = false;
	this.tableGenerated = false;
}

/**
 * Function will send XML data to delete search folder on server
 * @param		Object		webclientObj		webclient obj to use for sending request
 */
AdvancedFindListModule.prototype.deleteSearchFolder = function(webclientObj)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["entryid"] = this.searchFolderEntryID;

	/**
	 * here we are sending request for deleting search folder using parentWebclient
	 * so on server side a new advancedfindlistmodule is loaded and variables are reset
	 * in that module, so there will be two copies of same module
	 *
	 * so if we are refreshing this dialog then it will not correctly reset variables
	 * on server and therefore it woould not work properly
	 *
	 * to solve this problem we can send all requests using parentWebclient,
	 * but don't know that, it is a proper solution or not.
	 */

	if(typeof webclientObj != "undefined") {
		webclientObj.xmlrequest.addData(this, "delete_searchfolder", data, webclient.modulePrefix);
		webclientObj.xmlrequest.sendRequest();
	} else {
		webclient.xmlrequest.addData(this, "delete_searchfolder", data);
		webclient.xmlrequest.sendRequest();
	}
}

/**
 * Function will show search indicator when search is in progress
 * this will be only shown when we are using search folders
 */
AdvancedFindListModule.prototype.enableSearchIndicator = function()
{
	var searchIndicator = dhtml.getElementById("search_indicator", "div", this.element);
	dhtml.addClassName(searchIndicator, "search_running");
}

/**
 * Function will remove search indicator when search is completed/stopped
 */
AdvancedFindListModule.prototype.disableSearchIndicator = function()
{
	var searchIndicator = dhtml.getElementById("search_indicator", "div", this.element);
	dhtml.removeClassName(searchIndicator, "search_running");
}

/**
 * Function will enable/disable action button based on search status
 * @param		String		search_status		search is started or stopped
 */
AdvancedFindListModule.prototype.toggleActionButtons = function(search_status)
{
	var searchStopButton = dhtml.getElementById("search_stop_button", "button", this.element);
	var searchStartButton = dhtml.getElementById("search_start_button", "button", this.element);
	var searchResetButton = dhtml.getElementById("search_reset_button", "button", this.element);
	var browseButton = dhtml.getElementById("folder_selector_button", "button", this.element);

	if(search_status == "search_start") {
		searchStopButton.disabled = false;
		searchStartButton.disabled = true;
		searchResetButton.disabled = true;
		browseButton.disabled = true;
	} else if(search_status == "search_stop") {
		searchStopButton.disabled = true;
		searchStartButton.disabled = false;
		searchResetButton.disabled = false;
		browseButton.disabled = false;

		/**
		 * if any search has returned 0 results then we will not generate table
		 * in table widget so loader will be still visible, so to remove that 
		 * loader we have to call the method here also
		 */
		// hide loader
		if(this.tableGenerated === false) {
			this.advFindTableWidget.hideLoader();
	
			// also show to user that no results has been found
			this.advFindTableWidget.showZeroResultMessage();
		}
	}
}

/**
 * Function will show a warning to the user that current user action will
 * reset all changes done by user, so user can continue or cancel action
 * @return		Boolean					true - user wants to continue
 *										false - user wants to abort action
 */
AdvancedFindListModule.prototype.checkForChangedValues = function()
{
	var showWarning = false;

	// check if user has changed something in search criteria widget
	if(this.searchCriteriaWidget.checkForChangedValues()) {
		showWarning = true;
	}

	// check if table widget contains data or not
	if(this.advFindTableWidget.getRowCount() !== 0) {
		showWarning = true;
	}

	// check variables are set or not
	if(this.tableGenerated || this.searchInProgress || this.searchFolderEntryID) {
		showWarning = true;
	}

	if(showWarning) {
		return confirm(_("This will clear your current search") + ".");
	} else {
		return true;
	}
}

/**
 * Function will be called when user clicks on a header column of table widget 
 * for sort the results on bases of the click column.This also set the sorting
 * column for the list
 * @param		Object			tblWidget		table widget object
 * @param		String			type			type of event
 * @param		HexString		columnId		columnId of element row
 * @param		EventObject		event			event object
 */
AdvancedFindListModule.prototype.eventAdvFindColumnClick = function(tblWidget, type, columnId, event) {
	if(typeof columnId != "undefined" && columnId) {

		//get the old columns on which the result were sorted
		var old_sortDirection = tblWidget.sortDirection;
		var old_sortColumn = tblWidget.sortColumn;

		var sortColumn = columnId.id.substring(columnId.id.indexOf("_") + 1);
		if(columnId.id.indexOf("sort") > 0) {
			sortColumn = columnId.id.substring(columnId.id.indexOf("_") + 1, columnId.id.indexOf("sort") - 1);
		}
		
		var sortDirection = "";
		// change direction only when you click the already sorted column
		if (sortColumn == old_sortColumn){
			sortDirection = old_sortDirection == "desc"? "asc" : "desc";
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
			
		var sortColumns = new Array();
		var column = new Object();
		column["attributes"] = new Object();
		column["attributes"]["direction"] = sortDirection;
		column["_content"] = sortColumn;
		// push column in a sortColumns array
		sortColumns.push(column);

		// If sortColumns is not available then return false otherwise return sortColumns.
		if(sortColumns.length <= 0){
			this.sort = false;
		}else{
			this.sort = sortColumns;
		}
		this.search();
	}
}

/**
 * Function will be called when user double clicks on a message in table widget
 * @param		Object			tblWidget		table widget object
 * @param		String			type			type of event
 * @param		HexString		rowId			rowid of element row
 * @param		EventObject		event			event object
 */
AdvancedFindListModule.prototype.eventAdvFindRowDblClick = function(tblWidget, type, rowId, event) {
	if(typeof rowId != "undefined" && rowId) {
		this.advFindOpenItem(rowId);
	}
}

/**
 * Function will be called when user right clicks on a message in table widget
 * @param		Object			tblWidget		table widget object
 * @param		String			type			type of event
 * @param		HexString		rowId			rowid of element row
 * @param		EventObject		event			event object
 */
AdvancedFindListModule.prototype.eventAdvFindRowContextMenu = function(tblWidget, type, rowId, event) {
	if(typeof rowId != "undefined" && rowId) {
		var selectedRow = tblWidget.getRowByRowID(rowId);
		webclient.menu.buildContextMenu(this.id, selectedRow.id, this.contextmenu, event.clientX, event.clientY);
	}
}

/**
 * Function will get item properties based on entryid
 * @param		HexString		entryid		entryid of item
 * @param		Array			itemProps	item properties
 */
AdvancedFindListModule.prototype.getItemDataByEntryid = function(entryid)
{
	return this.itemProps[entryid];
}

/**
 * Function will open dialog for selected messages, here we are using openitem internal event
 * which will be registered by listmodule
 * @param		HTMLElement		element				selected row element
 */
AdvancedFindListModule.prototype.advFindOpenItem = function(rowId) {
	if(typeof rowId != "undefined" && rowId) {
		// if selected row is passed then open that item
		var item = this.advFindTableWidget.getDataByRowID(rowId);
		var itemData = this.getItemDataByEntryid(item["entryid"]);

		// open dialog
		var messageType = messageClassToMessageType(itemData["message_class"].replace(/\./g,"_").toLowerCase());
		this.sendEvent("openitem", itemData["entryid"], messageType);
	} else {
		// if element is not passed then open all selected items
		var items = this.advFindTableWidget.getSelectedRowData();
		for(var index = 0; index < items.length; index++) {
			var item = this.getItemDataByEntryid(items[index]["entryid"]);

			// open dialog
			var messageType = messageClassToMessageType(itemData["message_class"].replace(/\./g,"_").toLowerCase());
			this.sendEvent("openitem", item["entryid"], messageType);
		}
	}
}

/**
 * Function which set the categories on a message
 * @param		string		messageEntryid		entryid of the message
 * @param		string		categories			list of categories to be set, divided by ; (semi-colon)
 */ 
AdvancedFindListModule.prototype.setCategories = function(messageEntryid, parentEntryid, categories)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = parentEntryid;
	data["props"] = new Object();
	data["props"]["entryid"] = messageEntryid;
	data["props"]["categories"] = categories;

	webclient.xmlrequest.addData(this, "save", data);
	webclient.xmlrequest.sendRequest();
}

/**
 * Function which resizes the view.
 */ 
AdvancedFindListModule.prototype.resize = function()
{
	var dialogDimensions = dhtml.getBrowserInnerSize();
	var dialogHeight = dialogDimensions['y'];
	var dialogWidth = dialogDimensions['x'];

	var searchWidgetElement = dhtml.getElementById("advanced_find_searchwidget", "div", this.element);
	var tableWidgetElement = dhtml.getElementById("advanced_find_tablewidget", "div", this.element);
	var resizeBar = dhtml.getElementById("advanced_find_view_resizebar", "div", this.element);
	var actionButtons = dhtml.getElementById("action_buttons", "div", this.element);
	var itemCountElement = dhtml.getElementById("advanced_find_itemcount", "div", this.element);

	// set position of action buttons
	actionButtons.style.left = searchWidgetElement.offsetWidth + 32 + "px";

	/**
	 * get heights of search widget and action buttons
	 * and set height / width of view element according to it
	 */
	var searchWidgetElementHeight = searchWidgetElement.offsetTop + searchWidgetElement.offsetHeight;
	var actionButtonsHeight = actionButtons.offsetTop + actionButtons.offsetHeight;

	var contentElementTop = 0;
	if(actionButtonsHeight < searchWidgetElementHeight) {
		contentElementTop = searchWidgetElementHeight + 10;
	} else {
		contentElementTop = actionButtonsHeight;
	}

	var contentElementHeight = dialogHeight - contentElementTop;
	if(contentElementHeight < 0) {
		contentElementHeight = 0;
	}

	// set height of view element
	this.contentElement.style.top = contentElementTop - itemCountElement.offsetHeight + "px";
	this.contentElement.style.height = contentElementHeight + "px";

	// resize container of table widget
	tableWidgetElement.style.height = contentElementHeight - itemCountElement.offsetHeight + "px";

	// resize table widget
	this.advFindTableWidget.resize();
}

/**
 * @destructor 
 * also calls destructor of widgets
 */
AdvancedFindListModule.prototype.destructor = function()
{
	// remove paging elements
	if(this.pagingTool) {
		this.pagingTool.destructor();
		dhtml.deleteAllChildren(dhtml.getElementById("pageelement_"+ this.id, "div", this.element).parentNode);
	}

	if(this.searchFolderEntryID) {
		// delete search folder before closing the dialog
		this.deleteSearchFolder(parentWebclient);
	}

	if(this.timeoutId) {
		// clear timer
		clearTimeout(this.timeoutId);
		this.timeoutId = false;
	}

	// call destructor of widgets
	this.advFindTableWidget.destructor();
	this.searchCriteriaWidget.destructor();

	delete this.searchCriteriaWidget;
	delete this.advFindTableWidget;

	// remove registered events
	dhtml.removeEvents(this.element);

	// remove elements
	dhtml.deleteAllChildren(this.element);
	dhtml.deleteElement(this.element);

	AdvancedFindListModule.superclass.destructor(this);
}

/**
 * Function will show folder selection dialog,
 * so user can select target folders to search
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				selected row element
 * @param		Object			event				event data object
 */
function eventAdvFindShowFolderSelectionDialog(moduleObject, element, event) {
	var windowData = new Object();
	var uri = DIALOG_URL+"task=selectfolder_modal&multipleSelection=true&validatorType=search&title=" + _("Select Folder(s)");
	
	if(typeof moduleObject.selectedFolders != "undefined") {
		windowData["entryid"] = moduleObject.selectedFolders;
		windowData["subfolders"] = moduleObject.searchSubfolders;
	}

	webclient.openModalDialog(moduleObject, "selectfolder", uri, 300, 320, advFindFolderSelectorCallback, null, windowData);
}

/**
 * callback function for selectfolder dialog
 * Function will store selected folder data in module
 * @param		Object			result				selected folders' data
 * @param		Object			callBackData		callBackData
 */
function advFindFolderSelectorCallback(result, callBackData) {
	module.selectedFolders = result["selected_folders"];
	module.searchSubfolders = result["subfolders"];
	module.storeid = result["storeid"];

	advFindSetSearchLocation(result["selected_folders"]);

	return true;
}

/**
 * Function will change message type and also reset search results
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				selected row element
 * @param		Object			event				event data object
 */
function eventAdvFindChangeMessageType(moduleObject, element, event) {
	// reset search criteria
	if(!moduleObject.resetSearch()) {
		// select previous value
		var selectedOptionValue = moduleObject.container_class.substring(4);
		selectedOptionValue = selectedOptionValue.toLowerCase();

		for(var option in element.options) {
			if(element.options[option] != null) {
				if(element.options[option].defaultSelected == true) {
					element.selectedIndex = option;
					break;
				}
			}
		}

		return;
	};

	var displayName, entryid, container_class;
	if(typeof parentWebclient != "undefined" && parentWebclient.hierarchy) {
		// get selected message type's value and title
		var selectedMessageType = element.options[element.selectedIndex];
		selectedMessageType.defaultSelected = true;
		var selectedMessageTypeValue = selectedMessageType.value;
		var selectedMessageTypeTitle = selectedMessageType.firstChild.nodeValue;

		moduleObject.searchCriteriaWidget.changeMessageType(selectedMessageTypeValue, selectedMessageTypeTitle);

		switch(selectedMessageTypeValue) {
			case "any_item":
				entryid = parentWebclient.hierarchy.defaultstore.root.entryid;
				container_class = "IPF.Note";
				break;
			case "appointments":
				entryid = parentWebclient.hierarchy.defaultstore.defaultfolders.calendar;
				container_class = "IPF.Appointment";
				break;
			case "contacts":
				entryid = parentWebclient.hierarchy.defaultstore.defaultfolders.contact;
				container_class = "IPF.Contact";
				break;
			case "notes":
				entryid = parentWebclient.hierarchy.defaultstore.defaultfolders.note;
				container_class = "IPF.StickyNote";
				break;
			case "tasks":
				entryid = parentWebclient.hierarchy.defaultstore.defaultfolders.task;
				container_class = "IPF.Task";
				break;
			case "messages":
			default:
				entryid = parentWebclient.hierarchy.defaultstore.defaultfolders.inbox;
				container_class = "IPF.Note";
				break;
		}

		/**
		 * outlook automatically selects subfolders option when any
		 * option is selected, so we are doing same thing here
		 */
		moduleObject.searchSubfolders = true;

		// get foldername and remove whitespaces
		displayName = parentwindow.dhtml.getElementById(entryid).lastChild.firstChild.firstChild.nodeValue;
		displayName = displayName.trim();

		moduleObject.entryid = entryid;
		moduleObject.storeid = parentWebclient.hierarchy.defaultstore.id;
		moduleObject.container_class = container_class;

		// also change value of selectedFolders variable
		for(var index = 0; index < moduleObject.selectedFolders.length; index++) {
			// remove previous values
			delete moduleObject.selectedFolders[index];
		}

		// add new values
		moduleObject.selectedFolders[0] = new Object();
		moduleObject.selectedFolders[0]["folderentryid"] = moduleObject.entryid;
		moduleObject.selectedFolders[0]["foldername"] = displayName;
		moduleObject.selectedFolders[0]["storeentryid"] = moduleObject.storeid;

		// change location search box value
		advFindSetSearchLocation(moduleObject.selectedFolders);
	}

	// resize module
	moduleObject.resize();
}

/**
 * Global Event Function
 * Function will stop search
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				selected row element
 * @param		Object			event				event data object
 */
function eventAdvFindStopSearch(moduleObject, element, event) {
	moduleObject.stopSearch();

	// disable search indicator
	if(moduleObject.useSearchFolder) {
		moduleObject.disableSearchIndicator();
	}

	// disable stop button and enable other buttons
	moduleObject.toggleActionButtons("search_stop");

	moduleObject.searchInProgress = false;
	moduleObject.searchFolderEntryID = false;

	if(this.timeoutId) {
		// clear timer
		clearTimeout(this.timeoutId);
		this.timeoutId = false;
	}
}

/**
 * Global Event Function
 * Function will start search, it will also reset table widget to show new search results
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				selected row element
 * @param		Object			event				event data object
 */
function eventAdvFindStartSearch(moduleObject, element, event) {

	// enable search indicator
	if(typeof moduleObject.selectedFolders != "undefined") {
		// get any folder property and check it supports search folders or not
		var selectedFolderProps = parentWebclient.hierarchy.getFolder(moduleObject.selectedFolders[0]["folderentryid"]);
		if(selectedFolderProps && (selectedFolderProps["store_support_mask"] & STORE_SEARCH_OK) == STORE_SEARCH_OK) {
			moduleObject.useSearchFolder = true;
		} else {
			moduleObject.useSearchFolder = false;
		}
	}

	if(moduleObject.useSearchFolder) {
		moduleObject.enableSearchIndicator();
	}

	// enable stop button and disable other buttons
	moduleObject.toggleActionButtons("search_start");

	// reset table widget
	moduleObject.advFindTableWidget.resetWidget();

	// show loader in table widget
	moduleObject.advFindTableWidget.showLoader();

	// remove paging stuff
	advFindRemovePagingElement(this);

	// set item count to zero initially
	advFindUpdateItemCount(0, false);

	// set variables
	moduleObject.searchInProgress = true;
	moduleObject.searchFolderEntryID = false;
	moduleObject.tableGenerated = false;

	// send data to server to start search
	if(!moduleObject.search()) {
		// error occured in sending request to server
		moduleObject.clearSearch();
	}
}

/**
 * Global Event Function
 * Function will reset all search options and table widget
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				selected row element
 * @param		Object			event				event data object
 */
function eventAdvFindNewSearch(moduleObject, element, event) {
	moduleObject.resetSearch();
}

/**
 * Global Event Function
 * Function will open a selected item
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				selected row element
 * @param		Object			event				event data object
 */
function eventAdvFindContextMenuOpenMessage(moduleObject, element, event) {
	// execute double click event of selected item
	var rowId = moduleObject.advFindTableWidget.getSelectedRowID(0);

	if(typeof rowId != "undefined" && rowId) {
		moduleObject.advFindOpenItem(rowId);
	}

	// remove context menu
	if(dhtml.getElementById("contextmenu")) {
		dhtml.deleteElement(dhtml.getElementById("contextmenu"));
	}
}

/**
 * Global Event Function
 * Function will open print dialog for selected item
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				selected row element
 * @param		Object			event				event data object
 */
function eventAdvFindContextMenuPrintMessage(moduleObject, element, event) {
	var rowId = moduleObject.advFindTableWidget.getSelectedRowID(0);

	if(typeof rowId != "undefined" && rowId) {
		var selectedItem = moduleObject.advFindTableWidget.getDataByRowID(rowId);
		moduleObject.printItem(selectedItem["entryid"]);
	}

	// remove context menu
	if(dhtml.getElementById("contextmenu")) {
		dhtml.deleteElement(dhtml.getElementById("contextmenu"));
	}
}

/**
 * Global Event Function
 * Function will open categories dialog for a selected message
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				selected row element
 * @param		Object			event				event data object
 */
function eventAdvFindContextMenuCategoriesMessage(moduleObject, element, event) {
	var rowId = moduleObject.advFindTableWidget.getSelectedRowID(0);

	if(typeof rowId != "undefined" && rowId) {
		var selectedItem = moduleObject.advFindTableWidget.getDataByRowID(rowId);
		var selectedItemData = moduleObject.getItemDataByEntryid(selectedItem["entryid"]);

		var callbackdata = new Object();
		callbackdata.moduleObject = moduleObject;
		callbackdata.entryid = selectedItemData["entryid"];
		callbackdata.parentEntryid = selectedItemData["parent_entryid"];

		var uri = DIALOG_URL + "task=categories_modal&storeid=" + moduleObject.storeid + "&parententryid=" + selectedItemData["parent_entryid"] + "&entryid=" + selectedItemData["entryid"];
		webclient.openModalDialog(moduleObject, "categories", uri, 350, 370, advFindCategoriesCallBack, callbackdata);
	}

	// remove context menu
	if(dhtml.getElementById("contextmenu")) {
		dhtml.deleteElement(dhtml.getElementById("contextmenu"));
	}
}

/**
 * Global Event Function
 * Function will delete selected item
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				selected row element
 * @param		Object			event				event data object
 */
function advFindCategoriesCallBack(categories, userData) {
	var entryid = userData["entryid"];
	var parentEntryid = userData["parentEntryid"];
	var moduleObject = userData["moduleObject"];

	moduleObject.setCategories(entryid, parentEntryid, categories);
}

/**
 * Global Event Function
 * Function will delete selected item
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				selected row element
 * @param		Object			event				event data object
 */
function eventAdvFindContextMenuDeleteMessage(moduleObject, element, event) {
	var selectedRowData = moduleObject.advFindTableWidget.getSelectedRowData();

	for(var index = 0; index < selectedRowData.length; index++) {
		var elemId = moduleObject.advFindTableWidget.getRowByRowID(selectedRowData[index]["rowID"]).id;
		moduleObject.deleteMessage(selectedRowData[index]["entryid"], elemId);
	}

	// remove context menu
	if(dhtml.getElementById("contextmenu")) {
		dhtml.deleteElement(dhtml.getElementById("contextmenu"));
	}
}

/**
 * Global Event Function
 * Function will be called on every keydown event of search widget container
 * @param		Object			moduleObject		advanced find module object
 * @param		HTMLElement		element				html element
 * @param		Object			event				event data object
 */
function eventAdvFindKeyEvents(moduleObject, element, event) {
	switch(event.keyCode) {
		case 13: // ENTER
			eventAdvFindStartSearch(moduleObject, element, event);
			break;
	}
}