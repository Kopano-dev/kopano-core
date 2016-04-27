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

function initAppointment()
{
	// Label
	dhtml.setValue(dhtml.getElementById("select_label"), dhtml.getElementById("label").value);
	
	// All Day Event
	if(dhtml.getElementById("alldayevent").value == "1") {
		dhtml.setValue(dhtml.getElementById("checkbox_alldayevent"), true);
		onChangeAllDayEvent();
		
		// Check if "all day event" is for 1 day, set duedate the same as startdate
		var duedate = dhtml.getElementById("duedate").getAttribute("unixtime");
		var startdate = dhtml.getElementById("startdate").getAttribute("unixtime");
		
		if(duedate) {
			dhtml.getElementById("duedate").setAttribute("unixtime", duedate-86400);
		}
	}

	// Busy Status
	dhtml.setValue(dhtml.getElementById("select_busystatus"), dhtml.getElementById("busystatus").value);
	
	// Start Date/Time
	appoint_dtp.setStartValue(dhtml.getElementById("startdate").getAttribute("unixtime"));
	dhtml.getElementById("startdate").setAttribute("unixtime",appoint_dtp.getStartValue());	
	
	// End Date/Time
	appoint_dtp.setEndValue(dhtml.getElementById("duedate").getAttribute("unixtime"));
	dhtml.getElementById("duedate").setAttribute("unixtime",appoint_dtp.getEndValue());
	
	// Reminder
	if(dhtml.getElementById("reminder").value == "1") {
		dhtml.setValue(dhtml.getElementById("checkbox_reminder"), true);
		onChangeReminder();
	}

	// Reminder Minutes Before Start
	var minutes = parseInt(dhtml.getElementById("reminder_minutes").value);
	if(minutes >= 0) {
		var reminderMinElem = dhtml.getElementById("select_reminder_minutes");
		var result = dhtml.setValue(reminderMinElem, minutes);

		// If no value has been set find the closed bigger value and select that option
		if(!result){
			var closeValueFound = false;
			for(var i = 0; i < reminderMinElem.options.length; i++){
				// If the listed reminder time is bigger than use that value and break out of the loop
				if(reminderMinElem.options[i].value > minutes){
					reminderMinElem.options[i].selected = true;
					closeValueFound = true;
					break;
				}
			}
			// If no value has been found, so select the biggest value from the list, which is the last one
			if(!closeValueFound){
				reminderMinElem.options[reminderMinElem.options.length-1].selected = true;
			}
		}
	}
	
	// Private
	if(dhtml.getElementById("sensitivity").value == "2") {
		dhtml.setValue(dhtml.getElementById("checkbox_private"), true);
	}
	
	// Importance
	setImportance(parseInt(dhtml.getElementById("importance").value));

	var responseStatus = parseInt(dhtml.getElementById("responsestatus").value);

	if(dhtml.getElementById("basedate").value.length > 0) {
		meetingRequestSetup(responseStatus);

		// Exception, turn off recurrence button
		dhtml.getElementById("seperator1").style.display = "none";
		dhtml.getElementById("seperator2").style.display = "none";
		dhtml.getElementById("recurrence").style.display = "none";
		// Disable the following fields as they are only editable in the series object or in non-recurring appointments
		dhtml.getElementById("contacts").disabled =true;
		dhtml.getElementById("categories").disabled =true;
		dhtml.getElementById("checkbox_private").disabled =true;
	} else if(dhtml.getElementById("to").value.length > 0 || dhtml.getElementById("cc").value.length > 0 || dhtml.getElementById("bcc").value.length > 0) {
		meetingRequestSetup(responseStatus);
	} else if(responseStatus == olResponseOrganized && dhtml.getElementById("meeting").value == olMeeting) {
		meetingRequestSetup(responseStatus);
	} else {
		meetingRequestSetup(mrSetupNormal);
	}

	// Set visible cumulative field
	var toFld = dhtml.getElementById("to").value;
	var ccFld = dhtml.getElementById("cc").value;
	var bccFld = dhtml.getElementById("bcc").value;

	// Set all values in array.
	var toccbccArray = new Array();
	if(toFld)
		toccbccArray.push(toFld);
	if(ccFld)
		toccbccArray.push(ccFld);
	if(bccFld)
		toccbccArray.push(bccFld);

	// Set toccbcc field value by joining array values.
	dhtml.getElementById("toccbcc").value = toccbccArray.join("; ");

	if(typeof module.itemProps.recipients != "undefined" && typeof module.itemProps.recipients.recipient != "undefined"){
		var tableData = new Array();
		var durationMinutes = Math.floor((module.itemProps.duedate - module.itemProps.startdate)/60);
		var durationHours = Math.floor(durationMinutes/60);
		durationMinutes = durationMinutes % 60;
		tableData.push(
		{
			name: {
				innerHTML: "&lt;"+_("Current Meeting Time")+"&gt;"
			},
			starttime: {
				innerHTML: (new Date(module.itemProps.startdate*1000)).strftime(_("%d-%m-%Y %H:%M")),
				timestamp: module.itemProps.startdate
			},
			endtime: {
				innerHTML: (new Date(module.itemProps.duedate*1000)).strftime(_("%d-%m-%Y %H:%M")),
				timestamp: module.itemProps.duedate
			},
			duration: {
				innerHTML: 
					((durationHours>0)?durationHours+" "+((durationHours==1)?_("hour"):_("hours"))+" ":"") + 
					((durationMinutes>0)?durationMinutes+" "+((durationMinutes==1)?_("minute"):_("minutes")):""),
				duration: module.itemProps.duedate - module.itemProps.startdate
			},
			conflicts: {
				innerHTML: ""
			}
		});
		var numProposingAttendees = 0;
		if(module.itemProps.recipients.recipient.length > 0){
			var recipientsList = module.itemProps.recipients.recipient;
		}else{
			var recipientsList = Array();
			recipientsList[0] = module.itemProps.recipients.recipient;
		}
		for(var i=0;i<recipientsList.length;i++){
			var recipient = recipientsList[i];
			// Check if the recipient is the organiser
			if(parseInt(recipient["proposednewtime"],10)){
				numProposingAttendees++;
				var durationMinutes = Math.floor((recipient["proposenewendtime"] - recipient["proposenewstarttime"])/60);
				var durationHours = Math.floor(durationMinutes/60);
				durationMinutes = durationMinutes % 60;
				tableData.push(
				{
					name: {
						innerHTML: recipient["display_name"]
					},
					starttime: {
						innerHTML: (new Date(recipient["proposenewstarttime"]*1000)).strftime(_("%d-%m-%Y %H:%M")),
						timestamp: recipient["proposenewstarttime"]
					},
					endtime: {
						innerHTML: (new Date(recipient["proposenewendtime"]*1000)).strftime(_("%d-%m-%Y %H:%M")),
						timestamp: recipient["proposenewendtime"]
					},
					duration: {
						innerHTML: 
							((durationHours>0)?durationHours+" "+((durationHours==1)?_("hour"):_("hours"))+" ":"") + 
							((durationMinutes>0)?durationMinutes+" "+((durationMinutes==1)?_("minute"):_("minutes")):""),
						//	((durationMinutes>0)?durationMinutes+" "+_("minutes")+" ":"")
						//innerHTML: (recipient["proposenewendtime"] - recipient["proposenewstarttime"])/60,
						duration: recipient["proposenewendtime"] - recipient["proposenewstarttime"]
					},
					conflicts: {
						innerHTML: ""
					}
				});
			}
		}

		// Set info text about number of attendees that proposed a new time and create list of proposed new times
		if(numProposingAttendees > 0){
			var meetingrequestResponseElement = dhtml.getElementById("meetingrequest_responses");
			var textMeetingrequestResponse = "";
			if(numProposingAttendees == 1){
				textMeetingrequestResponse += _("%d attendee proposed a new time for this meeting. Click the Scheduling tab for details.").sprintf(numProposingAttendees);
			}else{
				textMeetingrequestResponse += _("%d attendees proposed a new time for this meeting. Click the Scheduling tab for details.").sprintf(numProposingAttendees);
			}
			meetingrequestResponseElement.style.display = "block";
			dhtml.addTextNode(meetingrequestResponseElement, textMeetingrequestResponse);


			// Create a list of the propose new times using the tableWidget
			var tableWidgetElem = dhtml.addElement(dhtml.getElementById("appointment_freebusy_proposenewtime_container"), "div", false, "tableWidgetContainer");
			tableWidgetElem.style.height = "100%";

			this.propNewTime_tableWidget = new TableWidget(tableWidgetElem, false);
			this.propNewTime_tableWidget.addColumn("starttime", _("Proposed start date and time"), 200, 1);
			this.propNewTime_tableWidget.addColumn("endtime", _("Proposed end date and time"), 200, 2);
			this.propNewTime_tableWidget.addColumn("duration", _("Duration"), false, 3);
			this.propNewTime_tableWidget.addColumn("name", _("Proposed by"), false, 4);
			//this.propNewTime_tableWidget.addColumn("conflicts", _("Conflicts"), false, 4);
			this.propNewTime_tableWidget.generateTable(tableData);

			this.propNewTime_tableWidget.addRowListener(function(tblWidget, type, selected, select){
				var data = tblWidget.getDataByRowID(select);

				// Start Date/Time
				appoint_dtp.setStartValue(data.starttime.timestamp);
				dhtml.getElementById("startdate").setAttribute("unixtime",appoint_dtp.getStartValue());
				fb_module.setStartMeetingTime(data.starttime.timestamp);

				// End Date/Time
				appoint_dtp.setEndValue(data.endtime.timestamp);
				dhtml.getElementById("duedate").setAttribute("unixtime",appoint_dtp.getEndValue());
				fb_module.setEndMeetingTime(data.endtime.timestamp);

				fb_module.updatePicker();

			},"select");
			//this.propNewTime_tableWidget.resize();
			dhtml.getElementById("appointment_freebusy_proposenewtime_container").style.display = "block";
		}else{
			dhtml.getElementById("appointment_freebusy_proposenewtime_container").style.display = "none";
		}

	}else{
		dhtml.getElementById("appointment_freebusy_proposenewtime_container").style.display = "none";
	}

	resizeFreeBusyContainer();
	
}

