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

readmailitemmodule.prototype = new ItemModule;
readmailitemmodule.prototype.constructor = readmailitemmodule;
readmailitemmodule.superclass = ItemModule.prototype;

function readmailitemmodule(id)
{
	if(arguments.length > 0) {
		this.init(id);
	}
}

readmailitemmodule.prototype.init = function(id)
{
	readmailitemmodule.superclass.init.call(this, id);

	this.keys["respond_mail"] = KEYS["respond_mail"];
	this.keys["edit_item"] = KEYS["edit_item"];
	this.keys["respond_meeting"] = KEYS["respond_meeting"];

	// Add keycontrol events
	webclient.inputmanager.addObject(this);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["respond_mail"], "keyup", eventReadMailItemKeyCtrlRespond);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["edit_item"], "keyup", eventReadMailItemKeyCtrlEdit);
}

readmailitemmodule.prototype.item = function(action)
{
	var message = action.getElementsByTagName("item")[0];

	if(message && message.childNodes) {

		webclient.pluginManager.triggerHook("client.module.readmailitemmodule.item.before", {message: message});

		this.setRepliedForwardedInfo(message);

		this.setImportanceSensitivityInfo(message);

		if(dhtml.getXMLValue(message, "delegated_by_rule", false)) {
			// don't show this message for MRs in sent items of delegator
			if(dhtml.getXMLValue(message, "rcvd_representing_name", "") != dhtml.getXMLValue(message, "received_by_name", "")) {
				var receivedString = _("Received for") + NBSP + dhtml.getXMLValue(message, "rcvd_representing_name", "");
				var extrainfo = dhtml.getElementById("extrainfo");
				if(extrainfo) {
					dhtml.addElement(extrainfo, "p", false, false, receivedString);
					extrainfo.style.display = "block";
				}
			}
		}
	}

	readmailitemmodule.superclass.item.call(this, action);
	this.setMeetingrequest(message);
	this.setConflictAppointmentInfo(message);
	this.setWindowTitle(message);
	window.onresize();
}

readmailitemmodule.prototype.setImportanceSensitivityInfo = function(message)
{
	// display importance/sensitivity state
	var importance = dhtml.getXMLValue(message, "importance", IMPORTANCE_NORMAL);
	var sensitivity = dhtml.getXMLValue(message, "sensitivity", SENSITIVITY_NONE);
	if (importance!=IMPORTANCE_NORMAL || sensitivity!=SENSITIVITY_NONE){
		var extrainfo = dhtml.getElementById("extrainfo");

		var infoString = false;
		if(sensitivity == SENSITIVITY_PERSONAL) {
			infoString = _("Please treat this as Personal") + ".";
		} else if(sensitivity == SENSITIVITY_PRIVATE) {
			infoString = _("Please treat this as Private") + ".";
		} else if(sensitivity == SENSITIVITY_COMPANY_CONFIDENTIAL) {
			infoString = _("Please treat this as Confidential") + ".";
		}

		if(infoString) {
			dhtml.addElement(extrainfo, "p", false, false, infoString);
			extrainfo.style.display = "block";
		}

		infoString = false;
		if (importance == IMPORTANCE_LOW){
			infoString = _("This message was sent with Low importance") + ".";
		}else if (importance == IMPORTANCE_HIGH){
			infoString = _("This message was sent with High importance") + ".";
		}

		if(infoString) {
			dhtml.addElement(extrainfo, "p", false, false, infoString);
			extrainfo.style.display = "block";
		}
	}
}

