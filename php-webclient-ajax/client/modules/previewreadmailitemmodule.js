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

previewreadmailitemmodule.prototype= new ItemModule;
previewreadmailitemmodule.prototype.constructor = previewreadmailitemmodule;
previewreadmailitemmodule.superclass = ItemModule.prototype;

function previewreadmailitemmodule(id, element)
{
	if(arguments.length > 0) {
		this.init(id, element);
	}
}

previewreadmailitemmodule.prototype.destructor = function()
{
	// clear timer for read flag
	this.clearReadFlagTimer();

	this.messageentryid = false;

	dhtml.removeEvent(window, "resize", eventPreviewItemResize);
	dhtml.removeEvents(this.element);
	dhtml.deleteAllChildren(this.element);
	webclient.inputmanager.removeObject(this);
}

previewreadmailitemmodule.prototype.init = function(id, element)
{
	this.readFlagTimer = false;

	dhtml.addClassName(element,"previewpane");
	previewreadmailitemmodule.superclass.init.call(this, id, element);

	webclient.inputmanager.addObject(this, element);
	webclient.inputmanager.bindEvent(this, "focus", eventPreviewSetFocus);
}

previewreadmailitemmodule.prototype.initializeView = function()
{
	var meetingrequest = dhtml.addElement(this.element, "div", false, "meetingrequest");
	meetingrequest.style.display = "none";
	
	var header = dhtml.addElement(this.element, "div", false, "header");
	var previewpane_size_state_classname = "folderstate_close";
	if (webclient.settings.get("folders/entryid_"+ this.parententryid +"/previewpane_header", "full")=="full"){
		previewpane_size_state_classname = "folderstate_open";
	}
	dhtml.addElement(header, "span", previewpane_size_state_classname +" previewpane_size", "previewpane_size", NBSP);
	
	var subject = dhtml.addElement(header, "span", false, "subject", NBSP);
	
	dhtml.addElement(header, "div", false, "conflict");
	
	dhtml.addElement(header, "div", false, "extrainfo");
	dhtml.addElement(header, "div", false, "from");
	
	var recipients = dhtml.addElement(header, "div", false, "recipients");
	var table = "<table border='0' cellpadding='0' cellspacing='0'>";
	table += "<tr><td class='property' width='40' nowrap>" + _("To") + ":</td><td><div id='to' class='property_data'></div></td></tr>";
	table += "<tr><td class='property' width='40' nowrap>" + _("CC") + ":</td><td><div id='cc' class='property_data'></div></td></tr>";
	table += "<tr><td class='property' width='40' nowrap>" + _("BCC") + ":</td><td><div id='bcc' class='property_data'></div></td></tr>";
	table += "</table>";
	var meetingrequestData = "<div id='meetingrequest_data'>";
	meetingrequestData += "<table border='0' cellpadding='0' cellspacing='0'>";
	meetingrequestData += "	<tr id='meetingrequest_data_startdate_row'>";
	meetingrequestData += "		<td class='property' width='40' nowrap>" + _("Start date") + ": </td>";
	meetingrequestData += "		<td><div id='meetingrequest_startdate' class='property_data'></div></td>";
	meetingrequestData += "	</tr>";
	meetingrequestData += "	<tr id='meetingrequest_data_duedate_row'>";
	meetingrequestData += "		<td class='property' width='40' nowrap>" + _("End date") + ": </td>";
	meetingrequestData += "		<td><div id='meetingrequest_duedate' class='property_data'></div></td>";
	meetingrequestData += "	</tr>";
	meetingrequestData += "	<tr id='meetingrequest_data_when_row'>";
	meetingrequestData += "		<td class='property' width='40' nowrap>" + _("When") + ": </td>";
	meetingrequestData += "		<td><div id='meetingrequest_when' class='property_data'></div></td>";
	meetingrequestData += "	</tr>";
	//Proposed Time
	meetingrequestData += "	<tr id='meetingrequest_proposed_row'>";
	meetingrequestData += "		<td class='property' width='40' nowrap>" + _("Proposed") + ": </td>";
	meetingrequestData += "		<td><span id='proposed_start_whole'></span> - <span id='proposed_end_whole'></span></td>";
	meetingrequestData += "	</tr>";
	
	meetingrequestData += "	<tr>";
	meetingrequestData += "		<td class='property' width='40' nowrap>" + _("Location") + ": </td>";
	meetingrequestData += "		<td><div id='meetingrequest_location' class='property_data'></div></td>";
	meetingrequestData += "	</tr>";
	meetingrequestData += "</table>";
	meetingrequestData += '</div>';
	recipients.innerHTML = table + meetingrequestData;

	// Task Request
	var taskrequestData = "<div id='taskrequest_data' style='display:none;'>";
	taskrequestData += "<table border='0' cellpadding='0' cellspacing='0'>";
	taskrequestData += "	<tr id='taskrequest_data_duedate_row'>";
	taskrequestData += "		<td class='property' width='40' nowrap>" + _("Due date") + ": </td>";
	taskrequestData += "		<td><div id='taskrequest_duedate' class='property_data'></div></td>";
	taskrequestData += "	</tr>";
	taskrequestData += "	<tr id='taskrequest_data_status_row'>";
	taskrequestData += "		<td class='property' width='40' nowrap>" + _("Status") + ": </td>";
	taskrequestData += "		<td><div id='taskrequest_status' class='property_data'></div></td>";
	taskrequestData += "	</tr>";
	taskrequestData += "	<tr id='taskrequest_data_priority_row'>";
	taskrequestData += "		<td class='property' width='40' nowrap>" + _("Priority") + ": </td>";
	taskrequestData += "		<td><span id='taskrequest_priority'></span></td>";
	taskrequestData += "	</tr>";
	taskrequestData += "	<tr id='taskrequest_data_complete_row'>";
	taskrequestData += "		<td class='property' width='40' nowrap>" + _("Complete") + ": </td>";
	taskrequestData += "		<td><div id='taskrequest_complete' class='property_data'></div></td>";
	taskrequestData += "	</tr>";
	taskrequestData += "</table>";
	taskrequestData += '</div>';
	recipients.innerHTML += taskrequestData;

	var atachments = dhtml.addElement(header, "div", false, "attachment_data");
	var table = "<table border='0' cellpadding='0' cellspacing='0'>";
	table += "<tr><td class='property' width='100' nowrap>" + _("Attachments") + ":</td><td><div id='attachments' class='property_data' style='padding: 5px;'></div></td></tr>";
	table += "</table>";
	atachments.innerHTML = table;
	
	// Weird bug in IE. Can't set iframe property 'frameBorder' by javascript. 
	// So innerHTML used to create an iFrame.
	// The javascript in the src attribute is to suppress the security warning in IE
	

	// Adding a scroller element to the page for iPad users
	var scrollerStartHTML = '<div id="scroller" class="ipadscroller">';
	var scrollerEndHTML = '</div>';
	// WARNING THIS MEANS THAT ALL ELEMENT REFERENCES BEFORE THIS POINT BECOME INVALID
	if (window.BROWSER_IE){
		// SSL fix for IE
		this.element.innerHTML += scrollerStartHTML + "<iframe id='html_body' onload='linkifyDOM(this.contentDocument);' frameborder='0' src=\"javascript:document.open();document.write('<html></html>');document.close();\"></iframe>" + scrollerEndHTML;
	}else{
		this.element.innerHTML += scrollerStartHTML + "<iframe id='html_body' onload='linkifyDOM(this.contentDocument);' frameborder='0'></iframe>" + scrollerEndHTML;
	}	

	var accept = dhtml.addElement(dhtml.getElementById("meetingrequest"), "span", "menubutton icon icon_accept", "accept", _("Accept"));
	var tentative = dhtml.addElement(dhtml.getElementById("meetingrequest"), "span", "menubutton icon icon_tentative" , "tentative", _("Tentative"));
	var decline = dhtml.addElement(dhtml.getElementById("meetingrequest"), "span", "menubutton icon icon_decline" , "decline", _("Decline"));
	var removefromcalendar = dhtml.addElement(dhtml.getElementById("meetingrequest"), "span", "menubutton icon icon_removefromcalendar" , "removefromcalendar", _("Remove from Calendar"));
	var proposenewtime = dhtml.addElement(dhtml.getElementById("meetingrequest"), "span", "menubutton icon icon_proposenewtime" , "proposenewtime", _("Propose New Time"));
	var notcurrent = dhtml.addElement(dhtml.getElementById("meetingrequest"), "span", "menubutton icon icon_not_current", "notcurrent", _("Not Current"));
	
	dhtml.addEvent(this, accept, "click", eventPreviewItemAcceptClick);
	dhtml.addEvent(this, tentative, "click", eventPreviewItemTentativeClick);
	dhtml.addEvent(this, decline, "click", eventPreviewItemDeclineClick);
	dhtml.addEvent(this, removefromcalendar, "click", eventPreviewItemRemoveFromCalendarClick);
	dhtml.addEvent(this, proposenewtime, "click", eventPreviewItemProposeNewTimeClick);
	dhtml.addEvent(this, notcurrent, "click", eventPreviewItemNotCurrentClick);

	dhtml.addEvent(this, accept, "mouseover", eventPreviewItemButtonOver);
	dhtml.addEvent(this, tentative, "mouseover", eventPreviewItemButtonOver);
	dhtml.addEvent(this, decline, "mouseover", eventPreviewItemButtonOver);
	dhtml.addEvent(this, removefromcalendar, "mouseover", eventPreviewItemButtonOver);
	dhtml.addEvent(this, proposenewtime, "mouseover", eventPreviewItemButtonOver);
	dhtml.addEvent(this, notcurrent, "mouseover", eventPreviewItemButtonOver);

	dhtml.addEvent(this, accept, "mouseout", eventPreviewItemButtonOver);
	dhtml.addEvent(this, tentative, "mouseout", eventPreviewItemButtonOver);
	dhtml.addEvent(this, decline, "mouseout", eventPreviewItemButtonOver);
	dhtml.addEvent(this, removefromcalendar, "mouseout", eventPreviewItemButtonOver);
	dhtml.addEvent(this, proposenewtime, "mouseout", eventPreviewItemButtonOver);
	dhtml.addEvent(this, notcurrent, "mouseout", eventPreviewItemButtonOver);
	
	dhtml.addEvent(this, dhtml.getElementById("previewpane_size"), "mouseup", eventPreviewItemSize);
}

