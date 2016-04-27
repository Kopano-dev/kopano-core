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

hierarchymodule.prototype = new Module;
hierarchymodule.prototype.constructor = hierarchymodule;
hierarchymodule.superclass = Module.prototype;

function hierarchymodule(id, element)
{
	if(arguments.length > 0) {
		this.init(id, element);
	}
}

hierarchymodule.prototype.init = function(id, element)
{
	hierarchymodule.superclass.init.call(this, id, element);
	
	this.stores = new Array();
	this.defaultstore = false;
	this.openedFolders = new Object();
	this.getOpenedFolders();
	this.selectedFolder = false;
	// this flag is set when selected folder is same as selectedContextFolder
	this.selectedFolderFlag = false;

	this.folderIndex = 0;
	this.folderEvents = new Object();
	this.folderEvents["mousedown"] = eventHierarchySelectFolder;
	this.folderEvents["click"] = eventHierarchyChangeFolder;
	this.folderEvents["contextmenu"] = eventHierarchyContextMenu;

	this.treeEvents = new Object();
	this.treeEvents["ShowBranch"] = eventHierarchyShowBranch;
	this.treeEvents["SwapFolder"] = eventTreeSwapFolder;

	this.setTitle(_("Folder list"), _("All folders"));
	this.contentElement = dhtml.addElement(this.element, "div", false, "hierarchy");
	
	this.sharedFoldersElement = dhtml.addElement(this.element, "div", false, "shared_folder");
	this.hideSharedFolderActions();
	var sfLink = dhtml.addElement(this.sharedFoldersElement,"a", false, false, _("Open shared folders") + "...");
	dhtml.addEvent(this.id, sfLink, "click", eventSharedFoldersClick);
	
	dhtml.addElement(this.sharedFoldersElement,"br");

	if(MUC_AVAILABLE){
		var mucLink = dhtml.addElement(this.sharedFoldersElement,"a", false, false, _("Open Multi User Calendar"));
		dhtml.addEvent(this.id, mucLink, "click", eventAdvancedCalendarClick);
	}
	webclient.pluginManager.triggerHook('client.module.hierarchymodule.sharedFoldersPane.buildup', {sharedFoldersElement: this.sharedFoldersElement});
	
	dhtml.addEvent(this.id, document.body, "mouseup", eventHierarchyCheckSelectedContextFolder);
	dhtml.addEvent(this.id, this.contentElement, "mousemove", eventHierarchyMouseMoveScroll);

	// List of keys handled by hierarchymodule
	this.keys["new"] = KEYS["new"];
	this.keys["open"] = KEYS["open"];
}

hierarchymodule.prototype.execute = function(type, action)
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
						case "mailbox_owner":
							store["mailbox_owner_entryid"] = item.nodeValue;
							break;
						case "name":
							store["name"] = item.nodeValue;
							break;
						case "type":
							store["type"] = item.nodeValue;
							if (store["type"] == "default"){
								this.defaultstore = store;
								webclient.menu.defaultstoreid = this.defaultstore["id"];
							}
							break;
						case "username":
							store["username"] = item.nodeValue;
							break;
						case "emailaddress":
							store["emailaddress"] = item.nodeValue;
							break;
						case "foldertype":
							store["foldertype"] = item.nodeValue;
							break;
						case "userfullname":
							store["userfullname"] = item.nodeValue;
							break;
					}
				}

				var defaultfolders = stores[j].getElementsByTagName("defaultfolders")[0];
				store["defaultfolders"] = new Object();
				
				if(defaultfolders) {
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
				
				var newStore = true;
				for(var i=0;i<this.stores.length;i++){
					if(this.stores[i].id == store.id && this.stores[i].foldertype == store.foldertype){
						this.stores[i] = store;
						newStore = false;
					}
					
					if(store.type=="other" && this.stores[i].type=="other" && this.stores[i].username.toLowerCase()==store.username.toLowerCase() && store.foldertype=="all"){
						this.stores[i] = store;
						newStore = false;
					}

					// we have found that we are updating an older store, so break the loop here
					if(newStore === false) {
						break;
					}
				}

				if(store.type == "other" && store.foldertype == "all") {
					// if we are adding whole store of an user then we should also remove folders that were added for same user
					for(var i=0; i<this.stores.length; i++) {
						if(this.stores[i].type == "other" && this.stores[i].foldertype != "all" && this.stores[i].username.toLowerCase() == store.username.toLowerCase()) {
							this.stores.splice(i, 1);
						}
					}
				}

				if (newStore){
					this.stores.push(store);
				}
			}

			// sorting storelist
			this.stores.sort(this.sortStores);
			// Rearrange the archive stores in the list to get the below the user's original store
			this.reorderArchiverStores();
			
			this.createHierarchyList(true);
			this.showSharedFolderActions();
			break;
		case "folder":
			var folder = action.getElementsByTagName("folder")[0];
			
			var store_entryid = folder.getElementsByTagName("store_entryid")[0];
			var entryid = folder.getElementsByTagName("entryid")[0];
			var deleteFolder = folder.getElementsByTagName("folderdelete")[0];

			// check if this folder must be deleted
			if(deleteFolder && deleteFolder.firstChild && entryid.firstChild) {
				for(var i = 0; i < this.stores.length; i++)
				{
					for(var j = 0; j < this.stores[i]["folders"].length; j++)
					{
						if(this.stores[i]["folders"][j]["entryid"] == entryid.firstChild.nodeValue) {
							this.stores[i]["tree"].deleteNode(this.stores[i]["folders"][j]["entryid"], true);
							this.stores[i]["folders"].splice(j, 1);
						}
					}
				}
				
				this.createHierarchyList();

			} else if(entryid.firstChild) { // folder is added or changed

				var folderobject = this.getFolder(entryid.firstChild.nodeValue);

				for(var i = 0; i < this.stores.length; i++)
				{
					if(this.stores[i]["id"] == store_entryid.firstChild.nodeValue) {
						if(!folderobject) {
							// folder is added
							this.stores[i]["folders"].push(this.setFolder(folder));
							this.changeFolder(this.stores[i]["tree"], "add", this.setFolder(folder));
							webclient.xmlrequest.addData(this, "list");
						} else {
							// folder is changed
							var changedFolder = this.setFolder(folder);
							
							if(!this.isRootFolder) {
								this.stores[i]["folders"][this.folderIndex] = changedFolder;
							} else {
								this.stores[i]["root"] = changedFolder;
								webclient.xmlrequest.addData(this, "list");
							}
							
							var changeType = "change";
							if(folderobject["parent_entryid"] != changedFolder["parent_entryid"]) {
								// selected parentFolder
								var parentFolder = dhtml.getElementById(this.stores[i].tree.getNode(dhtml.getTextNode(entryid,"")).parentid);
								this.selectedFolderFlag = (this.selectedFolder == this.selectedContextFolder)? false : true; 
								if(parentFolder){
									parentFolder = parentFolder.childNodes[1];
									eventHierarchySelectFolder(this, parentFolder, false);
									//change folder if the selected folder is not same as parent folder of deleted folder
									if(!this.selectedFolderFlag)
										eventHierarchyChangeFolder(this, parentFolder, false);
								}
								changeType = "move";
							}
							
							this.setDocumentTitle(changedFolder);
							this.changeFolder(this.stores[i]["tree"], changeType, changedFolder);
						}
					}
				}
			}
			break;

		case "closesharedfolder":
			// FIXME: need to check the foldertype and remove the right folder/store
			// for now, a dirty reload is done...

			this.stores = new Array();
			this.list();

			/*
			var username = action.getElementsByTagName("username");
			var foldertype = action.getElementsByTagName("foldertype");
			if (username && username[0] && username[0].firstChild){
				username = username[0].firstChild.nodeValue;
				var deleteStore = null;
				for(var i=0; i<this.stores.length; i++){
					// delete from view
					if (this.stores[i].username && this.stores[i].username == username) {
						this.stores[i].tree.deleteNode(this.stores[i].subtree_entryid, true);
						deleteStore = i;
					}
				}
				if (deleteStore!=null){
					this.stores.splice(deleteStore,1);
				}
			}
			*/
			break;
		case "error":
			var errors = action.getElementsByTagName("error")[0];
			var hresult = dhtml.getTextNode(errors.getElementsByTagName("hresult")[0],"");
			var username = dhtml.getTextNode(errors.getElementsByTagName("username")[0],"").toLowerCase();
			var errorString = dhtml.getTextNode(errors.getElementsByTagName("message")[0],"");
			if(username.length > 0){
				webclient.settings.deleteSetting("otherstores/"+username);
			}
			if(errorString.length > 0){
				alert(errorString);
			}
			break;
	}
}

