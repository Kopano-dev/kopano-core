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

tasklistmodule.prototype = new ListModule;
tasklistmodule.prototype.constructor = tasklistmodule;
tasklistmodule.superclass = ListModule.prototype;

var modid;

function tasklistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

tasklistmodule.prototype.init = function(id, element, title, data)
{
	modid = id;
	
	tasklistmodule.superclass.init.call(this, id, element, title, data);
	this.initializeView();
	this.unixtime = new Object()
	this.previousvalue = new Object()

	webclient.menu.buildTopMenu(this.id, "task", this.menuItems, eventListNewMessage);

	var items = new Array();
	items.push(webclient.menu.createMenuItem("open", _("Open"), false, eventListContextMenuOpenMessage));
	items.push(webclient.menu.createMenuItem("print", _("Print"), false, eventListContextMenuPrintMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("markread", _("Mark Read"), false, eventMailListContextMenuMessageFlag));
	items.push(webclient.menu.createMenuItem("markunread", _("Mark Unread"), false, eventMailListContextMenuMessageFlag));
	items.push(webclient.menu.createMenuItem("assigntask", _("Assign Task"), false, eventTaskListContextMenuMessageAssignTask))
	items.push(webclient.menu.createMenuItem("categories", _("Categories"), false, eventListContextMenuCategoriesMessage));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("delete", _("Delete"), false, eventListContextMenuDeleteMessage));
	this.contextmenu = items;

	webclient.inputmanager.addObject(this, this.element);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["refresh"], "keyup", eventListKeyCtrlRefreshFolder);
}


// this function updates the 'type' attribute of the (hard coded) date fields to 'timestamp_date'
// in the given XML action object
tasklistmodule.prototype.setDateOnlyFields = function(action)
{
	var dateFields = ["duedate", "startdate", "datecompleted"];
	for(var j = 0; j < dateFields.length; j++){
		var dateElements = action.getElementsByTagName(dateFields[j]);
		for(var i = 0; i < dateElements.length; i++){
			dateElements[i].setAttribute("type", "timestamp_date");
		}
	}
}

tasklistmodule.prototype.messageList = function(action)
{
	this.setDateOnlyFields(action);
	tasklistmodule.superclass.messageList.call(this, action);
}


/**
 * Function which is used for setting a message complete (task)
 * @param string messageEntryid entryid of the message
 * @param boolean complete true - set message complete  
 */ 
tasklistmodule.prototype.completeStatus = function(messageEntryid, complete)
{
	var msgProps = this.itemProps[messageEntryid];

	if (msgProps.taskstate && (msgProps.taskstate == 3 || msgProps.taskstate == 4))
		alert(_("You do not own this task. Your changes may be overwritten by the task owner."));

	tasklistmodule.superclass.completeStatus.call(this, messageEntryid, complete);
}