function resizeFreeBusyContainer(){
	var freebusyContainerHeight = (document.body.clientHeight - 100);
	if(dhtml.getElementById("appointment_freebusy_proposenewtime_container")){
		if(dhtml.getElementById("appointment_freebusy_proposenewtime_container").offsetHeight > 0){
			freebusyContainerHeight = freebusyContainerHeight - dhtml.getElementById("appointment_freebusy_proposenewtime_container").offsetHeight - 5;
		}
	}
	dhtml.getElementById("freebusy_container").style.height = freebusyContainerHeight + "px";
	var dialogContentWidth = dhtml.getElementById("dialog_content").clientWidth;
	dhtml.getElementById("freebusy_container").style.width = (((dialogContentWidth>0)?dialogContentWidth:780) -4) + "px";
}

function syncRecipientFields(newRecipsInFields){
	var fields = {
		visible: dhtml.getElementById("toccbcc"),
		to: dhtml.getElementById("to"),
		cc: dhtml.getElementById("cc"),
		bcc: dhtml.getElementById("bcc")
	};
	if(!fields.visible || !fields.to || !fields.cc || !fields.bcc){
		return false;
	}

	// Get value of recipeints;
	var recips = {
		visible: fields.visible.value.trim().split(";"),
		to: fields.to.value.split(";"),
		cc: fields.cc.value.split(";"),
		bcc: fields.bcc.value.split(";")
	}

	// Trim all recipient strings and create lookup index
	var lookupIndex = {
		visible: new Object(),
		to: new Object(),
		cc: new Object(),
		bcc: new Object()
	};

	for(var i in recips){
		for(var j=(recips[i].length-1);j>=0;j--){
			recips[i][j] = recips[i][j].trim();
			if(recips[i][j] != ""){
				lookupIndex[i][ recips[i][j] ] = 1;
			}else{
				recips[i].splice(j, 1);
			}
		}
	}


	/**
	 * Loop through all recipients in TO, CC and BCC fields  and check to see 
	 * what items are not in the visible cumulative field and can be removed.
	 */
	if(!newRecipsInFields){
		for(var i=(recips.to.length-1);i>=0;i--){
			// Recipient not found in visible cumulative field.
			if(!lookupIndex.visible[ recips.to[i] ]){
				recips.to.splice(i, 1);
				
			}
		}
		for(var i=(recips.cc.length-1);i>=0;i--){
			// Recipient not found in visible cumulative field.
			if(!lookupIndex.visible[ recips.cc[i] ]){
				recips.cc.splice(i, 1);
				
			}
		}
		for(var i=(recips.bcc.length-1);i>=0;i--){
			// Recipient not found in visible cumulative field.
			if(!lookupIndex.visible[ recips.bcc[i] ]){
				recips.bcc.splice(i, 1);
				
			}
		}
	/**
	 * New recipients are placed in the TO, CC and/or BCC fields and must be 
	 * added to the visible cumulative field.
	 */
	}else{
		for(var i=(recips.to.length-1);i>=0;i--){
			// Recipient not found in visible cumulative field.
			if(!lookupIndex.visible[ recips.to[i] ]){
				recips.visible[ recips.visible.length ] = recips.to[i];
			}
		}
		for(var i=(recips.cc.length-1);i>=0;i--){
			// Recipient not found in visible cumulative field.
			if(!lookupIndex.visible[ recips.cc[i] ]){
				recips.visible[ recips.visible.length ] = recips.cc[i];
			}
		}
		for(var i=(recips.bcc.length-1);i>=0;i--){
			// Recipient not found in visible cumulative field.
			if(!lookupIndex.visible[ recips.bcc[i] ]){
				recips.visible[ recips.visible.length ] = recips.bcc[i];
			}
		}
		
	}
	/**
	 * Loop through all recipients in visible cumulative field and see what 
	 * items are not yet in the TO, CC or BCC fields and add them.
	 */
	for(var i=0;i<recips.visible.length;i++){
		// Recipient not found in TO, CC or BCC field.
		if(!lookupIndex.to[ recips.visible[i] ] && !lookupIndex.cc[ recips.visible[i] ] && !lookupIndex.bcc[ recips.visible[i] ]){
			// Add to TO field
			recips.to[ recips.to.length ] = recips.visible[i];
			lookupIndex.to[ recips.visible[i] ] = recips.visible[i];
		}
	}

	// Set fields
	
	fields.visible.value = recips.visible.length == 0? "" :recips.visible.join("; ") + "; " ;
	fields.to.value      = recips.to.length == 0? "" :recips.to.join("; ") + "; ";
	fields.cc.value      = recips.cc.length == 0? "" :recips.cc.join("; ") + "; ";
	fields.bcc.value     = recips.bcc.length == 0? "" :recips.bcc.join("; ") + "; ";

	// if location is set then check wheather resource is present in recpients field or not
	if(fb_module && fb_module.hasResource && recips.bcc.length == 0)
		dhtml.getElementById('location').value = '';
}


function initDate(startdate, enddate)
{
	var additionalDefaultSeconds = 0;
	if (!startdate){
		startdate = new Date(new Date().ceilHalfhour());
	}
	if (!enddate){
		additionalDefaultSeconds = 1800;
		enddate = new Date(new Date().ceilHalfhour());
	}
	appoint_dtp.setStartValue(startdate.getTime()/1000);
	appoint_dtp.setEndValue((enddate.getTime()/1000)+additionalDefaultSeconds);
}

