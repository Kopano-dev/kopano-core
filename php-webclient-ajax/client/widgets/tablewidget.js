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
* Table widget - generates a table
*/

TableWidget.prototype = new Widget;
TableWidget.prototype.constructor = TableWidget;
TableWidget.superclass = Widget.prototype;

function TableWidget(element, multipleSelect, windowObj, noLoader)
{
	this.init(element, multipleSelect, windowObj, noLoader);
}

//TODO: order mapping> this.sortorder = [0=>rowID, 1=>rowID]

TableWidget.prototype.init = function(element, multipleSelect, windowObj, noLoader)
{
	TableWidget.superclass.init(this);

	this.element = element;
	this.windowObj = windowObj || false;
	this.columns = new Array();
	this.data = new Object();
	//this.addColumn("scrollbar", "", 16, 99999);
	this.htmlrefs = new Object();
	this.htmlrefs["headertable"] = null;
	this.htmlrefs["contenttable"] = null;
	this.selected = new Object();
	this.numSelected = 0;
	this.multipleSelect = (typeof multipleSelect != "undefined")? Boolean(multipleSelect) : true;
	this.noLoader = (typeof noLoader != "undefined")? Boolean(noLoader) : false;
	this.prevSelectedMessageID = false;

	this.columnResizeListeners = new Array();
	this.eventRowListeners = new Array();

	this.sortColumn = false;
	this.sortDirection = false;

	if(!this.noLoader) {
		// Show loader
		this.showLoader();
	}


//	// Prevent IE scrolling the pane when user does up/down/pgup/etc
//	dhtml.addEvent(this, this.divElement, "keydown", this.onDivKeyDown);

	// add keyboard event
	//var module = webclient.getModule(this.moduleID);
	webclient.inputmanager.addObject(this, this.element);
	webclient.inputmanager.bindEvent(this, "keydown", eventTableWidgetKeyboard);

}

TableWidget.prototype.destructor = function(widgetObject)
{
	// remove inputmanager binding
	webclient.inputmanager.removeObject(this);

	// reset contents of widget
	this.resetWidget();

	TableWidget.superclass.destructor(this);
}

/**
 * Function will reset all contents of table widget, this function is created to
 * reuse table widget object, when data is changed in table widget, so we don't
 * have to create new table widget object everytime when results are changed
 * and also we can empty table widget data
 */
TableWidget.prototype.resetWidget = function()
{
	// remove events registered with elements
	dhtml.removeEvents(this.element);

	// remove elements
	dhtml.deleteAllChildren(this.element);

	// reinitialize variables
	this.columns = new Array();
	this.data = new Object();
	this.htmlrefs["headertable"] = null;
	this.htmlrefs["contenttable"] = null;
	this.selected = new Object();
	this.numSelected = 0;
	this.eventRowListeners = new Array();
	this.eventColumnListeners = new Array();
	this.columnResizeListeners = new Array();
	this.prevSelectedMessageID = false;
}

TableWidget.prototype.addColumn = function(id, columnName, columnWidth, columnOrder){
/**
 * COLUMNDATA:
 * - name
 * - width (abs, rel)
 * - sort (none, asc, desc)
 * - 
::types
static width (nonresizable)
fixed width (resizable)
variabel width (resizable)

When resizing column take width from all other variable columns

FEATURES:
different label for mouseover text
escaping innerHTML, id, etc for double quotes
Adding classnames for rows/cells when adding data

 */
	this.columns.push({
		id: id,
		name: columnName,
		type: 1,
		order: columnOrder,
		resizable: 1,
		width: columnWidth
	});

	this.columns = this.columns.sort(function(a, b){
		return a['order'] - b['order'];
	});
}