/**
* Saves tasks
* @param element table row element which contains the item.
*/
tasklistmodule.prototype.SubmitMessage = function (moduleObject, element, event)
{
	var entryid = false;
	if (moduleObject.itemProps[moduleObject.entryids[element.id]]) {
		entryid = moduleObject.itemProps[moduleObject.entryids[element.id]]["entryid"];

		var msgProps = this.itemProps[entryid];
		if (msgProps.taskstate && (msgProps.taskstate == 3 || msgProps.taskstate == 4))
			alert(_("You do not own this task. Your changes may be overwritten by the task owner."));
	}

	var property = "editprops";
	var props = new Object();
	var commonstart = false;
	var commonend = false;
	
	if (element.id == "insertrow"){
		property = "insertprops";
	}

	//StartDate
	var text_startdate = dhtml.getElementById(property +"_module"+ moduleObject.id +"_text_startdate");	
	var startdate = 0;
	if (text_startdate && text_startdate.value) {
		dhtml.getElementById(property +"_module"+ moduleObject.id +"_startdate").value = Date.parseDate(text_startdate.value, _("%d-%m-%Y"), true).getTime()/1000;			
		startdate = Date.parseDate(text_startdate.value, _("%d-%m-%Y"), true).getTime()/1000;			
		commonstart = Date.parseDate(text_startdate.value, _("%d-%m-%Y"), false).getTime()/1000;
	}

	//Duedate
	var text_duedate = dhtml.getElementById(property +"_module"+ moduleObject.id +"_text_duedate");
	var duedate = 0;
	if (text_duedate && text_duedate.value) {
		dhtml.getElementById(property +"_module"+ moduleObject.id +"_duedate").value = Date.parseDate(text_duedate.value, _("%d-%m-%Y"), true).getTime()/1000;
		duedate = Date.parseDate(text_duedate.value, _("%d-%m-%Y"), true).getTime()/1000;
		commonend = Date.parseDate(text_duedate.value, _("%d-%m-%Y"), false).getTime()/1000;
	} else {
		if (startdate){
			dhtml.getElementById(property +"_module"+ moduleObject.id +"_duedate").value = startdate;
			duedate = startdate;
			commonend = commonstart;
		}
	}

	if (duedate < startdate){
		alert(_("The due date of a task cannot occur before its start date."));
		return false;
	}
	// Percent Complete
	var text_percent_complete = dhtml.getElementById(property +"_module"+ moduleObject.id +"_text_percent_complete");
	if (text_percent_complete) {
		var percent = text_percent_complete.value;
		if (percent.indexOf("%") >= 0) {
			percent = percent.substring(0, percent.indexOf("%"));
		}
		
		percent = parseInt(percent);
		if (percent >= 0) {
			dhtml.getElementById(property +"_module"+ moduleObject.id +"_percent_complete").value = percent / 100;
		}
	}
	
	//Importance
	var importance = moduleObject.itemProps[moduleObject.entryids[element.id]];
	var importanceElement = dhtml.getElementById(property +"_module"+ moduleObject.id +"_importance");
		if (importance &&  importanceElement !== null) {
		importanceElement.value = importance["importance"];
	}
	
	//filter duplicate categories
	var categories = dhtml.getElementById(property +"_module"+ moduleObject.id +"_categories");
	if (categories) {
		moduleObject.filtercategories(categories, categories.value);
	}

	//get properties from input fields
	props = getProps(element, this.properties, entryid);
	
	/*The following code can only be implemented after calling 'getProps()' function on 'props' object*/

	if (commonstart)
		props["commonstart"] = commonstart;

	if (commonend)
		props["commonend"] = commonend;

	//complete
	var complete = dhtml.getElementById(property +"_module"+ moduleObject.id +"_complete");
	// Complete column is not visible
	if (!complete || percent) {
		if (percent && percent == 100){
			props["status"] = "2";
			props["complete"] = "1";
			props["datecompleted"] = parseInt((new Date()).getTime()/1000);
			props["reminder"] = "0";
		}else if(percent > 0){
			props["status"] = "1";
			props["complete"] = "-1";
		}
	} else if (complete && complete.checked) {
		props["complete"] = "1";
		props["datecompleted"] = parseInt((new Date()).getTime()/1000);
		props["status"] = "2";
	}
	
	//Owner
	var owner = dhtml.getElementById(property +"_module"+ moduleObject.id +"_owner");
	if (!owner) {
		props["owner"] = webclient.fullname;
	}
	
	if (moduleObject.itemProps[moduleObject.entryids[element.id]]) {
		props["entryid"] = moduleObject.itemProps[moduleObject.entryids[element.id]]["entryid"];
		props["parententryid"] = moduleObject.itemProps[moduleObject.entryids[element.id]]["parententryid"];
	}

	moduleObject.save(props);
	return true;
}

/**
 * Function which is called when delete button is pressed from menu
 * @param array messages list of selected messages
 * @param boolean softDelete message should be soft deleted or not (for Shift + Del)
 */
tasklistmodule.prototype.deleteMessages = function (messages, softDelete)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.entryid;

	var folder = webclient.hierarchy.getFolder(this.entryid);
	
	if (folder && (folder.rights["deleteowned"] > 0 || folder.rights["deleteany"] > 0)) {
		if (softDelete && !confirm(_("Are you sure that you want to permanently delete the selected item(s)?")))
			return;

		// pass softdelete variable value to the server
		data["softdelete"] = softDelete || false;
		
		data["entryid"] = new Array();

		if (messages.length > 0) {
			if (this.itemProps[this.entryids[messages[0]]]['taskstate'] == 2) {
				var windowData = new Object();
				windowData['subject'] = this.itemProps[this.entryids[messages[0]]]['subject'];
				windowData['parentModule'] = this;
				windowData['softDelete'] = softDelete;
				windowData['taskrequest'] = true;
				webclient.openModalDialog(this, "deletetaskoccurrence", DIALOG_URL+"task=deletetaskoccurrence_modal&entryid="+ this.entryids[messages[0]] +"&storeid="+ this.storeid +"&parententryid="+ this.entryid, 300, 220, null, null, windowData);

				messages.splice(0, 1);
			} else if (parseInt(this.itemProps[this.entryids[messages[0]]]['recurring'], 10) && !parseInt(this.itemProps[this.entryids[messages[0]]]['dead_occurrence'], 10)) {
				// if first message is recurring then ask for deleting occurrence or series.
				var windowData = new Object();
				windowData['subject'] = this.itemProps[this.entryids[messages[0]]]['subject'];
				windowData['parentModule'] = this;
				windowData['softDelete'] = softDelete;
				webclient.openModalDialog(this, "deletetaskoccurrence", DIALOG_URL+"task=deletetaskoccurrence_modal&entryid="+ this.entryids[messages[0]] +"&storeid="+ this.storeid +"&parententryid="+ this.entryid, 300, 220, null, null, windowData);

				messages.splice(0, 1);
			}

			// Anyways! delete rest of the selected messages
			for(var i = 0; i < messages.length; i++) {
				// check row is not selected for editing...
				if (this.editMessage !== this.selectedMessages[i]){
					data["entryid"].push(this.entryids[messages[i]]);
				}
			}
		}

		data["start"] = this.rowstart;
		data["rowcount"] = this.rowcount;

		if (this.sort) {
			data["sort"] = new Object();
			data["sort"]["column"] = this.sort;
		}

		if(data["entryid"].length > 0) {
			// don't send request if entryid is empty
			webclient.xmlrequest.addData(this, "delete", data);
			webclient.xmlrequest.sendRequest(true);
		}
	}
}
/**
 * Function which requests server to delete a task
 * @param boolean deleteOccurrence true if delete only occurrence else false to delete whole series
 * @param string entryid entryid of task
 * @param boolean softDelete message should be soft deleted or not (for Shift + Del)
 */
