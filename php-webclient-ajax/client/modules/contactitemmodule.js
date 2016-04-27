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

contactitemmodule.prototype = new ItemModule;
contactitemmodule.prototype.constructor = contactitemmodule;
contactitemmodule.superclass = ItemModule.prototype;

function contactitemmodule(id)
{
	if(arguments.length > 0) {
		this.init(id);
	}
}

contactitemmodule.prototype.init = function(id)
{
	// display name prefixes
	var titles = new Object();
	titles["none"] = "";
	titles["dr."] = _("Dr.");
	titles["miss"] = _("Miss");
	titles["mr."] = _("Mr.");
	titles["mrs."] = _("Mrs.");
	titles["ms."] = _("Ms.");
	titles["prof."] = _("Prof.");
	this.titleOptions = titles;

	// display name suffixes
	var suffix = new Object();
	suffix["none"] = "";
	suffix["I"] = "I";
	suffix["II"] = "II";
	suffix["III"] = "III";
	suffix["jr."] = _("Jr.");
	suffix["sr."] = _("Sr.");
	this.suffixOptions = suffix;

	// address types
	var address_type = new Object();
	address_type["home"] = _("Home");
	address_type["business"] = _("Business");
	address_type["other"] = _("Other");
	this.address_type = address_type;

	// phone number types
	var phone_type = new Object();
	phone_type["assistant"] = _("Assistant");	phone_type["office"] = _("Business");
	phone_type["business2"] = _("Business 2");	phone_type["business_fax"] = _("Business Fax");
	phone_type["callback"] = _("Callback");		phone_type["car"] = _("Car");
	phone_type["company"] = _("Company");		phone_type["home"] = _("Home");
	phone_type["home2"] = _("Home 2");			phone_type["home_fax"] = _("Home Fax");
	phone_type["isdn"] = _("ISDN");				phone_type["cellular"] = _("Mobile");		
	phone_type["other"] = _("Other");			phone_type["primary_fax"] = _("Other Fax");
	phone_type["pager"] = _("Pager");			phone_type["primary"] = _("Primary");
	phone_type["radio"] = _("Radio");			phone_type["telex"] = _("Telex");
	phone_type["ttytdd"] = _("TTY/TDD");
	this.phone_type = phone_type;

	/**
	 * parsing of contact details has always been tricky work as different countries
	 * follows different formats for address, name and phone so its always hard to cover all formats
	 * so here a default contact parser object is used for normal parsing if anyone wants different parsing 
	 * then they can create a plugin that will create a child class of this class and override some methods
	 */
	// check plugins for contact parser object
	var data = {parsingObject : false};
	webclient.pluginManager.triggerHook('client.module.contactitemmodule.init.parsingObject', data);
	this.parsingObject = data["parsingObject"];

	if(!this.parsingObject) {
		// use default parser
		this.parsingObject = new ContactParsingObject();
	}
	
	contactitemmodule.superclass.init.call(this, id);
}

contactitemmodule.prototype.executeOnLoad = function()
{
	initContact();
	
	// Add keycontrol event
	webclient.inputmanager.addObject(this);
	webclient.inputmanager.bindKeyControlEvent(this, KEYS["mail"], "keyup", eventContactItemKeyCtrlSave);
}

contactitemmodule.prototype.save = function(props, dialog_attachments)
{
	var data = new Object();
	if(this.storeid) {
		data["store"] = this.storeid;
	}
	
	if(this.parententryid) {
		data["parententryid"] = this.parententryid;
	}
	
	data["props"] = props;
	data["dialog_attachments"] = dialog_attachments;
	
	if(parentWebclient) {
		parentWebclient.xmlrequest.addData(this, "save", data, webclient.modulePrefix);
		parentWebclient.xmlrequest.sendRequest(true);
	} else {
		webclient.xmlrequest.addData(this, "save", data);
		webclient.xmlrequest.sendRequest();
	}
}

/**
 * Function will parse data from html elements and give results to advprompt dialog
 * @param array data[element id] => value
 * @param string value can be full_name, address or phone_number
 * @param boolean result should be returned or not
 * @return object result[key] => value
 */
