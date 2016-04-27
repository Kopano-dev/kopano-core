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

contactlistmodule.prototype = new ListModule;
contactlistmodule.prototype.constructor = contactlistmodule;
contactlistmodule.superclass = ListModule.prototype;

function contactlistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
	this.emailList = Object();
}

contactlistmodule.prototype.init = function(id, element, title, data)
{
	// entryid of the folder
	this.entryid = data["entryid"];
	
	this.character = webclient.settings.get("folders/entryid_"+this.entryid+"/selected_char", "a");

	this.defaultview = webclient.settings.get("folders/entryid_"+this.entryid+"/selected_view", "contact_cards");

	contactlistmodule.superclass.init.call(this, id, element, title, data);

	this.keys["view"] = KEYS["view"];

	this.menuItems.push(webclient.menu.createMenuItem("seperator", ""));
	this.menuItems.push(webclient.menu.createMenuItem("contact_cards", _("Cards"), _("Address Cards"), eventContactlistSwitchView));
	this.menuItems.push(webclient.menu.createMenuItem("contact_list", _("List"), _("List View"), eventContactlistSwitchView));
	this.menuItems.push(webclient.menu.createMenuItem("seperator", ""));
	this.menuItems.push(webclient.menu.createMenuItem("search", _("Search"), _("Quick Search"), eventContactListToggleSearchBar, false, true));

	webclient.pluginManager.triggerHook('client.module.contactlistmodule.topmenu.buildup', {topmenu: this.menuItems});
	webclient.menu.buildTopMenu(this.id, "contact", this.menuItems, eventListNewMessage);

	var items = new Array();
	items.push(webclient.menu.createMenuItem("open", _("Open"), false, eventListContextMenuOpenMessage));
	items.push(webclient.menu.createMenuItem("print", _("Print"), false, eventListContextMenuPrintMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("createmail", _("Email Message"), false, eventContactlistClickEmail));
	items.push(webclient.menu.createMenuItem("categories", _("Categories"), false, eventListContextMenuCategoriesMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("delete", _("Delete"), false, eventListContextMenuDeleteMessage));
	webclient.pluginManager.triggerHook('client.module.contactlistmodule.contextmenu.buildup', {contextmenu: items});
	this.contextmenu = items;

	// define all search fields
	var searchFields = new Object();
	searchFields["full_name prefix suffix file_as company_name email_address_display_name_1 email_address_display_name_2 email_address_display_name_3 business_address home_address other_address"] = _("All text fields");
	searchFields["full_name prefix suffix"] = _("Name");
	searchFields["file_as"] = _("File as");
	searchFields["company_name"] = _("Company");
	searchFields["email_address_display_name_1 email_address_display_name_2 email_address_display_name_3"] = _("Email address");
	searchFields["home_telephone_number cellular_telephone_number office_telephone_number business_fax_number"] = _("Phone number");
	searchFields["business_address home_address other_address"] = _("Address");
	this.searchFields = searchFields;

	// check current store supports search folder or not
	var selectedFolderProps = webclient.hierarchy.getFolder(data["entryid"]);
	if(selectedFolderProps && (selectedFolderProps["store_support_mask"] & STORE_SEARCH_OK) == STORE_SEARCH_OK) {
		this.useSearchFolder = true;
	}

	this.initializeView();

	this.action = false;
	// Used by keycontrol, to switch between views
	this.availableViews = new Array("contact_cards", "contact_list");
}

contactlistmodule.prototype.initializeView = function(view)
{
	if (view){
		webclient.settings.set("folders/entryid_"+this.entryid+"/selected_view", view);
	}else{
		view = this.defaultview;
	}
	
	if (view == "contact_cards"){
		this.setTitle(this.title, NBSP, true);
	}else{
		this.setTitle(this.title, false, true);
	}
	this.selectedview = view;
	
	// create a container for search bar
	this.searchBarContainer = dhtml.addElement(this.element, "div", "listview_topbar", "listview_topbar_" + this.id);

	this.contentElement = dhtml.addElement(this.element, "div");
	
	if(this.filterRestriction) {
		// search restriction is on then remove letter filtering
		this.resetLetterFiltering();
	}

	var data = new Object();
	data["character"] = this.character;

	this.events["alfabet"] = new Object();
	this.events["alfabet"]["mouseover"] = eventContactListMouseOverAlfabetItem;
	this.events["alfabet"]["mouseout"] = eventContactListMouseOutAlfabetItem;
	this.events["alfabet"]["click"] = eventContactListClickAlfabetItem;
	this.viewController.initView(this.id, view, this.contentElement, this.events, data);
	
	// create html elements for search bar
	this.initSearchBar();

	// Add keycontrol events
	webclient.inputmanager.addObject(this, this.element);
	if (!webclient.inputmanager.hasKeyControlEvent(this, "keyup", eventContactListKeyCtrlChangeView)){
		webclient.inputmanager.bindKeyControlEvent(this, this.keys["view"], "keyup", eventContactListKeyCtrlChangeView);
	}
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["refresh"], "keyup", eventListKeyCtrlRefreshFolder);
}

/**
 * Function which execute an action. This function is called by the XMLRequest object. 
 * @param string type the action type
 * @param object action the action tag 
 */ 
contactlistmodule.prototype.execute = function(type, action)
{
	webclient.menu.showMenu();

	this.saveEmailInModule(type, action);
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
		case "search":
			this.updateSearch(action);
			break;
		case "search_error":
			this.handleSearchError(action);
			break;
		case "convert_contact":
			this.convertSelectedContactItem(action);
			break;
	}
}

