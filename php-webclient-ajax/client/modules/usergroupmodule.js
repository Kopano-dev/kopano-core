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

usergroupmodule.prototype = new Module;
usergroupmodule.prototype.constructor = usergroupmodule;
usergroupmodule.superclass = Module.prototype;

function usergroupmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
	
}

usergroupmodule.prototype.init = function(id, element, title, data)
{
	usergroupmodule.superclass.init.call(this, id, element, title, data);
}

usergroupmodule.prototype.getGroups = function(){
	webclient.xmlrequest.addData(this, "getgroups", {});
	webclient.xmlrequest.sendRequest();
}

usergroupmodule.prototype.getUsers = function(groupEntryID){
	webclient.xmlrequest.addData(this, "getgroupusers", {group_entry_id: groupEntryID});
	webclient.xmlrequest.sendRequest();
}

/**
 * Function which execute an action. This function is called by the XMLRequest object.
 * @param string type the action type
 * @param object action the action tag 
 */ 
usergroupmodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "groups":
			this.listGroups(action);
			break;
		case "users":
			this.listUsers(action);
			break;
	}
}


usergroupmodule.prototype.listGroups = function(action)
{
	var select = document.getElementById("usergroup_dialog_usergroup_list");
	while(select.options.length > 0){
		select.remove(0);
	}

	dhtml.addEvent(this, select, "change", eventUserGroupModuleDialogOnChange);
	var items = action.getElementsByTagName("item");
	for(var i=0;i<items.length;i++){
		var item = items[i];
		var data = {
			group_entry_id: dhtml.getXMLValue(item, "entryid"),
			display_name: dhtml.getXMLValue(item, "subject")
		}
		select.options[select.options.length] = new Option(data.display_name, data.group_entry_id);
	}
}

usergroupmodule.prototype.listUsers = function(action)
{
	var select = document.getElementById("usergroup_dialog_usergroup_userlist", "select");
	while(select.options.length > 0){
		select.remove(0);
	}

	var items = action.getElementsByTagName("item");
	for(var i=0;i<items.length;i++){
		var item = items[i];
		var data = {
			userentryid: dhtml.getXMLValue(item, "userentryid"),
			display_name: dhtml.getXMLValue(item, "display_name"),
			emailaddress: dhtml.getXMLValue(item, "emailaddress"),
			username: dhtml.getXMLValue(item, "username"),
			access: dhtml.getXMLValue(item, "access"),
			storeid: dhtml.getXMLValue(item, "storeid"),
			calentryid: dhtml.getXMLValue(item, "calentryid")
		}
		var opt = new Option(data.display_name, data.display_name);
		opt.data = data;
		select.options[select.options.length] = opt;
		dhtml.addEvent(this, select.options[(select.options.length-1)], "click", eventUserGroupUserModuleDialogOnClick);
	}
}

function eventUserGroupModuleDialogOnChange(moduleObj, element, event){
	var select = document.getElementById("usergroup_dialog_usergroup_userlist", "select");
	while(select.options.length > 0){
		select.remove(0);
	}
	moduleObj.getUsers(element.value);
}

function eventUserGroupUserModuleDialogOnClick(moduleObj, element, event){
	
}