function submitAppointment(send, requestStatus)
{
	if(module.saving){
		return false;
	}

	if(tabbarControl.getSelectedTab() == "scheduling"){
		module.setStartTime(fb_module.getStartMeetingTime());
		module.setEndTime(fb_module.getEndMeetingTime());
		module.setLocation(fb_module.getResource());
		module.setUserList(fb_module.getUserList());
		module.setAllDayEvent(fb_module.isAllDayEvent());
	}

	// Label
	dhtml.getElementById("label").value = dhtml.getValue(dhtml.getElementById("select_label"));
	
	// Busy Status
	dhtml.getElementById("busystatus").value = dhtml.getValue(dhtml.getElementById("select_busystatus"));

	if(isNaN(appoint_dtp.getEndValue())){
		alert(_("The end date you entered occurs before the start date."));
		return false;
	}

	// Startdate
	dhtml.getElementById("startdate").value = appoint_dtp.getStartValue();
	dhtml.getElementById("commonstart").value = appoint_dtp.getStartValue();
	
	// Duedate
	dhtml.getElementById("duedate").value = appoint_dtp.getEndValue();
	dhtml.getElementById("commonend").value = appoint_dtp.getEndValue();

	// Basedate
	if(dhtml.getElementById("basedate").getAttribute("unixtime"))
    	dhtml.getElementById("basedate").value = dhtml.getElementById("basedate").getAttribute("unixtime");

	// Duration	
	dhtml.getElementById("duration").value = (dhtml.getElementById("duedate").value - dhtml.getElementById("startdate").value) / 60;

	// All Day Event
	var alldayeventset = dhtml.getValue(dhtml.getElementById("checkbox_alldayevent"));
	if(alldayeventset) {
		dhtml.getElementById("alldayevent").value = "1";

		var startdate = new Date(appoint_dtp.getStartValue()*1000);
		var enddate = new Date(appoint_dtp.getEndValue()*1000);
		enddate.addDays(1);

		// Startdate
		dhtml.getElementById("startdate").value = startdate.getTime() / 1000;
		dhtml.getElementById("commonstart").value = startdate.getTime() / 1000;
		
		// Duedate
		dhtml.getElementById("duedate").value = enddate.getTime() / 1000;
		dhtml.getElementById("commonend").value = enddate.getTime() / 1000;
		
		// Duration. Apparently this should be the intended duration, which may be longer or shorter than
		// the actual duration due to DST changes. It is therefore always a multiple of 1440.
		var days = Math.ceil( (enddate.getTime() - startdate.getTime()) / (1000 * 60 * 60 * 24));
		dhtml.getElementById("duration").value = 1440 * days;
	} else {
		dhtml.getElementById("alldayevent").value = "-1";
	}
	
	// Reminder
	var reminderset = dhtml.getValue(dhtml.getElementById("checkbox_reminder"));
	if(reminderset) {
		dhtml.getElementById("reminder").value = "1";
	} else {
		dhtml.getElementById("reminder").value = "-1";
	}
	
	// Reminder Minutes Before Start
	var minutes = dhtml.getValue(dhtml.getElementById("select_reminder_minutes"));
	dhtml.getElementById("reminder_minutes").value = minutes;

	// Reminder Time
	dhtml.getElementById("reminder_time").value = appoint_dtp.getStartValue();
	dhtml.getElementById("flagdueby").value = appoint_dtp.getStartValue() - (minutes*60);
	
	// Private
	var checkbox_private = dhtml.getElementById("checkbox_private");
	if(checkbox_private.checked) {
		dhtml.getElementById("sensitivity").value = "2";
		dhtml.getElementById("private").value = "1";
	} else {
		dhtml.getElementById("sensitivity").value = "0";
		dhtml.getElementById("private").value = "-1";
	}
	
	//Recurrence Pattern
	var recurtext = dhtml.getElementById("recurtext");
	if (recurtext) {
		dhtml.getElementById("recurring_pattern").value = (recurtext.firstChild?recurtext.firstChild.nodeValue:"");
	}
	
	// Contacts
	dhtml.getElementById("contacts_string").value = dhtml.getElementById("contacts").value;
	// Get receipients for the appointment.
	var recepientsList = getRecipients();
	if (!send){
		// check meeting request
		var isMeeting = module.itemProps.meeting && module.itemProps.meeting==1;
		var isOrganizer = module.itemProps.responsestatus && module.itemProps.responsestatus==1;
		var endtime = dhtml.getElementById("commonend").value * 1000;
		if (isMeeting && isOrganizer) {
			// Check whether there are any recepients in MR or not.
			if( endtime>(new Date().getTime()) && recepientsList.length > 0) {
				send = confirm(_("Would you like to send an update to the attendees regarding changes to this meeting?"));
			}
		}
	} else if (recepientsList.length == 0) {
		// If there is no recpients in MR than confirm with user whether he wants to send MR or not.
		if(!confirm(_("This meeting request cannot be sent since it has no recipients. Would you like to save and close this meeting instead?")))
			return;
	}

	if(module){
		var props = getPropsFromDialog();

		if(module.isMeeting && validateEmailAddress(dhtml.getElementById("toccbcc").value, false, module.resolveForSendingMR?false:true)){
			dhtml.getElementById("meeting").value = olMeeting;
			props["meeting"] = olMeeting;

			if(!module.itemProps.entryid || (module.itemProps.responsestatus && module.itemProps.responsestatus == olResponseOrganized)) {
				dhtml.getElementById("responsestatus").value = olResponseOrganized;
				props["responsestatus"] = olResponseOrganized;

				if(!send) props["requestsent"] = "0";
			}

			var recipients = new Object();
			recipients["recipient"] = createCompatibleRecipientList(recepientsList);

			/**
			 * Add the proposednewtimes of the existing recipients to the current recipients.
			 */
			if(typeof module.itemProps.recipients != "undefined" && typeof module.itemProps.recipients.recipient != "undefined"){
				var oldRecips = module.itemProps.recipients.recipient;	// Recipients as stored in the module
				if(typeof oldRecips != "undefined" && oldRecips.length > 0){
					for(var i=0;i<recipients["recipient"].length;i++){
						if(recipients["recipient"][i]["address"]){
							for(var j=0;j<oldRecips.length;j++){
								if(oldRecips[j]["email_address"] == recipients["recipient"][i]["address"]){
									if(parseInt(oldRecips[j]["proposednewtime"], 10) == 1){
										recipients["recipient"][i]["proposednewtime"] = oldRecips[j]["proposednewtime"]
										recipients["recipient"][i]["proposenewstarttime"] = oldRecips[j]["proposenewstarttime"]
										recipients["recipient"][i]["proposenewendtime"] = oldRecips[j]["proposenewendtime"]
										break;
									}
								}
							}
						}
					}
				}
			}
			module.save(props, send, recipients, dhtml.getElementById("dialog_attachments").value);
			module.saving = true;

			/**
			 * If changes are to be saved for accepting/tentatively
			 * meeting request, then send response.
			 * Ask for confirmation to user of sending response to organizer.
			 */
			if (requestStatus) {
				module.savingMeetingRequest = true;
				// Open send Metting Request Mail Confirmation dialog box.
				webclient.openModalDialog(module, 'sendMRMailConfirmation', DIALOG_URL+'task=sendMRMailConfirmation_modal', 320, 280, sendMRMailConfirmationCallback, requestStatus);
			}
		}else if(!module.isMeeting){
			module.save(props, send, false, dhtml.getElementById("dialog_attachments").value);
			module.saving = true;
		} else if (module.resolveForSendingMR !== true) {
			// Set status for the resolver to know he has to call this function again
			module.resolveForSendingMR = true;
			// Save arguments in module to use in checkNamesCallBackAppointment
			module.send = send;
			module.requestStatus = requestStatus;
			checkNames(checkNamesCallBackAppointment);
		}else{
			// Reset status for the resolver to know he does not have to call this function again
			module.resolveForSendingMR = false;
		}
	}
}

/**
 * Function which sends request for saving request status.
 * @param boolean sendResponse if it is true than reponse is sent to organizer via mail otherwise it is not sent.
 * @param string requestStatus it is a type of request values are accept, tentative or decline.
 */
function sendMRMailConfirmationCallback(noResponse, requestStatus)
{
	var basedateEle = dhtml.getElementById("basedate");
	var basedate = false;
	var body = false;
	if(basedateEle && basedateEle.value)
		basedate = basedateEle.value;
	
	if(typeof noResponse == "object"){
		body = noResponse.body;
		noResponse = noResponse.type;
	}

	switch(requestStatus)
	{
		case "accept":
			module.acceptMeetingRequest(noResponse, basedate, body);
			break;
		case "tentative":
			module.tentativeMeetingRequest(noResponse, basedate, body);
			break;
		case "decline":
			module.declineMeetingRequest(noResponse, basedate, body);
			break;
	}
	module.savingMeetingRequest = true;

	window.setTimeout("window.close()",1200);
}

// Cancelling an already-sent invitation causes a 'cancel' message to be sent and
// the message window to be closed. If this is a new invitation, just go back to a
// non-meetingrequest mode.
function cancelInvitation()
{
	var responseStatus = dhtml.getElementById("responsestatus").value;
	
	// responseStatus is the value of 'responseStatus' as it is on the server. This means that
	// if it is equal to '1', then this is a meeting request that we have already sent in the past.
	if(responseStatus == 1) {
		var isMeeting = module.itemProps.meeting && module.itemProps.meeting==1;
		var isOrganizer = module.itemProps.responsestatus && module.itemProps.responsestatus==1;
		if (isMeeting && isOrganizer)
			var basedate = dhtml.getElementById('basedate').getAttribute('unixtime');
			if(basedate){
				webclient.openModalDialog(module,"deleteoccurence", DIALOG_URL+"entryid="+module.messageentryid+"&storeid="+module.storeid+"&task=deleteoccurrence_modal&basedate="+basedate+"&parententryid="+module.parententryid+"&meeting="+1, 300, 200, callbackdeleteoccurence, null, {parentModule: module, parentModuleType: "item"});
			}else{
				deleteAppointment();
			}
	} if(responseStatus == 3 || responseStatus == 5) {
		// We're an attendee, we cannot cancel
	} else {
		meetingRequestSetup(mrSetupNormal);
	}
}