/**
 * Function which does error handling while converting the selected contact item
 * @param object action the action tag 
 *
 * @TODO:
 * we can also drag an contact item to Task Folder but 
 * This is currently not implemented as it is not possible to send Task Request from WA.
 */

contactlistmodule.prototype.convertSelectedContactItem = function(action)
{
	var message = action.getElementsByTagName("item");
	var targetFolder = dhtml.getTextNode(action.getElementsByTagName("targetfolder")[0],"");
	var parentEntryid = dhtml.getTextNode(action.getElementsByTagName("targetfolderentryid")[0],"");

	var recipientString = "";
	for(var i=0;i < message.length; i++){
		var messageClass = dhtml.getXMLValue(message[i], "message_class", "").split(".");
		var objectType  = parseInt(dhtml.getXMLValue(message[i], "objecttype"),10);
		
		if(messageClass[1] == "DistList"){
			switch (objectType){
				case MAPI_DISTLIST:		// groups in GAB
					recipientString += '[' + dhtml.getXMLValue(message[i], "dl_name") + "]";
					break;
				case MAPI_MESSAGE:		// groups in contact folders
					recipientString += this.mergeABItems(message[i]);
					break;
			}
		}else{
			var isEmailEmpty = true;
			for (var j=1; j <= 3 ; j++){
				var email = dhtml.getXMLValue(message[i], "email_address_"+j, "");
				if(email != ""){
					var email_displayname = dhtml.getXMLValue(message[i], "email_address_display_name_"+j, "").replace("("+email+")","");
					email_displayname = email_displayname +"<"+ email +">";
					recipientString += email_displayname +";";
					isEmailEmpty = false;
				}
			}
			if(isEmailEmpty) var emailStringEmpty = true;
		}
	}
	
	//show an alert message for contacts which do not have e-mail address specified
	if(emailStringEmpty){
		if(message.length < 2){
			if(!confirm(_("You must first enter a valid e-mail address for this contact before you can send a message to it.")))
				return;
		}else{
			if(!confirm(_("Some of these contacts do not have e-mail addresses specified. E-mail addresses are required for sending them e-mail.")))
				return;
		}
	}

	var uri = DIALOG_URL+"task=" + targetFolder + "_standard&storeid=" + this.storeid+ "&parententryid=" + parentEntryid;
	if(targetFolder == "appointment") uri += "&meeting=true";
		webclient.openWindow(this, 
							targetFolder, 
							uri,
							720, 580, 
							true, null, null, 
							{
								"emails" : recipientString, 
								"action" : "convert_contact"
							});
}

/**
 * Function which gives groups items as recipients depending on address book type
 * @param object message the mesasge.
 * @return string of the recipients in groups
 */
contactlistmodule.prototype.mergeABItems = function(message)
{
	var result = "";
	var members = message.getElementsByTagName("members")[0];

	if(members){	
		var member = members.getElementsByTagName("member");
		for (var j = 0; j < member.length; j++){
			var displayName = dhtml.getXMLValue(member[j], "display_name", "");
			var emailAddress = dhtml.getXMLValue(member[j], "email_address", "");
			var objectType = parseInt(dhtml.getXMLValue(member[j], "objecttype"),10);

			if(objectType == MAPI_DISTLIST) {
				if(emailAddress)	result += "[" + emailAddress +  "]; ";
			} else {
				result += displayName + " <" + emailAddress + ">;" ;
			}
		}
	}
	return result;
}