tasklistmodule.prototype.deleteMessage = function (deleteFlag, entryid, softDelete)
{
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.entryid;
	data["entryid"] = new Array(entryid);
	data["start"] = this.rowstart;
	data["rowcount"] = this.rowcount;
	data["softdelete"] = softDelete;

	if (this.sort) {
		data["sort"] = new Object();
		data["sort"]["column"] = this.sort;
	}

	data["deleteFlag"] = deleteFlag;

	if (deleteFlag == 'complete')
		data["dateCompleted"] = parseInt((new Date()).getTime()/1000);

	// don't send request if entryid is empty
	webclient.xmlrequest.addData(this, "delete", data);
	webclient.xmlrequest.sendRequest(true);
}


function getProps(element, properties, entryid)
{
	var props = new Object();
	
	// All input fields with an id
	var input = element.getElementsByTagName("input");

	// create a object to check the properties which needs to be updated.
	var checkProps = new Object();
	for(var i=0;i<properties.length;i++){
		checkProps[properties[i]["id"]] = properties[i];
	}

	for (var i = 0; i < input.length; i++) {
			//prepare xml tagname from id of input fields...
			var position = input[i].id.indexOf("_") + 1;
			var tagname = input[i].id.substr(position);
			position = tagname.indexOf("_") + 1;
			tagname = tagname.substr(position);

			// assign the properties to props only when the properties contain that tagname property.
			// check all values only if we have entryid (means we are in edit mode/ on creation mode check everything)
			if(entryid){
				if(typeof checkProps[tagname] == "undefined"){
					continue;
				}
			}

			if (input[i].id) {
				switch (input[i].type)
				{
					case "checkbox":
						if (input[i].checked) {
							props[tagname] = "1";
						} else {
							props[tagname] = "-1";
						}
						break;
					case "hidden":
					case "text":
					default:					
						props[tagname] = input[i].value;
						break;
				}
			}
		}
	return props;
}

/**
* functions for spinner buttons of percent_complete field
*/
function completeSpinnerUp(text_percent, statusid)
{		
	
	//extract percent_complete id from text_percent_complete...
	percent_complete = text_percent.substring(0,text_percent.indexOf("_"));
	tmp = text_percent.substr(text_percent.indexOf("_") + 1);
	percent_complete += "_"+ tmp.substring(0,tmp.indexOf("_"));
	tmp = tmp.substr(tmp.indexOf("_") + 1);
	tmp = tmp.substr(tmp.indexOf("_") + 1);		
	percent_complete += "_"+ tmp;
	
	var text_percent_complete = dhtml.getElementById(text_percent);
	var statusElement = dhtml.getElementById(statusid);
	
	if (text_percent_complete) {
		var percent = text_percent_complete.value;
		
		if (percent.indexOf("%") >= 0) {
			percent = percent.substring(0, percent.indexOf("%"));
		}
		
		percent = parseInt(percent);
		if (percent >= 0) {
			if(percent >= 0 && percent <= 24) {
				percent = 25;
			} else if(percent >= 25 && percent <= 49) {
				percent = 50;
			} else if(percent >= 50 && percent <= 74) {
				percent = 75;
			} else if(percent >= 75 && percent <= 100) {
				percent = 100;
				if (statusElement) statusElement.setAttribute("disabled","true");
			} else {
				percent = dhtml.getElementById(percent_complete).value * 100;
			}
			
			dhtml.getElementById(percent_complete).value = (percent / 100);
		} else {
			percent = dhtml.getElementById(percent_complete).value * 100;
		}
		
		text_percent_complete.value = percent + "%";
		
		if (statusElement) {
			if(percent == 100) {
				statusElement.value = 2;
				statusElement.checked = true;
			} else if(percent < 100 ) {
				statusElement.value = 1;			
				statusElement.checked = false;
			}
		}
	}
}

