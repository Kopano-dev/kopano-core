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

hierarchyselectmodule.prototype = new Module;
hierarchyselectmodule.prototype.constructor = hierarchyselectmodule;
hierarchyselectmodule.superclass = Module.prototype;

function hierarchyselectmodule(id, element)
{
	if(arguments.length > 0) {
		this.init(id, element);
	}
}

/* Creates a hierarchy-select module on the position of 'element', identified
 * by module ID 'id'. The 'storeonly' optional value is TRUE if only full
 * stores should be shown (so shared folders are NOT shown)
 */
hierarchyselectmodule.prototype.init = function(id, element, storeonly, multipleSelection, validatorType)
{
	hierarchyselectmodule.superclass.init.call(this, id, element);
	
	this.defaultstore = false;
	this.stores = new Array();
	this.selectedFolder = false;
	this.selectedFolderStoreId = false;
	this.selectedMultipleFolders = new Array();
	this.selectedMultipleFolderStoreIds = new Array();

	// allow multiple selection of folders
	if(typeof multipleSelection != "undefined" && multipleSelection) {
		this.multipleSelection = multipleSelection;
	} else {
		this.multipleSelection = false;
	}

	// validation function that will be used when validating selection of folders
	if(typeof validatorType != "undefined" && validatorType) {
		this.validatorType = validatorType;
	} else {
		this.validatorType = false;
	}

	if(storeonly)
	    this.storeonly = true;
    else
        this.storeonly = false;

	this.folderEvents = new Object();
	this.folderEvents["click"] = eventHierarchyCopyMoveSelectFolder;

	this.treeEvents = new Object();
	this.treeEvents["ShowBranch"] = eventTreeShowBranch;
	this.treeEvents["SwapFolder"] = eventTreeSwapFolder;
	if(this.multipleSelection) {
		this.treeEvents["SelectMultipleFolder"] = eventTreeSelectMultipleFolders;
	}

	this.contentElement = this.element;	
}

hierarchyselectmodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "list":
			var stores = action.getElementsByTagName("store");

			for(var j = 0; j < stores.length; j++)
			{
				var store = new Object();

				var attributes = stores[j].attributes;
				for(var i = 0; i < attributes.length; i++)
				{
					var item = attributes.item(i);
					
					switch(item.nodeName)
					{
						case "id":
							store["id"] = item.nodeValue;
							break;
						case "subtree":
							store["subtree_entryid"] = item.nodeValue;
							break;
						case "name":
							store["name"] = item.nodeValue;
							break;
						case "type":
							store["type"] = item.nodeValue;
							if (store["type"] == "default"){
								this.defaultstore = store;
							}
							break;
						case "foldertype":
							store["foldertype"] = item.nodeValue;
							break;
					}
				}

				var defaultfolders = stores[j].getElementsByTagName("defaultfolders")[0];
				store["defaultfolders"] = new Object();
				if (defaultfolders && defaultfolders.childNodes){	
					for(var i = 0; i < defaultfolders.childNodes.length; i++)
					{
						var folder = defaultfolders.childNodes[i];
						
						if(folder.firstChild) {
							store["defaultfolders"][folder.nodeName] = folder.firstChild.nodeValue;
						}
					}
				}
				
				var folders = stores[j].getElementsByTagName("folder");
				store["folders"] = new Array();
				
				for(var i = 0; i < folders.length; i++)
				{
					var folder = this.setFolder(folders[i]);
					
					if(folder) {
						if(folder["entryid"] == store["subtree_entryid"]) {
							folder["display_name"] = store["name"];
							store["root"] = folder;
						} else {
							store["folders"].push(folder);
						}
					}
				}
				
				this.stores.push(store);
			}
			
			// sorting storelist
			this.stores.sort(this.sortStores);
			
			this.createHierarchyList();
			
			if(this.selectedFolder) {
				if(this.multipleSelection) {
					this.selectFolderCheckbox(this.selectedMultipleFolders);
				} else {
					this.selectFolder(this.selectedFolder);
				}
			}

			break;
	}
}

