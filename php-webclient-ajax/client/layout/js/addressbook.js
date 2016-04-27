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

function onOpenItem(entryid)
{
	if(fields.length < 1) {
		// In anonymous-select, double-clicking the item just
		// selects the single item, and closes the window
		addressBookSubmit();
	} else {
		// Otherwise, only add the selected contact(s)
		addSelectedContacts();
	}
}

/**
* This function will add the selected contacts to the view
*/
function addSelectedContacts(destId)
{
	// when no destination is given, we use the default for that view
	if (!destId){
		destId = module.dest;
	}

	var destElem = dhtml.getElementById(destId);

	if(destElem) {
		var selectedMessages = module.getSelectedMessages();
		var selectedEntryid = null;
		
		if(selectedMessages.length >= 1) {
			for(var i=0;i<selectedMessages.length;i++){
				var element = dhtml.getElementById(selectedMessages[i]);
				
				var addName = false;
				selectedEntryid = module.entryids[selectedMessages[i]];
				var props = module.itemProps[selectedEntryid];
				
				// TODO:	This whole switch structure can be removed when we alter all code that is calling the addressbook,
				//			because all properties are returned in callbackdata. Only thing to check is when we need to expand dist lists or not.
				//			For now we assume that when type=fullemail we want to expand. Else we just give the item.
				switch(module.type){
					case "username":
					case "username_single":
						if (props["fileas"] && props["fileas"].trim()!=""){
							addName = props["fileas"].trim();
						}
						break;
					case "displayname":
					case "displayname_single":
						if (props["display_name"] && props["display_name"].trim()!=""){
							addName = props["display_name"].trim();
						}
						break;
					case "all_single":
					case "email_single":
						if (props["email_address"] && props["email_address"].trim()!=""){
							addName = props["email_address"].trim();
						}
						if (props["smtp_address"] && props["smtp_address"].trim()!=""){
							addName = props["smtp_address"].trim();
						}
						break;
					case "fullemail":
					default:
						if (props["email_address"] && props["email_address"].trim()!=""){
							switch (parseInt(props["objecttype"], 10)){
								case MAPI_MAILUSER:		// users in GAB
									addName = props["email_address"];
									if(props["smtp_address"]) {
    									addName = props["smtp_address"];
									}
									if (props["display_name"] && props["display_name"].trim()!="") {
										addName = props["display_name"] + " <"+addName+">";
									}
									break;
								case MAPI_DISTLIST:		// groups in GAB
									addName = '[' + props["email_address"] + "]";
									break;
								case MAPI_MESSAGE:		// users/groups in contact folders
									var messageClassParts = props["message_class"].split(".");
									if(messageClassParts[1].toLowerCase() == "contact") {
										addName = props["email_address"];
										if(props["smtp_address"]) {
											addName = props["smtp_address"];
										}
										if (props["display_name"] && props["display_name"].trim()!="") {
											addName = props["display_name"] + " <"+addName+">";
										}
									} else if(messageClassParts[1].toLowerCase() == "distlist") {
										addName = mergeABItems(props["members"]);
									}
									break;
							}
						}
						break;
				}

				if (addName!=false){
					// fix entryid for contacts with multiple addressess
					if (props.entryid && props.entryid.indexOf("_")>0){
						props.entryid = props.entryid.substr(0,props.entryid.indexOf("_"));
					}

					if (module.type.indexOf("_single") > 0){
						destElem.value = addName;
						destElem.props = props;
					}else{
						// add email separator
						if (destElem.value.length > 0 && destElem.value.trim().substr(-1,1)!=";"){
							destElem.value  += "; ";
						}
						destElem.value += addName;
						if (typeof destElem.props == "undefined"){
							destElem.props = new Object;
						}
						destElem.props["multiple"] = true;
						destElem.props[props.entryid] = props;
						destElem.props[props.entryid].value = addName;
					}
					destElem.value = destElem.value.trim();
				}
			}
		} else if(destElem.id != "anonymous") {
			alert(_("Please select a contact"));
		}
	}
}

function addressBookSearchKeyInput(moduleObj, element, event)
{
	var keycode = event.keyCode;

	// skip the search functionality on following keys
	switch(keycode){
		case 13:
			searchAddressBook();
			return;
		case 16: // escape shift key
		case 17: // escape ctrl key
		case 18: // escape alt key
		case 37: // escape left arrow key
		case 38: // escape up arrow key
		case 39: // escape right arrow key
		case 40: // escape down arrow key
				return;
	}

	// fix loss-of-scope in setTimeout function
	var moduleObject = moduleObj;
	var textBoxElement = element;

	if(searchKeyInputTimer) window.clearTimeout(searchKeyInputTimer);
	searchKeyInputTimer = window.setTimeout(
							function(){
								addressBookPerformQuickSearch(moduleObject, textBoxElement, keycode);
							}, 500
						);
}

