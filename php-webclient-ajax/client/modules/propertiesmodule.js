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
* Folder properties module
*/

propertiesmodule.prototype = new Module;
propertiesmodule.prototype.constructor = propertiesmodule;
propertiesmodule.superclass = Module.prototype;

function propertiesmodule(id, element)
{
	if(arguments.length > 0) {
		this.init(id, element);
	}
}

propertiesmodule.prototype.init = function(id, element, title, data)
{
	this.permissionChanged = false;

	if(data) {
		for(var property in data)
		{
			this[property] = data[property];
		}
	}
	
	propertiesmodule.superclass.init.call(this, id, element, title, data);
}

propertiesmodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "folderprops":
			this.setFolderDialogProps(action);
			this.initPermissions(action.getElementsByTagName("permissions")[0]);
			break;
		case "error":
			var error = action.getElementsByTagName("error")[0];
			var hresult = dhtml.getTextNode(error.getElementsByTagName("hresult")[0],"");
			var errorString = dhtml.getTextNode(error.getElementsByTagName("message")[0],"");

			if(errorString.length > 0){
				alert(errorString);
			}
			break;
		case "saved":
			window.close();
			break;
	}
}

/**
* make a XML request for folder properties and set the return handler
* data must be an array with the store and folder entryids
*/
propertiesmodule.prototype.getFolderProps = function(data, handler)
{
	this.handler = handler;
	webclient.xmlrequest.addData(this, "folderprops", data);
	webclient.xmlrequest.sendRequest();
}

/**
* This function will give the properties from XML to the dialog function handler
*/
propertiesmodule.prototype.setFolderDialogProps = function(action)
{
	var properties = new Object();
	properties["display_name"] = action.getElementsByTagName("display_name")[0].firstChild.nodeValue;
	properties["content_count"] = action.getElementsByTagName("content_count")[0].firstChild.nodeValue;
	properties["content_unread"] = action.getElementsByTagName("content_unread")[0].firstChild.nodeValue;
	properties["comment"] = action.getElementsByTagName("comment")[0].firstChild?action.getElementsByTagName("comment")[0].firstChild.nodeValue:"";
	properties["container_class"] = action.getElementsByTagName("container_class")[0].firstChild.nodeValue;
	properties["parent_display_name"] = action.getElementsByTagName("parent_display_name")[0].firstChild?action.getElementsByTagName("parent_display_name")[0].firstChild.nodeValue:"";
	properties["message_size"] = action.getElementsByTagName("message_size")[0].firstChild?action.getElementsByTagName("message_size")[0].firstChild.nodeValue:0;
	if (this.handler){
		this.handler(properties);
	}
	
	//quota
	var tableElement = dhtml.getElementById("folder_properties").getElementsByTagName("tbody")[0];
	var store_size = dhtml.getTextNode(action.getElementsByTagName("store_size")[0],"");
	var quota_warn = dhtml.getTextNode(action.getElementsByTagName("quota_warning")[0],"");
	var quota_soft = dhtml.getTextNode(action.getElementsByTagName("quota_soft")[0],"");
	var quota_hard = dhtml.getTextNode(action.getElementsByTagName("quota_hard")[0],"");
	//if there is a quota then...
	if(quota_hard.length > 0 || quota_soft.length > 0){
		var rowElement = dhtml.addElement(tableElement,"tr");
		var dataElement = dhtml.addElement(rowElement,"td");
		dataElement.setAttribute("colspan","2");
		dhtml.addElement(dataElement,"hr");
		
		rowElement = dhtml.addElement(tableElement,"tr");
		dataElement = dhtml.addElement(rowElement,"td","","",_("Quota")+":");
		var quotaElement = dhtml.addElement(rowElement,"td","","quota_properties");
		
		//show quota if there is a quota else show text "none"
		if(quota_hard != 0 || quota_soft != 0){
			var quotaBar = new QuotaWidget(quotaElement,"");
			quotaBar.update(store_size,quota_warn,quota_soft,quota_hard);
		}else{
			dhtml.addTextNode(quotaElement,_("none"));
		}
	}
}