previewreadmailitemmodule.prototype.item = function(action)
{
	// started loading a new message so remove timer set for read flag
	this.clearReadFlagTimer();

	this.deleteLoadMessage();

	var message = action.getElementsByTagName("item")[0];
	// if there is no message then return, because there is no need to show data in preview pane.
	if(!message) return false;
	
	this.initializeView();
	// remember item properties
	this.updateItemProps(message);

	/**
	 * Update module object properties with new received item properties. Could also do with this.setData() but this function
	 * takes only storeid and parententryid as agruments.
	 */
	this.rootentryid = this.itemProps['entryid'];
	this.messageentryid = this.itemProps['entryid'];

	this.setConflictInfo(message);

	if(message && message.childNodes) {

		webclient.pluginManager.triggerHook("client.module.previewreadmailitemmodule.item.before", {message: message});

		// Use functions from 'readmailitemmodule', to prevent double implementations.
		// First check if the 'readmailitemmodule' exists.
		if(readmailitemmodule && readmailitemmodule.prototype) {
			readmailitemmodule.prototype.storeid = this.storeid;
			readmailitemmodule.prototype.rootentryid = this.rootentryid;
			readmailitemmodule.prototype.messageentryid = this.messageentryid;
			readmailitemmodule.prototype.attachNum = this.attachNum;

			readmailitemmodule.prototype.setRecipients(message);
			readmailitemmodule.prototype.setProperties(message);
			readmailitemmodule.prototype.setAttachments.call(this, message);
			readmailitemmodule.prototype.setBody(message);
			readmailitemmodule.prototype.setFrom(message);
			readmailitemmodule.prototype.setRepliedForwardedInfo(message);
			readmailitemmodule.prototype.setImportanceSensitivityInfo(message);
			readmailitemmodule.prototype.setConflictAppointmentInfo(message);
		}

		var isCounterProposal = (dhtml.getXMLValue(message,"counter_proposal", 0) > 0)?true:false;
		var isRecurring = (dhtml.getXMLValue(message,"recurring", 0) > 0)?true:false;

		// Only set the meeting request fields when we are dealing with a Meeting Request.
		if(dhtml.getXMLValue(message,"message_class","").indexOf("IPM.Schedule.Meeting") === 0){
			dhtml.getElementById("meetingrequest").style.display = "block";
			dhtml.getElementById("taskrequest_data").style.display = "none";

			dhtml.getElementById("meetingrequest_startdate").innerHTML = strftime( _("%a %x %X"), dhtml.getXMLValue(message, "startdate", "") ); 
			dhtml.getElementById("meetingrequest_duedate").innerHTML   = strftime( _("%a %x %X"), dhtml.getXMLValue(message, "duedate", "") );

			// set unixtime attribute to pass it to proposenewtime dialog
			dhtml.getElementById("meetingrequest_startdate").setAttribute("unixtime", dhtml.getXMLValue(message, "startdate", ""));
			dhtml.getElementById("meetingrequest_duedate").setAttribute("unixtime", dhtml.getXMLValue(message, "duedate", ""));

			/**
			 * Set startdate/duedate or when in meeting request data pane
			 */
			var recurring_pattern = message.getElementsByTagName("recurring_pattern")[0];
			if (recurring_pattern && recurring_pattern.firstChild && recurring_pattern.firstChild.nodeValue.length > 0) {
				// Show when row when you are dealing with a recurring item...
				dhtml.getElementById("meetingrequest_data_when_row").style.display = "";
				dhtml.getElementById("meetingrequest_when").innerHTML = recurring_pattern.firstChild.nodeValue
				// ...and hide startdate/duedate
				dhtml.getElementById("meetingrequest_data_startdate_row").style.display = "none";
				dhtml.getElementById("meetingrequest_data_duedate_row").style.display = "none";
			}else{
				// Show startdate/duedate when you are NOT dealing with a recurring item...
				dhtml.getElementById("meetingrequest_data_startdate_row").style.display = "";
				dhtml.getElementById("meetingrequest_data_duedate_row").style.display = "";
				// ...and hide when row
				dhtml.getElementById("meetingrequest_data_when_row").style.display = "none";
			}
			
			if(isCounterProposal && dhtml.getXMLValue(message,"message_class","").indexOf("IPM.Schedule.Meeting.Resp") === 0){
				// Show proposed time when message is a counter proposal...
				dhtml.getElementById("meetingrequest_proposed_row").style.display = "";
			} else {
				// ...otherwise hide proposed time
				dhtml.getElementById("meetingrequest_proposed_row").style.display = "none";
			}
			dhtml.addTextNode(dhtml.getElementById("meetingrequest_location"), dhtml.getXMLValue(message, "location", ""));

			// Hide the "Remove From Calendar" button when this is not a Meeting Cancellation.
			if(dhtml.getXMLValue(message,"message_class","").indexOf("IPM.Schedule.Meeting.Canceled") !== 0){
				dhtml.getElementById("removefromcalendar").style.display = "none";
			}
			// Hide the "Accept/Decline/Tentative" buttons and "Propose New Time" button when this is not a Meeting Cancellation.
			if(dhtml.getXMLValue(message,"message_class","").indexOf("IPM.Schedule.Meeting.Request") !== 0){
				dhtml.getElementById("accept").style.display = "none";
				dhtml.getElementById("decline").style.display = "none";
				dhtml.getElementById("tentative").style.display = "none";
				dhtml.getElementById("proposenewtime").style.display = "none";
				dhtml.getElementById("notcurrent").style.display = "none";
			}else{
				if(isRecurring){
					// Hide proposenewtime buttons when you do have a RECURRING meeting REQUEST
					dhtml.getElementById("proposenewtime").style.display = "none";
				}
				
				//If meeting request is out of date.
				if (dhtml.getXMLValue(message, "out_of_date", false)){
					dhtml.getElementById("accept").style.display = "none";
					dhtml.getElementById("decline").style.display = "none";
					dhtml.getElementById("tentative").style.display = "none";
					dhtml.getElementById("proposenewtime").style.display = "none";
					
					var textMeetingResponseText = _("This meeting request was updated after this message was sent. You should open the latest update or open the item from the calendar.");
					this.showextrainfo(textMeetingResponseText, true);
				} else if(dhtml.getXMLValue(action, 'sent_representing_email_address', "").trim() == dhtml.getXMLValue(action, 'received_by_email_address', "").trim()){
					//If the meeting is created and organiser is set as attendee, inbox of the organizer do not require meeting request bar in the response msg
					dhtml.getElementById("meetingrequest").style.display = "none";
					
					this.showextrainfo(_("As the meeting organizer, you do not need to respond to the meeting"));
				} else {
					dhtml.getElementById("notcurrent").style.display = "none";
				}
			}

			// Determine whether the message is a response and the buttons should be hidden
			if(dhtml.getXMLValue(message,"message_class","").indexOf("IPM.Schedule.Meeting.Resp") === 0){
				if (dhtml.getXMLValue(action, 'appt_not_found', false)) {
					this.showextrainfo(_("This meeting is not in the Calendar; it may have been moved or deleted."));
				}

				dhtml.getElementById("meetingrequest").style.display = "none";
				// Set the meeting response text
				var textMeetingResponseText = "";
				switch(dhtml.getXMLValue(message, "message_class", false)){
					case "IPM.Schedule.Meeting.Resp.Pos":
						textMeetingResponseText += " " + _("has accepted");
						break;
					case "IPM.Schedule.Meeting.Resp.Tent":
						textMeetingResponseText += " " + _("has tentatively accepted");
						break;
					case "IPM.Schedule.Meeting.Resp.Neg":
						textMeetingResponseText += " " + _("has declined");
						break;
				}
				if(isCounterProposal){
					if(textMeetingResponseText != ""){
						textMeetingResponseText += " " + _("and");
					}
					textMeetingResponseText += " " + _("proposed a new time for this meeting");
				}
				if(textMeetingResponseText != ""){
					textMeetingResponseText = dhtml.getXMLValue(message, "sent_representing_name", "") + textMeetingResponseText + ".";
					this.showextrainfo(textMeetingResponseText);
				}
			}

		} else if (dhtml.getXMLValue(message, "message_class", "").indexOf("IPM.TaskRequest") === 0) {
			// Task request stuff
			dhtml.getElementById("taskrequest_data").style.display = "";
			dhtml.getElementById("meetingrequest_data").style.display = "none";

			// User has received task assignment
			if (dhtml.getXMLValue(message, "message_class", "") == "IPM.TaskRequest") {
				// Show only accept.decline buttons
				dhtml.getElementById("meetingrequest").style.display = "block";
				dhtml.getElementById("accept").style.display = "block";
				dhtml.getElementById("decline").style.display = "block";
				dhtml.getElementById("tentative").style.display = "none";
				dhtml.getElementById("proposenewtime").style.display = "none";
				dhtml.getElementById("notcurrent").style.display = "none";
				dhtml.getElementById("removefromcalendar").style.display = "none";
			}

			// Display task details in headers
			var duedate = dhtml.getXMLValue(message, "duedate", false);
			var duedate_text = "";
			if (duedate) {
				var startdate = dhtml.getXMLValue(message, "startdate", false);
				if (startdate) duedate_text += _("Starts on %s").sprintf(strftime("%a %x %x", startdate)) + ", ";

				if (duedate_text.length == 0) duedate_text += _("Due on %s").sprintf(strftime("%a %x %X", duedate));
				else duedate_text += _("due on %s").sprintf(strftime("%a %x %X", duedate));

				this.showextrainfo(_("Due on %s").sprintf(strftime("%a %x %X", duedate)), true);
			} else {
				duedate_text += _("None");
			}
			dhtml.getElementById("taskrequest_duedate").innerHTML = duedate_text;

			//show recurrence
			if (this.itemProps.recurring == 1 && this.itemProps.recurrProps)
				this.showextrainfo(getTaskRecurrencePattern(this.itemProps.recurrProps));

			// Task status
			var status = dhtml.getXMLValue(message, "status", 0);
			if (status == 0) status = _("Not Started");
			else if (status == 1) status = _("In Progress");
			else if (status == 2) status = _("Complete");
			else if (status == 3) status = _("Wait for other person");
			else if (status == 4) status = _("Deferred");
			dhtml.getElementById("taskrequest_status").innerHTML = status;

			// Task Importance
			var importance = dhtml.getXMLValue(message, "importance", IMPORTANCE_LOW);
			if (importance == IMPORTANCE_LOW) importance = _("Low");
			else if (importance == IMPORTANCE_NORMAL) importance = _("Normal");
			else if (importance == IMPORTANCE_HIGH) importance = _("High");
			dhtml.getElementById("taskrequest_priority").innerHTML = importance;

			dhtml.getElementById("taskrequest_complete").innerHTML = dhtml.getXMLValue(message, "percent_complete", 0)*100 +"%";

			var taskhistorydesc;
			var taskstate = dhtml.getXMLValue(message, "taskstate", tdsNOM);
			var taskhistory = dhtml.getXMLValue(message, "taskhistory", thNone);
			if(taskstate == tdsOWN) {
				taskhistorydesc = { 	1 : _("Accepted by %u on %d"),
										2 : _("Declined by %u on %d"),
										3 : _("Last update was sent by %u on %d"),
										5 : _("Assigned by %u on %d") };
			} else {
				taskhistorydesc = { 	1 : _("Accepted by %u on %d"),
										2 : _("Declined by %u on %d"),
										3 : _("Last update was received from %u on %d"),
										5 : _("Waiting for response from recipient") };
			}

			if(taskhistorydesc[taskhistory]) {
				var history = taskhistorydesc[taskhistory];

				var time = new Date(dhtml.getXMLValue(message, "assignedtime", 0)*1000);

				history = history.replace("%u", dhtml.getXMLValue(message, "tasklastdelegate", ""));
				history = history.replace("%d", time.strftime(_("%d/%m/%Y %H:%M")));

				this.showextrainfo(history);
			}
		} else {
			dhtml.getElementById("meetingrequest").style.display = "none";
			dhtml.getElementById("meetingrequest_data").style.display = "none";
		}

		if (webclient.settings.get("folders/entryid_"+this.parententryid+"/previewpane_header", "full")=="small"){
			dhtml.getElementById("recipients").style.display = "none";
			dhtml.getElementById("attachment_data").style.display = "none";
			dhtml.getElementById("from").style.display = "none";
			dhtml.removeClassName(this.header_size, "folderstate_open");
			dhtml.addClassName(this.header_size, "folderstate_close");
		}
		
	}

	if(dhtml.getXMLValue(message, "delegated_by_rule", false)) {
		// don't show this message for MRs in sent items of delegator
		if(dhtml.getXMLValue(message, "rcvd_representing_name", "") != dhtml.getXMLValue(message, "received_by_name", "")) {
			var receivedString = _("Received for") + NBSP + dhtml.getXMLValue(message, "rcvd_representing_name", "");
			this.showextrainfo(receivedString);
		}
	}

	dhtml.addEvent(this.id, window, "resize", eventPreviewItemResize);

	// update message flags in object level variable
	this.updateMessageFlags(message);

	// use timeout to give the browser some time to render the message when it is html
	var isHTML = message.getElementsByTagName("isHTML")[0];
	if(isHTML && isHTML.firstChild) {
		window.setTimeout("eventPreviewLoaded("+this.id+")",50);
	}else{
		eventPreviewLoaded(this.id);
	}

	// message is loaded now, so start read flag timer
	if(message) this.setReadFlagTimer();

	var headerElem = dhtml.getElementById("header");
	dhtml.addEvent(this, headerElem, "mousedown", forceDefaultActionEvent);
	dhtml.addEvent(this, headerElem, "mouseup", forceDefaultActionEvent);
	dhtml.addEvent(this, headerElem, "selectstart", forceDefaultActionEvent);
	dhtml.addEvent(this, headerElem, "mousemove", forceDefaultActionEvent);
}

