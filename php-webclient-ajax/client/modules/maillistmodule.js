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

maillistmodule.prototype = new ListModule;
maillistmodule.prototype.constructor = maillistmodule;
maillistmodule.superclass = ListModule.prototype;

function maillistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

maillistmodule.prototype.init = function(id, element, title, data)
{
	maillistmodule.superclass.init.call(this, id, element, title, data);

	this.menuItems.push(webclient.menu.createMenuItem("seperator", ""));
	this.menuItems.push(webclient.menu.createMenuItem("reply", _("Reply"), _("Reply"), eventMailListReplyMessage));
	this.menuItems.push(webclient.menu.createMenuItem("replyall", _("Reply All"), _("Reply All"), eventMailListReplyAll));
	this.menuItems.push(webclient.menu.createMenuItem("forward", _("Forward"), _("Forward"), eventMailListForwardMessage));
	this.menuItems.push(webclient.menu.createMenuItem("seperator", ""));
	this.menuItems.push(webclient.menu.createMenuItem("search", _("Search"), _("Quick Search"), eventMailListSearchBar, "S", true));
	//check weather selected folder is public; if so don't display rules options
	if(typeof data["storeid"] != "undefined" && !webclient.hierarchy.isPublicStore(data["storeid"])) {
		this.menuItems.push(webclient.menu.createMenuItem("seperator", ""));
		this.menuItems.push(webclient.menu.createMenuItem("rules", _("Rules"), _("Edit e-mail rules"), eventMailListRules));
	}
	this.menuItems.push(webclient.menu.createMenuItem("seperator", ""));
	this.menuItems.push(webclient.menu.createMenuItem("send_receive", _("Send/Receive"), _("Send or Receive mails") , eventSendReceiveItems));

    /* Trigger the hook so plugins can add functionality in the top menu */
    webclient.pluginManager.triggerHook('client.module.maillistmodule.topmenu.buildup', {topmenu: this.menuItems});

	webclient.menu.buildTopMenu(this.id, "createmail", this.menuItems, eventListNewMessage);
	
	var items = new Array();
	items.push(webclient.menu.createMenuItem("open", _("Open"), false, eventListContextMenuOpenMessage));
	items.push(webclient.menu.createMenuItem("save_email", _("Save Email As a File"), false, eventListContextMenuSaveEmailMessage));
	items.push(webclient.menu.createMenuItem("print", _("Print"), false, eventListContextMenuPrintMessage));
	items.push(webclient.menu.createMenuItem("edit", _("Edit as New Message"), false, eventListContextMenuEditMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("reply", _("Reply"), false, eventMailListContextMenuReply));
	items.push(webclient.menu.createMenuItem("replyall", _("Reply All"), false, eventMailListContextMenuReplyAll));
	items.push(webclient.menu.createMenuItem("forward", _("Forward"), false, eventMailListContextMenuForward));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("markread", _("Mark Read"), false, eventMailListContextMenuMessageFlag));
	items.push(webclient.menu.createMenuItem("markunread", _("Mark Unread"), false, eventMailListContextMenuMessageFlag));
	items.push(webclient.menu.createMenuItem("categories", _("Categories")+"...", false, eventListContextMenuCategoriesMessage));
	items.push(webclient.menu.createMenuItem("forward_items", _("Forward Items"), false, eventListContextMenuForwardMultipleItems));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("flag_status_red", _("Red Flag"), false, eventMailListContextMenuRedFlag));
	items.push(webclient.menu.createMenuItem("flag_status_complete", _("Flag Complete"), false, eventMailListContextMenuFlagComplete));
	items.push(webclient.menu.createMenuItem("flagno", _("Delete Flag"), false, eventMailListContextMenuDeleteFlag));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("junk", _("Move to Junk Folder"), false, eventListContextMenuMoveJunkMessage));
	items.push(webclient.menu.createMenuItem("delete", _("Delete"), false, eventListContextMenuDeleteMessage));
	//items.push(webclient.menu.createMenuItem("copy", _("Copy/Move Message"), false, eventListContextMenuCopyMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("task", _("Create a task from email"), false, eventListContextMenuTask));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("options", _("Options")+"...", false, eventListContextMenuMessageOptions));

	webclient.pluginManager.triggerHook('client.module.maillistmodule.contextmenu.buildup', {contextmenu: items});
	this.contextmenu = items;
	
	this.layoutmenu = new Array();
	
	var previewMode = webclient.settings.get("global/previewpane","right"); // global default
	previewMode = webclient.settings.get("folders/entryid_"+this.entryid+"/previewpane",previewMode);
	this.layoutmenu.push(webclient.menu.createMenuItem("previewpane_right", _("Previewpane right"), false, eventLayoutSwitchPreviewpane));
	this.layoutmenu.push(webclient.menu.createMenuItem("previewpane_bottom", _("Previewpane bottom"), false, eventLayoutSwitchPreviewpane));
	this.layoutmenu.push(webclient.menu.createMenuItem("previewpane_off", _("Previewpane off"), false, eventLayoutSwitchPreviewpane));

	// Handle events from listmodule
	this.addEventHandler("selectitem", this, this.onSelectItem);
	this.addEventHandler("deletitem", this, this.onDeleteItem);
	
	// List of keys that should be handled
	this.keys["respond_mail"] = KEYS["respond_mail"];
	this.keys["readingpane"] = KEYS["readingpane"];

	// register object for keycontrol
	webclient.inputmanager.addObject(this, this.element);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["respond_mail"], "keyup", eventMailListKeyCtrlRespond, true);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["readingpane"], "keyup", eventMailListKeyCtrlToggleReadingPane, false);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["refresh"], "keyup", eventListKeyCtrlRefreshFolder);

	// check for search capabilities of store
	if(typeof data["entryid"] != "undefined" && data["entryid"] != "") {
		var selectedFolderProps = webclient.hierarchy.getFolder(data["entryid"]);
		if(selectedFolderProps && (selectedFolderProps["store_support_mask"] & STORE_SEARCH_OK) == STORE_SEARCH_OK) {
			this.useSearchFolder = true;
		}
	} else {
		/**
		 * a special feature of maillistmodule, if entryid is not present then we can say that
		 * current folder is inbox of user's own store so search folders will always be supported
		 */
		 this.useSearchFolder = true;
	}

	this.initializeView();
}

