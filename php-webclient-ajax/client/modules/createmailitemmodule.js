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

createmailitemmodule.prototype = new ItemModule;
createmailitemmodule.prototype.constructor = createmailitemmodule;
createmailitemmodule.superclass = ItemModule.prototype;

function createmailitemmodule(id)
{
	if(arguments.length > 0) {
		this.init(id);
	}
}

createmailitemmodule.prototype.init = function(id)
{
	createmailitemmodule.superclass.init.call(this, id);
	
	/**
	 * Build "Add Signature" menu
	 */
	this.addSigMenuItems = new Array();
	// Get signature IDs 
	var id_lookup = String(parentWebclient.settings.get("createmail/signatures/id_lookup",""));
	if(id_lookup != ""){
		var signatureIDs = id_lookup.split(";");
		// Get the signature data and add to menu
		for(var i=0;i<signatureIDs.length;i++){
			// Prepare data object to hold the signature ID that we can use when we click on the menu item.
			var data = new Object();
			data["sig_id"] = signatureIDs[i];
			// Get signature from settings.
			var sig_name = parentWebclient.settings.get("createmail/signatures/"+signatureIDs[i]+"/name","");
			// Add to menu
			this.addSigMenuItems.push(webclient.menu.createMenuItem("addsignature", sig_name, false, eventAddSignatureMenuItem, null, null, data));
		}
	}

	this.message_action = false;
	this.message_action_entryid = false;
	
	this.keys["mail"] = KEYS["mail"];

	// Add keycontrol event
	webclient.inputmanager.addObject(this);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["mail"], "keyup", eventCreateMailItemKeyCtrlSubmit);
}


createmailitemmodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "item":
			webclient.menu.showMenu();
			this.item(action);
			break;	
		case "update":		// mail saved successfully
			this.updateMessage(action);

			window.document.title = _("Create E-Mail");
			window.messageChanged = false;
			break;
		case "error":
			var errorString = dhtml.getXMLValue(action, "message", "");
			if(errorString.length > 0){
				alert(errorString);
			}

			// we got a response from the server so we are ready for another request
			window.waitForSaveResponse = false;
			break;
		case "success":		// mail sent successfully
			window.messageChanged = false;
			window.close();
			break;

		// will built attachment items while composing mail
		case "attach_items":		
			webclient.menu.showMenu();
			this.setAttachItemData(action);
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
Function which updates entryid element, before sending the mail, so that draft message is removed when submitting message.
@param action -info that is to be updated.
*/
createmailitemmodule.prototype.updateMessage = function(action)
{
	var item = action.getElementsByTagName("item")[0];
	var entryid = dhtml.getXMLValue(item, "entryid", false);
	var parententryid = dhtml.getXMLValue(item, "parententryid", false);
	var storeid = dhtml.getXMLValue(item, "storeid", false);

	var property = new Object();
	property["entryid"] = entryid;
	property["parententryid"] = parententryid;
	//set the store of the message to be sent to server, for a case in which any delagate store is open
	property["storeid"] = storeid ? storeid : this.storeid;
	
	// Update message entryID.
	this.messageentryid = property["entryid"];
	this.setData(property);
	
	var entryidElement = dhtml.getElementById("entryid");
	var parententryidElement = dhtml.getElementById("parent_entryid");
	
	if (entryidElement && parententryidElement){
		entryidElement.value = property["entryid"];
		parententryidElement.value = property["parententryid"];
	}

	var attachments = item.getElementsByTagName("attachment");
	if(attachments && attachments.length > 0) {
		this.attachments = new Array();
		this.newattachments = new Array();
		this.deletedattachments = new Object();
		for(var i = 0; i < attachments.length; i++)
		{
			var attach_num = attachments[i].getElementsByTagName("attach_num")[0];
			var attach_method = attachments[i].getElementsByTagName("attach_method")[0];
			var name = attachments[i].getElementsByTagName("name")[0];
			var size = attachments[i].getElementsByTagName("size")[0];
			var filetype = attachments[i].getElementsByTagName("filetype")[0];
			var cid = attachments[i].getElementsByTagName("cid")[0];
			var hidden = attachments[i].getElementsByTagName("hidden")[0];
			
			if(attach_num && attach_num.firstChild) {
				var attachment = new Object();
				attachment["attach_num"] = attach_num.firstChild.nodeValue;
				
				if(attach_method && attach_method.firstChild) {
					attachment["attach_method"] = attach_method.firstChild.nodeValue;
				}
				
				if(name && name.firstChild) {
					attachment["name"] = name.firstChild.nodeValue;
				}
				
				if(size && size.firstChild) {
					attachment["size"] = size.firstChild.nodeValue;
				}
				attachment["filetype"] = (filetype && filetype.firstChild?filetype.firstChild.nodeValue:false);
				attachment["cid"] = (cid && cid.firstChild?cid.firstChild.nodeValue:false);
				attachment["hidden"] = (dhtml.getTextNode(hidden, false) == "1")?true:false;
				this.attachments.push(attachment);
			}
		}
		/**
		 * Since message is saved, attachments are no longer avaliable from state files.
		 * So, initialize inlineattachments and inlineimages again.
		 */
		this.inlineattachments = new Object();
		this.inlineimages = new Array();
		this.setAttachments();
		this.setAttachmentbarAttachmentOptions();
	}
	window.waitForSaveResponse = false;
}