hierarchyselectmodule.prototype.setFolder = function(folderobject)
{
	var folder = false;
	
	if(folderobject) {
		var entryid = folderobject.getElementsByTagName("entryid")[0];
		var parent_entryid = folderobject.getElementsByTagName("parent_entryid")[0];
		var display_name = folderobject.getElementsByTagName("display_name")[0];
		var subfolders = folderobject.getElementsByTagName("subfolders")[0];
		var content_count = folderobject.getElementsByTagName("content_count")[0];
		var content_unread = folderobject.getElementsByTagName("content_unread")[0];
		var container_class = folderobject.getElementsByTagName("container_class")[0];
		var store_support_mask = folderobject.getElementsByTagName("store_support_mask")[0];
		
		if(entryid.firstChild && parent_entryid.firstChild && display_name.firstChild) {
			folder = new Object();
			folder["entryid"] = entryid.firstChild.nodeValue;
			folder["parent_entryid"] = parent_entryid.firstChild.nodeValue;
			folder["display_name"] = display_name.firstChild.nodeValue;
			folder["subfolders"] = subfolders.firstChild.nodeValue;
			folder["content_count"] = content_count.firstChild.nodeValue;
			folder["content_unread"] = content_unread.firstChild.nodeValue;
			folder["container_class"] = container_class.firstChild.nodeValue;
			folder["store_support_mask"] = store_support_mask.firstChild.nodeValue;
		}
	}
	
	return folder;
}

/**
 * Function will get folder properties from DOM, based on passed entryid of folder
 * @param			HexString		entryid					entryid of folder
 * @return			Object			selected_folder			folder properties
 */
hierarchyselectmodule.prototype.getFolder = function(entryid)
{
	this.isRootFolder = false;
	this.folderIndex = false;
	this.folderstoreid = false;
	
	for(var i = 0; i < this.stores.length; i++) {
		if(this.stores[i]["root"] && this.stores[i]["root"]["entryid"] == entryid) {
			var folder = this.stores[i]["root"];
			folder["storeid"] = this.stores[i]["id"];
			this.folderstoreid = folder["storeid"];
			this.isRootFolder = true;
			return folder;
		} else {
			for(var j = 0; j < this.stores[i]["folders"].length; j++)
			{
				var folder = this.stores[i]["folders"][j];

				if(folder["entryid"] == entryid) {
					this.folderIndex = j;
					this.folderstoreid = this.stores[i]["id"];
					folder["storeid"] = this.folderstoreid;
					return folder;
				}
			}
		}
	}
	
	return false;
}

hierarchyselectmodule.prototype.createHierarchyList = function(load)
{
	this.deleteLoadMessage();
	
	// Loop through all stores
	for(var i = 0; i < this.stores.length; i++)
	{
		var store = this.stores[i];
		var tree = new Tree(this.id, this.contentElement, this.treeEvents, this.multipleSelection);
		
		// If required, skip shared folders
		if(store["foldertype"] != "all" && this.storeonly)
		    continue;
		
		store["tree"] = tree;

		var attributes = new Object;
		attributes.storeid = store["id"];
		attributes.displayname = store["root"]["display_name"];

		tree.createNode(store["root"]["parent_entryid"], store["root"]["entryid"], true, store["root"]["display_name"], "store", store["root"]["subfolders"], (store["root"]["entryid"] == this.defaultstore["root"]["entryid"]?true:false), this.folderEvents, null, attributes);
		
		// Loop through all folders in the store
		for(var j = 0; j < store["folders"].length; j++)
		{
			var folder = store["folders"][j];
			var iconClass = false;
			
			// Handle special folders
			for(var folderType in store["defaultfolders"])
			{
				if(store["defaultfolders"][folderType] == folder["entryid"]) {
					iconClass = folderType;
				}
			}
			
			if(!iconClass) {
				switch(folder["container_class"])
				{
					case "IPF.Appointment":
						iconClass = "calendar";
						break;
					case "IPF.Contact":
						iconClass = "contact";
						break;
					case "IPF.Task":
						iconClass = "task";
						break;
					case "IPF.StickyNote":
						iconClass = "note";
						break;
					default:
						iconClass = "mail";
						break;
				}
			}
		
			attributes = new Object;
			attributes.storeid = store["id"];
			attributes.displayname = folder["display_name"];
			
			// Create the actial node in the tree 
			tree.createNode(folder["parent_entryid"], folder["entryid"], false, folder["display_name"], iconClass, folder["subfolders"], false, this.folderEvents, null, attributes);
		}
		//to check weather the parent folders of selected folder is open or not.
		if(this.selectedFolder){
			var selectedNode = tree.getNode(this.selectedFolder);
			if (selectedNode && selectedNode.attributes.storeid == store.id) {
				// as public folders by default are not expanded in dialog window,so expand selected folder's root node 
				// if the selected folders's store is public
				if( store.type == "public")
					tree["root"]["open"] = true;

				var parentNode	= tree.getNode(selectedNode["parentid"]);
				while((parentNode != false) && (parentNode["id"] != store.root["entryid"])){
					parentNode["open"] =(parentNode["hasChildNodes"]=="1")? true: false;
					selectedNode = parentNode;
					parentNode = tree.getNode(selectedNode["parentid"]);
				}
			}
		}
		tree.buildTree();
	}

}