/**
 * This function is used to sort the store list
 * returns -1 when storeA must be before storeB
 * returns 1 when storeA must be after storeB
 */
hierarchymodule.prototype.sortStores = function(storeA, storeB)
{
	// sort default store always as first
	if(storeA["type"]=="default") return -1;
	if(storeB["type"]=="default") return 1;

	// sort public store always as last
	if(storeA["type"]=="public") return 1;
	if(storeB["type"]=="public") return -1;

	// sort archive at the end just before the public store
	// When comparing two archive stores we want them to sort on name
	if(storeA["type"]=="archive" && storeB["type"]!="archive") return 1;
	if(storeA["type"]!="archive" && storeB["type"]=="archive") return -1;

	// sort other folders after other stores
	if(storeA["foldertype"]!="all" && storeB["foldertype"]=="all") return 1;
	if(storeA["foldertype"]=="all" && storeB["foldertype"]!="all") return -1;

	// else sort on name
	if(storeA["name"] > storeB["name"]) return 1;
	if(storeA["name"] < storeB["name"]) return -1;
	return 0;
}

/**
 * Will rearrange the archive stores so that each archive store is directly placed below the user's 
 * original store. For that we extract the archive stores from the list and insert them at the 
 * proper places by comparing the mailbox_owner_entrid properties of both stores.
 * It will sort this classes' this.stores property.
 */
hierarchymodule.prototype.reorderArchiverStores = function(){
	/*
	 * The compare function sortStores has the archivers sorted next to eachother. We need to find 
	 * the range within the list of stores to be able to extract them. For that we have to find the 
	 * start position of that range and the number of archive stores in the list.
	 */ 
	var start, num = 0;
	for(var i=0;i<this.stores.length;i++){
		if(this.stores[i]['type'] == 'archive'){
			if(!start){
				start = i;
			}
			num++;
		}
	}
	// Extract the archive stores from the list to insert them at the correct places
	var archStores = this.stores.splice(start,num);
	// For each extracted archive store we check at what position they need to be inserted
	for(var i=0;i<archStores.length;i++){
		for(var j=0;j<this.stores.length;j++){
			// Compare the two entryids of the original store and the archiver to found out if they are linked
			if(compareEntryIds(archStores[i]['mailbox_owner_entryid'], this.stores[j]['mailbox_owner_entryid'])){
				// Break out of the look if the entryids match so we can use the j variable as target location
				break;
			}
		}
		// Add the archive store after the found owner stor.  If not found it will add it at the end,
		// but that is not likely as the archive store cannot be opened without the original store.
		this.stores.splice(j+1,0,archStores[i]);
	}
}

hierarchymodule.prototype.isDefaultStore = function(storeid)
{
	for(var i = 0; i < this.stores.length; i++)
	{
		if (this.stores[i]["id"] == storeid){
			if (this.stores[i]["type"] && this.stores[i]["type"] == "default"){
				return true;
			}	
		}
	}
	return false;
}

hierarchymodule.prototype.isPublicStore = function(storeid)
{
	for(var i = 0; i < this.stores.length; i++)
	{
		if (this.stores[i]["id"] == storeid){
			if (this.stores[i]["type"] && this.stores[i]["type"] == "public"){
				return true;
			}	
		}
	}
	return false;
}

hierarchymodule.prototype.isSharedFolder = function(storeid)
{
	for(var i = 0; i < this.stores.length; i++)
	{
		if (this.stores[i]["id"] == storeid){
			if (this.stores[i]["type"] && this.stores[i]["type"] == "other" && this.stores[i]["foldertype"] != "all"){
				return true;
			}	
		}
	}
	return false;
}

hierarchymodule.prototype.isSharedStore = function(storeid)
{
	for(var i = 0; i < this.stores.length; i++)
	{
		if (this.stores[i]["id"] == storeid){
			if (this.stores[i]["type"] && this.stores[i]["type"] == "other"){
				return true;
			}	
		}
	}
	return false;
}

hierarchymodule.prototype.setFolder = function(folderobject)
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
		var access = {
			"modify": folderobject.getElementsByTagName("modify")[0],
			"read": folderobject.getElementsByTagName("read")[0],
			"delete": folderobject.getElementsByTagName("delete")[0],
			"create_hierarchy": folderobject.getElementsByTagName("create_hierarchy")[0],
			"create_contents": folderobject.getElementsByTagName("create_contents")[0]
		};
		var rights = {
			"deleteowned": folderobject.getElementsByTagName("deleteowned")[0],
			"deletedany": folderobject.getElementsByTagName("deletedany")[0]
		};
		
		
		if(entryid.firstChild && parent_entryid.firstChild) {
			folder = new Object();
			folder["entryid"] = dhtml.getTextNode(entryid, "");
			folder["parent_entryid"] = dhtml.getTextNode(parent_entryid,"");
			folder["display_name"] = dhtml.getTextNode(display_name,"");
			folder["subfolders"] = dhtml.getTextNode(subfolders,"");
			folder["content_count"] = dhtml.getTextNode(content_count, "0");
			folder["content_unread"] = dhtml.getTextNode(content_unread, "0");
			folder["container_class"] = dhtml.getTextNode(container_class,"IPF.Note");
			folder["store_support_mask"] = dhtml.getTextNode(store_support_mask,"0");

			folder["access"] = new Object();
			folder["access"]["modify"] = dhtml.getTextNode(access["modify"], "0");
			folder["access"]["read"] = dhtml.getTextNode(access["read"], "0");
			folder["access"]["delete"] = dhtml.getTextNode(access["delete"], "0");
			folder["access"]["create_hierarchy"] = dhtml.getTextNode(access["create_hierarchy"], "0");
			folder["access"]["create_contents"] = dhtml.getTextNode(access["create_contents"], "0");

			folder["rights"] = new Object();
			folder["rights"]["deleteowned"] = dhtml.getTextNode(rights["deleteowned"], "0");
			folder["rights"]["deleteany"] = dhtml.getTextNode(rights["deleteany"], "0");

			if (folder["entryid"]==""){
				folder = false;
			}
		}
	}
	
	return folder;
}

