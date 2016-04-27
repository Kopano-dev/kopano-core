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
 * File contains restoreItemListModule class.
 * this module is responsible for handling the restore items module
 */

restoreitemslistmodule.prototype = new ListModule;
restoreitemslistmodule.prototype.constructor = restoreitemslistmodule;
restoreitemslistmodule.superclass = ListModule.prototype;

/**
 * Defining class for restoreitemslistmodule.
 * @param string id define the id for module.
 * @param Dom_Object element contains the reference of element object.
 * @param String title title string
 * @param Object data contains the data needed to create the module.
 */
function restoreitemslistmodule(id, element, title, data)
{
	if(arguments.length > 0) {
		this.init(id, element, title, data);
	}
}

/**
 * Constructor function for restoreItemListModule
 */
restoreitemslistmodule.prototype.init = function(id, element, title, data)
{
	restoreitemslistmodule.superclass.init.call(this, id, element, title, data);
}

/**
 * execute function for restoreItemListModule
 * called to show the message list
 * @param Strin type define the action type [list/delete/deleteall/restore/restoreall/
 * folderlist/deleteallfolder/deletefolder/restoreallfolder/restorefolder/]
 * @param Object action contains the response data.
 */
restoreitemslistmodule.prototype.execute = function(type, action){	

	//parse the xml and create a data object which will pass to table widget to render table.
	this.itemProps = new Object();
	var items = action.getElementsByTagName("item");
	if(items && items.length > 0) {
		for(var i = 0; i < items.length; i++)
		{
			var item = items[i];
			var entryid = dhtml.getXMLValue(item, this.uniqueid);
			var parent_entryid = dhtml.getXMLValue(item, "parent_entryid", false); 
			
			if(!parent_entryid || (parent_entryid==this.entryid && entryid) || action.getAttribute("searchfolder")) {
				var rowid = this.getRowId(entryid);
				var element = dhtml.getElementById(rowid);			
			}
			this.updateItemProps(item);
		}
	}

	//call the function to create table widget and insert the data in it.
	//second param to function call is added to define the type of restore items.
	initRestoreItems(this.itemProps, type);
}

/**
 * function to update itemprops which will be sent as data to table widget.
 * @param Object item contains the response data.
 * return - none, update this.itemProps object.
 */
restoreitemslistmodule.prototype.updateItemProps = function(item)
{
	var entryid = dhtml.getXMLValue(item, this.uniqueid, null)
	if (entryid){
		this.itemProps[entryid] = new Object();
		for(var j=0;j<item.childNodes.length;j++){
			if (item.childNodes[j].nodeType == 1){
				var prop_name = item.childNodes[j].tagName;
				var prop_val = dom2array(item.childNodes[j]);
				if (prop_val!==null){
					this.itemProps[entryid][prop_name] = prop_val;
				}
			}
		}
	}
}

/**
 * Function loadSortSettings overrides the function of listmodule.loadSortSettings()
 * @return (object) result Sort object to sort the data on basis of some column.
 */
restoreitemslistmodule.prototype.loadSortSettings = function ()
{
	var column = new Object();
	column["attributes"] = new Object();
	var data = new Object();

	// workaround for the time being for sorting the data on basis of column "deleted_on"
	// later on when we need to sort the data on any specific column [i.e. when table widget support the sorting]
	// we can make this generic and work. as for now making this as hardcoded.
	data["deleted_on"] = "desc";

	for(var i in data){
		if(i != "prototype"){//workarround		
			column["attributes"]["direction"] = data[i];
			column["_content"] = i;
		}
	}
	
	if(!column["_content"]){
		var result = false;
	}else{
		var result = new Array(column);
	}
	
	return result;
}