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

taskitemmodule.prototype = new ItemModule;
taskitemmodule.prototype.constructor = taskitemmodule;
taskitemmodule.superclass = ItemModule.prototype;

function taskitemmodule(id)
{
	if(arguments.length > 0) {
		this.init(id);
	}

	// The element ID's of recurrence information
	this.recurids = new Array('recurring', 'startocc', 'endocc', 'start', 'end', 'term', 'regen', 'everyn', 'subtype', 'type', 'weekdays', 'month', 'monthday', 'nday', 'numoccur');
}

taskitemmodule.prototype.init = function(id)
{
	taskitemmodule.superclass.init.call(this, id);
}

taskitemmodule.prototype.executeOnLoad = function()
{
	initTask();
	showTaskRecurrence();
	if (typeof window.taskrequest != "undefined" && window.taskrequest == "true")
		setTaskRequestMode(tdsOWNNEW, tdmtTaskReq);
	else
		setTaskRequestMode();
	resizeBody();

	// Add keycontrol event
	webclient.inputmanager.addObject(this);
	webclient.inputmanager.bindKeyControlEvent(this, KEYS["mail"], "keyup", eventTaskListKeyCtrlSave);
}

taskitemmodule.prototype.item = function(action)
{
	var dateFields = ["duedate", "startdate", "datecompleted"];

	for(var j=0; j<dateFields.length; j++){
		var dateElements = action.getElementsByTagName(dateFields[j]);
		for(var i=0; i<dateElements.length; i++){
			if (dateElements[i].attributes && dateElements[i].attributes["unixtime"]){
				dateElements[i].firstChild.nodeValue = strftime_gmt(_("%a %x"), dateElements[i].firstChild.nodeValue);
			}
		}
	}
	// set the reminder's default value, which will be used later 
	// for validating the marking a task (in)/complete and turning on/off reminders.
	dhtml.getElementById("previousReminderValue").value = dhtml.getXMLValue(action, "reminder", "0");

	taskitemmodule.superclass.item.call(this, action);
	this.setDisabledProperties();
}

taskitemmodule.prototype.getRecurrence = function()
{
	var recurrence = new Object;

	for(i=0;i < this.recurids.length; i++) {		
		recurrence[this.recurids[i]] = parseInt(dhtml.getElementById(this.recurids[i]).value);
	}
	
	//Set allday_event flag to show it in recurrence dialog
	if (parseInt(dhtml.getElementById("endocc").value, 10) == 1440){
		recurrence["allday_event"] = 1;
	}
	
	return recurrence;
}


taskitemmodule.prototype.setRecurrence = function(recurrence)
{
	if (recurrence) {

		for(i=0;i < this.recurids.length; i++) {
			dhtml.getElementById(this.recurids[i]).value = recurrence[this.recurids[i]];		
		}
	
		dhtml.getElementById('recurring').value = 1;
		dhtml.getElementById('recurring_reset').value = 1;
		dhtml.getElementById('icon_index').value = 1281;

		var startdate = Date.parseDate(dhtml.getElementById("text_commonstart").value, _("%d-%m-%Y"), true, true);
		var duedate = Date.parseDate(dhtml.getElementById("text_commonend").value, _("%d-%m-%Y"), true, true);

		if (startdate) {
			var duration = (duedate ? duedate.getTime() - startdate.getTime() : startdate.getTime())/1000;
			dhtml.getElementById("text_commonstart").value = strftime(_("%d-%m-%Y"), recurrence['start']);
			dhtml.getElementById('startdate').value = recurrence['start'];
			dhtml.getElementById('commonstart').value = recurrence['start'];

			dhtml.getElementById("text_commonend").value = strftime(_("%d-%m-%Y"), recurrence['start'] + duration);
			dhtml.getElementById('duedate').value = recurrence['start'] + duration;
			dhtml.getElementById('commonend').value = recurrence['start'] + duration;
		} else {
			dhtml.getElementById("text_commonend").value = strftime(_("%d-%m-%Y"), recurrence['start']);
			dhtml.getElementById('duedate').value = recurrence['start'];
			dhtml.getElementById('commonend').value = recurrence['start'];
		}
	} else {
		dhtml.getElementById('recurring').value = 0;
		dhtml.getElementById('icon_index').value = 1080;
		dhtml.getElementById('recurring_reset').value = "";
	}

	showTaskRecurrence();
	resizeBody();
}

