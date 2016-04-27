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

var parentModule;

function initFieldChooser()
{
	var dialogname = window.name;
	if(!dialogname) {
		dialogname = window.dialogArguments.dialogName;
	}

	parentModule = window.windowData.parentModule;
	var propertylist = parentModule.propertylist;

	var columns = dhtml.getElementById("columns");
	var selected_columns = dhtml.getElementById("selected_columns");

	var properties = new Array();
	var selected_properties = new Array();
	
	for(var i = 0; i < propertylist.length; i++)
	{
		var property = propertylist[i];
		
		if(property["name"]) {
			if(property["visible"]) {
				selected_properties.push(property);
			} else {
				properties.push(property);			
			}
		}
	}
	
	properties.sort(sortColumns);
	selected_properties.sort(orderColumns);
	
	for(var i = 0; i < properties.length; i++)
	{
		columns.options[columns.options.length] = new Option(properties[i]["name"], properties[i]["id"]);
	}
	
	for(var i = 0; i < selected_properties.length; i++)
	{
		selected_columns.options[selected_columns.options.length] = new Option(selected_properties[i]["name"], selected_properties[i]["id"]);		
	}
}

function submitFields()
{
	var properties = new Array();
	var columns = dhtml.getElementById("columns");
	var selected_columns = dhtml.getElementById("selected_columns");
	
	for(var i = 0; i < selected_columns.options.length; i++)
	{
		var property = new Object();
		property["id"] = selected_columns.options[i].value;
		property["action"] = "add";
		property["order"] = "" + i;
		properties.push(property);
	}
	
	for(var i = 0; i < columns.options.length; i++)
	{
		var property = new Object();
		property["id"] = columns.options[i].value;
		property["action"] = "delete";
		properties.push(property);
	}

	window.resultCallBack(properties, window.callBackData);
}

function addColumn()
{
	var columns = dhtml.getElementById("columns");
	var selected_columns = dhtml.getElementById("selected_columns");

	if(columns.selectedIndex >= 0) {
		var previewPanePosition = webclient.settings.get("global/previewpane", "right");
		previewPanePosition = webclient.settings.get("folders/entryid_"+parentModule.entryid+"/previewpane", previewPanePosition);

		if( parentModule.previewPane && previewPanePosition == "right" ){
			if(selected_columns.options.length == 2 ){
				this.addColumnForRightPane(columns,selected_columns);
				this.deleteColumnForRightPane(columns,selected_columns);
			}else{
				this.addColumnForRightPane(columns,selected_columns);
			}
		}else{
			var selected_index = columns.selectedIndex;
			var option = columns.options[selected_index];
			selected_columns.options[selected_columns.options.length] = new Option(option.text, option.value);
			columns.remove(selected_index);
		
			// Select the added column in 'selected_columns'
			selected_columns.options[selected_columns.options.length - 1].selected = true;
			
			// Select the next or previous option in 'columns'
			var next_option = selected_index - 1;
			if(selected_index < columns.options.length) { 
				next_option++;	
			}

			if(next_option < columns.options.length && next_option >= 0) {
				columns.options[next_option].selected = true;
			}
		}
	}
}

function deleteColumn()
{
	var columns = dhtml.getElementById("columns");
	var selected_columns = dhtml.getElementById("selected_columns");

	if(selected_columns.selectedIndex >= 0) {
		var previewPanePosition = webclient.settings.get("global/previewpane", "right");
		previewPanePosition = webclient.settings.get("folders/entryid_"+parentModule.entryid+"/previewpane", previewPanePosition);

		if( parentModule.previewPane && previewPanePosition == "right" ){
			this.deleteColumnForRightPane(columns,selected_columns);
		}else{
			var selected_index = selected_columns.selectedIndex;
			var option = selected_columns.options[selected_index];
			var selected_value = option.value;
			columns.options[columns.options.length] = new Option(option.text, option.value);
			
			selected_columns.remove(selected_columns.selectedIndex);

			// Select the next or previous option in 'selected_columns'
			var next_option = selected_index - 1;
			if(selected_index < selected_columns.options.length) { 
				next_option++;	
			}

			if(next_option < selected_columns.options.length && next_option >= 0) {
				selected_columns.options[next_option].selected = true;
			}
			
			// Select the deleted column in 'selected_columns' in 'columns'
			sortAvailableFields(selected_value);
		}
	}
}

function columnUp()
{
	var selected_columns = dhtml.getElementById("selected_columns");
	
	if(selected_columns.selectedIndex >= 0) {
		var previous_option_index = selected_columns.selectedIndex - 1;
		
		if(previous_option_index >= 0) {
			var previous_option = selected_columns.options[previous_option_index];
			var selected_option = selected_columns.options[selected_columns.selectedIndex];

			selected_columns.options[previous_option_index] = new Option(selected_option.text, selected_option.value);
			selected_columns.options[selected_columns.selectedIndex] = new Option(previous_option.text, previous_option.value);
			
			selected_columns.options[previous_option_index].selected = true;
		}
	}
}

