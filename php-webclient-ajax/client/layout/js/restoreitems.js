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
 * function to handle the action restore selected items.
 * collect the entry ids from selected rows and pass them to request
 * with action_type = restore / folderrestore , to server.
 */
function restore_selected_items(){
	//action_type is defined to know which type of request(ie.for message or folder) should be send 
	//to server when the button in menu bar is clicked
	var action_type = (dhtml.getElementById("message").checked) ? "restoremsg" : "restorefolder";
	var selectedRowCount = tableWidget.getNumSelectedRows();
	var entryIds = new Array();
	var displaynames = new Array();
	if(selectedRowCount > 0){
		for (var i=0; i<selectedRowCount; i++){
			var rowId = tableWidget.getSelectedRowID(i);
			entryIds.push(tableWidget.getDataByRowID(rowId).entryid);
			//gets the diplay name of corresponding folder entryid to send it on server while  restoring folder  
			if(action_type == "restorefolder"){
				displaynames.push(tableWidget.getDataByRowID(rowId).display_name.innerHTML);
			}
		}
		var send = confirm(_("Would you like to restore the selected items?"));
		if(send) {
			var data = createRequestObject(module, action_type, entryIds, displaynames);
	
			webclient.xmlrequest.showLoader();
			if(parentWebclient){
				parentWebclient.xmlrequest.addData(module, action_type, data, webclient.modulePrefix);
				parentWebclient.xmlrequest.sendRequest();
			}else{
				webclient.xmlrequest.addData(module,  action_type, data);
				webclient.xmlrequest.sendRequest();
			}
		}
	}else{
		alert(_("Please select a row first to restore"));
	}
}
/**
 * function to handle the action delete permanently selected items.
 * collect the entry ids from selected rows and pass them to request
 * with action_type = delete / folderdelete, to server.
 */
function permanent_delete_items(){
	//action_type is defined to know which type of request(ie.for message or folder) should be send 
	//to server when the button in menu bar is clicked
	var action_type = (dhtml.getElementById("message").checked)? "deletemsg" : "deletefolder";
	var selectedRowCount = tableWidget.getNumSelectedRows();
	var entryIds = new Array();
	if(selectedRowCount > 0){
		for (var i=0; i<selectedRowCount; i++){
			var rowId = tableWidget.getSelectedRowID(i);
			entryIds.push(tableWidget.getDataByRowID(rowId).entryid);
		}
		var send = confirm(_("Would you like to delete the selected items permanently (items will not be recoverable)?"));
		if(send) {
			var data = createRequestObject(module, action_type, entryIds);
			webclient.xmlrequest.showLoader();
			webclient.xmlrequest.addData(module, action_type, data);
			webclient.xmlrequest.sendRequest();
		}
	}else{
		alert(_("Please first select a row to delete."));
	}
}

/**
 * function to handle the action delete all items permanently
 * pass the parameter action_type = deleteall / folderdeleteall, to server.
 */
function permanent_delete_all(){
	//action_type is defined to know which type of request(ie.for message or folder) should be send 
	//to server when the button in menu bar is clicked
	var action_type = (dhtml.getElementById("message").checked)? "deleteallmsg" : "deleteallfolder";
	var rowCount = tableWidget.getRowCount();
	if(parseInt(rowCount,10) > 0){
		var send = confirm(_("Would you like to delete all items permanently, after that you will not be able to restore them anyway?"));
		if(send) {
			var data = createRequestObject(module, action_type, null);
			webclient.xmlrequest.showLoader();
			// The cancel call will delete the item for us	
			webclient.xmlrequest.addData(module, action_type, data);
			webclient.xmlrequest.sendRequest();
		}
	}else{
		alert(_("There are no items in the list to delete."));
	}
}

/**
 * function to handle the action restore all items permanently
 * pass the parameter action_type = restoreall / folderrestoreall, to server.
 */
function permanent_restore_all(){
	//action_type is defined to know which type of request(ie.for message or folder) should be send 
	//to server when the button in menu bar is clicked
	var action_type = (dhtml.getElementById("message").checked)? "restoreallmsg" : "restoreallfolder";
	var rowCount = tableWidget.getRowCount();
	if(parseInt(rowCount,10) > 0){
		var send = confirm(_("Would you like to restore all items?"));
		if(send) {
			var data = createRequestObject(module, action_type, null);
			webclient.xmlrequest.showLoader();
			if(parentWebclient){
				parentWebclient.xmlrequest.addData(module, action_type, data, webclient.modulePrefix);
				parentWebclient.xmlrequest.sendRequest();
			}else{
				webclient.xmlrequest.addData(module, action_type, data);
				webclient.xmlrequest.sendRequest();
			}
		}	
	}else{
		alert(_("There are no items in list to Restore"));
	}
}

/**
 * function to initialize the table widget and put the data into it.
 * it also update the status bar's data.
 * @param object data an object with data to be filled in table widget.
 * @param string type is a variable used to set views ie. folder/message 
 * in restore item dialog.
 */