TableWidget.prototype.generateTable = function(data, sort){
	if(!this.noLoader) {
		// Hide loader
		this.hideLoader();
	}
	//get the sorting info from listmodule to set the sort in tableWidget
	if(sort){
		this.sortColumn = sort[0]["_content"];
		this.sortDirection = sort[0]["attributes"]["direction"];
	}

	if(this.element){
		// First clear the old table if this is not the initial generation
		this.htmlrefs["headertable"] = null;
		this.htmlrefs["contenttable"] = null;
		if(this.element.childNodes.length > 0){
			while(this.element.childNodes.length){
				this.element.removeChild(this.element.childNodes.item(0));
			}
		}

		if(typeof data != "undefined"){
			// Clear existing data
			this.clearData();
			// First store the data in the table widget
			for(var key in data){
				this.addData(data[key]);
			}
		}

		var availableWidth = this.element.offsetWidth;
		/*
		 * When the element is not visible it is not given an offsetHeight. When this happens the 
		 * list will not set a height and the vertical scrollbar will not show when the list is 
		 * longer that can be shown in the available height.
		 */
		var height = this.element.offsetHeight || parseInt(this.element.style.height,10);
		var availableContentHeight = height - 25;	// Minus header table
		this.recalcColumnWidths(availableWidth);

		var content = "";

		content += '<div style="height: 100%;" id="tablewidget['+this.widgetID+'][container]" class="tableview">';
		content += '<div id="tablewidget['+this.widgetID+'][headercontainer]" class="columnbackground">';

		content += '<table border="0" cellpadding="0" cellspacing="0" style="width: 100%;"><tbody><tr><td>';
		content += '<table border="0" cellpadding="0" cellspacing="0" style="width: 100%;" id="tablewidget['+this.widgetID+'][headertable]"><tbody>';
		content += '<tr class="columns">';

//		content += '<td id="property_icon_index" class="column message_icon icon_icon_index" title="Sort On Icon" width="25">';
//		content += '<span class="column_seperator">&nbsp;</span>&nbsp;';
//		content += '</td>';
//		content += '<td id="property_complete" class="column message_icon icon_complete" title="Sort On Complete" width="25">';
//			content += '<span class="column_seperator">&nbsp;</span>&nbsp;';
//		content += '</td>';
//		content += '<td id="property_importance" class="column message_icon icon_importance" title="Sort On Priority">';
//			content += '<span class="column_seperator">&nbsp;</span>&nbsp;';
//		content += '</td>';
//		content += '<td id="property_subject" class="column" title="Sort On Subject">';
//			content += '<span class="column_seperator">&nbsp;</span>Subject';
//		content += '</td>';
//		content += '<td id="property_duedate" class="column" title="Sort On Due Date" width="150">';
//			content += '<span class="column_seperator">&nbsp;</span>Due Date';
//		content += '</td>';
//		content += '<td id="property_owner" class="column" title="Sort On Owner" width="150">';
//			content += '<span class="column_seperator">&nbsp;</span>Owner';
//		content += '</td>';
//		content += '<td id="property_percent_complete" class="column" title="Sort On Percent Completed" width="100">';
//			content += '<span class="column_seperator">&nbsp;</span>% Completed';
//		content += '</td>';

		for(var i=0;i<this.columns.length;i++){
			content += '<td id="property_'+this.columns[i]["id"]+'" columnID="'+this.columns[i]["id"]+'" '+
                            'class="column tablewiget_column_icon icon_'+ this.columns[i]["id"] +
                                    (i == this.columns.length - 1 ? ' tablewidget_header_lastcolumn' : '') + '" '+
                            'title="'+this.columns[i]['name']+'" ';
			if(parseInt(this.widths[ this.columns[i]["id"] ],10) > 0){
				content += ' style="width:'+this.widths[ this.columns[i]['id'] ]+'px;" ';
			}
			content += '>';
			content += '<span class="column_seperator">&nbsp;</span>'+this.columns[i]['name']+'';
			// add sort icon to column 
			if(this.columns[i]["id"] == this.sortColumn){
				content += "&nbsp;<span class='sort_" + this.sortDirection + "'>&nbsp;</span>";
			}
			content += '</td>';
		}

		content += '</tr></tbody></table>';

		content += '</div>';

		content += '<div id="tablewidget['+this.widgetID+'][contentcontainer]" style="width: 100%; height:'+availableContentHeight+'px; overflow-x: hidden; overflow-y: auto; position: relative;">';


		content += '<table  style="width:100%;" id="tablewidget['+this.widgetID+'][contenttable]" class="table" border="0" cellpadding="0" cellspacing="0"><tbody>';
		
		var firstRow = true;
		for(var key in this.data){
			content += this.generateRowColumnsHTML(this.data[key], firstRow);
			firstRow = false;
		}
		
		content += '</tbody></table>';

		content += '</div></div>';

		//contentElement.innerHTML += content;
		this.element.innerHTML += content;

		this.htmlrefs["headertable"] = dhtml.getElementById("tablewidget["+this.widgetID+"][headertable]", "table", this.element);
		this.htmlrefs["contenttable"] = dhtml.getElementById("tablewidget["+this.widgetID+"][contenttable]", "table", this.element);

		for(var i=0;i<this.htmlrefs["contenttable"].rows.length;i++){
			this.htmlrefs["contenttable"].rows[i].rowID = this.htmlrefs["contenttable"].rows[i].getAttribute("rowID") || "";
			dhtml.addEvent(this, this.htmlrefs["contenttable"].rows[i], "mousedown", eventTableWidgetClickRow, this.windowObj);
			dhtml.addEvent(this, this.htmlrefs["contenttable"].rows[i], "dblclick", eventTableWidgetDblClickRow, this.windowObj);
			dhtml.addEvent(this, this.htmlrefs["contenttable"].rows[i], "contextmenu", eventTableWidgetRowContextMenu, this.windowObj);
		}
	
		if(this.htmlrefs["headertable"]){
			var columnRow = this.htmlrefs["headertable"].rows[0];
			if(columnRow) {
				for(var i = 0; i < columnRow.cells.length - 1; i++)
				{
					dhtml.addEvent(this, columnRow.cells[i], "click", eventTableWidgetColumnSort, this.windowObj);
				}
			}
		}
	}
}