/**
 * Function which deletes an item 
 */ 
taskitemmodule.prototype.deleteMessage = function(deleteFlag)
{
	if(this.messageentryid) {
		var data = new Object();
		data["store"] = this.storeid;
		data["parententryid"] = this.parententryid;
		data["entryid"] = this.messageentryid;
		data["deleteFlag"] = deleteFlag;

		if (deleteFlag == 'complete')
		data["dateCompleted"] = parseInt((new Date()).getTime()/1000);

		/**
		 * This function is also called from main window,
		 * so need to perform typeof() check.
		 */
		if(typeof(parentWebclient) != "undefined") {
			parentWebclient.xmlrequest.addData(this, "delete", data, webclient.modulePrefix);
			parentWebclient.xmlrequest.sendRequest(true);
		} else {
			webclient.xmlrequest.addData(this, "delete", data);
			webclient.xmlrequest.sendRequest();
		}

		window.close();
	}
}

taskitemmodule.prototype.resize = function()
{
	var html_body = dhtml.getElementById("disabled_html_body");

	if(html_body) {
		var height = document.documentElement.clientHeight - dhtml.getElementTop(html_body);

		if(height < 50) {
			height = 50;
		}

		var width = html_body.parentNode.offsetWidth;
		if(width < 50) {
			width = 50;
		}

		html_body.style.height = (height - 10) + "px";
		html_body.style.width = "100%";
	}
}

taskitemmodule.prototype.setDisabledProperties = function()
{
	dhtml.addTextNode(dhtml.getElementById("disabled_subject"), dhtml.getElementById("subject").value);
	dhtml.addTextNode(dhtml.getElementById("disabled_owner"), dhtml.getElementById("owner").value);

	// Duedate
	var duedate = dhtml.getElementById("text_commonend").value;
	var duedate_text = "";
	if (duedate.toLowerCase() != "none") {
		var startdate = dhtml.getElementById("text_commonstart").value;
		if (startdate.toLowerCase() != "none") duedate_text += _("Starts on %s").sprintf(startdate) + ", ";

		if (duedate_text.length == 0) duedate_text += _("Due on %s").sprintf(duedate);
		else duedate_text += _("due on %s").sprintf(duedate);
	} else {
		duedate_text += _("None");
	}
	dhtml.addTextNode(dhtml.getElementById("disabled_duedate"), duedate_text);

	// Status
	var select = dhtml.getElementById("select_status");
	var status = dhtml.getElementById("status").value;
	for(var i=0; i < select.options.length; i++) {
		if (select.options[i].value == parseInt(status, 10)){
			dhtml.addTextNode(dhtml.getElementById("disabled_status"), select.options[i].text);
		}
	}

	// Importance
	var select = dhtml.getElementById("select_priority");
	var priorty = dhtml.getElementById("importance").value;
	for(var i=0; i < select.options.length; i++) {
		if (select.options[i].value == parseInt(priorty, 10)){
			dhtml.addTextNode(dhtml.getElementById("disabled_priority"), select.options[i].text);
		}
	}

	// Percent Complete
	var percent_complete = dhtml.getElementById("percent_complete");
	dhtml.addTextNode(dhtml.getElementById("disabled_percentcomplete"), (percent_complete.value * 100) + "%");

	// Body
	var html_body = dhtml.getElementById("html_body");
	if (html_body) dhtml.getElementById("disabled_html_body").value = html_body.value;
}

taskitemmodule.prototype.reclaimOwnerShip = function(entryid)
{
	var data = new Object();

	if(this.storeid) data["store"] = this.storeid;
	if(this.parententryid) data["parententryid"] = this.parententryid;

	data["props"] = new Array();
	data["props"]["entryid"] = this.messageentryid;

	if(parentWebclient) {
		parentWebclient.xmlrequest.addData(this, "reclaimownership", data, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest(true);
	} else {
		webclient.xmlrequest.addData(this, "reclaimownership", data);
		webclient.xmlrequest.sendRequest();
	}
}