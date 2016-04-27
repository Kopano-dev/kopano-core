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

distlistmodule.prototype = new ListModule;
distlistmodule.prototype.constructor = distlistmodule;
distlistmodule.superclass = ListModule.prototype;

// implement some functions from ItemModule
distlistmodule.prototype.setProperties = ItemModule.prototype.setProperties;
distlistmodule.prototype.setBody = ItemModule.prototype.setBody;

function distlistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

distlistmodule.prototype.init = function(id, element, title, data)
{
	distlistmodule.superclass.init.call(this, id, element, title, data, "internalid");

	this.initializeView();

	this.action = "list";
	this.xml = new XMLBuilder();

	var items = new Array();
	items.push(webclient.menu.createMenuItem("delete", _("Delete"), false, eventDistListDelete));
	this.contextmenu = items;

	dhtml.addEvent(id, document.body, "click", eventListRemoveContextMessage);

	// this.members contains list of distlist members with the following properties: 
	// address, distlisttype, entryid, icon_index, internalid, message_flags, name, type

	this.members = new Array();
	this.newInternalId = 1;
}

distlistmodule.prototype.resize = function()
{
	this.contentElement.style.height = (document.documentElement.clientHeight - 180)+"px";
	this.element.style.height = (document.documentElement.clientHeight - 180)+"px";
	this.viewController.resizeView();
}

distlistmodule.prototype.executeOnLoad = function()
{
	initdistlist();
	
	// Add keycontrol event
	webclient.inputmanager.addObject(this);
	webclient.inputmanager.bindKeyControlEvent(this, KEYS["mail"], "keyup", eventDistListKeyCtrlSave);
}