/**
 * Function will save the email address from the contacts in action
 * @param string type the action type
 * @param object action the action tag 
 */ 
contactlistmodule.prototype.saveEmailInModule = function(type,action)
{
	var items = action.getElementsByTagName("item");
	for(var i=0; i < items.length; i++){
		var item = items[i];
		var entryID = dhtml.getTextNode(item.getElementsByTagName("entryid")[0],"");
		var email = dhtml.getTextNode(item.getElementsByTagName("email_address_1")[0],"");
		var email_displayname = dhtml.getTextNode(item.getElementsByTagName("display_name")[0],"");
		var sender = "";
		if(email_displayname.length > 0) {
			sender = email_displayname;
		}
		if(email.length > 0 && email != email_displayname) {
			sender += " <"+email+">";
		}
		if(type != "delete"){
			this.emailList[entryID] = sender;
		}else{
			this.emailList[entryID] = "";
		}
	}
}

contactlistmodule.prototype.actionAfterDelete = function()
{
	this.viewController.resizeView();
}

/**
 * Function will create restriction array
 * @return object data restriction array
 */
contactlistmodule.prototype.getRestrictionData = function()
{
	var data = new Object();

	if(this.character) {
		data["character"] = this.character;
	}

	// get search restrictions
	data = this.getSearchRestrictionData(data);

	return data;
}

/**
 * "Customized" event handler for opening items. Normally a read flag is set 
 * causing a request to the server. This request returns an update which causes a
 * reload of the entire contactlist, because the view does not support a single 
 * update. This function does not set the read receipt flag, but still opens the 
 * dialog.
 * 
 * message_type is the type of message "appointment", "task", "contact" etc (usally a part of the message_class)
 */
contactlistmodule.prototype.onOpenItem = function(entryid, message_type)
{
	var uri = DIALOG_URL+"task=" + message_type + "_standard&storeid=" + this.storeid + "&parententryid=" + this.entryid + "&entryid=" + entryid;
	webclient.openWindow(this, message_type, uri);
}

/**
 * Function will create html elements of search bar 
 * and set the visibility of the search bar
 */
contactlistmodule.prototype.initSearchBar = function()
{
	dhtml.addElement(this.searchBarContainer, "span", false, false, NBSP + _("Search") + NBSP + NBSP);
	
	// search fields target selector
	var searchFilterTarget = dhtml.addElement(this.searchBarContainer, "select", "searchfiltertarget");
	for(var key in this.searchFields) {
		searchFilterTarget.options[searchFilterTarget.length] = new Option(this.searchFields[key], key, false, false);
	}
	this.searchBarContainer.searchFilterTarget = searchFilterTarget;	// add reference to DOM

	var defaultTarget = webclient.settings.get("folders/entryid_" + this.entryid + "/searchbar/target", "0");
	searchFilterTarget.selectedIndex = defaultTarget;

	dhtml.addElement(this.searchBarContainer, "span", false, false, NBSP + NBSP + _("for") + NBSP + NBSP);

	var searchFilterInputBoxValue = "";
	if(this.filterRestriction) {
		searchFilterInputBoxValue = this.filterRestriction;
	}
	var searchFilterInputBox = dhtml.addElement(null, "input", "searchfilter");
	searchFilterInputBox.value = searchFilterInputBoxValue;
	searchFilterInputBox.setAttribute("type", "text");
	this.searchBarContainer.appendChild(searchFilterInputBox);
	this.searchBarContainer.searchFilterInputBox = searchFilterInputBox;	// add reference to DOM

	// set events for search input box
	dhtml.addEvent(this, searchFilterInputBox, "click", eventContactListSearchFilterClick);
	dhtml.addEvent(this, searchFilterInputBox, "mousedown", eventContactListSearchFilterClick);
	dhtml.addEvent(this, searchFilterInputBox, "mouseup", eventStopBubbling);
	dhtml.addEvent(this, searchFilterInputBox, "mousemove", eventContactListSearchBarFilterMouseMove);
	dhtml.addEvent(this, searchFilterInputBox, "keydown", eventContactListSearchFilterKeyDown);
	dhtml.addEvent(this, searchFilterInputBox, "selectstart", eventContactListSearchBarFilterMouseMove);
	dhtml.addEvent(this, searchFilterInputBox, "focus", eventContactListSearchFilterFocus);
	dhtml.addEvent(this, searchFilterInputBox, "blur", eventContactListSearchFilterFocus);
	dhtml.addEvent(this, searchFilterTarget, "mousedown", function(module,element,event){event.stopPropagation()});

	// subfolder search
	// Only show subfolders when store supports search folders
	if(this.useSearchFolder) {
		var searchSubfolders = dhtml.addElement(null, "input", "searchsubfolders", "searchsubfolders_" + this.id);
		searchSubfolders.setAttribute("type", "checkbox");

		if(this.searchSubfolders === "true") {
			// check if subfolders checkbox is already selected
			searchSubfolders.defaultChecked = true;
		} else {
			searchSubfolders.defaultChecked = false;
		}

		this.searchBarContainer.appendChild(searchSubfolders);
		this.searchBarContainer.searchSubfolders = searchSubfolders;	// add reference to DOM

		var label = dhtml.addElement(this.searchBarContainer, "label", false, false, _("Subfolders") + NBSP);
		label.setAttribute("for", "searchsubfolders_" + this.id);
	}

	// create butttons
	var searchFilterButton = dhtml.addElement(this.searchBarContainer, "button", "searchfilterbutton", "searchfilterbutton", NBSP);
	dhtml.addEvent(this, searchFilterButton, "click", eventContactListSearchBarSearch);
	
	var searchClearButton = dhtml.addElement(this.searchBarContainer, "button", "searchclearbutton", false, NBSP);
	this.searchBarContainer.searchClearButton = searchClearButton;		// add reference to DOM
	if(!this.filterRestriction) {
		this.searchBarContainer.searchClearButton.disabled = true;
	}
	dhtml.addEvent(this, searchClearButton, "click", eventContactListSearchBarClear);

	// set the visibility of the search bar
	var displaySearchBar = webclient.settings.get("folders/entryid_" + this.entryid + "/searchbar/show", "false");

	var searchMenuItem = dhtml.getElementById("search", "a", this.menuBarLeft);
	if(displaySearchBar == "true") {
		this.searchBarContainer.style.display = "block";
		webclient.menu.toggleItem(searchMenuItem, true);
	} else {
		this.searchBarContainer.style.display = "none";
		webclient.menu.toggleItem(searchMenuItem, false);
	}
}

