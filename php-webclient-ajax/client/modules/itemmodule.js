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
 * ItemModule
 * The module to load, save and delete one item. 
 */
ItemModule.prototype = new Module;
ItemModule.prototype.constructor = ItemModule;
ItemModule.superclass = Module.prototype;

function ItemModule(id, element)
{
	if(arguments.length > 0) {
		this.init(id, element);
	}
}

/**
 * Function which intializes the module.
 * @param integer id id
 * @param object element the element for the module
 */ 
ItemModule.prototype.init = function(id, element)
{
	ItemModule.superclass.init.call(this, id, element);
	
	this.storeid = false;
	this.parententryid = false;
	this.messageentryid = false;
	
	this.rootentryid = false;
	this.attachNum = false;
	
	this.attachments = new Array();
	this.newattachments = new Array();
	this.deletedattachments = new Object();
	this.owner = false;
	this.inlineattachments = new Object();
	this.inlineimages = new Array();

	// Contains all the properties for the item in this module
	this.itemProps = new Object();

}

/**
 * Function which execute an action. This function is called by the XMLRequest object.
 * @param string type the action type
 * @param object action the action tag 
 */ 
ItemModule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "item":
			this.item(action);
			webclient.menu.showMenu();
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
 * Function which will add the selected items as embedded message attachments while composing the mail.
 * @param object action the action tag 
 */ 
ItemModule.prototype.setAttachItemData = function(action)
{
	if(this.message_action == "reply" || this.message_action == "replyall"){
		this.deleteattachments = new Object();
		this.attachments = new Array();
	}
	var item = action.getElementsByTagName("item")[0];
	var attachments = item.getElementsByTagName("attachment");
	if(attachments && attachments.length > 0) {
		for(var i = 0; i < attachments.length; i++)
		{
			var attachment = new Object();
			 attachment["attach_num"] = dhtml.getXMLValue(attachments[i], "attach_num", false);
			 attachment["attach_method"] = dhtml.getXMLValue(attachments[i], "attach_method", false);
			 attachment["name"] = dhtml.getXMLValue(attachments[i], "name", false);
			 attachment["size"] = dhtml.getXMLValue(attachments[i], "size", false);
			 attachment["filetype"] = dhtml.getXMLValue(attachments[i], "filetype", false);
			 attachment["entryid"] = dhtml.getXMLValue(attachments[i], "entryid", false);
			 attachment["hidden"] = dhtml.getXMLValue(attachments[i], "hidden", false);

			this.newattachments.push(attachment);
		}
	}
	this.setAttachmentData(this.newattachments);
	this.setAttachments();
}

/**
 * Function which sets the properties for the item.
 * @param object action the action tag 
 */  
ItemModule.prototype.item = function(action)
{

	var message = action.getElementsByTagName("item")[0];
	this.photoLoadedFlag = false;

	if(message && message.childNodes) {

		webclient.pluginManager.triggerHook("client.module.itemmodule.item.before", {message: message});

		// remember item properties
		this.updateItemProps(message);
		
		this.setConflictInfo(message);
		this.setRecipients(message);
		var attachments = message.getElementsByTagName("attachment");
	
		if(attachments && attachments.length > 0) {
			for(var i = 0; i < attachments.length; i++)
			{
				var attach_num = attachments[i].getElementsByTagName("attach_num")[0];
				var attach_method = attachments[i].getElementsByTagName("attach_method")[0];
				var name = attachments[i].getElementsByTagName("name")[0];
				var size = attachments[i].getElementsByTagName("size")[0];
				var filetype = attachments[i].getElementsByTagName("filetype")[0];
				var hidden = attachments[i].getElementsByTagName("hidden")[0];

				// property for checking whether contactphoto is attached with attachments or not
				var attachment_contactphoto = attachments[i].getElementsByTagName("attachment_contactphoto")[0];
				// Get height width element of the photo.
				var attachment_contactphoto_sizex = attachments[i].getElementsByTagName("attachment_contactphoto_sizex")[0];
				var attachment_contactphoto_sizey = attachments[i].getElementsByTagName("attachment_contactphoto_sizey")[0];

				var cid = attachments[i].getElementsByTagName("cid")[0];
				
				
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
					if (attachment_contactphoto) {
						attachment["attachment_contactphoto"] = (attachment_contactphoto.firstChild.nodeValue == "1")?true:false;
						if (attachment["attachment_contactphoto"] == true) {
							// Get height width of the photo.
							if(typeof(attachment_contactphoto_sizex) != "undefined")
								attachment["attachment_contactphoto_sizex"] = attachment_contactphoto_sizex.firstChild.nodeValue;
							if(typeof(attachment_contactphoto_sizey) != "undefined")
								attachment["attachment_contactphoto_sizey"] = attachment_contactphoto_sizey.firstChild.nodeValue;
						}
					}

					this.attachments.push(attachment);
				}
			}
		}
	
		this.setProperties(message);
		this.setAttachments(message);
		this.setBody(message);
		this.setFrom(message);
	}
	
	if(this.executeOnLoad) {
		this.executeOnLoad();
	}
}

/**
 * Function will show an error message returned by server.
 * @param XMLNode action the action tag
 */
ItemModule.prototype.handleError = function(action)
{
	// show error message to user
	var errorMessage = dhtml.getXMLValue(action, "message", false);

	//Show error message, and also confirm for closing the window, to don't loose any information.
	if(errorMessage) {
		var msgString = errorMessage + "\n" + _("Would you like to close the window?");
		if (confirm(msgString))
			window.close();
	}
}

ItemModule.prototype.setProperties = function(message)
{
	for(var i = 0; i < message.childNodes.length; i++)
	{
		var property = message.childNodes[i];

		if(property) {
			var element = dhtml.getElementById(property.tagName);
			
			if(element) {
				// exclude "attachments" and "recipients" because we know that these are not
				// directly used as a value in the form. they are processed by 'setAttachments'
				// and 'setRecipients'.
				if (property.tagName == "attachments" || property.tagName == "recipients")
					continue;

				if(property.attributes && property.attributes.length > 0) {
					for(var j = 0; j < property.attributes.length; j++)
					{
						var item = property.attributes.item(j);
						if (item.nodeName!="type"){
							element.setAttribute(item.nodeName, item.nodeValue);
						}
					}
				}
				var value = "";
				if (property.firstChild){
					value = property.firstChild.nodeValue;
				}
				if (property.getAttribute("type") == "timestamp"){		
					value = strftime(_("%a %x %X"), property.firstChild.nodeValue);
				}
				if (value == null){
					value = "";
				}
				switch(element.tagName.toLowerCase()) 
				{
					case "span": 
					case "div": 
						element.innerHTML = value.htmlEntities();
						break; 
					default:
					case "input": 
					case "textarea": 
						element.value = value;
						break; 
				}
			}
		}
	}
} 

/**
 * setAttachmentData
 * 
 * Used by the attachment dialog to set the list of new attachments and the list of attachments that
 * have to be deleted. The dialog cannot just set it in the property of this module directly. If you
 * do this you set a reference to an Array/Object in the attachment dialog. After the user is done 
 * with uploading it will close that dialog and the reference will point to a non-existing object. 
 * There we need to create a copy of the object and remove all references. 
 * 
 * @param newattachments Array List of new attachments
 * @param deletedattachments Object List of attachments to be deleted
 */
ItemModule.prototype.setAttachmentData = function(newattachments, deletedattachments)
{
	// We have to copy the newattachments list, but cannot use the dhtml.clone function as it can not copy an Array.
	copyNewattachments = new Array();
	for(var i in newattachments){
		copyNewattachments[i] = new Object();
		for(var j in newattachments[i]){
			copyNewattachments[i][j] = newattachments[i][j];
		}
	}
	this.newattachments = copyNewattachments;

	this.deletedattachments = dhtml.clone(deletedattachments, true);
}

/**
 * Function which sets the attachments
 */ 
