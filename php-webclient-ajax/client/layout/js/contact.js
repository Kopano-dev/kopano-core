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


function initContact()
{
	// Fileas
	setFileAs();
	
	// Email Address 1
	dhtml.getElementById("email_address").value = dhtml.getElementById("email_address_1").value;
	dhtml.getElementById("email_address_display_name").value = dhtml.getElementById("email_address_display_name_1").value;

	// Address
	var mailing_address = dhtml.getElementById("mailing_address").value;
	if(parseInt(mailing_address) > 0) {
		dhtml.setValue(dhtml.getElementById("checkbox_mailing_address"), true);
	}
	
	// Default: Business Address
	var addressType = "business";
	switch(mailing_address)
	{
		// Home
		case "1":
			var addressType = "home";
			break;
		// Other
		case "3":
			var addressType = "other";
			break;
	}
	
	var address_button_value = "business";
	switch(mailing_address)
	{
		case "1":
			address_button_value = "home";
			break;
		case "3":
			address_button_value = "other";
			break;
	}
	dhtml.getElementById("select_address_combo").value = address_button_value;

	dhtml.getElementById("selected_address").value = mailing_address;
	dhtml.getElementById("address_street").value = dhtml.getElementById(addressType + "_address_street").value;
	dhtml.getElementById("address_city").value = dhtml.getElementById(addressType + "_address_city").value;
	dhtml.getElementById("address_state").value = dhtml.getElementById(addressType + "_address_state").value;
	dhtml.getElementById("address_postal_code").value = dhtml.getElementById(addressType + "_address_postal_code").value;
	dhtml.getElementById("address_country").value = dhtml.getElementById(addressType + "_address_country").value;

	// set values for telephone input boxes
	dhtml.getElementById("telephone_number_1").value = dhtml.getElementById("office_telephone_number").value;
	dhtml.getElementById("telephone_number_2").value = dhtml.getElementById("home_telephone_number").value;
	dhtml.getElementById("telephone_number_3").value = dhtml.getElementById("business_fax_number").value;
	dhtml.getElementById("telephone_number_4").value = dhtml.getElementById("cellular_telephone_number").value;

	// Birthday
	dhtml.setDate(dhtml.getElementById("birthday"));
	
	// Wedding Anniversary
	dhtml.setDate(dhtml.getElementById("wedding_anniversary"));
	
	// Private
	if(dhtml.getElementById("sensitivity").value == "2") {
		dhtml.setValue(dhtml.getElementById("checkbox_private"), true);
	}
}