//callback which sends a cancelation message to attendees and close the appointment window
function callbackdeleteoccurence()
{
	window.setTimeout("window.close()",1200);	
}

// If 'send' is TRUE, then send a cancellation
function deleteAppointment()
{
	if(module.messageentryid) {
		var basedate = dhtml.getElementById("basedate").getAttribute("unixtime");
		basedate = basedate ? parseInt(basedate, 10) : 0;

		module.deleteMessage(basedate);
	} else {
		window.close();
	}
}

function onChangeReminder()
{
	var checkbox_reminder = dhtml.getElementById("checkbox_reminder");
	
	if(checkbox_reminder) {
		var reminder = dhtml.getElementById("reminder");
		var select_reminder_minutes = dhtml.getElementById("select_reminder_minutes");
		// if item is alldayevent then its reminder times should be set to 18hrs
		var alldayevent_reminder_minutes = "1080";
		
		if(checkbox_reminder.checked) {
			select_reminder_minutes.disabled = false;
			select_reminder_minutes.style.background = "#FFFFFF";
			// if reminder is checked and is all day then set reminder time to 18hours 
			if(dhtml.getElementById("checkbox_alldayevent").checked){
				select_reminder_minutes.value = alldayevent_reminder_minutes;
			}
			reminder.value = "1";
		} else {
			select_reminder_minutes.disabled = true;
			select_reminder_minutes.style.background = "#DFDFDF";
			// if reminder is unchecked and is all day then set reminder time to default value
			if(dhtml.getElementById("checkbox_alldayevent").checked){
				select_reminder_minutes.value = parentWebclient.settings.get("calendar/reminder_minutes",15);
			}

			reminder.value = "-1";
		}
	}
}

function onChangeAllDayEvent(forceFlag)
{
	var checkbox_alldayevent = dhtml.getElementById("checkbox_alldayevent");
	var text_startdate_time = dhtml.getElementById("text_startdate_time");
	var text_duedate_time = dhtml.getElementById("text_duedate_time");
	// if item is alldayevent then its reminder times should be set to 18hrs
	var alldayevent_reminder_minutes = "1080";
	// the flag will be passed for forcefully selecting the appointment as allday event.
	if(forceFlag)
		dhtml.getElementById("checkbox_alldayevent").checked = true;

	if(checkbox_alldayevent.checked) {
		appoint_dtp.startPicker.timeElement.hide();
		appoint_dtp.endPicker.timeElement.hide();
		dhtml.setValue(dhtml.getElementById("select_busystatus"), "0");
		// when an allday event is created, if the default reminder is on in setting, we need to set reminder time to 18hrs and if not to default value.
		if(dhtml.getElementById("checkbox_reminder").checked){
			dhtml.setValue(dhtml.getElementById("select_reminder_minutes"), alldayevent_reminder_minutes);
		}else{
			dhtml.setValue(dhtml.getElementById("select_reminder_minutes"), parentWebclient.settings.get("calendar/reminder_minutes",15));
		}
	}else if(dhtml.getElementById("entryid").value != "" || window.location.search.indexOf("allDayEvent=true")>0 ){
		//set the start time as work day starttime when an allday appointment is unchecked
		var dayStart_time = parseInt(webclient.settings.get("calendar/workstartday","540"));
		var time_resolution = parseInt(60/webclient.settings.get("calendar/appointment_time_size","2"));
		appoint_dtp.startPicker.timeElement.setValue(dayStart_time * 60);
		appoint_dtp.endPicker.timeElement.setValue((dayStart_time + time_resolution) * 60);

		appoint_dtp.startPicker.timeElement.show();
		appoint_dtp.endPicker.timeElement.show();
		dhtml.setValue(dhtml.getElementById("select_busystatus"), "2");
		dhtml.setValue(dhtml.getElementById("select_reminder_minutes"), parentWebclient.settings.get("calendar/reminder_minutes",15));
	}else{
		appoint_dtp.startPicker.timeElement.show();
		appoint_dtp.endPicker.timeElement.show();
		dhtml.setValue(dhtml.getElementById("select_busystatus"), "2");
		dhtml.setValue(dhtml.getElementById("select_reminder_minutes"), parentWebclient.settings.get("calendar/reminder_minutes",15));
	}
}

function meetingRequestSetup(level) {
	var meetingrequest_organiser = dhtml.getElementById("meetingrequest_organiser");
	var meetingrequest_organiser_name = dhtml.getElementById("meetingrequest_organiser_name");
	var meetingrequest_recipient = dhtml.getElementById("meetingrequest_recipient");
	var sendbutton = dhtml.getElementById("send");
	var savebutton = dhtml.getElementById("save");
	var deletebutton = dhtml.getElementById("delete");
	var acceptbutton = dhtml.getElementById("accept");
	var tentativebutton = dhtml.getElementById("tentative");
	var declinebutton = dhtml.getElementById("decline");
	var proposenewtimebutton = dhtml.getElementById("proposenewtime");
	var printButton = dhtml.getElementById("print");
	var inviteattendeesbutton = dhtml.getElementById("inviteattendees");
	var cancelinviteattendeesbutton = dhtml.getElementById("cancelinviteattendees");
	var checknames = dhtml.getElementById("checknames");
	var seperator1 = dhtml.getElementById("seperator1");
	
	if(module.itemProps && module.itemProps['entryid']) {
		// Show print button for saved appointments and MRs
		printButton.style.display = "block";
	}

	if(level == 0) { // normal
		meetingrequest_recipient.style.display = "none";
		meetingrequest_organiser.style.display = "none";
		sendbutton.style.display = "none";
		deletebutton.style.display = "block";
		savebutton.style.display = "block";
		cancelinviteattendeesbutton.style.display = "none";
		inviteattendeesbutton.style.display = "block";
		acceptbutton.style.display = "none";
		tentativebutton.style.display = "none";
		declinebutton.style.display = "none";
		proposenewtimebutton.style.display = "none";
		checknames.style.display = "none";
		seperator1.style.display = "none";
		dhtml.getElementById("windowtitle").innerHTML = _("Appointment");
		window.document.title = _("Appointment");
		module.isMeeting = false;
	} else if(level == 1) {	// organiser
		meetingrequest_recipient.style.display = "block";
		meetingrequest_organiser.style.display = "none";
		sendbutton.style.display = "block";
		deletebutton.style.display = "none";
		savebutton.style.display = "block";
		cancelinviteattendeesbutton.style.display = "block";
		inviteattendeesbutton.style.display = "none";
		acceptbutton.style.display = "none";
		tentativebutton.style.display = "none";
		declinebutton.style.display = "none";
		proposenewtimebutton.style.display = "none";
		checknames.style.display = "block";
		seperator1.style.display = "block";
		dhtml.getElementById("windowtitle").innerHTML = _("Meeting");
		window.document.title = _("Meeting");
		if(window.propNewTime_tableWidget)
			window.propNewTime_tableWidget.resize();
		resizeFreeBusyContainer();
		module.isMeeting = true;
	} else { // attendee
		meetingrequest_recipient.style.display = "none";
		meetingrequest_organiser.style.display = "";
		meetingrequest_organiser_name.innerHTML = dhtml.getElementById("sent_representing_email_address").value;
		sendbutton.style.display = "none";
		deletebutton.style.display = "block";
		savebutton.style.display = "block";
		cancelinviteattendeesbutton.style.display = "none";
		inviteattendeesbutton.style.display = "none";
		acceptbutton.style.display = "block";
		tentativebutton.style.display = "block";
		declinebutton.style.display = "block";
		proposenewtimebutton.style.display = "block";
		checknames.style.display = "none";
		seperator1.style.display = "none";
		dhtml.getElementById("windowtitle").innerHTML = _("Meeting");
		window.document.title = _("Meeting");
		module.isMeeting = true;
		if(window.propNewTime_tableWidget) 
			window.propNewTime_tableWidget.resize();
		resizeFreeBusyContainer();

	}

	/**
	 * Using responsestatus to determine whether to show the Accept/Tentative/Decline buttons.
	 * respNone         0x00000000
	 * respTentative    0x00000002
	 * respAccepted     0x00000003
	 * respDeclined     0x00000004
	 * respNotResponded 0x00000005
	*/
	var responseStatus = parseInt(dhtml.getElementById("responsestatus").value);
	if(responseStatus > 1){
		acceptbutton.style.display = "block";
		tentativebutton.style.display = "block";
		declinebutton.style.display = "block";
		proposenewtimebutton.style.display = "block";
	}

	var meeting = dhtml.getElementById('meeting');
	var extrainfo = dhtml.getElementById("extrainfo");
	// Meeting has been cancelled
	if (meeting.value == 7) {
		sendbutton.style.display = "none";
		acceptbutton.style.display = "none";
		tentativebutton.style.display = "none";
		declinebutton.style.display = "none";
		proposenewtimebutton.style.display = "none";
		deletebutton.style.display = "block";
		extrainfo.style.display = "block";
		extrainfo.innerHTML = "<p>"+ NBSP + _("Meeting has been canceled.") +"</p>";
	} else {
		extrainfo.style.display = "none";
	}

	resizeBody();
}