readmailitemmodule.prototype.setRepliedForwardedInfo = function(message)
{
	// Replied or Forwarded
	var last_verb_executed = message.getElementsByTagName("last_verb_executed")[0];
	if(last_verb_executed && last_verb_executed.firstChild) {
		var time = false;
		var last_verb_execution_time = dhtml.getTextNode(message.getElementsByTagName("last_verb_execution_time")[0]);
		if(last_verb_execution_time) {
			time = strftime(_("%a %x %X"),last_verb_execution_time);
		}

		var infoString = false;
		switch(last_verb_executed.firstChild.nodeValue)
		{
			case "102":
				infoString = _("You replied this message")+".";
				if (time)
					infoString = _("You replied this message on %s")+".";
				break;
			case "103":
				infoString = _("You replied this message to all")+".";
				if (time)
					infoString = _("You replied this message to all on %s")+".";
				break;
			case "104":
				infoString = _("You forwarded this message")+".";
				if (time)
					infoString = _("You forwarded this message on %s")+".";
				break;
		}

		if (infoString){
			var extrainfo = dhtml.getElementById("extrainfo");

			if(extrainfo) {
				infoString = infoString.sprintf(time);

				dhtml.addElement(extrainfo, "p", false, false, infoString);
				extrainfo.style.display = "block";
			}
		}
	}
}

readmailitemmodule.prototype.setAttachments = function(message)
{
	var attachments = message.getElementsByTagName("attachment");
	var showDownloadAll = false;

	if(attachments && attachments.length > 0) {
		var attachmentsElement = dhtml.getElementById("attachments");

		if(attachmentsElement) {
			for(var i = 0; i < attachments.length; i++)
			{
				var attachment = attachments[i];
				var attach_num = attachment.getElementsByTagName("attach_num")[0];
				var attach_method = attachment.getElementsByTagName("attach_method")[0];
				var name = attachment.getElementsByTagName("name")[0];
				var size = attachment.getElementsByTagName("size")[0];
				var cid = attachment.getElementsByTagName("cid")[0];
				var hidden = dhtml.getXMLValue(attachment, "hidden", false);

				if(attach_num && attach_num.firstChild && !hidden) {
					if(name) {
						var attachmentElement = dhtml.addElement(attachmentsElement, "a", "attachment");
						attachmentElement.setAttribute("attach_num", dhtml.getTextNode(attach_num, false));
						attachmentElement.href = "#";

						if(size && size.firstChild) {
							var kb = Math.round(size.firstChild.nodeValue / 1024) + _("kb");
							if(size.firstChild.nodeValue < 1024) {
								kb = size.firstChild.nodeValue + _("B");
							}
						}

						if(kb) {
							dhtml.addTextNode(attachmentElement, dhtml.getTextNode(name, _("Untitled Attachment")) + " (" + kb + ");"+ NBSP );
						} else {
							dhtml.addTextNode(attachmentElement, dhtml.getTextNode(name, _("Untitled Attachment")) + ";"+ NBSP );
						}

						dhtml.addEvent(this, attachmentElement, "mousedown", eventAttachmentClick);
					}
				}
				var attach_method = dhtml.getXMLValue(attachment, "attach_method", "");
				if(attach_method != "5" && attach_method != "false"){
					showDownloadAll = true;
				}
			}
			if(attachments.length > 0 && attachmentsElement.getElementsByTagName("a").length > 1 && showDownloadAll){
				var downAllAttach = dhtml.addElement(null, "span", "downloadAllAttach", "", _("Download all attachments"));
				attachmentsElement.insertBefore(downAllAttach, attachmentsElement.getElementsByTagName("a")[0]);
				dhtml.addEvent(this, downAllAttach, "mousedown", eventDownloadAllAttachmentsAsZipArchive);
			}
			if(attachmentsElement.offsetHeight > 43) {
				attachmentsElement.style.height = "45px";
			}
		}
	}else{
		var attachmentsElement = dhtml.getElementById("attachment_data");
		if (attachmentsElement){
			dhtml.deleteAllChildren(attachmentsElement);
		}
	}
}