function addressBookPerformQuickSearch(moduleObj, element, keycode)
{
	var value = dhtml.getElementById("name").value.replace('^\\s*','');
	//if search string is empty
	if(value.trim() == ""){
		// scroll the table to top
		var divElem = dhtml.getElementById("divelement");
		if(divElem) divElem.scrollTop = 0;
		// get the selected row and deselect that
		eventDeselectMessages(moduleObj, element, window.event);
		return;
	}

	// get the sorting option on which we have to search
	var searchon = false;
	if(typeof moduleObj.sort[0] != "undefined" && typeof moduleObj.sort[0]["_content"] != "undefined") {
		searchon = moduleObj.sort[0]["_content"];
	}

	// assign the full AB for searching.
	if((typeof moduleObj.searchList == "undefined") || (moduleObj.searchList == null) || (value.trim() == "") || (keycode == 8)){
		moduleObj.searchList = moduleObj.itemProps;
	}
	
	moduleObj.searchList = moduleObj.itemProps;
	// escape some special characters from value.
	value = value.replace(/[\\|\*|\+|\(|\)|?]/g, "");
	var pattern = new RegExp("^"+value,"i");
	var firstChar = new RegExp("^"+value.substring(0,1),"i");
	var itemProperties = moduleObj.searchList;
	moduleObj.searchList = null;
	var arr = [];
	for(key in itemProperties){
		var item = (typeof itemProperties[key][searchon] != "undefined") ? itemProperties[key][searchon] : itemProperties[key]["display_name"]; 
		if(pattern.test(item)){
			if(!moduleObj.searchList) moduleObj.searchList = new Object();
			moduleObj.searchList[key] = new Object();
			moduleObj.searchList[key][searchon] = item;
			arr.push(item);
		}
		if(arr.length >0 && !firstChar.test(item)){
			break;
		}
	}
	if(arr.length <=0){
		for(key in itemProperties){
			var item = (typeof itemProperties[key][searchon] != "undefined") ? itemProperties[key][searchon] : itemProperties[key]["display_name"]; 
			if(value < item){
				if(!moduleObj.searchList) moduleObj.searchList = new Object();
				moduleObj.searchList[key] = new Object();
				moduleObj.searchList[key][searchon] = item;
				arr.push(item);
				break;
			}
		}
	}
	// select the searched contact
	var allDivs = dhtml.getElementsByClassNameInElement(dhtml.getElementById("items"),"rowcolumntext","div");
	var allDivsLen = allDivs.length;
	for(var i =0;i<allDivsLen;i++){
		if(html_entity_decode(allDivs[i].innerHTML) == arr[0]){
			var selectElem = allDivs[i].parentNode.parentNode;
			var selElemTop = dhtml.getElementTop(selectElem);
			dhtml.getElementById("divelement").scrollTop = parseInt(selectElem.id, 10) * 18;
			dhtml.executeEvent(selectElem, "mousedown");
			break;
		}
	}
}

function addressBookSubmit()
{
	var result;

	if(fields.length > 0) {
		result = new Object();
		for(var i = 0; i < fields.length; i++) {
			var props = dhtml.getElementById(fields[i]).props;
			if (typeof props != "undefined"){
				result[fields[i]] = props
			}else{
				result[fields[i]] = new Object;
			}
			result[fields[i]].value = dhtml.getElementById(fields[i]).value;
		}
	} else {
		addSelectedContacts();

		// No fields given, return a single entry with entryid
		result = dhtml.getElementById("anonymous").props;
		if(result) {
			result.value = dhtml.getElementById("anonymous").value;
		} else {
			return false;
		}
	}

	// If result callback  function is defined then call the function.
	if(typeof(window.resultCallBack) == "function") {
		window.resultCallBack(result, window.callBackData);
		window.close();
	}
}

function mergeABItems(items)
{
	var result = "";
	if (items["member"]){
		items = items["member"];
	}

	if (!items.length){
		items = new Array(items);
	}

	for(var i=0;i<items.length;i++){
		if(parseInt(items[i].objecttype, 10) == MAPI_DISTLIST) {
			if(items[i].email_address) {
				result += "[" + items[i].email_address +  "]; ";
			}
		} else {
			if (items[i].email_address){
				var email = items[i].email_address;
				if(items[i].smtp_address)
					email = items[i].smtp_address;

				if (items[i].display_name){
					email = items[i].display_name + " <" + email + ">";
				}else if (items[i].fileas){
					email = items[i].fileas + " <" + email + ">";
				}

				result += email +"; ";
			}
		}
	}
	return result;
}