/**
 * Function will create a timer based on user specified time in settings
 * so after user specified time message will be marked as read and also read receipt
 * will be sent
 */
previewreadmailitemmodule.prototype.setReadFlagTimer = function()
{
	if(this.messageentryid){
		var readFlagTime = webclient.settings.get("global/mail_readflagtime", "0");
		var module = this;		// Fix loss of scope in timeout
		this.readFlagTimer = window.setTimeout(function() {
			module.setReadFlag((module.sendReadReceipt() ? "read,receipt" : "read,noreceipt"));
		}, parseInt(readFlagTime, 10) * 1000);
	}
}

/**
 * Function will clear the timer that was set for marking a mail as read
 * after user specified time
 */
previewreadmailitemmodule.prototype.clearReadFlagTimer = function()
{
	if(this.readFlagTimer){
		window.clearTimeout(this.readFlagTimer);
		this.readFlagTimer = false;
	}
}

/**
 * Function will update message flags value to object level variable
 * when an update of message arrives in maillistmodule, it checks if message is loaded in preview pane
 * then it updates this variable so previewitem module timer will not again send request for setting
 * a read flag
 */
previewreadmailitemmodule.prototype.updateMessageFlags = function(message)
{
	var flags = parseInt(dhtml.getXMLValue(message, "message_flags", 0), 10);

	if(!isNaN(flags)) {
		this.itemMessageFlags = flags;
	}
}

