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
 * This object is used to parse / combine detailed information received from detailed dialog boxes
 * this object does not stores / retrieves information it just operates on passed data
 */
function ContactParsingObject()
{
	this.incompleteInfo = false;
}

/**
 * Function will parse data and return an object with different fields
 * @param array data[element id] => value
 * @return object result[key] => value
 */
ContactParsingObject.prototype.parseDetailedFullNameInfo = function (data) {
	// reset variable
	this.incompleteInfo = false;

	// initialize data, so we will not get undefined anywhere
	// default value of properties will be empty string
	var result = {
		'title' : '',
		'first_name' : '',
		'middle_name' : '',
		'last_name' : '',
		'suffix' : ''
	};
	var displayNameParts = [];
	var firstElement, lastElement;

	// split display name ([0] => title, [1] => first_name, [2] => middle_name,
	//						[3] => last_name, [4] => suffix)
	displayNameParts = data.split(/\xA0|\x20/g); // \u0020 => space, \u00a0 => No-break space

	// Remove white spaces in array
	var tmpParts = displayNameParts;
	displayNameParts = [];
	for(var key in tmpParts)
	{
		if(tmpParts[key].length > 0 && tmpParts[key].trim() != "") {
			displayNameParts.push(tmpParts[key].trim());
		}
	}

	switch(displayNameParts.length) {
		case 5:
			result["title"] = displayNameParts.shift();
			result["first_name"] = displayNameParts.shift();
			result["middle_name"] = displayNameParts.shift();
			result["last_name"] = displayNameParts.shift();
			result["suffix"] = displayNameParts.shift();
			break;
		case 4:
			// search for display name prefix in name
			firstElement = displayNameParts.shift();
			for(var key in module.titleOptions) {
				if(module.titleOptions[key].toLowerCase() == firstElement.toLowerCase()) {
					result["title"] = firstElement;
					break;
				} else {
					result["title"] = "";
				}
			}

			// search for suffix in name
			lastElement = displayNameParts.pop();
			for(var key in module.suffixOptions) {
				if(module.suffixOptions[key].toLowerCase() == lastElement.toLowerCase()) {
					result["suffix"] = lastElement;
					break;
				} else {
					result["suffix"] = "";
				}
			}

			result["first_name"] = ( result["title"] != "" ) ? displayNameParts.shift() : firstElement;
			result["last_name"] = ( result["suffix"] != "" ) ? displayNameParts.pop() : lastElement;
			result["middle_name"] = ( displayNameParts.length > 0 ) ? displayNameParts.shift() : "";
			break;
		case 3:
			// search for display name prefix in name
			firstElement = displayNameParts.shift();
			for(var key in module.titleOptions) {
				if(module.titleOptions[key].toLowerCase() == firstElement.toLowerCase()) {
					result["title"] = firstElement;
					break;
				} else {
					result["title"] = "";
				}
			}

			// search for suffix in name
			lastElement = displayNameParts.pop();
			for(var key in module.suffixOptions) {
				if(module.suffixOptions[key].toLowerCase() == lastElement.toLowerCase()) {
					result["suffix"] = lastElement;
					break;
				} else {
					result["suffix"] = "";
				}
			}

			if(result["title"] == "" && result["suffix"] == "") {
				result["first_name"] = firstElement;
				result["middle_name"] = displayNameParts.shift();
				result["last_name"] = lastElement;
			} else if(result["title"] != "" && result["suffix"] == "") {
				result["first_name"] = displayNameParts.shift();
				result["middle_name"] = "";
				result["last_name"] = lastElement;
			} else if(result["title"] == "" && result["suffix"] != "") {
				result["first_name"] = firstElement;
				result["middle_name"] = "";
				result["last_name"] = displayNameParts.shift();
			}
			break;
		case 2:
			// search for display name prefix in name
			firstElement = displayNameParts.shift();
			for(var key in module.titleOptions) {
				if(module.titleOptions[key].toLowerCase() == firstElement.toLowerCase()) {
					result["title"] = firstElement;
					break;
				} else {
					result["title"] = "";
				}
			}

			// search for suffix in name
			lastElement = displayNameParts.pop();
			for(var key in module.suffixOptions) {
				if(module.suffixOptions[key].toLowerCase() == lastElement.toLowerCase()) {
					result["suffix"] = lastElement;
					break;
				} else {
					result["suffix"] = "";
				}
			}

			if(result["title"] == "" && result["suffix"] == "") {
				result["first_name"] = firstElement;
				result["last_name"] = lastElement;
			} else if(result["title"] == "" && result["suffix"] != "") {
				result["last_name"] = firstElement;
				this.incompleteInfo = true; // information entered is incomplete or unclear
			} else if(result["title"] != "" && result["suffix"] == "") {
				result["last_name"] = lastElement;
				this.incompleteInfo = true; // information entered is incomplete or unclear
			}
			break;
		case 1:
			result["first_name"] = displayNameParts.shift();
			this.incompleteInfo = true; // information entered is incomplete or unclear
			break;
		default:
			result["title"] = displayNameParts.shift();
			result["first_name"] = displayNameParts.shift();
			result["suffix"] = displayNameParts.pop();
			result["last_name"] = displayNameParts.pop();
			result["middle_name"] = displayNameParts.join(NBSP);
			break;
	}

	return result;
}