createmailitemmodule.prototype.item = function(action)
{

	var message = action.getElementsByTagName("item")[0];
	
	if(message && message.childNodes) {
		var attachments = message.getElementsByTagName("attachment");
	
		if(attachments && attachments.length > 0) {
			for(var i = 0; i < attachments.length; i++)
			{
				var attach_num = attachments[i].getElementsByTagName("attach_num")[0];
				var attach_method = attachments[i].getElementsByTagName("attach_method")[0];
				var name = attachments[i].getElementsByTagName("name")[0];
				var size = attachments[i].getElementsByTagName("size")[0];
				var filetype = attachments[i].getElementsByTagName("filetype")[0];
				var cid = attachments[i].getElementsByTagName("cid")[0];
				var hidden = attachments[i].getElementsByTagName("hidden")[0];
				
				if(attach_num && attach_num.firstChild) {
					var attachment = new Object();
					attachment["attach_num"] = attach_num.firstChild.nodeValue;
					
					if(attach_method && attach_method.firstChild) {
						attachment["attach_method"] = attach_method.firstChild.nodeValue;
					}
					
					if(name && name.firstChild) {
						attachment["name"] = name.firstChild.nodeValue;
					}
					
					if(size && size.firstChild) {
						attachment["size"] = size.firstChild.nodeValue;
					}
					attachment["filetype"] = (filetype && filetype.firstChild?filetype.firstChild.nodeValue:false);
					attachment["cid"] = (cid && cid.firstChild?cid.firstChild.nodeValue:false);
					attachment["hidden"] = (dhtml.getTextNode(hidden, false) == "1")?true:false;
					this.attachments.push(attachment);
				}
			}
		}
		switch(this.message_action)
		{
			case "reply":
				this.setSender(message);
				this.setSubjectBody(message, _("RE"));
				this.setInlineAttachmentData(this.attachments); //message contains inline attachments
				/**
				 * NOTE: We do not want to send attachments of original mail
				 * when we are replying to that mail. But new attachments 
				 * can be sent with that mail.
				 */
				this.attachments = new Array();
				break;
			case "replyall":
				this.setSender(message);
				this.setRecipients(message, "replyall");
				this.setSubjectBody(message, _("RE"));
				this.setInlineAttachmentData(this.attachments); //message contains inline attachments
				this.attachments = new Array();
				break;
			case "forward":
				this.setSubjectBody(message, _("FW"));
				this.setAttachments(message);
				break;
			case "edit":
				this.setRecipients(message);
				this.setSubjectBody(message, false);
				this.setAttachments(message);
				this.setBody(message, false, true);
				break;
			default:
				this.setRecipients(message);
				this.setBody(message, false, true);
				
				for(var i = 0; i < message.childNodes.length; i++)
				{
					var property = message.childNodes[i];
					
					if(property && property.firstChild)
					{
						var element = dhtml.getElementById(property.tagName);
						
						if(element) {
							element.value = property.firstChild.nodeValue;
						}
					}
				}
				
				this.setAttachments(message);
				break;
		}

		this.setFrom(message);
		this.setDelegatorAsSender(message);
	}

	setImportance(parseInt(dhtml.getElementById("importance").value));
}