function columnDown()
{
	var selected_columns = dhtml.getElementById("selected_columns");
	
	if(selected_columns.selectedIndex >= 0) {
		var next_option_index = selected_columns.selectedIndex + 1;
		
		if(next_option_index < selected_columns.options.length) {
			var next_option = selected_columns.options[next_option_index];
			var selected_option = selected_columns.options[selected_columns.selectedIndex];

			selected_columns.options[next_option_index] = new Option(selected_option.text, selected_option.value);
			selected_columns.options[selected_columns.selectedIndex] = new Option(next_option.text, next_option.value);
			
			selected_columns.options[next_option_index].selected = true;
		}
	}
}

function sortAvailableFields(selectedId)
{
	var columns = dhtml.getElementById("columns");
	var properties = new Array();
	
	for(var i = columns.options.length - 1; i >= 0; i--)
	{
		var property = new Object();
		property["id"] = columns.options[i].value;
		property["name"] = columns.options[i].text;
		properties.push(property);
		columns.remove(i);
	} 

	properties.sort(sortColumns);

	for(var i = 0; i < properties.length; i++)
	{
		columns.options[columns.options.length] = new Option(properties[i]["name"], properties[i]["id"]);
		
		if(properties[i]["id"] == selectedId) {
			columns.options[columns.options.length - 1].selected = true;
		}
	}
}

function orderColumns(a, b)
{
	if(a["order"] > b["order"]) return 1;
	if(a["order"] < b["order"]) return -1;
	return 0;
}

function sortColumns(a, b)
{
	if(a["name"] > b["name"]) return 1;
	if(a["name"] < b["name"]) return -1;
	return 0;
}

//to add fields when right preview pane in use
function addColumnForRightPane(columns, selected_columns)
{
	var remove_index = new Array();
	var selected_index = columns.selectedIndex;
	var option = columns.options[selected_index];

	switch(option.value){
		case ("display_to"):
		case ("client_submit_time"):
			for(var i = 0; i < columns.options.length; i++){
				if(columns.options[i].value == "client_submit_time" || columns.options[i].value == "display_to"){
					selected_columns.options[selected_columns.options.length] = new Option(columns.options[i].text, columns.options[i].value);
					remove_index.push(i);
				}
			}
			break;
			
		case ("sent_representing_name"):
		case ("message_delivery_time"):
			for(var i = 0; i < columns.options.length; i++){
				if(columns.options[i].value == "sent_representing_name" || columns.options[i].value == "message_delivery_time"){
					selected_columns.options[selected_columns.options.length] = new Option(columns.options[i].text, columns.options[i].value);
					remove_index.push(i);
				}
			}
			break;
	}

	//remove items
	for(var j = remove_index.length - 1; j >= 0; j--){
		columns.remove(remove_index[j]);
	}

	// Select the added column in 'selected_columns'
	if(selected_columns.options.length > 2){
		selected_columns.options[0].selected = true;
	}else{
		selected_columns.options[selected_columns.options.length - 2].selected = true;
	}

	// Select the next or previous option in 'columns'
	if(columns.options.length > 0) {
		columns.options[0].selected = true;
	}


}

//to delete fields when right preview pane in use
function deleteColumnForRightPane(columns, selected_columns)
{
	var remove_index = new Array();
	var selected_index = selected_columns.selectedIndex;
	var option = selected_columns.options[selected_index];

	switch(option.value){
		case ("display_to"):
		case ("client_submit_time"):
			for(var i = 0; i < selected_columns.options.length; i++){
				if(selected_columns.options[i].value == "client_submit_time" || selected_columns.options[i].value == "display_to"){
					columns.options[columns.options.length] = new Option(selected_columns.options[i].text, selected_columns.options[i].value);
					remove_index.push(i);
				}
			}
			break;
		case ("sent_representing_name"):
		case ("message_delivery_time"):
			for(var i = 0; i < selected_columns.options.length; i++){
				if(selected_columns.options[i].value == "sent_representing_name" || selected_columns.options[i].value == "message_delivery_time"){
					columns.options[columns.options.length] = new Option(selected_columns.options[i].text, selected_columns.options[i].value);
					remove_index.push(i);
				}
			}
			break;
	}

	//remove items
	for(var j = remove_index.length - 1; j >= 0; j--){
		selected_columns.remove(remove_index[j]);
	}

	// Select the next or previous option in 'selected_columns'
	if(selected_columns.options.length > 0) {
		selected_columns.options[0].selected = true;
	}

	// Select the deleted column in 'selected_columns' in 'columns'
	sortAvailableFields(option.value);
}