function onAppointmentTabChange(newTab,oldTab)
{
	if(oldTab == "appointment" && fb_module){
		//update the meetingtime in freebusy tab
		fb_module.setStartMeetingTime(module.getStartTime());
		fb_module.setEndMeetingTime(module.getEndTime());
		fb_module.setAllDayEvent(module.isAllDayEvent());
		var recurring = dhtml.getElementById("recurring").value;
		var basedate = dhtml.getElementById("basedate").value;
		if(recurring != 1 || basedate) {
			fb_module.updatePicker();
		}
		syncTrackingTab();
	}
	if(newTab == "scheduling"){
		if(!fb_module){
			//freebusy module (second tab)
			fb_module = new freebusymodule(module.getStartTime());
			var fb_moduleID = webclient.addModule(fb_module);
			fb_module.init(fb_moduleID,dhtml.getElementById("scheduling_tab"));
			//fb_module.initView(1,webclient.username, module.getUserList());
			var organizer = false;

			// See if there is a recipient with recipient_flags == 3 (organizer)
			if(module.itemProps.recipients && module.itemProps.recipients.recipient.length > 0){
			    for(var i in module.itemProps.recipients.recipient) {
			        var recipient = module.itemProps.recipients.recipient[i];
			        
			        if(recipient.recipient_flags == 3) {
			            organizer = recipient.display_name;
			        }
			    } 
			}
			
			if (!organizer && module.itemProps && module.itemProps.sent_representing_name) {
				organizer = module.itemProps.sent_representing_name;
			} else if (!organizer) {
				var organizerStore = parentWebclient.hierarchy.getStore(module.storeid);
				//if user is creating a meeting as delegate the organizer name in freebusy should me the owner of the calendar.
				if(organizerStore.emailaddress && organizerStore.emailaddress != webclient.username){
					organizer = organizerStore.username;
				}else{
					var mucUsername = getMUCStoreUsername(parentWebclient, module.storeid);
					if(mucUsername){
						organizer = mucUsername;
					}else{
						organizer = webclient.username;
					}
				}
			}
			fb_module.initView(1, organizer, module.getUserList());
			
			//update the meetingtime in freebusy tab
			fb_module.setStartMeetingTime(module.getStartTime());
			fb_module.setEndMeetingTime(module.getEndTime());
			fb_module.setAllDayEvent(module.isAllDayEvent());

			// Disable the picker when opening the whole serie of a recurrent item.
			var recurring = dhtml.getElementById("recurring").value;
			var basedate = dhtml.getElementById("basedate").value;
			if(recurring == 1 && !basedate) {
				// Disable the picker in the Scheduling tab when recurrent
				if(fb_module) fb_module.disablePicker();
			}else{
				// Enable the picker in the Scheduling tab when recurrent
				if(fb_module){
					fb_module.updatePicker();
					fb_module.enablePicker();
				}
			}
			//set autoChangeResourceRecipient flag to false whenever a saved appointment is opened and is switched to FBview
			if (module.itemProps && module.itemProps.entryid != undefined)
				fb_module.autoChangeResourceRecipient = false;
		}	
		else{
			fb_module.resize();	
			fb_module.setUserList(module.getUserList());
		}
		// Case in which schedule tab is selected after tracking tab.
		if(oldTab == "tracking") {
			fb_module.setStartMeetingTime(module.getStartTime());
			fb_module.setEndMeetingTime(module.getEndTime());
			fb_module.setAllDayEvent(module.isAllDayEvent());

			var recurring = dhtml.getElementById("recurring").value;
			var basedate = dhtml.getElementById("basedate").value;
			if(recurring != 1 || basedate) {
				fb_module.updatePicker();
			}
		}
	}
	if(oldTab == "scheduling"){
		//update the appointment time in appointment tab
		module.setStartTime(fb_module.getStartMeetingTime());
		module.setEndTime(fb_module.getEndMeetingTime());
		module.setAllDayEvent(fb_module.isAllDayEvent());
	}
	if(newTab == "appointment"){
		if(fb_module){
			var fbUserlist = fb_module.getUserList();
			if(fbUserlist.length>0){
				// Check wheather the username is the organiser or not to set menubar in Appointment module 
				var meetingRecipientStatus = false;
				for (var j = 0; j < fbUserlist.length; j++){
					// Get emailaddress of opened folder-object, to check organizer status
					var storeObject = parentWebclient.hierarchy.getStore(module.storeid);
					var storeEmail = webclient.emailaddress.toLowerCase();
					if(storeObject && storeObject.emailaddress)
						storeEmail = storeObject.emailaddress.toLowerCase();

					if(fbUserlist[j]["emailaddress"] != undefined && fbUserlist[j]["emailaddress"].toLowerCase() == storeEmail && fbUserlist[j]["recipient_flags"] && fbUserlist[j]["recipient_flags"] == (recipSendable | recipOrganizer))
						meetingRecipientStatus = true;
				}
				meetingRecipientStatus? meetingRequestSetup(mrSetupOrganiser): meetingRequestSetup(mrSetupAttendee);
			}
			else{
				meetingRequestSetup(mrSetupNormal);
			}
			module.setLocation(fb_module.getResource());
			module.setUserList(fbUserlist);
			//set autoChangeResourceRecipient flag to true as user as switched to Fb view
			fb_module.autoChangeResourceRecipient = false;
		}
	}
	if(newTab == "tracking"){
		if(!tableWidget){
			createTrackingTable(generateDataForTrackingTable(module));
		}
		syncTrackingTab();
	}
	// update the box at Appointment tab. which contain small info about attendees.
	updateInfoBox(module);
	if(newTab == "tracking"){
		switch(oldTab)
		{
			case "appointment":
				syncTrackingTabWithAppointmentTab("scheduling", "appointment");
				break;
			case "scheduling":
				syncTrackingTabWithAppointmentTab("appointment", "scheduling");
				break;
		}
	}
}

/**
 * Will loop through all the multiusercalendarmodules and try to find the a store that has been 
 * opened by one of the modules that matches the supplied storeid argument. It will return the 
 * username of the one that matches.
 *
 * @param Object webclient The webclient to retrieve the modules from
 * @param string storeid the storeid
 * @return string username Username
 */
function getMUCStoreUsername(webclient, storeid){
	var mucModules = webclient.getModulesByName('multiusercalendarmodule');
	for(var i=0;i<mucModules.length;i++){
		var groups = mucModules[i].groups;
		for(var j=0;j<groups.length;j++){
			var groupStoreId = groups[i].storeid;
			if(groupStoreId == storeid){
				return groups[i].username;
			}
		}
	}
	return false;
}

