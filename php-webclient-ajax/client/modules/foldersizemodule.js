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
* Folder properties module
*/

foldersizemodule.prototype = new Module;
foldersizemodule.prototype.constructor = foldersizemodule;
foldersizemodule.superclass = Module.prototype;

function foldersizemodule(id, element)
{
	if(arguments.length > 0) {
		this.init(id, element);
	}
}

foldersizemodule.prototype.init = function(id, element, title, data)
{
	if(data) {
		for(var property in data)
		{
			this[property] = data[property];
		}
	}
	
	foldersizemodule.superclass.init.call(this, id, element, title, data);
}

foldersizemodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "foldersize":
			this.setFolderSizeData(action);
			break;
	}
}

/**
* make a XML request for folder size data
* data must be an array with the store and folder entryid
*/
foldersizemodule.prototype.getFolderSize = function()
{
	var data = {
		"store": this.store,
		"entryid": this.entryid
	};
	webclient.xmlrequest.addData(this, "foldersize", data);
	webclient.xmlrequest.sendRequest();
}

foldersizemodule.prototype.setFolderSizeData = function(action){
	var mainfolder = action.getElementsByTagName("mainfolder");
	// Make sure to only get folder-items belong the subfolders-item
	var subfolderlist = action.getElementsByTagName("subfolders");
	subfolderlist = action.getElementsByTagName("folder");

	// Get the properies for the selected folder
	dialogData = {
		'name': dhtml.getXMLValue(mainfolder[0], "name").htmlEntities(),
		'size': parseInt(dhtml.getXMLValue(mainfolder[0], "size"),10) + _("KB"),
		'totalsize': parseInt(dhtml.getXMLValue(mainfolder[0], "totalsize"),10) + _("KB"),
		'subfolders': new Array()
	
	};
	// Build data for subfolder list
	for(var i=0;i<subfolderlist.length;i++){
		dialogData['subfolders'].push({
			name: {
				innerHTML: dhtml.getXMLValue(subfolderlist[i], "name").htmlEntities()
			},
			size: {
				innerHTML: parseInt(dhtml.getXMLValue(subfolderlist[i], "size"),10) + _("KB")
			},
			totalsize: {
				innerHTML: parseInt(dhtml.getXMLValue(subfolderlist[i], "totalsize"),10) + _("KB")
			}
		});
		
	}
	this.size_data = dialogData;
	parseFolderSizeData(dialogData);
}
