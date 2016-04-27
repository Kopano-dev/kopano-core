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

addressbooklistmodule.prototype = new ListModule;
addressbooklistmodule.prototype.constructor = addressbooklistmodule;
addressbooklistmodule.superclass = ListModule.prototype;

function addressbooklistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

addressbooklistmodule.prototype.init = function(id, element, title, data)
{
	addressbooklistmodule.superclass.init.call(this, id, element, title, data);

	this.initializeView();

	this.action = "list";

	this.enableVariableColumns = false;

	// disable message selection when loading addressbook
	this.preserveSelectionOnReload = false;

	// We don't want rowcolumn/quick_edit events since it makes it slower
    delete this.events["rowcolumn"];
    delete this.events["insertcolumn"];
    delete this.events["insertrow"];
    delete this.events["menu"];

	var items = new Array();
	if (data["showsendmail"] == "true") {
		// If addressbook is opened from the main toolbar then show "Email Message" option as contextmenu item.
		items.push(webclient.menu.createMenuItem("createmail", _("Email Message"), false, eventABSendMailTo));
	} else {
		// If addressbook is opened from the other dialoge then show "Select" option as contextmenu item.
		items.push(webclient.menu.createMenuItem("select", _("Select"), false, eventListContextMenuOpenMessage));
	}
	items.push(webclient.menu.createMenuItem("details", _("Properties"), false, eventListContextMenuOpenProperties));
	this.contextmenu = items;
}

addressbooklistmodule.prototype.retrieveHierarchy = function()
{
	var data = new Object();
	if (this.source=="all"||this.source=="gab"){
		data["gab"] = "all";
	}
	if (this.source=="all"||this.source=="contacts"){
		data["contacts"] = {stores:{store:new Array(),folder:new Array()}}
		for(var i=0;i<parentWebclient.hierarchy.stores.length;i++){
			var store = parentWebclient.hierarchy.stores[i];
			switch(store.foldertype){
				case "contact":
					data["contacts"]["stores"]["folder"].push(store.id);
					break;
				case "all":
					data["contacts"]["stores"]["store"].push(store.id);
					break;
			}
		}
	}

	webclient.xmlrequest.addData(this, "hierarchy",data);
}

addressbooklistmodule.prototype.execute = function(type, action)
{
	webclient.menu.showMenu();
	
	switch(type)
	{
		case "list":
			this.messageList(action);
			// Content in addressbook is loaded and table widget is added so call resize function.
			if (window.onresize)
				window.onresize();
			// empty old selected messages if there was any.
			if(this.selectedMessages)
				this.selectedMessages = new Array();
			break;
		case "hierarchy":
			this.updateHierarchy(action);
			break;
		case "item":
			this.item(action);
			break;
	}
}

addressbooklistmodule.prototype.list = function(action, noLoader, restriction)
{
	if(this.storeid) {
		var data = new Object();
		data["store"] = this.storeid;
		data["entryid"] = this.entryid;

		var contactfolder = dhtml.getElementById("addressbookfolders");
		if(contactfolder && contactfolder.value){
			data["store"] = contactfolder.options[ contactfolder.selectedIndex ].storeid;
		}

		this.sort = this.loadSortSettings();
		if(this.sort) {
			data["sort"] = new Object();
			data["sort"]["column"] = this.sort;
		}

		// Set restriction for search_string/pagination_character.
		if(restriction) {
			data["restriction"] = restriction;
		}

		// hiding of users/groups is only for gab & addresslist
		if(this.action == "globaladdressbook") {
			if(typeof this.hide_groups != "undefined" && isArray(this.hide_groups) && this.hide_groups.length > 0) {
				data["hide_groups"] = this.hide_groups;
			}

			if(typeof this.hide_users != "undefined" && isArray(this.hide_users) && this.hide_users.length > 0) {
				data["hide_users"] = this.hide_users;
			}

			if(typeof this.hide_companies != "undefined") {
				data["hide_companies"] = this.hide_companies;
			}
		} else if(typeof this.groups != "undefined") {
			data["groups"] = this.groups;
		}

		webclient.xmlrequest.addData(this, this.action, data);
		webclient.xmlrequest.sendRequest();

		if (!noLoader)
			this.viewController.loadMessage();
	}
}


/**
* This function will return the index of a property column in the table view
*
* TODO: move this to the table view, but for now I only need it here
*/
addressbooklistmodule.prototype.getPropertyIndex = function(fieldname)
{
	var result = -1, i = 0;
	while(result<0 && i<module.properties.length){
		var property = module.properties[i];
		if (property.id == fieldname){
			result = parseInt(property.order,10);
		}
		i++;
	}
	if (result == -1){
		result = false;
	}
	return result;
}

