<?php
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

?>
<?php

function getJavaScript_onload(){
?>
/**
 * Required input is 
 * window.windowData = {
 * 	windowname: "##NAME##",
 * 	dialogtitle: "##TITLE##",
 * 	fields: [
 * 		{
 * 			name: "##NAME##",
 * 			label: "##LABEL##",
 * 			type: "(normal|email)",
 * 			required: (true|false),
 * 			value: ""
 * 		},
 * 		{
 * 			name: "##NAME##",
 * 			label_start: "##LABEL##",
 *			label_end: "##LABEL##",
 * 			type: "(combineddatetime)",
 * 			required: (true|false),
 * 			value_start: "",
 * 			value_end: ""
 * 		},
 * 	]
 * };
 *
 * field type can be normal, email, combineddatetime, select, checkbox
 */
if(window.windowData && window.windowData.windowname){
	document.title = window.windowData.windowname;
}

if(window.windowData && window.windowData.dialogtitle){
	// this will overwrite title set by getDialogTitle() method
	dhtml.getElementById("windowtitle").innerHTML = window.windowData.dialogtitle;
}

var fieldsTable = dhtml.getElementById("fields", "table");
if(fieldsTable && window.windowData && typeof window.windowData.fields == "object"){
	var addedFocus = false;
	for(var i in window.windowData.fields){
		var fieldData = window.windowData.fields[i];
		var row = fieldsTable.insertRow(fieldsTable.rows.length);

		if(window.windowData.fields[i].type == "combineddatetime"){
			var thCell = dhtml.addElement(row, "th", null, null, fieldData.label_start);
			var tdCell = dhtml.addElement(row, "td", null, fieldData.id_start);
			var row2 = fieldsTable.insertRow(fieldsTable.rows.length);
			var thCell2 = dhtml.addElement(row2, "th", null, null, fieldData.label_end);
			var tdCell2 = dhtml.addElement(row2, "td", null, fieldData.id_end);

			var appoint_start = new DateTimePicker(tdCell, "");
			var appoint_end = new DateTimePicker(tdCell2, "");

			// set start & end datetime values
			if(fieldData.value_start && fieldData.value_end) {
				appoint_start.setValue(fieldData.value_start);
				appoint_end.setValue(fieldData.value_end);
			}

			var appoint_dtp = new combinedDateTimePicker(appoint_start,appoint_end);
			window.windowData.fields[i].combinedDateTimePicker = appoint_dtp;

		}else if(window.windowData.fields[i].type == "textarea"){
			var thCell = dhtml.addElement(row, "th", null, null, fieldData.label);
			var tdCell = dhtml.addElement(row, "td");

			var input = dhtml.addElement(tdCell, "textarea", "text", fieldData.name);
			input.value = (fieldData.value)?fieldData.value:"";
			if(!addedFocus){
				input.focus();
				addedFocus = true;
			}

		}else if(window.windowData.fields[i].type == "select"){
			var thCell = dhtml.addElement(row, "th", null, null, fieldData.label);
			var tdCell = dhtml.addElement(row, "td");

			var selectbox = dhtml.addElement(tdCell, "select", "text", fieldData.name);
			var index = 0;

			if(typeof fieldData.value == "object") {
				for(var key in fieldData.value) {
					if(typeof fieldData.value[key] == "object" && fieldData.value[key]["selected"] == true) {
						selectbox.options[index++] = new Option(fieldData.value[key]["text"], key, true, true);
					} else {
						selectbox.options[index++] = new Option(fieldData.value[key], key, false);
					}
				}
			}
			if(!addedFocus){
				selectbox.focus();
				addedFocus = true;
			}
			window.windowData.fields[i].selectBox = selectbox;

		}else if(window.windowData.fields[i].type == "checkbox"){
			var thCell = dhtml.addElement(row, "th");
			thCell.setAttribute("colSpan", 2);

			var checkbox = dhtml.addElement(null, "input", "text", fieldData.name);
			checkbox.setAttribute("type", "checkbox");
			// add element in DOM after setting all attributes,
			// otherwise IE doesn't support changing type attribute after adding checkbox to DOM
			thCell.appendChild(checkbox);

			var label = dhtml.addElement(thCell, "a", null, null, fieldData.label);

			if(typeof fieldData.value != "undefined") {
				if(fieldData.value == true) {
					checkbox.checked = true;
				}
			}
			if(!addedFocus){
				checkbox.focus();
				addedFocus = true;
			}
			window.windowData.fields[i].checkBox = checkbox;

		}else{
			var thCell = dhtml.addElement(row, "th", null, null, fieldData.label);
			var tdCell = dhtml.addElement(row, "td");

			var input = dhtml.addElement(tdCell, "input", "text", fieldData.name);
			input.value = (fieldData.value)?fieldData.value:"";
			if(!addedFocus){
				input.focus();
				addedFocus = true;
			}
		}
	}

}

<?php
}

function getDialogTitle(){
	return _("Input");
}

function getIncludes() {
	$includes = array(
		"client/layout/js/advprompt.js",
		"client/widgets/datetimepicker.js",
		"client/widgets/combineddatetimepicker.js",
		"client/layout/js/date-picker.js",
		"client/layout/js/date-picker-language.js",
		"client/layout/js/date-picker-setup.js",
		"client/layout/css/date-picker.css",
		"client/widgets/datepicker.js",
		"client/widgets/timepicker.js"
	);
	return $includes;
}

function getBody() { ?>
	<table id="fields"></table>
	<?=createConfirmButtons("if(advPromptSubmit()) window.close();")?>
<?php } // getBody
?>
