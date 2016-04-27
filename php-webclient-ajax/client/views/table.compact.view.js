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
 * Compact table view: special view only for e-mail folders. It requires a
 * fixed set of fields: Subject, From, Date, Priority, Attachments, Flags,
 * message class, message flags. If these fields are not in the data set,
 * they are not shown, and any extra fields are ignored.
 */

TableCompactView.prototype = new TableView;
TableCompactView.superclass = TableView.prototype;
TableCompactView.prototype.constructor = TableCompactView;

function TableCompactView(moduleID, element, events, data, uniqueid)
{	
	if(arguments.length > 0) {
		this.init(moduleID, element, events, data, uniqueid);
	}
}

TableCompactView.prototype.init = function(moduleID, element, events, data, uniqueid)
{
	TableCompactView.superclass.init.call(this, moduleID, element, events, data, uniqueid);
	this.idCount = 0;
}

/**
 * Adds the actual table rows to the output element
 */
TableCompactView.prototype.addRows = function(rows)
{
	var entryids = new Object();
	// Reset the idCount as we are adding the entire table from scratch again
	this.idCount = 0;
	
	var table = new Array();
	table.push("<table class='table' border='0' cellpadding='0' cellspacing='0'><tbody>");

	for(var i = 0; i < rows.length; i++)
	{
		var row = rows[i];

		var entryid = dhtml.getXMLValue(row, this.uniqueid);
		var id = this.idCount++;
		entryids[id] = entryid;
		table.push(this.renderRow(id, row));
	}

	table.push("</tbody></table>");	
	this.divElement.innerHTML = table.join("");

	// Set Events.
	var tableElement = this.divElement.getElementsByTagName("table")[0];
	
	// Resize table width. Only in IE.
	if(window.BROWSER_IE && !window.BROWSER_IE8) {
		tableElement.style.width = this.divElement.clientWidth + "px";
	}
	
	for(var i = 0; i < tableElement.rows.length; i++)
	{
		if(typeof(dragdrop) != "undefined") {
			dragdrop.addDraggable(tableElement.rows[i], "folder",null,null,this.moduleID);
		}
		
		if(this.events["row"]) {
			dhtml.setEvents(this.moduleID, tableElement.rows[i], this.events["row"]);
		}

		if(this.events["rowcolumn"]) {
			for(var column in this.events["rowcolumn"]) {
				// We only support events on the flag_status and icon_index columns
				switch(column) {
					case 'flag_icon':
						dhtml.setEvents(this.moduleID, tableElement.rows[i].cells[2], this.events["rowcolumn"][column]);
						break;
					case 'icon_index':
						dhtml.setEvents(this.moduleID, tableElement.rows[i].cells[0], this.events["rowcolumn"][column]);
						break;
				}
			}
		}
	}

	return entryids;
}

TableCompactView.prototype.addItem = function(row, properties, action)
{
	var entry = false;
	var tableElement = this.divElement.getElementsByTagName("table")[0];
	var id = this.idCount++;
	
	if(tableElement && row.childNodes.length > 0) {
		var entryid = row.getElementsByTagName(this.uniqueid)[0];

		if(entryid && entryid.firstChild) {
            // Create a new element to build the row in
            var newElement = document.createElement("div");
            
            // Create the new row in its own table
            newElement.innerHTML = "<table><tbody>" + this.renderRow(id, row) + "</tbody></table>";
            var newRowElement = newElement.firstChild.firstChild.firstChild;

            // Add events
            if(typeof(dragdrop) != "undefined") {
                dragdrop.addDraggable(newRowElement, "folder",null,null,this.moduleID);
            }
            
            if(this.events["row"]) {
                dhtml.setEvents(this.moduleID, newRowElement, this.events["row"]);
            }

            if(this.events["rowcolumn"]) {
                for(var column in this.events["rowcolumn"]) {
                    // We only support events on the flag_status and icon_index columns
                    switch(column) {
                        case 'flag_status':
                            dhtml.setEvents(this.moduleID, newRowElement.cells[2], this.events["rowcolumn"][column]);
                            break;
                        case 'icon_index':
                            dhtml.setEvents(this.moduleID, newRowElement.cells[0], this.events["rowcolumn"][column]);
                            break;
                    }
                }
            }

            // Add new row under tbody
            var row = tableElement.insertRow(-1);
            
            tableElement.firstChild.replaceChild(newRowElement, row);

            // Fill return structure
            entry = new Object();
            entry["id"] = id;
            entry[this.uniqueid] = entryid.firstChild.nodeValue;


		}
	}
	
    return entry;	
}

/**
 * Updates a single row
 *
 * This is implemented rather strangely, but this is the only way to replace an existing <tr> while
 * using innerHTML. This allows us to user renderRow() for both single-row updates and the main
 * table list.
 */
 
