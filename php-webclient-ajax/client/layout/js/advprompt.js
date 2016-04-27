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

function advPromptSubmit() {
	if(window.windowData && typeof window.windowData.fields == "object"){
		var result = new Array();
		for(var i in window.windowData.fields){
			var fieldData = window.windowData.fields[i];

			switch(fieldData.type){
				case "textarea":
					var field = dhtml.getElementById(fieldData.name, "textarea");
					if(field){
						// Check for required field
						if(fieldData.required && (field.value).trim().length == 0){
							alert(_("Please fill in field \"%s\"").replace(/%s/g,fieldData.label));
							field.focus();
							return false;
						}
						result[fieldData.name] = (field.value).trim();
					// Field not found
					}else{
						return false;
					}

				case "combineddatetime":
					if(fieldData.combinedDateTimePicker){
						result[fieldData.name] = {
							start: fieldData.combinedDateTimePicker.getStartValue(),
							end: fieldData.combinedDateTimePicker.getEndValue()
						};
					}
					break;
				case "select":
					if(fieldData.selectBox) {
						if(typeof fieldData.selectBox.value == "undefined") {
							result[fieldData.name] = "";
						} else {
							result[fieldData.name] = fieldData.selectBox.options[fieldData.selectBox.selectedIndex].text;
						}
					}
					break;
				case "checkbox":
					if(fieldData.checkBox) {
						if(fieldData.checkBox.checked == true) {
							result[fieldData.name] = true;
						} else {
							result[fieldData.name] = false;
						}
					}
					break;
				case "normal":
				case "email":
				default:
					var field = dhtml.getElementById(fieldData.name, "input");
					if(field){
						// Check for required field
						if(fieldData.required && (field.value).trim().length == 0){
							alert(_("Please fill in field \"%s\"").replace(/%s/g,fieldData.label));
							field.focus();
							return false;
						}
						// Check for validation of field
						switch(fieldData.type){
							case "email":
								if(!validateEmailAddress((field.value).trim(), true)){
									// Alert is done by validateEmailAddress when it fails
									field.focus();
									return false;
								}
								break;
						}
						result[fieldData.name] = (field.value).trim();
					// Field not found
					}else{
						return false;
					}
					break;
			}

		}
		// This point is only reached when all checks are passed
		window.resultCallBack(result, window.callBackData);
		return true;
	// Cannot find window.windowData or window.windowData.field is not an array
	}else{
		return false;
	}
}