hierarchyselectmodule.prototype.list = function()
{
	webclient.xmlrequest.addData(this, "list");
	webclient.xmlrequest.sendRequest();
	this.loadMessage();
}

hierarchyselectmodule.prototype.selectFolder = function(entryid)
{
	var element = dhtml.getElementById(entryid);
	if (element){
		var folderName = element.getElementsByTagName("div")[1];
		if(folderName) {
			dhtml.executeEvent(folderName, "click");
		} else {
			eventHierarchyCopyMoveSelectFolder(this, folderName, false);
		}
		this.selectedFolderStoreId = element.storeid;
	}

	this.selectedFolder = entryid;
}

/**
 * Function will automatically selects checkbox for showing
 * selected folders, and it will also unfold tree to show selected folders
 * @param		Object		entryids		selected folders entryids
 */
hierarchyselectmodule.prototype.selectFolderCheckbox = function(entryids)
{
	for(var key in entryids) {
		var element = dhtml.getElementById(entryids[key]["folderentryid"]);
		if (element){
			var checkbox = dhtml.getElementById("folder_select", "input", element);
			var folderName = element.getElementsByTagName("div")[1];
			if(checkbox) {
				if(window.BROWSER_IE) {
					/**
					 * IE doesn't automatically selects checkbox
					 * when click event is trigerred, so manually do it
					 */
					checkbox.checked = true;
				}
				dhtml.executeEvent(checkbox, "click");
			}

			// also unfold the parent tree node if its directly not visible
			var branchElement = element.parentNode;
			var parentElem;

			// recursively unfold tree structure
			while(branchElement.id.substring(0, 6) == "branch") {
				parentElem = dhtml.getElementById(branchElement.id.substring(6));
				var folderStateElement = parentElem.firstChild;
				if(dhtml.hasClassName(folderStateElement, "folderstate_close")) {
					dhtml.executeEvent(folderStateElement, "click");
				}

				branchElement = parentElem.parentNode;
				if(!branchElement) {
					break;
				}
			}
		}
	}
}

/**
 * Function will call validation function based on functionality type
 * @param		HTMLElement		folderElement		currently selected folder
 * @param		array			multipleStoreid		array of all storeids of selected folders
 * @param		array			multipleEntryid		array of all entryids of selected folders
 * @return		Boolean								true - if selection is valid
 *													false - if selection not valid
 */
hierarchyselectmodule.prototype.validateSelection = function(folderElement, multipleStoreid, multipleEntryid)
{
	switch(this.validatorType) {
		case "search":
			return this.validateSearchSelection(folderElement, multipleStoreid, multipleEntryid);
		default:
			// no validation
			return true;
	}
}

/**
 * Function will validate selection based on currently selected folder
 * and previously selected folder
 * @param		HTMLElement		folderElement		currently selected folder
 * @param		array			multipleStoreid		array of all storeids of selected folders
 * @param		array			multipleEntryid		array of all entryids of selected folders
 * @return		Boolean								true - if selection is valid
 *													false - if selection not valid
 */
hierarchyselectmodule.prototype.validateSearchSelection = function(folderElement, multipleStoreid, multipleEntryid)
{
	var selected_folder = false;
	var storeid = false;
	var entryid = false;

	// if there are multiple folders selected then check for only first folder
	// because storeid of all folders will be same
	if(multipleStoreid.length > 0 && multipleEntryid.length > 0) {
		storeid = multipleStoreid[0];
		entryid = multipleEntryid[0];
	}

	for(var index=0; index<multipleEntryid.length; index++) {
		if(folderElement.id === multipleEntryid[index]) {
			// we are deselecting already selected folder so don't do any validations
			return true;
		}
	}

	// don't allow selection in different stores
	if(storeid && folderElement.storeid !== storeid) {
		alert(_("When searching this type of folder, you can not search other folders at the same time") + ".");
		return false;
	}

	// don't allow multiple folder selection in store if it doesn't support search folders
	if(storeid && entryid) {
		selected_folder = this.getFolder(entryid);

		// check for search capability of store
		if((selected_folder["store_support_mask"] & STORE_SEARCH_OK) != STORE_SEARCH_OK) {
			alert(_("The folder you selected does not let you search folders at the same time. To search other folders, clear the checkbox next to this folder") + ".");
			return false;
		}
	}
	return true;
}