function submitContact(moduleObject)
{
	//A Contact item does not have the timezone property set by default so,
	var timezone = false;

	// before saving contents check for opened detailed dialogs
	for(var dialogname in webclient.dialogs) {
		if(dialogname.match("contactitem_detailfullnamedialog") != null && !window.BROWSER_IE) {
			if(typeof webclient.dialogs[dialogname].window == "undefined" || webclient.dialogs[dialogname].window.closed == false) {
				webclient.dialogs[dialogname].window.focus();
				return false;
			}
		} else if(dialogname.match("contactitem_detailphonenumberdialog") != null && !window.BROWSER_IE) {
			if(typeof webclient.dialogs[dialogname].window == "undefined" || webclient.dialogs[dialogname].window.closed == false) {
				webclient.dialogs[dialogname].window.focus();
				return false;
			}
		}
	}

	// File As
	dhtml.getElementById("fileas").value = dhtml.getValue(dhtml.getElementById("select_fileas"));
	/**
	 * this property contains fileas value format, but WA doesn't generate that value
	 * so we need to reset it so OL will not consider this property when showing fileas
	 * REF: MS_OXOCNTC 2.2.1.1.12 PidLidFileUnderId
	 */
	dhtml.getElementById("fileas_selection").value = "-1";

	// Current Email Address
	var selected_email_address = dhtml.getElementById("selected_email_address");
	var email_address = dhtml.getElementById("email_address");
	var email_address_display_name = dhtml.getElementById("email_address_display_name");

	dhtml.getElementById("email_address_" + selected_email_address.value).value = email_address.value;
	dhtml.getElementById("email_address_display_name_" + selected_email_address.value).value = email_address_display_name.value;
	
	// Current Address
	var selected_address = dhtml.getElementById("selected_address");

	data = new Object();
	data["street"] = dhtml.getElementById("address_street").value;;
	data["city"] = dhtml.getElementById("address_city").value;
	data["state"] = dhtml.getElementById("address_state").value;
	data["zip"] = dhtml.getElementById("address_postal_code").value;
	data["country"] = dhtml.getElementById("address_country").value;

	moduleObject.storeDetailedInfo(data, "address", false);

	// update phone number input fields
	updatePhoneNumberFields();

	// Mailing Address
	var mailing_address = dhtml.getElementById("mailing_address");
	var checkbox_mailing_address = dhtml.getElementById("checkbox_mailing_address");
	if(checkbox_mailing_address.checked) {
		mailing_address.value = selected_address.value;
	}
	
	// Birthday
	if(dhtml.getElementById("text_birthday").value != ""){
		dhtml.getElementById("birthday").value = Date.parseDate(dhtml.getElementById("text_birthday").value, _("%d-%m-%Y")).getTime()/1000;
	}else{
		dhtml.getElementById("birthday").value = "";
	}
	
	// Special Date
	if(dhtml.getElementById("text_wedding_anniversary").value != ""){
		dhtml.getElementById("wedding_anniversary").value = Date.parseDate(dhtml.getElementById("text_wedding_anniversary").value, _("%d-%m-%Y")).getTime()/1000;
	}else{
		dhtml.getElementById("wedding_anniversary").value = "";
	}

	// Private
	var checkbox_private = dhtml.getElementById("checkbox_private");
	if(checkbox_private.checked) {
		dhtml.getElementById("sensitivity").value = "2";
		dhtml.getElementById("private").value = "1";
	} else {
		dhtml.getElementById("sensitivity").value = "0";
		dhtml.getElementById("private").value = "-1";
	}
	
	// Contacts
	dhtml.getElementById("contacts_string").value = dhtml.getElementById("contacts").value;
	
	//check the Special Date's like birthday or Anniversary date to set the timezone properties
	if((dhtml.getElementById("birthday").value).trim() != "" || (dhtml.getElementById("wedding_anniversary").value).trim() != ""){
		timezone = getTimeZone();
	}
	submit_contact(timezone);
}

/**
 * this function is used to build fileas select box based on display name and company name properties
 * it will automatically select option that is previously selected by user or the option that is stored in message
 */
function setFileAs()
{
	//replacing space or non-breaking space with NBSP so the we can compare strings in same format
	var fileas = dhtml.getElementById("fileas").value.replace(/\xA0|\x20/g, NBSP);
	var select_fileas = dhtml.getElementById("select_fileas");
	var display_name = dhtml.getElementById("display_name");

	// store the currently selected option index that will be used to select the same option after
	// rebuilding the select box
	var tempIndex = select_fileas.options.selectedIndex;

	// Remove all options in select list
	if(select_fileas.options.length > 0) {
		for(var i = select_fileas.options.length - 1; i >= 0; i--)
		{
			select_fileas.remove(i);
		}
	}

	// Remove prefix and suffix of name.
	var name = display_name.value.replace(dhtml.getElementById("display_name_prefix").value, "").replace(dhtml.getElementById("generation").value, "").replace(",", "").replace(" ", NBSP);
	
	// Split display name ([0] => GIVENNAME, [1] => MIDDLENAME, [2] => SURNAME)
	// \u0020 => space, \u00a0 => No-break space
	var names = name.split(/\xA0|\x20/g);

	// Remove white spaces in array
	var tmpnames = names;
	names = new Array();
	for(var i = 0; i < tmpnames.length; i++)
	{
		if(tmpnames[i].length > 0 && tmpnames[i].trim() != "") {
			names.push(tmpnames[i].trim());
		}
	}

	if(names.length > 0) {
		// Build up two different options (SURNAME, GIVENNAME MIDDLENAME and GIVENNAME MIDDLENAME SURNAME)
		var givenname = names.join(NBSP);
		var surname = "";
		if(names.length > 1) {
			surname = names[names.length-1] + "," + NBSP;
			names.splice(names.length-1,1);
			surname += names.join(NBSP);
		}
		
		// Add the two options to select list
		if(surname.trim() != "")
			select_fileas.options[select_fileas.options.length] = new Option(surname, surname);
		if(givenname.trim() != "")
			select_fileas.options[select_fileas.options.length] = new Option(givenname, givenname);
	} else {
		var	surname = false;
		var givenname = false;
	}

	// Check if company name is set
	var company_name = dhtml.getElementById("company_name");
	if(company_name.value != "") {
		// Add company name to select list
		select_fileas.options[select_fileas.options.length] = new Option(company_name.value, company_name.value);

		// Add two options which contains the company name and the full name
		if(surname)
			select_fileas.options[select_fileas.options.length] = new Option(company_name.value + NBSP + "(" + surname + ")", company_name.value + NBSP + "(" + surname + ")");
		if(givenname)
			select_fileas.options[select_fileas.options.length] = new Option(company_name.value + NBSP + "(" + givenname + ")", company_name.value + NBSP + "(" + givenname + ")");
		if(surname)
			select_fileas.options[select_fileas.options.length] = new Option(surname + NBSP + "(" + company_name.value + ")", surname + NBSP + "(" + company_name.value + ")");
		if(givenname)
			select_fileas.options[select_fileas.options.length] = new Option(givenname + NBSP + "(" + company_name.value + ")", givenname + NBSP + "(" + company_name.value + ")");
	}

	if(tempIndex >= 0 && tempIndex <= select_fileas.options.length) {
		// we are rebuilding an existing select box so select previously selected option
		select_fileas.options[tempIndex].selected = true;
	} else if(tempIndex == -1 && select_fileas.options.length > -1) {
		// we are loading this contact first time so select option that is stored in message
		for(var index=0; index < select_fileas.options.length; index++) {
			if(select_fileas.options[index].value == fileas) {
				select_fileas.options[index].selected = true;
				break;
			}
		}
	}
}

