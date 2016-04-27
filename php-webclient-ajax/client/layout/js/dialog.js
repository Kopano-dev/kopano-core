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

function initEditor(useHTMLEditor, FCKEDITOR_JS_PATH, client_lang, spellchecker, elementId, editorHeight){
	if (useHTMLEditor){
		var height = (parseInt(editorHeight)) ?editorHeight:366;
		var editorElement = new String("html_body");
		
		if(parentWebclient.settings.get("createmail/from", "false") != "false"){
			height -= 24;
		}
		
		//Set id of edior element.
		if (elementId && elementId.length > 0) {
			editorElement = elementId;
		}
		
		var oFCKeditor = new FCKeditor(editorElement, "100%", height+"px");
		oFCKeditor.EditorPath = FCKEDITOR_JS_PATH + "/";
		oFCKeditor.BasePath = oFCKeditor.EditorPath;
		oFCKeditor.ReplaceTextarea();
		document.fcklanguage = client_lang;
		document.fckspellcheck = spellchecker;
	}
}

/**
 * Returns whether the HTML Editor is used for the body. It does so by detecting whether the 
 * FCKeditorAPI is available and it can get the instance for the html_body editor.
 * @return Boolean Returns whether the HTML Editor is used or not
 */
function usingHTMLEditor(){
	if(typeof(FCKeditorAPI) != "undefined" && (fckEditor = FCKeditorAPI.GetInstance("html_body"))) {
		return true;
	}else{
		return false;
	}
}