/**
 * Funtion will send a request to server to set read flag for an unread message
 *
 * @param String flag comma seperated string of flags (receipt, noreceipt, read)
 */
previewreadmailitemmodule.prototype.setReadFlag = function(flag)
{
	// prevent request if message was already marked as read
	if ((this.itemMessageFlags & MSGFLAG_READ) != MSGFLAG_READ){
		var data = new Object();

		// Since we are directly opening task item from Task folder in previewpane, we need to access taskrequest entry ids here.
		if (typeof this.itemProps.taskrequest != "undefined") {
			data["store"] = this.itemProps.taskrequest.storeid;
			data["parententryid"] = this.itemProps.taskrequest.parententryid;
			data["entryid"] = this.itemProps.taskrequest.entryid;
		} else {
			data["store"] = this.storeid;
			data["parententryid"] = this.parententryid;
			data["entryid"] = this.messageentryid;
		}

		if(flag) {
			data["flag"] = flag;
		}
	
		webclient.xmlrequest.addData(this, "read_flag", data);
		webclient.xmlrequest.sendRequest();
	}
}

previewreadmailitemmodule.prototype.resize = function()
{
	var docHeight = dhtml.getBrowserInnerSize().y;

	this.element.style.height = docHeight - dhtml.getElementTop(this.element) - 10 + "px";

	// Both the body as the scroller element need to be resized.
	var html_body = dhtml.getElementById("html_body");
	var scrollerElem = document.getElementById('scroller');
	if (html_body){
		scrollerElem.style.width = (this.element.offsetWidth - 30) + "px";
		html_body.style.width = (this.element.offsetWidth - 30) + "px";

		var height = (this.element.offsetHeight - html_body.offsetTop) - 40;
		if(height < 3) {
			height = 3;
		}

		scrollerElem.style.height = height + "px";
		html_body.style.height = height + "px";
	}
}