addressbooklistmodule.prototype.saveSortSettings = function (data)
{
	var path = "addressbook/sort";
	webclient.settings.deleteSetting(path);
	webclient.settings.setArray(path,data);
}

addressbooklistmodule.prototype.loadSortSettings = function ()
{
	var path = "addressbook/sort";
	
	var column = new Object();
	column["attributes"] = new Object();
	data = webclient.settings.get(path);
	for(var i in data){
		if(i != "prototype"){//workarround		
			column["attributes"]["direction"] = data[i];
			column["_content"] = i;
		}
	}
	if(!column["_content"]){
		var result = false;
	}else{
		var result = new Array(column);
	}
	return result;
}

addressbooklistmodule.prototype.updateHierarchy = function(action)
{
	var items = action.getElementsByTagName("folder");
	if (!items)
		return;

	var defaultEntryid = webclient.settings.get("addressbook/default/entryid","");
	var folderList = dhtml.getElementById("addressbookfolders");
	for(var i=0;i<items.length;i++){
		var folder = dom2array(items[i]);

		var name = folder.display_name;
		if (folder["parent_entryid"] && folder["parent_entryid"]!=folder["entryid"]){
			name = NBSP+NBSP+name;	
		}
		
		var newOption = dhtml.addElement(folderList, "option", null, null, name);
		newOption.value = folder["entryid"];
		if (folder["entryid"]==defaultEntryid){
			newOption.selected = true;
		}
		newOption.folderType = folder["type"];
		if (typeof(folder["storeid"])!="undefined")
			newOption.storeid = folder["storeid"];

	}
}

/** 
 * Opens the entry based on the supplied entryid. The messageClass is only needed when calling the 
 * openDetails method.
 * @param entryid String Entryid of the entry that has to be opened
 * @param messageClass String Message class of the entry based on the CSS classname 
 */
addressbooklistmodule.prototype.onOpenItem = function(entryid, messageClass) {
	// Only open the details dialog when this has been set for this dialog
	if(this.detailsonopen){
		this.openDetails(entryid, messageClass);
		return false;
	}
}

/** 
 * Opens the entry based on the supplied entryid. The messageClass is only needed when the object 
 * type of the entry is a MAPI_MESSAGE. In that case the superlclass' onOpenItem is called to handle
 * according to the default behavior. The Message class is usually based on the CSS class name and
 * will be used to determine the type of dialog that has to be opened.
 * @param entryid String Entryid of the entry that has to be opened
 * @param messageClass String Message class of the entry based on the CSS classname 
 */
addressbooklistmodule.prototype.openDetails = function(entryid, messageClass) {
	var objecttype = parseInt(this.itemProps[ entryid ].objecttype, 10);

	switch(objecttype){
		case MAPI_MAILUSER:
			var uri = DIALOG_URL+"task=gab_detail_mailuser_standard&storeid=" + this.storeid + "&entryid=" + entryid;
			webclient.openWindow(this, "gab_detail", uri, FIXEDSETTINGS.ABITEM_DIALOG_MAILUSER_WIDTH, FIXEDSETTINGS.ABITEM_DIALOG_MAILUSER_HEIGHT);
			break;
		case MAPI_DISTLIST:
		case MAPI_ABCONT:
			var uri = DIALOG_URL+"task=gab_detail_distlist_standard&storeid=" + this.storeid + "&entryid=" + entryid;
			webclient.openWindow(this, "gab_detail", uri, FIXEDSETTINGS.ABITEM_DIALOG_DISTLIST_WIDTH, FIXEDSETTINGS.ABITEM_DIALOG_DISTLIST_HEIGHT);
			break;
		case MAPI_MESSAGE:
			// If it is a normal item open the item using the superclass' method
			addressbooklistmodule.superclass.onOpenItem.apply(this, arguments);
			break;
	}
}

/**
 * Called when the user wants to open the properties dialog of an entry in the GAB.
 * @param moduleObject Object Module Object
 * @param element HTMLElement Clicked element
 * @param event Object Event object
 */
function eventListContextMenuOpenProperties(moduleObject, element, event)
{
	// Hide the menu
	element.parentNode.style.display = "none";

	// Get the id of the element the menu is opened for. This is not necessarily the selected item
	var elementIdParentContextMenu = element.parentNode.elementid;

	// Get the message_class through the CSS classname
	// TODO refactor this code, as this is used everywhere in the WA.
	var parentElement = dhtml.getElementById(elementIdParentContextMenu);
	var messageClass = false;
	if(parentElement){
		var classNames = parentElement.className.split(" ");
		for(var index in classNames){
			if(classNames[index].indexOf("ipm_") >= 0 && messageClass==false) {
				messageClass = classNames[index].substring(classNames[index].indexOf("_") + 1);
			}
		}
	}

	// Open the details of the item
	moduleObject.openDetails(moduleObject.entryids[elementIdParentContextMenu], messageClass);
}