function onChangeEmailAddress()
{
	var select_email_address = dhtml.getElementById("select_email_address");
	var selected_email_address = dhtml.getElementById("selected_email_address");
	var email_address = dhtml.getElementById("email_address");
	var email_address_display_name = dhtml.getElementById("email_address_display_name");
	
	dhtml.getElementById("email_address_" + selected_email_address.value).value = email_address.value;
	dhtml.getElementById("email_address_display_name_" + selected_email_address.value).value = email_address_display_name.value;
	
	selected_email_address.value = select_email_address.options[select_email_address.selectedIndex].value;

	email_address.value = dhtml.getElementById("email_address_" + selected_email_address.value).value;
	email_address_display_name.value = dhtml.getElementById("email_address_display_name_" + selected_email_address.value).value;
}

/**
 * Function is called when value of email_address_display_name is changed.
 * if email_address_x is set then don't allow email_address_display_name to set empty.
 */
function onUpdateEmailAddressDisplayName() {
	var email_address = dhtml.getElementById("email_address");
	var email_address_display_name = dhtml.getElementById("email_address_display_name");

	if(email_address_display_name.value == "" && email_address.value != "")
		onUpdateEmailAddress();
}

function onUpdateEmailAddress()
{
	var email_address = dhtml.getElementById("email_address");
	var email_address_display_name = dhtml.getElementById("email_address_display_name");
	var select_fileas = dhtml.getElementById("select_fileas");

	email_address_display_name.value = "("+email_address.value.trim()+")";
	if (select_fileas.value.trim()!=""){
		email_address_display_name.value = select_fileas.value.trim()+NBSP+email_address_display_name.value;
	}
}

function onChangeAddress(moduleObject)
{
	var selected_address = dhtml.getElementById("selected_address");
	var mailing_address = dhtml.getElementById("mailing_address");
	var checkbox_mailing_address = dhtml.getElementById("checkbox_mailing_address");

	var address_element_id = "business";
	switch(selected_address.value)
	{
		case "1":
			address_element_id = "home";
			break;
		case "3":
			address_element_id = "other";
			break;
	}

	data = new Object();
	data["street"] = dhtml.getElementById("address_street").value;;
	data["city"] = dhtml.getElementById("address_city").value;
	data["state"] = dhtml.getElementById("address_state").value;
	data["zip"] = dhtml.getElementById("address_postal_code").value;
	data["country"] = dhtml.getElementById("address_country").value;

	moduleObject.storeDetailedInfo(data, "address", false);

	if(typeof module.selectedMenuItem == "undefined") {
		var selectedMenuItem = "business";
	} else {
		var selectedMenuItem = module.selectedMenuItem;
	}

	selected_address.value = 2;
	switch(selectedMenuItem)
	{
		case "home":
			selected_address.value = 1;
			break;
		case "other":
			selected_address.value = 3;
			break;
	}

	dhtml.getElementById("address_street").value = dhtml.getElementById(selectedMenuItem + "_address_street").value;
	dhtml.getElementById("address_city").value = dhtml.getElementById(selectedMenuItem + "_address_city").value;
	dhtml.getElementById("address_state").value = dhtml.getElementById(selectedMenuItem + "_address_state").value;
	dhtml.getElementById("address_postal_code").value = dhtml.getElementById(selectedMenuItem + "_address_postal_code").value;
	dhtml.getElementById("address_country").value = dhtml.getElementById(selectedMenuItem + "_address_country").value;

	if(selected_address.value == mailing_address.value) {
		dhtml.setValue(checkbox_mailing_address, true);
	} else {
		dhtml.setValue(checkbox_mailing_address, false);
	}
}