/**
 * Function will show a message to user if he wants to send a read receipt or not
 * message should be displayed or not is decided upon user specified settings
 */
previewreadmailitemmodule.prototype.sendReadReceipt = function()
{
	var result = false;
	if (((this.itemMessageFlags & MSGFLAG_READ) != MSGFLAG_READ) && (this.itemMessageFlags & MSGFLAG_RN_PENDING) == MSGFLAG_RN_PENDING){
		switch(webclient.settings.get("global/readreceipt_handling", "ask")){
			case "ask":
				result = confirm(_("The sender of this message has asked to be notified when you read this message.")+"\n"+_("Do you wish to notify the sender?"));
				break;
			case "never":
				result = false;
				break;
			case "always":
				result = true;
				break;
		}
	}
	return result;
}

function eventPreviewLoaded(moduleID)
{
	// Remove contextmenu and other elements onmousedown and onscroll in iFrame.
	var html_body = dhtml.getElementById("html_body");
	if(html_body) {
		if(!html_body.contentWindow.document.body){
			html_body.contentWindow.document.appendChild(html_body.contentWindow.document.createElement("body"));
			html_body.contentWindow.document.body.innerHTML = "&nbsp;";
		}
		
		if (html_body.contentWindow.document.body.innerHTML == "") {
			html_body.contentWindow.document.body.innerHTML = "&nbsp;";
		}
		
		html_body.contentWindow.document.body.style.height = "100%";
		html_body.contentWindow.document.body.style.margin = "0";
		
		html_body.contentWindow.document.body.onmousedown = eventPreviewItemMouseDown;
		html_body.contentWindow.document.body.onmouseup = eventPreviewItemMouseUp;
		html_body.contentWindow.onscroll = eventPreviewItemMouseDown;	
	}

	webclient.getModule(moduleID).resize();
}