readmailitemmodule.prototype.setBody = function(message)
{
	var html_body = dhtml.getElementById("html_body");

	if(html_body) {
		var body = message.getElementsByTagName("body")[0];
		var isHTML = message.getElementsByTagName("isHTML")[0];

		if(body && body.childNodes.length > 0) {
			var content = "";
			var element = body.firstChild;
			for(var i = 0; i < body.childNodes.length; i++)
			{
				content += element.nodeValue;
				element = element.nextSibling;
			}

			if(!isHTML || !isHTML.firstChild) {
				content = convertPlainToHtml(content);
				content = "<html><body><pre wrap style=\"white-space: -moz-pre-wrap; white-space: -pre-wrap; white-space: -o-pre-wrap; white-space: pre-wrap; word-wrap: break-word;\">" + content + "</pre></body></html>";
			} else {
			    content = convertAnchors(content);
			}

			var data = new Object();
			data["content"] = content;
			webclient.pluginManager.triggerHook("client.module.readmailitemmodule.setbody.predisplay", data);
			// Magical whiteline fix. Somehow this whiteline makes the content
			// variable to not hold undefined as value after the next line
			// "I'll take weird fixes for 500, Alex"
			content = data["content"];

			html_body.contentWindow.document.open();
			html_body.contentWindow.document.write(content);
			html_body.contentWindow.document.close();

			// Register events for keycontrol
			dhtml.addEvent(webclient.inputmanager, html_body.contentWindow.document, "keyup", eventInputManagerKeyControlKeyUp);
			dhtml.addEvent(webclient.inputmanager, html_body.contentWindow.document, "keydown", eventInputManagerKeyControlKeyDown);

			// Register the touch functions to make scrolling work within the scroller-element for iPad users
			this.touch = {
				startY: 0,
				startX: 0,
				scroller: document.getElementById("scroller")
			};
			dhtml.addEvent(this, html_body.contentWindow.document.body, "touchstart", eventReadmailScrollBodyTouchStart);
			dhtml.addEvent(this, html_body.contentWindow.document.body, "touchmove", eventReadmailScrollBodyTouchMove);

			var data = new Object();
			data["iframedocument"] = html_body.contentWindow.document;
			webclient.pluginManager.triggerHook("client.module.readmailitemmodule.setbody.postdisplay", data);
		}
	}
}