createmailitemmodule.prototype.setDelegatorAsSender = function(message)
{
	var delegatorStoreId = message.getElementsByTagName("store_entryid")[0].firstChild.nodeValue;
	var delegateFlag = false;
	var stores = parentWebclient.hierarchy.stores;
	for(var storeIndex = 0;storeIndex<stores.length;storeIndex++){
		if(stores[storeIndex].type == "other"){
			var storeid = stores[storeIndex].id;

			// Opened folder of another user will contain id and foldertype followed by '_', so remove foldertype
			if(storeid.indexOf("_") > 0)
				storeid = storeid.substr(0, storeid.indexOf("_"));

			if(delegatorStoreId == storeid){
				delegateFlag = true;
				break;
			}
		}
	}
	if(delegateFlag){
		var delegatorName = "";
		var delegatorEmail = "";
		if(dhtml.getXMLNode(message, "received_by_name"))
			delegatorName = dhtml.getXMLValue(message, "received_by_name");

		if(dhtml.getXMLNode(message, "received_by_email_address"))
			delegatorEmail = dhtml.getXMLValue(message, "received_by_email_address");
		
		if(delegatorName && delegatorEmail) {
			var delegateEmailAddress = delegatorName + " <" + delegatorEmail + ">";
			var fromSelect = dhtml.getElementById("from", "select");
			if(fromSelect){
				var fromRow = dhtml.getElementById("from_row", "tr");
				if(fromRow){
					fromRow.style.display = "";
				}
				fromSelect.options[fromSelect.options.length] = new Option(delegateEmailAddress, delegateEmailAddress);
				fromSelect.options[fromSelect.options.length-1].selected = true;
			}
		}
	}
}

createmailitemmodule.prototype.checkNames = function(action)
{
	var names = action.getElementsByTagName("name");
	
	if(names && names.length > 0) {
		var to = dhtml.getElementById("to").value.split(";");
		var cc = dhtml.getElementById("cc").value.split(";");
		var bcc = dhtml.getElementById("bcc").value.split(";");

		this.setName(names, "to", to);
		this.setName(names, "cc", cc);
		this.setName(names, "bcc", bcc);
	}
}

createmailitemmodule.prototype.setName = function(names, type, recipients)
{
	var recipienttype = dhtml.getElementById(type);
	
	if(recipienttype) {
		recipienttype.value = "";
		
    	for(var i = 0; i < recipients.length; i++)
        {
        	var recipient = recipients[i];

            if(recipient != " " && recipient.length > 1) {
            	if(recipient.indexOf(" ") == 0) {
            		recipient = recipient.substring(1);
            	}
				
				for(var j = 0; j < names.length; j++)
				{
					var name = names[j];
            		var nameid = name.getElementsByTagName("nameid")[0];
            		var nametype = name.getElementsByTagName("nametype")[0];
            		var emailaddress = name.getElementsByTagName("emailaddress")[0];
    
            		if(nameid && nameid.firstChild && nametype && nametype.firstChild && emailaddress && emailaddress.firstChild) {
              			if(nametype.firstChild.nodeValue == type)
              			{  	
              				if(nameid.firstChild.nodeValue == recipient) {
		                		recipient = emailaddress.firstChild.nodeValue;
        		        	}
            			}
            		}
            	}
        
            	recipienttype.value += recipient + "; ";
            }
    	}
    }
}

createmailitemmodule.prototype.setSender = function(message)
{
	var replyto = message.getElementsByTagName("reply-to");
	var element = dhtml.getElementById("to");

	if(replyto.length > 0) {
		for(var i = 0; i < replyto.length; i++)
		{
			var email_address = replyto[i].getElementsByTagName("email_address")[0];
	
			if(email_address && email_address.firstChild) {
				element.value = email_address.firstChild.nodeValue + "; ";
			}
		}
	} else {
		/**
		 * Get "sent_representing_email_address" and "sent_representing_name"
		 * If "sent_representing_email_address"  and "sent_representing_name" are not set 
		 * then get sender email address from "sender_email_address" and "sender_name"
		 * This happens when mail is sended using sendas option(not on behalf of).
		 */
		var sent_representing_email_address = dhtml.getTextNode(message.getElementsByTagName("sent_representing_email_address")[0],"");		
		if(sent_representing_email_address == "")
			sent_representing_email_address = dhtml.getXMLValue(message, "sender_email_address", "").toLowerCase();
		
		var sent_representing_name = dhtml.getTextNode(message.getElementsByTagName("sent_representing_name")[0],"");
		if(sent_representing_name == "")
			sent_representing_name = dhtml.getXMLValue(message, "sender_name", "");
		
		element.value = nameAndEmailToString(sent_representing_name,sent_representing_email_address,MAPI_MAILUSER,false) + "; ";
	}
}

createmailitemmodule.prototype.setSubjectBody = function(message, type)
{
	var subject = message.getElementsByTagName("normalized_subject")[0];

	var subjectElement = dhtml.getElementById("subject");
	if(subject && subject.firstChild) {
		var subjectValue = subject.firstChild.nodeValue;
		if(type && subject.firstChild.nodeValue.indexOf(type) < 0) {
			subjectValue = type + ": " + subjectValue;
		}
		
		subjectElement.value = subjectValue;
	}else if(type){
		subjectElement.value = type + ": ";
	}
	
	this.setBody(message, true, false);
}