function eventPreviewItemResize(moduleObject, element, event)
{
	moduleObject.resize();
}

function eventPreviewItemMouseDown()
{
	dhtml.executeEvent(document.body, "mouseup");
}

/**
 * This function is used to propagate iframe mouseup event to main
 * body's mouseup event so when dropping an item to iframe will be handled properly
 */
function eventPreviewItemMouseUp()
{
	dhtml.executeEvent(document.body, "mouseup");
}

function eventPreviewItemAcceptClick(moduleObject, element, event)
{
	var messageClass = moduleObject.itemProps["message_class"];
	if (messageClass == "IPM.TaskRequest") {
		moduleObject.sendAcceptTaskRequest();
	} else {
		var windowData = new Object();
		windowData['module'] = moduleObject;
		windowData['requestStatus'] = 'accept';
		webclient.openModalDialog(-1, 'sendMRMailConfirmation', DIALOG_URL+'task=sendMRMailConfirmation_modal', 320, 280, previewreadmailitemSendMRMailConfirmation_dialog_callback, null, windowData);
	}
}

function eventPreviewItemTentativeClick(moduleObject, element, event)
{
	var windowData = new Object();
	windowData['module'] = moduleObject;
	windowData['requestStatus'] = 'tentative';
	webclient.openModalDialog(-1, 'sendMRMailConfirmation', DIALOG_URL+'task=sendMRMailConfirmation_modal', 320, 280, previewreadmailitemSendMRMailConfirmation_dialog_callback, null, windowData);
}

