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

reminderlistmodule.prototype = new ListModule;
reminderlistmodule.prototype.constructor = reminderlistmodule;
reminderlistmodule.superclass = ListModule.prototype;

function reminderlistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

reminderlistmodule.prototype.init = function(id, element, title, data)
{
	if (typeof(data) == "undefined"){
		data = new Object;
	}
	data["has_no_menu"] = true;

	reminderlistmodule.superclass.init.call(this, id, element, title, data);

	this.timer = window.setTimeout("webclient.reminder.getReminders()",15000);
	this.lastrowchecksum = "";
	
	/**
	 * row id of next reminder which should be selected by default after snoozing or dismissing a reminder
	 */
	this.nextSelectionRowId = false;
}


reminderlistmodule.prototype.showReminderWindow = function()
{
	if (!this.isReminderWindowOpen()){
		this.reminderWindow = webclient.openWindow(this, "reminderdialog", DIALOG_URL+"task=reminder_standard", 450, 335);
	}else{

		this.reminderWindow.focus();
	}

	return this.reminderWindow;
}

reminderlistmodule.prototype.isReminderWindowOpen = function()
{
	if (typeof(this.reminderWindow)=="undefined" || this.reminderWindow == null || typeof(this.reminderWindow.webclient)=="undefined"){
		return false;
	}
	return true;
}

/**
 * Function which execute an action. This function is called by the XMLRequest object.
 * @param string type the action type
 * @param object action the action tag 
 */ 
reminderlistmodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "getreminders":
			this.loadTable(action);
			clearTimeout(this.timer);
			this.timer = window.setTimeout("webclient.reminder.getReminders()",60000);
			break;
		default:
	}
}

/**
* Event handler for opening items
*
* message_type is the type of message "appointment", "task", "contact" etc (usally a part of the message_class)
*/
reminderlistmodule.prototype.onOpenItem = function(entryid, message_type)
{
	this.setReadFlag(entryid, "read,"+(this.sendReadReceipt(dhtml.getElementById(entryid))?"receipt":"noreceipt"));

	var uri = DIALOG_URL+"task=" + message_type + "_standard&storeid=" + webclient.hierarchy.defaultstore.id + "&parententryid=" + this.itemProps[entryid]["parent_entryid"] + "&entryid=" + entryid;
	webclient.openWindow(this, message_type, uri);
}

// entryids must be an array of entryids even if it is just one item!
reminderlistmodule.prototype.snoozeItems = function(entryids, snoozeTime)
{
	for(var i=0;i<entryids.length;i++){
		var data = new Object();
		data["entryid"] = entryids[i];
		data["snoozetime"] = snoozeTime;
		webclient.xmlrequest.addData(this, "snooze", data);
	}
	this.getReminders();
}

// entryids must be an array of entryids even if it is just one item!
reminderlistmodule.prototype.dismissItems = function(entryids)
{
	for(var i=0;i<entryids.length;i++){
		var data = new Object();
		data["entryid"] = entryids[i];
		webclient.xmlrequest.addData(this, "dismiss", data);
	}
	this.getReminders();
}

/**
 * Get the reminders
 *
 * Normally this is called every 60 seconds, so it will only popup the reminders
 * if there is a change. However, if you set 'force' to TRUE, it will force the
 * reminders window to open
 */
reminderlistmodule.prototype.getReminders = function(force)
{
    if(force)
        this.lastrowchecksum = "";
        
	var data = new Object();
	webclient.xmlrequest.addData(this, "getreminders", data);
	webclient.xmlrequest.sendRequest();
}