TableWidget.prototype.recalcColumnWidths = function(availableWidth){
	this.widths = new Object();
	var variableColumns = new Array();
	//variableColumns = 

	availableWidth = availableWidth - 16;
	this.widths["scrollbar"] = 16;

	// First calculate the columns with a width
	for(var i=0;i<this.columns.length;i++){
		if(this.columns[i]['width']){
			availableWidth = availableWidth - parseInt(this.columns[i]['width'] ,10);
			this.widths[ this.columns[i]['id'] ] = parseInt(this.columns[i]['width'] ,10);
		}else{
			variableColumns.push(this.columns[i]);
		}
	}
	// Then calculate the variable columns
	for(var i=0;i<variableColumns.length;i++){
		//availableWidth = availableWidth - parseInt(variableColumns[i]['width'] ,10);
		/**
		 * Calculate the width of this column by deviding the available width 
		 * over the remaining columns.
		 */
		this.widths[ variableColumns[i]['id'] ] = Math.floor( availableWidth / variableColumns.length );
	}
}


TableWidget.prototype.generateRowColumnsHTML = function(rowData, addWidthData){
	var content = "";
	//content += '<tr group="folder" id="0" class="row ipm_task" messageflags="9"><td class="rowcolumn message_icon icon_task" width="25">&nbsp;</td><td class="rowcolumn" width="25"><input type="checkbox"></td><td class="rowcolumn message_icon">&nbsp;</td><td class="rowcolumn"><div class="rowcolumntext">Submit travel expenses okt 2007</div></td><td class="rowcolumn" width="150"><div class="rowcolumntext">&nbsp;</div></td><td class="rowcolumn" width="150"><div class="rowcolumntext">&nbsp;</div></td><td class="rowcolumn" width="100"><div class="rowcolumntext">NaN%</div></td></tr>';

	//content += '<tr group="folder" id="0" class="row ipm_task" messageflags="9">';
	content += '<tr class="row" id="tablewidget_crow['+this.widgetID+']['+rowData["rowID"]+']" rowID="'+rowData["rowID"]+'">';
	for(var i=0;i<this.columns.length;i++){
		if(rowData[ this.columns[i]["id"] ]){
			// Allowing custom css class to be set on the columns
			var cellClassnames = rowData[ this.columns[i]["id"] ]['css'] || "";
			if(rowData[ this.columns[i]["id"] ]['innerHTML']){

				var columnWidth = "";
				if(addWidthData && parseInt(this.widths[ this.columns[i]["id"] ],10) > 0){
					columnWidth = ' style="width:'+this.widths[ this.columns[i]['id'] ]+'px;" ';
				}

				content += '<td class="rowcolumn '+cellClassnames+'" '+columnWidth+' columnID="'+this.columns[i]["id"]+'">'+rowData[ this.columns[i]["id"] ]['innerHTML']+'</td>';
			}else{
				/**
				 * if there is no data in a cell for first row then also we have to give
				 * width to that cell, otherwise it will break layout
				 */
				var columnWidth = "";
				if(addWidthData && parseInt(this.widths[ this.columns[i]["id"] ],10) > 0){
					columnWidth = ' style="width:'+this.widths[ this.columns[i]['id'] ]+'px;" ';
				}

				content += '<td class="rowcolumn '+cellClassnames+'" '+columnWidth+' columnID="'+this.columns[i]["id"]+'"></td>';
			}
		}else{
			content += '<td></td>';
		}
	}
	content += '</tr>';
	return content;
	/**
COLUMN TYPES
module can insert data or innerHTML
rowData[columnID] = 
	innerHTML = ""

	*/
}