function onChangeMailingAddress(element)
{
	var mailing_address = dhtml.getElementById("mailing_address");
	var selected_address = dhtml.getElementById("selected_address");

	if(element.checked) {
		mailing_address.value = selected_address.value;
	} else {
		mailing_address.value = 0;
	}
}

function eventContactItemSendMailTo()
{
	var toValue = dhtml.getElementById("display_name").value+" <"+dhtml.getElementById("email_address").value+">";
	webclient.openWindow(this, "createmail", DIALOG_URL+"task=createmail_standard&to="+encodeURIComponent(toValue));
}

function categoriesCallBack(categories)
{
	dhtml.getElementById("categories").value = categories;
}

function abCallBack(recipients)
{
	dhtml.getElementById("contacts").value = recipients["contacts"].value;
}

/* functions for detailed contacts */

/**
 * Function will create an advprompt dialog for detailed fullname
 * @param object moduleObject
 */
function eventShowDetailFullNameDialog(moduleObject) {
	if(!moduleObject.contactDialogOpen)
		return false;
	/*
	 * IE doesn't return webclient.dialogs[dialogname].window object so it will be always undefined,
	 * but since this fix is only needed for FF we have to remove this checking in IE
	 * in FF
	 * if user first time opens detailed dialog then window object will be undefined, in this case allow to show detailed dialog
	 * if user has opened and closed detailed dialog then window object will be defined so we have to check property window.closed for actual state of the dialog
	 * if detailed dialog is already open then window.closed will be false so don't show new detailed dialog
	 */
 	// check dialog is already opened or not (only for FF)
	for(var dialogname in webclient.dialogs) {
		if(dialogname.match("contactitem_detailfullnamedialog") != null && !window.BROWSER_IE) {
			if(typeof webclient.dialogs[dialogname].window == "undefined" || webclient.dialogs[dialogname].window.closed == false) {
				webclient.dialogs[dialogname].window.focus();
				return false;
			}
		}
	}

	var result = moduleObject.parseDetailedInfo(dhtml.getElementById("display_name").value, "full_name", true);
	var titles = new Object();
	var suffix = new Object();

	for(var key in moduleObject.titleOptions) {
		// if title is given then select it in select box
		if(typeof result["title"] != "undefined" && result["title"].toLowerCase() == moduleObject.titleOptions[key].toLowerCase()) {
			titles[key] = new Object();
			titles[key]["selected"] = true;
			titles[key]["text"] = moduleObject.titleOptions[key];
		} else {
			titles[key] = moduleObject.titleOptions[key];
		}
	}

	for(var key in moduleObject.suffixOptions) {
		// if suffix is given then select it in select box
		if(typeof result["suffix"] != "undefined" && result["suffix"].toLowerCase() == moduleObject.suffixOptions[key].toLowerCase()) {
			suffix[key] = new Object();
			suffix[key]["selected"] = true;
			suffix[key]["text"] = moduleObject.suffixOptions[key];
		} else {
			suffix[key] = moduleObject.suffixOptions[key];
		}
	}

	webclient.openModalDialog(moduleObject.id, 'contactitem_detailfullnamedialog', DIALOG_URL+'task=advprompt_modal', 325, 300, contactitemDetailfullname_dialog_callback, {
			moduleObject: moduleObject
		}, {
			windowname: _("Check Full Name"),
			dialogtitle: _("Name details"),
			fields: [{
				name: "title",
				label: _("Title") + ":",
				type: "select",
				required: false,
				value: titles
			},
			{
				name: "first_name",
				label: _("First") + ":",
				type: "input",
				required: false,
				value: result["first_name"]
			},
			{
				name: "middle_name",
				label: _("Middle") + ":",
				type: "input",
				required: false,
				value: result["middle_name"]
			},
			{
				name: "last_name",
				label: _("Last") + ":",
				type: "input",
				required: false,
				value: result["last_name"]
			},
			{
				name: "suffix",
				label: _("Suffix") + ":",
				type: "select",
				required: false,
				value: suffix
			},
			{
				name: "setting",
				label: _("Show this again when name is incomplete or unclear"),
				type: "checkbox",
				required: false,
				value: result["setting"]
			}]
		}
	);
}

