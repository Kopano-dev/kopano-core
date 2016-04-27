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

function initSharedFolder() {
	var folders = new Array();
	folders["calendar"] = _("Calendar");
	folders["contact"] = _("Contact");
	folders["all"] = _("Entire Inbox");
	folders["inbox"] = _("Inbox");
	folders["note"] = _("Notes");
	folders["task"] = _("Tasks");

	var show_only_folders = (window.windowData && window.windowData["sharetype"] == "folder") ? true : false;
	var foldertype = dhtml.getElementById("foldertype");

	for (var i in folders) {
		if (show_only_folders && i == "all") continue;

		var option = dhtml.addElement(null, "option");
		option.text = folders[i];
		option.value = i;
		foldertype.options[foldertype.length] = option;
	}

	foldertype.value = show_only_folders ? "calendar" : "all";

	// toggle checbox state
	if(foldertype.value == "all") {
		dhtml.getElementById("subfolders_checkbox").disabled = true;
		dhtml.addClassName(dhtml.getElementById("subfolders_label"), "disabled_text");
	} else {
		dhtml.getElementById("subfolders_checkbox").disabled = false;
		dhtml.removeClassName(dhtml.getElementById("subfolders_label"), "disabled_text");
	}

	dhtml.addEvent(-1, foldertype, "change", sharedFolderOnChangeFolderType);
}

function submitSharedFolder(){

    if (dhtml.getElementById("username").value.trim()==""){
        alert(_("You must specify a username!"));
        return;
    }else if(window.usernamefromAB === true){
		submitSharedFolderWithResolvedName();
	}else{
		checkNames(checkNamesCallBackForSharedFolder);
	}
}

function addressBookCallBack(userdata) {

	window.usernamefromAB = true;
    dhtml.getElementById('username').value = userdata.value;
}

function openSelectUserDialog(storeid){
	var windowData = new Object();
	windowData["hide_users"] = ["contact"];
	windowData["hide_groups"] = ["dynamic", "normal", "everyone", "security"];
	windowData["hide_companies"] = true;
    webclient.openModalDialog(module, 'addressbook', DIALOG_URL+'task=addressbook_modal&storeid='+storeid+'&type=username_single&source=gab', 800, 500, addressBookCallBack, false, windowData);
}

function sharedFolderOnChangeFolderType(moduleObject, element, event) {
	if(element.value == "all") {
		dhtml.getElementById("subfolders_checkbox").checked = false;
		dhtml.getElementById("subfolders_checkbox").disabled = true;
		dhtml.addClassName(dhtml.getElementById("subfolders_label"), "disabled_text");
	} else {
		dhtml.getElementById("subfolders_checkbox").disabled = false;
		dhtml.removeClassName(dhtml.getElementById("subfolders_label"), "disabled_text");
	}
}

/**
 * Function which call resolvename module
 * @param function callBackFunction which will return the resolved name
 */
function checkNames(callBackFunction){
	if(dhtml.getElementById("username").value.trim() == "")
		return;
	
	var resolveQue = new Object();
	/**
	 * This is sort of Hack. 
	 * As currently the resolvename module is very tightly bounded with recipients fields.
	 * So, if u want to resolve any names you need to create an object with to,cc,bcc fields
	 */ 
	resolveQue["to"] = dhtml.getElementById("username").value;
	resolveQue["cc"] = '';
	parentWebclient.resolvenames.resolveNames(resolveQue, callBackFunction, true);
}

/**
 * Function is a CallBack function which will return reslved name
 * @param Object resolveObj it has the list of resolved names
 */
function checkNamesCallBackForSharedFolder(resolveObj)
{
	for(var i in resolveObj){

		//replace unresolved name
		var unResolved = dhtml.getElementById("username").value.trim();
		for(var keyword in resolveObj[i]){
			if(unResolved == keyword)
				unResolved = resolveObj[i][keyword]["username"];
		}
	}	
	dhtml.getElementById("username").value = unResolved ;
	submitSharedFolderWithResolvedName();
}

/**
 * Function is to submit the OpenSharedfolder dialog with resolved username
 * this will return the username, folder, subfolder data to the callback.
 */
function submitSharedFolderWithResolvedName(){
	var username = dhtml.getElementById("username").value;
    var folder = dhtml.getElementById("foldertype").value;
    var subfolders = dhtml.getElementById("subfolders_checkbox").checked;

    var result = new Object;
    result.username = username;
    result.folder = folder;
    result.subfolders = String(subfolders);
		
	window.close();
	return window.resultCallBack(result, window.callBackData);	
}

/**
 * Function which is called if username textbox is edited.
 * this sets the global usernamefromAB's value
 */
function userNameChange(){
	window.usernamefromAB = false;
}