maillistmodule.prototype.execute = function(type, action)
{
	webclient.pluginManager.triggerHook('client.module.maillistmodule.execute.before', {type: type, action: action});
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
		case "search":
			this.updateSearch(action);
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
		//it check whether a task is created or not,thus does the error handling.
		case "task_created":
			if(dhtml.getXMLValue(action, "success", false))
				alert(_("A new task has been created and is moved into task folder"));
			else
				alert(_("You have insufficient privileges to create a new task for this item"));
			break;
		case "convert_meeting":
			this.convertSelectedMeetingItem(action);
			break;
	}
}

/**
 * Function which does error handling while converting the selected meeting item
 * @param object action the action tag 
 */ 
maillistmodule.prototype.convertSelectedMeetingItem = function(action)
{
	var message = action.getElementsByTagName("item");
	var targetFolder = dhtml.getTextNode(action.getElementsByTagName("targetfolder")[0],"");
	var parentEntryid = dhtml.getTextNode(action.getElementsByTagName("targetfolderentryid")[0],"");

	for(var i=0;i < message.length; i++){
		var entryid = dhtml.getXMLValue(message[i], "entryid");
		var messageClass = dhtml.getXMLValue(message[i], "message_class", "").trim();
		var delegator = dhtml.getXMLValue(message[i], "rcvd_representing_name");
		var meetingType = parseInt(dhtml.getXMLValue(message[i], "out_of_date"),10);
		var responseStatus = parseInt(dhtml.getXMLValue(message[i], "responsestatus"),10);
	
		//show an error message for dragged meeting requests which do not have correct privileges set
		if(messageClass == "IPM.Schedule.Meeting.Request"){
			if(delegator){
				alert(_("Cannot copy this item. Cannot open calendar for user %s: Access Denied.").sprintf(delegator));
			}else if(meetingType){
				alert(_("You received a more recent update to this meeting. This meeting request is out of date. You cannot respond this request."));
			}else if(responseStatus == olResponseNotResponded){
				this.sendConfirmationAcceptFromMailList(entryid);
				alert(_("The meeting has been accepted and moved to your calendar. A response has been sent to the meeting organiser."));
			}

		}else{
			var uri = DIALOG_URL+"task=" + targetFolder + "_standard&storeid=" + this.storeid+ "&parententryid=" + parentEntryid;
			webclient.openWindow(this, 
								targetFolder, 
								uri,
								720, 580, 
								true, null, null, 
								{
									"data" : action, 
									"action" : "convert_meeting"
								});
		}
	}
}