propertiesmodule.prototype.save = function(props)
{
	var data = props;
	data["store"] = this.store;
	data["entryid"] = this.entryid;
	
	if(parentWebclient) {
		parentWebclient.xmlrequest.addData(this, "save", data, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest();
	} else {
		webclient.xmlrequest.addData(this, "save", data);
		webclient.xmlrequest.sendRequest();
	}
}

// permission stuff


propertiesmodule.prototype.initPermissions = function(perm_data)
{
	var userlist = this.permissionElements["userlist"];

	this.permissions = new Object();
	var perm_tags = perm_data.getElementsByTagName("grant");
	for(var i=0; i< perm_tags.length; i++){
		var entryid = dhtml.getTextNode(perm_tags[i].getElementsByTagName("entryid")[0], 0);
		this.permissions[entryid] = new Object();
		this.permissions[entryid]["rights"] = dhtml.getTextNode(perm_tags[i].getElementsByTagName("rights")[0], 0);
		this.permissions[entryid]["username"] = dhtml.getTextNode(perm_tags[i].getElementsByTagName("username")[0], _("unknown"));
		this.permissions[entryid]["fullname"] = dhtml.getTextNode(perm_tags[i].getElementsByTagName("fullname")[0], _("Unknown user/group"));
		this.permissions[entryid]["usertype"] = dhtml.getTextNode(perm_tags[i].getElementsByTagName("usertype")[0], 0);
		
		var option = dhtml.addElement(userlist, "option");
		option.value = entryid;
		option.text = this.permissions[entryid]["fullname"];
	}

	this.setPermissionProfile(0);
}

propertiesmodule.prototype.getPermissionData = function()
{
	var result = new Object();
	
	// first store any changes for the current user
	if (this.permissionSelectedUser){
		this.permissions[this.permissionSelectedUser]["rights"] = this.getPermissionRights();
	}
	
	// construct array with permissions	
	var grants = new Array();
	for(var entryid in this.permissions){
		var user = new Object();
		user["entryid"] = entryid;
		user["rights"] = this.permissions[entryid]["rights"];
		grants[grants.length] = user;
	}
	
	if (grants.length>0){
		result["grant"] = grants;
	}

	return result;
}

propertiesmodule.prototype.getPermissionRights = function()
{
	var result = 0;
	for(var name in this.permissionElements){
		var element = this.permissionElements[name];
		if (name.substr(0, 8) == "ecRights"){
			if (element.checked && element.value>0){
				result += parseInt(element.value, 10);
			}	
		}
	}
	return result;
}

propertiesmodule.prototype.clearPermissionRights = function()
{
	this.permissionElements["ecRightsCreate"].checked = false;
	this.permissionElements["ecRightsReadAny"].checked = false;
	this.permissionElements["ecRightsCreateSubfolder"].checked = false;
	this.permissionElements["ecRightsFolderAccess"].checked = false;
	this.permissionElements["ecRightsFolderVisible"].checked = false;

	var edit_items = this.permissionElements["edit_items"];	
	for(var i=0;i<edit_items.length;i++){
		edit_items[i].checked = false;
	}
	this.permissionElements["ecRightsEditNone"].checked = true;

	var delete_items = this.permissionElements["del_items"];	
	for(var i=0;i<delete_items.length;i++){
		delete_items[i].checked = false;
	}
	this.permissionElements["ecRightsDeleteNone"].checked = true;
	this.setPermissionProfile(0);
}

propertiesmodule.prototype.setPermissionRights = function(rights)
{
	this.clearPermissionRights();
	if((rights & ecRightsCreate) == ecRightsCreate) this.permissionElements["ecRightsCreate"].checked = true;
	if((rights & ecRightsReadAny) == ecRightsReadAny) this.permissionElements["ecRightsReadAny"].checked = true;
	if((rights & ecRightsCreateSubfolder) == ecRightsCreateSubfolder) this.permissionElements["ecRightsCreateSubfolder"].checked = true;
	if((rights & ecRightsFolderAccess) == ecRightsFolderAccess) this.permissionElements["ecRightsFolderAccess"].checked = true;
	if((rights & ecRightsFolderVisible) == ecRightsFolderVisible) this.permissionElements["ecRightsFolderVisible"].checked = true;

	// edit_items
	this.permissionElements["ecRightsEditNone"].checked = true;
	if((rights & ecRightsEditOwned) == ecRightsEditOwned) this.permissionElements["ecRightsEditOwned"].checked = true;
	if((rights & ecRightsEditAny) == ecRightsEditAny) this.permissionElements["ecRightsEditAny"].checked = true;

	// delete_items
	this.permissionElements["ecRightsDeleteNone"].checked = true;
	if((rights & ecRightsDeleteOwned) == ecRightsDeleteOwned) this.permissionElements["ecRightsDeleteOwned"].checked = true;
	if((rights & ecRightsDeleteAny) == ecRightsDeleteAny) this.permissionElements["ecRightsDeleteAny"].checked = true;

	this.setPermissionProfile(rights);
}

propertiesmodule.prototype.setPermissionProfile = function(rights)
{
	// update profile, but disable event handler
	var profile = this.permissionElements["profile"];
	profile.disableEvent = true;
	profile.value = rights;

	// set profile to "other" when there is no profile for the given rights
	if (profile.value!=rights){
		profile.value = -1;
	}
	profile.disableEvent = false;
}

function eventPermissionsUserlistChange(moduleObject, element, event)
{
	if (moduleObject.permissionSelectedUser){
		moduleObject.permissions[moduleObject.permissionSelectedUser]["rights"] = moduleObject.getPermissionRights();
	}

	var rights = moduleObject.permissions[element.value]["rights"];
	moduleObject.setPermissionRights(rights);
	moduleObject.permissionSelectedUser = element.value;

}

function eventPermissionsProfileChange(moduleObject, element, event)
{
	if (!element.disableEvent && element.value != -1) {
		moduleObject.setPermissionRights(element.value);

		moduleObject.permissionChanged = true;
	}
}

function eventPermissionChange(moduleObject, element, event)
{
	var rights = moduleObject.getPermissionRights();
	moduleObject.setPermissionProfile(rights);

	moduleObject.permissionChanged = true;
}

function eventPermissionAddUser(moduleObject, element, event)
{
	callBackData = new Object;
	callBackData.module = moduleObject;
	
	var windowData = new Object();
	windowData["hide_users"] = ["non_active", "contact", "equipment", "room"];
	windowData["hide_groups"] = ["dynamic", "normal"];

	webclient.openModalDialog(moduleObject, 'addressbook', DIALOG_URL+'task=addressbook_modal&storeid='+moduleObject.store+'&type=displayname&source=gab&', 800, 500, abCallBack, callBackData, windowData);
}

function abCallBack(userdata, module)
{
	handlePermissionUserElementChanged(userdata, callBackData);
}
/**
 * Function which handle the returned data from address book page as selected users and module.
 * 			retrieve the users from result object and then add them in permissions and properties select box.
 * @param object result an object with selected user's data.
 * @param object callbackData an object with data related to module which will be needed to 
 							  perform the action related to the module.
 */
function handlePermissionUserElementChanged(result, callbackData) {

	// store current permissions for selected user
	if (callbackData.module.permissionSelectedUser){
		callbackData.module.permissions[callbackData.module.permissionSelectedUser]["rights"] = callbackData.module.getPermissionRights();
	}
 
	if(result){
		if(module){
			var users = new Array();
			// Add a dummy to prevent the xmlbuilder to convert an array with a single element.
			users.push({});
			var select = callbackData.module.permissionElements["userlist"];
			for(var i=0;i<select.options.length;i++){
				userData = select.options[i].data;
				for(var j in userData){
					if(userData[j] == null){
						delete userData[j];
					}
				}
				users.push(userData);
			}

			if(result.multiple){
				for(var key in result){
					if(key != "multiple" && key != "value"){
						eventPermissionUserElementChanged(callbackData.module, result[key].display_name, result[key].entryid);
					}
				}
			}else{
				eventPermissionUserElementChanged(callbackData.module, result.display_name, result.entryid);
			}
		}
	}
}
function eventPermissionUserElementChanged(moduleObject, newUser, newEntryid){

	moduleObject.permissionChanged = true;

	// store current permissions for selected user
	if (moduleObject.permissionSelectedUser){
		moduleObject.permissions[moduleObject.permissionSelectedUser]["rights"] = moduleObject.getPermissionRights();
	}
		
	// add new user
	if (typeof moduleObject.permissions[newEntryid] == "undefined"){
		if (newUser.trim()!=""){
			var userlist = moduleObject.permissionElements["userlist"];
			var option = dhtml.addElement(null, "option");
			option.text = newUser;
			option.value = newEntryid;
			userlist.options[userlist.options.length] = option;
			moduleObject.permissions[newEntryid] = new Object();
			moduleObject.permissions[newEntryid]["entryid"] = newEntryid;
			moduleObject.permissions[newEntryid]["username"] = newUser;
			moduleObject.permissions[newEntryid]["fullname"] = newUser;
			moduleObject.permissions[newEntryid]["rights"] = ecRightsNone|ecRightsFolderVisible;
			moduleObject.permissionSelectedUser = newEntryid;
			moduleObject.setPermissionRights(ecRightsNone|ecRightsFolderVisible);
		}
	}else{
		alert(_("User already exists"));
	}
}

function eventPermissionDeleteUser(moduleObject, element, event)
{
	var userlist = moduleObject.permissionElements["userlist"];
	var old_selected = userlist.selectedIndex;
	var old_value = userlist.value;

	moduleObject.permissionChanged = true;

	// delete useracl
	userlist.selectedIndex = -1;
	userlist.removeChild(userlist.options[old_selected]);
	
	delete moduleObject.permissions[old_value];
	moduleObject.permissionSelectedUser = null;
}