readmailitemmodule.prototype.setMeetingrequest = function(message)
{
	var isCounterProposal = (dhtml.getXMLValue(message,"counter_proposal", 0) > 0)?true:false;
	var isRecurring = (dhtml.getXMLValue(message,"recurring", 0) > 0)?true:false;
	var sentemailAddress = dhtml.getXMLValue(message, "sent_representing_email_address", "").trim();
	var receivedemailAddress = dhtml.getXMLValue(message, "received_by_email_address", "").trim();
	/**
	 * Message class types:
	 * - IPM.Schedule.Meeting
	 * - IPM.Schedule.Meeting.Request
	 * - IPM.Schedule.Meeting.Resp
	 * - IPM.Schedule.Meeting.Resp.Pos
	 * - IPM.Schedule.Meeting.Resp.Tent
	 * - IPM.Schedule.Meeting.Resp.Neg
	 * - IPM.Schedule.Meeting.Canceled
	 */
	var showMeetingElements = new Array();
	var hideMeetingElements = new Array();
	if(dhtml.getXMLValue(message,"message_class","").indexOf("IPM.Schedule.Meeting") === 0){
		// Show meetingrequest pane (location and startstart/duedate or when)
		showMeetingElements.push("meetingrequest");

		if(dhtml.getXMLValue(message,"message_class","").indexOf("IPM.Schedule.Meeting.Request") === 0){

			if (dhtml.getXMLValue(message, "out_of_date", false)){
				//Hide accept/ decline/ tentative buttons when request is out-of-date
				hideMeetingElements.push("accept");
				hideMeetingElements.push("tentative");
				hideMeetingElements.push("decline");
				hideMeetingElements.push("proposenewtime");

				var textMeetingResponseText = _("This meeting request was updated after this message was sent. You should open the latest update or open the item from the calendar.");
				var elemExtraInfo = dhtml.getElementById("extrainfo");
				dhtml.addElement(elemExtraInfo, "p", false, false, textMeetingResponseText);
				elemExtraInfo.style.display = "block";
			}else if(sentemailAddress == receivedemailAddress){
				//Hide accept/ decline/ tentative/ notcurrent buttons when request is out-of-date
				hideMeetingElements.push("accept");
				hideMeetingElements.push("tentative");
				hideMeetingElements.push("decline");
				hideMeetingElements.push("proposenewtime");
				hideMeetingElements.push("not_current");

				//If the meeting is created and organiser is set as attendee, inbox of the organizer do not require meeting request bar in the response msg
				var textMeetingResponseText = _("As the meeting organizer, you do not need to respond to the meeting");
				var elemExtraInfo = dhtml.getElementById("extrainfo");
				dhtml.addElement(elemExtraInfo, "p", false, false, textMeetingResponseText);
				elemExtraInfo.style.display = "block";
			} else {
				hideMeetingElements.push("not_current");

				// Show Accept/Tentative/Decline buttons only when you have a Meeting REQUEST and is not out-of-date.
				showMeetingElements.push("accept");
				showMeetingElements.push("tentative");
				showMeetingElements.push("decline");

				if(!isRecurring){
					// Show proposenewtime buttons when you have a non-recurring meeting REQUEST
					showMeetingElements.push("proposenewtime");
				} else {
					// Hide proposenewtime buttons when you do NOT have a meeting REQUEST or a recurring one
					hideMeetingElements.push("proposenewtime");
				}

				// Since message is a meeting request, so add keycontrol for responding to meeting request.
				webclient.inputmanager.bindKeyControlEvent(this, this.keys["respond_meeting"], "keyup", eventReadMailItemKeyCtrlRespondMR);
			}
		}else{
			// Hide Accept/Tentative/Decline buttons when NOT a Meeting REQUEST
			hideMeetingElements.push("accept");
			hideMeetingElements.push("tentative");
			hideMeetingElements.push("decline");
			// Hide proposenewtime buttons when you do NOT have a meeting REQUEST
			hideMeetingElements.push("proposenewtime");
			hideMeetingElements.push("not_current");
		}

		if(dhtml.getXMLValue(message,"message_class","").indexOf("IPM.Schedule.Meeting.Canceled") === 0){
			// Show Removefromcalendar button when you are dealing with an CANCELED meeting
			showMeetingElements.push("removefromcalendar");
		}else{
			// Show Removefromcalendar button when you are NOT dealing with an CANCELED meeting
			hideMeetingElements.push("removefromcalendar");
		}

		var recurring_pattern = message.getElementsByTagName("recurring_pattern")[0];
		if (recurring_pattern && recurring_pattern.firstChild && recurring_pattern.firstChild.nodeValue.length > 0) {
			// Show when row when you are dealing with a recurring item...
			showMeetingElements.push("meetingrequest_when_row");
			dhtml.getElementById("when").innerHTML = recurring_pattern.firstChild.nodeValue
			// ...and hide startdate/duedate
			hideMeetingElements.push("meetingrequest_startdate_row");
			hideMeetingElements.push("meetingrequest_duedate_row");
		}else{
			// Show startdate/duedate when you are NOT dealing with a recurring item...
			showMeetingElements.push("meetingrequest_startdate_row");
			showMeetingElements.push("meetingrequest_duedate_row");
			// ...and hide when row
			hideMeetingElements.push("meetingrequest_when_row");
		}

		//Proposed Time
		if(isCounterProposal && dhtml.getXMLValue(message,"message_class","").indexOf("IPM.Schedule.Meeting.Resp") === 0){
			// Show ProposeNewTime row when message is a counter proposal
			showMeetingElements.push("meetingrequest_proposed_row");

			if (dhtml.getXMLValue(message, 'appt_not_found', false)) {
				var elemExtraInfo = dhtml.getElementById("extrainfo");
				dhtml.addElement(elemExtraInfo, "p", false, false, _("This meeting is not in the Calendar; it may have been moved or deleted."));
				elemExtraInfo.style.display = "block";
				hideMeetingElements.push("accept_proposal");
				hideMeetingElements.push("view_all_proposals");
			} else {
				showMeetingElements.push("accept_proposal");
				showMeetingElements.push("view_all_proposals");
			}
		} else {
			// Hide ProposeNewTime row when message is NOT a counter proposal
			hideMeetingElements.push("meetingrequest_proposed_row");
			hideMeetingElements.push("accept_proposal");
			hideMeetingElements.push("view_all_proposals");
		}
	}else{
		// Hide meetingrequest pane (location and startstart/duedate or when)
		hideMeetingElements.push("meetingrequest");
		// Hide menu buttons when NOT a Meeting
		hideMeetingElements.push("accept");
		hideMeetingElements.push("tentative");
		hideMeetingElements.push("decline");
		hideMeetingElements.push("removefromcalendar");
		hideMeetingElements.push("proposenewtime");
		hideMeetingElements.push("not_current");
		hideMeetingElements.push("accept_proposal");
		hideMeetingElements.push("view_all_proposals");
	}

	for(var elID in hideMeetingElements){
		var el = dhtml.getElementById(hideMeetingElements[elID]);
		if (el) el.style.display = "none";
	}
	for(var elID in showMeetingElements){
		var el = dhtml.getElementById(showMeetingElements[elID]);
		if (el){
			if(el.nodeName == "TR"){
				el.style.display = "";
			}else{
				el.style.display = "block";
			}
		}
	}

}