contactitemmodule.prototype.parseDetailedInfo = function (data, type, returnResult) {
	this.result = {};

	switch(type) {
		case "full_name":
			// parse full name and return an object with all fields
			if(data.length >= 1) {
				this.result = this.parsingObject.parseDetailedFullNameInfo(data);
			}

			// check that dialog should be displayed when field is incomplete or unclear
			var settingValue = webclient.settings.get("addressbook/show_detailed_name_dialog", "true");
			
			// convert true/false string type values to boolean type
			settingValue = (settingValue == "true") ? true : false;
			this.result["setting"] = settingValue;

			if(returnResult == false && this.parsingObject.incompleteInfo == true) {
				if(settingValue) {
					dhtml.executeEvent(dhtml.getElementById("fullname_button"), "click");
				}

				// we don't need to reset this flag as contactparsingobject will reset it
				this.parsingObject.incompleteInfo = false;
			}

			// store different field values to appropriate hidden variables
			this.storeDetailedInfo(this.result, type, false);
			break;
		case "phone_number":
			// parse phone number and return an object with all fields
			if(data.length >= 1) {
				this.result = this.parsingObject.parseDetailedPhoneNumberInfo(data);
			}
			break;
	}

	if(typeof returnResult != "undefined" && returnResult == true) {
		return this.result;
	}
}

/**
 * Function will get data from advprompt dialog and store it in html elements
 * @param data array data[element id] => value
 * @param type string value can be full_name, address or phone_number
 * @param show boolean info should be only stored or displayed to user
 */
contactitemmodule.prototype.storeDetailedInfo = function (data, type, show) {
	// check for missing field values
	for(var key in data){
		data[key] = typeof data[key] != "undefined" ? data[key] : "";
	}
	
	switch(type) {
		case "full_name":
			// save result to hidden variables
			dhtml.getElementById("display_name_prefix").value = data["title"];
			dhtml.getElementById("given_name").value = data["first_name"];
			dhtml.getElementById("middle_name").value = data["middle_name"];
			dhtml.getElementById("surname").value = data["last_name"];
			dhtml.getElementById("generation").value = data["suffix"];

			if(typeof show != "undefined" && show == true) {
				// set value for display name input box
				dhtml.getElementById("display_name").value = this.parsingObject.combineFullNameInfo(data);
			}

			// convert true/false boolean type values to string type to store it in settings
			webclient.settings.set("addressbook/show_detailed_name_dialog", (data["setting"] == true) ? "true" : "false");

			// set value for fileas field
			setFileAs();
			break;
		case "phone_number":
			// set value for phone number input box
			dhtml.getElementById(this.selectedPhoneInputID).value = this.parsingObject.combinePhoneNumberInfo(data);
			updatePhoneNumberFields();
			break;
		case "address":
			// check which address is selected
			address_prefix = "business";
			switch(dhtml.getElementById("selected_address").value)
			{
				case "1":
					address_prefix = "home";
					break;
				case "3":
					address_prefix = "other";
					break;
			}

			// save result to hidden variables
			dhtml.getElementById(address_prefix + "_address_street").value = data["street"];
			dhtml.getElementById(address_prefix + "_address_city").value = data["city"];
			dhtml.getElementById(address_prefix + "_address_state").value = data["state"];
			dhtml.getElementById(address_prefix + "_address_postal_code").value = data["zip"];
			dhtml.getElementById(address_prefix + "_address_country").value = data["country"];

			var address = this.parsingObject.combineAddressInfo(data);
			dhtml.getElementById(address_prefix + "_address").value = address;

			// convert true/false boolean type values to string type to store it in settings
			webclient.settings.set("addressbook/show_detailed_address_dialog", (data["setting"] == true) ? "true" : "false");
			break;
	}
}

/**
 * Function which will add the selected items as text in body of the composing contact.
 * @param object action the action tag
 */ 
contactitemmodule.prototype.setBodyFromItemData = function(action)
{
	var message = action.getElementsByTagName("item");
	//set the body of contact item
	for(var i=0;i<message.length;i++){
		this.setBody(message[i], true, false, true);
	}
	// setting focus and also setting displayname and emailaddreass for single items
	if(message.length == 1){
		var displayname = dhtml.getXMLValue(message[0], "sent_representing_name", "").toLowerCase();
		
		dhtml.getElementById("display_name").value = displayname ;
		dhtml.getElementById("email_address_display_name").value = displayname;
		dhtml.getElementById("email_address").value = dhtml.getXMLValue(message[0], "sent_representing_email_address", "").toLowerCase();
		setFileAs();

		dhtml.getElementById("html_body").focus();
	}else{
		dhtml.getElementById("display_name").focus();
	}
}

/**
 * Function will resize the view (called when dialog is resized)
 */
contactitemmodule.prototype.resize = function()
{
	var html_body = dhtml.getElementById("html_body");
	if(html_body) {
		var height = html_body.clientHeight;
		var attachFieldContainer = dhtml.getElementById("attachfieldcontainer");

		if(attachFieldContainer) {
			height -= attachFieldContainer.offsetHeight;
		}

		if(height < 50) {
			height = 50;
		}

		html_body.style.height = (height - 10) + "px";
	}
}