ItemModule.prototype.setAttachments = function()
{
	var attachmentsElement = dhtml.getElementById("itemattachments");

	if(attachmentsElement) {
		attachmentsElement.innerHTML = NBSP;
			
		// Message attachments
		this.addAttachments(attachmentsElement, this.attachments);
		// Uploaded attachments
		this.addAttachments(attachmentsElement, this.newattachments);
	
		if(attachmentsElement.innerHTML.length <= 0) {
			attachmentsElement.innerHTML = NBSP;
		}
		
		if(attachmentsElement.offsetHeight > 30) {
			attachmentsElement.style.height = "32px";
		}
	}
	resizeBody();
}

/**
 * Function which adds the attachments to the view.
 * @param object attachmentsElement element
 * @param array attachments the attachments 
 */  
ItemModule.prototype.addAttachments = function(attachmentsElement, attachments)
{
	this.setInlineAttachmentData(attachments);
	
	if(attachments && attachments.length > 0) {
		for(var i = 0; i < attachments.length; i++)
		{
			var attachment = attachments[i];

			if(attachment["attach_num"] && !attachment["hidden"]) {
				if(typeof(this.deletedattachments[attachment["attach_num"]]) == "undefined" && typeof this.inlineimages[attachments[i]["attach_num"]] == "undefined") {
					if(name && name.firstChild) {
						attachment["name"] = name.firstChild.nodeValue;
					}
					
					var kb = "0";
					if(attachment["size"]) {
						kb = Math.round(attachment["size"] / 1024) + _("kb");
						if(attachment["size"] < 1024) {
							kb = attachment["size"] + _("B");
						}
					}
					
					var attachmentElement = dhtml.addElement(attachmentsElement, "span");
					attachmentElement.setAttribute("attach_num", (attachment["attach_num"]||false));

					var attachmentLink = dhtml.addElement(attachmentElement, "a");
					attachmentLink.setAttribute("attach_num", (attachment["attach_num"]||false));
					attachmentLink.href = "#";
					var name = attachment["name"] || _("Untitled Attachment");
					dhtml.addTextNode(attachmentLink, name + " (" + kb + ");");
					dhtml.addEvent(this, attachmentLink, "mousedown", eventAttachmentClick);

					dhtml.addTextNode(attachmentsElement, NBSP);
					
					if (typeof this.inlineattachments[attachments[i]["attach_num"]] != "undefined") {
						attachmentElement.id = "inline_"+ attachments[i]["attach_num"];
					}
				}
			}

			// If contactphoto is attached then show it in photos.
			if (attachment["attachment_contactphoto"] && !this.photoLoadedFlag) {
				// push attachment into its element as Inline base64_encoded picture
				var contactphotoElement = dhtml.getElementById("contactphoto");
				var contactphotobinElement = dhtml.addElement(contactphotoElement, "img");
				contactphotobinElement.src = "index.php?load=download_attachment&store="+ this.storeid +"&entryid="+ this.messageentryid +"&attachNum[]="+ attachment["attach_num"] +"&openType=inline";

				// Set Height and width of the contact photo element.
				if(typeof(attachment["attachment_contactphoto_sizey"]) != "undefined")
					contactphotobinElement.height = attachment["attachment_contactphoto_sizey"];
				if(typeof(attachment["attachment_contactphoto_sizex"]) != "undefined")
					contactphotobinElement.width = attachment["attachment_contactphoto_sizex"];

				window.contactphotobinElement = contactphotobinElement;
				this.photoLoadedFlag = true;
			}
		}
	}
	if(!this.photoLoadedFlag) {
		var contactphotoElement = dhtml.getElementById("contactphoto");
		dhtml.addClassName(contactphotoElement,"default_contact_photo");
		this.photoLoadedFlag = true;
	}
}

/**
 * Function which requests for the item properties to build the body
 * @param object message contains selected messages entryids and type
 */ 
ItemModule.prototype.sendConversionItemData = function(message){
	
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.parententryid;
	data["messages"] = new Object();
	data["messages"]["message"]= new Object();
	data["messages"]["message"]= message.entryids;

	webclient.xmlrequest.addData(this, "convert_item", data); 
	webclient.xmlrequest.sendRequest();
}


/**
 * Function which will add the selected items as text in body of the composing mail.
 * @param object action the action tag 
 */ 
ItemModule.prototype.setBodyFromItemData = function(action)
{
	var message = action.getElementsByTagName("item");
	//set the body of contact item
	for(var i=0;i<message.length;i++){
		this.setBody(message[i], true, false, true);
	}
	// setting focus and also setting subject for single items
	if(message.length == 1){
		dhtml.getElementById("subject").value = convertHtmlToPlain(dhtml.getXMLValue(message[0], "subject", ""));
		dhtml.getElementById("html_body").focus();
	}else{
		dhtml.getElementById("subject").focus();
	}
}

ItemModule.prototype.setBody = function(message, isReply, isConcept, addAsText)
{
	var html_body = dhtml.getElementById("html_body");
	
	if(html_body && !addAsText) {
		var body = message.getElementsByTagName("body")[0];

		if(body && body.childNodes.length > 0) {
			var content = "";
			var element = body.firstChild;
			for(var i = 0; i < body.childNodes.length; i++)
			{
				content += element.nodeValue;
				element = element.nextSibling;
			}

			html_body.value = content;
		}
	}else{ // build body content for displaying attachment at text in message
		var content = "";
		var data, count;
		var message_class = dhtml.getXMLValue(message, "message_class", "").split(".");
		// check the selected items message_class to build the template,which is added to body
		switch(message_class[1]){
			case "Appointment":
				data = this.createAppointmentTemplate(message, false);
				break;
			case "Contact":
				data = this.createContactTemplate(message, false);
				break;
			case "StickyNote":
				data = this.createStickyNoteTemplate(message, false);
				break;
			case "Task":
				data = this.createTaskTemplate(message, false);
				break;
			default:
				data = this.createMailTemplate(message, false);
				break;
		}
		//build the Template data to display
		content = "-------------------------\n";
		for(var key in data){
			if(key == "body"){
				content += data[key].value;
			}else{
				if(usingHTMLEditor()){
					content += data[key].label + ":\t" + data[key].value.htmlEntities() + "\n";
				}else{
					content += data[key].label + ":\t" + data[key].value + "\n";
				}
			}
		}
		if(html_body)
			insertAtCursor(html_body, content);
	}
}

/**
 * Function which build the appointment template with labels and value 
 * @param object message the action tag  
 * @param boolean htmlFormat tells wheather build content is HTML format or not
 * @return object result contains all the values.
 */ 