function changeAddressBookFolder(folders)
{
	var folder;
	if (typeof(folders)=="undefined" || folders.options.length==0){
		folder = new Object;
		folder.folderType = webclient.settings.get("addressbook/default/foldertype","gab");
		folder.value = webclient.settings.get("addressbook/default/entryid","");
		folder.storeid = webclient.settings.get("addressbook/default/storeid","");
	}else{
		folder = folders.options[folders.selectedIndex];
	}

	var sortSaveList = new Object();
	
	// Get sorting data from the settings.
	var sortData = module.loadSortSettings();
	/**
	 * If folder is changed to contact from GAB then change sorting column on email_address
	 * If folder is changed to GAB from contact then change sorting column on smtp_address
	 */
	if(sortData)
	{
		if(folder.folderType == "contacts" && (sortData[0]["_content"] == "smtp_address" || sortData[0]["_content"] == "email_address")) {
			sortSaveList["email_address"] = sortData[0]["attributes"]["direction"];
			module.saveSortSettings(sortSaveList);
		} else if(sortData[0]["_content"] == "email_address") {
			sortSaveList["smtp_address"] = sortData[0]["attributes"]["direction"];
			module.saveSortSettings(sortSaveList);
		}
	}

	if (module.source=="gab" && folder.folderType && folder.folderType!="gab"){
		// when default ab folder is not from GAB and we request only from GAB: force to use GAB
		module.entryid = "";
		module.action = "globaladdressbook";
		module.list(module.action, false, getPaginationRestriction());
	}else if(!folder.folderType || folder.folderType=="gab") {
		module.entryid = folder.value;
		//module.gabentryid = folder.value;
		module.action = "globaladdressbook";
		module.list(module.action, false, getPaginationRestriction());
	} else {
		module.storeid = folder.storeid;
		module.entryid = folder.value;
		module.action = "contacts";
		module.list(module.action, false, getPaginationRestriction());
	}

	// change selection according to selected character for that folder
	changePaginationSelection(module.selectedCharacter, getABPaginationCharacter()); 
}

function searchAddressBook()
{
	// remove selection from pagination bar
	var character = dhtml.getElementById("character_" + module.selectedCharacter);
	if(character)
		dhtml.removeClassName(character, "characterselect");

	var name = dhtml.getElementById("name");
	
	// Create search restriction.
	if(name.value.trim() != "") {
		var searchData = new Object();
		searchData["searchstring"] = name.value;
		module.list(false, false, searchData);
	} else {
		module.list(false, false, false);
	}
}

function getAddressBookRecipients(recipients)
{
	var parentwindow = window.opener;
	if(!parentwindow) {
		if(window.dialogArguments) {
			parentwindow = window.dialogArguments.parentWindow;
		}
	}

	switch (typeof recipients){
		case "string":
			recipients = new Array(recipients);
			break;
		case "undefined":
			recipients = new Array("to", "cc", "bcc");
			break;
	}
	
	for(var i = 0; i < recipients.length; i++)
	{
		var type = parentwindow.dhtml.getElementById(recipients[i]);
		if(type) {	
			dhtml.getElementById(recipients[i]).value = type.value;
		}
	}
}

function setAddressBookRecipients(recipients)
{
	var parentwindow = window.opener;
	if(!parentwindow) {
		if(window.dialogArguments) {
			parentwindow = window.dialogArguments.parentWindow;
		}
	}
	
	switch (typeof recipients){
		case "string":
			recipients = new Array(recipients);
			break;
		case "undefined":
			recipients = new Array("to", "cc", "bcc");
			break;
	}

	for(var i = 0; i < recipients.length; i++)
	{
		var type = parentwindow.dhtml.getElementById(recipients[i]);
		if(type) {
			var el = dhtml.getElementById(recipients[i]);
			type.value = el.value;
			if (el.entryid){
				type.entryid = el.entryid;
			}
		}	
	}
	
	// call addressbook handler of calling module if exists
	if (parentwindow && parentwindow.module && parentwindow.module.abHandler){
		parentwindow.setTimeout(parentwindow.module.abHandler, 10);
	}
}

