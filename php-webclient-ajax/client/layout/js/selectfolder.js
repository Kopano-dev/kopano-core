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

function submitFolder() {
	if(module.multipleSelection) {
		// for multiple selection
		var selectedFolderElement;
		var selectedFoldersId = module.selectedMultipleFolders;

		if(typeof selectedFoldersId != "object" || selectedFoldersId.length <= 0) {
			alert(_("Please select at least one folder") + ".");
			return;
		}

		var result = new Object();
		result["subfolders"] = dhtml.getElementById("subfolders_checkbox").checked;
		result["selected_folders"] = new Object();
		result["storeid"] = module.selectedMultipleFolderStoreIds[0];

		for(var key in selectedFoldersId) {
			selectedFolderElement = dhtml.getElementById(selectedFoldersId[key]);

			result["selected_folders"][key] = new Object();
			result["selected_folders"][key]["folderentryid"] = selectedFoldersId[key];
			result["selected_folders"][key]["foldername"] = selectedFolderElement.displayname;
			result["selected_folders"][key]["storeentryid"] = selectedFolderElement.storeid;
		}
	} else {
		// for single selection
		var selectedFolderId = module.selectedFolder;
		
		if(!module.selectedFolder) {
			alert(_("Please select a folder") + ".");
			return;
		}
		
		var selectedFolderElement = dhtml.getElementById(selectedFolderId);
		
		var result = new Object;
		
		result.folderentryid = selectedFolderId;
		result.foldername = selectedFolderElement.displayname;
		result.storeentryid = selectedFolderElement.storeid;
	}

	if(window.resultCallBack(result, window.callBackData))
		window.close();
	else
		window.focus();
}
