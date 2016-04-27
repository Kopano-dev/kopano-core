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

function proposeNewTime(moduleObject)
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
	 * this readmail dialog is invoked it will send the proposal imediately. It wants to close the 
	 * readmail dialog, but then finds out (in IE this is an issue) that the modal dialog is still 
	 * open. It cannot close the window. By making the advprompt a normal window this issue is 
	 * avoided.
	 */
	webclient.openWindow(-1, 'previewreadmailitem_proposenewtimedialog', DIALOG_URL+'task=advprompt_modal', 325,250, true, proposeNewTime_readmaildialog_callback, {
			moduleObject: module ? module : {}
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
function proposeNewTime_readmaildialog_callback(result){
	if(module){
		module.proposalMeetingRequest(parseInt(result.combineddatetimepicker.start,10), parseInt(result.combineddatetimepicker.end,10), result.body);
		window.close();
	}
}

function sendMRMailConfirmationCallback(noResponse, requestStatus)
{
	var basedate = false;
	var body = false;
	
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
	window.setTimeout("window.close()",1200);
}

/**
 * Function for responding to message.
 */
function respondToMail(respond_type){
	var extraParams = "&storeid="+ module.storeid +"&parententryid="+ module.parententryid +"&entryid="+ module.messageentryid;
	if (module.attachNum){
		extraParams += "&rootentryid="+ module.rootentryid;
		
		for(var j = 0; j < module.attachNum.length; j++)
		{
			extraParams += "&attachNum[]=" + module.attachNum[j];
		}
	}
	/**
	 * When the user wants to reply/forward on a message we have to let the main window handle the 
	 * opening of the dialog. If the user has the "Close dialog on reply"-setting enabled then this 
	 * dialog will be closed when the createmail dialog is opened. If that happens the main window 
	 * will not have registered the dialog as being opened. When the createmail-dialog sends the 
	 * mail it will wait for a succes message. Since the main window is unaware of that dialog, it 
	 * is unable to let the dialog know the message was sent succesfully (or if there was a problem).
	 * Therefor we have to register the dialog at the main window. We cannot let the main window 
	 * create the dialog for us as that triggers popup blockers to block our dialog. So we have to 
	 * create the dialog here and let the main window know of its existance.
	 */
	var windowObj = webclient.openWindow(module, 'createmail', DIALOG_URL+'task=createmail_standard&message_action='+ respond_type + extraParams);
	parentWebclient.registerDialog(module, 'createmail', windowObj);
}

/**
 * callback function for categories, categories can be changed through shortcuts.
 */
function categoryByKeyControlCallBack(categories)
{
	if (categories){
		var props = getPropsFromDialog();
		props["categories"] = categories;
		// Remember categories as we do not want to have separate xml response for categores.
		module.itemProps.categories = categories;
		module.save(props);
	}
}

/**
 * KeyControl function which responds to message on specific keys.
 */
function eventReadMailItemKeyCtrlRespond(moduleObject, element, event)
{
	// Get respond type ie reply/replyall/forward
	var respond_type = array_search(event.keyCombination, this.keys["respond_mail"]);
	if (respond_type) respondToMail(respond_type);
}

/**
 * KeyControl function which responds to meeting request on specific keys.
 */
function eventReadMailItemKeyCtrlRespondMR(moduleObject, element, event)
{
	switch(event.keyCombination)
	{
		case this.keys["respond_meeting"]["accept"]:
			moduleObject.acceptMeetingRequest();
			break;
		case this.keys["respond_meeting"]["tentative"]:
			moduleObject.tentativeMeetingRequest();
			break;
		case this.keys["respond_meeting"]["decline"]:
			moduleObject.declineMeetingRequest();
			break;
	}
	window.close();
}

/**
 * KeyControl function which edit items like categorize, mark read/unread, flag etc.
 */
function eventReadMailItemKeyCtrlEdit(moduleObject, element, event)
{
	switch(event.keyCombination)
	{
		case this.keys["edit_item"]["toggle_read"]:
			// Since opened message is always marked as read, we will now mark it as unread
			moduleObject.setReadFlag(moduleObject.itemProps.entryid, "unread,noreceipt");
			break;
		case this.keys["edit_item"]["categorize"]:
			var windowData = new Object();
			
			// Get message categories for sending it to categories dialog.
			windowData['categories'] = moduleObject.itemProps.categories ? moduleObject.itemProps.categories : '';
			webclient.openModalDialog(moduleObject, 'categories', DIALOG_URL +'task=categories_modal', 350, 370, categoryByKeyControlCallBack, false, windowData);
			break;
		case this.keys["edit_item"]["print"]:
			var extraParams = '';
			
			if (moduleObject.attachNum){
				for(var j = 0; j < module.attachNum.length; j++)
					extraParams += "&attachNum[]=" + module.attachNum[j];
			}
			webclient.openModalDialog(moduleObject, 'printing', DIALOG_URL +'entryid='+ moduleObject.messageentryid +'&storeid='+ moduleObject.storeid +'&task=printitem_modal'+ extraParams, 600, 600);
			break;
		case this.keys["edit_item"]["toggle_flag"]:
			webclient.openModalDialog(moduleObject, 'flag', DIALOG_URL +'task=flag_modal', 350, 210);
			break;
	}
}

/**
 * Function to open a print item dialog
 */
function openPrintItemDialog(){
	var extraParams = "&storeid=" + module.storeid + "&entryid=" + module.messageentryid;
	if (module.attachNum){
		for(var j = 0; j < module.attachNum.length; j++)
		{
			extraParams += "&attachNum[]=" + module.attachNum[j];
		}
	}

	webclient.openWindow(module, 'printing', DIALOG_URL+'task=printitem_modal&message_action='+ extraParams);
}

/**
 * Function which opens corresponding meeting from Calendar.
 * @param boolean viewAllProposals true if we want to view all proposals else false
 */
function openMeeting(viewAllProposals){
	if (module) module.openMeeting(viewAllProposals);
}