function eventPreviewItemDeclineClick(moduleObject, element, event)
{
	var messageClass = moduleObject.itemProps["message_class"];
	if (messageClass == "IPM.TaskRequest") {
		moduleObject.sendDeclineTaskRequest();
	} else {
		var windowData = new Object();
		windowData['module'] = moduleObject;
		windowData['requestStatus'] = 'decline';
		webclient.openModalDialog(-1, 'sendMRMailConfirmation', DIALOG_URL+'task=sendMRMailConfirmation_modal', 320, 280, previewreadmailitemSendMRMailConfirmation_dialog_callback, null, windowData);
	}
}

function eventPreviewItemProposeNewTimeClick(moduleObject, element, event)
{
	// get current date & time of meeting request
	var meetingrequest_startdate = dhtml.getElementById("meetingrequest_startdate");
	var meetingrequest_duedate = dhtml.getElementById("meetingrequest_duedate");
	var meetingrequest_startdate_value, meetingrequest_duedate_value;

	if(typeof meetingrequest_startdate != "undefined" && typeof meetingrequest_duedate != "undefined") {
		meetingrequest_startdate_value = meetingrequest_startdate.getAttribute("unixtime");
		meetingrequest_duedate_value = meetingrequest_duedate.getAttribute("unixtime");
	} else {
		meetingrequest_startdate_value = false;
		meetingrequest_duedate_value = false;
	}

	webclient.openModalDialog(-1, 'previewreadmailitem_proposenewtimedialog', DIALOG_URL+'task=advprompt_modal', 325,250, previewreadmailitemProposenewtime_dialog_callback, {
			moduleObject: moduleObject
		}, {
			windowname: _("Propose New Time"),
			dialogtitle: _("Propose New Time"),
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
function previewreadmailitemProposenewtime_dialog_callback(result, callbackData){
	if(callbackData.moduleObject){
		callbackData.moduleObject.proposalMeetingRequest(parseInt(result.combineddatetimepicker.start,10), parseInt(result.combineddatetimepicker.end,10), result.body);
	}

}

function previewreadmailitemSendMRMailConfirmation_dialog_callback(noResponse)
{
	var body = false;
	if(typeof noResponse == "object"){
		body = noResponse.body;
		noResponse = noResponse.type;
	}

	if(this.windowData) {
		var moduleObject = this.windowData["module"]; 
		var requestStatus = this.windowData["requestStatus"]; 
	}

	switch(requestStatus)
	{
		case "accept":
			moduleObject.acceptMeetingRequest(noResponse, false, body);
			break;
		case "tentative":
			moduleObject.tentativeMeetingRequest(noResponse, false, body);
			break;
		case "decline":
			moduleObject.declineMeetingRequest(noResponse, false, body);
			break;
	}
}

function eventPreviewItemButtonOver(moduleObject, element, event)
{
	dhtml.toggleClassName(element, "menubuttonover");
}

function eventPreviewItemSize(moduleObject, element, event)
{
	if (webclient.settings.get("folders/entryid_"+moduleObject.parententryid+"/previewpane_header", "full")=="full"){
		dhtml.getElementById("recipients").style.display = "none";
		dhtml.getElementById("attachment_data").style.display = "none";
		dhtml.getElementById("from").style.display = "none";
		dhtml.removeClassName(element, "folderstate_open");
		dhtml.addClassName(element, "folderstate_close");
		webclient.settings.set("folders/entryid_"+moduleObject.parententryid+"/previewpane_header", "small");
	}else{
		dhtml.getElementById("recipients").style.display = "block";
		dhtml.getElementById("attachment_data").style.display = "block";
		dhtml.getElementById("from").style.display = "block";
		dhtml.removeClassName(element, "folderstate_close");
		dhtml.addClassName(element, "folderstate_open");
		webclient.settings.set("folders/entryid_"+moduleObject.parententryid+"/previewpane_header", "full");
	}

	moduleObject.resize();
}

function eventPreviewSetFocus(moduleObject, element, event)
{
	var html_body = dhtml.getElementById("html_body");
	if (html_body)
		html_body.contentWindow.focus();
}

function eventPreviewItemRemoveFromCalendarClick(moduleObject, element, event)
{
    moduleObject.removeFromCalendar();
}

/**
 * Function which shows extrainfo about the item.
 * @param string value information that is to be shown.
 */
previewreadmailitemmodule.prototype.showextrainfo = function (value, deleteAllChildren)
{
	var elemExtraInfo = dhtml.getElementById("extrainfo");

	if (typeof deleteAllChildren != 'undefined' && deleteAllChildren) {
		dhtml.deleteAllChildren(elemExtraInfo);
	}

	dhtml.addElement(elemExtraInfo, "p", false, false, value);
	elemExtraInfo.style.display = "block";
}