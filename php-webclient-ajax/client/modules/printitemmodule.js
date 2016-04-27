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
* Module for making the print preview and printing
*/
printitemmodule.prototype = new ItemModule;
printitemmodule.prototype.constructor = printitemmodule;
printitemmodule.superclass = ItemModule.prototype;

function printitemmodule(id)
{
	if(arguments.length > 0) {
		this.init(id);
	}
}

printitemmodule.prototype.init = function(id)
{
	printitemmodule.superclass.init.call(this, id);
	this.printpreview = false;
	
	// Add keycontrol event
	webclient.inputmanager.addObject(this);

	this.keys["print"] = KEYS["edit_item"]["print"];
	webclient.inputmanager.bindKeyControlEvent(this, this.keys, "keyup", eventPrintItemKeyCtrlPrint);
}

printitemmodule.prototype.item = function(action)
{
	// get the iframe for the preview
	printing_frame = dhtml.getElementById("printing_frame").contentWindow;
	
	var message = action.getElementsByTagName("item")[0];
	this.propData = new Object();
	
	if(message && message.childNodes) {
		for(var i = 0; i < message.childNodes.length; i++)
		{
			var property = message.childNodes[i];

			if(property && property.firstChild && property.tagName == "members") {
				this.propData[property.tagName] = property;
			}else if(property && property.firstChild && property.tagName == "attachments") {
				var attachmentList = new Array();

				/**
				 * Fetch all attachments from response and save all attachments 
				 * in attachment list array which are not hidden.
				 */
				var allAttachments = property.getElementsByTagName("attachment");
				for(var j = 0 ; j < allAttachments.length; j++){
					var hiddenAttach = dhtml.getXMLValue(allAttachments[j], "hidden");
					var name = dhtml.getXMLValue(allAttachments[j], "name");
					if(!(hiddenAttach && hiddenAttach == 1)) {
						if(name) attachmentList.push(name);
					}
				}
				if(attachmentList.length > 0){
					this.propData[property.tagName] = attachmentList.join("; ");
				}
			}else {
				if(property && property.firstChild && property.firstChild.nodeValue)
				{
					if (property.tagName=="body"){
						this.propData[property.tagName] = property;
					}else{
						this.propData[property.tagName] = property.firstChild.nodeValue;
					}
				}
			}
		}
	}
	
	// select which preview type we need and create the right object
	switch(this.propData["message_class"])
	{
		case "IPM.Task":
			if(this.propData["duedate"]){
				this.propData["duedate"]=strftime_gmt(_("%a %x"), this.propData["duedate"]);
			}
			
			if(this.propData["startdate"]){
				this.propData["startdate"]=strftime_gmt(_("%a %x"), this.propData["startdate"]);
			}
			
			if(this.propData["last_modification_time"]){
				this.propData["last_modification_time"]=strftime(_("%a %x %X"), this.propData["last_modification_time"]);
			}
			this.printpreview = new printMessageItem();
			break;
			
		case "IPM.Appointment":
		case "IPM.OLE.CLASS.{00061055-0000-0000-C000-000000000046}":
			/**
			 * If appointment is of recurrence type than set duedate, startdate and 
			 * last_modification_time acordingly. also set type of the reccurrence from its value
			 */
			if(this.propData["recurring"]) {
				if(this.propData["duedate"]){
					this.propData["duedate"]=strftime(_("%a %x %X"), this.propData["duedate"]);
				}
				if(this.propData["startdate"]){
					this.propData["startdate"]=strftime(_("%a %x %X"), this.propData["startdate"]);
				}
				if(this.propData["last_modification_time"]){
					this.propData["last_modification_time"]=strftime(_("%a %x %X"), this.propData["last_modification_time"]);
				}
				// Set recurrence type.
				if(this.propData["type"]){
					switch(this.propData["type"])
					{
						case "10":
							this.propData["recurrencetype"]= _("Daily");
						break;
						case "11":
							this.propData["recurrencetype"]= _("Weekly");
						break;
						case "12":
							this.propData["recurrencetype"]= _("Monthly");
						break;
						case "13":
							this.propData["recurrencetype"]= _("Yearly");
						break;
					}
				}
			}else{
				if(this.propData["duedate"]){
					this.propData["duedate"]=strftime(_("%a %x %X"), this.propData["duedate"]);
				}
				
				if(this.propData["startdate"]){
					this.propData["startdate"]=strftime(_("%a %x %X"), this.propData["startdate"]);
				}
				
				if(this.propData["last_modification_time"]){
					this.propData["last_modification_time"]=strftime(_("%a %x %X"), this.propData["last_modification_time"]);
				}
			}
			this.printpreview = new printMessageItem();
			break;
			
		case "IPM.Note":
			if(this.propData["message_delivery_time"]){
				this.propData["message_delivery_time"]=strftime(_("%a %x %X"), this.propData["message_delivery_time"]);
			}
			
			if(this.propData["last_modification_time"]){
				this.propData["last_modification_time"]=strftime(_("%a %x %X"), this.propData["last_modification_time"]);
			}
			this.printpreview = new printMessageItem();
			break;
			
		case "IPM.Schedule.Meeting":
		case "IPM.Schedule.Meeting.Request":
		case "IPM.Schedule.Meeting.Resp":
		case "IPM.Schedule.Meeting.Resp.Pos":
		case "IPM.Schedule.Meeting.Resp.Tent":
		case "IPM.Schedule.Meeting.Resp.Neg":
		case "IPM.Schedule.Meeting.Canceled":
			if(this.propData["message_delivery_time"]){
				this.propData["message_delivery_time"]=strftime(_("%a %x %X"), this.propData["message_delivery_time"]);
			}
			if(this.propData["duedate"]){
				this.propData["duedate"]=strftime(_("%a %x %X"), this.propData["duedate"]);
			}
			
			if(this.propData["startdate"]){
				this.propData["startdate"]=strftime(_("%a %x %X"), this.propData["startdate"]);
			}
			
			if(this.propData["last_modification_time"]){
				this.propData["last_modification_time"]=strftime(_("%a %x %X"), this.propData["last_modification_time"]);
			}
			this.printpreview = new printMessageItem();
			break;

		case "IPM.DistList":
			if(this.propData["last_modification_time"]){
				this.propData["last_modification_time"]=strftime(_("%a %x %X"), this.propData["last_modification_time"]);
			}
			this.printpreview = new printMessageItem();
			break;
		case "IPM.StickyNote":
		case "IPM.Contact":
		default:
			if(this.propData["last_modification_time"]){
				this.propData["last_modification_time"]=strftime(_("%a %x %X"), this.propData["last_modification_time"]);
			}
			
			if(this.propData["creation_time"]){
				this.propData["creation_time"]=strftime(_("%a %x %X"), this.propData["creation_time"]);
			}
			this.printpreview = new printMessageItem();
	}
	//update recipients 
	this.updateItemProps(message);
	this.propData["recipients"] = this.itemProps["recipients"];

	// give control to the printpreview object.
	if (this.printpreview){
		this.printpreview.init(this, printing_frame);
	}
}
/**
 * Function to handle the print preview from create mail dialog.
 */
printitemmodule.prototype.printFromUnsaved = function(parentwindow){
	// get the properties required for print preview.
	this.propData = parentwindow.getPropsFromDialog();
	
	// creates the attachment data for printpreview.
	var attachs = opener.module.attachments;
	var attachNames = new Array();
	for(var i=0;i<attachs.length;i++){
		attachNames.push(attachs[i]["name"]);
	}
	var newattachs = opener.module.newattachments;
	for(var i=0;i<newattachs.length;i++){
		attachNames.push(newattachs[i]["name"]);
	}
	this.propData["attachments"] = attachNames.join(", ");

	// initialize the printpreview 
	this.printpreview = new printMessageItem();
	this.printpreview.init(this, dhtml.getElementById("printing_frame").contentWindow, true);

	// show the menu of printpreview
	webclient.menu.showMenu(); 
}

function eventPrintItemKeyCtrlPrint(moduleObject, element, event)
{
	if (printing_frame){
		printing_frame.focus();
		printing_frame.print();
	}
}