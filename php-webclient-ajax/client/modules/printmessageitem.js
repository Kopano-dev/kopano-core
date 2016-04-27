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
* Generates Print Preview for Mail items
*/
function printMessageItem()
{
}

/**
* this function does all the creating stuff
*
*@param object moduleObject The PrintItemModule object
*@param	window frame The iframe window object where to write the print preview to.
*@param	boolean unSavedMessageFlag The flag to determine that data to be shown if from unsaved or saved.
*/
printMessageItem.prototype.init = function(moduleObject, frame, unSavedMessageFlag)
{
	// initialize data
	this.frame = frame;
	this.module = moduleObject;
	this.props = this.module.propData;
	this.doc = this.frame.document;

	this.createHTML(unSavedMessageFlag);
}
/**
 * Function which creates the HTML for print.
 *
 * @param	boolean unSavedMessageFlag The flag to determine that data to be shown if from unsaved or saved.
 */
printMessageItem.prototype.createHTML = function(unSavedMessageFlag)
{
	// set content/type
	this.doc.open("text/html");
	
	// write header
	this.doc.writeln('<html>');
	this.doc.writeln('	<head>');
	this.doc.writeln('		<title>Zarafa WebAccess - '+webclient.fullname+'</title>');
	this.doc.writeln('	</head>');

	// begin body
	this.doc.writeln('	<body>');
	this.doc.writeln('		<h1 style="font-family: sans-serif; font-size: 12pt;border-bottom: 3px solid #000;">'+webclient.fullname+'</h1>');

	// populate from/to/cc/bcc fields for name and email address
	this.populateNameFields(unSavedMessageFlag);

	// write mail properties (from/to/cc/subject etc)
	this.doc.writeln('		<table cellspacing=0 cellpadding=0 style="font-family: sans-serif; font-size: 10pt">');

	if(parentWebclient.hierarchy.defaultstore.defaultfolders.sent == this.props.parent_entryid){ // sent folder
		if(this.props.sender_name && this.props.sender_email_address && (this.props.sender_name.length >0 || this.props.sender_email_address.length > 0) && this.props.sender_email_address != this.props.sent_representing_email_address){
			this.props.from_email_combined = _("%s on behalf of %s").sprintf(nameAndEmailToString(this.props.sender_name, this.props.sender_email_address, false, true), nameAndEmailToString(this.props.sent_representing_email_address, this.props.sent_representing_email_address, false, true));
		}
	}
	if(!this.props.client_submit_time){ // drafts folder
		if(this.props.sender_email_address != this.props.sent_representing_email_address){
			this.props.from_email_combined = _("on behalf of %s").sprintf(nameAndEmailToString(this.props.sent_representing_email_address, this.props.sent_representing_email_address, false, true));
		}
	}

	this.writeLine(_("From"),"from_email_combined");
	this.writeLine(_("Sent"),"message_delivery_time");
	this.writeLine(_("To"),"to_email_combined");
	this.writeLine(_("CC"),"cc_email_combined");
	this.writeLine(_("BCC"),"bcc_email_combined");
	this.writeLine(_("Subject"),"subject");
	this.writeLine(_("Creation Time"),"creation_time","IPM.StickyNote");
	this.writeLine(_("Modified"),"last_modification_time");
	
	this.writeLine(_("Location"),"location");//appointment
	this.writeLine(_("Start Date"),"startdate");//appointment, task
	this.writeLine(_("End Date"),"duedate");//appointment, task
	this.writeLine(_("Recurrence Pattern"),"RecurrencePattern");//appointment
	
	//task
	this.writeLine("","","IPM.Task");
	if(this.props["status"]){
		switch(this.props["status"]){
			case "1":
				this.props["status"] = _("In Progress");
				break;
			case "2":
				this.props["status"] = _("Complete");
				break;
			case "3":
				this.props["status"] = _("Wait for other person");
				break;
			case "4":
				this.props["status"] = _("Deferred");
				break;
		}
		this.writeLine(_("Status"),"status");
	}
	if(this.props["percent_complete"]){
		this.props["percent_complete"] = (this.props["percent_complete"]*100)+'%';
		this.writeLine(_("Percent Complete"),"percent_complete");
		this.writeLine(_("Date completed"),"datecompleted");
	}
	this.writeLine("","","IPM.Task");
	if(this.props["totalwork"]){
		this.props["totalwork"] = Math.round(this.props["totalwork"]/6)/10+' '+_("hours");
		this.writeLine(_("Total Work"),"totalwork");
	}
	if(this.props["actualwork"]){
		this.props["actualwork"] = Math.round(this.props["actualwork"]/6)/10+' '+_("hours");
		this.writeLine(_("Actual Work"),"actualwork");
	}
	this.writeLine("","","IPM.Task");
	this.writeLine(_("Owner"),"owner");
	this.writeLine("","","IPM.Task");
	
	this.writeLine(_("Contacts"),"contacts_string");
	this.writeLine("","","IPM.Task");
	
	//contacts
	this.writeLine(_("Full Name"),"display_name");
	this.writeLine(_("Last Name"),"surname");
	this.writeLine(_("First Name"),"given_name");
	this.writeLine(_("Middle Name"),"middle_name");
	this.writeLine(_("Job Title"),"title");
	this.writeLine("","","IPM.Contact");
	this.writeLine(_("Mobile"),"cellular_telephone_number");
	this.writeLine(_("IM Address"),"im");
	this.writeLine("","","IPM.Contact");
	this.writeLine(_("Company"),"company_name");
	this.writeLine(_("Business Address"),"business_address");
	this.writeLine(_("Business")+" "+_("Phone"),"office_telephone_number");
	this.writeLine(_("Business Fax"),"business_fax_number");
	this.writeLine(_("Home address"),"home_address");
	this.writeLine(_("Home")+" "+_("Phone"),"home_telephone_number");
	this.writeLine("","","IPM.Contact");
	this.writeLine(_("E-mail"),"email_address_1");
	this.writeLine(_("Display Name"),"email_address_display_name_1");
	this.writeLine(_("E-mail")+"(2)","email_address_2");
	this.writeLine(_("Display Name"),"email_address_display_name_2");
	this.writeLine(_("E-mail")+"(3)","email_address_3");
	this.writeLine(_("Display Name"),"email_address_display_name_3");
	this.writeLine(_("Web Page"),"webpage");
		
	this.writeLine(_("Categories"),"categories");
	this.writeLine(_("Company"), "companies");
	this.writeLine(_("Billing information"), "billinginformation");
	this.writeLine(_("Mileage"), "mileage");

	// Recurrence type
	this.writeLine(_("Recurrence Type"), "recurrencetype");

	// Recurrence pattern
	this.writeLine(_("Recurrence Pattern"), "recurring_pattern");

	this.writeLine(_("Attachments"), "attachments");

	// write member list
	this.writeLine("", "", "IPM.DistList");
	this.writeMembers("IPM.DistList");

	this.doc.writeln('		</table>');
	this.doc.writeln('		<br>');

	// write mail
	this.writeBody(unSavedMessageFlag);
	this.doc.body.setAttribute("style","word-wrap:break-word");

	// write footer
	this.doc.writeln('	</body>');
	this.doc.writeln('</html>');

	this.doc.close();
}