TableWidget.prototype.addRowHTML = function(rowData, insertAt){
	if(this.htmlrefs["contenttable"]){
		if(insertAt < 0 || typeof insertAt == "undefined"){
			var insertAt = this.htmlrefs["contenttable"].rows.length;
		}

		if(insertAt <= this.htmlrefs["contenttable"].rows.length){
			// Create new row by creating a completely new table
			var container = dhtml.addElement(false, "div");
			/**
			 * When the row has to be positioned as the first row (insertAt==0) 
			 * the width data has to be added.
			 */
			container.innerHTML = "<table>" + this.generateRowColumnsHTML(rowData, (insertAt == 0)) + "</table>";
			if(container.childNodes.length > 0 && container.childNodes.item(0).rows){
				var newRow = container.childNodes.item(0).rows[0].cloneNode(true);

				// Add events to row
				newRow.rowID = newRow.getAttribute("rowID") || "";
				dhtml.addEvent(this, newRow, "mousedown", eventTableWidgetClickRow, this.windowObj);
				dhtml.addEvent(this, newRow, "dblclick", eventTableWidgetDblClickRow, this.windowObj);
				dhtml.addEvent(this, newRow, "contextmenu", eventTableWidgetRowContextMenu, this.windowObj);

				// Get reference to current first row so we can strip it of it width data
				if(insertAt == 0){
					var oldFirstRow = this.htmlrefs["contenttable"].rows[0];
				}

				// Insert new row
				var newEmptyRow = this.htmlrefs["contenttable"].insertRow(insertAt);
				newEmptyRow.parentNode.replaceChild(newRow, newEmptyRow);

	//TODO: fix removing old width settings
				// Clear old first row of width settings
				if(oldFirstRow){
					for(var i=0;i<oldFirstRow.cells.length;i++){
						oldFirstRow.cells[i].style.width = "";
					}
				}
			}
		}
	}else{
		return false;
	}
}

// This method returns either false on failure or otherwise the integer of the position (Watch out this can be 0!)
TableWidget.prototype.deleteRowHTML = function(rowID){
	if(typeof rowID != "undefined" && this.htmlrefs["contenttable"]){
		for(var i=0;i<this.htmlrefs["contenttable"].rows.length;i++){
			if(rowID == this.htmlrefs["contenttable"].rows[i].getAttribute("rowID")){
				this.htmlrefs["contenttable"].deleteRow(i);
				return i;
			}
		}
		return false;
	}else{
		return false;
	}
}


// TODO: recalculate height
TableWidget.prototype.resize = function(){
	// Resize header table
	var headerTbl = this.htmlrefs["headertable"];
	if(headerTbl){
		this.recalcColumnWidths(this.element.offsetWidth);
		for(var i=0;i<headerTbl.rows.length;i++){
			for(var j=0;j<headerTbl.rows[i].cells.length;j++){
				var columnID = headerTbl.rows[i].cells[j].getAttribute("columnID");
				if(columnID){
					if(this.widths[columnID]){
						headerTbl.rows[i].cells[j].style.width = parseInt(this.widths[columnID],10)+"px";
					}
				}
			}
		}
	}
	// Resize content table
	var contentTbl = this.htmlrefs["contenttable"];
	if(contentTbl && contentTbl.rows.length > 0){
		this.recalcColumnWidths(this.element.offsetWidth);
		for(var i=0;i<contentTbl.rows[0].cells.length;i++){
			var columnID = contentTbl.rows[0].cells[i].getAttribute("columnID");
			if(columnID){
				if(this.widths[columnID]){
					contentTbl.rows[0].cells[i].style.width = parseInt(this.widths[columnID],10)+"px";
				}
			}
		}
	}
	// Resize vertically
	var headerContainer = dhtml.getElementById("tablewidget["+this.widgetID+"][headercontainer]", "div", this.element);
	var contentContainer = dhtml.getElementById("tablewidget["+this.widgetID+"][contentcontainer]", "div", this.element);
	if(contentContainer != null) {
		var contentContainerHeight = this.element.offsetHeight - headerTbl.offsetHeight;
		if(contentContainerHeight > 0) {
			contentContainer.style.height = contentContainerHeight + "px";
		}
	}
}

