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

function addressbook_loadSettings(settings){
	
	dialoghelper.loadABHierarchy(parentWebclient.hierarchy, addressbook_showhierarchy);

}

function addressbook_saveSettings(settings){
	var field;

	var folderList = dhtml.getElementById("addressbook_folder");
	field = folderList[folderList.selectedIndex];
	settings.set("addressbook/default/foldertype",field.folderType);
	settings.set("addressbook/default/entryid",field.value);
	settings.set("addressbook/default/storeid",field.storeid);
}

function addressbook_showhierarchy(data)
{
	var gabentryid = webclient.settings.get("addressbook/default/entryid","");

	var items = data["folder"];
	var folderList = dhtml.getElementById("addressbook_folder");
	for(var i=0;i<items.length;i++){
		var folder = items[i];

		var name = folder.display_name;
		if (folder["parent_entryid"] && folder["parent_entryid"]!=folder["entryid"]){
			name = NBSP+NBSP+name;	
		}
		
		var newOption = dhtml.addElement(folderList, "option", null, null, name);
		newOption.value = folder["entryid"];
		if (folder["entryid"] == gabentryid)
			newOption.selected = true;
		newOption.folderType = folder["type"];
		if (typeof(folder["storeid"])!="undefined")
			newOption.storeid = folder["storeid"];
	}

}
