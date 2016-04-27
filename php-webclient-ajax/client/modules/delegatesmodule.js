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
 * DelegatesModule
 * This Delegates Module extends Module.
 */
delegatesmodule.prototype = new Module;
delegatesmodule.prototype.constructor = delegatesmodule;
delegatesmodule.superclass = Module.prototype;

function delegatesmodule(id)
{
	if(arguments.length > 0) {
		this.init(id);
	}
}

/**
 * Function which intializes the module.
 * @param integer id id
 * @param object data	module data
 */ 
delegatesmodule.prototype.init = function(id, data)
{
	if(data) {
		for(var property in data)
		{
			this[property] = data[property];
		}
	}
	// Object that holds all delegates informations.
	this.delegateProps = new Object();

	// Object that holds all selected delegates in list.
	this.selectedDelegates = new Array();

	delegatesmodule.superclass.init.call(this, id);
}

/**
 * Function which execute an action. This function is called by the XMLRequest object.
 * @param string type the action type
 * @param object action the action tag 
 */ 
delegatesmodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "list":
			this.createDelegatesList(action);
			break;
		case "error":
			var error = dhtml.getXMLNode(action, "error", 0);
			var errorString = dhtml.getTextNode(error.getElementsByTagName("message")[0],"");

			if(errorString.length > 0){
				alert(errorString);
			}
			break;
		case "newuserpermissions":
			var delegate = new Object();
			var item = dhtml.getXMLNode(action, "delegate", 0);
			var entryid = dhtml.getXMLValue(item, "entryid", false);

			if (entryid){
				var fullname = dhtml.getXMLValue(item, "fullname", false);
				var see_private = dhtml.getXMLValue(item, "see_private", false);
				var delegate_meeting_rule = dhtml.getXMLValue(item, "delegate_meeting_rule", false);
				var permissions = dhtml.getXMLNode(item, "permissions", 0);

				delegate["entryid"] = entryid;
				delegate["fullname"] = fullname;
				delegate["see_private"] = see_private;
				delegate["delegate_meeting_rule"] = delegate_meeting_rule;
				delegate["permissions"] = new Object();

				if (permissions){
					var rights = permissions.getElementsByTagName("rights");
					var folder = new Object();

					for (var j = 0; j < rights.length; j++){
						folder[rights[j].getAttribute("foldername")] = dhtml.getTextNode(rights[j], 0);
					}
					delegate["permissions"] = folder;
				}
			}
			// Send as array because we can also have multiple delegates.
			initDelegatePermissions(new Array(delegate));
			break;
		case "saved":
			window.close();
			break;
	}
}

/**
 * Function which sends a request to the server, with the action "list".
 */ 
delegatesmodule.prototype.getDelegates = function ()
{
	var data = new Object();

	webclient.xmlrequest.addData(this, "list", data);
	webclient.xmlrequest.sendRequest();
}

/**
 * Function which takes care of the list action. It is responsible for
 * calling the "initDelegates" function in the tablewidget to create list.
 * @param object action the action tag
 */ 
delegatesmodule.prototype.createDelegatesList = function (action)
{
	var delegates = new Object();
	var items = action.getElementsByTagName("delegate");

	// Delegates
	for (var i = 0; i < items.length; i++){
		var entryid = dhtml.getXMLValue(items[i], "entryid", false);

		if (entryid){
			var fullname = dhtml.getXMLValue(items[i], "fullname", false);
			var see_private = dhtml.getXMLValue(items[i], "see_private", false);
			var delegate_meeting_rule = dhtml.getXMLValue(items[i], "delegate_meeting_rule", false);
			var permissions = dhtml.getXMLNode(items[i], "permissions", 0);

			delegates[entryid] = new Object();
			delegates[entryid]["entryid"] = entryid;
			delegates[entryid]["fullname"] = fullname;
			delegates[entryid]["see_private"] = see_private;
			delegates[entryid]["delegate_meeting_rule"] = delegate_meeting_rule;
			delegates[entryid]["permissions"] = new Object();

			if (permissions){
				var rights = permissions.getElementsByTagName("rights");
				var folder = new Object();

				for (var j = 0; j < rights.length; j++){
					folder[rights[j].getAttribute("foldername")] = dhtml.getTextNode(rights[j], 0);
				}
				delegates[entryid]["permissions"] = folder;
			}
		}		
	}
	this.delegateProps = delegates;
	// Initialize delegates dialog
	populateDelegateList(delegates);
}
/**
 * Function which saves a set of properties for a delegate.
 * @param object delegate the properties which should be saved 
 */ 
delegatesmodule.prototype.save = function (delegates)
{
	var data = new Object();
	data["delegate"] = delegates;
	
	webclient.xmlrequest.addData(this, "save", data);
	webclient.xmlrequest.sendRequest();
}
/**
 * Function which requests for user permissions
 * when user is added using addressbook.
 * @param string entryid entryid of the user
 */
delegatesmodule.prototype.getNewUserPermissions = function (entryid)
{
	var data = new Object();
	data["entryid"] = entryid;
	
	webclient.xmlrequest.addData(this, "newuserpermissions", data);
	webclient.xmlrequest.sendRequest();
}
/**
 * Function which returns array contains information
 * selected delegates from list.
 * @return array delegates -selected delegates.
 */
delegatesmodule.prototype.getSelectedDelegates = function ()
{
	var delegates = false;
	if (this.selectedDelegates.length > 0){
		delegates = new Array();

		for (var i = 0; i < this.selectedDelegates.length; i++)
			delegates.push(this.delegateProps[this.selectedDelegates[i]]);
	}
	return delegates;
}
/**
 * Function which opens addressbook for added new delegate.
 */
function eventAddDelegate(moduleObject, element, event)
{
	var windowData = new Object();
	windowData["hide_groups"] = ["dynamic", "normal", "everyone"];
	windowData["hide_companies"] = true;

	parentWebclient.openModalDialog(-1, 'addressbook', DIALOG_URL+'task=addressbook_modal&storeid='+ moduleObject.storeid +'&type=username_single&source=gab', 800, 500, delegatesFromABCallBack, false, windowData);
}
/**
 * Function which removes delegates.
 */
function eventRemoveDelegate(moduleObject, element, event)
{
	// Get selected delegates.
	var delegates = moduleObject.getSelectedDelegates();

	if (delegates){
		// Delete delegate info.
		for (var i = 0; i < delegates.length; i++)
			delete moduleObject.delegateProps[delegates[i]["entryid"]];

		populateDelegateList(moduleObject.delegateProps);
		this.selectedDelegates = new Array();
	}
}
/**
 * Function which opens permissions dialog
 * from which delegates settings are edited.
 */  
function eventEditDelegatePermissions(moduleObject, element, event)
{
	var windowData = new Object();

	windowData["newDelegate"] = false;
	windowData["delegate"] = moduleObject.getSelectedDelegates();

	if (windowData["delegate"]) {
		webclient.openModalDialog(module, 'delegatespermission', DIALOG_URL+'task=delegatespermission_modal', 430, 375, setPermissionsCallBack, false, windowData);
	}
}