/**
 * Function which sets the body
 * @param object message the action tag 
 * @param boolean isReply checks the mail is a reply to a mail 
 * @param boolean isConcept 
 * @param boolean addAsText checks whether any attachment is added as text to mail body
 */ 
createmailitemmodule.prototype.setBody = function(message, isReply, isConcept, addAsText)
{
	var html_body = dhtml.getElementById("html_body");
	var use_html = dhtml.getElementById("use_html");

	if(html_body) {
		var content = "";
		var replyinfo = "";
		var reply_seperator = "";
		var contentprefix = "";
		var contentpostfix = "";
		var signature_seperator_start = "";
		var signature_seperator_end = "";
		
		var htmlFormat = use_html.value == "true" ? true : false;

		if(message) {
			// check the selected items message_class to build the template, 
			// which will be displayed in body of mail.
			var message_class = dhtml.getXMLValue(message, "message_class", "").split(".");
			switch(message_class[1]){
				case "Appointment":
					data = this.createAppointmentTemplate(message, htmlFormat);
					break;
				case "Contact":
					data = this.createContactTemplate(message, htmlFormat);
					break;
				case "StickyNote":
					data = this.createStickyNoteTemplate(message, htmlFormat);
					break;
				case "Task":
					data = this.createTaskTemplate(message, htmlFormat);
					break;
				default:
					data = this.createMailTemplate(message, htmlFormat);
					break;
			}
			
			//get the body content to form the text of message
			if(isReply) {
				var isMailPrefix;
				if (this.message_action == "forward")
					isMailPrefix = webclient.settings.get("createmail/on_message_forwards", "add_prefix");
				else
					isMailPrefix = webclient.settings.get("createmail/on_message_replies", "add_prefix");

				if(htmlFormat) {
					reply_seperator = "<p><br/>&nbsp;</p>";
					if(isMailPrefix == "add_prefix") {
						contentprefix = "<blockquote style='border-left: 2px solid #325FBA; padding-left: 5px;margin-left:5px;'>";
						contentpostfix = "</blockquote>";
					}
					replyinfo = addAsText ? "" : "-----" + _("Original message") + "-----<br />";
					//build the Template data to display
					for(var key in data){
						if(key == "body"){
							content = data[key].value;
						}else{
							replyinfo += "<strong>" + data[key].label + ":</strong>\t" + data[key].value.htmlEntities() + "<br />";
						}
					}
				} else {
					reply_seperator = "\n\n";
					replyinfo = addAsText ? "" :"-----" + _("Original message") + "-----\n";
					for(var key in data){
						if(key == "body"){
							content = data[key].value;
						}else{
							replyinfo += data[key].label + ":\t" + data[key].value + "\n";
						}
					}
					// plain text reply/forward, use ">" to quote the message
					content = content.wordWrap(80,"\n")
					if(isMailPrefix == "add_prefix") {
						content = content.replace(/^/g,"> ");
						content = content.replace(/\n/g,"\n> ");
					}
				}
			} else if(isConcept) {
				content = data.body.value;
			}
		}

		var signature = "";
		if(!isConcept){
			var signatureData = this.getSignatureData();

			if(signatureData && signatureData != ""){
				if(signatureData["type"] == "plain") {
					// signature is stored in plain format
					if(!htmlFormat) {
						// no conversion
						signature = signatureData["content"];
						signature_seperator_start = "\n\n";
					} else {
						// convert plain signature to html format
						signature = convertPlainToHtml(signatureData["content"]);
						signature_seperator_start = "<p><br/>";
						signature_seperator_end = "</p>";
					}
				} else {
					// signature is stored in html format
					if(!htmlFormat) {
						//convert html signature to plain text format
						signature = convertHtmlToPlain(signatureData["content"]);
						signature_seperator_start = "\n\n";
					} else {
						// no conversion
						signature = signatureData["content"];
						signature_seperator_start = "<p><br/>";
						signature_seperator_end = "</p>";
					}
				}
				signature = signature_seperator_start + signature + signature_seperator_end;
			}
		}

		// If cursorPos is set at end then don't add signature now.
		var cursorPos = webclient.settings.get("createmail/cursor_position", "start");

		if(cursorPos == "end" && isReply) {
		this.signature = addAsText ? "": signature;
			content = contentprefix + replyinfo + content + contentpostfix + reply_seperator;
		} else {
			if(addAsText){
				content = reply_seperator + contentprefix + replyinfo + content + contentpostfix;
			}else{
				content = signature + reply_seperator + contentprefix + replyinfo + content + contentpostfix;
			}
		}

		if(htmlFormat) {
			content = content.replace(/\n/g, "<br />");
		}

		// Check if HTML editor is loaded and set the body
		if(htmlFormat) {
			/**
			 * Store the content in a temporary variable. If the XML data is 
			 * loaded before the FCK editor is ready we have to hold on to the 
			 * body until the editor is loaded.
			 */
			document.fckEditorContent = content;

			// If the editor is already available and fully loaded we can set the body already.
			if(document.fckEditor){
				window.bodyset = true;
				setContentInBodyArea(addAsText);
				setCursorPosition(this.messageAction);
			}
		} else {
			if(addAsText){
				// as we need the attach item text at same place of the position on the cursor
				insertAtCursor(html_body, content);
			}else{
				html_body.value = content;
				setCursorPosition(this.messageAction);
			}
		}
	}
}