hierarchymodule.prototype.createHierarchyList = function(load)
{
	this.deleteLoadMessage();
	
	for(var i = 0; i < this.stores.length; i++)
	{
		var store = this.stores[i];
		if(store["root"]){
			// whem this is the default store (IPM_SUBTREE) and this is the first time, make sure the folder is expanded
			if (store["root"]["entryid"] == this.defaultstore["root"]["entryid"] && typeof this.openedFolders[store["root"]["entryid"]] == "undefined"){
				this.openedFolders[store["root"]["entryid"]] = "open";
			}
			
			// folder_extra is to display count of unread item in shared folders
			var folder_extra = false;
			if(store["type"] == "other" && Number(store["root"]["content_unread"]) > 0){
				folder_extra = new Object();
				folder_extra["class"] = "unread_count";
				folder_extra["text"] = "("+store["root"]["content_unread"]+")";
			}
			
			var tree = new Tree(this.id, this.contentElement, this.treeEvents);
			this.tree = tree;
			store["tree"] = tree;
			tree.createNode(store["root"]["parent_entryid"], 
							store["root"]["entryid"], 
							true, 
							store["root"]["display_name"], 
							"store folder_icon_"+store["foldertype"], 
							store["root"]["subfolders"], 
							(this.openedFolders[store["root"]["entryid"]] == "open"?true:false), 
							this.folderEvents,
							folder_extra
							);
			
			for(var j = 0; j < store["folders"].length; j++)
			{
				var folder = store["folders"][j];
				var iconClass = false;
				var dropNotAllowed = false;
				
				for(var folderType in store["defaultfolders"])
				{
					if(store["defaultfolders"][folderType] == folder["entryid"]) {
						iconClass = folderType;
						if(folderType == "syncissues" || folderType == "conflicts" || folderType == "localfailures" || folderType == "serverfailures"){
							dropNotAllowed = true;
						}
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
			
				// Check to see if a counter needs to be added by the tree for either
				// unread messages or for the drafts folder the content_count.
				var folder_extra = false;
				if ((store["defaultfolders"]["drafts"] == folder["entryid"]) || (store["defaultfolders"]["outbox"] == folder["entryid"])){
					if(Number(folder["content_count"]) > 0){
						folder_extra = new Object();
						folder_extra["class"] = "total_count";
						folder_extra["text"] = "["+folder["content_count"]+"]";
					}
				}else if (Number(folder["content_unread"]) > 0){
					folder_extra = new Object();
					folder_extra["class"] = "unread_count";
					folder_extra["text"] = "("+folder["content_unread"]+")";
				}
	
				tree.createNode(folder["parent_entryid"], 
								folder["entryid"], 
								false, 
								folder["display_name"], 
								iconClass, 
								folder["subfolders"], 
								(this.openedFolders[folder["entryid"]] == "open"?true:false), 
								this.folderEvents, 
								folder_extra,
								null,
								dropNotAllowed
								);
			}
			
			tree.buildTree();
			tree.addEventHandler("statechange", this.updateOpenedFolder, this);
		}else{
			//delete user if you have no access
			webclient.settings.deleteSetting("otherstores/"+store["username"].toLowerCase());
		}
	}
	
	this.selectLastFolder(load);
	
	// Now hierarchy list is created, so bind keycontroller.
	webclient.inputmanager.addObject(this, this.element);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["new"], "keyup", eventHierarchyKeyCtrlNewItem, false);
	webclient.inputmanager.bindKeyControlEvent(this, this.keys["open"], "keyup", eventHierarchyKeyCtrlOpenFolder, false);
}

/*
 * Makes sure that the correct folder is selected. If 'load' is true,
 * also sends a signal to the maillistmodule to load that folder.
 */
hierarchymodule.prototype.selectLastFolder = function(load)
{
	this.selectFolder(load, this.selectedFolder);
}

/*
 * Opens folder in hierarchy tree
 * @param boolean load if true then open folder else select folder
 * @param string entryid entryid of folder which is to be opened/select
 */
hierarchymodule.prototype.selectFolder = function(load, entryid){
	var element = dhtml.getElementById(entryid);
	if(!element) {
		var store = this.defaultstore;
		if(store) {
			element = dhtml.getElementById(store["defaultfolders"]["inbox"]);
			if (webclient.settings && webclient.settings.get("global/startup/folder","inbox")=="last"){
				element = dhtml.getElementById(webclient.settings.get("global/startup/folder_lastopened",store["defaultfolders"]["inbox"]));
			}else if (webclient.settings.get("global/startup/folder","inbox")=="today"){	
				element = dhtml.getElementById(webclient.settings.get("global/startup/folder_lastopened", store["subtree_entryid"]));
			}
			if (!element){
				element = dhtml.getElementById(store["defaultfolders"]["inbox"]);
			}
		}
	}
	var folderName = element.getElementsByTagName("div")[1];
	if(folderName && load) {
		eventHierarchySelectFolder(this, folderName, false);
		eventHierarchyChangeFolder(this, folderName, false);
	} else {
		eventHierarchySelectFolder(this, folderName, false);
	}
}

hierarchymodule.prototype.changeFolder = function(treeObject, type, folder)
{
	switch(type)
	{
		case "add":
			var node = treeObject.createNode(folder["parent_entryid"], folder["entryid"], false, folder["display_name"], "mail", false, false, this.folderEvents, folder["extra"]);
			if (!treeObject.addNode(node)){
				/* Adding of this folder failed, reload complete hierarchy list */
				this.list(true);
				return;
			}			
			this.openedFolders[folder["parent_entryid"]] = "open";

			break;
		case "change":
			var node = treeObject.getNode(folder["entryid"]);
			
			if(node) {
				node["value"] = folder["display_name"];
				if (parseInt(node["hasChildNodes"],10)==1 && parseInt(folder["subfolders"],10)==-1){
					// no subfolders anymore, delete all subnodes first
					var nodes = treeObject.getChildren(node, true);
					for(var n=0; n<nodes.length; n++){
						treeObject.deleteNode(nodes[n].id, true);
					}					
				}
				node["hasChildNodes"] = folder["subfolders"];
				node["open"] = (this.openedFolders[folder["entryid"]] == "open")?true:false;


				// Get store object
				var folderObj = this.getFolder(folder["entryid"]);
				var store = this.getStore(folderObj["storeid"]);
				// Check to see if a counter needs to be added by the tree for either
				// unread messages or for the drafts folder the content_count.
				node["extra"] = false;
				if (store && (store["defaultfolders"]["drafts"] == folder["entryid"]) || (store["defaultfolders"]["outbox"] == folder["entryid"])){
					if(Number(folder["content_count"]) > 0){
						node["extra"] = new Object();
						node["extra"]["class"] = "total_count";
						node["extra"]["text"] = "["+folder["content_count"]+"]";
					}
				}else if (Number(folder["content_unread"]) > 0){
					node["extra"] = new Object();
					// if selected folder is updated with unread count we have to 
					// set the correct font color while building the hierarchy tree.
					if(compareEntryIds(this.selectedFolder, node["id"]))
						node["extra"]["class"] = "selectedfolder_unread_count" ;
					else
						node["extra"]["class"] = "unread_count" ;
					node["extra"]["text"] = "("+folder["content_unread"]+")";
				}
				
				treeObject.changeNode(node);
			}
			
			if(folder["entryid"] == this.selectedFolder) {
				this.setNumberItems(folder["content_count"], folder["content_unread"]);
			}
			break;
		case "move":
			var node = treeObject.getNode(folder["entryid"]);
			if(node) {
				node["parentid"] = folder["parent_entryid"];
				node["value"] = folder["display_name"];
				node["hasChildNodes"] = folder["subfolders"];
				node["open"] = false;
				
				node["extra"] = false;
				if (folder["content_unread"]>0){
					node["extra"] = new Object();
					node["extra"]["class"] = "unread_count";
					node["extra"]["text"] = "("+folder["content_unread"]+")";
				}
				//check the parentNode to see wheather the node is deleted its so that expanding behaviour of node is taken care
				var parentNode = treeObject.getNode(folder["parent_entryid"]);
				if(parentNode && parentNode["id"] == this.defaultstore.defaultfolders.wastebasket){
					parentNode["open"] = (this.openedFolders[folder["parent_entryid"]] == "open")?true:false;
					
					//if there is no child element of the parentNode then we need to remove the branch container and set the folderstate as close 
					if(parentNode["hasChildNodes"] == "-1"){
						parentNode["open"] = false;		
					
						var branchElement = dhtml.getElementById("branch"+parentNode["id"], "div", this.contentElement);
						if(branchElement)
							dhtml.deleteAllChildren(branchElement);
							dhtml.deleteElement(branchElement);
					}
				}
				treeObject.moveNode(node, parentNode["open"]);
			}
			break;
	}

	// update hiearchylist to see all changes
	if(typeof dragdrop != "undefined") {
		dragdrop.updateTargets("folder");
	}
	this.setOpenedFolders();
}

hierarchymodule.prototype.list = function(reset)
{
	var data = false;
	if(reset) {
		data = "reset";
	}

	webclient.xmlrequest.addData(this, "list", data);
	this.hideSharedFolderActions();
	this.loadMessage();
}
/**
 * Function which sets the footer info "125 item(s) - (4) new".
 * @param integer totalNumberItems total number of items in the folder
 * @param integer numberNewItems total number of new items in the folder 
 */  
hierarchymodule.prototype.setNumberItems = function(totalNumberItems, numberNewItems)
{
	var numberitems = dhtml.getElementById("numberitems");
	while(numberitems.hasChildNodes())
	{
		numberitems.removeChild(numberitems.childNodes[0]);
	}

	var number = document.createElement("span");
	number.innerHTML = totalNumberItems + " " + _("items") + " <b>&#183;</b> ";
	numberitems.appendChild(number);
	
	var newItems = document.createElement("span");
	newItems.innerHTML = "(" + numberNewItems + ") " + _("new");
	numberitems.appendChild(newItems);
}

hierarchymodule.prototype.createFolder = function(name, type, parent_entryid)
{
	var folder;
	if (!parent_entryid){
		folder = this.getFolder(this.selectedContextFolder);
	} else {
		folder = this.getFolder(parent_entryid);
	}

	if(folder) {
		var data = new Object();
		data["store"] = this.folderstoreid;
		data["parententryid"] = folder["entryid"];
		data["name"] = name;
		data["type"] = type;
		
		webclient.xmlrequest.addData(this, "add", data);
	}
	
	eventHierarchyCheckSelectedContextFolder(this);
}

hierarchymodule.prototype.modifyFolder = function(name, entryid)
{
	var folder;
	if (!entryid){
		folder = this.getFolder(this.selectedContextFolder);
	} else {
		folder = this.getFolder(entryid);
	}

	if(folder) {
		var data = new Object();
		data["store"] = this.folderstoreid;
		data["entryid"] = folder["entryid"];
		data["name"] = name;
		
		webclient.xmlrequest.addData(this, "modify", data);
	}
	eventHierarchyCheckSelectedContextFolder(this);
}

hierarchymodule.prototype.deleteFolder = function(folder)
{
	if (!folder){
		folder = this.getFolder(this.selectedFolder);
	}


	if(folder) {
		if(confirm(_("Are you sure you want to delete") + " " + folder["display_name"] + '?')) {
			var data = new Object();
			data["store"] = this.folderstoreid;
			data["parententryid"] = folder["parent_entryid"];
			data["entryid"] = folder["entryid"];
			
			// delete folder settings
			var path = "folders/entryid_"+folder["entryid"];
			webclient.settings.deleteSetting(path);
			
			// send request to server(php)
			webclient.xmlrequest.addData(this, "delete", data);
		}
	}
}

hierarchymodule.prototype.emptyFolder = function(folder)
{
	if (folder) {
		if(confirm(_("Are you sure you want to empty %s?").sprintf(folder["display_name"]))) {
			var data = new Object();
			data["store"] = folder["storeid"];
			data["entryid"] = folder["entryid"];
			webclient.xmlrequest.addData(this, "emptyfolder", data);
			this.reloadListModule(folder["entryid"], false);
		}
	}
}

hierarchymodule.prototype.addToFavorite = function(entryid, favoritename, flags)
{
	var data = new Object();
	data["store"] = this.folderstoreid;
	data["entryid"] = entryid;
	data["favoritename"] = favoritename;
	data["flags"] = flags;
	
	webclient.xmlrequest.addData(this, "addtofavorite", data);
}

hierarchymodule.prototype.copyFolder = function(destination_entryid, destination_storeid, type, entryid)
{	
	var folder;
	if (!entryid){
		folder = this.getFolder(this.selectedContextFolder);
	} else {
		folder = this.getFolder(entryid);
	}

	if(folder) {
		var data = new Object();
		data["store"] = this.folderstoreid;
		data["parententryid"] = folder["parent_entryid"];
		data["entryid"] = folder["entryid"];
		data["destinationentryid"] = destination_entryid;
		data["destinationstoreid"] = destination_storeid;
		
		if (type && type == "move"){
			data["movefolder"] = "1";
		}
		
		webclient.xmlrequest.addData(this, "copy", data);
	}
	
	eventHierarchyCheckSelectedContextFolder(this);
}

hierarchymodule.prototype.setReadFlags = function()
{
	var folder = this.getFolder(this.selectedContextFolder);

	if(folder) {
		var data = new Object();
		data["store"] = this.folderstoreid;
		data["entryid"] = folder["entryid"];
		
		webclient.xmlrequest.addData(this, "readflags", data);
		this.reloadListModule(folder["entryid"], true);

	}
	
	eventHierarchyCheckSelectedContextFolder(this);
}

/**
 * reload listmodule that is loaded in a particular folder
 * @param HexString entryid entryid of folder that should be reloaded
 * @param boolean storeUniqueIds store entryids of currently selected items to reselect it after reload
 */
hierarchymodule.prototype.reloadListModule = function(entryid, storeUniqueIds)
{
	// reload listmodule if needed
	var moduleIDs = webclient.layoutmanager.getModuleIDs();
	for(var i=0;i<moduleIDs.length;i++){
		// we only need to check ListModules
		if (webclient.getModule(moduleIDs[i]) instanceof ListModule){
			var listmodule = webclient.getModule(moduleIDs[i]);
			if (listmodule.entryid == entryid){
				listmodule.list(false, false, storeUniqueIds);
			}
		}
	}
}

hierarchymodule.prototype.getStore = function(entryid)
{
	/**
	 * Opened delegate folders have entryids like [entryid]_calendar,
	 * [entryid]_contact etc., so remove that string part from the entryid.
	 */
	entryid = (entryid.indexOf('_') > 0) ? entryid.substr(0, entryid.indexOf('_')) : entryid;;
	var result = false;
	for(var i = 0; i < this.stores.length; i++) {
		/**
		 * Opened delegate folders have storeids like storeid_calendar,
		 * [STOREID]_contact etc., so remove that string part from the storeid.
		 */
		var tmpEntryid = this.stores[i].id;
		tmpEntryid = (tmpEntryid.indexOf('_') > 0) ? tmpEntryid.substr(0, tmpEntryid.indexOf('_')) : tmpEntryid;;
		if (compareEntryIds(tmpEntryid, entryid)) {
			result = this.stores[i];
		}
	}
	return result;
}

/**
 * Function will get folder properties from DOM, based on passed entryid of folder
 * @param			HexString		entryid					entryid of folder
 * @return			Object			selected_folder			folder properties
 */
hierarchymodule.prototype.getFolder = function(entryid)
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
					folder["userfullname"] = this.stores[i]["userfullname"];
					return folder;
				}
			}
		}
	}
	
	return false;
}