/*
* This function makes sure that the body of the mail is written correctly. 
* In Firefox for example there are multiple textnodes that holds the 
* message, so get them all.
*
* @param	boolean unSavedMessageFlag The flag to determine that data to be shown if from unsaved or saved.
*/
printMessageItem.prototype.writeBody = function(unSavedMessageFlag)
{
	var body = this.props["body"];
	if(!unSavedMessageFlag){
		if(body && body.childNodes && body.childNodes.length > 0) {
			var content = "";
			var element = body.firstChild;
			for(var i = 0; i < body.childNodes.length; i++)
			{
				content += element.nodeValue;
				element = element.nextSibling;
			}

			if (!this.props["isHTML"] || this.props["isHTML"]=="0"){
				content = convertPlainToHtml(content);
				content = "<pre style=\"white-space: -moz-pre-wrap; white-space: -pre-wrap; white-space: -o-pre-wrap; white-space: pre-wrap; word-wrap: break-word;\">" + content + "</pre>";
			}

			this.doc.write(content);
		}
	}else{
		if(this.props["use_html"] == "true"){// (!this.props["isHTML"] || this.props["isHTML"]=="0"){
			var content = "<style> body{font-family: Arial, Verdana, Sans-Serif; font-size: 12px;padding: 5px 5px 5px 5px;margin: 0px;border-style: none;background-color: #ffffff;}"+
						  "p, ul, li{margin-top: 0px;margin-bottom: 0px;} </style>" + body;
		}else{ 
			var content = convertPlainToHtml(body);
			content = "<pre style=\"white-space: -moz-pre-wrap; white-space: -pre-wrap; white-space: -o-pre-wrap; white-space: pre-wrap; word-wrap: break-word;\">" + content + "</pre>";
		}
		this.doc.write(content);
	}
}