/**
 * Function will parse data and return an object with different fields
 * @param array data[element id] => value
 * @return object result[key] => value
 * This function is not useful anymore as we are not using parser for address.
 */
ContactParsingObject.prototype.parseDetailedAddressInfo = function (data) {
	// reset variable
	this.incompleteInfo = false;

	// initialize data, so we will not get undefined anywhere
	// default value of properties will be empty string
	var result = {
		'street' : '',
		'country' : '',
		'state' : '',
		'city' : '',
		'zip' : ''
	};
	var addressParts = [];
	var singleLine;

	// split address ([0] => street, [1] => city state zip, [2] => country)
	addressParts = data.split(/\x0D|\x0A|\x0D\x0A/g); // \u000D => '\r' or carriage return
													  // \u000A => '\n' or new line

	// Remove white spaces in array
	var tmpParts = addressParts;
	addressParts = [];
	for(var key in tmpParts)
	{
		if(tmpParts[key].length > 0 && tmpParts[key].trim() != "") {
			addressParts.push(tmpParts[key].trim());
		}
	}

	switch(addressParts.length) {
		case 3:
			result["street"] = addressParts.shift();
			result["country"] = addressParts.pop();
			singleLine = addressParts.shift();
			break;
		case 2:
			result["street"] = addressParts.shift();
			singleLine = addressParts.shift();
			this.incompleteInfo = true; // information entered is incomplete or unclear
			break;
		case 1:
			singleLine = addressParts.shift();
			this.incompleteInfo = true; // information entered is incomplete or unclear
			break;
		default:
			result["country"] = addressParts.pop();
			singleLine = addressParts.pop();
			result["street"] = addressParts.join(NBSP);
			break;
	}

	if(typeof singleLine != "undefined" && singleLine.length >= 1) {
		// split address ([0] => city, [1] => state, [2] => zip)
		addressParts = singleLine.split(/\xA0|\x20/g);

		// Remove white spaces in array
		var tmpParts = addressParts;
		addressParts = [];
		for(var key in tmpParts)
		{
			if(tmpParts[key].length > 0 && tmpParts[key].trim() != "") {
				addressParts.push(tmpParts[key].trim());
			}
		}

		switch(addressParts.length) {
			case 3:
				if(!isNaN(parseInt(addressParts[addressParts.length - 1], 10))) {
					result["zip"] = addressParts.pop();
				}
				result["state"] = addressParts.pop();
				result["city"] = addressParts.join(NBSP);
				break;
			case 2:
				result["city"] = addressParts.shift();
				result["state"] = addressParts.shift();
				break;
			case 1:
				result["city"] = addressParts.shift();
				break;
			default:
				if(!isNaN(parseInt(addressParts[addressParts.length - 1], 10))) {
					result["zip"] = addressParts.pop();
				}
				result["state"] = addressParts.pop();
				result["city"] = addressParts.join(NBSP);
		}
	}

	return result;
}

/**
 * Function will parse data and return an object with different fields
 * @param array data[element id] => value
 * @return object result[key] => value
 */