TableCompactView.prototype.updateItem = function(element, item, properties)
{
	var id = element.id;
	var entry = new Object;
	var selected = dhtml.hasClassName(element, "rowselected");

	// Create a new element to build the row in
	var newElement = document.createElement("div");
	
	// Create the new row in its own table
	newElement.innerHTML = "<table><tbody>" + this.renderRow(id, item) + "</tbody></table>";
	var newRowElement = newElement.firstChild.firstChild.firstChild;
	
	// Select the row if it was previously selected
	if(selected)
		dhtml.addClassName(newRowElement, "rowselected");
		
	// Re-attach events
	dhtml.copyEvents(newRowElement, element);
	// Re-attach events for flag and read/unread changes
	dhtml.copyEvents(newRowElement.cells[0], element.cells[0]);
	dhtml.copyEvents(newRowElement.cells[2], element.cells[2]);
	// Copy group attribute. This attribute is used by the dragdrop.
	newRowElement.setAttribute("group", element.getAttribute("group"));
	
	// Replace the existing row with this one	
	var parentElement = element.parentNode;
	parentElement.replaceChild(newRowElement, element);
	
	entry["id"] = id;
	entry[this.uniqueid] = dhtml.getXMLValue(item, this.uniqueid);
	
	return entry;
}

/**
 * Returns HTML <tr> line representing one table row
 */
TableCompactView.prototype.renderRow = function(elemid, row)
{
	var rowData = new Array;
	
	if(row.childNodes.length > 0) {
		var message_class = dhtml.getXMLValue(row,"message_class","");
		// Get message class (double click => open window)
		var messageClass = "";
		if(message_class) {
			messageClass = message_class.replace(/\./g, "_").toLowerCase();
			switch(messageClass) {
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
					messageClass = "ipm_readmail";
					
					var messageUnsent = row.getElementsByTagName("message_unsent")[0];
					if(messageUnsent && messageUnsent.firstChild) {
						messageClass = "ipm_createmail";
					}
					break;
				case "ipm_taskrequest":
				case "ipm_taskrequest_accept":
				case "ipm_taskrequest_decline":
				case "ipm_taskrequest_update":
					messageClass = "ipm_task";
					break;
			}
			messageClass += " read_unread";
		}

		// Get message flag (unread)
		var messageUnread = "message_unread";
		var message_flags = parseInt(dhtml.getXMLValue(row,"message_flags", -1),10);
		if(message_flags == -1 || (message_flags & MSGFLAG_READ) == MSGFLAG_READ) {
			messageUnread = "";
		}
		
		if(message_flags == -1) {
			messageflaghtml = "";
		} else {
			messageflaghtml = 'messageflags="' + message_flags + '"';
		}
		
		var hasattachClass = "";
		if(dhtml.getXMLValue(row,"hasattach", false) == true) {
			if (dhtml.getXMLValue(row, "hideattachments", false) == false)
				hasattachClass = "icon_hasattach";
		}
		
		// Create row
		rowData.push("<tr id='" + elemid + "' class='row " + messageUnread + " " + messageClass + "' " + messageflaghtml + " entryid='"+dhtml.getXMLValue(row, this.uniqueid)+"' >");

		var isStub = dhtml.getXMLValue(row, "stubbed", false);
		var iconClass = iconIndexToClassName(dhtml.getXMLValue(row,"icon_index"), dhtml.getXMLValue(row, "message_class"), !messageUnread, isStub);

		var recvDate = dhtml.getXMLValue(row, "message_delivery_time");
		var sentDate = dhtml.getXMLValue(row, "client_submit_time");

		var to = dhtml.getXMLValue(row, "display_to");
		var from = dhtml.getXMLValue(row, "sent_representing_name");

		var date = this.dateProp == "message_delivery_time" ? recvDate : sentDate;
		var email = this.emailProp == "sent_representing_name" ?  from : to;
		
		if(!date)
			date = _("(none)");
		else
			date = strftime(_("%a %x %X"),date);
		if(!email)
			email = "";

		var flagClass = this.getFlagClass(parseInt(dhtml.getXMLValue(row, "flag_status"),10), parseInt(dhtml.getXMLValue(row, "flag_icon"), 10));

		var importanceClass = "";
		switch(parseInt(dhtml.getXMLValue(row, "importance"))){
			case 0:
				importanceClass = "icon_importance_low";
				break;
			case 2:
				importanceClass = "icon_importance_high";
				break;
		}
		

		// Create cells
		rowData.push("<td class='rowcolumn message_icon " + messageClass + " " + iconClass + " compacticonfield'>&nbsp;</td>");
		rowData.push("<td class='rowcolumn compactcenter'><table class='compactcenter'>");
		rowData.push("  <tr>");
		rowData.push("    <td class='compactleftfield compacttopfield'><div class='rowcolumntext'>" + email.htmlEntities() + "</div></td>");
		rowData.push("    <td class='compactrightfield compacttopfield'><div class='rowcolumntext'>" + date + "</div></td>");
		rowData.push("  </tr>");
		rowData.push("  </table><table class='compactcenter'>");
		rowData.push("  <tr>");
		rowData.push("    <td class='compactbottomfield compactleftfield'><div class='rowcolumntext'>" + dhtml.getXMLValue(row, "subject", "").htmlEntities() + "</div></td>");
		if (importanceClass!="")
			rowData.push("    <td class='compacticonfield message_icon " + importanceClass + "'>&nbsp;</td>");
		if (hasattachClass!="")
			rowData.push("    <td class='compacticonfield message_icon " + hasattachClass + "'>&nbsp;</td>");
		rowData.push("  </tr>");
		rowData.push("</table></td>");
		rowData.push("<td class='rowcolumn message_icon compacticonfield " + flagClass +"'></td>");
		rowData.push("</tr>");
	}
	
	return rowData.join("");
}