/**
 * Set recipient information if required / found
 */
readmailitemmodule.prototype.setRecipients = function(message)
{
	//get from recipients and add them to id="from"
	var fromElement = dhtml.getElementById("from");
	if(fromElement) {
		var sent_representing_name = dhtml.getTextNode(message.getElementsByTagName("sent_representing_name")[0],"");
		var sent_representing_email_address = dhtml.getTextNode(message.getElementsByTagName("sent_representing_email_address")[0],"");

		var sender_email_address = dhtml.getTextNode(message.getElementsByTagName("sender_email_address")[0],"");
		var sender_name = dhtml.getTextNode(message.getElementsByTagName("sender_name")[0],"");

		var sender_representation = "";
		if(sent_representing_name.length > 0) {
			sender_representation = sent_representing_name;
		}

		if(sent_representing_email_address.length > 0 && sent_representing_email_address != sent_representing_name) {
			sender_representation += " <"+sent_representing_email_address+">";
		}

		// we have an other sender, show "on behalf of" message
		if((sender_name.length > 0 || sender_email_address.length > 0) && sender_email_address != sent_representing_email_address){
			var sender = "";
			if(sender_name.length > 0) {
				sender += sender_name;
			}

			if(sender_email_address.length > 0 && sender_email_address != sender_name) {
				sender += " <"+sender_email_address+">";
			}

			var recipientItemSenderRepresentation = dhtml.addElement(fromElement,"a","emailaddress","sender_label", sender);
			recipientItemSenderRepresentation.href = "#";
			dhtml.addEvent(this, recipientItemSenderRepresentation, "click", eventReadmailClickEmail);
			dhtml.addEvent(this, recipientItemSenderRepresentation, "contextmenu", eventReadmailAddressContextMenu);

			dhtml.addElement(fromElement,"span","emailaddress","", NBSP+_("on behalf of")+NBSP);
		}

		var recipientItemSenderRepresentation = dhtml.addElement(fromElement,"a","emailaddress","senderrepresentatation_label",sender_representation);
		recipientItemSenderRepresentation.href = "#";
		dhtml.addEvent(this, recipientItemSenderRepresentation, "click", eventReadmailClickEmail);
		dhtml.addEvent(this, recipientItemSenderRepresentation, "contextmenu", eventReadmailAddressContextMenu);
	}

	//get to and cc recipient and add them to id="to" and id="cc"
	var recipients = message.getElementsByTagName("recipient");
	if(recipients && recipients.length > 0) {
        var elements = new Array();

        elements["to"] = dhtml.getElementById("to");
        elements["cc"] = dhtml.getElementById("cc");
        elements["bcc"] = dhtml.getElementById("bcc");

		for(var i = 0; i < recipients.length; i++)
		{
			var recipient = recipients[i];
			var name = dhtml.getTextNode(recipient.getElementsByTagName("display_name")[0],"");
			var email_address = dhtml.getTextNode(recipient.getElementsByTagName("email_address")[0],"");
			var type = dhtml.getTextNode(recipient.getElementsByTagName("type")[0],"");


			if(type.length > 0) {
				var element = elements[type];

				if(element) {
					var recipientString = "";
                    if(element.firstChild != null) {
						dhtml.addElement(element,"span","","","; ");
					}
					if(name.length > 0) {
						recipientString += name;
					}
					if(recipientString.length > 0 && name != email_address) {
						recipientString += " <"+email_address+">";
					}
					var recipientItem = dhtml.addElement(element,"a","emailaddress",type+"_label"+i,recipientString);
					recipientItem.href = "#";
					dhtml.addEvent(this, recipientItem, "click", eventReadmailClickEmail);
					dhtml.addEvent(this, recipientItem, "contextmenu", eventReadmailAddressContextMenu);
				}
			}
		}

		var toElement = dhtml.getElementById("to");
		if(toElement && toElement.offsetHeight > 43) {
			toElement.style.height = "45px";
		}

		var ccElement = dhtml.getElementById("cc");
		if(ccElement && ccElement.offsetHeight > 43) {
			ccElement.style.height = "45px";
		}
		if (ccElement && ccElement.innerHTML.trim() == ""){
			ccElement.parentNode.parentNode.style.display = "none";
		}

		var bccElement = dhtml.getElementById("bcc");
		if(bccElement && bccElement.offsetHeight > 43) {
			bccElement.style.height = "45px";
		}
		if (bccElement && bccElement.innerHTML.trim() == ""){
			bccElement.parentNode.parentNode.style.display = "none";
		}


	}
}
readmailitemmodule.prototype.setReadFlag = function(messageEntryid, flag)
{
	if ((this.itemProps.message_flags & MSGFLAG_READ) == MSGFLAG_READ){
		var data = new Object();
		data["store"] = this.storeid;
		data["parententryid"] = this.parententryid;
		data["entryid"] = messageEntryid;

		if(flag) {
			data["flag"] = flag;
		}

		if(typeof parentWebclient != "undefined") {
			parentWebclient.xmlrequest.addData(this, "read_flag", data, webclient.modulePrefix);
			parentWebclient.xmlrequest.sendRequest(true);
		} else {
			webclient.xmlrequest.addData(this, "read_flag", data);
			webclient.xmlrequest.sendRequest();
		}
	}
}
readmailitemmodule.prototype.setConflictAppointmentInfo = function(message)
{
	var conflictMsg = dhtml.getXMLValue(message, 'meetingconflicting', false);

	if (conflictMsg){
		var extrainfo = dhtml.getElementById("extrainfo");

		if(extrainfo) {
			dhtml.deleteAllChildren(extrainfo);
			dhtml.addElement(extrainfo, "p", false, false, conflictMsg);
			extrainfo.style.display = "block";
		}
	}
}