TableWidget.prototype.getColumnData = function(columnID){
	for(var i=0;i<this.columns.length;i++){
		if(this.columns[i]['id'] == columnID){
			return this.columns[i];
		}
	}
	return false;
}


/**
 * Data methods
 */
TableWidget.prototype.clearData = function(){
	this.data = new Object();
}
TableWidget.prototype.addData = function(data){
	var uniqueRowID = data["rowID"] || false;
	// If no rowID has been supplied generate one...
	if(!uniqueRowID){
		uniqueRowID = Math.ceil(Math.random()*1000000);
	// ...otherwise use the supplied rowID.
	}else{
		/**
		 * Make sure no illegal characters will get 
		 * into the ID attribute of the HTML element.
		 */
		uniqueRowID = (new String(uniqueRowID)).replace(/[^A-Za-z0-9_\- ]./g, "");
	}
	while(this.data[uniqueRowID]){
		uniqueRowID+="a";
	}
	this.data[uniqueRowID] = data;
	// Make sure the rowID is set in the data object
	this.data[uniqueRowID]["rowID"] = uniqueRowID;
	return uniqueRowID;
}
TableWidget.prototype.updateData = function(rowID, data){
	if(this.data[ rowID ]){
		if(typeof data != "undefined"){
			for(var col in data){
				if(col != rowID){
					this.data[ rowID ][ col ] = data[ col ];
				}
			}
		}
		return true;
	}else{
		data["rowID"] = rowID;
		this.addData(data);
	}
}
TableWidget.prototype.deleteData = function(rowID){
	if(this.data[ rowID ]){
		delete this.data[ rowID ];
		return true;
	}else{
		return false;
	}
}
TableWidget.prototype.getDataByRowID = function(rowID){
	if(this.data[rowID]){
		return this.data[rowID];
	}else{
		return false;
	}
}


/**
 * ROW methods
 */
TableWidget.prototype.getRowCount = function(){
	if(this.htmlrefs["contenttable"]){
		return this.htmlrefs["contenttable"].rows.length;
	}else{
		return 0;
	}
}
TableWidget.prototype.getRowByRowID = function(rowID){
	var row = dhtml.getElementById("tablewidget_crow["+this.widgetID+"]["+rowID+"]", "tr", this.htmlrefs["contenttable"]);
	return row || false;
}
TableWidget.prototype.prevRowID = function(rowID){
	var row = this.getRowByRowID(rowID);
	if(row && row.previousSibling && row.previousSibling.getAttribute){
		return row.previousSibling.getAttribute("rowID") || false;
	}else{
		return false;
	}
}
TableWidget.prototype.nextRowID = function(rowID){
	var row = this.getRowByRowID(rowID);
	if(row && row.nextSibling && row.nextSibling.getAttribute){
		return row.nextSibling.getAttribute("rowID") || false;
	}else{
		return false;
	}
}
TableWidget.prototype.addRow = function(data, insertAt){
	var rowID = this.addData(data);
	this.addRowHTML(this.data[ rowID ], insertAt);
	return rowID;
}
TableWidget.prototype.updateRow = function(rowID, data){
	this.updateData(rowID, data);
	var fullData = this.getDataByRowID(rowID);
	// Only replace row when data is available
	if(fullData){
		var position = this.deleteRowHTML(rowID);
		this.addRowHTML(fullData, position);
	}
	return fullData["rowID"];
}
TableWidget.prototype.deleteRow = function(rowID){
	var nextRowID = this.nextRowID(rowID);
	var position = this.deleteRowHTML(rowID);
	/**
	 * Since only the first row has the width data we need to reset the first 
	 * row, but only if there are any other rows left
	 */
	if(position === 0 && nextRowID){
		this.updateRow(nextRowID);
	}
	this.deleteData(rowID);
	return rowID;
}


/**
 * Selection methods
 */