function resizeBody()
{
	var use_html = dhtml.getElementById("use_html");
	var html_body = dhtml.getElementById("html_body");
	if(use_html && use_html.value == "true") {
		html_body = dhtml.getElementById("html_body___Frame");
	}
	
	if(html_body) {
		var height = document.documentElement.clientHeight - dhtml.getElementTop(html_body);
		
		var categoriesbar = dhtml.getElementById("categoriesbar");
		if(categoriesbar) {
			height -= categoriesbar.offsetHeight;
		}
		
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

	//Resize module
	if (module && typeof(module.resize) != "undefined") {
		module.resize();
	}
}

function getPropsFromDialog() {
	var props = new Object();
	
	// All input fields with an id
	var input = document.getElementsByTagName("input");
	for(var i = 0; i < input.length; i++) {
		if(input[i].id) {
			switch (input[i].type){
				case "checkbox":
				case "radio":
					props[input[i].id] = input[i].checked;
					break;
				case "button":
					// if button with id then don't send it as props
					break;
				case "hidden":
				case "text":
				default:
					props[input[i].id] = input[i].value;
			}
		}
	}
	
	// All textarea's with an id (except html_body)
	var textarea = document.getElementsByTagName("textarea");
	for(var i = 0; i < textarea.length; i++) {
		if(textarea[i].id && textarea[i]!="html_body") {
			props[textarea[i].id] = textarea[i].value;
		}
	}

	// select boxes
	var select = document.getElementsByTagName("select");
	for(var i = 0; i < select.length; i++) {
		if(select[i].id) {
			props[select[i].id] = select[i].value;
		}
	}

	// Body
	if(typeof(FCKeditorAPI) != "undefined" && (fckEditor = FCKeditorAPI.GetInstance("html_body"))) {
		props["body"] = cleanHTML(fckEditor.GetXHTML());
	}else{
		var body = dhtml.getElementById("html_body");
		if (body) {
			props["body"] = body.value;
		}
	}
	
	return props;
}

function getRecipients(keepDuplicate) {
	keepDuplicate = keepDuplicate || false;
	var recipients = new Array();
	var types = new Array("to", "cc", "bcc");

	for(var i = 0; i < types.length; i++) {
		var element = dhtml.getElementById(types[i]);
	
		if(element && element.value != "") {
			var recipientRows = element.value.split(";");

			for(var j = 0; j <  recipientRows.length; j++)
			{
				if(recipientRows[j].trim() != "") {
					var recipient = new Object();

					var regex = new RegExp(/([^<]*){0,1}\s*(<\s*([^>]*)>\s*){0,1}/);
					var result = regex.exec(recipientRows[j].trim());

					if(typeof result[1] != "undefined" && result[1] != ""){	
						result[1] = result[1];
					} else {
						// username
						result[1] = result[3];
					}
					
					if(typeof result[3] != "undefined" && result[3] != ""){
						result[3] = result[3];
					} else {
						// username@domain.com
						result[3] = result[1];
					}
					
					if(typeof result[3] == "undefined" || result[3] == ""){
						recipient["name"] = result[1].trim();
						recipient["address"] = result[1].trim();
					} else {
						recipient["name"] = result[1].trim();
						recipient["address"] = result[3].trim();
					}
									
					recipient["type"] = "mapi_" + types[i];
					recipients.push(recipient);
				}
			}
		}
	}

	if (!keepDuplicate) {
		/**
		 * create a temp object on basis of email addresses
		 * which basically removes the duplicate recipients from recipients array
		 */
		var tempRecipientObject = new Object();
		for(var i=recipients.length-1;i>=0;i--){
			tempRecipientObject[recipients[i]["address"]] = recipients[i];
		}
		//empty the recipient list
		recipients = new Array();

		// create the array again from unique recipient Object.
		for(var address in tempRecipientObject){
			recipients.push(tempRecipientObject[address]);
		}
	}
	return recipients;
}

function submit_createmail(send) {

	if (!window.waitForSaveResponse){
		var inlineimages = new Object();
		if (typeof FCKeditorAPI != "undefined" && (fckeditor = FCKeditorAPI.GetInstance('html_body'))){
			var fckEditorDocument = fckeditor.EditorDocument;
			module.setCIDInlineImages(fckEditorDocument);
			inlineimages["images"] = module.checkUsedInlineAttachments(fckEditorDocument);
		}
		
		if(send && module){
			if(validateEmailAddress(dhtml.getElementById("to").value, false, window.resolveForSendingMessage?false:true) && 
			 validateEmailAddress(dhtml.getElementById("cc").value, false, window.resolveForSendingMessage?false:true) && 
			 validateEmailAddress(dhtml.getElementById("bcc").value, false, window.resolveForSendingMessage?false:true)){
	 			window.waitForSaveResponse = true;
				var props = getPropsFromDialog();
				var recipients = new Object();
				recipients["recipient"] = getRecipients();
				if (recipients["recipient"].length==0){
					alert(_("Please input a valid email address!"));
					window.waitForSaveResponse = false;
					return;
				}
				
				module.save(props, send, recipients, dhtml.getElementById("dialog_attachments").value, inlineimages);
	
				// close opener window
				if (webclient.settings.get("createmail/close_read_on_reply", "no")=="yes" && window.opener && window.opener.name.indexOf("readmail")==0){
					window.opener.close();
				}
				
				window.messageChanged = false;
				// window will be closed after we have received response from server
			} else if (window.resolveForSendingMessage !== true) {
				// Set status for the resolver to know he has to call this function again
				window.resolveForSendingMessage = true;
	  			checkNames(checkNamesCallBackCreateMail);
	 		}else{
	  			// Reset status for the resolver to know he does not have to call this function again
	  			window.resolveForSendingMessage = false;
			}
		} else{
			window.waitForSaveResponse = true;
			var props = getPropsFromDialog();
			var recipients = new Object();
			var recipientList = getRecipients();
			if(recipientList.length > 0){
				recipients["recipient"] = recipientList;
			}
			
			module.save(props, send, recipients, dhtml.getElementById("dialog_attachments").value, inlineimages);
		}
	}
}

function submit_stickynote() {
	if(module) {
		var props = getPropsFromDialog();
				
		module.save(props);
		window.close();
	}
}
/**
 * Function will send a save request to server with all contact's data from client
 * @param object timezone data is passed if any specialDate events are set in contact Details.  
 */
function submit_contact(timezone)
{
	if(module) {
		var props = getPropsFromDialog();
		props["timezone"] = timezone;
		
		module.save(props, dhtml.getElementById("dialog_attachments").value);
		window.close();
	}
}

function submit_task(send) {
	if(module) {

		/* Task has been assigned to someone, check if correct email-address is given
		 * if not then do auto resolvenames 
		 */
		if (send == "request") {
			if (validateEmailAddress(dhtml.getElementById("to").value, false, !window.resolveForSendingMessage)) {
			} else if (window.resolveForSendingMessage != true) {
				// Set status for the resolver to know he has to call this function again
				window.resolveForSendingMessage = true;
				checkNames(checkNamesCallBackTask);
				return false;
			} else {
				// Reset status for the resolver to know he does not have to call this function again
				window.resolveForSendingMessage = false;
				return false;
			}
		}

		var props = getPropsFromDialog();

		// check for percent complete value
		var text_percent_complete = props["text_percent_complete"];

		if(text_percent_complete.indexOf("%") >= 0) {
			text_percent_complete = text_percent_complete.substring(0, text_percent_complete.indexOf("%"));
		}

		text_percent_complete = parseInt(text_percent_complete);

		if(isNaN(text_percent_complete) || text_percent_complete > 100 || text_percent_complete < 0) {
			alert(_("Percent complete must be a number between 0 and 100"));
			return false;
		}

		if(props["select_status"] == "2" && props["reminder"] == "1"){
			alert(_("Since this task is complete, its reminder has been turned off."));
			props["reminder"] = "0";
		}

		var recipients = new Object();
		var recipientList = getRecipients(true);
		if(recipientList.length > 0){
			recipients["recipient"] = recipientList;

			var filter = new RegExp(/^\[([^[]+)\]$/);	// To search for a group
			var multipleRecips = 0;
			for(var i in recipientList) {
				if (recipientList[i]["type"] == "mapi_to") {
					multipleRecips++;

					// User cannot send a task request himself/herself.
					if (recipientList[i]["address"] == webclient.emailaddress) {
						alert(_("You cannot send a task request to yourself."));
						return false;
					}

					// Prompt that task updates will not be mentained if more than one assignee
					if ((recipientList[i]["address"] && filter.test(recipientList[i]["address"].trim())) || multipleRecips > 1) {
						if (!confirm(_("This task is addressed to more than one recipient, therefore your copy of the task will not be updated.")))
							return false;
						props['taskmultrecips'] = true;
					}
				}
			}
		} else if (send == "request"){
			alert(_("You must enter at least one recipient in To field."));
			return;
		}

		module.save(props, send, recipients, dhtml.getElementById("dialog_attachments").value);

		return true;
	}
}

function delete_item()
{
	module.deleteMessage();
	window.close();
}

function setImportance(value) 
{
	var importance = dhtml.getElementById("importance");

	if(importance) {
		var priority_high = dhtml.getElementById("priority_high");
		var priority_low = dhtml.getElementById("priority_low");

		switch(value)
		{
			case 0:
				dhtml.addClassName(priority_low, "menubuttonselected");
				dhtml.removeClassName(priority_high, "menubuttonselected");
				break;
			case 2:
				dhtml.addClassName(priority_high, "menubuttonselected");
				dhtml.removeClassName(priority_low, "menubuttonselected");
				break;
			default:
				dhtml.removeClassName(priority_low, "menubuttonselected");
				dhtml.removeClassName(priority_high, "menubuttonselected");
				break;
		}
		
		importance.value = value;
	}
}

//@todo fix this function
function changeItem(type)
{
	if(window.opener)
	{
		var message = window.opener.moduleObject.entryids;
		
		if(message) {
			var nextMessage = false;
			switch(type)
			{
				case "next":
					nextMessage = message.nextSibling;
					//this will not work when you change to an other view or page
					break;
				case "previous":
					nextMessage = message.previousSibling;
					//this will not work when you change to an other view or page
					break;
			}
			
			if(nextMessage) {
				var nextMessageLocation = new String(window.location);
				window.location = nextMessageLocation.substring(0, nextMessageLocation.lastIndexOf("=") + 1) + nextMessage.id;
				
				window.opener.dhtml.executeEvent(nextMessage, "mousedown");
				window.opener.dhtml.executeEvent(nextMessage, "mouseup");
			}
		}
	}
}

function changeCheckBoxStatus(id)
{
	var checkbox = dhtml.getElementById(id);
	
	if(checkbox) {
		if(checkbox.checked) {
			checkbox.checked = false;
		} else {
			checkbox.checked = true;
		}
	}
}

function attachmentSelect()
{
	var filelist = dhtml.getElementById("filelist");
	
	if(filelist) {
		var value = filelist.options[filelist.selectedIndex].value;
		
		var type = dhtml.getElementById("type");
		type.value = "attachment";
		if(String(value).search(/^[0-9]*$/) == -1){
			type.value = "new";
		}
		
		var deleteattachment = dhtml.getElementById("deleteattachment");
		deleteattachment.value = value;
	}
}

/**
 * eventMenuAddSignature
 * 
 * If the module contains signature items and if so, build the menu.
 */
function eventMenuAddSignature(){
	if(module.addSigMenuItems.length > 0){
		var posX = dhtml.getElementLeft(dhtml.getElementById("addsignature"));
		var posY = dhtml.getElementTop(dhtml.getElementById("addsignature"));
		posY += dhtml.getElementById("addsignature").offsetHeight;
		webclient.menu.buildContextMenu(module.id, "addsignature", module.addSigMenuItems, posX, posY);
	}else{
		alert(_("No signatures"));
	}
}

/**
 * addSignature
 * 
 * Add selected signature to the body of the message.
 * @param signatureID number ID of signature that is selected.
 */
function addSignature(signatureID)
{
	setMessageChanged();

	var signatureData = module.getSignatureData(signatureID);
	if(signatureData) {
		var use_html = dhtml.getElementById("use_html");
		if(use_html) {
			if(use_html.value == "false") {
				// Convert html signature to plain text
				if(signatureData["type"] != "plain") {
					signatureData["content"] = convertHtmlToPlain(signatureData["content"]);
				}
				
				var body = dhtml.getElementById("html_body");
				if (body.setSelectionRange){
					body.value = body.value.substring(0, body.selectionStart) + signatureData["content"] + body.value.substring(body.selectionStart, body.selectionEnd) + body.value.substring(body.selectionEnd, body.value.length);
				} else if (document.selection && document.selection.createRange) {
					body.value = body.value.substring(0, window.start) + signatureData["content"] + body.value.substring(window.start, window.end) + body.value.substring(window.end, body.value.length);
				}
			} else {
				if(typeof(FCKeditorAPI) != "undefined" && (fckEditor = FCKeditorAPI.GetInstance("html_body"))) {
					fckEditor.Selection.Restore();
					signatureData["content"] = signatureData["content"].replace(/\n/g, "<br>");
					fckEditor.InsertHtml(signatureData["content"]);
				}
			}
		}
	} else {
		alert(_("No signature"));
	}
}

function getMailOptions()
{
	var parentwindow = window.opener;
	if(!parentwindow) {
		if(window.dialogArguments) {
			parentwindow = window.dialogArguments.parentWindow;
		}
	}
	
	var importance = dhtml.getElementById("importance");
	if(importance) {
		for(var i = 0; i < importance.options.length; i++)
		{
			var option = importance.options[i];
			if(option.value == parentwindow.dhtml.getElementById("importance").value) {
				option.selected = true;
			}
		}
	}
	
	var sensitivity = dhtml.getElementById("sensitivity");
	if(sensitivity) {
		for(var i = 0; i < sensitivity.options.length; i++)
		{
			var option = sensitivity.options[i];
			if(option.value == parentwindow.dhtml.getElementById("sensitivity").value) {
				option.selected = true;
			}
		}
	}

	var read_receipt = dhtml.getElementById("read_receipt");
	if(read_receipt) {
		var checked = false;
		if(parentwindow.dhtml.getElementById("read_receipt_requested").value == "true") {
			checked = true;
		}
		
		read_receipt.checked = checked;
	}
}

/**
 * Function will start the resolveNames process
 * @param object callBackFunction callback function
 */ 
function checkNames(callBackFunction)
{
	var recipients = new Array("to", "cc", "bcc");
	var resolveObj = new Object();
	for(var i = 0; i < recipients.length; i++)
	{
		if(dhtml.getElementById(recipients[i]).value){
			resolveObj[recipients[i]] = dhtml.getElementById(recipients[i]).value;
		} else {
			resolveObj[recipients[i]] = "";
		}
	}
	parentWebclient.resolvenames.resolveNames(resolveObj, callBackFunction);
}

/**
 * Function is the callback function of resolveNames and will replace the resolved names
 * @param = "Object" of the resolved names
 * @param fireChangeEvent after updating field whether change event should fire or not.
 */ 
function checkNamesCallBack(resolveObj, fireChangeEvent)
{
	for(var i in resolveObj){
		if(dhtml.getElementById(i) && dhtml.getElementById(i).value){
			
			//replace unresolved name
			var unResolved = dhtml.getElementById(i).value.split(";");
			for(var keyword in resolveObj[i]){
				for(var j=0; j<unResolved.length; j++){
					unResolved[j] = unResolved[j].trim();
					if(unResolved[j] == keyword){
						if(resolveObj[i][keyword]["message_class"] == "IPM.DistList"){
							// if message class is IPM.DistList (that means a distribution list not group, then we have 
							// to show the expanded email addresses (of its users) which are in emailaddress already.
							unResolved[j] = resolveObj[i][keyword]["emailaddress"];
						}else{
							unResolved[j] = nameAndEmailToString(resolveObj[i][keyword]["fullname"], resolveObj[i][keyword]["emailaddress"], resolveObj[i][keyword]["objecttype"], false);
						}
					}
				}				
			}
			
			//build string
			var newString = "";
			for(var j=0; j<unResolved.length; j++){
				if (unResolved[j].trim().length > 0)
					newString += unResolved[j]+";";
			}
			dhtml.getElementById(i).value = newString;

			// also fire change event if change event is happen on to, cc, bcc fields (growing fields) of createmail
			if(fireChangeEvent) {
				dhtml.executeEvent(dhtml.getElementById(i), "change");
			}
		}
	}
}

function timeSpinnerUp(time_element)
{
	if(time_element) {
		if(time_element.value.indexOf(":")) {
			var hour = time_element.value.substring(0, time_element.value.indexOf(":"));
			var minutes = time_element.value.substring(time_element.value.indexOf(":") + 1);
			
			if(hour.substring(0, 1) == "0") {
				hour = hour.substring(1);
			}
			
			if(minutes.substring(0, 1) == "0") {
				minutes = minutes.substring(1);
			}
			
			hour = parseInt(hour);
			minutes = parseInt(minutes);
			
			if(hour >= 0 && minutes >= 0) {
				if(minutes >= 0 && minutes <= 29) {
					minutes = 30;
				} else if(minutes >= 30 && minutes <= 59) {
					hour++;
					minutes = 0;
				}
				
				if(hour > 23) {
					hour = 0;
				}
				
				time_element.value = (hour < 10?"0" + hour:hour) + ":" + (minutes < 10?"0" + minutes:minutes);
			} else {
				time_element.value = "09:00";
			}
		} else {
			time_element.value = "09:00";
		}
	}
	
	onChangeDate();
}

function timeSpinnerDown(time_element)
{
	if(time_element) {
		if(time_element.value.indexOf(":")) {
			var hour = time_element.value.substring(0, time_element.value.indexOf(":"));
			var minutes = time_element.value.substring(time_element.value.indexOf(":") + 1);
			
			if(hour.substring(0, 1) == "0") {
				hour = hour.substring(1);
			}
			
			if(minutes.substring(0, 1) == "0") {
				minutes = minutes.substring(1);
			}
			
			hour = parseInt(hour);
			minutes = parseInt(minutes);
			
			if(hour >= 0 && minutes >= 0) {
				if(minutes >= 0 && minutes <= 29) {
					minutes = 30;
					hour--;
				} else if(minutes >= 30 && minutes <= 59) {
					minutes = 0;
				}
				
				if(hour < 0) {
					hour = 23;
				}
				
				time_element.value = (hour < 10?"0" + hour:hour) + ":" + (minutes < 10?"0" + minutes:minutes);
			} else {
				time_element.value = "09:00";
			}
		} else {
			time_element.value = "09:00";
		}
	}
	
	onChangeDate();	
}

function onChangeDate(fieldName)
{
	var text_startdate = dhtml.getElementById("text_commonstart");
	var text_startdate_time = dhtml.getElementById("text_startdate_time");
	
	var text_duedate = dhtml.getElementById("text_commonend");
	var text_duedate_time = dhtml.getElementById("text_duedate_time");
	
	if(text_duedate.value.toLowerCase() == _("None").toLowerCase()) {
		text_duedate.value = text_startdate.value;
	}
	
	var startdate = Date.parseDate(text_startdate.value + " " + (text_startdate_time?text_startdate_time.value:"00:00"), _("%d-%m-%Y") + " " + _("%H:%M"));
	var duedate =   Date.parseDate(text_duedate.value + " " + (text_duedate_time?text_duedate_time.value:"00:00"), _("%d-%m-%Y") + " " + _("%H:%M"));

	switch(fieldName)
	{
		case 'text_commonstart':
			var old_startdate = text_startdate.getAttribute("oldvalue") ? Date.parseDate(text_startdate.getAttribute("oldvalue"), _("%d-%m-%Y")) : 0;
			var diffInSeconds = (old_startdate == 0) ? 0 : duedate - old_startdate;

			if (diffInSeconds > 0) {
				text_duedate.value = strftime(_("%d-%m-%Y"), (startdate.getTime() + diffInSeconds)/1000);
			} else {
				if(old_startdate != 0 || startdate > duedate)
					text_duedate.value = strftime(_("%d-%m-%Y"), (startdate.getTime())/1000);
			}
			break;
		case 'text_commonend':
			if ((text_startdate.value.toLowerCase() != _("None").toLowerCase() && text_duedate.value.toLowerCase() != _("None").toLowerCase())) {
				if ((duedate && startdate) && (startdate > duedate))
					text_duedate.value = text_duedate.getAttribute('oldvalue');
			}
			break;
		default:
			if(duedate && startdate) {
				if(startdate > duedate) {
					text_startdate.value = text_duedate.value;

					if(text_startdate_time && text_duedate_time) {
						text_startdate_time.value = text_duedate_time.value;
					}
				}
			}
	}

	if(text_startdate.value.toLowerCase() != _("None").toLowerCase()) text_startdate.setAttribute('oldvalue', text_startdate.value);
	if(text_duedate.value.toLowerCase() != _("None").toLowerCase()) {
		text_duedate.setAttribute('oldvalue', text_duedate.value);

		var reminder = dhtml.getElementById("text_reminderdate");
		if (reminder) reminder.value = text_duedate.value;
	}
}


function changeFromAddress(){
	var from = dhtml.getElementById("from").value;

	var name = "";
	var email = "";

	var regex = new RegExp(/([^<]*){0,1}\s*(<\s*([^>]*)>\s*){0,1}/);
	var result = regex.exec(from.trim());

	if(result[1] != undefined && result[3] != undefined){
		if(!result[1] || result[1] == "" || !result[3] || result[3] != ""){
			name = result[1];
			email = result[3];

			if(!name){
				name = email;
			}
			if(!email){
				email = name;
			}
		}
	}

	dhtml.getElementById("sent_representing_name").value = name;
	dhtml.getElementById("sent_representing_email_address").value = email;
	dhtml.getElementById("sent_representing_addrtype").value = "SMTP";
}

/**
 * ZarafaDnD Firefox extension functions
 */
function getDnDinfo(){
	if(DND_FILEUPLOAD_URL){
		var url = window.location.protocol + "//" + window.location.host + "" + window.location.pathname;
		url = url.replace("index.php", "");
		if(url.charAt(url.length-1) != "/"){
			url+="/";
		}

		var dndinfo = new Object();
		dndinfo["url"] = url + DND_FILEUPLOAD_URL;
		dndinfo["dialog_attachments"] = dhtml.getElementById('dialog_attachments').value;
		return dndinfo;
	}
	return false;
}
function setDnDAttachments(input){
	var newattachments = new Array();
	var files = input.split("||");
	for(var i=0;i<files.length;i++){
		var file = files[i].split("|");
		var attachment = new Object();
		attachment["attach_num"] = file[0]
		attachment["name"] = file[1]
		attachment["size"] = file[2];
		newattachments.push(attachment);
	}
	module.newattachments = newattachments;

	if(module.messageAction == "reply" || module.messageAction == "replyall") {
		// remove previous attachments when replying to a mail
		module.attachments = new Array();
	}

	module.setAttachments();
}
function allowDnDFiles(element){
	if(window.BROWSER_IE)
		return false;

	// Fire event to let the ZarafaDnD Firefox extension know that this dialog accepts dragged files
	dhtml.executeEvent((element||document.body), "ZarafaDnD");
}

/**
 * Function which will clean the string that is copied
 * directly from Word.
 * @param string html contains the raw paste from the clipboard
 * @return string cleaned string
 */
function cleanHTML(html)
{
	html = html.replace(/<o:p>\s*<\/o:p>/g, '') ;
	html = html.replace(/<o:p>(.*?)<\/o:p>/g, "<p>$1</p>") ;

	// Remove mso-xxx styles.
	html = html.replace( /\s*mso-[^:]+:[^;"'}]+;?/gi, '' ) ;

	html = html.replace( /\s*TEXT-INDENT: 0cm\s*;/gi, '' ) ;
	html = html.replace( /\s*TEXT-INDENT: 0cm\s*"/gi, "\"" ) ;

	html = html.replace( /\s*PAGE-BREAK-BEFORE: [^\s;]+;?"/gi, "\"" ) ;

	html = html.replace( /\s*FONT-VARIANT: [^\s;]+;?"/gi, "\"" ) ;

	html = html.replace( /\s*tab-stops:[^;"]*;?/gi, '' ) ;
	html = html.replace( /\s*tab-stops:[^"]*/gi, '' ) ;

	// Remove Class attributes
	html = html.replace(/<(\w[^>]*) class=([^ |>]*)([^>]*)/gi, "<$1$3") ;

	// Remove empty styles.
	html =  html.replace( /\s*style="\s*"/gi, '' ) ;

	html = html.replace( /<SPAN\s*[^>]*>\s*&nbsp;\s*<\/SPAN>/gi, '&nbsp;' ) ;

	html = html.replace( /<SPAN\s*[^>]*><\/SPAN>/gi, '' ) ;

	// Remove Lang attributes
	html = html.replace(/<(\w[^>]*) lang=([^ |>]*)([^>]*)/gi, "<$1$3") ;

	html = html.replace( /<SPAN\s*>(.*?)<\/SPAN>/gi, '$1' ) ;

	html = html.replace( /<FONT\s*>(.*?)<\/FONT>/gi, '$1' ) ;

	// Remove XML elements and declarations
	html = html.replace(/<\\?\?xml[^>]*>/gi, '' ) ;

	// Remove Tags with XML namespace declarations: <o:p><\/o:p>
	html = html.replace(/<\/?\w+:[^>]*>/gi, '' ) ;

	// Remove comments [SF BUG-1481861].
	html = html.replace(/<\!--.*?-->/g, '' ) ;

	html = html.replace( /<(U|I|STRIKE)>&nbsp;<\/\1>/g, '&nbsp;' ) ;

	html = html.replace( /<H\d>\s*<\/H\d>/gi, '' ) ;

	// Remove "display:none" tags.
	html = html.replace( /<(\w+)[^>]*\sstyle="[^"]*DISPLAY\s?:\s?none(.*?)<\/\1>/ig, '' ) ;

	// Remove language tags
	html = html.replace( /<(\w[^>]*) language=([^ |>]*)([^>]*)/gi, "<$1$3") ;

	// Remove onmouseover and onmouseout events (from MS Word comments effect)
	html = html.replace( /<(\w[^>]*) onmouseover="([^\"]*)"([^>]*)/gi, "<$1$3") ;
	html = html.replace( /<(\w[^>]*) onmouseout="([^\"]*)"([^>]*)/gi, "<$1$3") ;
	html = html.replace( /<H1([^>]*)>/gi, '<div$1><b><font size="6">' ) ;
		
	html = html.replace( /<H2([^>]*)>/gi, '<div$1><b><font size="5">' ) ;
	html = html.replace( /<H3([^>]*)>/gi, '<div$1><b><font size="4">' ) ;
	html = html.replace( /<H4([^>]*)>/gi, '<div$1><b><font size="3">' ) ;
	html = html.replace( /<H5([^>]*)>/gi, '<div$1><b><font size="2">' ) ;
	html = html.replace( /<H6([^>]*)>/gi, '<div$1><b><font size="1">' ) ;

	html = html.replace( /<\/H\d>/gi, '<\/font><\/b><\/div>' ) ;

	// Remove empty tags (three times, just to be sure).
	// This also removes any empty anchor
	html = html.replace( /<([^\s>]+)(\s[^>]*)?>\s*<\/\1>/g, '' ) ;
	html = html.replace( /<([^\s>]+)(\s[^>]*)?>\s*<\/\1>/g, '' ) ;
	html = html.replace( /<([^\s>]+)(\s[^>]*)?>\s*<\/\1>/g, '' ) ;
	
	return html ;
}
/**
 * Function which retrieve current cursor position
 * within textarea.
 * @param element textArea textarea which contains message body.
 */
function IE_getCursorPosition(textArea)
{
    if (document.selection) { // IE
        var selectedRange = document.selection.createRange();
        var tmpSelectedRange = selectedRange.duplicate();
		tmpSelectedRange.moveToElementText(textArea);
		// Now set 'tmpSelectedRange' end point to end point of original range that is selectedRange 
		tmpSelectedRange.setEndPoint('EndToEnd', selectedRange );
		// Now we can calculate start and end points
		window.start = tmpSelectedRange.text.length - selectedRange.text.length;
		window.end = window.start + selectedRange.text.length;
    } else if (textArea.selectionStart || (textArea.selectionStart == "0")) { // Mozilla/Netscape
        window.start = textArea.selectionStart;
        window.end = textArea.selectionEnd;
    }
}
/**
 * Function which call IE_getCursorPosition() to
 * keep trac of cursor position.
 */
function IE_tracCursorPosition(moduleObject, element)
{
	var use_html = dhtml.getElementById("use_html");
	if (use_html){
		if(use_html.value == "false") {
			IE_getCursorPosition(element);
		}
	}
}
/**
 * Function which keeps trac of cursor position
 * when editor is loaded.
 */
function IE_tracCursorInEditor()
{
	var fckEditor = FCKeditorAPI.GetInstance("html_body");
	if (fckEditor && fckEditor.HasFocus){
		if (fckEditor.Selection.GetSelection().type != "None") fckEditor.Selection.Save();
	}
}

/**
 * Function which insert the data at the selected cursor position
 * in the Textarea
 */
function insertAtCursor(textArea, value) {
	var newCursorPos=0;
	//IE support
	if (document.selection) {
		textArea.focus();
		//in effect we are creating a text range with zero length at the cursor location and replacing it
		//with value
		var cursorPosition = document.selection.createRange();
		cursorPosition.move('character', window.start);
		cursorPosition.text = value;

	}else if (textArea.selectionStart || textArea.selectionStart == '0') {//Mozilla/Firefox support
		
		//start of the selection and from the end point of the selection to the end of the field value.
		//Then we concatenate the first substring, value,and the second substring to get the new value.
		var startPos = textArea.selectionStart;
		var endPos = textArea.selectionEnd;
		textArea.value = textArea.value.substring(0, startPos) + value + textArea.value.substring(endPos, textArea.value.length);

		newCursorPos = textArea.value.substring(0, startPos).length + value.length;
		textArea.setSelectionRange(newCursorPos , newCursorPos );

	} else {
		textArea.value += value;
	}
	
}

/**
 * Function which validates the input text is a valid date or not
 * This function is pretty much same as that in datepicker widget.
 * its duplicated here coz we do not use datepicker widgets every where 
 * while using the Calendar(Date) object.
 */
function eventDateInputChange(moduleObject, element, event)
{
	if(element.value && element.value.toLowerCase() != _("None").toLowerCase()) {
		var newValue = Date.parseDate(element.value.trim(),_("%d-%m-%Y"), true);
		if(!newValue){
			alert(_("You must specify a valid date and/or time. Check your entries in this dialog box to make sure they represent a valid date and/or time."));
			element.value = _("None");
			if(element.onchange)
				element.onchange(element);
		}else{
			element.value = newValue.print(_("%d-%m-%Y")); 
			if(element.onchange)
				element.onchange(element);
		}
	}
}

/**
 * Change the title of the dialog
 * @param name String New name to be shown as title
 */
function setDialogTitle(name){
	// Set title of browser window
	window.document.title = name;
	// Set the inline dialog title
	dhtml.getElementById("windowtitle").innerHTML = name;
}