hierarchymodule.prototype.isDefaultFolder = function(entryid)
{
	var isDefaultFolder = false;
	
	for(var i = 0; i < this.stores.length; i++) {
		if(this.stores[i]["root"]["entryid"] == entryid) {
			isDefaultFolder = true;
		} else {
			for(var folder in this.stores[i]["defaultfolders"])
			{
				if(this.stores[i]["defaultfolders"][folder] == entryid) {
					isDefaultFolder = true;
				}
			}
		}
	}
	
	return isDefaultFolder;
}

hierarchymodule.prototype.isSpecialFolder = function(type, entryid) 
{
	var result = false;
	
	for(var i = 0; i < this.stores.length; i++) {
		if (this.stores[i]["defaultfolders"][type] && this.stores[i]["defaultfolders"][type] == entryid) {
			result = true;
		}
	}
	return result;
}

hierarchymodule.prototype.setOpenedFolders = function()
{
	var data = "";
	if (webclient.settings){
		for(var entryid in this.openedFolders){
			if (this.openedFolders[entryid]=="open"){
				webclient.settings.set("folders/entryid_"+entryid+"/is_open", 1);
			}else{
				webclient.settings.set("folders/entryid_"+entryid+"/is_open", 0);
			}
		}
	}
}

/**
* This function checks if the give folder is a parent in some way of the selected folder
*/
hierarchymodule.prototype.isParentOfSelected = function(parentFolder)
{
	var result = false;

	// first check if parentFolder has childs
	if (parentFolder["subfolders"] != -1){
		var childFolder = this.getFolder(this.selectedFolder);

		// check if parentFolder is on the same store
		if (parentFolder["storeid"] == childFolder["storeid"]){

			// check if selectedFolder is a direct child
			if (childFolder["parent_entryid"] == parentFolder["entryid"]){
				result = true;
			}else{
				// loop trough parents
				var folder = this.getFolder(childFolder["parent_entryid"]);
				while(folder && folder["entryid"]!=parentFolder["entryid"] && !this.isPublicStore(folder["storeid"])){
					folder = this.getFolder(folder["parent_entryid"]);
				}
				
				// final check
				if (folder && folder["entryid"]==parentFolder["entryid"]){
					result = true;
				}
			}		
		}
	}
	return result;
}