TableWidget.prototype.clearSelection = function(resetPrevRowID){
	if(typeof resetPrevRowID == "undefined" || resetPrevRowID == null) {
		resetPrevRowID = true;
	}

	var deselected = new Array();
	for(var i in this.selected){
		var row = this.getRowByRowID(this.selected[i]);
		if(row){
			dhtml.removeClassName(row, "rowselected");
		}
		deselected.push(this.selected[i]);
	}
	this.selected = new Object();
	this.numSelected = 0;

	if(resetPrevRowID)
		this.prevSelectedMessageID = false;
	
	// Notify external systems of update
	if(deselected.length > 0)
		this.callRowListener("deselect", {change: deselected});
}
TableWidget.prototype.deselectRow = function(rowID, resetPrevRowID){
	var deselected = new Array();
	if(typeof rowID != "object")
		rowID = [rowID];

	if(typeof resetPrevRowID == "undefined" || resetPrevRowID == null) {
		resetPrevRowID = true;
	}

	for(var i in rowID){
		if(this.selected[rowID[i]]){
			var row = this.getRowByRowID(rowID[i]);
			if(row){
				delete this.selected[rowID[i]];
				this.numSelected--;
				dhtml.removeClassName(row, "rowselected");
				deselected.push(rowID[i]);

				if(this.prevSelectedMessageID == rowID[i] && resetPrevRowID) {
					this.prevSelectedMessageID = false;
				}
			}
		}
	}
	// Notify external systems of update
	if(deselected.length > 0){
		this.callRowListener("deselect", {change: deselected});
	}
}
TableWidget.prototype.selectRow = function(rowID, adjustScroller, overWritePrevRowID){
	var selected = new Array();
	if(typeof rowID != "object")
		rowID = [rowID];
	
	if(typeof adjustScroller == "undefined" || adjustScroller == null) {
		adjustScroller = false;
	}

	if(typeof overWritePrevRowID == "undefined" || overWritePrevRowID == null) {
		overWritePrevRowID = true;
	}

	for(var i in rowID){
		if(!this.selected[rowID[i]]){
			var row = this.getRowByRowID(rowID[i]);
			if(row){
				this.selected[rowID[i]] = rowID[i];
				this.numSelected++;
				dhtml.addClassName(row, "rowselected");
				selected.push(rowID[i]);

				if(overWritePrevRowID)
					this.prevSelectedMessageID = rowID[i];
			}
		}
	}

	// change scrollbar position to the selected row
	if(adjustScroller !== false && row !== false) {
		var contentContainer = dhtml.getElementById("tablewidget["+this.widgetID+"][contentcontainer]", "div", this.element);
		if(contentContainer != null) {
			contentContainer.scrollTop = row.offsetTop;
		}
	}

	// Notify external systems of update
	if(selected.length > 0){
		this.callRowListener("select", {change: selected});
	}
}
TableWidget.prototype.getSelectedRowID = function(iterator){
	if(iterator >= 0 && iterator < this.getNumSelectedRows()){
		for(var i in this.selected){
			if(iterator == 0){
				return this.selected[i];
			}
			iterator--;
		}
	}
	return false;
}
TableWidget.prototype.getNumSelectedRows = function(){
	return this.numSelected;
}
TableWidget.prototype.getSelectedRowData = function(){
	var returnData = new Array();
	for(var i=0;i<this.getNumSelectedRows();i++){
		returnData.push(this.getDataByRowID(this.getSelectedRowID(i)));
	}
	return returnData;
}
TableWidget.prototype.isRowIDSelected = function(rowID){
	if(this.selected[rowID])
		return true;
	else
		return false;
}


/**
 * RowListener methods
 */
TableWidget.prototype.addRowListener = function(func, type, scope){
	var listenerObj = new Object();
	listenerObj["type"] = type || "all";
	listenerObj["func"] = func;
	listenerObj["scope"] = scope || this;
	this.eventRowListeners.push(listenerObj);
}

/**
  * Checks to see if this object has any listeners for a specified row event
  * @param {String} eventName The name of the event to check for
  * @return {Boolean} True if the event is being listened for, else false
  */
TableWidget.prototype.hasRowListener = function(eventName){
	for(var i in this.eventRowListeners){
		if(this.eventRowListeners[i]['type'] == eventName.toLowerCase()){
			return typeof this.eventRowListeners[i]['func'] == 'function';
		}else{
			return false;
		}
	}
}

TableWidget.prototype.callRowListener = function(type, data){
	var args = new Array();
	args[0] = this;			// ref to table widget
	switch(type){
		case "select":
			args[1] = "select";				// type
			args[2] = this.selected;		// array with selected rowIDs
			args[3] = data.change || false;	// array with newly selected rowIDs
			break;
		case "deselect":
			args[1] = "deselect";			// type
			args[2] = this.selected;		// array with selected rowIDs
			args[3] = data.change || false;	// array with deselected rowIDs
			break;
		case "dblclick":
			args[1] = "dblclick";			// type
			args[2] = data.rowID || false;	// rowID of double clicked row
			args[3] = data.event || false;	// event object
			break;
		case "contextmenu":
			args[1] = "contextmenu";		// type
			args[2] = data.rowID || false;	// rowID of selected row
			args[3] = data.event || false;	// event object
			break;
	}
	for(var i in this.eventRowListeners){
		if(this.eventRowListeners[i]['type'] == 'all' || this.eventRowListeners[i]['type'] == type){
			try {
				this.eventRowListeners[i]["func"].apply(this.eventRowListeners[i]["scope"], args);
			}
			catch (e){
				alert(e);
			}
		}
	}
}