createmailitemmodule.prototype.setData = function(data)
{
	if(data["storeid"]) {
		this.storeid = data["storeid"];
	}
	
	if(data["parententryid"]) { 
		this.parententryid = data["parententryid"];
	}
	
	if(data["message_action"]) { 
		this.message_action = data["message_action"];
		
		if(data["message_action_entryid"]) {
			this.message_action_entryid = data["message_action_entryid"];
		}
	}
}

createmailitemmodule.prototype.save = function(props, send, recipients, dialog_attachments, inlineimages)
{
	var data = new Object();
	if(this.storeid) {
		data["store"] = this.storeid;
	}
	
	data["props"] = props;
	data["recipients"] = recipients;
	data["dialog_attachments"] = dialog_attachments;
	data["inline_attachments"] = inlineimages;
	
	// We don't want to save attachments again if message is already saved.	
	if(this.message_action && (typeof props["entryid"] != 'undefined' && props["entryid"] == '')) {
		data["message_action"] = new Object();
		data["message_action"]["action_type"] = this.message_action;
		data["message_action"]["entryid"] = this.message_action_entryid;
		data["message_action"]["storeid"] = this.storeid;
	}
	
	if(send) {
		data["send"] = true;
	}

	if(parentWebclient) {
		parentWebclient.xmlrequest.addData(this, "save", data, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest(true);
	} else {
		webclient.xmlrequest.addData(this, "save", data);
		webclient.xmlrequest.sendRequest();
	}
}

/**
 * getSignature
 * 
 * Retrieve signature from the Settings Object. 
 * If no ID is supplied then check message_action (newmsg, reply, fwd).
 * @param id number (Optional) ID of the signature that needs to be retrieved.
 */
createmailitemmodule.prototype.getSignatureData = function(id){
	var signatureID = false;
	var sig_content = "";

	if(id){
		var name = parentWebclient.settings.get("createmail/signatures/"+id+"/name", false);
		if(name){
			signatureID = id;
		}else{
			signatureID = false;
		}
	}else{
		switch(this.message_action){
			case "reply":
			case "replyall":
			case "forward":
				signatureID = parentWebclient.settings.get("createmail/signatures/select_sig_replyfwd", false);
				break;
			default:
				signatureID = parentWebclient.settings.get("createmail/signatures/select_sig_newmsg", false);
		}
	}

	if(signatureID){
		sig_content = parentWebclient.settings.get("createmail/signatures/"+signatureID, "");
	}

	return sig_content;
}

/**
 * Checks wheather fckEditor is loaded, and then set the content in fckEditor's body
 * @param boolean addAsText checks if the content has any attachment added as text
 */
function setContentInBodyArea(addAsText)
{
	if(document.fckEditor && document.fckEditor.SetHTML) {
		
		if(addAsText ){
			//this checks the case in which we add attachment as text to a replying or forwarding mail
			document.fckEditor.Selection.Restore();
			document.fckEditor.InsertHtml(document.fckEditorContent);
		}else{
			document.fckEditor.SetHTML(document.fckEditorContent);
		}
		// if the content is already loaded and there is no further action for 
		// attaching any item as text then set fckEditor as false.
		if(!addAsText && module.messageAction == "undefined")	document.fckEditor = false;
		
		document.fckEditorContent = false;
	} else {
		window.setTimout(setBody, '500');
	}
}

/**
 * eventAddSignatureMenuItem
 * 
 * 
 * @param moduleObject object module
 * @param element HTMLelem referenced element
 * @param event object event
 */
function eventAddSignatureMenuItem(moduleObject, element, event){
	element.parentNode.style.display = "none";
	addSignature(element.data["sig_id"]);
}