hierarchymodule.prototype.getOpenedFolders = function()
{
	var data = webclient.settings.get("folders");
	for (var id in data){
		var entryid = id.substring(8);
		if (webclient.settings.get("folders/"+id+"/is_open", 0) == 1){
			this.openedFolders[entryid] = "open";
		}else{
			this.openedFolders[entryid] = "closed";
		}
	}
}

hierarchymodule.prototype.resize = function()
{
	var height = this.element.offsetHeight - this.sharedFoldersElement.clientHeight - this.contentElement.offsetTop - 4;
	if(height < 3) {
		height = 3;
	}
	
	this.contentElement.style.height = height + "px";
}

hierarchymodule.prototype.hideSharedFolderActions = function()
{
	this.sharedFoldersElement.style.display = "none";
}

hierarchymodule.prototype.showSharedFolderActions = function()
{
	this.sharedFoldersElement.style.display = "block";
	this.resize(); // TODO: via LayoutManager

	// Set selected folder at the center in hierachy 
	var openedFolderElement = dhtml.getElementById(this.selectedFolder);
	if(openedFolderElement)
		this.contentElement.scrollTop = openedFolderElement.offsetTop - this.contentElement.clientHeight/2;
}

hierarchymodule.prototype.openSharedFolder = function(username, foldertype, subfolders)
{
    username = username.toLowerCase();
	if (typeof foldertype == "undefined"){
		foldertype = "all";
	}

	if (username == webclient.username.toLowerCase()){
		alert(_("This shared folder is your own folder."));
		return false;
	}
	
	var otherstores = webclient.settings.get("otherstores");
	if (otherstores) {
		for(var other_user in otherstores)
		{
			if (other_user == username && otherstores[other_user][foldertype]){
				alert(_("This shared folder is already open."));
				return false;
			}
		}
	}

	webclient.settings.set("otherstores/"+username+"/"+foldertype+"/type", foldertype);
	webclient.settings.set("otherstores/"+username+"/"+foldertype+"/show_subfolders", subfolders);

	var data = new Object();
	data["username"] = username;
	data["foldertype"] = foldertype;
	webclient.xmlrequest.addData(this, "opensharedfolder", data);
	
	return true;
}