/**
* This function will add alfabetbar in addressbook dialog.
* and it will register all the events for alfabetbar
*/
function initPagination()
{
	alfabetElement = dhtml.getElementById("alfabet-bar");
	alfabetElement.style.display = "block";
	alfabetElement.style.visibility = "visible";

	// Get selected pagiantion character from settings.
	module.selectedCharacter = getABPaginationCharacter();

	var alfabet = new Array("...", "123", "a", "b", "c", "d", "e", "f", 
							"g", "h", "i", "j", "k", "l", "m", "n", "o", 
							"p", "q", "r", "s", "t", "u", "v", "w", "x", 
							"y", "z");

	// Register events for alfabetitems.
	alfabetItemEvents = new Object();
	alfabetItemEvents["mouseover"] = eventAddressbookListMouseOverAlfabetItem;
	alfabetItemEvents["mouseout"] = eventAddressbookListMouseOutAlfabetItem;
	alfabetItemEvents["click"] = eventAddressbookListClickAlfabetItem;
	
	for(var i = 0; i < alfabet.length; i++)
	{
		var className = "character";
		if(alfabet[i] == module.selectedCharacter)
			className += " characterselect";

		var character = dhtml.addElement(alfabetElement, "div", className, "character_" + alfabet[i], alfabet[i]);
		dhtml.setEvents(module.id, character, alfabetItemEvents);
	}
}

/**
* This function will return pagination character from settings.
* If it is not saved in setting then it will return its default value "a".
*/
function getABPaginationCharacter()
{
	if(typeof module.action == "undefined" || module.action == "globaladdressbook" || module.action == "list") {
		var selectedCharacter = webclient.settings.get("addressbook/default/pagination_character", "a");
	} else if(typeof module.entryid != "undefined") {
		var selectedCharacter = webclient.settings.get("folder/entryid_" + module.entryid + "/pagination_character", "a");
	}

	return selectedCharacter;
}

/**
* This function will set pagination character in settings.
* @param character string pagination character which is to be set in the settings.
*/
function setABPaginationCharacter(character)
{
	if(typeof character == "string") {
		if(typeof module.action == "undefined" || module.action == "globaladdressbook" || module.action == "list") {
			webclient.settings.set("addressbook/default/pagination_character", character);
		} else if(typeof module.entryid != "undefined") {
			webclient.settings.set("folder/entryid_" + module.entryid + "/pagination_character", character);
		}
	}
}

/**
* This function will return pagination restriction which is used in XML request.
*/
function getPaginationRestriction()
{
	if(module.enableGABPagination == false) 
		return false;

	this.selectedCharacter = getABPaginationCharacter();

	var pagRestriction = new Object();
	if(this.selectedCharacter)
		pagRestriction["pagination_character"] = this.selectedCharacter;
	else
		return false;

	return pagRestriction;
}

function changePaginationSelection(oldSelectedChar, newSelectedChar) {
	// remove selection from old element
	var character = dhtml.getElementById("character_" + oldSelectedChar);
	if(character) {
		dhtml.removeClassName(character, "characterselect");
	}

	// overwrite value of selected character
	module.selectedCharacter = newSelectedChar;

	// add selection to new element
	var character = dhtml.getElementById("character_" + module.selectedCharacter);
	if(character) {
		dhtml.addClassName(character, "characterselect");
	}
}

/**
* This function will highlight the character in alfabetbar on mouseover.
*/
function eventAddressbookListMouseOverAlfabetItem(moduleObject, element, event)
{
	dhtml.addClassName(element, "characterover");
}

/**
* This function will remove highlight from the character in alfabetbar on mouseout.
*/
function eventAddressbookListMouseOutAlfabetItem(moduleObject, element, event)
{
	if(element) {
		dhtml.removeClassName(element, "characterover");
	}
}

/**
* This function will send the request for data when user selects any character from alfabetbar.
*/
function eventAddressbookListClickAlfabetItem(moduleObject, element, event)
{
	dhtml.getElementById("name").value = "";

	// change selection
	changePaginationSelection(moduleObject.selectedCharacter, element.id.substring(element.id.indexOf("_") + 1));

	setABPaginationCharacter(moduleObject.selectedCharacter);

	if(typeof(moduleObject.selectedCharacter) != "undefined" && moduleObject.selectedCharacter != ""){
		module.list(false, false, getPaginationRestriction());
	} else {
		module.list();
	}
}
/**
 * This function will open new mail dialog with selected contacts in TO fields.
 * @param object moduleObject Contains all the properties of a module object.
 * @param HTML_dom_element element The dom element's reference on which the event is fired.
 * @param eventObject event Event name and event information
 */
function eventABSendMailTo(moduleObject, element, event)
{
	// Clear the text-field.
	dhtml.getElementById("anonymous").value = "";
	addSelectedContacts("anonymous");

	if(element)
		element.parentNode.style.display = "none";
	webclient.openWindow(this, "createmail", DIALOG_URL+"task=createmail_standard&to="+encodeURIComponent(dhtml.getElementById("anonymous").value));
}