function completeSpinnerDown(text_percent,statusid)
{	
	
	//extract percent_complete id from text_percent_complete...
	percent_complete = text_percent.substring(0,text_percent.indexOf("_"));
	tmp = text_percent.substr(text_percent.indexOf("_") + 1);
	percent_complete += "_"+ tmp.substring(0,tmp.indexOf("_"));
	tmp = tmp.substr(tmp.indexOf("_") + 1);
	tmp = tmp.substr(tmp.indexOf("_") + 1);		
	percent_complete += "_"+ tmp;
	
	var text_percent_complete = dhtml.getElementById(text_percent);
	var statusElement = dhtml.getElementById(statusid);
	
	if (text_percent_complete) {
		var percent = text_percent_complete.value;
		
		if (percent.indexOf("%") >= 0) {
			percent = percent.substring(0, percent.indexOf("%"));
		}
		
		percent = parseInt(percent);
		if (percent >= 0) {
			if(percent >= 0 && percent <= 25) {
				percent = 0;
			} else if(percent >= 26 && percent <= 50) {
				percent = 25;
			} else if(percent >= 51 && percent <= 75) {
				percent = 50;
			} else if(percent >= 76 && percent <= 100) {
				percent = 75;
				if (statusElement) statusElement.removeAttribute("disabled");
			} else {
				percent = dhtml.getElementById(percent_complete).value * 100;
			}
			
			text_percent_complete.value = percent + "%";
			dhtml.getElementById(percent_complete).value = (percent / 100);
		} else {
			percent = dhtml.getElementById(percent_complete).value * 100;			
		}
		
		text_percent_complete.value = percent + "%";
		
		if (percent < 100 && statusElement) {
			statusElement.checked = false;
			statusElement.value = 0;
		}
	}
}

/**
* Changes the value of text_percent_complete field when status checkbox is clicked...
* @param string moduleid -id of the module
* @param string property -'insertprops' for insertrow, 'editprops' for editrow...
*/
function percentchange(moduleid, property)
{		
	//check whether percent_complete columns exists.
	if (dhtml.getElementById(property +"_module"+ moduleid +"_complete").checked) {
		if(dhtml.getElementById(property +"_module"+ moduleid +"_text_percent_complete")) {
			dhtml.getElementById(property +"_module"+ moduleid +"_text_percent_complete").value = "100%";
		}
		dhtml.getElementById(property +"_module"+ moduleid +"_percent_complete").value = "1";
	} else {
		if(dhtml.getElementById(property +"_module"+ moduleid +"_text_percent_complete")) {
			dhtml.getElementById(property +"_module"+ moduleid +"_text_percent_complete").value = "0%";
		}
		dhtml.getElementById(property +"_module"+ moduleid +"_percent_complete").value = "0";
	}

	dhtml.executeEvent(dhtml.getElementById("divelement"), "mousedown");
}

function categoriesInsertCallBack(categories)
{
	moduleObject = webclient.getModule(modid);
	if (dhtml.getElementById("editprops_module"+ modid +"_categories")) {
		moduleObject.filtercategories(dhtml.getElementById("editprops_module"+ modid +"_categories"), categories);
		return;
	}
	moduleObject.filtercategories(dhtml.getElementById("insertprops_module"+ modid +"_categories"), categories);
}

/**
* Passes categories from input field to dialog window...
*@param string 	fieldid		-id of category field
*@param integer	moduleID	-id of module
*/
function eventcategoriesToWindow (fieldid, moduleID)
{
	var categoriesElement = dhtml.getElementById(fieldid);
	var moduleObject = webclient.getModule(moduleID);

	var categoryData = new Object();
	categoryData["categories"] = categoriesElement.value;	
	
	webclient.openModalDialog(moduleObject, "categories", DIALOG_URL +"task=categories_modal", 350, 370, categoriesInsertCallBack, null, categoryData);
}

tasklistmodule.prototype.item = function(action)
{
	this.setDateOnlyFields(action);
	tasklistmodule.superclass.item.call(this, action);
}

function eventTaskListContextMenuMessageAssignTask(moduleObject, element, event)
{
	element.parentNode.style.display = "none";

	var messages = moduleObject.getSelectedMessages();

	webclient.openWindow(moduleObject, "task", DIALOG_URL+"task=task_standard&storeid=" + moduleObject.storeid +"&parententryid="+ moduleObject.entryid +"&entryid="+ moduleObject.entryids[messages[0]] +"&taskrequest=true");
}