maillistmodule.prototype.messageList = function(action)
{
    var title = dhtml.getXMLValue(action, "folder_title");
	var entryid = dhtml.getXMLValue(action, "entryid");
    var storeid = dhtml.getXMLValue(action, "storeid");
    var isinbox = dhtml.getXMLValue(action, "isinbox");

	if(title) {
		// if title is already added, then change only folder name
		titleBar = dhtml.getElementsByClassNameInElement(dhtml.getElementById("main"), "zarafa_title")[0];
		if(titleBar) {
			titleBar.firstChild.firstChild.nodeValue = title;
		} else {
			this.setTitle(title, false, true);
		}
    }

    if(storeid && entryid) {
        this.entryid = entryid;
        this.storeid = storeid;
        if(this.previewreadmailitemmodule)
            this.previewreadmailitemmodule.setData(this.storeid, this.entryid);
    }
    this.isinbox = isinbox;

    maillistmodule.superclass.messageList.call(this, action);
}

/**
 * Function which adds or updates an item in the view.
 * this function additionally updates flags in previewitem module and then
 * calls parent class' item method for default handling
 * 
 * @param XMLObject action the action tag 
 */ 
maillistmodule.prototype.item = function(action)
{
	// update message flags in previewitem module
	if(this.previewreadmailitemmodule) {
		var items = action.getElementsByTagName("item");
		for(var index = 0; index < items.length; index++) {
			var item = items[index];
			var entryid = dhtml.getXMLValue(item, "entryid", false);

			if(compareEntryIds(this.previewreadmailitemmodule.messageentryid, entryid)) {
				this.previewreadmailitemmodule.updateMessageFlags(item);
			}
		}
	}

    maillistmodule.superclass.item.call(this, action);
}

function eventLayoutSwitchPreviewpane(moduleObject, element, event){
	element.parentNode.style.display = "none";
	var previewMode;
	if (dhtml.hasClassName(element, "icon_previewpane_off")){
		previewMode = "off";
	}else if (dhtml.hasClassName(element, "icon_previewpane_right")){
		previewMode = "right";
	}else if (dhtml.hasClassName(element, "icon_previewpane_bottom")){
		previewMode = "bottom";
	}
	webclient.settings.set("folders/entryid_"+this.entryid+"/previewpane",previewMode);
	if(this.isinbox)
		webclient.settings.set("folders/entryid_inbox/previewpane",previewMode);

	webclient.hierarchy.selectLastFolder(true); // reload this folder
}

maillistmodule.prototype.initializeView = function()
{
	if (this.title!=false){
		this.setTitle(this.title, false, true);
	}
	this.searchBarContainer = dhtml.addElement(this.element, "div", "listview_topbar", "listview_topbar_"+this.id);

	this.contentElement = dhtml.addElement(this.element, "div");

	this.viewController.initView(this.id, "email", this.contentElement, this.events);

	// If the entryid is not set, then we're viewing the inbox
	var previewMode = webclient.settings.get("global/previewpane","right"); // global default
	if(!this.entryid)
		previewMode = webclient.settings.get("folders/entryid_inbox/previewpane",previewMode);
	else
		previewMode = webclient.settings.get("folders/entryid_"+this.entryid+"/previewpane",previewMode);
	
	if (previewMode == "right" || previewMode == "bottom"){
		this.previewreadmailitemmodule = webclient.dispatcher.loadModule("previewreadmailitemmodule");
		if(this.previewreadmailitemmodule) {
			var moduleID = webclient.addModule(this.previewreadmailitemmodule);
			this.previewPane = webclient.layoutmanager.addModule(moduleID, previewMode == "bottom" ? "main" : "right", BOX_LAYOUT, INSERT_ELEMENT_AT_BOTTOM);
			this.previewreadmailitemmodule.init(moduleID, this.previewPane);
			this.previewreadmailitemmodule.setData(this.storeid, this.entryid);
			if (previewMode == "right") {
				webclient.layoutmanager.updateElements("right");
			}
		}
	}

	this.initSearchBar();
}