hierarchymodule.prototype.closeSharedFolder = function(username, foldertype)
{
    username = username.toLowerCase();
	if (typeof foldertype == "undefined"){
		foldertype = "all";
	}

	var folderfound = webclient.settings.get("otherstores/"+username+"/"+foldertype, null);

	if (folderfound != null){
		webclient.settings.deleteSetting("otherstores/"+username+"/"+foldertype);
	
		// send delete to server
		var data = new Object();
		data["username"] = username;
		data["foldertype"] = foldertype;
		webclient.xmlrequest.addData(this, "closesharedfolder", data);
	}
}

/**
 * Fucntion which shows folder name and number of unread message in that folder in titlebar of browser.
 * For example- 'Inbox (9) - Zarafa Webaccess'
 *@param Object object which specifices properties of opened folder.
 */
hierarchymodule.prototype.setDocumentTitle = function (folder)
{
	// Check this folder is opened.
	if (this.selectedFolder === folder["entryid"]) {
		var folderDisplayName = folder["display_name"];

		// See if user's fullname is defined because we want to distinguish between shared folders.
		if (typeof folder["userfullname"] != 'undefined' && folder["userfullname"].trim().length > 0) folderDisplayName = folder["userfullname"] +" - "+ folder["display_name"];

		document.title = ((folder["content_unread"] != "0") ? folderDisplayName +" (" + folder["content_unread"] + ") " : folderDisplayName) + " - " + _("Zarafa Webaccess");
	}
}

/**
 * Function which updates state of folder.
 *@param HTMLElement element folder element whose state is changed
 *@param integer state state of folder either 'open' or 'closed'
 */
hierarchymodule.prototype.updateOpenedFolder = function(element, state)
{
	if(this.openedFolders) {
		this.openedFolders[element.id] = (state == this.tree.FOLDER_STATE_OPEN) ? "open" : "closed";
	}

	this.setOpenedFolders();
}

function eventHierarchyCheckSelectedContextFolder(moduleObject, element, event)
{
	if(moduleObject.selectedContextFolder) {
		var folder = dhtml.getElementById(moduleObject.selectedContextFolder);

		if(folder) {
			var folderIcon = folder.getElementsByTagName("div")[1];
			var folderName = folderIcon.getElementsByTagName("span")[0];
			if(folderName.className.indexOf("folder_title_context") > 0) {
				folderName.className = folderName.className.substring(0, folderName.className.indexOf("folder_title_context"));
			}
		}
	}
}

function eventHierarchyMouseMoveScroll(moduleObject, element, event){
	// return if the drag event is not occuring.
	if(!dragdrop.targetover) return;
	
	//get the element' top position 
	var elementTop = element.offsetTop || 0;
	var tempEl = element.offsetParent;
	//get the actuall top position from the very top left corner of browser area.
	while (tempEl != null) {
		elementTop += tempEl.offsetTop;
		tempEl = tempEl.offsetParent;
	}

	//calculate the mouse position on element and do the scrolling
	if(event.clientY < (elementTop+20)){
		element.scrollTop -= 5;
	}else if(event.clientY > (elementTop+element.offsetHeight-20)){
		element.scrollTop += 5;
	}
}

function eventHierarchySelectFolder(moduleObject, element, event)
{
	if(!event) {
		event = new Object();
	}

	if(event.button == 0 || event.button == 1 || !event.button && !moduleObject.selectedFolderFlag) {
		if( moduleObject.selectedFolder){
			var folder = dhtml.getElementById(moduleObject.selectedFolder);
			if(folder) {
				var folderIcon = folder.getElementsByTagName("div")[1];
				var folderName = folderIcon.getElementsByTagName("span")[0];
				dhtml.removeClassName(folderName, "folder_title_selected");
				dhtml.addClassName(folderName, "folder_title");
				// reset the unread_count font color to default as per theme when a folder is selected
				dhtml.removeClassName(folderName.getElementsByTagName("span")[0], "selectedfolder_unread_count");
				dhtml.addClassName(folderName.getElementsByTagName("span")[0], "unread_count");

			}
		}
		moduleObject.selectedFolder = element.parentNode.id;
		var folderName = element.getElementsByTagName("span")[0];
		dhtml.removeClassName(folderName, "folder_title");
		dhtml.removeClassName(folderName, "folder_title_context");
		dhtml.addClassName(folderName, "folder_title_selected");
		// this will set the unread_count font color to white, only when a folder is selected
		dhtml.removeClassName(folderName.getElementsByTagName("span")[0], "unread_count");
		dhtml.addClassName(folderName.getElementsByTagName("span")[0], "selectedfolder_unread_count");
	}
}

function eventHierarchyShowBranch(moduleObject, element, event)
{
	// The toggleBranch method retuns the new state of the folder
	var state = this.tree.toggleBranch(element.parentNode.id);

	switch(state){
		case this.tree.FOLDER_STATE_OPEN:
			state = "open";
			break;
		case this.tree.FOLDER_STATE_CLOSED:
			state = "closed";
			break;
	}

	if(moduleObject.openedFolders) {
		moduleObject.openedFolders[element.parentNode.id] = state;
	}
	
	if(typeof dragdrop != "undefined") {
		dragdrop.updateTargets("folder");
	}
	
	moduleObject.setOpenedFolders();

	var folder = moduleObject.getFolder(element.parentNode.id);
	if (moduleObject.isParentOfSelected(folder)){
		var folderElement = element.parentNode.getElementsByTagName("div")[1];
		dhtml.executeEvent(folderElement, "mousedown");
		dhtml.executeEvent(folderElement, "click");
	}
}

function eventHierarchyChangeFolder(moduleObject, element, event)
{
	if(!event) {
		event = new Object();
	}
	
	if(event.button == 0 || event.button == 1 || !event.button) {
		var storeid = false;
		if(moduleObject.defaultstore) {
			storeid = moduleObject.defaultstore["id"];
		}
	
		var folder = moduleObject.getFolder(element.parentNode.id);
	
		if(folder) {
			var data = new Object();
			var storeid = moduleObject.folderstoreid;

			// Opened folder of another user will contain id and foldertype followed by '_', so remove foldertype
			if(storeid.indexOf("_") > 0)
				storeid = storeid.substr(0,storeid.indexOf("_"));
			
			moduleObject.setNumberItems(folder["content_count"], folder["content_unread"]);
			moduleObject.sendEvent("changefolder", storeid, folder);
			moduleObject.setDocumentTitle(folder);
		}
	}
}