/**
 * Function is used to check status of the search and send request
 * for further data if search is running
 * @param object action the action tag 
 */
contactlistmodule.prototype.updateSearch = function(action)
{
	var search_error = dhtml.getXMLValue(action, "searcherror", false);
	if(search_error) {
		this.clearSearchBar();
		alert(_("Error in search, please try again") + ".");
	}

	// call parent class' updateSearch() method
	contactlistmodule.superclass.updateSearch.call(this, action);
}

/**
 * in addresscards view, this function removes letter filtering
 * and resets it to default character
 */
contactlistmodule.prototype.resetLetterFiltering = function() {
	// remove current selection
	var currentSelectedCharacter = dhtml.getElementById("character_" + this.character);
	if(currentSelectedCharacter) {
		currentSelectedCharacter.className = currentSelectedCharacter.className.substring(0, currentSelectedCharacter.className.lastIndexOf("characterover"));
	}

	// change selection to default
	this.character = "...";

	var defaultCharacter = dhtml.getElementById("character_" + this.character);
	if(defaultCharacter) {
		defaultCharacter.className += " characterover";
	}

	webclient.settings.set("folders/entryid_" + this.entryid + "/selected_char", this.character);
}

function eventContactListMouseOverAlfabetItem(moduleObject, element, event)
{
	element.className += " characterover";
}

function eventContactListMouseOutAlfabetItem(moduleObject, element, event)
{
	if(element.className.indexOf("characterover") > 0) {
		element.className = element.className.substring(0, element.className.lastIndexOf("characterover"));
	}
}

function eventContactListClickAlfabetItem(moduleObject, element, event)
{
	var character = dhtml.getElementById("character_" + moduleObject.character);
	if(character) {
		character.className = character.className.substring(0, character.className.lastIndexOf("characterover"));
	}
	
	element.className += " characterover";

	moduleObject.character = element.id.substring(element.id.indexOf("_") + 1);
	webclient.settings.set("folders/entryid_"+moduleObject.entryid+"/selected_char", moduleObject.character);

	// if exists then remove current search restriction
	if(moduleObject.filterRestriction != false) {
		this.clearSearchBar();
	} else {
		moduleObject.list();
	}
}