maillistmodule.prototype.loadFieldSettings = function()
{
	if(webclient.hasRightPane){
		var path = "folders/entryid_"+this.entryid+"/fields";
		data = webclient.settings.get(path);
		var result = new Array();
		for(var i in data){
			switch(data[i]['id']){
				case ("display_to"):
				case ("sent_representing_name"):
				case ("client_submit_time"):
				case ("message_delivery_time"):
					result.push(data[i]);
					break;
			}
		}
		return result;
	}else {
		 return maillistmodule.superclass.loadFieldSettings.call(this);
	}
}

maillistmodule.prototype.updateSearch = function(action)
{
	var search_error = dhtml.getXMLValue(action, "searcherror", false);
	if(search_error) {
		this.clearSearchBar();
		alert(_("Error in search, please try again"));
	}

	// call parent class' updateSearch() method
	maillistmodule.superclass.updateSearch.call(this, action);
}

maillistmodule.prototype.getRestrictionData = function()
{
	var result = new Object();

	// get search restrictions
	result = this.getSearchRestrictionData(result);

	return result;
}

maillistmodule.prototype.resize = function(action, messageEntryid)
{
	var elementHeight = (this.element.offsetHeight - this.contentElement.offsetTop) - 1;

	if (elementHeight<0) 
		elementHeight = 0;

	this.contentElement.style.height = (elementHeight) + "px";
	
	this.viewController.resizeView();
}

maillistmodule.prototype.replyMail = function(action, messageEntryid)
{
	this.setReadFlag(messageEntryid, "read,"+(this.sendReadReceipt(messageEntryid)?"receipt":"noreceipt"));

	webclient.openWindow(this, "createmail", DIALOG_URL+"task=createmail_standard&message_action=" + action + "&storeid=" + this.storeid + "&parententryid=" + this.entryid + "&entryid=" + messageEntryid);
}

maillistmodule.prototype.destructor = function()
{
	// Unregister from InputManager.
	webclient.inputmanager.removeObject(this);
	dhtml.removeEvent(document.body, "click", eventListCheckSelectedContextMessage);
	
	this.element.innerHTML = "";
	
	if(this.previewreadmailitemmodule) {
		this.previewreadmailitemmodule.destructor();
		webclient.deleteModule(this.previewreadmailitemmodule);
	}
	
	maillistmodule.superclass.destructor(this);
}

