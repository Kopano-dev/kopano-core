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

appointmentitemmodule.prototype = new ItemModule;
appointmentitemmodule.prototype.constructor = appointmentitemmodule;
appointmentitemmodule.superclass = ItemModule.prototype;

function appointmentitemmodule(id)
{
	if(arguments.length > 0) {
		this.init(id);
	}

	// The element ID's of recurrence information
	this.recurids = new Array('recurring', 'startocc', 'endocc', 'start', 'end', 'term', 'regen', 'everyn', 'subtype', 'type', 'weekdays', 'month', 'monthday', 'nday', 'numoccur');
}

appointmentitemmodule.prototype.init = function(id)
{
	appointmentitemmodule.superclass.init.call(this, id);

	this.keys["edit_item"] = KEYS["edit_item"];
	this.keys["respond_meeting"] = KEYS["respond_meeting"];
	this.keys["mail"] = KEYS["mail"];

	// Add keycontrol events
	webclient.inputmanager.addObject(module);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["edit_item"], "keyup", eventAppointmentItemKeyCtrlEdit);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["respond_meeting"], "keyup", eventAppointmentItemKeyCtrlRespond);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["mail"], "keyup", eventAppointmentItemKeyCtrlSubmit);
}

appointmentitemmodule.prototype.executeOnLoad = function()
{
	initAppointment();
	showRecurrence();
	
	var meetingStatus = parseInt(this.itemProps.meeting, 10);
	var responseStatus = parseInt(this.itemProps.responsestatus, 10);
	
	// Show tracking tab only when appointment is a meeting request
	// and it is in organizer's calendar
	if(!isNaN(meetingStatus) && meetingStatus == olMeeting) {
		var tab_tracking = dhtml.getElementById("tab_tracking");
		if(!isNaN(responseStatus) && responseStatus == olResponseOrganized) {
			dhtml.removeClassName(tab_tracking, "tab_hide");
		} else {
			dhtml.addClassName(tab_tracking, "tab_hide");
		}
	}

	if (this.counterProposal) {
		if (this.viewAllProposals) {
			dhtml.executeEvent(dhtml.getElementById('tab_scheduling'), 'click');
		} else {
			initDate(new Date(this.proposedStartDate * 1000), new Date(this.proposedEndDate * 1000));
		}
	}
}

appointmentitemmodule.prototype.setStartTime = function(unixtime)
{
	dhtml.getElementById("startdate").setAttribute("unixtime",unixtime);
	dhtml.getElementById("commonstart").setAttribute("unixtime",unixtime);
	appoint_dtp.setStartValue(unixtime);
}

appointmentitemmodule.prototype.setEndTime = function(unixtime)
{
	dhtml.getElementById("duedate").setAttribute("unixtime",unixtime);
	dhtml.getElementById("commonend").setAttribute("unixtime",unixtime);
	appoint_dtp.setEndValue(unixtime);
}

/**
 * Function will create and allday event and show/hide time element of date pickers
 * in the appointment tab
 * @param boolean allday event flag
 */
appointmentitemmodule.prototype.setAllDayEvent = function(allDayEventFlag)
{
	dhtml.getElementById("checkbox_alldayevent").checked = allDayEventFlag;
	if(allDayEventFlag){
		appoint_dtp.startPicker.timeElement.hide();
		appoint_dtp.endPicker.timeElement.hide();
	}else{
		appoint_dtp.startPicker.timeElement.show();
		appoint_dtp.endPicker.timeElement.show();
	}
}
/**
 * Function will set the location of the meeting in the appointment
 * @param string resource
 */
appointmentitemmodule.prototype.setLocation = function(resource)
{
	if(resource){
		var location = dhtml.getElementById("location").value;
		if(!location){
			dhtml.getElementById("location").value = resource;
		}else if (location != resource){
			if(confirm(_("Do you want to update the location '%s' with new location '%s'").sprintf(location, resource)))
				dhtml.getElementById("location").value = resource;
			else
				dhtml.getElementById("location").value = location;
		}
	}
}

appointmentitemmodule.prototype.getStartTime = function()
{
	return appoint_dtp.getStartValue();
}

appointmentitemmodule.prototype.getEndTime = function()
{
	return appoint_dtp.getEndValue();
}