function eventHierarchyContextMenu(moduleObject, element, event)
{
	if(moduleObject.selectedContextFolder) {
		var folder = dhtml.getElementById(moduleObject.selectedContextFolder);

		if(folder) {
			var folderIcon = folder.getElementsByTagName("div")[1];
			var folderName = folderIcon.getElementsByTagName("span")[0];
			dhtml.removeClassName("folder_title_context");
		}
	}
	
	var folder = moduleObject.getFolder(element.parentNode.id);
	
	if(folder) {
		var items = new Array();
		if (!moduleObject.isRootFolder) {
			items.push(webclient.menu.createMenuItem("open", _("Open"), false, eventHierarchyOpenFolder));
			items.push(webclient.menu.createMenuItem("seperator", ""));
			if(!moduleObject.isDefaultFolder(folder["entryid"])) {
				items.push(webclient.menu.createMenuItem("copy", _("Copy/Move Folder"), false, eventHierarchyCopyFolder));
				items.push(webclient.menu.createMenuItem("renamefolder", _("Rename Folder"), false, eventHierarchyModifyFolder));
			}
		}	
		if (!moduleObject.isSharedFolder(folder["storeid"]))
    		items.push(webclient.menu.createMenuItem("newfolder", _("New Folder"), false, eventHierarchyNewFolder));
		items.push(webclient.menu.createMenuItem("seperator", ""));
		if (!moduleObject.isRootFolder) {
			items.push(webclient.menu.createMenuItem("markread", _("Mark All Messages Read"), false, eventHierarchyMarkMessagesRead));
	
			if(!moduleObject.isDefaultFolder(folder["entryid"])) {
				items.push(webclient.menu.createMenuItem("seperator", ""));
				items.push(webclient.menu.createMenuItem("deletefolder", _("Delete Folder"), false, eventHierarchyDeleteFolder));
			}
			if(moduleObject.isSpecialFolder("wastebasket", folder["entryid"]) || moduleObject.isSpecialFolder("junk", folder["entryid"])) {
				items.push(webclient.menu.createMenuItem("emptyfolder", _("Empty folder"), false, eventHierarchyEmptyFolder));
			}
			/**
			 * The check on the entryid (if it starts with 00000001) is used 
			 * because a folder that has been added as favorites folder will 
			 * have a flag at the start of the entryid. 
			 */
			if(moduleObject.isPublicStore(folder["storeid"]) && folder["entryid"].substr(0, 8) != "00000001" && !moduleObject.isFavoritesFolder(folder["entryid"])) {
				items.push(webclient.menu.createMenuItem("seperator", ""));
				items.push(webclient.menu.createMenuItem("addtofavorite", _("Add to favorites folder"), false, eventHierarchyAddToFavoriteFolder));
			}
	
			items.push(webclient.menu.createMenuItem("seperator", ""));
		} else {

			if (moduleObject.isSharedStore(folder["storeid"])){
				items.push(webclient.menu.createMenuItem("closestore", _("Close store"), false, eventHierarchyCloseStore));
				items.push(webclient.menu.createMenuItem("seperator", ""));
			}
		}
		if (moduleObject.isDefaultStore(folder.storeid) && moduleObject.isRootFolder){
			items.push(webclient.menu.createMenuItem("reload", _("Reload"), false, eventHierarchyReload));
		}
		items.push(webclient.menu.createMenuItem("properties", _("Properties"), false, eventHierarchyPropertiesFolder));

		webclient.pluginManager.triggerHook('client.module.hierarchymodule.contextmenu.buildup', {contextmenu: items, folder: folder});

		webclient.menu.buildContextMenu(moduleObject.id, folder["entryid"], items, event.clientX, event.clientY);
	}
	
	moduleObject.selectedContextFolder = folder["entryid"];
	var span = element.getElementsByTagName("span")[0];	
	span.className += " folder_title_context";
	
	return false;
}

function eventHierarchyOpenFolder(moduleObject, element, event)
{
	var entryid = element.parentNode.elementid;
	if(entryid) {
		moduleObject.selectFolder(true, entryid);

		element.parentNode.style.display = "none";
	}
}

function eventHierarchyCopyFolder(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var folder = moduleObject.getFolder(moduleObject.selectedContextFolder);
	webclient.openModalDialog(moduleObject, "copyfolder", DIALOG_URL+"task=copyfolder_modal&source_entryid="+folder.entryid, 300, 400, null, null, {parentModule: moduleObject});
}

function eventHierarchyNewFolder(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var folder = moduleObject.getFolder(moduleObject.selectedContextFolder);
	webclient.openModalDialog(moduleObject, "newfolder", DIALOG_URL+"task=createfolder_modal&parent_entryid="+folder.entryid, 300, 420, null, null, {parentModule: moduleObject});
}

function eventHierarchyModifyFolder(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var folder = moduleObject.getFolder(moduleObject.selectedContextFolder);
	webclient.openModalDialog(moduleObject, "modifyfolder", DIALOG_URL+"task=modifyfolder_modal&entryid="+folder.entryid, 300, 150, null, null, {parentModule: moduleObject});
}

function eventHierarchyMarkMessagesRead(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	moduleObject.setReadFlags();
}

function eventHierarchyDeleteFolder(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var folder = moduleObject.getFolder(moduleObject.selectedContextFolder);
	moduleObject.deleteFolder(folder);
}
function eventHierarchyPropertiesFolder(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var folder = moduleObject.getFolder(moduleObject.selectedContextFolder);
	var disable_permissions = 0;
	if(moduleObject.isPublicStore(folder["storeid"])) {
		/**
		 * Disable permissions tab for public root folder & favorites (sub)folders
		 * The check on the entryid (if it starts with 00000001) is used 
		 * because a folder that has been added as favorites folder will 
		 * have a flag at the start of the entryid. 
		 */
		if(moduleObject.isFavoritesFolder(folder["entryid"]) || moduleObject.isRootFolder || folder["entryid"].substr(0, 8) == "00000001") {
			disable_permissions = 1;
		}
	}
	webclient.openWindow(moduleObject, "properties", DIALOG_URL+"task=properties_standard&entryid="+folder.entryid+"&storeid="+folder.storeid+"&disable_permissions=" + disable_permissions, 425, 450);
}

function eventSharedFoldersClick(moduleObject, element, event)
{
	webclient.openModalDialog(moduleObject, "sharedfolder", DIALOG_URL+"task=sharedfolder_modal&storeid="+moduleObject.defaultstore["id"], 300, 230, callBackSharedFolder, this);
}

function eventAdvancedCalendarClick(moduleObject, element, event)
{
	// Delete the datepicker module when it has been loaded
	if (webclient.datepicker){
		webclient.deleteModule(webclient.datepicker);
		webclient.datepicker = null;
		dragdrop.updateTargets("folder");
	}
	webclient.loadModule("multiusercalendarmodule", "MultiUserCalendar", "main", null, BORDER_LAYOUT);
	moduleObject.selectedFolder = false;
}

function callBackSharedFolder(result, module)
{
    return module.openSharedFolder(result.username, result.folder, result.subfolders);
}

function eventHierarchyCloseStore(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var folder = moduleObject.getFolder(moduleObject.selectedContextFolder);
	var username = moduleObject.getStore(folder.storeid).username;
	var foldertype = moduleObject.getStore(folder.storeid).foldertype;
	moduleObject.closeSharedFolder(username, foldertype);
}