ItemModule.prototype.createAppointmentTemplate= function(message, htmlFormat)
{
	var properties = getAppointmentProperties();
	var result = new Object;
	for(var tagName in properties)
	{
		var xmlValue = dhtml.getXMLValue(message, tagName, -1);
		if(xmlValue !== -1){
			switch(tagName){
				case "busystatus":
					switch(parseInt(xmlValue, 10))
					{
						case fbFree:
							xmlValue = _("Free");
							break;
						case fbTentative:
							xmlValue = _("Tentative");
							break;
						case fbBusy:
							xmlValue = _("Busy");
							break;
						case fbOutOfOffice:
							xmlValue = _("Out of Office");
							break;
						default:
							xmlValue = NBSP;
							break;
					}
					break;
				case "importance":
					switch(parseInt(xmlValue, 10)) {
						case IMPORTANCE_LOW:
							xmlValue = _("Low");
							break;
						case IMPORTANCE_HIGH:
							xmlValue = _("High");
							break;
						case IMPORTANCE_NORMAL:
						default:
							xmlValue = NBSP;
							break;
						}
					break;
				case "meeting":
					// first check that we are really dealing with a meeting and not an appointment
					var meetingStatus = parseInt(xmlValue, 10);
					if(!isNaN(meetingStatus) && meetingStatus !== olNonMeeting) {
						var responseStatus = parseInt(dhtml.getXMLValue(message, "responsestatus", olResponseNone), 10);
						switch(responseStatus)
						{
							case olResponseNone:
								xmlValue = _("No Response");
								break;
							case olResponseOrganized:
								xmlValue = _("Meeting Organizer");
								break;
							case olResponseTentative:
								xmlValue = _("Tentative");
								break;
							case olResponseAccepted:
								xmlValue = _("Accepted");
								break;
							case olResponseDeclined:
								xmlValue = _("Declined");
								break;
							case olResponseNotResponded:
								xmlValue = _("Not Yet Responded");
								break;
						}
						result["required_attendee"] = new Object;
						result["required_attendee"].label = _("Required Attendee");
						result["required_attendee"].value = this.generateRecipientStringFromXML(message, "to", false);

					} else {
						xmlValue = NBSP;
					}
					break;
				case "recurring":
					var xmlValue = parseInt(xmlValue, 10);
					if(!isNaN(xmlValue)) {
						var recurring_type = parseInt(dhtml.getXMLValue(message, "recur_type"),10);
						switch(recurring_type)
						{
							case olRecursDaily:
								xmlValue = _("Daily");
								break;
							case olRecursWeekly:
								xmlValue = _("Weekly");
								break;
							case olRecursMonthly:
							case olRecursMonthNth:
								xmlValue = _("Monthly");
								break;
							case olRecursYearly:
							case olRecursYearNth:
								xmlValue = _("Yearly");
								break;
						}

						var recurring_pattern = dhtml.getXMLValue(message, "recurring_pattern", "");
						result["recurring_pattern"] = new Object;
						result["recurring_pattern"].label = _("Recurring Pattern");
						result["recurring_pattern"].value = recurring_pattern;
					}else{
						xmlValue = _("None");
					}
					break;
				case "startdate":
				case "duedate":
					xmlValue = strftime(_("%a %x %X"), xmlValue);
					break;
				case "body":
					xmlValue = htmlFormat? xmlValue.htmlEntities() : xmlValue;
					break;
			}
			// add data in result
			result[tagName] = new Object;
			result[tagName].label = properties[tagName];
			result[tagName].value = xmlValue;
		}
	}
	return result;
}

/**
 * Function which build the contact template with labels and value 
 * @param object message the action tag  
 * @param boolean htmlFormat tells wheather build content is HTML format or not
 * @return object result contains all the values.
 */ 
ItemModule.prototype.createContactTemplate= function(message, htmlFormat)
{
	var properties = getContactProperties();
	var result = new Object;
	for(var tagName in properties)
	{
		var xmlValue = dhtml.getXMLValue(message, tagName, -1);
		if(xmlValue !== -1){
			switch(tagName){
				case "birthday":
				case "wedding_anniversary":
					xmlValue = strftime(_("%a %x %X"), xmlValue);
					break;
				case "body":
					xmlValue = htmlFormat? xmlValue.htmlEntities() : xmlValue;
					break;
			}
			// add data in result
			result[tagName] = new Object;
			result[tagName].label = properties[tagName];
			result[tagName].value = xmlValue;
		}
	}
	return result;
}


/**
 * Function which build the stickyNote template with labels and value 
 * @param object message the action tag  
 * @param boolean htmlFormat tells wheather build content is HTML format or not
 * @return object result contains all the values.
 */  
ItemModule.prototype.createStickyNoteTemplate= function(message, htmlFormat)
{
	var result = new Object;
		// add data in result
		result["body"] = new Object;
		result["body"].label = _("Body");
		result["body"].value = htmlFormat ? dhtml.getXMLValue(message, "body", "").htmlEntities() : dhtml.getXMLValue(message, "body", "");
		
	return result;
}


/**
 * Function which build the task template with labels and value 
 * @param object message the action tag  
 * @param boolean htmlFormat tells wheather build content is HTML format or not
 * @return object result contains all the values.
 */ 
ItemModule.prototype.createTaskTemplate= function(message, htmlFormat)
{
	var properties = getTaskProperties();
	var result = new Object;
	for(var tagName in properties)
	{
		var xmlValue = dhtml.getXMLValue(message, tagName, -1);
		if(xmlValue !== -1){
			switch(tagName){
				case "startdate":
				case "duedate":
				case "datecompleted":
					xmlValue = strftime(_("%a %x"), xmlValue);
					break;
				case "totalwork":
				case "actualwork":
					xmlValue = (parseInt(xmlValue, 10)/60)+ "hours";
					break;
				case "status":
					switch(xmlValue){
						case olTaskNotStarted:
							xmlValue = _("Not Started");
							break;
						case olTaskInProgress:
							xmlValue = _("In Progress");
							break;
						case olTaskComplete:
							xmlValue = _("Complete");
							break;
						case olTaskWaiting:
							xmlValue = _("Waiting");
							break;
						case olTaskDeferred:
							xmlValue = _("Deferred");
							break;
					}
					break;
				case "percent_complete":
					xmlValue = xmlValue *100 +"%";
					break;
				case "body":
					xmlValue = htmlFormat? xmlValue.htmlEntities() : xmlValue;
					break;
			}
			// add data in result
			result[tagName] = new Object;
			result[tagName].label = properties[tagName];
			result[tagName].value = xmlValue;
		}
	}
	return result;
}

/**
 * Function which build the mail template with labels and value 
 * @param object message the action tag  
 * @param boolean htmlFormat tells wheather build content is HTML format or not
 * @return object result contains all the values.
 */ 
ItemModule.prototype.createMailTemplate= function(message, htmlFormat)
{
	var properties = getMailProperties();
	var result = new Object;

	for(var tagName in properties)
	{
		var xmlValue = dhtml.getXMLValue(message, tagName, -1);

		if(xmlValue !== -1){
			switch(tagName){
				case "message_delivery_time":
					xmlValue = strftime(_("%a %x %X"), xmlValue);
					break;
				case "sent_representing_email_address":
					xmlValue = nameAndEmailToString(dhtml.getXMLValue(message, "sent_representing_name", ""), xmlValue, MAPI_MAILUSER , false);
					break;
				case "attachment":
					xmlValue = this.generateAttachmentStringFromXML(message);
					break;
				case "body":
					var isHTML = dhtml.getXMLValue(message, "isHTML", false);
					var content = xmlValue;
					// HTML reply with a HTML email
					if(isHTML && htmlFormat){
						// Get index of the start of the body content
						var bodyOpenIndex = content.search(/<body/gi);
						if(bodyOpenIndex > 0){
							// Fetch index after body tag
							bodyOpenIndex = content.indexOf(">", bodyOpenIndex) + 1;
						}else{
							bodyOpenIndex = 0;
						}
						// Get index of the end of the body content
						var bodyCloseIndex = content.search(/<\/body/gi);
						if(bodyCloseIndex == -1){
							bodyCloseIndex = content.length;
						}
						// Extract body content
						content = content.slice(bodyOpenIndex, bodyCloseIndex);

						// Remove some more tags
						content = removeTagFromSource(content, "head", true, true);
						content = removeTagFromSource(content, "style", true);
						content = removeTagFromSource(content, "script", false);
						content = removeTagFromSource(content, "title", true);
						content = removeTagFromSource(content, "meta", false);
						content = removeTagFromSource(content, "base", false);
						content = removeTagFromSource(content, "link", false);

						// HACK: Remove newlines to fix HTML
						content = content.replace(/\n/g, " ");
						content = content.replace(/\r/g, " ");
					}

					if (!isHTML && htmlFormat){
						// please note that currently all replies and forwards are plain text!
						content = content.replace(/&/g, "&amp;");
						content = content.replace(/</g, "&lt;");
						content = content.replace(/>/g, "&gt;");
						content = content.replace(/ {2}/g, " &nbsp;");
					}
					xmlValue = content;
					break;
			}

			// add data in result
			if(xmlValue != "") {
				result[tagName] = new Object;
				result[tagName].label = properties[tagName];
				result[tagName].value = xmlValue;
			}
		}
	}

	var recipients = this.generateRecipientStringFromXML(message, false, false);
	if (recipients){
		for(var type in recipients){
			result[type] = new Object;
			result[type].label = type;
			result[type].value = recipients[type];
		}
	}

	return result;
}