/**
 * Function will enable or disable subfolders option based on currently selected folder
 * @param		array		multipleStoreid		array of all storeids of selected folders
 * @param		array		multipleEntryid		array of all entryids of selected folders
 */
hierarchyselectmodule.prototype.toggleSubfoldersOption = function(multipleStoreid, multipleEntryid)
{
	var storeid = false;
	var entryid = false;

	// if there are multiple folders selected then check for only first folder
	// because storeid of all folders will be same
	if(multipleStoreid.length > 0 && multipleEntryid.length > 0) {
		storeid = multipleStoreid[0];
		entryid = multipleEntryid[0];
	}

	if(storeid && entryid) {
		var selected_folder = this.getFolder(entryid);

		if(selected_folder && (selected_folder["store_support_mask"] & STORE_SEARCH_OK) == STORE_SEARCH_OK) {
			// enable search subfolders option
			var subFoldersCheckboxElem = dhtml.getElementById("subfolders_checkbox");
			var subFoldersLabelElem = dhtml.getElementById("subfolders_label");
			subFoldersCheckboxElem.disabled = false;
			dhtml.removeClassName(subFoldersLabelElem, "disabled_text");
		} else {
			// disable search subfolders option
			var subFoldersCheckboxElem = dhtml.getElementById("subfolders_checkbox");
			var subFoldersLabelElem = dhtml.getElementById("subfolders_label");
			subFoldersCheckboxElem.disabled = true;
			subFoldersCheckboxElem.checked = false;
			dhtml.addClassName(subFoldersLabelElem, "disabled_text");
		}
	} else {
		// if no folder is selected then by default disable subfolders option
		var subFoldersCheckboxElem = dhtml.getElementById("subfolders_checkbox");
		var subFoldersLabelElem = dhtml.getElementById("subfolders_label");
		subFoldersCheckboxElem.disabled = true;
		subFoldersCheckboxElem.checked = false;
		dhtml.addClassName(subFoldersLabelElem, "disabled_text");
	}
}

/**
* grab sortStores function from hierarchymodule
*/
if (typeof hierarchymodule != "undefined")
	hierarchyselectmodule.prototype.sortStores = hierarchymodule.prototype.sortStores;

/**
 * Global Event Function
 * Function will be called when user clicks on checkbox for a folder
 * @param		Object			moduleObject		module object
 * @param		HTMLElement		element				html element
 * @param		EventObject		event				event object
 */
function eventTreeSelectMultipleFolders(moduleObject, element, event)
{
	if(element){
		var folderElement = element.parentNode;
		var folderName = element.nextSibling;

		if(!this.validateSelection(folderElement, moduleObject.selectedMultipleFolderStoreIds, moduleObject.selectedMultipleFolders)) {
			return false;
		}

		if(element.checked) {
			dhtml.array_push(moduleObject.selectedMultipleFolders, folderElement.id, false, true);
			dhtml.array_push(moduleObject.selectedMultipleFolderStoreIds, folderElement.storeid, false, true);
		} else {
			dhtml.array_push(moduleObject.selectedMultipleFolders, folderElement.id, true);
			dhtml.array_push(moduleObject.selectedMultipleFolderStoreIds, folderElement.storeid, true);
		}

		// for backward compatibility call this method, don't want to disturb existing
		// functionality so using dirty way of calling a event function
		eventHierarchyCopyMoveSelectFolder(moduleObject, folderName, event);

		// enable or disable subfolders option
		moduleObject.toggleSubfoldersOption(moduleObject.selectedMultipleFolderStoreIds, moduleObject.selectedMultipleFolders);
	}
}

function eventHierarchyCopyMoveSelectFolder(moduleObject, element, event)
{
	if(!event) {
		event = new Object();
	}

	if(event.button == 0 || event.button == 1 || !event.button) {
		if(moduleObject.selectedFolder) {
			var folder = dhtml.getElementById(moduleObject.selectedFolder);
			if(folder) {
				var folderIcon = folder.getElementsByTagName("div")[1];
				var folderName = folderIcon.getElementsByTagName("span")[0];
				folderName.className = "folder_title";
			}
		}
		
		moduleObject.selectedFolder = element.parentNode.id;
		moduleObject.selectedFolderStoreId = element.parentNode.storeid;
		//trigger the internal event register on onchangefolder in hierarchy.
		moduleObject.sendEvent("changefolder");

		var folderName = element.getElementsByTagName("span")[0];
		folderName.className = "folder_title_selected";
	}
}