/**
 * Callback Function to store details of fullname entered in advprompt dialog
 * @param array result[element id] => value
 * @param object callbackdata
 */
function contactitemDetailfullname_dialog_callback(result, callbackData) {
	if(callbackData.moduleObject){
		callbackData.moduleObject.storeDetailedInfo(result, "full_name", true);
	}
}

/**
 * Function will create an advprompt dialog for detailed phone numbers
 * @param object moduleObject
 * @param htmlElement element on which event executed
 */
function eventShowDetailPhoneNumberDialog(moduleObject, element) {
	// store id of phone input box on which operation should be perfomed
	moduleObject.selectedPhoneInputID = element.id;
	moduleObject.selectedPhoneInputID = moduleObject.selectedPhoneInputID.replace("button", "number");

	var result = moduleObject.parseDetailedInfo(dhtml.getElementById(moduleObject.selectedPhoneInputID).value, "phone_number", true);

	webclient.openModalDialog(moduleObject.id, 'contactitem_detailphonenumberdialog', DIALOG_URL+'task=advprompt_modal', 325,250, contactitemDetailphonenumber_dialog_callback, {
			moduleObject: moduleObject
		}, {
			windowname: _("Check Phone Number"),
			dialogtitle: _("Phone details"),
			fields: [{
				// @TODO make select box
				name: "country_code",
				label: _("Country/Region Code") + ":",
				type: "input",
				required: false,
				value: result["country_code"]
			},
			{
				name: "city_code",
				label: _("City/Area code") + ":",
				type: "input",
				required: false,
				value: result["city_code"]
			},
			{
				name: "local_number",
				label: _("Local number") + ":",
				type: "input",
				required: false,
				value: result["local_number"]
			},
			{
				name: "extension",
				label: _("Extension") + ":",
				type: "input",
				required: false,
				value: result["extension"]
			}]
		}
	);
}

/**
 * Callback Function to store details of phone numbers entered in advprompt dialog
 * @param array result[element id] => value
 * @param object callbackdata
 */
function contactitemDetailphonenumber_dialog_callback(result, callbackData) {
	if(callbackData.moduleObject){
		callbackData.moduleObject.storeDetailedInfo(result, "phone_number", true);
	}
}

/**
 * Function will show phone selection menu
 * @param object moduleObject
 * @param htmlElement element which called this function
 * @param event event information
 */
function eventShowPhoneTypeMenu(moduleObject, element, event) {
	var items = new Array();
	var selected = "not_selected";

	for(var key in moduleObject.phone_type) {
		if(key != "isdn" && key.indexOf("_fax") == -1 && dhtml.getElementById(key + "_telephone_number").value != "") {
			// if phone number then suffix is "_telephone_number"
			selected = "selected";
		} else if((key == "isdn" || key.indexOf("_fax") != -1) && dhtml.getElementById(key + "_number").value != "") {
			// if fax number or isdn number then suffix is "_number"
			selected = "selected";
		} else {
			selected = "not_selected";
		}

		items.push(webclient.menu.createMenuItem(selected + " " + key, moduleObject.phone_type[key], false, eventOnChangePhoneType, false, false, key));
	}

	// show context menu
	webclient.menu.buildContextMenu(moduleObject.id, element.id, items, element.offsetLeft + element.offsetWidth, element.offsetTop);

	// store id of element which initiated menu
	element = element.previousSibling;
	moduleObject.selectedPhoneElementID = element.id.slice(element.id.lastIndexOf("_") + 1); // value can be 1, 2, 3, 4
}

/**
 * Function will be executed when you select a phone type from menu
 * @param object moduleObject
 * @param htmlElement element which called this function
 * @param event event information
 */
function eventOnChangePhoneType(moduleObject, element, event) {
	var phoneElementID = moduleObject.selectedPhoneElementID;

	// retrieve values of phone number from hidden input fields
	if(element["data"] != "isdn" && dhtml.getElementById(element["data"] + "_telephone_number") != null) {
		dhtml.getElementById("telephone_number_" + phoneElementID).value = dhtml.getElementById(element["data"] + "_telephone_number").value;
	} else {
		dhtml.getElementById("telephone_number_" + phoneElementID).value = dhtml.getElementById(element["data"] + "_number").value;
	}

	// name of the selected phone type is stored in data variable of element
	dhtml.getElementById("selected_phone_" + phoneElementID).value = element["data"];

	dhtml.getElementById("telephone_button_" + phoneElementID).value = element.innerHTML + ":";

	// set focus to the phone input box
	dhtml.getElementById("telephone_number_" + phoneElementID).focus();

	// remove context menu
	if(dhtml.getElementById("contextmenu")) {
		dhtml.deleteElement(dhtml.getElementById("contextmenu"));
	}
}