/**
 * Function which sets the From field
 * @param object message the action tag  
 */ 
ItemModule.prototype.setFrom = function(message)
{
	var fromSelect = dhtml.getElementById("from", "select");
	
	var message_flags = parseInt(dhtml.getXMLValue(message, "message_flags", 0), 10);
	if (fromSelect && (message_flags & MSGFLAG_UNSENT)==MSGFLAG_UNSENT){

		var sent_representing_name = dhtml.getXMLValue(message, "sent_representing_name", "");
		var sent_representing_email_address = dhtml.getXMLValue(message, "sent_representing_email_address", "").toLowerCase();

		if(sent_representing_name != "" && sent_representing_email_address != ""){
			var emailString = sent_representing_name + " <" + sent_representing_email_address + ">";
			// Set the selected FROM address (only works when item is already in the list)
			fromSelect.value = emailString;
			if(fromSelect.value != emailString){
				fromSelect.options[fromSelect.options.length] = new Option(emailString);
				fromSelect.options[fromSelect.options.length-1].selected = true;
			}
			var fromRow = dhtml.getElementById("from_row", "tr");
			if(fromRow && fromSelect.options.length > 0){
				fromRow.style.display = "";
			}
		}

	}
}

/**
 * Function which sets the data for this module.
 * @param string storeid store id
 * @param string parententryid parent entry id 
 */  
ItemModule.prototype.setData = function(storeid, parententryid)
{
	if(storeid) {
		this.storeid = storeid;
	}
	
	if(parententryid) { 
		this.parententryid = parententryid;
	}
}

/**
 * Function which opens one item.
 * @param string entryid message entryid
 * @param string rootentryid the root entryid, used for embedded message in embedded message ... (optional)
 * @param array attachNum the attachment numbers, used for embedded message in embedded message ... (optional)
 * @param array basedate the basedate for appointments (optional)
 */ 
ItemModule.prototype.open = function(entryid, rootentryid, attachNum, basedate)
{
	if(entryid) {
		entryid = (entryid.indexOf('_') > 0) ? entryid.substr(0, entryid.indexOf('_')) : entryid;
		this.messageentryid = entryid;
		
		if(rootentryid) {
			this.rootentryid = rootentryid;
		} else {
			this.rootentryid = entryid;
		}
		
		this.attachNum = attachNum;
	
		var data = new Object();
		data["store"] = this.storeid;
		data["parententryid"] = this.parententryid;
		data["entryid"] = entryid;

		if(basedate){
			data['basedate'] = basedate;
		}
		
		if(rootentryid) {
			data["rootentryid"] = rootentryid;
			
			if(attachNum && attachNum.push) {
				data["attachments"] = new Object();
				data["attachments"]["attach_num"] = new Array();
				
				for(var i = 0; i < attachNum.length; i++)
				{
					data["attachments"]["attach_num"].push(attachNum[i]);
				}
			}
		}
		
		webclient.xmlrequest.addData(this, "open", data);
		webclient.xmlrequest.sendRequest();
	} else {
		webclient.menu.showMenu();
	}
}

/**
 * Function which requests to add selected item as attachments
 * @param object forwardItems contains the array of entryids of selected items
 */ 
ItemModule.prototype.forwardMultipleItems = function(forwardItems)
{		
	var data = new Object();
	data["store"] = this.storeid;
	data["parententryid"] = this.parententryid;
	data["entryids"] = new Array();
	
	data["entryids"] = forwardItems; 
	data["dialog_attachments"] = dhtml.getElementById('dialog_attachments').value
	
	webclient.xmlrequest.addData(this, "attach_items", data);
	webclient.xmlrequest.sendRequest();
}

/**
 * Function which saves an item.
 * @param object props the properties to be saved
 * @param string dialog_attachments used to add attachments (optional)   
 */ 