maillistmodule.prototype.initSearchBar = function()
{
	// create search bar
	var searchText = dhtml.addElement(this.searchBarContainer, "span", false, false, _("Search")+" "+NBSP);

	// create search target selector
	var filterTarget = dhtml.addElement(this.searchBarContainer, "select", "searchfiltertarget");
	(dhtml.addElement(filterTarget, "option", false, false,_("All text fields"))).value = "subject body sender_name sender_email sent_representing_name sent_representing_email to cc";
	(dhtml.addElement(filterTarget, "option", false, false,_("Subject"))).value = "subject";
	(dhtml.addElement(filterTarget, "option", false, false,_("Sender"))).value = "sender_name sender_email sent_representing_name sent_representing_email";
	(dhtml.addElement(filterTarget, "option", false, false,_("Body"))).value = "body";
	(dhtml.addElement(filterTarget, "option", false, false,_("Subject or Sender"))).value = "subject sender_name sender_email sent_representing_name sent_representing_email";
	(dhtml.addElement(filterTarget, "option", false, false,_("To or Cc"))).value = "to cc";

	var defaultTarget = webclient.settings.get("folders/entryid_"+this.entryid+"/searchbar/target", "0");
	filterTarget.selectedIndex = defaultTarget;
	
	this.searchBarContainer.filterTarget = filterTarget; // add reference tot DOM

	dhtml.addTextNode(this.searchBarContainer, NBSP+" "+_("for")+" "+NBSP);

	var previewMode = webclient.settings.get("global/previewpane","right"); // global default
	if(!this.entryid)
		previewMode = webclient.settings.get("folders/entryid_inbox/previewpane",previewMode);
	else
		previewMode = webclient.settings.get("folders/entryid_"+this.entryid+"/previewpane",previewMode);
	
	if (previewMode == "right"){
		// Create a two-line search bar
		dhtml.addElement(this.searchBarContainer, "br");

		// Shift inputbox to be directly under filterTarget
		var spacer = dhtml.addElement(this.searchBarContainer, "div", false, false, NBSP);
		spacer.style.marginLeft = (searchText.offsetWidth-3) + "px";
		spacer.style.display = "inline";
	}
	
	// create search input element
	var searchFilterInputBox = dhtml.addElement(null, "input", "searchfilter");
	searchFilterInputBox.setAttribute("type", "text");
	searchFilterInputBox.setAttribute("autocomplete","off"); // workaround for Firefox 1.5 bug with autocomplete and emtpy string: "'Permission denied to get property XULElement.selectedIndex' when calling method: [nsIAutoCompletePopup::selectedIndex]"
	searchFilterInputBox.value = "";

	dhtml.addEvent(this, searchFilterInputBox, "click", eventMailListSearchBarFilterClick);
	dhtml.addEvent(this, searchFilterInputBox, "mousedown", eventMailListSearchBarFilterClick);
	dhtml.addEvent(this, searchFilterInputBox, "mouseup", eventStopBubbling);
	dhtml.addEvent(this, searchFilterInputBox, "mousemove", eventMailListSearchBarFilterMouseMove);
	dhtml.addEvent(this, searchFilterInputBox, "focus", eventMailListSearchBarFilterFocus);
	dhtml.addEvent(this, searchFilterInputBox, "blur", eventMailListSearchBarFilterFocus);
	dhtml.addEvent(this, searchFilterInputBox, "keydown", eventMailListSearchBarFilterKey);
	dhtml.addEvent(this, searchFilterInputBox, "selectstart", eventMailListSearchBarFilterMouseMove);
	dhtml.addEvent(this, filterTarget, "mousedown", function(module,element,event){event.stopPropagation()});

	this.searchBarContainer.appendChild(searchFilterInputBox);
	this.searchBarContainer.searchFilterInputBox = searchFilterInputBox; // add reference to DOM

	// subfolder search
	// Only show subfolders option when store supports search folders
	if(this.useSearchFolder){
		var searchSubfolders = dhtml.addElement(null, "input", "searchsubfolders", "searchsubfolders_"+this.id);
		searchSubfolders.setAttribute("type", "checkbox");
		this.searchBarContainer.appendChild(searchSubfolders);

		var label = dhtml.addElement(this.searchBarContainer, "label", false, false, _("Subfolders") + NBSP);
		label.setAttribute("for", "searchsubfolders_" + this.id);
		this.searchBarContainer.searchSubfolders = searchSubfolders;
	}

	// create buttons
	var filterButton = dhtml.addElement(this.searchBarContainer, "button", "searchfilterbutton", "searchfilterbutton", NBSP);
	dhtml.addEvent(this, filterButton, "click", eventMailListSearchBarSearch);

	var searchClearButton = dhtml.addElement(this.searchBarContainer, "button", "searchclearbutton", false, NBSP);
	dhtml.addEvent(this, searchClearButton, "click", eventMailListSearchBarClear);
	this.searchBarContainer.searchClearButton = searchClearButton; // add reference to DOM
	this.searchBarContainer.searchClearButton.disabled = true;

	// set the visibility of the search bar
	var displaySearchBar = webclient.settings.get("folders/entryid_"+this.entryid+"/searchbar/show","false");

	var searchMenuItem = dhtml.getElementById("search", "a", this.menuBarLeft);
	if(displaySearchBar == "true") {
		this.searchBarContainer.style.display = "block";
		webclient.menu.toggleItem(searchMenuItem, true);
		this.resize();
	}else{
		this.searchBarContainer.style.display = "none";
		webclient.menu.toggleItem(searchMenuItem, false);
	}
}


maillistmodule.prototype.onDeleteItem = function(entryid)
{
	// Remove message in previewpane when selected message is deleted/moved
	if(this.previewPane && this.previewreadmailitemmodule && 
		compareEntryIds(this.previewreadmailitemmodule.messageentryid, entryid)) 
	{
		this.previewreadmailitemmodule.destructor();
	}
}