function showRecurrence()
{
	var recurring = dhtml.getElementById("recurring").value;
	var basedate = dhtml.getElementById("basedate").value;
	var startocc = dhtml.getElementById("startocc").value;
	var endocc = dhtml.getElementById("endocc").value;
	var start = parseInt(dhtml.getElementById("start").value);
	var end = parseInt(dhtml.getElementById("end").value);
	var term = parseInt(dhtml.getElementById("term").value);
	var regen = parseInt(dhtml.getElementById("regen").value);
	var everyn = parseInt(dhtml.getElementById("everyn").value);
	var type = parseInt(dhtml.getElementById("type").value);
	var subtype = parseInt(dhtml.getElementById("subtype").value);
	var weekdays = parseInt(dhtml.getElementById("weekdays").value);
	var month = parseInt(dhtml.getElementById("month").value);
	var monthday = parseInt(dhtml.getElementById("monthday").value);
	var nday = parseInt(dhtml.getElementById("nday").value);
	var numoccur = parseInt(dhtml.getElementById("numoccur").value);

	if(recurring == 1 && !basedate) {
		// Label
		dhtml.getElementById("recur").style.display = "";            // Appointment tab
		dhtml.getElementById("startend").style.display = "none";     // Appointment tab
		dhtml.getElementById("fbrecur").style.display = "";          // Scheduling tab
		dhtml.getElementById("fbstartdate").style.display = "none";  // Scheduling tab
		dhtml.getElementById("fbenddate").style.display = "none";    // Scheduling tab
		// Disable the picker in the Scheduling tab when recurrent
		if(fb_module) fb_module.disablePicker();
		
		var recurtext = _("Occurs every") + " ";
		switch(type) {
			case 10:
				if(everyn == 1) {
					recurtext += _("workday");
				} else if(everyn == 1440) {
					recurtext += _("day");
				} else {
					recurtext += (everyn / 1440);
					recurtext += " ";
					recurtext += _("days");
				}
				break;
			case 11:
				if(everyn == 1)
					recurtext += _("week");
				else {
					recurtext += everyn;
					recurtext += " ";
					recurtext += _("weeks");
				}
				break;
			case 12:
				if(everyn == 1)
					recurtext += _("month");
				else {
					recurtext += everyn;
					recurtext += " ";
					recurtext += _("months");
				}
				break;
			case 13:
				if(everyn <= 12)
					recurtext += _("year");
				else {
					recurtext += everyn/12;
					recurtext += " ";
					recurtext += _("years");
				}
				break;
		}

		recurtext += " ";
		recurtext += _("effective");
		recurtext += " ";
		
		var startDate = new Date(start*1000);
		recurtext += startDate.print(_("%d-%m-%Y"));

		// Term==35 means the series runs indefinitely
		if(term != 35){
			// Term==0x22 means the series run for x occurrences
			if(term == 0x22){
				recurtext += " ";
				if(numoccur==1){
					recurtext += _("for 1 occurrence");
				}else{
					recurtext += _("for %d occurrences").sprintf(numoccur);
				}
				recurtext += " ";
				
			// Series run until end date
			}else{
				recurtext += " ";
				recurtext += _("until");
				recurtext += " ";

				var endDate = new Date(end*1000);
				recurtext += endDate.print(_("%d-%m-%Y"));
			}
		}
		if(startocc != 0 && endocc != 1440){
			recurtext += " ";
			recurtext += _("from");
			recurtext += " ";
			recurtext += secondsToTime(startocc*60)
			recurtext += " ";
			recurtext += _("to");
			recurtext += " ";
			recurtext += secondsToTime(endocc*60);
			recurtext += ".";
		}
		dhtml.getElementById("recurtext").innerHTML = recurtext;
		dhtml.getElementById("fbrecurtext").innerHTML = recurtext;
		
	} else {
		dhtml.getElementById("recur").style.display = "none";        // Appointment tab
		dhtml.getElementById("startend").style.display = "";         // Appointment tab
		dhtml.getElementById("fbrecur").style.display = "none";      // Scheduling tab
		dhtml.getElementById("fbstartdate").style.display = "";      // Scheduling tab
		dhtml.getElementById("fbenddate").style.display = "";        // Scheduling tab
		// Enable the picker in the Scheduling tab when recurrent
		if(fb_module) fb_module.enablePicker();

	}
	// Adjust body textarea according to available client area.
	resizeBody();
}

function categoriesCallBack(categories) {
	dhtml.getElementById("categories").value = categories;
}

function abCallBackRecipients(recips) {
	var attendees = recips['to'];

	// if user has selected resource then automatically mark it as resource instead of required attendee
	if(attendees.multiple){
		for(var key in attendees){
			if(key != 'multiple' && key != 'value'){
				var recipientType = parseInt(attendees[key]['display_type'], 10);
				if(recipientType === DT_EQUIPMENT || recipientType === DT_ROOM) {
					// Check whether we need to add an extra semicolon at the end
					if(!isLastRecipientClosedWithSemicolon(dhtml.getElementById('bcc').value)){
						dhtml.getElementById('bcc').value += '; ';
					}
					dhtml.getElementById('bcc').value += attendees[key].value + '; ';
				} else {
					// Check whether we need to add an extra semicolon at the end
					if(!isLastRecipientClosedWithSemicolon(dhtml.getElementById('to').value)){
						dhtml.getElementById('to').value += '; ';
					}
					dhtml.getElementById('to').value += attendees[key].value + '; ';
				}
			}
		}
	} else {
		// this is not going to happen as we always use multiple recipients in meeting request dialog
		var recipientType = parseInt(attendees['display_type'], 10);
		if(recipientType === DT_EQUIPMENT || recipientType === DT_ROOM) {
			// Check whether we need to add an extra semicolon at the end
			if(!isLastRecipientClosedWithSemicolon(dhtml.getElementById('bcc').value)){
				dhtml.getElementById('bcc').value += '; ';
			}
			dhtml.getElementById('bcc').value += attendees.value + '; ';
		} else {
			// Check whether we need to add an extra semicolon at the end
			if(!isLastRecipientClosedWithSemicolon(dhtml.getElementById('to').value)){
				dhtml.getElementById('to').value += '; ';
			}
			dhtml.getElementById('to').value += attendees.value + '; ';
		}
	}

	syncRecipientFields(true);
}

function abCallBackContacts(recips) {
	dhtml.getElementById("contacts").value = recips['contacts'].value;
}

function callBackRecurrence(recurrence) {
	module.setRecurrence(recurrence);
}
	
/**
 * function to initialize the table widget and put the data into it.
 * @param object module an object with data to be filled in table widget.
 */
	 
function createTrackingTable(items){
	var columnData = [
			{id:"icon_index",name:"","title":_("Icon"),"sort":false,"visibility":true,"order":0,"width":"20"},
			{id:"display_name",name:_("Name"),"title":_("Name"),"sort":true,"visibility":true,"order":1},
			{id:"recipient_attendees",name:_("Attendance"),"title":_("Attendance"),"sort":true,"visibility":true,"order":2},
			{id:"recipient_status",name:_("Response"),"title":_("Response"),"sort":true,"visibility":true,"order":3}
		];

	//create table widget view here
	var tableWidgetElem = dhtml.getElementById("tracking_table");
	tableWidget = new TableWidget(tableWidgetElem);
	for (var x in columnData){
		var col = columnData[x];
		tableWidget.addColumn(col["id"],col["name"],col["width"],col["order"],col["title"],col["sort"],col["visibility"]);
	}
	
	//generate tableWidget
	tableWidget.generateTable(items);
}

/**
 * Function which retrieves the required data from module object which is needed for table widget and info box.
 * @param object module Module Object
 * @return Array items an array of objects 
 */
function generateDataForTrackingTable(module){
	var items = new Array();
	
	//add a default organizer row
	var firstItem = new Object();
	firstItem["icon_index"] = {innerHTML : "<div class='message_icon icon_icon_index'>&nbsp;</div>"};
	var organizer = null;
	
	// See if there is a property with sent_representing_email_address set.(organizer)
	if (module.itemProps.sent_representing_email_address && module.itemProps.sent_representing_email_address.length != 0) {
		var address = module.itemProps.sent_representing_email_address;
		organizer = address.substring(0, address.indexOf("@"));
	}
	
	if(!organizer) {
		organizer = webclient.username;
	}
	firstItem["display_name"] = {innerHTML : organizer};
	firstItem["recipient_attendees"] = {innerHTML : _("Organizer")};
	firstItem["recipient_status"] = {innerHTML : _("No Response")};
	firstItem["recipient_status_num"] = {innerHTML : 0};
	items.push(firstItem);
	
	if(module.itemProps.recipients && module.itemProps.recipients.recipient.length > 0){
		// when there is only one recipient, that time module.itemProps.recipients.recipient 
		// works as a object rather an array. so converting it to an array to work properly.
		var obj = module.itemProps.recipients.recipient;
		//checking that obj is an array or not.
		if(!(typeof(obj.join)=="function" && typeof(obj.sort)=="function" && typeof(obj.reverse)=="function")) {
			var value = module.itemProps.recipients.recipient;
			module.itemProps.recipients.recipient = [value];
		}
		
	    for(var i in module.itemProps.recipients.recipient) {
	    	var item = new Object();
	    	item["icon_index"] = {innerHTML : "<div class='message_icon icon_icon_index'>&nbsp;</div>"};
	    	item["display_name"] = {innerHTML : module.itemProps.recipients.recipient[i].display_name};
	    	item["recipient_attendees"] = {innerHTML : module.itemProps.recipients.recipient[i].recipient_attendees};
	    	item["recipient_status"] = {innerHTML : module.itemProps.recipients.recipient[i].recipient_status};
	    	item["recipient_status_num"] = {innerHTML : module.itemProps.recipients.recipient[i].recipient_status_num};
	        items.push(item);
	    } 
	}
	return items;
}