function initRestoreItems(data, type){

	if(type == "list"){
		var columnData = [
			{id:"icon_index",name:"","title":"Icon","sort":false,"visibility":true,"order":0,"width":25},
			{id:"sender_name",name:_("From"),"title":"From","sort":true,"visibility":true,"order":1,"width":200},
			{id:"subject",name:_("Subject"),"title":"Subject","sort":true,"visibility":true,"order":2,"width":300},
			{id:"deleted_on",name:_("Deleted on"),"title":"Deleted on","sort":true,"visibility":true,"order":3,"width":100},
			{id:"message_size",name:_("Size"),"title":"Size","sort":true,"visibility":true,"order":4,"width":""}
		]; 
	}else{
		//columns title for recover folder items are defined
		var columnData = [
			{id:"folder_icon_index",name:"","title":"Icon","sort":false,"visibility":true,"order":0,"width":30},
			{id:"display_name",name:_("Name"),"title":"Folder Name","sort":true,"visibility":true,"order":1,"width":300},
			{id:"deleted_on",name:_("Deleted on"),"title":"Deleted on","sort":true,"visibility":true,"order":2,"width":200},
			{id:"content_count",name:_("Item count"),"title":"Item Count","sort":true,"visibility":true,"order":3,"width":""}
		]; 	
	} 

	//create table widget view here
	var tableWidgetElem = dhtml.getElementById("restoreitemstable");
	tableWidgetElem.innerHTML ="";
	 
 	tableWidget = new TableWidget(tableWidgetElem);
	
	for (var x in columnData){
		var col = columnData[x];
		tableWidget.addColumn(col["id"],col["name"],col["width"],col["order"],col["title"],col["sort"],col["visibility"]);
	}

	//put the data in table widget
	var items = new Array();
	
	//create an object to pass as data object in tablewidget.
	for(var i in data){
		var itemData = data[i];
		var item = new Object();

		for(var j=0;j<columnData.length;j++){
			var value = "";
			var colId = columnData[j]["id"];
			switch(colId){
				case "icon_index":
					value = "<div class='rowcolumn message_icon "+iconIndexToClassName(itemData["icon_index"], itemData["message_class"], false)+"'>&nbsp;</div>";
					break;
				case "deleted_on":
					value = strftime(_("%x %X"), itemData["deleted_on"]);
					break;
				case "folder_icon_index":
					value = "<div class='rowcolumn message_icon folder_icon_publicfolders'>&nbsp;</div>";	
					break;
				default:
					if(typeof(itemData[colId])!="undefined"){
						value = String(itemData[colId]).htmlEntities();
					}else{
						value = "&nbsp;";
					}
			}
			item[colId] = {innerHTML: value};
		}
		item["entryid"] = itemData["entryid"];
		item["rowID"] = itemData["entryid"];
		items.push(item);
	}

	//generate tableWidget
	tableWidget.generateTable(items);

	//set the status bar message
	var status = dhtml.getElementById("restoreitems_status");
	var selectedRowCount = tableWidget.getRowCount();
	if(type == "list"){
		status.innerHTML = _("Total %s recoverable items").sprintf("<b>"+selectedRowCount+"</b>");
	}else{
		status.innerHTML = _("Total %s recoverable folders").sprintf("<b>"+selectedRowCount+"</b>");
	}

	//Hide the loader. // need to check.. actually it should work but not working properly.
	if(webclient.xmlrequest.getRequestCount() > 0){
		//webclient.xmlrequest.hideLoader();
		dhtml.getElementById("zarafa_loader").style.visibility = "hidden";
	}

}

/**
 * function which creates the request object for all functions.
 * @param Object moduleObject contains all data of module  object.
 * @param String actio_type define the action.
 * @param Array entryIdList array of selected folder entryIds 
 * @param Array displayNames array of selected folder displayNames 
 * if null passed as value that means function is delete
 * @return the request data object.
 */
function createRequestObject(moduleObject, action_type, entryIdList, displayNames){
	var data = new Object;
	data["store"] = moduleObject.storeid;
	data["entryid"] = moduleObject.entryid;
	data["attributes"] = new Object();
	data["attributes"]["type"] = action_type;
	data["sort"] = new Object;
	data["sort"]["column"] = moduleObject.loadSortSettings();
	var regtype = /(folder$)|(^folder)/;
	if(action_type.match(regtype)){
		//folders tag is added to data which contains entryid and displayname of folder
		data["folders"]= new Array();
		data["folders"]["folder"]= new Array();
		
		if(entryIdList){ 
			for(var i=0; i<entryIdList.length; i++ ){
				var folderList = new Object();
	
				if(!!entryIdList){ 
					folderList["entryid"] = entryIdList[i];
				}
				if(!!displayNames){ 
					folderList["display_name"] = displayNames[i];
				}
				data["folders"]["folder"].push(folderList);
			}
		}
	}else{
		//messages tag is added to data which contains entryid and of message
		data["messages"]= new Array();
		data["messages"]["message"]= new Array();
		
		if(!!entryIdList){ 
			for(var i=0; i<entryIdList.length; i++ ){
				var messageList = new Object();
				messageList["entryid"] = entryIdList[i];
				data["messages"]["message"].push(messageList);
			}
		}
	}			
	return data;
}

	

/**
 * function which generates an event when radio button is clicked
 * in restore items dialog. it changes the view; message or folder 
 * according to the option selected in restore items.   
 */
function loadRecoverableData(moduleObject, element, event){
	var entryIds = new Array();
	
	if (element && element.checked){
			var data = createRequestObject(module, element.value, entryIds);
			webclient.xmlrequest.addData(module, element.value, data);	
			webclient.xmlrequest.sendRequest();
	}	
}	