/**
 * Function will be executed when you select an address type from menu
 * @param object moduleObject
 * @param htmlElement element which called this function
 * @param event event information
 */
function eventOnChangeAddressType(moduleObject, element, event) {
	// name of the selected address type is stored in data variable of element
	moduleObject.selectedMenuItem = dhtml.getElementById("select_address_combo").value;

	onChangeAddress(moduleObject);

	// set focus to the address textarea
	dhtml.getElementById("address_street").focus();
}

/**
 * Function will update prefix & suffix according to change in display name
 * and vice versa
 * called on "onchange" event of display name, prefix and suffix
 */
function updateDisplayName() {
	// save result of advprompt dialog to hidden variables
	var display_name = "";
	var display_name_prefix = dhtml.getElementById("display_name_prefix").value;
	var given_name = dhtml.getElementById("given_name").value;
	var middle_name = dhtml.getElementById("middle_name").value;
	var surname = dhtml.getElementById("surname").value;
	var generation = dhtml.getElementById("generation").value;

	display_name = (display_name_prefix != "") ? display_name_prefix + NBSP : "";
	display_name += (given_name != "") ? given_name + NBSP : "";
	display_name += (middle_name != "") ? middle_name + NBSP : "";
	display_name += (surname != "") ? surname + NBSP : "";
	display_name += (generation != "") ? generation : "";

	// set value for display name input box
	dhtml.getElementById("display_name").value = display_name;

	// set value for fileas field
	setFileAs();
}

/**
 * Function will update hidden input boxes with values entered in phone input boxes
 */
function updatePhoneNumberFields() {
	var selected_phone, telephone_number;

	// store phone numbers to hidden input fields
	for(var i = 1; i <= 4; i++) {
		selected_phone = dhtml.getElementById("selected_phone_" + i).value;
		telephone_number = dhtml.getElementById("telephone_number_" + i).value;

		if(dhtml.getElementById(selected_phone + "_telephone_number") != null) {
			dhtml.getElementById(selected_phone + "_telephone_number").value = telephone_number;
		} else {
			dhtml.getElementById(selected_phone + "_number").value = telephone_number;
		}
	}

	/**
	 * display changes done in phone numbers fields to user,
	 * so user will be aware if he overwrites any value
	 */
	for(var i = 1; i <= 4; i++) {
		selected_phone = dhtml.getElementById("selected_phone_" + i).value;

		if(dhtml.getElementById(selected_phone + "_telephone_number") != null) {
			dhtml.getElementById("telephone_number_" + i).value = dhtml.getElementById(selected_phone + "_telephone_number").value;
		} else if(dhtml.getElementById(selected_phone + "_number") != null) {
			dhtml.getElementById("telephone_number_" + i).value = dhtml.getElementById(selected_phone + "_number").value;
		}
	}
}
/**
 * Keycontrol function which saves contact.
 */
function eventContactItemKeyCtrlSave(moduleObject, element, event, keys)
{
	switch(event.keyCombination)
	{
		case keys["save"]:
			submitContact(moduleObject);
			break;
	}
}

/**
 * Function will check wheather the date in inputField is a valid Date or not.Thus will display 
 * an alert message for invalid Date
 */
function eventDatePickerInputChange()
{
	var oldValue = dhtml.getElementById("text_birthday").value.trim();
	/**
	 * Split date string in to sub parts to get value
	 * German language  ->		dd.mm.yyyy
	 * English US language ->	dd/mm/yyyy
	 * Other languages  ->		dd-mm-yyyy
	 */
	var valueArr = oldValue.split(/[-.//]/);

	if(valueArr.length > 0 && valueArr[0] != ""){	
		var result = checkDateValidation(valueArr[0], valueArr[1], valueArr[2]);
		if(result){
			dhtml.getElementById("text_birthday").value = oldValue;
		}else{
			alert(_("You must specify a valid date"));
			dhtml.getElementById("text_birthday").value = '';
		}
	}
}