/**
 * Function which update the data of info box.
 * i.e. - Set the total number of accepted/tentative/declined attendees in a box 
 *        which is on appointment tab.
 * @param object module Module Object
 */
function updateInfoBox(module){
	var items = generateDataForTrackingTable(module);
	var infobox = dhtml.getElementById("meetingrequest_responses");
	if(items.length >1){
		var accepted = 0, tentative = 0, declined = 0;
		for(var i=0;i<items.length;i++){
			switch( parseInt(items[i].recipient_status_num.innerHTML, 10) ) {
				case olResponseTentative: // tentative
					tentative++;
					break;
				case olResponseAccepted: //accepted
					accepted++;
					break;
				case olResponseDeclined: // declined
					declined++;
					break;
			}
		}
		infobox.style.display = "block";
		infobox.innerHTML = NBSP + accepted +" "+ _("attendee accepted") +", "+ tentative +" "+ _("tentatively accepted") +", "+ declined +" "+ _("declined") +".";
	}
}

/**
 * Function to sync the data of tracking tab.
 * function add or remove the attendees and their stats from tracking tab.
 */
function syncTrackingTab(){
	if(!tableWidget || !fb_module){
		return false;
	}
	syncRecipientFields(true);
	var items = new Array();
	var value;
	var attendeeList = createCompatibleUserList(fb_module.getUserList());
	for(var j = 0; j< attendeeList.length;j++){
		var item = new Object();
		item["icon_index"] = {innerHTML : "<div class='message_icon icon_icon_index'>&nbsp;</div>"};
		item["display_name"] = {innerHTML : attendeeList[j].fullname};
		item["email_address"] = {innerHTML : attendeeList[j].emailaddress};
		switch(attendeeList[j].recipienttype){
			case MAPI_ORIG:
				value = _("Organizer");
				break;
			case MAPI_TO:
				value = _("Required");
				break;
			case MAPI_CC:
				value = _("Optional");
				break;
			case MAPI_BCC:
				value = _("Resource");
				break;
		}
		item["recipient_attendees"] = {innerHTML : value};
		switch(attendeeList[j].recipient_status_num)
		{
			case olResponseOrganized:
				value = _("Organizer");
				break;
			case olResponseTentative:
				value = _("Tentative");
				break;
			case olResponseAccepted:
				value = _("Accepted");
				break;
			case olResponseDeclined:
				value = _("Declined");
				break;
			case olResponseNotResponded:
				value = _("Not Responded");
				break;
			case olResponseNone:
			default:
				value = _("No Response");
				break;
		}
		item["recipient_status"] = {innerHTML : value};
		item["recipient_status_num"] = {innerHTML : attendeeList[j].recipient_status_num};
		item["recipient_type"] = {innerHTML : attendeeList[j].recipienttype};
		items.push(item);
	}
	tableWidget.generateTable(items);
	module.itemProps.recipients = new Object();
	module.itemProps.recipients.recipient = new Array();
	
	for (var i = 1; i < items.length; i++) {
		var item = new Object();
		for (var j in items[i]) {
			item[j] = items[i][j].innerHTML;
		}
		module.itemProps.recipients.recipient.push(item);
	}
}

/**
 * Function to sync the tracking tab with appointment tab.
 * It calls 2 different function to sync the tabs.
 * TODO: not the best solution but working as of now. 
 *       [Need to fix it with better Solution]
 * @param string oldtab -old tab
 * @param string newtab -new tab
 */
function syncTrackingTabWithAppointmentTab(oldtab, newtab) {
	onAppointmentTabChange(oldtab, newtab);
	window.check = window.setTimeout(function(){
		syncTrackingTab(true);
	},1000);
}

/**
 * Function which returns userList compatible to
 * appointmentitemmodule and FBmodule
 * @param array userList userList that is to be made compatible.
 * @return array attendeeList Compatible attendee list
 */
function createCompatibleUserList(userList)
{
	if (!module.itemProps.recipients) {
		return userList;
	}
	var module_recipients = module.getUserList();
	var attendeeList = new Array();
	var flag = 0;

	var recipient_type = new Array();
	recipient_type[_("Organizer")] = MAPI_ORIG;
	recipient_type[_("Required")] = MAPI_TO;
	recipient_type[_("Optional")] = MAPI_CC;
	recipient_type[_("Resource")] = MAPI_BCC;
	
	var recipient_status_num = new Array();
	recipient_status_num[_("No Response")] = olResponseNone;
	recipient_status_num[_("Organizer")] = olResponseOrganized;
	recipient_status_num[_("Tentative")] = olResponseTentative;
	recipient_status_num[_("Accepted")] = olResponseAccepted;
	recipient_status_num[_("Declined")] = olResponseDeclined;
	recipient_status_num[_("Not Responded")] = olResponseNotResponded;
	
	var insertnew = 0;
	var recipients = new Array();
	
	/**
	 * Prepare array that only contains info of attendees that
	 * are invited in appointment. As first item contains 
	 * info of owner, push it to array recipients.
	 */
	recipients.push(module_recipients[0]);
	
	//Convert to Array object if not in Array()
	if (!isArray(module.itemProps.recipients.recipient)){
		module.itemProps.recipients.recipient = Array(module.itemProps.recipients.recipient);
	}
	
	for (var i = 1; i < module_recipients.length; i++) {
		insertnew = 0;
		for (var j = 0; j < module.itemProps.recipients.recipient.length; j++) {
			var recipient = module.itemProps.recipients.recipient[j];
			
			if (module_recipients[i]["fullname"] == recipient["display_name"]) {
				var item = new Object();
				item["emailaddress"] = recipient["email_address"];
				item["entryid"] = module_recipients[i]["entryid"];
				item["fullname"] = recipient["display_name"];
				item["display_name"] = recipient["display_name"];
				item["recipient_flags"] = parseInt(recipient["recipient_flags"], 10);
				item["recipient_status"] = recipient["recipient_status"];
				item["recipienttype"] = recipient_type[recipient["recipient_attendees"]];
				recipients.push(item);
				insertnew = 1;
				break;
			}
		}
		
		if (insertnew == 0) {
			var item = new Object();
			item = module_recipients[i];
			item["recipienttype"] = module_recipients[i]["recipienttype"];
			item["recipient_flags"] = module_recipients[i]["recipient_flags"]?parseInt(module_recipients[i]["recipient_flags"], 10):1;
			item["recipient_status"] = module_recipients[i]["recipient_status"]?module_recipients[i]["recipient_status"]:"No Response";
			recipients.push(item);
		}
	}
	/**
	 * Create compatible array(userList) that contains all
	 * necesary items that are required to update
	 * tracking table.
	 */
	
	attendeeList.push(recipients[0]);
	for (var i = 1; i < userList.length; i++) {
		insertnew = 0;
		if (userList[i]["email_address"]) {
			userList[i]["emailaddress"] = userList[i]["email_address"];
		}
		
		for (var j = 1; j < recipients.length; j++) {
			if (recipients[j]["fullname"] == userList[i]["fullname"]) {
				var item = new Object();
				item = recipients[j];
				item["entryid"] = userList[i]["entryid"];
				item["recipient_status_num"] = recipient_status_num[recipients[j]["recipient_status"]];
				attendeeList.push(item);
				insertnew = 1;
				break;
			}
		}
		
		if (insertnew == 0) {
			var item = new Object();
			item["entryid"] = userList[i]["entryid"];
			item["fullname"] = userList[i]["fullname"];
			item["display_name"] = userList[i]["fullname"];
			item["emailaddress"] = userList[i]["emailaddress"];
			item["recipienttype"] = userList[i]["recipienttype"];
			item["recipient_flags"] = 1;
			item["recipient_status"] = "NO Response";
			item["recipient_status_num"] = 0;
			attendeeList.push(item);
		}
	}

	return attendeeList;
}


/**
 * Function which returns recipient List that contains 
 * other necessary properties(like recipient_status, recipient_attendees, etc)
 * that are required to preserve while saving appointment again.
 * @param array userList userList with less properties
 * @return array recipientList list with other necessary properties
 */

