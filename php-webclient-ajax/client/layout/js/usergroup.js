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

function multiusercalendarSubmit(){
	var result = {
		groupEntryID: false
	};

	var usergroup = document.getElementById("usergroup_dialog_usergroup_list");
	if(usergroup && usergroup.selectedIndex >= 0){
		result.groupEntryID = usergroup.value;
	}


	window.resultCallBack(result, window.callBackData);
	return true;
}

function addGroup(){
	webclient.openModalDialog(-1, 'mucalendar_loadgroup', DIALOG_URL+'task=advprompt_modal', 300,150, mucalendarDialog_addgroup_addCallBack, null, {
		windowname: _("Enter a group name"),
		fields: [{
			name: "groupname",
			label: _("Enter a group name"),
			type: "normal",
			required: true,
			value: ""
		}
		]
	});
}
function removeGroup(){
	var data = {
		moduleID: module.id, 
		groupEntryID: false,
		groupEntryIDs: new Array()
	}
	var usergroup = document.getElementById("usergroup_dialog_usergroup_list");
	if(usergroup && usergroup.selectedIndex >= 0){
		data.groupEntryID = usergroup.value;
		for(var i=0;i<usergroup.options.length;i++){
			if(usergroup.options[i].selected){
				data.groupEntryIDs.push(usergroup.options[i].value);
			}
		}

		webclient.xmlrequest.addData(module, "removegroup", {group_entry_id: data.groupEntryIDs});
		webclient.xmlrequest.addData(module, "getgroups", {});
		setTimeout(function(){
			webclient.xmlrequest.sendRequest();
		}, 0);

		// Clear userlist
		var userlist = document.getElementById("usergroup_dialog_usergroup_userlist", "select");
		while(userlist.options.length > 0){
			userlist.remove(0);
		}

	}
}

function addUser(){
	var data = {
		moduleID: module.id, 
		groupEntryID: false
	}
	var usergroup = document.getElementById("usergroup_dialog_usergroup_list");
	if(usergroup && usergroup.selectedIndex >= 0){
		data.groupEntryID = usergroup.value;

		var windowData = new Object();
		windowData["hide_users"] = ["contact"];
		windowData["hide_groups"] = ["dynamic", "normal", "everyone", "security"];
		windowData["hide_companies"] = true;

		webclient.openModalDialog(-1, 'addressbook', DIALOG_URL+'&type=username&source=gab&task=addressbook_modal&storeid=' + parentWebclient.hierarchy.defaultstore["id"], 800, 500, mucalendarDialog_addusertogroup_addCallBack, data, windowData);
	}
}

function removeUser(){
	var data = {
		moduleID: module.id, 
		groupEntryID: false
	};

	var usergroup = document.getElementById("usergroup_dialog_usergroup_list", "select");
	var userlist = document.getElementById("usergroup_dialog_usergroup_userlist", "select");
	var users = new Array();
	users.push({});
	if(usergroup && userlist && usergroup.selectedIndex >= 0 && userlist.selectedIndex >= 0){
		for(var i=0;i<userlist.options.length;i++){
			if(!userlist.options[i].selected){
				users.push(userlist.options[i].data);
			}
		}
		data.groupEntryID = usergroup.value;
		webclient.xmlrequest.addData(module, "savegroup", {users: users, group_entry_id: data.groupEntryID});
		webclient.xmlrequest.addData(module, "getgroupusers", {group_entry_id: data.groupEntryID});
		setTimeout(function(){
			webclient.xmlrequest.sendRequest();
		}, 0);
	}
}

function mucalendarDialog_addusertogroup_addCallBack(result, callbackData) {
	if(result && typeof callbackData.moduleID == "number" && callbackData.groupEntryID){
		if(module){
			var users = new Array();
			// Add a dummy to prevent the xmlbuilder to convert an array with a single element.
			users.push({});
			var select = document.getElementById("usergroup_dialog_usergroup_userlist", "select");
			for(var i=0;i<select.options.length;i++){
				userData = select.options[i].data;
				for(var j in userData){
					if(userData[j] == null){
						delete userData[j];
					}
				}
				users.push(userData);
			}
			// newUser array is create which has the list of all new user being added to group
			var newUser = new Array();
			if(result.multiple){
				for(var key in result){
					if(key != "multiple" && key != "value"){
						newUser.push({
							userentryid: result[key].entryid,
							username: result[key].fileas,
							display_name: result[key].display_name,
							emailaddress: result[key].email_address
						});
					}
				}
			}else{
				newUser.push({
					userentryid: result.entryid,
					username: result.fileas,
					display_name: result.display_name,
					emailaddress: result.email_address
				});
			}
			// here we check for the duplicate data, by comparing the two userlist and 
			// newUserlist to check if user already exist.
			if(newUser && newUser.length > 0){
				for(var i=0; i<newUser.length; i++){
					for(var j=0; j<users.length; j++){
						if(compareEntryIds(newUser[i].userentryid, users[j].userentryid))
							newUser[i] = false;
					}
					if(newUser[i])
						users.push(newUser[i]);
				}
			}

			webclient.xmlrequest.addData(module, "savegroup", {users: users, group_entry_id: callbackData.groupEntryID});
			webclient.xmlrequest.addData(module, "getgroupusers", {group_entry_id: callbackData.groupEntryID});
			setTimeout(function(){
				webclient.xmlrequest.sendRequest();
			}, 0);
		}
	}
}


function mucalendarDialog_addgroup_addCallBack(result, callbackData) {
	/**
	 * The use of settimout is to cut off any references to the window where this callback 
	 * originated from. The code below performs an AJAX request. When the result is returned the 
	 * browser throws an error:
	 * 
	 * [Exception... "Component returned failure code: 0x80040111 (NS_ERROR_NOT_AVAILABLE) [nsIXMLHttpRequest.status]" 
	 * nsresult: "0x80040111 (NS_ERROR_NOT_AVAILABLE)" 
	 * location: "JS frame :: http://localhost/Connectux/webaccess/
	 *   static.php?version=6.00-7715&p[]=client/core/xmlrequest.js :: anonymous :: line 129" data: no]
	 * 
	 * This error is fixed when a settimeout is used. This probably prevents it from returning 
	 * anything back to the window from which the call originated. Since the window is no longer 
	 * there (it is closed after the callback function has been invoked) the AJAX code somehow fails
	 * on line 129 of /client/core/xmlrequest.js :
	 *    if (typeof (requests[i].status)=="undefined"
	 * 
	 * URL that helped figuring out the problem:
	 * http://matthom.com/archive/2006/05/15/scary-ajax-error
	 */
	setTimeout(function(){
		var groupName = result.groupname;
		if(groupName && groupName.length > 0){

			webclient.xmlrequest.addData(module, "savegroup", {users: [], groupname: groupName});
			// The getGroups method will perform the xmlrequest: webclient.xmlrequest.sendRequest();
			module.getGroups();
		}
	},0);
}