/**
 * Function which adds listener to columns
 * @param		object		func		event which will listen to the event fires
 * @param		String		type		type of event registered
 * @param		String		scope		scope for the event 
 */
TableWidget.prototype.addColumnListener = function(func, type, scope){
	var listenerObj = new Object();
	listenerObj["type"] = type || "all";
	listenerObj["func"] = func;
	listenerObj["scope"] = scope || this;
	this.eventColumnListeners.push(listenerObj);
}

 /**
  * Checks to see if this object has any listeners for a specified column event
  * @param {String} eventName The name of the event to check for
  * @return {Boolean} True if the event is being listened for, else false
  */
TableWidget.prototype.hasColumnListener = function(eventName){
	for(var i in this.eventColumnListeners){
		if(this.eventColumnListeners[i]['type'] == eventName.toLowerCase()){
			return typeof this.eventColumnListeners[i]['func'] == 'function';
		}else{
			return false;
		}
	}
}

/**
 * Function will is called on columns events
 * @param		String		type		type of event registered
 * @param		Object		data		contains the columns Id and event registered
 */
TableWidget.prototype.callColumnListener = function(type, data){
	var args = new Array();
	args[0] = this;			// ref to table widget
	switch(type){
		case "click":
			args[1] = "click";			// type
			args[2] = data.columnID || false;	// columnID of clicked column
			args[3] = data.event || false;	// event object
			break;
	}
	for(var i in this.eventColumnListeners){
		if(this.eventColumnListeners[i]['type'] == 'all' || this.eventColumnListeners[i]['type'] == type){
			try {
				this.eventColumnListeners[i]["func"].apply(this.eventColumnListeners[i]["scope"], args);
			}
			catch (e){
				alert(e);
			}
		}
	}
}


/**
 * Event functions
 */
function eventTableWidgetKeyboard(moduleObject, element, event){
	if(typeof moduleObject == "undefined")
		return false;

	if (event.type == "keydown"){

		switch (event.keyCode){
			case 38: // KEY_UP
//				selectElement = element.previousSibling;
//				if(!event.ctrlKey)
//					openItem = true;

				var prevRowID = moduleObject.prevRowID(moduleObject.getSelectedRowID(moduleObject.getNumSelectedRows()-1));
				if(prevRowID){
					//if(!event.ctrlKey)
						moduleObject.clearSelection();

					moduleObject.selectRow(prevRowID);
				}


				break;
			case 40: // KEY_DOWN

// TODO: Must use separate selected and markedSelected ??
// TRY OUTLOOK: select message -> use ctrl + up
				var nextRowID = moduleObject.nextRowID(moduleObject.getSelectedRowID(0));
				if(nextRowID){
					//if(!event.ctrlKey)
						moduleObject.clearSelection();

					moduleObject.selectRow(nextRowID);
				}
				break;
		}
	}
}