ContactParsingObject.prototype.parseDetailedPhoneNumberInfo = function (data) {
	// reset variable
	this.incompleteInfo = false;

	// initialize data, so we will not get undefined anywhere
	// default value of properties will be empty string
	var result = {
		'country_code' : '',
		'city_code' : '',
		'local_number' : '',
		'extension' : ''
	};
	var phoneNumberParts = [];
	var tmpElement;

	// check for extension number
	if(data.indexOf("x") != -1) {
		result["extension"] = data.slice(data.indexOf("x"));
		// remove extension from phone number
		data = data.replace(result["extension"], "");

		// remove x character from extension
		result["extension"] = result["extension"].replace("x", "").trim();
	} else if(data.indexOf("/") != -1) {
		result["extension"] = data.slice(data.indexOf("/"));
		// remove extension from phone number
		data = data.replace(result["extension"], "");

		// remove / character from extension
		result["extension"] = result["extension"].replace("/", "").trim();
	}

	// split phone number ([0] => country_code, [1] => area_code, [2] => local_number)
	phoneNumberParts = data.split(/\xA0|\x20/g); // \u0020 => space, \u00a0 => No-break space

	// Remove white spaces in array
	var tmpParts = phoneNumberParts;
	phoneNumberParts = [];
	for(var key in tmpParts)
	{
		if(tmpParts[key].length > 0 && tmpParts[key].trim() != "") {
			phoneNumberParts.push(tmpParts[key].trim());
		}
	}

	switch(phoneNumberParts.length) {
		case 3:
			result["country_code"] = phoneNumberParts.shift();
			result["city_code"] = phoneNumberParts.shift().replace(/\(|\)/g, "");	// remove brackets from area code
			result["local_number"] = phoneNumberParts.shift();
			break;
		case 2:
			tmpElement = phoneNumberParts.shift();
			result["country_code"] = (tmpElement.indexOf("+") != -1) ? tmpElement : "";
			result["city_code"] = (result["country_code"] == "") ? tmpElement.replace(/\(|\)/g, "") : "";
			result["local_number"] = phoneNumberParts.shift();
			break;
		case 1:
			result["local_number"] = phoneNumberParts.shift();
			break;
		default:
			result["country_code"] = phoneNumberParts.shift();
			result["city_code"] = phoneNumberParts.shift().replace(/\(|\)/g, "");	// remove brackets from area code
			result["local_number"] = phoneNumberParts.join(NBSP);
			break;
	}

	return result;
}

/**
 * Function is used to combine the stored detailed phone number info into a string that can be presented to the user.
 * @param data array array of detailed info of phone number
 * @return string string of phone number which is created by combining different detailed informations
 */
ContactParsingObject.prototype.combinePhoneNumberInfo = function (data) {
	var phone_number = "";

	// check for invalid data
	if(typeof data != "object") {
		return phone_number;
	}

	phone_number = (data["country_code"] != "") ? data["country_code"] + NBSP : "";
	phone_number += (data["city_code"] != "") ? "(" + data["city_code"] + ")" + NBSP : "";
	phone_number += (data["local_number"] != "") ? data["local_number"] + NBSP : "";
	phone_number += (data["extension"] != "") ? "x" + NBSP + data["extension"] : "";

	return phone_number.trim();
}

/**
 * Function is used to combine the stored detailed address info into a string that can be presented to the user.
 * @param data array array of detailed info of address
 * @return string string of address which is created by combining different detailed informations
 */
ContactParsingObject.prototype.combineAddressInfo = function (data) {
	var address = "";

	// check for invalid data
	if(typeof data != "object") {
		return address;
	}

	address = (data["street"] != "") ? data["street"] + CRLF : "";
	address += (data["city"] != "") ? data["city"] + NBSP : "";
	address += (data["state"] != "") ? data["state"] + NBSP : "";
	address += (data["zip"] != "") ? data["zip"] + CRLF : "";
	address += (data["country"] != "") ? data["country"] : "";

	return address.trim();
}

/**
 * Function is used to combine the stored detailed full name info into a string that can be presented to the user.
 * @param data array array of detailed info of full name
 * @return string string of name which is created by combining different detailed informations
 */
ContactParsingObject.prototype.combineFullNameInfo = function (data) {
	var display_name = "";

	// check for invalid data
	if(typeof data != "object") {
		return display_name;
	}

	display_name = (data["title"] != "") ? data["title"] + NBSP : "";
	display_name += (data["first_name"] != "") ? data["first_name"] + NBSP : "";
	display_name += (data["middle_name"] != "") ? data["middle_name"] + NBSP : "";
	display_name += (data["last_name"] != "") ? data["last_name"] + NBSP : "";
	display_name += (data["suffix"] != "") ? data["suffix"] : "";

	return display_name.trim();
}