/**
 * Function which will return the all day status of an appointment
 */
appointmentitemmodule.prototype.isAllDayEvent = function()
{
	return dhtml.getElementById("checkbox_alldayevent").checked;
}

/**
 * Function will return the selected users
 * @return Array[]->entryid
 *                ->fullname
 *                ->emailaddress
 */ 
appointmentitemmodule.prototype.getUserList = function()
{
	var result = new Array();
	var index = 0;
	result[index] = this.owner;
	index++;

	var userList = dhtml.getElementById("to").value.split(";");
	var organizerFound = false;
	if(dhtml.getElementById("to").value.length > 0){
		for(user in userList){
			if(userList[user].trim().length == 0)
				continue;

			result[index] = stringToEmail(userList[user].trim());
			result[index]["recipienttype"] = MAPI_TO;
			result[index]["recipient_flags"] = 1;	//recipient flag for attendees.

			index++;
		}
	}

	var userList = dhtml.getElementById("cc").value.split(";");
	if(dhtml.getElementById("cc").value.length > 0){
		for(user in userList){
			if(userList[user].trim().length == 0)
				continue;
				
			result[index] = stringToEmail(userList[user].trim());
			result[index]["recipienttype"] = MAPI_CC;
			result[index]["recipient_flags"] = 1;
			index++;
		}
	}

	var userList = dhtml.getElementById("bcc").value.split(";");
	if(dhtml.getElementById("bcc").value.length > 0){
		for(user in userList){
			if(userList[user].trim().length == 0)
				continue;
				
			result[index] = stringToEmail(userList[user].trim());
			result[index]["recipienttype"] = MAPI_BCC;
			index++;
		}
	}

	return result;
}

/**
 * Function will create the users in this given list
 * @param  Object[]->entryid
 *                 ->fullname
 *                 ->emailaddress 
 */ 
appointmentitemmodule.prototype.setUserList = function(inputUserlist)
{
	var result = "";
	var resultTO = "";
	var resultCC = "";
	var resultBCC = "";
	for(user in inputUserlist){
		var userData = inputUserlist[user];
		if(userData["recipient_flags"] == 3){
			if(!this.owner){
				this.owner = userData;
			}
		}else{
			/**
			 * If e-mail address is an external e-mail address.
			 * Conditions for external e-mail address
			 * 1) If entryid is not there for userdata.
			 * 2) If fullname and emailaddress are same.
			 * 3) If fullname and entryid are same. 
			 * (When userdata is set from freebusy module then 
			 * at that time entryid for external email address contains external e-mail address.)
			 */
			if(userData["entryid"].length == 0 || (userData["fullname"] == userData["emailaddress"]) || (userData["fullname"] == userData["entryid"])){
				switch(userData["recipienttype"]){
					case MAPI_CC:
						resultCC += userData["fullname"]+"; ";
						break;
					case MAPI_BCC:
						resultBCC += userData["fullname"]+"; ";
						break;
					case MAPI_ORIG:
						var emailaddress = this.itemProps["sent_representing_email_address"] || userData["emailaddress"];
						resultTO += nameAndEmailToString(userData["fullname"], emailaddress, MAPI_MAILUSER, false) + "; ";
						break;
					default:
						resultTO += nameAndEmailToString(userData["fullname"], userData["emailaddress"], userData["objecttype"], false)  + "; ";
						break;
				}
				result += nameAndEmailToString(userData["fullname"], userData["emailaddress"], userData["objecttype"], false)  + "; ";
			}
			else{
				switch(userData["recipienttype"]){
					case MAPI_CC:
						resultCC += nameAndEmailToString(userData["fullname"], userData["emailaddress"], userData["objecttype"], false) + "; ";
						break;
					case MAPI_BCC:
						resultBCC += nameAndEmailToString(userData["fullname"], userData["emailaddress"], userData["objecttype"], false) + "; "; 
						break;
					case MAPI_ORIG:
						var emailaddress = this.itemProps["sent_representing_email_address"] || userData["emailaddress"];
						resultTO += nameAndEmailToString(userData["fullname"], emailaddress, MAPI_MAILUSER, false) + "; ";
						break;
					default:
						resultTO += nameAndEmailToString(userData["fullname"], userData["emailaddress"], userData["objecttype"], false) +  "; "; 
						break;
				}
				result += nameAndEmailToString(userData["fullname"], userData["emailaddress"], userData["objecttype"], false) + "; ";
			}
		}
	}
	dhtml.getElementById("toccbcc").value = result;
	dhtml.getElementById("to").value = resultTO;
	dhtml.getElementById("cc").value = resultCC;
	dhtml.getElementById("bcc").value = resultBCC;
	this.oldToValue = result;
}