function eventTableWidgetClickRow(moduleObject, element, event){
	if(typeof moduleObject == "undefined")
		return false;

	event.preventDefault();
	event.stopPropagation();

	var button = 0;
	if(event.button) {			// FF
		button = event.button;
	} else if(event.which) {	// IE
		button = event.which;
	}

	if(element.getAttribute("rowID") && button != 2) {
		var rowID = element.getAttribute("rowID");

		/**
		 * This table defines what should happen when the user clicks on a row
		 * +----------+------+----------++----------+-------------+
		 * | selected | ctrl | only one || action   | clear first |
		 * +----------+------+----------++----------+-------------+
		 * |    0     |   0  |    -     || select   |    1        |
		 * +----------+------+----------++----------+-------------+
		 * |    0     |   1  |    -     || select   |    0        |
		 * +----------+------+----------++----------+-------------+
		 * |    1     |   0  |    0     || select   |    1        |
		 * +----------+------+----------++----------+-------------+
		 * |    1     |   1  |    0     || deselect |    0        |
		 * +----------+------+----------++----------+-------------+
		 * |    1     |   0  |    1     || nothing  |    0        |
		 * +----------+------+----------++----------+-------------+
		 * |    1     |   1  |    1     || deselect |    0        |
		 * +----------+------+----------++----------+-------------+
		 */
		// Deselect only this selected message with use of the ctrl key
		if(moduleObject.selected[rowID] && event.ctrlKey) {
			moduleObject.deselectRow(rowID);
		// Only do something when the clicked row is not the only row that is selected
		} else if(!moduleObject.isRowIDSelected(rowID) || moduleObject.getNumSelectedRows() != 1) {
			if((!event.ctrlKey && !event.shiftKey) || !this.multipleSelect)
				moduleObject.clearSelection();

			if(event.shiftKey && this.multipleSelect) {
				var prevRowElement = moduleObject.getRowByRowID(moduleObject.prevSelectedMessageID);
				var rowTopLeft = dhtml.getElementTopLeft(element);
				var prevRowTopLeft = dhtml.getElementTopLeft(prevRowElement);

				// clear previous selection, if selection is changed
				moduleObject.clearSelection(false);

				// up to down selection
				if(rowTopLeft[1] > prevRowTopLeft[1]) {
					var nextRowID = moduleObject.prevSelectedMessageID;
					while(nextRowID && nextRowID != rowID) {
						moduleObject.selectRow(nextRowID, false, false);
						nextRowID = moduleObject.nextRowID(nextRowID);
					}

					// select the last row
					if(nextRowID)
						moduleObject.selectRow(nextRowID, false, false);
				// down to up selection
				} else {
					var prevRowID = moduleObject.prevSelectedMessageID;
					while(prevRowID && prevRowID != rowID) {
						moduleObject.selectRow(prevRowID, false, false);
						prevRowID = moduleObject.prevRowID(prevRowID);
					}

					// select the last row
					if(prevRowID)
						moduleObject.selectRow(prevRowID, false, false);
				}
			} else {
				moduleObject.selectRow(rowID);
			}
		}
	}
}

function eventTableWidgetDblClickRow(moduleObject, element, event){
	if(typeof moduleObject == "undefined")
		return false;

	if(element.getAttribute("rowID")){
		var rowID = element.getAttribute("rowID");
		moduleObject.callRowListener("dblclick", {rowID: rowID, event: event});
	}
}

/**
 * Global event function
 * this function is registered with contextemenu event of table widget,
 * and it will call external event that is registered with table widget 
 * for context menu handling
 */
function eventTableWidgetRowContextMenu(moduleObject, element, event){
	if(typeof moduleObject == "undefined")
		return false;

	if(element.getAttribute("rowID")){
		var rowID = element.getAttribute("rowID");
		moduleObject.callRowListener("contextmenu", {rowID: rowID, event: event});
	}
}

/**
 * Function which shows loader.
 */
TableWidget.prototype.showLoader = function ()
{
	dhtml.addElement(this.element, "center", false, "tablewidget_loader["+ this.widgetID +"]", _("Loading..."), this.windowObj);
}
/**
 * Function which hides loader.
 */
TableWidget.prototype.hideLoader = function ()
{
	var loader = dhtml.getElementById("tablewidget_loader["+ this.widgetID +"]");
	dhtml.deleteElement(loader);
}

/**
 * Function which shows a message when table widget contains zero rows.
 */
TableWidget.prototype.showZeroResultMessage = function ()
{
	var msgElement = dhtml.getElementById("tablewidget_message["+ this.widgetID +"]");

	if(!msgElement) {
		dhtml.addElement(this.element, "center", false, "tablewidget_message["+ this.widgetID +"]", _("There are no items to show in this view."), this.windowObj);
	}
}

/**
 * Function which hides message which is shown when tablewidget contains zero rows.
 */
TableWidget.prototype.hideZeroResultMessage = function ()
{
	var msgElement = dhtml.getElementById("tablewidget_message["+ this.widgetID +"]");

	if(msgElement) {
		dhtml.deleteElement(msgElement);
	}
}

/**
 * Global event function
 * this function is registered with click event of table column for table widget,
 * and it will call external event that is registered with table widget 
 * for click handling
 */
function eventTableWidgetColumnSort(moduleObject, element, event){
	if(typeof moduleObject == "undefined")
		return false;

	moduleObject.callColumnListener("click", {columnID: element, event: event});
}