/**
 * Builds the column element at the top of the table
 */
TableCompactView.prototype.addColumns = function(columns, sortColumn, sortDirection)
{
	this.columns = columns;
	this.sortColumn = sortColumn;
	this.sortDirection = sortDirection;
	
	var emailName = "";
	var emailSort = "";
	var emailProp = "";
	var dateName = "";
	var dateSort = "";
	var dateProp = "";
	
	for(var i=0;i<this.columns.length;i++) {
		if(this.columns[i]["id"] == "sent_representing_name") {
			emailName = this.columns[i]["name"];
			emailSort = this.columns[i]["title"];
			emailProp = this.columns[i]["id"];
		}
		if(this.columns[i]["id"] == "display_to") {
			emailName = this.columns[i]["name"];
			emailSort = this.columns[i]["title"];
			emailProp = this.columns[i]["id"];
		}
		if(this.columns[i]["id"] == "message_delivery_time") {
			dateName = this.columns[i]["name"];
			dateSort = this.columns[i]["title"];
			dateProp = this.columns[i]["id"];
		}
		if(this.columns[i]["id"] == "client_submit_time") {
			dateName = this.columns[i]["name"];
			dateSort = this.columns[i]["title"];
			dateProp = this.columns[i]["id"];
		}
	}
	
	this.emailProp = emailProp;
	this.dateProp = dateProp;
	
	var table = new Array();
	table.push("<div id='columnbackground'>");
	table.push("<table class='table' width='100%' border='0' cellpadding='0' cellspacing='0'><tr class='columns'>");

	table.push("<td class='compacticonfield' style='cursor:default;'>&nbsp;</td>");
	table.push("<td class='compactcenter'><table width='100%' class='compactcenter'><tr>");
	
	var sort;
	var idext;

	if(sortColumn == emailProp) {
		sort = "&nbsp;<span class='sort_" + sortDirection + "'>&nbsp;</span>";
		idext = "_sort_" + sortDirection;
	} else {
		sort = "";
		idext = "";
	}
		
	table.push("<td id='property_" + emailProp + idext +"' class='column compacttopfield compactleftfield' title='" + emailSort + "'><span class='column_seperator'>&nbsp;</span>" + emailName + sort + "</td>");
		
	if(sortColumn == dateProp) {
		sort = "&nbsp;<span class='sort_" + sortDirection + "'>&nbsp;</span>";
		idext = "_sort_" + sortDirection;
	} else {
		sort = "";
		idext = "";
	}
		
	table.push("<td id='property_" + dateProp + idext +"' class='column compacttopfield compactleftfield' title='" + dateSort + "'><span class='column_seperator'>&nbsp;</span>" + dateName + sort + "</td>");
	
	table.push("</tr></table>");
	table.push("<td class='column' width='16' style='cursor:default;'>&nbsp;</td></tr></table></div>");
	this.columnsElement.innerHTML = table.join("");

	if(this.events["column"]) {
		var tableElement = this.columnsElement.getElementsByTagName("table")[1]; // need second table element due to nested table
		if(tableElement) {
			var columnRow = tableElement.rows[0];

			if(columnRow) {
				for(var i = 0; i < columnRow.cells.length ; i++)
				{
					dhtml.setEvents(this.moduleID, columnRow.cells[i], this.events["column"]);
				}
			}
		}
	}

}

/**
 * Returns the total number of rows
 */
TableCompactView.prototype.getRowCount = function()
{
	var elems = this.divElement.getElementsByTagName("tr");
	return elems.length/3; // we use 3 TR's for a single row!
}

/**
 * Returns the element ID of a specific row number
 */
TableCompactView.prototype.getElemIdByRowNumber = function(rownum)
{
	rownum = rownum * 3; // we use 3 TR's for a single row!
	var elems = this.divElement.getElementsByTagName("tr");
	if (typeof elems[rownum] != "undefined"){
		return elems[rownum].id;
	}else{
		return;
	}	
}