readmailitemmodule.prototype.setWindowTitle = function(message){
	//get the subject value, if subject is empty then set Untitled.
	var subject = dhtml.getXMLValue(message, "subject", _("Untitled"));

	// if subject length is more than 50 characters then truncate them to 50 characters and add ellipsis
	if(subject.length > 50)
		subject = subject.substring(0,50) + "...";

	// set subject as document title.
	document.title = subject;
}

/**
 * Function which opens corresponding meeting from Calendar.
 * @param boolean viewAllProposals true if we want to view all proposals else false
 */
readmailitemmodule.prototype.openMeeting = function(viewAllProposals){
	if (parentWebclient && this.itemProps && this.itemProps.appointment){
		var extraParams = ""

		extraParams += "&counterproposal=1";
		extraParams += "&proposedstartdate="+ this.itemProps.proposed_start_whole;
		extraParams += "&proposedenddate="+ this.itemProps.proposed_end_whole;
		if(this.itemProps.appointment.basedate != "" && typeof this.itemProps.appointment.basedate != "undefined") {
			extraParams += "&basedate="+ this.itemProps.appointment.basedate;
		}

		if (viewAllProposals)
			extraParams += "&viewallproposals=1";

		// Open window with reference to main webclient because ReadMail dialog is going to be closed.
		parentWebclient.openWindow(-1, "appointment", DIALOG_URL+"task=appointment_standard&storeid="+ this.itemProps.appointment.storeid +"&parententryid="+ this.itemProps.appointment.parententryid +"&entryid="+ this.itemProps.appointment.entryid + extraParams).focus();
	}
	window.close();
}