appointmentitemmodule.prototype.getRecurrence = function()
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

appointmentitemmodule.prototype.setRecurrence = function(recurrence)
{
	if(recurrence) {

		for(i=0;i < this.recurids.length; i++) {
			dhtml.getElementById(this.recurids[i]).value = recurrence[this.recurids[i]];		
		}
	
		dhtml.getElementById('recurring').value = 1;
		dhtml.getElementById('recurring_reset').value = 1;
		dhtml.getElementById('icon_index').value = 1025;
		
		//Set appointment as alldayevent
		if (recurrence["alldayevent"] && recurrence["alldayevent"] == 1) {
			//Set startdate and enddate
			dhtml.setValue(dhtml.getElementById("checkbox_alldayevent"), true);
			onChangeAllDayEvent();
			dhtml.getElementById("endocc").value = 1440;
			dhtml.getElementById("startocc").value = 0;
			dhtml.getElementById("duration").value = 1440;
		}else if(this.isAllDayEvent()){
			/**
			 * this check is to unset allday event in appointment dialog, in case when the appointment
			 * dialog has all day event check but in recurring dialog user has unchecks 
			 * all day checkbox, thus we would like to propogate this change to appointment dialog
			 */
			dhtml.setValue(dhtml.getElementById("checkbox_alldayevent"), false);
			onChangeAllDayEvent();
		}
	} else {
		dhtml.getElementById('recurring').value = 0;
		dhtml.getElementById('icon_index').value = 1024;
		dhtml.getElementById('recurring_reset').value = "";
		dhtml.getElementById('recurtext').innerHTML = "";
		dhtml.getElementById('recur_type').value = 0;
	}

	showRecurrence();
}

appointmentitemmodule.prototype.setTimezone = function(tz)
{
	var elems = new Array('timezone','timezonedst','dststartmonth', 'dststartweek', 'dststartday', 'dststarthour',
							'dstendmonth','dstendweek', 'dstendday', 'dstendhour');
	if(tz) {
		for(var i=0; i< elems.length; i++) {
			dhtml.getElementById(elems[i]).value = tz[elems[i]];
		}
	}
}