function eventContactlistSwitchView(moduleObject, element, event)
{
	var newView = element.id;
	moduleObject.destructor(moduleObject);
	moduleObject.initializeView(newView);
	moduleObject.list();
	moduleObject.resize();
}

/**
 * Function will open an create email dialog and put the email address in the "to" field
 */
function eventContactlistClickEmail(moduleObject, element, event) 
{ 
	element.parentNode.style.display = "none";
	var entryID = moduleObject.entryids[element.parentNode.elementid];
	var email = moduleObject.emailList[entryID];
	webclient.openWindow(moduleObject, "createmail", DIALOG_URL+"task=createmail_standard&to=" + email);
	eventListCheckSelectedContextMessage(moduleObject); 
}

/**
 * Function will change view when keys are presses
 */
function eventContactListKeyCtrlChangeView(moduleObject, element, event)
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

/**
 * Function is used to toggle search menuitem and show search bar
 * @param object moduleObject current module
 * @param object element element initiated this event
 * @param object event the event object
 */
function eventContactListToggleSearchBar(moduleObject, element, event)
{
	if(moduleObject.searchBarContainer.style.display == "none") {
		webclient.menu.toggleItem(element, true);
		webclient.settings.set("folders/entryid_" + moduleObject.entryid + "/searchbar/show", "true");
		moduleObject.searchBarContainer.style.display = "block";
	} else {
		webclient.menu.toggleItem(element, false);
		webclient.settings.set("folders/entryid_" + moduleObject.entryid + "/searchbar/show", "false");
		moduleObject.searchBarContainer.style.display = "none";
	}

	// when there is a restriction active, remove it and reload contactlist
	if(moduleObject.filterRestriction != false) {
		this.clearSearchBar();
	}

	moduleObject.resize();
}

/**
 * Function is used to reset search options and if search is running then stop it
 * @param object moduleObject current module
 * @param object element element initiated this event
 * @param object event the event object
 */
function eventContactListSearchBarClear(moduleObject, element, event)
{
	moduleObject.clearSearchBar();
}

/**
 * Function is used to start search
 * @param object moduleObject current module
 * @param object element element initiated this event
 * @param object event the event object
 */
function eventContactListSearchBarSearch(moduleObject, element, event)
{
	var input = moduleObject.searchBarContainer.searchFilterInputBox;
	var target = moduleObject.searchBarContainer.searchFilterTarget;
	var subfolders = moduleObject.searchBarContainer.searchSubfolders;

	if(input.value.trim() != "") {
		if(typeof moduleObject.viewController.viewObject.view == "undefined" || moduleObject.viewController.viewObject.view != "table") {
			// only for address cards view
			moduleObject.resetLetterFiltering();
		}

		moduleObject.searchInProgress = true;
		moduleObject.filterRestriction = input.value;
		moduleObject.filterRestrictionTarget = target.value;
		moduleObject.rowstart = 0;
		input.blur();

		webclient.settings.set("folders/entryid_" + moduleObject.entryid + "/searchbar/target", target.selectedIndex.toString());

		if(subfolders) {
			moduleObject.searchSubfolders = subfolders.checked ? "true" : "false";
		} else {
			moduleObject.searchSubfolders = false;
		}

		// enable search indicator
		if(moduleObject.useSearchFolder) {
			moduleObject.enableSearchIndicator();
		}

		// send xml data to server to start the search
		moduleObject.search();
	}
}

/**
 * Function is called when user clicks on search text box
 * @param object moduleObject current module
 * @param object element element initiated this event
 * @param object event the event object
 */
function eventContactListSearchFilterClick(moduleObject, element, event)
{
	var result = false;

	event.stopPropagation();

	if(!element.hasFocus) {
		element.focus();
	} else {
		result = true;
	}
	return result;
}

/**
 * Function is called when user enters anything in search text box
 * @param object moduleObject current module
 * @param object element element initiated this event
 * @param object event the event object
 */
function eventContactListSearchFilterKeyDown(moduleObject, element, event)
{
	moduleObject.searchBarContainer.searchClearButton.disabled = false;

	// don't send key events to parent div
	// to prevent firing events registered by table view 
	event.stopPropagation();

	if(event.keyCode == 13) {	// ENTER key
		// start searching
		eventContactListSearchBarSearch(moduleObject, element, event);
	}
}

function eventContactListSearchFilterFocus(moduleObject, element, event)
{
	element.hasFocus = (event.type == "focus");
}

function eventContactListSearchBarFilterMouseMove(moduleObject, element, event)
{
	event.stopPropagation();
	return element.hasFocus;
}