distlistmodule.prototype.list = function()
{
	if(this.storeid && this.entryid) {
		var data = new Object();
		data["store"] = this.storeid;
		data["entryid"] = this.entryid;

		// Load column/field settings from the saved settings.
		this.columns = this.loadFieldSettings();

		if(this.columns.length > 0) {
			var tablecolumns = new Array();
			for(var i = 0; i < this.columns.length; i++)
			{
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
		
		// Send request for data.
		webclient.xmlrequest.addData(this, this.action, data);
		webclient.xmlrequest.sendRequest();

		this.loadMessage();
	}else{
		// new distlist, set columns for table view
		var columns = new Array();
		columns.push({id: {value:"icon_index"}, visible: {value:1}, name: {value:_("Icon")}, order: {value: 0}});
		columns.push({id: {value:"name"}, visible: {value:1}, name: {value:_("Name")}, title: {value:_("Name")}, order: {value:1}, length: {value:"percentage"}});
		columns.push({id: {value:"address"}, visible: {value:1}, name: {value:_("E-mail")}, title: {value:_("E-mail")}, order: {value:2}, length: {value:400}});

		// Load column/field settings from the saved settings.
		var fieldData = this.loadFieldSettings();

		for(var i = 0; i<fieldData.length; i++)
		{
			for(var j = 0; j<columns.length; j++)
			{
				if(columns[j]["id"].value == fieldData[i]["id"])
				{
					columns[j]["order"].value = fieldData[i]["order"];
					columns[j]["visible"].value = (fieldData[i]["action"] == "add")?1:0;
				}
			}
		}

		var data = new Object();
		data["column"] = columns;
		if(this.members.length > 0)
		{
			data["item"] = this.members;
		}

		var dom = buildDOM(data, "list");

		/**
		 * Call execute of superclass with "list" type and
		 * pass the contents with dom object for loading contents in table.
		 */
		distlistmodule.superclass.execute.call(this, "list", dom);
	}
}

// To save field settings for distribution list.
distlistmodule.prototype.saveFieldSettings = function(data)
{
	// path for saving field settings for distribution list
	var path = "distributionlist/fields";

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
	data = webclient.settings.get(path);
}

// To load field settings for distribution list.
distlistmodule.prototype.loadFieldSettings = function()
{
	// path for loading field settings for distribution list
	path = "distributionlist/fields";

	data = webclient.settings.get(path);
	var result = new Array();
	for(var i in data){
		result.push(data[i]);
	}
	return result;
}

distlistmodule.prototype.execute = function(type, action)
{
	// reset previous member data
	this.members = new Array();

	// get item props
	var props = action.getElementsByTagName("props");
	if (props && props[0]){
		this.setProperties(props[0]);
		this.setBody(props[0]);
	}

	// get members
	var items = action.getElementsByTagName("item");
	for(var i = 0; i < items.length; i++) {
		var item = collapseXML(items[i]);

		// Add into the xml document an internalid field, as if it came from 
		// the server.
		var child = action.ownerDocument.createElement("internalid");
		this.newInternalId++;
		child.appendChild(action.ownerDocument.createTextNode(""+this.newInternalId));
		items[i].appendChild(child);
		
		item.internalid = {value: this.newInternalId};
		this.members.push(item);
	}
	// display data
	distlistmodule.superclass.execute.call(this, type,action);
}

distlistmodule.prototype.save = function()
{
	var data = new Object();
	if(this.storeid) {
		data["store"] = this.storeid;
	}
	
	if(this.parententryid) {
		data["parententryid"] = this.parententryid;
	}

	if(this.entryid) {
		data["entryid"] = this.entryid;
	}
	
	data["props"] = getPropsFromDialog();
	
	var members = new Array;
	for(var i=0; i<this.members.length; i++){
		var member = new Object;
		
		for(var field in this.members[i]){
			member[field] = this.members[i][field].value;
		}
		members.push(member);
	}
	// dont send members in request if it is empty.
	if(members && members.length >=1 )
		data["members"] = {item: members};
	
	if(parentWebclient) {
		parentWebclient.xmlrequest.addData(this, "save", data, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest(true);
	} else {
		webclient.xmlrequest.addData(this, "save", data);
		webclient.xmlrequest.sendRequest();
	}
}

distlistmodule.prototype.removeItems = function(items) 
{
	if (typeof items == "undefined"){ 
		items = this.getSelectedIDs(); 
	}

	// if no message is selected then don't execute this function
	if(items == false) {
		return false;
	}

	var delreq = new Object;
	delreq.internalid = new Array;
	for(var i=0;i<items.length;i++) {
		// Mark member as deleted
		var mId = this.getMemberIdByInternalId(items[i]);
		this.members[mId].deleted = {value: true};
		
		// Delete member from view
		var delobj = {value: items[i]};
		delreq.internalid.push(delobj);
	}
	
	var dom = buildDOM(delreq, "delete");
	distlistmodule.superclass.execute.call(this, "delete", dom);
}

distlistmodule.prototype.getMemberIdByInternalId = function(internalid)
{
	internalid = parseInt(internalid,10);

	for(var i=0;i<this.members.length;i++){
		if (this.members[i] && this.members[i].internalid && parseInt(this.members[i].internalid.value,10) == internalid){
			return i;
		}
	}
	return 0;
}

distlistmodule.prototype.checkMissing = function()
{
	for(var i=0;i<this.members.length;i++){
		if (this.members[i].missing && this.members[i].missing.value == 1){
			if (confirm(_("Could not find Contact '%s'. It may have been deleted or moved from its original location. Would you like to remove it from this list?").sprintf(this.members[i].name.value))){
				var items = new Array();
				items.push(this.members[i].internalid.value);
				this.removeItems(items);
			}else{
				// replace as oneoff entry
				var member = this.members[i];

				delete member.missing;
				delete member.message_class;
				member.entryid.value = "oneoff_"+member.internalid.value;
				member.distlisttype = {value: "ONEOFF"};
			}
		}
	}
}

distlistmodule.prototype.addItem = function(item)
{
	// item: (addrtype), display_name, email_address, entryid, (icon_index), (message_class), (dl_name), (value) 
	var member = new Object;

	// if user is GAB user then use smtp address
	if(item.addrtype == "ZARAFA" || item.objecttype == MAPI_MAILUSER)
		member.address = {value: item.smtp_address};
	else
		member.address = {value: item.email_address};
	member.message_flags = {value: 1};

	if (item.entryid){
		member.entryid = {value: item.entryid, attributes: {type: "binary"} };
		if (item.objecttype && item.objecttype == MAPI_DISTLIST){
			// ab group
			member.message_flags = {value: 0};
			member.distlisttype = {value: "DL_DIST_AB"};
		} else if(item.objecttype && item.objecttype == MAPI_MESSAGE) {
			if (item.addrtype == "ZARAFA") {
				// real distlist
				member.distlisttype = {value: "DL_DIST"};
			} else {
				// user in contact folders
				member.distlisttype = {value: "DL_USER"};
				if (item.email_address_number && item.email_address_number>1){
					member.distlisttype.value += item.email_address_number;
				}
			}
		}else{
			// ab user
			member.distlisttype = {value: "DL_USER_AB"};
		}
	}else{
		member.entryid = {value: "oneoff_"+this.newInternalId};
		member.distlisttype = {value: "ONEOFF"};
	}
	if (item.addrtype == "ZARAFA") {
		member.icon_index = {value: 514};
	}else{
		if (item.icon_index)
			member.icon_index = {value: item.icon_index};
		else
			member.icon_index = {value: 512};
	}

	member.name = {value: item.display_name};
	member.type = {value: item.addrtype};

	if(typeof item["internalId"] == "undefined") {
		this.newInternalId++;
		member.internalid = {value: this.newInternalId};
		this.members.push(member);
	} else {
		member.internalid = {value: item.internalId};
		var memberId = this.getMemberIdByInternalId(item.internalId);
		this.members[memberId] = member;
	}

	var dom = buildDOM(member, "item");

	distlistmodule.superclass.execute.call(this, "item", dom);
}

/** 
 * Opens the entry based on the supplied internalId which is uniqueid for this module. The messageClass is only needed when calling the 
 * openDetails method.
 * @param internalId String uniqueid for the entry that has to be opened
 * @param messageClass String Message class of the entry based on the CSS classname 
 */
distlistmodule.prototype.onOpenItem = function(internalId, messageClass) {
	this.openDetails(internalId, messageClass);
	return false;
}

/** 
 * Opens the entry based on the supplied internalId i.e. uniqueid. The messageClass is only needed when the object 
 * type of the entry is a MAPI_MESSAGE. In that case the superlclass' onOpenItem is called to handle
 * according to the default behavior. The Message class is usually based on the CSS class name and
 * will be used to determine the type of dialog that has to be opened.
 * @param internalId internal String Entryid of the entry distlistmodule has its own uniqueId
 * that is internalId, which denotes to the specific item in the itemProps data.
 * @param messageClass String Message class of the entry based on the CSS classname 
 */
distlistmodule.prototype.openDetails = function(internalId, messageClass) {
	var memberId = this.getMemberIdByInternalId(internalId);
	var distlisttype = this.members[memberId].distlisttype.value;

	if (this.members[memberId].missing && this.members[memberId].missing.value == 1){
		if (confirm(_("Could not find Contact '%s'. It may have been deleted or moved from its original location. Would you like to remove it from this list?").sprintf(this.members[memberId].name.value))){
			// Remove missing contact from the list.
			var items = new Array();
			items.push(this.members[memberId].internalid.value);
			this.removeItems(items);
			return;
		}else{
			// Replace missing entry as oneoff entry.
			var member = this.members[memberId];

			delete member.missing;
			delete member.message_class;
			member.entryid.value = "oneoff_"+member.internalid.value;
			member.distlisttype.value = "ONEOFF";
			distlisttype = "ONEOFF";
		}
	}

	var entryid = this.members[memberId].entryid.value;

	switch(distlisttype){
		case "DL_USER_AB":
			var uri = DIALOG_URL+"task=gab_detail_mailuser_standard&storeid=" + this.storeid + "&entryid=" + this.members[memberId].entryid.value;
			webclient.openWindow(this, "gab_detail", uri, FIXEDSETTINGS.ABITEM_DIALOG_MAILUSER_WIDTH, FIXEDSETTINGS.ABITEM_DIALOG_MAILUSER_HEIGHT);
			break;
		case "DL_DIST_AB":
			var uri = DIALOG_URL+"task=gab_detail_distlist_standard&storeid=" + this.storeid + "&entryid=" + this.members[memberId].entryid.value;
			webclient.openWindow(this, "gab_detail", uri, FIXEDSETTINGS.ABITEM_DIALOG_DISTLIST_WIDTH, FIXEDSETTINGS.ABITEM_DIALOG_DISTLIST_HEIGHT);
			break;
		case "DL_DIST":
		case "DL_USER":
		case "DL_USER2":
		case "DL_USER3":
			// If it is a normal item then open dialog and show item there.
			this.openItem(internalId, messageClass);
			break;
		case "ONEOFF":
			// Open emailaddress dialog for oneoff entryids
			webclient.openModalDialog(-1, 'addemail', DIALOG_URL+'task=emailaddress_modal&internalid='+internalId, 300,150, distlist_addNewCallback);
			break;
	}
}

/**
* Function to open MAPI_MESSAGE in dialog
*
* @param internalId String uniqueid for the entry that has to be opened
* @param message_type is the type of message "distlist" or "contact"
* (usally a part of the message_class)
*/
distlistmodule.prototype.openItem = function(internalId, message_type)
{
	var memberId = this.getMemberIdByInternalId(internalId);
	var entryid = this.members[memberId].entryid.value;

	var uri = DIALOG_URL+"task=" + message_type + "_standard&storeid=" + this.storeid + "&parententryid=" + this.entryid + "&entryid=" + entryid;
	webclient.openWindow(this, message_type, uri);
}

function eventDistListDelete(moduleObject, element, event)
{
	if(dhtml.getElementsByClassName("rowcontextmenu")[0]) {
		// pass the element which has been selected by right clicking
		// and has focus
		var items = new Array();
		items.push(this.entryids[dhtml.getElementsByClassName("rowcontextmenu")[0].id]);
	}

	moduleObject.removeItems(items);

	dhtml.executeEvent(document.body, "click");
}

function eventListRemoveContextMessage(moduleObject, element, event)
{
	var contextmenu = dhtml.getElementById("contextmenu");
	if(contextmenu) {
		dhtml.showHideElement(contextmenu, contextmenu.Left, contextmenu.Top, true);
	}
}