function eventHierarchyEmptyFolder(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var folder = moduleObject.getFolder(moduleObject.selectedContextFolder);
	moduleObject.emptyFolder(folder);
}

function eventHierarchyReload(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	moduleObject.list();
}

function eventHierarchyAddToFavoriteFolder(moduleObject, element, event)
{
	element.parentNode.style.display = "none";
	var folder = moduleObject.getFolder(moduleObject.selectedContextFolder);
	webclient.openModalDialog(moduleObject, "addtofavorite", DIALOG_URL+"task=addtofavorite_modal&foldername="+folder.display_name+"&entryid="+folder.entryid, 300, 320, null, null, {parentModule: moduleObject});
}
/**
 * Function which check whether entryID is of Favorites folder.
 * @param string entryID -entry id of folder to check for.
 * @return boolean -returns true it is Favorites Folder, false if not Favorites Folder.
 */
hierarchymodule.prototype.isFavoritesFolder = function(entryID)
{
	var result = false;
	for (var i in this.stores){
		// Check if store is public
		if (this.stores[i]["type"] == "public"){
			if (entryID == this.stores[i]["defaultfolders"]["favorites"]) result = true;
		}
	}
	
	return result;
}
/**
 * Function which handles keys combinations for creating new item.
 */
function eventHierarchyKeyCtrlNewItem(moduleObject, element, event)
{
	var extraParams = false;
	var item = false;
	var folder = moduleObject.getFolder(moduleObject.selectedFolder);
	var containerClass = "IPF.Note";

	if (folder && folder.container_class)
		containerClass = folder.container_class;

	var storeID = folder.storeid;
	var folderEntryID = folder.entryid;
	switch (event.keyCombination)
	{
		case this.keys["new"]["item"]:			// New Item
			element = webclient.menu.menuBarLeft.firstChild;
			dhtml.executeEvent(element, "click");
			break;
		case this.keys["new"]["mail"]:			// New Mail
			if (containerClass.toLowerCase().indexOf("note") == -1) {
				folderEntryID =  moduleObject.defaultstore.defaultfolders.drafts;
				storeID = moduleObject.defaultstore.id;
			}
			item = "createmail"
			break;
		case this.keys["new"]["appointment"]:	// New Appointment
			item = "appointment";
			if (containerClass.toLowerCase().indexOf(item) == -1) {
				folderEntryID = moduleObject.defaultstore.defaultfolders.calendar;
				storeID = moduleObject.defaultstore.id;
			}
			var dtmodule = webclient.getModulesByName("datepickerlistmodule");
			if (dtmodule[0] && dtmodule[0].selectedDate){
				newappDate = new Date(dtmodule[0].selectedDate+((webclient.settings.get("calendar/workdaystart",9*60)*ONE_HOUR)/60));
				extraParams = (extraParams?extraParams+"&":"")+("date="+newappDate.getTime()/1000);
			}
			break;
		case this.keys["new"]["meeting_request"]:		// New Meeting Request
			item = "appointment";
			if (containerClass.toLowerCase().indexOf(item) == -1) {
				folderEntryID = moduleObject.defaultstore.defaultfolders.calendar;
				storeID = moduleObject.defaultstore.id;
			}
			extraParams = "meeting=true";
			break;
		case this.keys["new"]["task"]:			// New Task
		case this.keys["new"]["contact"]:		// New Contact
		case this.keys["new"]["note"]:			// New Note
		case this.keys["new"]["taskrequest"]:	// New Task Request
			item = array_search(event.keyCombination, this.keys["new"]);
			if (item == "taskrequest") {
				item = (item == "taskrequest")? "task" : item;
				extraParams = "taskrequest=true";
			}

			if (containerClass.toLowerCase().indexOf(item) == -1) {
				folderEntryID = moduleObject.defaultstore.defaultfolders[item];
				storeID = moduleObject.defaultstore.id;
			}
			item = (item == "note")? "stickynote" : item;
			break;
		case this.keys["new"]["distlist"]:		// New Distributionlist
			item = "distlist";
			if (containerClass.toLowerCase().indexOf("contact") == -1) {
				folderEntryID = moduleObject.defaultstore.defaultfolders.contact;
				storeID = moduleObject.defaultstore.id;
			}
			break;
		case this.keys["new"]["folder"]:		// New Folder
			item = "folder";
			webclient.openWindow(moduleObject, "folder", DIALOG_URL+"task=createfolder_modal&parent_entryid=" + moduleObject.selectedFolder +(extraParams?"&"+extraParams:""), 300, 400, null, null, null, {parentModule: moduleObject});
			break;
	}
	
	if (item && item != "folder")
		webclient.openWindow(-1, item, DIALOG_URL+"task="+ item +"_standard&storeid="+ storeID +"&parententryid="+ folderEntryID + (extraParams?"&"+extraParams:""));
}
/**
 * Function which handles key combinations for opening default folders.
 */
function eventHierarchyKeyCtrlOpenFolder(moduleObject, element, event, keys)
{
	var folderName = false;
	var windowData = new Object();

	switch(event.keyCombination)
	{
		case this.keys["open"]["inbox"]:		// Jump to default Inbox folder
		case this.keys["open"]["calendar"]:	// Jump to default Calendar folder
		case this.keys["open"]["contact"]:	// Jump to default Contacts folder
		case this.keys["open"]["task"]:		// Jump to default Tasks folder
		case this.keys["open"]["note"]:		// Jump to default Notes folder
		case this.keys["open"]["journal"]:	// Jump to default Journal folder
			folderName = array_search(event.keyCombination, this.keys["open"]);
			break;
		case this.keys["open"]["muc"]:
			if(MUC_AVAILABLE){
				// Delete the datepicker module when it has been loaded
				if (webclient.datepicker){
					webclient.deleteModule(webclient.datepicker);
					webclient.datepicker = null;
					dragdrop.updateTargets("folder");
				}
				webclient.loadModule("multiusercalendarmodule", "MultiUserCalendar", "main", null, BORDER_LAYOUT);
			}
			moduleObject.selectedFolder = false;
			break;
		case this.keys["open"]["shared_folder"]:
			windowData["sharetype"] = "folder";
			webclient.openModalDialog(moduleObject, "sharedfolder", DIALOG_URL+"task=sharedfolder_modal&storeid="+ moduleObject.defaultstore["id"], 300, 230, callBackSharedFolder, this, windowData);
			break;
		case this.keys["open"]["shared_store"]:
			windowData["sharetype"] = "store";
			webclient.openModalDialog(moduleObject, "sharedfolder", DIALOG_URL+"task=sharedfolder_modal&storeid="+ moduleObject.defaultstore["id"], 300, 230, callBackSharedFolder, this, windowData);
			break;
	}

	// If folder is already opened then do nothing.
	if (folderName && moduleObject.selectedFolder !== moduleObject.defaultstore.defaultfolders[folderName]){
		webclient.hierarchy.selectFolder(true, moduleObject.defaultstore.defaultfolders[folderName]);
	}
}