// If send is true, send a cancellation
appointmentitemmodule.prototype.deleteMessage = function (basedate)
{
	var send = false;
	var meeting = parseInt(this.itemProps["meeting"], 10);
	var recurring = parseInt(this.itemProps["recurring"], 10);
	var responseStatus = parseInt(this.itemProps["responsestatus"], 10);
	var isMeetingOrganized = responseStatus == olResponseOrganized;
	var isMeetingAttendee = responseStatus != olResponseNone && responseStatus != olResponseOrganized;

	if(basedate) {
		//occurrence end date
		var end = dhtml.getElementById("duedate").getAttribute("unixtime");
		var endtime = new Date(end * 1000);
		
		if (meeting !== 0 && !isMeetingInPast(endtime) && !isMeetingCanceled(this.itemProps)) {
			/**
			 * if occurrence is later then send confirmation message for cancellation message.
			 * Check whether there are any recepients/resource in MR or not.
			 */
			if (isMeetingOrganized && this.itemProps.requestsent && this.itemProps.requestsent == "1") {
				/**
				 * with FF6 and above we have a problem of focus if a confirm box is called from parentwidow 
				 * when another dialog is opened, so to get the focus back to correct window/dialog we do this call
				 */
				window.focus();
				send = confirm(_("Would you like to send an update to the attendees regarding changes to this meeting?"));
			} else if (!isMeetingOrganized){
				sendConfirmationDeleteAppointmentItem(this, this.itemProps["subject"], basedate);
				return true;
			}
		} else {
			//directlly delete passed occurrences.
			var req = new Object;
			req['store'] = this.storeid;
			req['parententryid'] = this.parententryid;
			req['entryid'] = this.messageentryid;
			req['delete'] = 1;
			req['props'] = new Object;
			req['props']['entryid'] = this.messageentryid;
			req['props']['basedate'] = parseInt(basedate, 10);

			parentWebclient.xmlrequest.addData(this, 'save', req, webclient.modulePrefix);
			parentWebclient.xmlrequest.sendRequest(true);
		}

		if(send) {
			// delete occurrence and send cancellation message
			var data = new Array();
			data["store"] = this.storeid;
			data["entryid"] = this.messageentryid;
			data["delete"] = true;
			data["exception"] = true;
			data["basedate"] = basedate;
			
			parentWebclient.xmlrequest.addData(this, "cancelInvitation", data, webclient.modulePrefix);
			parentWebclient.xmlrequest.sendRequest(true);
		}
	} else {
		var end = 0;
		if (recurring == 1) {
			// Recurrence end date.
			end = this.itemProps.enddate_recurring;
		} else {
			// Normal meeting end date.
			end = this.itemProps.duedate;
		}

		var endtime = new Date(end * 1000);

		// if recurrence is later then send confirmation message for cancellation message.
		if(meeting !== 0 && !isMeetingInPast(endtime) && !isMeetingCanceled(this.itemProps)) {
			// Check whether there are any recepients in MR or not.
			if (isMeetingOrganized && this.itemProps.requestsent && this.itemProps.requestsent == "1" ) {
				send = confirm(_("Would you like to send an update to the attendees regarding changes to this meeting?"));
			} else if (isMeetingAttendee){
				sendConfirmationDeleteAppointmentItem(this, this.itemProps["subject"]);
				return true;
			}
		} else {
			//directlly delete passed MRs.
			appointmentitemmodule.superclass.deleteMessage.call(this);
		}

		if(send) {
			// delete MR and send cancellation message
			var data = new Array();
			data["store"] = this.storeid;
			data["entryid"] = this.messageentryid;

			parentWebclient.xmlrequest.addData(this, "cancelInvitation", data, webclient.modulePrefix);
			parentWebclient.xmlrequest.sendRequest(true);
		}
	}

	return false;
}

appointmentitemmodule.prototype.saveMessage = function (props, send, recipients, dialog_attachments)
{
	appointmentitemmodule.superclass.saveMessage.call(this, props, send, recipients, dialog_attachments);
}

appointmentitemmodule.prototype.execute = function (type, action)
{
	switch(type)
	{
		case "item":
			this.item(action);

			/** 
			 * If MR is recurring then attandees are not allowed
			 * to propose new time on whole reccurrence.
			 * http://msdn.microsoft.com/en-us/library/ee217972%28EXCHG.80%29.aspx
			 */
			var recurring = parseInt(dhtml.getXMLValue(action, "recurring", 0), 10);
			var basedate = parseInt(dhtml.getXMLValue(action, "basedate", 0), 10);
			var proposeNewTimeElem = dhtml.getElementById("proposenewtime");

			if(recurring && !basedate && proposeNewTimeElem)
				proposeNewTimeElem.style.display = "none"; 

			webclient.menu.showMenu();
			break;
		case "saved":
			this.messageSaved(action);
			break;
		case "deleted":
			this.messageDeleted(action);
			break;
		case "error":
			this.handleError(action);
			break;
		case "convert_item":
			this.setBodyFromItemData(action);
			break;
		case "getAttachments":		// Uploaded Attachment list.
			this.attachmentsList(action);
			break;
	}
}

/**
 * Function which saves an item.
 * @param object props the properties to be saved
 * @param string dialog_attachments used to add attachments (optional)   
 */ 