maillistmodule.prototype.onSelectItem = function(entryid)
{
	if(this.previewreadmailitemmodule) {
		// if we are first time loading previewitem then messageentryid will be false
		// if we pass false as entryid then compareEntryIds will return false
		if(!compareEntryIds(this.previewreadmailitemmodule.messageentryid, entryid)) {
			eventListPreviewTimer(this, entryid);
		}
	}
}

/**
 * Function which popups option to attendee when user drags any meeting item which is not responded.
 *@param string messageEntryid entryid of the dragged message
 *@param timestamp basedate date to the occurence
 *@param boolean noResponse flag to set response flag
 */
maillistmodule.prototype.sendConfirmationAcceptFromMailList = function(entryid, noResponse, basedate)
{
	var req = new Object;
	req['entryid'] = entryid;
	req['store'] = this.storeid;

	if (typeof basedate != 'undefined' && basedate !== "false" && basedate)
		req['basedate'] = basedate;

	// if sendResponse flag is set then attach sendResponse flag with request data.
	if(typeof(noResponse) != "undefined" && noResponse)
		req['noResponse'] = noResponse;

	webclient.xmlrequest.addData(this, 'acceptMeeting', req);
	webclient.xmlrequest.sendRequest(true);
}

function eventListPreviewTimer(moduleObject, entryid)
{
	window.clearTimeout(moduleObject.previewTimer);
	moduleObject.previewTimer = null;

	if(moduleObject.previewreadmailitemmodule) {
		if(!compareEntryIds(moduleObject.previewreadmailitemmodule.messageentryid, entryid)) {
			moduleObject.previewreadmailitemmodule.open(entryid, entryid);
			moduleObject.previewreadmailitemmodule.loadMessage();
		}
	}
}

function eventMailListReplyMessage(moduleObject, element, event)
{
	if(moduleObject.selectedMessages.length > 0) {
		moduleObject.replyMail("reply", moduleObject.entryids[moduleObject.selectedMessages[0]]);
	}
}

function eventMailListReplyAll(moduleObject, element, event)
{
	if(moduleObject.selectedMessages.length > 0) {
		moduleObject.replyMail("replyall", moduleObject.entryids[moduleObject.selectedMessages[0]]);
	}
}

function eventMailListForwardMessage(moduleObject, element, event)
{
	if(moduleObject.selectedMessages.length > 0) {
		moduleObject.replyMail("forward", moduleObject.entryids[moduleObject.selectedMessages[0]]);
	}
}

/**
 * Sends request to server to download email in '.eml' format.
 * @param moduleObject object maillist module object
 * @param element object HTML element object on which event is fired
 * @param event object event type object
 */
function eventListContextMenuSaveEmailMessage(moduleObject, element, event)
{
	// Hide Context menu.
	if (element && element.parentNode)
		element.parentNode.style.display = "none";

	/**
	 * Get selected Message's entryid and pass it to server and
	 * set 'emailAsAttachment' option to true to save email.
	 * This will save email in eml format.
	 */
	var selectedMessage = moduleObject.getSelectedMessages();
	var messegeEntryId = moduleObject.entryids[selectedMessage[0]];
	var action = webclient.base_url + "index.php?load=download_message&fileType=eml&store=" + moduleObject.storeid + "&entryid=" + messegeEntryId;

	var iframe = dhtml.getElementById("iframedownload");

	// Open download attahcment dialog.
	if (!iframe) iframe = dhtml.addElement(document.body, "iframe", "iframeDownload", "iframedownload");
	iframe.contentWindow.location = action;
}