function createCompatibleRecipientList(userList)
{
	var insertnew;
	var recipientList = new Array();
	var recipients = new Array();
	
	//Check this is not a new appointment
	if (!module.itemProps.recipients){
		return userList;
	}

	if (isArray(module.itemProps.recipients.recipient)) {
		recipients = module.itemProps.recipients.recipient;
	} else {
		recipients = Array(module.itemProps.recipients.recipient);
	}
	
	for (var i = 0; i < userList.length; i++) {
		insertnew = 0;
		
		for (var j = 0; j < recipients.length; j++) {
			if (userList[i]["address"] == recipients[j]["email_address"]) {
				var item = new Object();
				item["name"] = recipients[j]["display_name"];
				item["address"] = recipients[j]["email_address"];
				item["type"] = userList[i]["type"];				
				item["recipient_flags"] = (recipients[j]["recipient_flags"])?recipients[j]["recipient_flags"]: recipSendable;
				item["recipient_status_num"] = recipients[j]["recipient_status_num"];
				item["recipient_attendees"] = recipients[j]["recipient_attendees"];
				recipientList.push(item);
				insertnew = 1;
				break;
			}
		}
		
		if (insertnew == 0) {
			recipientList.push(userList[i]);
		}
	}
	return recipientList;
}

function eventAppointmentDialogProposeNewTimeClick(moduleObject)
{
	// get current date & time of meeting request
	var meetingrequest_startdate = dhtml.getElementById("startdate");
	var meetingrequest_duedate = dhtml.getElementById("duedate");
	var meetingrequest_startdate_value, meetingrequest_duedate_value;

	if(typeof meetingrequest_startdate != "undefined" && typeof meetingrequest_duedate != "undefined") {
		meetingrequest_startdate_value = meetingrequest_startdate.getAttribute("unixtime");
		meetingrequest_duedate_value = meetingrequest_duedate.getAttribute("unixtime");
	} else {
		meetingrequest_startdate_value = false;
		meetingrequest_duedate_value = false;
	}

	/**
	 * For this instance we open the advprompt dialog as a normal window. If we would open it as a 
	 * modal dialog than we would have a problem when sending the proposal message. When callback in
	 * this appointment dialog is invoked it will send the proposal imediately. It wants to close the 
	 * appointment dialog, but then finds out (in IE this is an issue) that the modal dialog is still 
	 * open. It cannot close the window. By making the advprompt a normal window this issue is 
	 * avoided.
	 */
	webclient.openWindow(-1, 'appointmentitem_proposenewtimedialog', DIALOG_URL+'task=advprompt_modal', 325,250, true, appointmentdialogProposenewtime_dialog_callback, {
			moduleObject: moduleObject
		}, {
			windowname: _("Propose New Time"),
			fields: [{
				name: "combineddatetimepicker",
				label_start: _("Start time"),
				label_end: _("End time"),
				id_start: "proposed_start",
				id_end: "proposed_end",
				type: "combineddatetime",
				required: true,
				value_start: meetingrequest_startdate_value,
				value_end: meetingrequest_duedate_value
			},
			{
				name: "body",
				label: _("Comment"),
				type: "textarea",
				required: false,
				value: false
			}]
		}
	);
}
// This method is called after the user has select a time to propose for the meeting
function appointmentdialogProposenewtime_dialog_callback(result, callbackData){
	if(callbackData.moduleObject){
		var basedate = false;
		if(dhtml.getElementById("basedate").getAttribute("unixtime"))
			basedate = dhtml.getElementById("basedate").getAttribute("unixtime");

		callbackData.moduleObject.proposalMeetingRequest(parseInt(result.combineddatetimepicker.start,10), parseInt(result.combineddatetimepicker.end,10), result.body, basedate);

		window.close();
	}

}
/**
 * Callback function for checknames in appointment.
 * @param Object resolveObj obj of the resolved names
 */
function checkNamesCallBackAppointment(resolveObj)
{
	checkNamesCallBack(resolveObj, false);

	var elements = new Array("to", "cc", "bcc");
	var toccbcc = dhtml.getElementById("toccbcc");

	toccbcc.value = "";

	// Collect resolved values from ("to", "cc", "bcc") and put them together in "toccbcc".
	for (var i = 0; i < elements.length; i++){
		var value = dhtml.getElementById(elements[i]).value;
		toccbcc.value += (value.trim() != ";") ? value : "";
	}
	// Send Meeting Request
	if(module.resolveForSendingMR === true){
		submitAppointment(module.send, module.requestStatus);
	}
}

/*
 * Keycontrol function which handles edit options on appointment item.
 */
function eventAppointmentItemKeyCtrlEdit(moduleObject, element, event)
{
	switch(event.keyCombination)
	{
		case this.keys["edit_item"]["print"]:
			webclient.openModalDialog(moduleObject, 'printing', DIALOG_URL +'entryid='+ moduleObject.messageentryid +'&storeid='+ moduleObject.storeid +'&task=printitem_modal', 600, 600);
			break;
		case this.keys["edit_item"]["categorize"]:
			webclient.openModalDialog(moduleObject, 'categories', DIALOG_URL +'task=categories_modal', 350, 370, categoriesCallBack);
			break;
	}
}
/*
 * Keycontrol function which responds to appointment item
 */
function eventAppointmentItemKeyCtrlRespond(moduleObject, element, event)
{
	var send = false;
	var requestStatus = false;
	
	switch(event.keyCombination)
	{
		case this.keys["respond_meeting"]["accept"]:
		case this.keys["respond_meeting"]["tentative"]:
		case this.keys["respond_meeting"]["decline"]:
			if (moduleObject.itemProps.responsestatus > 1){
				send = false;
				requestStatus = array_search(event.keyCombination, this.keys["respond_meeting"]);
				submitAppointment(send, requestStatus);
			}
			break;
	}
}
/**
 * Keycontrol function which submits appointments item
 */
function eventAppointmentItemKeyCtrlSubmit(moduleObject, element, event)
{
	switch(event.keyCombination)
	{
		case this.keys["mail"]["save"]:
			submitAppointment(false);
			window.close();
			break;
		case this.keys["mail"]["send"]:
			if (moduleObject.itemProps.responsestatus == 1){
				submitAppointment(true);
				window.close();
			}
			break;
	}
}

function eventOpenPrintAppointmentItemDialog(moduleObject)
{
	if(moduleObject.attachNum)
		webclient.openModalDialog(moduleObject, 'printing', DIALOG_URL+'entryid='+moduleObject.messageentryid+'&storeid='+moduleObject.storeid+'&task=printitem_modal'+'&attachNum[]='+moduleObject.attachNum, 600, 600);
	else
		webclient.openModalDialog(moduleObject, 'printing', DIALOG_URL+'entryid='+moduleObject.messageentryid+'&storeid='+moduleObject.storeid+'&task=printitem_modal', 600, 600);
}

/**
 * Callback function from Suggestionlist widget
 * Function which synchronizes recipients with other tabs after new recipients are selected from suggestionlist.
 */
function suggestionListCallBackAppointment()
{
	syncRecipientFields();
}

/**
 * Function which popups option to attendee when he/she deletes any meeting item.
 *@param object moduleObject module object
 *@param string subject subject of meeting
 *@param integer basedate basedate if it is an occurrence else false
 */
function sendConfirmationDeleteAppointmentItem(moduleObject, subject, basedate)
{
	var windowData = new Array();
	windowData["module"] = moduleObject;
	windowData["confirmDelete"] = true;
	windowData["basedate"] = basedate;
	windowData["subject"] = subject;
	webclient.openModalDialog(-1, 'sendMRMailConfirmation', DIALOG_URL+'task=sendMRMailConfirmation_modal', 350, 200, appointmentItemSendMRMailConfirmation_callback, moduleObject, windowData);
}

/**
 * Callback function from sendMRConfirmation dialog
 * this function decides whether to send response to organizer that attendee has deleted item.
 * 
 *@param object moduleObject module object
 *@param boolean sendResponse true if attendee wants to send response on deletion,
 * on false it won't semd delete response mail.
 *@param integer basedate basedate if it an occurrence
 */
function appointmentItemSendMRMailConfirmation_callback(moduleObject, sendResponse, basedate)
{
	if (sendResponse) {
		moduleObject.sendDeclineMeetingRequest(false, basedate);
	} else {

		if (basedate) {
			//directlly delete passed occurrences.
			var req = new Object;
			req['store'] = moduleObject.storeid;
			req['parententryid'] = moduleObject.parententryid;
			req['entryid'] = moduleObject.messageentryid;
			req['delete'] = 1;
			req['props'] = new Object;
			req['props']['entryid'] = moduleObject.messageentryid;
			req['props']['basedate'] = parseInt(basedate, 10);

			parentWebclient.xmlrequest.addData(moduleObject, 'save', req, webclient.modulePrefix);
			parentWebclient.xmlrequest.sendRequest(true);
		} else {
			appointmentitemmodule.superclass.deleteMessage.call(moduleObject);
		}
	}
	window.close();
}