ItemModule.prototype.save = function(props, send, recipients, dialog_attachments)
{
	var data = new Object();
	if(this.storeid) {
		data["store"] = this.storeid;
	}
	
	if(this.parententryid)
		data["parententryid"] = this.parententryid;
		
	data["props"] = props;
	if (recipients)
		data["recipients"] = recipients;

	if (dialog_attachments)
		data["dialog_attachments"] = dialog_attachments;
	
	if(this.message_action) {
		data["message_action"] = new Object();
		data["message_action"]["action_type"] = this.message_action;
		data["message_action"]["entryid"] = this.message_action_entryid;
	}
	
	if(send) {
		data["send"] = send;
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
 * Function which deletes an item 
 */ 
ItemModule.prototype.deleteMessage = function()
{
	if(this.messageentryid) {
		var data = new Object();
		data["store"] = this.storeid;
		data["parententryid"] = this.parententryid;
		data["entryid"] = this.messageentryid;

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
	}
}

ItemModule.prototype.setRecipients = function(message, actionType)
{
	var sender_email_address = dhtml.getXMLValue(message, "sent_representing_email_address", "").toLowerCase();
	var sender_email_name = dhtml.getXMLValue(message, "sent_representing_name", "");
	var sender_email_address = nameAndEmailToString(sender_email_name, sender_email_address, MAPI_MAILUSER);
	
	var elemdata = new Array();

	var recipients = message.getElementsByTagName("recipient");

	if(recipients && recipients.length > 0) {
		for(var i = 0; i < recipients.length; i++)
		{
			var recipient = recipients[i];
			if(dhtml.getXMLValue(recipient, "recipient_flags", false)!= 3){
				var display_name = dhtml.getXMLValue(recipient, "display_name", "");
				var email_address = dhtml.getXMLValue(recipient, "email_address", "").toLowerCase();
				var type = dhtml.getXMLValue(recipient, "type", "");
				var entryid = dhtml.getXMLValue(recipient, "entryid", "");
				var objecttype = dhtml.getXMLValue(recipient, "objecttype", "");

				if (!(actionType && actionType == "replyall" && compareEntryIds(entryid,webclient.userEntryid))){
					if(type && email_address) {
						if(typeof(elemdata[type]) == "undefined") elemdata[type] = new Array();
						elemdata[type].push(nameAndEmailToString(display_name,email_address,objecttype,false));
					} else if(type && display_name) {
						// if unresolved email address
						var element = dhtml.getElementById(type);
						if(element){
							if(typeof(elemdata[type]) == "undefined") elemdata[type] = new Array();
							elemdata[type].push(display_name);
						}
					}
				}
			}else{
				this.owner = new Object;
				this.owner["display_name"] = dhtml.getXMLValue(recipient, "display_name", false);
				this.owner["fullname"] = dhtml.getXMLValue(recipient, "display_name", false);
				this.owner["email_address"] = dhtml.getXMLValue(recipient, "email_address", false);
				this.owner["type"] = dhtml.getXMLValue(recipient, "type", false);
				this.owner["entryid"] = dhtml.getXMLValue(recipient, "entryid", false);
				this.owner["recipienttype"] = dhtml.getXMLValue(recipient, "recipienttype", false);
				this.owner["recipient_flags"] = dhtml.getXMLValue(recipient, "recipient_flags", 1);
			}
		}
	}
	// create a unique array of elementdata
	// this stops entering same user in type field multiple times
	for(var type in elemdata){
		//create a unique array.
		var elementArray = uniqueArray(elemdata[type]);
		if(actionType == "replyall"){
			//flag to check whether the sender is already in to field or not?
			var tosenderflag = false;
			//check if the sender is already in to field or not, if yes then make the flag true and break;
			for(var y in elemdata["to"]){
				if(elemdata["to"][y].indexOf(sender_email_address)!=-1){
					tosenderflag = true;
					break;
				}
			}
			// now check if the sender is present in to and also in cc, 
			// if yes then remove from cc field
			if(type == "cc"){
				for(var x in elementArray){
					if(elementArray[x].indexOf(sender_email_address)!=-1){
						if(!tosenderflag){
							elementArray.splice(x,1);
						}
					}
				}
			}
		}
		//re assign the values
		elemdata[type] = elementArray;
	}
	// set the data to elements.
	for(var type in elemdata) {
	    var element = dhtml.getElementById(type);
	    
	    if(element){
			if(elemdata[type].length > 0){
				element.value += elemdata[type].join("; ");
				// make unique them again, this will eliminate the duplicate contacts in to/cc feilds (if any)
				element.value = uniqueArray(element.value.split("; ")).join("; ");
				// Adding semicolon at the end, ready to go for the next recipient
				element.value += ';';
			}

			/**
			 * When new mail dialog is loaded we should fire change event 
			 * on all elastictextareas to resize it properly,
			 * but it should not fire change event on hidden fields like to/cc/bcc of MR dialog.
			 */
			if (element.type == "textarea")
				dhtml.executeEvent(element, "change"); 
		}
    }
}

/**sendAcceptTaskRequest
 *
 * This function sends requests to server to accept a task request
 */
ItemModule.prototype.sendAcceptTaskRequest = function()
{
	var req = new Object;
	req['entryid'] = this.itemProps['entryid'];
	req['store'] = this.storeid;

	if(typeof parentWebclient != "undefined") {
		parentWebclient.xmlrequest.addData(this, 'acceptTaskRequest', req, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest(true);
	} else {
		webclient.xmlrequest.addData(this, 'acceptTaskRequest', req);
		webclient.xmlrequest.sendRequest(true);
	}
}

/**sendDeclineTaskRequest
 *
 * This function declines a task request
 */
ItemModule.prototype.sendDeclineTaskRequest = function()
{
	var req = new Object;
	req['entryid'] = this.itemProps['entryid'];
	req['store'] = this.storeid;

	if(typeof parentWebclient != "undefined") {
		parentWebclient.xmlrequest.addData(this, 'declineTaskRequest', req, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest(true);
	} else {
		webclient.xmlrequest.addData(this, 'declineTaskRequest', req);
		webclient.xmlrequest.sendRequest(true);
	}
}



/**
 * Functions to do meeting requests. These are used from various modules, including the readitemmodule, the previewreaditemmodule and the appointmentitemmodule
 * @param boolean sendResponse if it is true than reponse is sent to organizer via mail otherwise it is not sent.
 */
ItemModule.prototype.sendAcceptMeetingRequest = function(tentative, noResponse, basedate, body)
{
	// Adding human readable body text that holds the start/duedate and location information.
	var meetingTimeInfo = this.addMeetingTimeInfoToBody(body, this.itemProps['startdate'], this.itemProps['duedate'], this.itemProps['location']);

	var req = new Object;
	req['entryid'] = this.messageentryid;
	req['store'] = this.storeid;
	req['tentative'] = tentative;
	req['meetingTimeInfo'] = meetingTimeInfo;

	if (typeof basedate != 'undefined' && basedate !== "false" && basedate)
		req['basedate'] = basedate;

	// if sendResponse flag is set then attach sendResponse flag with request data.
	if(typeof(noResponse) != "undefined" && noResponse)
		req['noResponse'] = noResponse;

	if(typeof parentWebclient != "undefined") {
		parentWebclient.xmlrequest.addData(this, 'acceptMeetingRequest', req, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest(true);
	} else {
		webclient.xmlrequest.addData(this, 'acceptMeetingRequest', req);
		webclient.xmlrequest.sendRequest(true);
	}
}

/**
 * Functions to do meeting requests. These are used from various modules, including the readitemmodule, the previewreaditemmodule and the appointmentitemmodule
 */
ItemModule.prototype.sendProposalMeetingRequest = function(start, end, body, basedate)
{
	// Adding the newly proposed time information to the body.
	var meetingTimeInfo = body + "\n\n\n-----------\n" + _("New Meeting Time Proposed") + " :\n" +
					strftime(_("%A, %B %e, %Y %H:%M"), start) + " - " +
					strftime(_("%A, %B %e, %Y %H:%M"), end);

	var req = new Object;
	var proposalinfo = new String();
	var gmt = new String(new Date(start));
	gmt = " "+ gmt.substring(gmt.indexOf(_("GMT")));
	
	req['entryid'] = this.messageentryid;
	req['store'] = this.storeid;
	req['tentative'] = 1;
	req['proposed_starttime'] = start;
	req['proposed_endtime'] = end;
	req['proposed_duration'] = 1;
	req['counter_proposal'] = true;
	req['meetingTimeInfo'] = (meetingTimeInfo)?meetingTimeInfo:null;

	if (typeof basedate != 'undefined' && basedate)
		req['basedate'] = basedate;

	if(typeof parentWebclient != "undefined") {
		parentWebclient.xmlrequest.addData(this, 'acceptMeetingRequest', req, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest();
	} else {
		webclient.xmlrequest.addData(this, 'acceptMeetingRequest', req);
		webclient.xmlrequest.sendRequest();
	}
}


ItemModule.prototype.sendDeclineMeetingRequest = function(noResponse, basedate, body)
{
	// Adding human readable body text that holds the start/duedate and location information.
	var meetingTimeInfo = this.addMeetingTimeInfoToBody(body, this.itemProps['startdate'], this.itemProps['duedate'], this.itemProps['location']);

	var req = new Object;
	req['entryid'] = this.messageentryid;
	req['store'] = this.storeid;
	req['meetingTimeInfo'] = meetingTimeInfo;

	if (typeof basedate != 'undefined' && basedate !== "false" && basedate)
		req['basedate'] = basedate;

	// if sendResponse flag is set then attach sendResponse flag with request data.
	if(typeof(noResponse) != "undefined" && noResponse)
		req['noResponse'] = noResponse;

	if(typeof parentWebclient != "undefined") {
		parentWebclient.xmlrequest.addData(this, 'declineMeetingRequest', req, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest(true);
	} else {
		webclient.xmlrequest.addData(this, 'declineMeetingRequest', req);
		webclient.xmlrequest.sendRequest(true);
	}
}

/**
 * Adding human readable body text that holds the start/duedate and the location
 * of the message.
 * 
 * @param msg_body String Body of the message
 * @param start Number Timestamp of the start date
 * @param due Number Timestamp of the due date
 * @param location String Location of the message
 * @param recurring_pattern String contains recur pattern if item is set to recurr else false
 */
ItemModule.prototype.addMeetingTimeInfoToBody = function(msg_body, start, due, location, recurring_pattern){
	location = location || "";
	var meetingTimeInfo = _("When") + ": ";
	
	if (recurring_pattern) {
		meetingTimeInfo += recurring_pattern + "\n";
	} else {
		meetingTimeInfo += strftime(_("%A, %B %e, %Y %H:%M"), start) + " - ";
		meetingTimeInfo += strftime(_("%A, %B %e, %Y %H:%M"), due) + "\n";
	}

	meetingTimeInfo += _("Where") + ": "  + location + "\n\n";
	meetingTimeInfo += "*~*~*~*~*~*~*~*~*~*\n\n" + (msg_body||"");

	return meetingTimeInfo;
}


ItemModule.prototype.acceptMeetingRequest = function(noResponse, basedate, body)
{
	this.sendAcceptMeetingRequest(0, noResponse, basedate, body);
}

ItemModule.prototype.proposalMeetingRequest = function(start, end, body, basedate)
{
	this.sendProposalMeetingRequest(start, end, body, basedate);
}

ItemModule.prototype.tentativeMeetingRequest = function(noResponse, basedate, body)
{
	this.sendAcceptMeetingRequest(1, noResponse, basedate, body);
}

ItemModule.prototype.removeFromCalendar = function()
{
	var req = new Object;
	
	req['entryid'] = this.messageentryid;
	req['store'] = this.storeid;
	
	if(typeof parentWebclient != "undefined") {
		parentWebclient.xmlrequest.addData(this, 'removeFromCalendar', req, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest(true);
	} else {
		webclient.xmlrequest.addData(this, 'removeFromCalendar', req);
		webclient.xmlrequest.sendRequest();
	}
}

ItemModule.prototype.resolveConflict = function()
{
	if(typeof parentWebclient != "undefined" && parentWebclient) {
		var wc = parentWebclient;
	}else{
		var wc = webclient;
	}
	if(wc.hierarchy.isSpecialFolder('conflicts', this.itemProps["parent_entryid"])){
		if(confirm(_("You will lose the modified data. Do you want to continue?"))){
			var data = new Object();
			data["store"] = this.storeid;
			data["parententryid"] = this.parententryid;
			data["entryid"] = this.messageentryid;
			if(typeof(this.itemProps["conflictitems"]["bin"]) == "string"){
				data["conflictentryid"] = this.itemProps["conflictitems"]["bin"];
			}else{
				data["conflictentryid"] = this.itemProps["conflictitems"]["bin"][0];
			}
			wc.xmlrequest.addData(this, "resolveConflict", data, webclient.modulePrefix);
			wc.xmlrequest.sendRequest();
			dhtml.getElementById("conflict").style.display = "none";
			
			if(window.opener){
				window.opener.opener = top;
				window.opener.close()
				resizeBody();
			}
		}
	}else{
		var conflictitems = this.itemProps["conflictitems"]["bin"];
		if(typeof(conflictitems) == "string"){
			conflictitems = new Array(conflictitems);
		}
		for(var i=0;i<conflictitems.length;i++){
			if (typeof this.itemProps["message_class"] != "undefined"){
				var message_class = this.itemProps["message_class"];
			}else{
				message_class = "IPM.Note";
			}
			message_class = message_class.replace(/\./g, "_").toLowerCase();
			switch(message_class) {
				case "ipm_note":
				case "ipm_post":
				case "report_ipm_note_ndr":
				case "ipm_schedule_meeting_request":
				case "ipm_schedule_meeting_resp_pos":
				case "ipm_schedule_meeting_resp_tent":
				case "ipm_schedule_meeting_resp_neg":
				case "report_ipm_note_ipnnrn":
				case "report_ipm_note_ipnrn":
					message_class = "ipm_readmail";
					break;
			}
			if(message_class.indexOf("ipm_") >= 0) {
				message_class = message_class.substring(message_class.indexOf("_") + 1);
			}
			var uri = DIALOG_URL+"task=" + message_class + "_standard&storeid=" + this.storeid + "&entryid=" + conflictitems[i] + "&parententryid=" + this.parententryid;
			webclient.openWindow(false, message_class, uri);
		}
	}
}

ItemModule.prototype.declineMeetingRequest = function(noResponse, basedate, body)
{
	this.sendDeclineMeetingRequest(noResponse, basedate, body);
}

ItemModule.prototype.updateItemProps = function(item)
{
	this.itemProps = new Object();
	for(var j=0;j<item.childNodes.length;j++){
		if (item.childNodes[j].nodeType == 1){
			var prop_name = item.childNodes[j].tagName;
			var prop_val = dom2array(item.childNodes[j]);
			if (prop_val!==null){
				this.itemProps[prop_name] = prop_val;
			}
		}
	}
}
/**
 * Function which stores information of saved as well as new 
 * uploaded attachments.
 * @param array attachments -attachments either saved or new.
 */
ItemModule.prototype.setInlineAttachmentData = function (attachments)
{
	var dialog_attachments = dhtml.getElementById("dialog_attachments");
	if (webclient.settings.get("createmail/mailformat","html") == "html"){
		for (var i=0; i<attachments.length; i++)
		{
			var type = attachments[i]["filetype"];
			// Only for images files.
			if (type && type.substring(0, type.indexOf('/')) == "image") {
				this.inlineattachments[attachments[i]["attach_num"]] = new Object();
				this.inlineattachments[attachments[i]["attach_num"]]["name"] = attachments[i]["name"].htmlEntities();
				this.inlineattachments[attachments[i]["attach_num"]]["cid"] = attachments[i]["cid"];
				this.inlineattachments[attachments[i]["attach_num"]]["filetype"] = attachments[i]["filetype"];
				
				if (String(attachments[i]["attach_num"]).search(/^[0-9]*$/) == -1) {
					// This is new attachment.
					this.inlineattachments[attachments[i]["attach_num"]]["path"] = BASE_URL  +"index.php?load=download_attachment&dialog_attachments="+ dialog_attachments.value +"&attachNum[]="+ attachments[i]["attach_num"];
				} else {
					if (this.messageentryid && this.storeid) {
						// This is saved attachment.
						this.inlineattachments[attachments[i]["attach_num"]]["path"] = BASE_URL +"index.php?load=download_attachment&store="+ this.storeid +"&entryid="+ this.messageentryid +"&attachNum[]="+ attachments[i]["attach_num"];
					}
				}
			}
		}
	}
}
/**
 * Function which set content id of all inline
 * images. This function is called when user
 * either saves or submits message.
 * @param object -document object of FCKeditor instance.
 */
ItemModule.prototype.setCIDInlineImages = function (editorDocument)
{
	for (var attach_Num in this.inlineimages){
		var cid = "";
		if (this.inlineattachments[attach_Num]["cid"]){
			cid = this.inlineattachments[attach_Num]["cid"];
		} else {
			cid = "image"+ generateRandomString(8) +"@"+ generateRandomString(8);
			// Save new ContectID.
			this.inlineattachments[attach_Num]["cid"] = cid;
		}
		for (var j=0; j < this.inlineimages[attach_Num]["id"].length; j++){
			var imageElement = dhtml.getElementById(this.inlineimages[attach_Num]["id"][j], "img", editorDocument);
			if (imageElement) {
				imageElement.setAttribute("_fcksavedurl","cid:"+ cid);
			}
		}
		this.inlineimages[attach_Num]["cid"] = cid;
	}
}
/**
 * Function which returns attach_Num and cid 
 * of all inline images that are either added or deleted.
 * @param object editorDocument -document object of FCKeditor instance. Required to find the images in the correct DOM tree.
 * @return array -returns array of all images used as inline. See below
 *			Array[]
 *				'add' 	 => Array[]
 *			 				 Array[0]->attach_Num
 *			 		                 ->cid
 *			 	'delete' => Array[]
 *							 Array[0]->attach_Num
 *			 		                 ->cid
 */
ItemModule.prototype.checkUsedInlineAttachments = function (editorDocument)
{
	var result = new Array();
	result["add"] = new Array();
	result["delete"] = new Array();
	
	for (var attach_Num in this.inlineimages){
		// Inline image must have a content Id.
		if (this.inlineimages[attach_Num]["cid"]){
			// Create image info to sent to server.
			var image = new Object();
			image["attach_Num"] = decodeURIComponent(attach_Num);
			image["cid"] = this.inlineimages[attach_Num]["cid"];
			var elementPresent = 0;
			
			// Check for image whether present in editor.
			for (var i in this.inlineimages[attach_Num]["id"]){
				var imageElement = dhtml.getElementById(this.inlineimages[attach_Num]["id"][i], "img", editorDocument);
				if (imageElement){
					elementPresent = 1;
					break;
				}
			}
			// If image added to editor push to add array, else if removed from editor push to delete array.
			if (elementPresent){
				result["add"].push(image);
			} else {
				result["delete"].push(image);
			}
		}
	}
	return result;
}
/**
 * Function which retrives all saved inline images
 * from the message body when body is loaded.
 * @param object editorDocument -document object of Fckeditor instance
 */
ItemModule.prototype.retrieveInlineImagesFromBody = function (editorDocument)
{
	var images = editorDocument.getElementsByTagName("img");
	for (var i=0; i<images.length; i++){
		
		var src = images[i].getAttribute("src");
		// Search for contentID and base_url.
		if (src.indexOf("attachCid=") > 0){

			var contentId = src.substr(src.indexOf("attachCid=")+10);
			contentId = contentId.substring(0, contentId.indexOf("&"));
			// Decode the CID so it will match the unencoded data in the inlineattachments array
			contentId = decodeURIComponent(contentId);
			
			for (var attach_Num in this.inlineattachments) {
				if (contentId == this.inlineattachments[attach_Num]["cid"]){

					// Create new array object if not defined.
					if (typeof this.inlineimages[attach_Num] == "undefined"){
						this.inlineimages[attach_Num] = new Array();
						this.inlineimages[attach_Num]["id"] = new Array();
					}
					
					// Store all element ids in module variable.
					var elementId = images[i].id ? images[i].id:generateRandomString(8);
					this.inlineimages[attach_Num]["id"].push(elementId);
					this.inlineimages[attach_Num]["cid"] = contentId;
					images[i].id = elementId;
				}
			}
		}
	}
}
/**
 * Function which adds insert icon to all image attachments
 * in attachments bar.
 */
ItemModule.prototype.setAttachmentbarAttachmentOptions = function()
{
	if (typeof FCKeditorAPI != "undefined"){
		// Loop through all images of msg and add insert icon along with attachment name.
		for (var attach_num in module.inlineattachments){
			// Get element that contains attachment name.
			var attachmentElement = dhtml.getElementById("inline_"+ attach_num);
			if (attachmentElement) {
				// Check if icon is already present.
				var icon = dhtml.getElementsByClassNameInElement(attachmentElement, "icon_insert_inline", "span");
				if (icon.length == 0){
					// Add inline css class that are inline attachments
					dhtml.addClassName(attachmentElement, "inline_attachments");
					// Create insert icon and add to attachment name.
					var insertIcon = dhtml.addElement(attachmentElement, "span", "icon icon_insert_inline");
					insertIcon.title = _("Add to message body");
					// Stupid IE bug
					insertIcon.innerHTML = NBSP;
					// Set necessary events for adding image as inline to message body.
					dhtml.addEvent(module, attachmentElement, "mouseover", eventItemAttachmentNameAddInlineMouseOver);
					dhtml.addEvent(module, attachmentElement, "mouseout", eventItemAttachmentNameAddInlineMouseOut);
					dhtml.addEvent(module, insertIcon, "click", eventItemAttachmentNameAddInlineClick);
				}
			}
		}
	}
}

/**
 * Function which displays conflict information,
 * if item is in conflict state.
 *
 * @param object message message
 */
ItemModule.prototype.setConflictInfo = function (message)
{
	if (message){
		if(message.getElementsByTagName("conflictitems").length) {
			var conflict = dhtml.getElementById("conflict");
			dhtml.addEvent(this, conflict, "click", eventConflictClick);
			conflict.style.display = "block";
			if(typeof parentWebclient != "undefined" && parentWebclient) {
				var specialfolder = parentWebclient.hierarchy.isSpecialFolder('conflicts', dhtml.getXMLValue(message, "parent_entryid", ""));
			}else{
				var specialfolder = webclient.hierarchy.isSpecialFolder('conflicts', dhtml.getXMLValue(message, "parent_entryid", ""));
			}
			if(specialfolder){
				dhtml.addElement(conflict, "p", false, false, _('You made changes to another copy of this item. Click here to replace the existing item with this version.'));
			}else{
				dhtml.addElement(conflict, "p", false, false, _('You made changes to another copy of this item. This is the most recent version. Click here to see the other versions.'));
			}
			//this function adds one extra row, which spoils the layout, to fix that layout call resizeBody.
			if(typeof resizeBody != "undefined")
				resizeBody();
		}else{
		    var conflictElement = dhtml.getElementById("conflict");
		    
		    if(conflictElement)
		        conflictElement.style.display = "none";
		}
	}
}

/**
 * Function generates a string which will be used to show the recipients in Template
 * @param object message object xml data of each message
 * @param string recipienttype Optional string which contains the recipient type
 * @return string result string which contains the recipients name and email
 */
ItemModule.prototype.generateRecipientStringFromXML = function(message, recipienttype, htmlformat)
{	
	var recipientList = new Object();
	var recipients = message.getElementsByTagName("recipients")[0];

	if(recipients){	
		var recipient = recipients.getElementsByTagName("recipient");
		for (var j = 0; j < recipient.length; j++){
			var resDisplayName = dhtml.getXMLValue(recipient[j], "display_name", "");
			var resEmailAddress = dhtml.getXMLValue(recipient[j], "email_address", "");
			var resType = dhtml.getXMLValue(recipient[j], "type", "");
			var resObjectType = dhtml.getXMLValue(recipient[j], "objecttype", "");
			if (resType != "bcc"){
				switch(resType){
					case "to":
						resType = _("To");
						break;
					case "cc":
						resType = _("CC");
						break;
				}
				if(!recipientList[resType]){
					recipientList[resType] = "";
				}
				recipientList[resType] += nameAndEmailToString(resDisplayName, resEmailAddress, resObjectType, htmlformat)+"; ";
			}
		}
	}
	//return the recipients data
	if(recipienttype){
		switch(recipienttype){
			case "to":
				recipienttype = _("To");
				break;
			case "cc":
				recipienttype = _("CC");
				break;
		}
		return recipientList[recipienttype];
	}else{
		return recipientList;
	}
}

/**
 * Function generates a string which will be used to show the attachments in Template
 * @param object message object xml data of each message
 * @return string attachments contains attachment data
 */
ItemModule.prototype.generateAttachmentStringFromXML = function(message)
{
	var attachment = new Array();
	var attachments = message.getElementsByTagName("attachment");
	for(var i = 0; i < attachments.length; i++){
		if(dhtml.getXMLValue(attachments[i], "hidden") != "1")
			attachment.push(convertHtmlToPlain(dhtml.getXMLValue(attachments[i], "name", _("Untitled"))));
	}
	return attachment.join(", ");
}

/**
 * Function sends request for getting information about uploaded files.
 */
ItemModule.prototype.getAttachments = function ()
{
	var data = new Object();
	data["store"] = this.storeid;
	data["entryid"] = this.parententryid;
	// Pass dialog_attachments id to get attachments for current dialog.
	data["dialog_attachments"] = dhtml.getElementById('dialog_attachments').value;

	webclient.xmlrequest.addData(this, "getAttachments", data);
	webclient.xmlrequest.sendRequest();
}

/**
 * Function extracts information from uploaded attachments and passes
 * array of uploaded files to setAttachmentData function to show attachments in attachmentbar.
 *
 * @param object action response from server, list of uploaded attachments.
 */
ItemModule.prototype.attachmentsList = function(action)
{
	var files = action.getElementsByTagName("files");
	var attachmentsData = new Array();

	if(files && files.length > 0) {
		for(var i = 0; i < files.length; i++)
		{
			var attachment= new Object();
			attachment["attach_num"] = dhtml.getXMLValue(files[i], "attach_num");
			attachment["filetype"] = dhtml.getXMLValue(files[i], "filetype");
			attachment["name"] = dhtml.getXMLValue(files[i], "name");
			attachment["size"] = dhtml.getXMLValue(files[i], "size");

			attachmentsData.push(attachment);
		}
	}

	// Set attachments information and display them in attachmentbar of dialog.
	module.setAttachmentData(attachmentsData);
	module.setAttachments();
}

function eventConflictClick(moduleObject, element, event)
{
	moduleObject.resolveConflict();
}

/**
 * Function which deletes out-of-date meeting request.
 */
function eventPreviewItemNotCurrentClick(moduleObject, element, event)
{
	if (confirm(_("This meeting request is out-of-date and will now be deleted."))) {
		moduleObject.deleteMessage();
		
		//Close window if meeting request is opened.
		if (typeof(parentWebclient) != "undefined"){
			window.close();
		}
	}
}
/**
 * Function which highlights attachment name on mouseover.
 */
function eventItemAttachmentNameAddInlineMouseOver(moduleObject, element, event)
{
	dhtml.addClassName(element, "attachmentover");
}
/**
 * Function which removes css class on mouseout.
 */
function eventItemAttachmentNameAddInlineMouseOut(moduleObject, element, event)
{
	dhtml.removeClassName(element, "attachmentover");
}

/**
 * Funtion which adds image to editor.
 */
function eventItemAttachmentNameAddInlineClick(moduleObject, element, event)
{
	if (typeof FCKeditorAPI != "undefined"){
		var fckEditor = FCKeditorAPI.GetInstance("html_body");
		// Get attach_num from id of parent element.
		var attach_num = element.parentNode.id.substr(element.parentNode.id.indexOf('_')+1);
		if (moduleObject.inlineattachments[attach_num]) {
			// Generate unique id for image element.
			var elementId = generateRandomString(8);
			// Create new array object if not defined.
			if (typeof moduleObject.inlineimages[attach_num] == "undefined") {
				moduleObject.inlineimages[attach_num] = new Array();
				moduleObject.inlineimages[attach_num]["id"] = new Array();
			}
			// Store all element ids in module variable.
			moduleObject.inlineimages[attach_num]["id"].push(elementId);
			// Create img tag.
			var imageElement = "<img id='"+ elementId +"' src='"+ moduleObject.inlineattachments[attach_num]["path"] +"'>";
			// Restore previous selection in editor If IE.
			if (window.BROWSER_IE)
				fckEditor.Selection.Restore();
			// Add image to editor.
			fckEditor.InsertHtml(imageElement);
			// Attachment name as it is now an inline attachment.
			element.parentNode.style.display = "none";
		}
	}
}

/**
 * Function which handles link actions to prevent termination of xmlrequests.
 */
function eventAttachmentClick(moduleObject, element, event)
{
	var attach_num = element.getAttribute("attach_num", false);
	var attachments = (moduleObject.itemProps && moduleObject.itemProps.attachments) ? moduleObject.itemProps.attachments.attachment : new Array();
	if(attachments.length === 0 && moduleObject.attachments){
		// clone array
		for(var i in moduleObject.attachments){
			attachments[i] = new Object();
			for(var j in moduleObject.attachments[i]){
				attachments[i][j] = moduleObject.attachments[i][j];
			}
		}
	}

	/**
	 * When an attachment is clicked in an edit dialog, there is also the possibility that the 
	 * attachment has not been saved at the server yet. So therefor we need add the list of unsaved 
	 * new attachments as well.
	 */
	if(moduleObject.newattachments){
		for(var i in moduleObject.newattachments){
			attachments[attachments.length] = moduleObject.newattachments[i];
		}
	}

	// If only one attachment, then wrap it into an array
	if (!attachments[0]){
		attachments = new Array(attachments);
	}

	if (attach_num){
		var attachment = false;
		
		// Find attachment from attach_num
		for(var i = 0; i < attachments.length; i++){
			if (attachments[i]["attach_num"] === attach_num){
				attachment = attachments[i];
				break;
			}
		}

		if (attachment){
			var attachNum = "";

			if(this.attachNum && this.attachNum.push) {
				for(var j = 0; j < this.attachNum.length; j++)
					attachNum += "&attachNum[]=" + this.attachNum[j];

				if (typeof module.itemProps.occurrAttachNum != 'undefined')
					attachNum += "&attachNum[]=" + module.itemProps.occurrAttachNum;
			}

			if (attachment["attach_method"] && attachment["attach_method"] == "5"){		// EMBEDDED MESSAGE
				var attach_message_class = attachment["attach_message_class"] ? attachment["attach_message_class"] : "IPM.Note";
				attach_message_class = attach_message_class.replace(/\./g, "_").toLowerCase();

				switch(attach_message_class) {
					case "ipm_note":
					case "ipm_post":
					case "report_ipm_note_ndr":
					case "ipm_schedule_meeting_request":
					case "ipm_schedule_meeting_resp_pos":
					case "ipm_schedule_meeting_resp_tent":
					case "ipm_schedule_meeting_resp_neg":
					case "ipm_schedule_meeting_canceled":
					case "report_ipm_note_ipnnrn":
					case "report_ipm_note_ipnrn":
						attach_message_class = "ipm_readmail";
						break;
				}
				if(attach_message_class.indexOf("ipm_") >= 0) {
					attach_message_class = attach_message_class.substring(attach_message_class.indexOf("_") + 1);
				}
				/**
				 * when an attachment items is selected or clicked,if messageAction is forwardasattachment then 
				 * embedded message is added as attachment items thus we need the entryid of the 
				 * embedded message to open it.
				 */
				if(moduleObject.messageAction == "forwardasattachment"){
					webclient.openWindow(false, attach_message_class, webclient.base_url + DIALOG_URL +"task="+ attach_message_class +"_standard&storeid=" + moduleObject.storeid + "&rootentryid=" + moduleObject.rootentryid+ "&entryid=" + attachment["entryid"]);
				}else{
					webclient.openWindow(false, attach_message_class, webclient.base_url + DIALOG_URL +"task="+ attach_message_class +"_standard&storeid=" + moduleObject.storeid + "&rootentryid=" + moduleObject.rootentryid + "&entryid=" + moduleObject.messageentryid + attachNum + "&attachNum[]=" + attach_num);
				}

			} else {
				attachNum += "&attachNum[]=" + attach_num;
				var action = webclient.base_url + "index.php?load=download_attachment&store=" + moduleObject.storeid + attachNum + "&openType=attachment";
				if(moduleObject.messageentryid) {
					action += "&entryid=" + moduleObject.messageentryid;
				}

				/**
				 * When you have to open a file that is not yet saved to a message, we have to 
				 * supply the dialog_attachments identifier string. This is only present in a dialog
				 * and must be retrieved from the <INPUT id="dialog_attachments"> HTML element.
				 * This will only worki in a dialog so we have to check whether it is available or 
				 * not.
				 */
				var dialog_attachments = dhtml.getElementById("dialog_attachments");
				if(dialog_attachments && dialog_attachments.value){
					action += "&dialog_attachments="+dialog_attachments.value;
				}

				var iframe = dhtml.getElementById("iframedownload");

				if (!iframe) iframe = dhtml.addElement(document.body, "iframe", "iframeDownload", "iframedownload");
				iframe.contentWindow.location = action;
			}
		}
	}
}

function eventDownloadAllAttachmentsAsZipArchive(moduleObject, element, event){
	
	var downloadAllAttachmentsUri = webclient.base_url + "index.php?load=download_attachment";
	downloadAllAttachmentsUri += "&store=" + moduleObject.storeid;
	downloadAllAttachmentsUri += "&openType=attachment";
	downloadAllAttachmentsUri += "&downloadType=downloadAll";
	downloadAllAttachmentsUri += (moduleObject.itemProps.subject) ? "&mailSubject=" + moduleObject.itemProps.subject.replace(/\s/g, '_').substring(0, 20) : "";

	if(moduleObject.rootentryid) {
		downloadAllAttachmentsUri += "&entryid=" + moduleObject.rootentryid;
	}

	// add all attachNum to url
	var downAttachElemOpt = dhtml.getElementById('attachments').getElementsByTagName("a");
	for(var j = 0; j < downAttachElemOpt.length; j++){
		if(downAttachElemOpt[j].getAttribute("attach_num"))
			downloadAllAttachmentsUri += "&attachNum[]=" + downAttachElemOpt[j].getAttribute("attach_num"); 
	}

	/**
	 * When you have to open a file that is not yet saved to a message, we have to 
	 * supply the dialog_attachments identifier string. This is only present in a dialog
	 * and must be retrieved from the <INPUT id="dialog_attachments"> HTML element.
	 * This will only working in a dialog so we have to check whether it is available or not.
	 */
	var dialog_attachments = dhtml.getElementById("dialog_attachments");
	if(dialog_attachments && dialog_attachments.value){
		downloadAllAttachmentsUri += "&dialog_attachments="+dialog_attachments.value;
	}

	var iframe = dhtml.getElementById("iframedownload");
	if (!iframe){
		iframe = dhtml.addElement(document.body, "iframe", "iframeDownload", "iframedownload");
	}

	iframe.contentWindow.location = downloadAllAttachmentsUri;
}