function eventMailListContextMenuReply(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	moduleObject.replyMail("reply", moduleObject.entryids[moduleObject.selectedContextMessage]);
	
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventMailListContextMenuReplyAll(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	moduleObject.replyMail("replyall", moduleObject.entryids[moduleObject.selectedContextMessage]);
	
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventMailListContextMenuForward(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	moduleObject.replyMail("forward", moduleObject.entryids[moduleObject.selectedContextMessage]);
	
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventMailListContextMenuMessageFlag(moduleObject, element, event)
{
	// hide context menu
	element.parentNode.style.display = "none";
	// get flag state
	var flag = "read";
	if(element.className.indexOf("unread") > 0) {
		flag = "unread";
	}
	// change flag
	var items = moduleObject.getSelectedMessages(element.parentNode.elementid);
	var flags = flag;
	for(i in items){
		if (flag=="read"){
			flags = "read,"+(moduleObject.sendReadReceipt(moduleObject.entryids[items[i]])?"receipt":"noreceipt");
		}else{
			flags = flag;
		}
		moduleObject.setReadFlag(moduleObject.entryids[items[i]], flags);

		// after manually setting the read flag we have to reset timer that is used to automatically
		// set read flag through previewitem module
		if(this.previewreadmailitemmodule && compareEntryIds(this.previewreadmailitemmodule.messageentryid, moduleObject.entryids[items[i]])) {
			this.previewreadmailitemmodule.clearReadFlagTimer();
		}
	}

	// deselect selected message
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventMailListContextMenuRedFlag(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var items = moduleObject.getSelectedMessages(element.parentNode.elementid);
	for(i in items){
		moduleObject.flagStatus(moduleObject.entryids[items[i]], olFlagMarked, olRedFlagIcon);
	}
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventMailListContextMenuFlagComplete(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var items = moduleObject.getSelectedMessages(element.parentNode.elementid);
	for(i in items){
		moduleObject.flagStatus(moduleObject.entryids[items[i]], olFlagComplete, olNoFlagIcon);
	}
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventMailListContextMenuDeleteFlag(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var items = moduleObject.getSelectedMessages(element.parentNode.elementid);
	for(i in items){
		moduleObject.flagStatus(moduleObject.entryids[items[i]], olNoFlag, olNoFlagIcon);
	}
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventMailListContextMenuMessageFlagChanged(moduleObject, element, event)
{
	var flagId = element.id;
	var elementIcon = 6;
	if(flagId == "flag_icon_red") {
		eventMailListContextMenuFlagIcon(moduleObject, element, olRedFlagIcon);
	} else if(flagId == "flag_icon_blue") {
		eventMailListContextMenuFlagIcon(moduleObject, element, olBlueFlagIcon);
	} else if(flagId == "flag_icon_yellow") {
		eventMailListContextMenuFlagIcon(moduleObject, element, olYellowFlagIcon);
	} else if(flagId == "flag_icon_green") {
		eventMailListContextMenuFlagIcon(moduleObject, element, olGreenFlagIcon);
	} else if(flagId == "flag_icon_orange") {
		eventMailListContextMenuFlagIcon(moduleObject, element, olOrangeFlagIcon);
	} else if(flagId == "flag_icon_purple") {
		eventMailListContextMenuFlagIcon(moduleObject, element, olPurpleFlagIcon);
	} else if(flagId == "flag_status_complete") {
		eventMailListContextMenuFlagComplete(moduleObject, element, event);
	} else if(flagId == "flagno") {
		eventMailListContextMenuDeleteFlag(moduleObject, element, event);
	}

	eventListCheckSelectedContextMessage(moduleObject);
}

function eventMailListContextMenuFlagIcon(moduleObject, element, flagIcon)
{
	element.parentNode.style.display = "none";
	var items = moduleObject.getSelectedMessages(element.parentNode.elementid);
	for(i in items){
		moduleObject.flagStatus(moduleObject.entryids[items[i]], olFlagMarked, flagIcon);
	}
	eventListCheckSelectedContextMessage(moduleObject);
}

function eventMailListSearchBar(moduleObject, element, event)
{
	if (moduleObject.searchBarContainer.style.display=="none"){
		webclient.menu.toggleItem(element, true);
		webclient.settings.set("folders/entryid_"+moduleObject.entryid+"/searchbar/show", "true");
		moduleObject.searchBarContainer.style.display = "block";
	}else{
		webclient.menu.toggleItem(element, false);
		webclient.settings.set("folders/entryid_"+moduleObject.entryid+"/searchbar/show", "false");
		moduleObject.searchBarContainer.style.display = "none";

		// when there is a restriction active, remove it and reload maillist
		if(moduleObject.filterRestriction != false){
			this.clearSearchBar();
		}
	}
	moduleObject.resize();
}

function eventMailListSearchBarFilterClick(moduleObject, element, event)
{
	var result = false;
	event.stopPropagation();
	if (!element.hasFocus){
		element.focus();
	}else{
		result = true;
	}
	return result;
}

function eventMailListSearchBarFilterFocus(moduleObject, element, event)
{
	element.hasFocus = (event.type=="focus")
}

function eventMailListSearchBarFilterMouseMove(moduleObject, element, event)
{
	event.stopPropagation();
	return element.hasFocus;
}

function eventMailListSearchBarFilterKey(moduleObject, element, event)
{
	moduleObject.searchBarContainer.searchClearButton.disabled = false;

	// don't send key events to parent div
	// to prevent firing events registered by table view 
	event.stopPropagation();

	if (event.keyCode == 13){
		eventMailListSearchBarSearch(moduleObject, element, event);
		return false;
	}
	return undefined;
}

function eventMailListSearchBarSearch(moduleObject, element, event)
{
	var input = moduleObject.searchBarContainer.searchFilterInputBox;
	var target = moduleObject.searchBarContainer.filterTarget;
	var subfolders = moduleObject.searchBarContainer.searchSubfolders;

	if (input.value.trim() != ""){
		moduleObject.filterRestriction = input.value;
		moduleObject.filterRestrictionTarget = target.value;
		if (subfolders)
			moduleObject.searchSubfolders = subfolders.checked?"true":"false";
		else
			moduleObject.searchSubfolders = false;
		moduleObject.rowstart = 0;
		moduleObject.searchInProgress = true;
		webclient.settings.set("folders/entryid_"+moduleObject.entryid+"/searchbar/target", target.selectedIndex);

		// enable search indicator
		if(moduleObject.useSearchFolder) {
			moduleObject.enableSearchIndicator();
		}

		moduleObject.search();
		input.blur();
	}
}

function eventMailListSearchBarClear(moduleObject, element, event)
{
	moduleObject.clearSearchBar();
}

function eventMailListRules(moduleObject, element, event)
{
	webclient.openModalDialog(moduleObject, "rules", DIALOG_URL+"task=rules_modal&storeid=" + this.storeid, 500, 300);
}

/**
 * function for sent the outbox mails and receive the new mail.
 * @param object moduleObject Contains all the properties of a module object.
 * @param dom_element element The dom element's reference on which the event gets fired.
 * @param event event Event name
 */
function eventSendReceiveItems(moduleObject, element, event){
	// Save old selected messages.
	moduleObject.list(false, false, true);
}

/**
 * Keycontrok function which handles respond actions for a selected message.
 */
function eventMailListKeyCtrlRespond(moduleObject, element, event)
{
	// Check is messages are selected.
	if(moduleObject && moduleObject.selectedMessages && moduleObject.selectedMessages.length > 0) {
		var action = false;
		
		switch(event.keyCombination)
		{
			case this.keys["respond_mail"]["reply"]:
			case this.keys["respond_mail"]["replyall"]:
			case this.keys["respond_mail"]["forward"]:
				action = array_search(event.keyCombination, this.keys["respond_mail"]);
				break;
		}
		// Handle action for a selected message.
		moduleObject.replyMail(action, moduleObject.entryids[moduleObject.selectedMessages[0]]);
	}
}
/**
 * Keycontrol function which toggles reading pane.
 */
function eventMailListKeyCtrlToggleReadingPane(moduleObject, element, event)
{
	if (moduleObject && moduleObject.entryid){
		// Get reading pane status for folder
		var currentPreviewMode = webclient.settings.get("folders/entryid_"+ moduleObject.entryid +"/previewpane", "right");
		var previewMode = "right";
		
		if (currentPreviewMode == "right")
			previewMode = "bottom";
		else if (currentPreviewMode == "bottom")
			previewMode = "off";
		else if (currentPreviewMode == "off")
			previewMode = "right";
			
		webclient.settings.set("folders/entryid_"+ moduleObject.entryid +"/previewpane", previewMode);
		if(this.isinbox)
		    webclient.settings.set("folders/entryid_inbox/previewpane", previewMode);
	
		webclient.hierarchy.selectLastFolder(true); // reload this folder
	}
}