reminderlistmodule.prototype.loadTable = function(action)
{
	var items = action.getElementsByTagName("item");

	this.reminderCount = items.length;

	if (this.reminderCount == 0){ // no reminders? no need to continue
		if (this.isReminderWindowOpen()){
			this.reminderWindow.close();
			this.reminderWindow = null;
		}
		return false;
	}
	
	// Do checksum detection to see if any reminders have changed. If so, popup the reminders
	// window. If not, just leave it closed.
	var newchecksum = dhtml.getXMLValue(action, "rowchecksum", "");
	
	if(this.lastrowchecksum != newchecksum) {
        // update internal data
        var columns = action.getElementsByTagName("column");
        this.columnData = new Array();
        for(var i=0;i<columns.length;i++){
            var col = dom2array(columns[i]);
            this.columnData.push(col);
        }

        this.itemProps = new Object();
        for(var i=0;i<items.length;i++){
            this.updateItemProps(items[i]);
        }

        this.showReminderWindow(); // creates window or popups existing window
        this.showData();
        this.lastrowchecksum = newchecksum;
    }
    
	return true;
}


reminderlistmodule.prototype.showData = function()
{
	if (!this.reminderWindow || !this.reminderWindow.dhtml)
		return;

	if (this.reminderCount == 0){ // no reminders? no need to continue
		if (this.isReminderWindowOpen()){
			this.reminderWindow.close();
			this.reminderWindow = null;
		}
		return false;
	}

	var tableWidgetElem = this.reminderWindow.dhtml.getElementById("remindertable");

	this.tableWidget = new TableWidget(tableWidgetElem, true, this.reminderWindow);

	for(var i=0;i<this.columnData.length;i++){
		var col = this.columnData[i];
		if (col["id"]=="icon_index"){
			this.tableWidget.addColumn(col["id"], "", 25, col["order"]);
		}else{
			this.tableWidget.addColumn(col["id"], col["name"], false, col["order"]);
		}
	}

	this.tableWidget.addRowListener(this.eventRowSelection, "all", this);

	var items = new Array();
	for(var i in this.itemProps){
		var itemData = this.itemProps[i];

		var item = new Object();
		for(var j=0;j<this.columnData.length;j++){
			var value = "";
			var colId = this.columnData[j]["id"];
			switch(colId){
				case "icon_index":
					value = "<div class='rowcolumn message_icon "+iconIndexToClassName(itemData["icon_index"], itemData["message_class"], false)+"'>&nbsp;</div>";
					break;
				case "remindertime":
					var dueBy = new Date(itemData["remindertime"]*1000);
					value = dueBy.simpleDiffString(new Date(), "", _("%s overdue"));
					break;
				default:
					if(typeof(itemData[colId])!="undefined"){
						value = String(itemData[colId]).htmlEntities();
					}else{
						value = "&nbsp;";
					}
			}
			item[colId] = {innerHTML: value};
		}
		item["entryid"] = itemData["entryid"];
		item["rowID"] = itemData["entryid"];
		items.push(item);
	}

	this.tableWidget.generateTable(items);

	if(this.tableWidget && this.nextSelectionRowId !== false) {
		// select row based on previous saved value of row id
		this.tableWidget.selectRow(this.nextSelectionRowId, true);
	} else  if (this.tableWidget && items[0] && items[0]["rowID"]) {
		// select first row
		this.tableWidget.selectRow(items[0]["rowID"], false);
	}
}

reminderlistmodule.prototype.getReminderDataByEntryid = function(entryid)
{
	return this.itemProps[entryid];
}