appointmentitemmodule.prototype.save = function(props, send, recipients, dialog_attachments)
{
	var data = new Object();
	if(this.storeid) {
		data["store"] = this.storeid;
	}

	if(this.isMeeting == true){
		// Add human readable BODY Text.
		props["meetingTimeInfo"] = this.addMeetingTimeInfoToBody(props["body"], props['startdate'], props['duedate'], props['location'], props["recurring"] ? props["recurring_pattern"] : false);
	}

	if(this.parententryid)
		data["parententryid"] = this.parententryid;
		
	data["props"] = props;
	if (recipients){
		data["recipients"] = recipients;
		if(this.owner){
			data["recipients"]["recipient"].unshift({
				"address": this.owner["email_address"],
				"name": this.owner["fullname"],
				"type": "mapi_to",
				"recipient_flags": 3 /* recipSenable | recipOrganizer */
			});
		}
	}

	if (dialog_attachments)
		data["dialog_attachments"] = dialog_attachments;
	
	if(this.message_action) {
		data["message_action"] = new Object();
		data["message_action"]["action_type"] = this.message_action;
		data["message_action"]["entryid"] = this.message_action_entryid;
	}
	
	if(send) {
		data["send"] = true;
	}

	//webclient.xmlrequest.addData(this, "save", data);
	//webclient.xmlrequest.sendRequest();
//TODO: update main webclient??
	if(typeof(parentWebclient) != "undefined") {
		parentWebclient.xmlrequest.addData(this, "save", data, webclient.modulePrefix);
		/**
		 * We dont want to wait while saving the message,
		 * b,couz if we are also sending response to meeting request
		 * then saving should be done prior to sending response.
		 */
		parentWebclient.xmlrequest.sendRequest(false);
//TODO: remove BETA quick fix
	}else{
		webclient.xmlrequest.addData(this, "save", data); 
		webclient.xmlrequest.sendRequest();
	}
}


appointmentitemmodule.prototype.messageSaved = function(action)
{
	if(Number(dhtml.getXMLValue(action, "meeting_request_saved"))){
		if(Number(dhtml.getXMLValue(action, "sent_meetingrequest"))){
			// Check if resources are planned in this appointment
			var userlist = this.getUserList();
			var resourcesPlanned = false;
			for(var i=0;i<userlist.length;i++){
				if(userlist[i]["recipienttype"] == MAPI_BCC){
					resourcesPlanned = true;
					break;
				}
			}
			if(resourcesPlanned && Number(dhtml.getXMLValue(action, "direct_booking_enabled"))){
				alert(_("Resources have been planned."));
			}
		}
		/**
		 * Dont close window, untill changes in meeting
		 * request are saved and response is send,
		 * while accepting/tentatively meeting request.
		 */
		if (!module.savingMeetingRequest) {
			window.close();
		}
	}else if(Number(dhtml.getXMLValue(action, "errorcode"))){
		var name = (dhtml.getXMLValue(action, "displayname"))?dhtml.getXMLValue(action, "displayname"):'(Unknown)';
		/**
		 * Error codes:
		 * 1: No access/permissions.
		 * 2: Resource does not automatically accept meeting requests (disabled atm, mail is sent instead).
		 * 3: Resource declines recurring meeting requests.
		 * 4: Resource declines conflicting meeting requests.
		 */
		switch(Number(dhtml.getXMLValue(action, "errorcode"))){
			case 1:
				alert(_("You marked \"%s\" as a resource. You cannot schedule a meeting with \"%s\" because you do not have the appropiate permissions for that account. Either enter the name as a required or optional attendee or talk to your administrator about giving you permission to schedule \"%s\".").sprintf(name, name, name));
				break;
			case 2:
				alert(_("\"%s\" has declined your meeting because \"%s\" does not automatically accept meeting requests. ").sprintf(name, name));
				break;
			case 3:
				alert(_("\"%s\" has declined your meeting because it is recurring. You must book each meeting separetly with this recurrence.").sprintf(name));
				break;
			case 4:
				alert(_("\"%s\" is already booked for this specified time. You must use another time or find another resource.").sprintf(name));
				break;
			default:
				alert(_("Meeting was not scheduled."));
				break;
		}
	}else if(dhtml.getXMLValue(action, "remindertime", false)){
		var errorMessageString = dhtml.getXMLValue(action, "error_message", false);
		if(errorMessageString)
			alert(errorMessageString);
	}else if(dhtml.getXMLValue(action, "proposetime", false)){
		var errorMessageString = dhtml.getXMLValue(action, "error_message", false);
		if (errorMessageString) 
			alert(errorMessageString);
	}

	module.saving = false;
	module.savingMeetingRequest = false;
}

/**
 * Called when a deleted action was successful. Will close the dialog.
 * @param {XMLElement} action Response from server
 */
appointmentitemmodule.prototype.messageDeleted = function(action)
{
	window.close();
}