printMessageItem.prototype.writeLine = function(label, field, msgClass)
{
	if (this.props[field] && (!msgClass || msgClass == this.props["message_class"])){		
		this.doc.writeln('			<tr><th align="left" width=160px style="vertical-align:top">'+label+':</th><td style="padding-left: 10px">'+this.props[field].replace(/</g,"&lt;").replace(/>/g,"&gt;")+'</td></tr>');
	}
	if(label == "" && field == "" && msgClass == this.props["message_class"]){
		this.doc.writeln('			<tr><th align="left">'+NBSP+'</th><td style="padding-left: 10px">'+NBSP+'</td></tr>');
	}
}

printMessageItem.prototype.writeMembers = function(msgClass) {
	var members = this.props["members"];
	var name, address, missing;

	if(this.props["message_class"] == msgClass) {
		this.doc.writeln('<tr><th align="left">'+ _("Members") +':</th></tr>');

		if(members && members.childNodes) {
			for(var i=0; i < members.childNodes.length; i++) {
				if(members && members.firstChild) {
					var member = members.childNodes[i];

					if(member.tagName == "member") {
						name = member.getElementsByTagName("name")[0].firstChild.nodeValue;
						address = member.getElementsByTagName("address")[0].firstChild.nodeValue;
						missing = member.getElementsByTagName("missing")[0] ? member.getElementsByTagName("missing")[0].firstChild.nodeValue : 0;

						if(missing != 1) {
							this.doc.writeln('<tr><td align="left">' + name + '</td><td style="padding-left: 10px">' + address + '</td></tr>');
						}
					}
				}
			}
		}
	}
}

/**
 * Populate the display name with email in a string representing to respective fields.
 * @param	boolean unSavedMessageFlag The flag to determine that data to be shown if from unsaved or saved.
 */
printMessageItem.prototype.populateNameFields = function(unSavedMessageFlag) {
	// populate from field
	if(this.props["sent_representing_name"])
		this.props["from_email_combined"] = this.props["sent_representing_name"] + " &lt;" + this.props["sent_representing_email_address"] +"&gt;";

	// populate to/cc/bcc fields
	var tempObj = new Object();
	if(!unSavedMessageFlag){
		if(this.props.recipients && this.props.recipients.recipient && this.props.recipients.recipient.length && this.props.recipients.recipient.length>0){
			for (var i=0;i<this.props.recipients.recipient.length;i++){
				var recip = this.props.recipients.recipient[i];
				var displayName = recip["display_name"] + " &lt;" + recip["email_address"] +"&gt;";
				var type = recip["type"];
				if(typeof tempObj[type] == "undefined")
					tempObj[type] = new Array();
				/**
				 * this check is to restrict the number of emailaddress displayed to 20 
				 * in print preview as well as printing the mail.
				 */
				if(tempObj[type].length < 20)
					tempObj[type].push(displayName);
				else
					continue;
			}
		}else{
			if(this.props.recipients.recipient)
				tempObj["to"] = [this.props.recipients.recipient["display_name"] + " &lt;" + this.props.recipients.recipient["email_address"] +"&gt;"];
		}
	}else{
		/**
		 * this check is to restrict the number of emailaddress displayed in 
		 * print preview as well as printing the mail.
		 * this converts the entered emails in array and all array have a maxlength of 20
		 */
		tempObj["to"]  = this.props["to"].split(';',19);
		tempObj["cc"]  = this.props["cc"].split(';',19);
		tempObj["bcc"]  = this.props["bcc"].split(';',19);
	}

	if(tempObj["to"]){
		if(tempObj["to"].length > 19)
			/**
			 * this will display "more" text in the field denoting that there are 
			 * more emailaddress in the mail which are not displayed in this printpreview
			 */
			tempObj["to"].push(_('more')+ '...');

		this.props["to_email_combined"] = tempObj["to"].join(", ");
	}
	if(tempObj["cc"]){
		if(tempObj["cc"].length > 19)
			tempObj["cc"].push(_("more")+'...');

		this.props["cc_email_combined"] = tempObj["cc"].join(", ");
	}
	if(tempObj["bcc"]){
		if(tempObj["bcc"].length > 19)
			tempObj["bcc"].push(_("more")+'...');

		this.props["bcc_email_combined"] = tempObj["bcc"].join(", ");
	}
}