reminderlistmodule.prototype.eventRowSelection = function(widget, type, selectedIDs, changedIDs)
{
	var selectedRowCount = widget.getNumSelectedRows();

	var fieldStartDate = dhtml.getElementById("reminderdate", "div", this.reminderWindow.document);
	var fieldIcon = dhtml.getElementById("remindericon", "div", this.reminderWindow.document);
	var fieldSubject= dhtml.getElementById("remindersubject", "div", this.reminderWindow.document);

	if (selectedRowCount != 1){
		fieldStartDate.innerHTML = _("%d reminders are selected").sprintf(selectedRowCount);
		fieldSubject.innerHTML = "&nbsp;";
		fieldIcon.className = "";
	}else{
		var entryid = widget.getSelectedRowData()[0].entryid;
		var itemData = this.getReminderDataByEntryid(entryid);
		fieldIcon.className = "icon " + iconIndexToClassName(itemData["icon_index"], itemData["message_class"], false);

		fieldSubject.innerHTML = itemData["subject"].htmlEntities();
		fieldStartDate.innerHTML = "&nbsp;";

		if (itemData["message_class"].substr(0, 15).toLowerCase() == "ipm.appointment"){
			fieldStartDate.innerHTML = _("Startdate: %s").sprintf((new Date(parseInt(itemData["appointment_startdate"])*1000)).strftime(_("%a %x %X")));
		}
	}
	
	//Open item on doubleclick
	if (type == "dblclick") {
		var row = widget.getRowByRowID(selectedIDs[0]);
		this.eventOpenItem(this, row, false);
		
	}
}

reminderlistmodule.prototype.eventDismiss = function(moduleObject, element, event)
{
	var items = moduleObject.tableWidget.getSelectedRowData();
	if (items.length > 0){
		var entryids = new Array();
		for(var i=0;i<items.length;i++){
			entryids.push(items[i]["entryid"]);
		}

		// store row id of next reminder, so when reloading we can select the next reminder
		this.nextSelectionRowId = this.getNextSelectionRow(this.tableWidget, items[0]["entryid"]);

		moduleObject.dismissItems(entryids);
	}
}

/**
 * get next/previous non-selected row id, so it will be used to select the row
 * when we will be reloading reminders dialog
 * @param {Object} tableWidget table widget object
 * @param {HexString} rowId row id of currently selected reminder
 * @return {HexString} row id of next reminder which should be selected on reload
 */
reminderlistmodule.prototype.getNextSelectionRow = function(tableWidget, rowId)
{
	var nextRowId = rowId;
	do {
		nextRowId = tableWidget.nextRowID(nextRowId);

		if(!nextRowId) {
			// no more next rows left
			break;
		}
	} while(tableWidget.isRowIDSelected(nextRowId));

	if(!nextRowId) {
		// re-assign original value as nextRowId will be false here
		nextRowId = rowId;

		// no next row then select previous row
		do {
			nextRowId = tableWidget.prevRowID(nextRowId);

			if(!nextRowId) {
				// no more previous rows left
				break;
			}
		} while(tableWidget.isRowIDSelected(nextRowId));
	}

	return nextRowId;
}

reminderlistmodule.prototype.eventDismissAll = function(moduleObject, element, event)
{
	var entryids = new Array();
	for(var i in moduleObject.itemProps){
		entryids.push(moduleObject.itemProps[i]["entryid"]);
	}
	moduleObject.dismissItems(entryids);
}

reminderlistmodule.prototype.eventSnooze = function(moduleObject, element, event)
{
	var snoozetime = dhtml.getElementById("snoozetime", "select", moduleObject.reminderWindow.document).value;
	var items = moduleObject.tableWidget.getSelectedRowData();
	if (items.length > 0){
		var entryids = new Array();
		for(var i=0;i<items.length;i++){
			entryids.push(items[i]["entryid"]);
		}

		// store row id of next reminder, so when reloading we can select the next reminder
		this.nextSelectionRowId = this.getNextSelectionRow(this.tableWidget, items[0]["entryid"]);

		moduleObject.snoozeItems(entryids, snoozetime);
	}
}

reminderlistmodule.prototype.eventOpenItem = function(moduleObject, element, event)
{
	var items = moduleObject.tableWidget.getSelectedRowData();
	for(var i=0;i<items.length;i++){
		var item = moduleObject.getReminderDataByEntryid(items[i]["entryid"]);
		moduleObject.onOpenItem(item["entryid"], item["message_class"].toLowerCase().replace(".","_").substring(4));
	}
}