function eventReadmailAddressContextMenu(moduleObject, element, event)
{
	var items = new Array();
	items.push(webclient.menu.createMenuItem("createmail", _("Send mail (%s)").sprintf(dhtml.getTextNode(element,"")), false, eventReadmailContextEmail));
	items.push(webclient.menu.createMenuItem("seperator", ""));
	items.push(webclient.menu.createMenuItem("contact", _("Add to Contacts"), false, eventReadmailContextAddContact));

	webclient.menu.buildContextMenu(moduleObject.id, element.id, items, event.clientX, event.clientY);
}

function eventReadmailContextEmail(moduleObject, element, event)
{
	element.parentNode.style.display="none"; // hide contextmenu

	// get correct element
	element = dhtml.getElementById(element.parentNode.elementid);
	eventReadmailClickEmail(moduleObject, element, event);
}

function eventReadmailContextAddContact(moduleObject, element, event)
{
	element.parentNode.style.display="none"; // hide contextmenu

	// get correct element
	element = dhtml.getElementById(element.parentNode.elementid);

	webclient.openWindow(moduleObject, "contact", DIALOG_URL+"task=contact_standard&address="+encodeURI(dhtml.getTextNode(element, "")));
}

function eventReadmailClickEmail(moduleObject, element, event)
{
	var email = dhtml.getTextNode(element,"");
	var validEmail = parseEmailAddress(email);
	// If it is not valid email address then it must be a group, so added brackets.
	if(!validEmail)
		email = "[" + email + "]";
	webclient.openWindow(this, "createmail", DIALOG_URL+"task=createmail_standard&to=" + email);
}

function eventReadmailSendMailFromPlainText(emailaddress)
{
	webclient.openWindow(this, "createmail", DIALOG_URL+"task=createmail_standard&to=" + emailaddress);
}

/**
 * Called when a touchstart event was triggered. Used to set the base coordinates for scrolling through the body.
 * @param moduleObject Object Module Object
 * @param element HTMLElement touched element
 * @param event Object Event object
 */
function eventReadmailScrollBodyTouchStart(moduleObj, element, event){
	moduleObj.touch.startY = event.targetTouches[0].pageY;
	moduleObj.touch.startX = event.targetTouches[0].pageX;
}

/**
 * Called when a touchnmove event was triggered. Used to scrol through the body.
 * @param moduleObject Object Module Object
 * @param element HTMLElement touched element
 * @param event Object Event object
 */
function eventReadmailScrollBodyTouchMove(moduleObj, element, event){
		event.preventDefault();
		var posy = event.targetTouches[0].pageY;
		var scroller = moduleObj.touch.scroller;
		var sty = scroller.scrollTop;

		var posx = event.targetTouches[0].pageX;
		var stx = scroller.scrollLeft;
		// Scroll the scroller DIV to the new position
		scroller.scrollTop = sty - (posy - moduleObj.touch.startY);
		scroller.scrollLeft = stx - (posx - moduleObj.touch.startX);
		// Store the new touch coordinates to use that as base for the next move
		moduleObj.touch.startY = posy;
		moduleObj.touch.startX = posx;
}
