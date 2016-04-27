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
 * WebClient
 * Object which is available for every other object. It startups the client and
 * it is responsible for loading and deleting modules.
 */ 
function WebClient()
{
	this.modules = new Object();
	this.sessionid = false;
}

/**
 * Function which intializes the WebClient
 * @param string base_url the base url of the webaccess
 * @param string modulePrefix the module prefix (used for create unique id in the XML)
 * @param object availableModules all the available modules
 * @param object settings all the settings
 * @param object pluginData all the plugin data
 */ 
WebClient.prototype.init = function(base_url, modulePrefix, availableModules, settings, pluginData)
{
	this.window = window;
	this.base_url = base_url;
	this.modulePrefix = modulePrefix;
	this.dialogs = new Object();
	this.dialogid = new Date().getTime();

	this.xmlrequest = new XMLRequest(modulePrefix);
	this.dispatcher = new Dispatcher(availableModules);
	this.menu = new Menu();

	this.settings = this.dispatcher.loadModule("settingsmodule");
	if (this.settings) {
		var settingsID = this.addModule(this.settings);
		this.settings.init(settingsID);
		this.settings.settings = settings;
	}else if (parentWebclient && parentWebclient.settings){
		this.settings = parentWebclient.settings;
	}
	
	// Fire up the PluginManager, let is know about the plugins and initialize them.
	this.pluginManager = new PluginManager();
	this.pluginManager.setPluginData(pluginData);
	this.pluginManager.init();

	startKeepAlive();

    /*
     * Trigger the hook so it is possible to execute code directly after the webclient
     * has finished initializing.
     */
    webclient.pluginManager.triggerHook('client.core.webclient.init.after', {});
}

WebClient.prototype.setUserInfo = function(username, fullname, entryid, emailaddress)
{
	this.username = username;
	this.fullname = fullname;
	this.userEntryid = entryid;
	this.emailaddress = emailaddress;
}


/**
 * Function which starts up the client. Set the right events and creates the
 * hierarchy list module. 
 */ 
WebClient.prototype.startClient = function()
{
	var twarning = dhtml.getElementById("trial_warning");
	if(twarning && typeof ZARAFA_TRIAL_EXPIRE_PERIOD != "undefined" && ZARAFA_TRIAL_EXPIRE_PERIOD){
		twarning.style.display = "block";
		twarning.innerHTML = "TRIAL LICENSE: Your trial period will expire in %s days.".sprintf(ZARAFA_TRIAL_EXPIRE_PERIOD);
	}

	// disable context menu
	dhtml.addEvent(-1, document.body, "mousedown", eventBodyMouseDown);
	dhtml.addEvent(-1, document.body, "mouseup", checkMenuState);
	dhtml.addEvent(-1, document.body, "contextmenu", eventBodyMouseDown);
	
	this.inputmanager = new InputManager();
	this.inputmanager.init();
	this.layoutmanager = new LayoutManager();
	dhtml.addEvent(-1, window, "resize", eventWebClientResize);	

	// TODO
	if (window.addEventListener){
        window.addEventListener("unload", eventWebClientUnload, false);
    } else if (window.attachEvent){
        window.attachEvent("onunload", eventWebClientUnload);
    }

    // Every "mouseup" on the body element, the menu's (context and default) should be checked 
	// if they should be removed or hidden from the page.
	this.inputmanager.bindEvent(webclient, "mouseup", checkMenuState);

	// Drag/Drop events
	dhtml.addEvent(-1, document.body, "mouseup", eventDragDropMouseUpDraggable);
	dhtml.addEvent(-1, document.body, "mousemove", eventDragDropMouseMoveDraggable);

	
	// Load hierarchy module
	this.hierarchy = this.dispatcher.loadModule("hierarchymodule");
	if(this.hierarchy) {
		var moduleID = this.addModule(this.hierarchy);
		// Add module to the "left" of the screen
		var element = this.layoutmanager.addModule(moduleID, "left");

		this.hierarchy.addEventHandler("changefolder", this, this.onHierarchyChangeFolder);

		this.hierarchy.init(moduleID, element);
		this.hierarchy.list(true);
	}
	
	if (webclient.settings.get("global/startup/folder","inbox")=="inbox"){	
	    // If the user has selected 'open inbox at startup', we know we want the inbox
	    // so we can post the request here already, instead of waiting for the hierarchy module
	    // to load.
	    
	    // This ignores the first event from the hierarchy module since we have already loaded that
	    // folder
    	this.folderOpened = true;
    	this.openFolder();
    }
	
	// start notification
	this.notification = this.dispatcher.loadModule("notificationmodule");
	if (this.notification) {
		var notificationID = this.addModule(this.notification);
		this.notification.init(notificationID);
	}

	// start reminders
	this.reminder = this.dispatcher.loadModule("reminderlistmodule");
	if (this.reminder) {
		var reminderID = this.addModule(this.reminder);
		this.reminder.init(reminderID);
	}

	// start resolvenames
	this.resolvenames = this.dispatcher.loadModule("resolvenamesmodule");
	if (this.resolvenames) {
		var resolvenamesID = this.addModule(this.resolvenames);
		this.resolvenames.init(resolvenamesID);
	}
	
	// Load the buttons module
	this.buttonsModule = this.loadModule("buttonsmodule", false, "left", null, BOX_LAYOUT, INSERT_ELEMENT_AT_BOTTOM);
	this.buttonsModule.hierarchy = this.hierarchy;

	// update layoutmanager
	this.layoutmanager.updateElements("left");
	
	eventWebClientResize();
	this.xmlrequest.sendRequest();
	
	/**
	 * mailto handling
	 * there are two ways for handling mailto links
	 * 1) by using windows registry to handle mailto protocol - send unencoded data - ?action=mailto&to= ...
	 * 2) by using firefox to register mailto protocol - send encoded data - ?action=mailto&url= ...
	 * both method sends different data so we will have to handle it differently
	 * if user wants to use any special character in subject or body then he has manually encode it in mailto url
	 * otherwise it will not work properly
	 */
	// if action attributes exist then process according to action type
	var actionUrl = new String(window.location.search);
	actionUrl = decodeURIComponent(actionUrl);

	// check URL is fully encoded or just its data part is encoded
	// we can't really tell the difference so we are using two different parameters for that
	var urlIsDoubleEncoded = actionUrl.search(/\&url\=/i) != -1 ? true : false;

	if(actionUrl.length > 0) {
		if (urlIsDoubleEncoded)
			// decode URI before using it
			actionUrl = decodeURIComponent(actionUrl);

		// remove first '?' from the URL
		actionUrl = actionUrl.replace("?", "");

		// remove '?' in URL that is appended before 'subject' tag
		actionUrl = actionUrl.replace("?", "&");

		this.handleURLAction(actionUrl);
	}
	
	/**
	 * check if user is set to out of office, and check that out of office message
	 * is not showing again and again in same login session.
	 * then ask user on login, if user wants to set out of office off?
	 */
	var outOfOfficeChangeId = webclient.settings.get("outofoffice_change_id", "false");
	if(webclient.settings.get("outofoffice/set","false") == "true" && outOfOfficeChangeId != "false" && outOfOfficeChangeId != webclient.sessionid){
		if (confirm(_("Out of Office currently on. Would you like to turn it off?")))
			webclient.settings.set("outofoffice/set","false");
		webclient.settings.set("outofoffice_change_id", webclient.sessionid);
	}
}

/** 
 * Function which executes proper action when action arguments are passed
 * in the URL of WA
 *
 * @param string action arguments that are passed in URL
 */
WebClient.prototype.handleURLAction = function (actionUrl)
{
	var keyValuePairs = dhtml.URLToObject(actionUrl);

	// check action type
	if(typeof keyValuePairs == "object"){
		if(keyValuePairs["action"] == "mailto") {
			// @TODO we can remove most of this code when we support , as email seperator ticket #5274

			// if there are more then one email address in TO, CC or BCC field then 
			// make it semicolon seperated instead of default comma seperated
			if(typeof keyValuePairs["url"] != "undefined") {
				// remove 'mailto:' tag from 'url' field
				keyValuePairs["to"] = keyValuePairs["url"].replace("mailto:", "");
				keyValuePairs["to"] = keyValuePairs["to"].replace(/,/g, "; ");
				delete keyValuePairs["url"];
			} else if(typeof keyValuePairs["to"] != "undefined") {
				// remove 'mailto:' tag from 'to' field
				keyValuePairs["to"] = keyValuePairs["to"].replace("mailto:", "");
				keyValuePairs["to"] = keyValuePairs["to"].replace(/,/g, "; ");
			}

			// didn't find on internet anything about multiple CC & BCC fields
			// in mailto links, although its handled here for precaution
			if(typeof keyValuePairs["cc"] != "undefined") {
				keyValuePairs["cc"] = keyValuePairs["cc"].replace(/,/g, "; ");
			}

			if(typeof keyValuePairs["bcc"] != "undefined") {
				keyValuePairs["bcc"] = keyValuePairs["bcc"].replace(/,/g, "; ");
			}

			var createmailUrl = "";
			for(var key in keyValuePairs) {
				if(key != "action") {
					// action attribute is not needed in creating createmail dialog URL
					createmailUrl = createmailUrl + "&" + encodeURIComponent(key) + "=" + encodeURIComponent(keyValuePairs[key]);
				}
			}

			// open createmail dialog
			webclient.openWindow(this, "createmail", DIALOG_URL + "task=createmail_standard" + createmailUrl);
		}else if(keyValuePairs["action"] == "openmail") {

			// open readmail dialog
			var uri = DIALOG_URL+"task=readmail_standard&storeid=" + encodeURIComponent(keyValuePairs["storeid"]) + "&parententryid=" + encodeURIComponent(keyValuePairs["parententryid"]) + "&entryid=" + encodeURIComponent(keyValuePairs["entryid"]);
			webclient.openWindow(this, "openmail", uri);
		}
	}
}
 

/**
 * Function which loads a module and returns the module object.
 * @param string moduleName module name
 * @param string title title of the module
 * @param string position position of module in the webclient
 * @param object data data (storeid, entryid, etc.)
 * @param integer LAYOUT the LAYOUT constant
 * @param integer INSERT_ELEMENT_AT the INSERT_ELEMENT_AT constant
 * @return object module object 
 */ 
WebClient.prototype.loadModule = function(moduleName, title, position, data, LAYOUT, INSERT_ELEMENT_AT)
{
	// If LAYOUT is BORDER_LAYOUT, remove all elements in that position
	if(LAYOUT == BORDER_LAYOUT) {
		var elements = this.layoutmanager.getElements(position);
		
		for(var moduleID in elements)
		{
			var module = this.getModule(moduleID);
			
			if(module) {
				this.deleteModule(module);
			}
		}
		// Removes all resizebar when a new module is loading within given position.
		this.layoutmanager.removeAllresizebars(position);
	}

	// Load module
	var module = this.dispatcher.loadModule(moduleName);

	if(module) {
		// Add module
		var moduleID = this.addModule(module);
		// Add element to LayoutManager
		var element = this.layoutmanager.addModule(moduleID, position, BOX_LAYOUT, INSERT_ELEMENT_AT);

		// Initialize module
		module.init(moduleID, element, title, data);
		module.list();
		
		// Update elemens (resize)
		this.layoutmanager.updateElements(position);
		
		// Keep trac of module that is loaded in opened folders
    	webclient.moduleIdInOpenedFolder = moduleID;
	}
	
	// Resize after loading module.
	eventResizePanes(false, false, false);
	
	return module;
}

/**
 * Function which returns a module.
 * @param integer moduleID the module id
 * @return object module object  
 */ 
WebClient.prototype.getModule = function(moduleID)
{
	var module = false;

	// sometimes modules are registered with objects but here we only need module id in form of string or number
	if(typeof moduleID != 'string' && typeof moduleID != 'number') {
		return module;
	}

	if(this.modules[moduleID]) {
		module = this.modules[moduleID];
	} else if(this.modules[this.modulePrefix + "_" + moduleID]) {
		module = this.modules[this.modulePrefix + "_" + moduleID];
	}else {
		// Loop through dialogs
		for(var dialogname in this.dialogs){
			var client = null;

			try{ //try/catch in order to prevent IE from throwing an exception because of missing windows
				if(this.dialogs[dialogname].window){
					if(typeof this.dialogs[dialogname].window.webclient == "object" && this.dialogs[dialogname].window.webclient != null){
						client = this.dialogs[dialogname].window.webclient;
					}
				}
			} catch(e) {}

			if(client !== null && client.getModule){
				module = client.getModule(moduleID);
				if(module){
					break;
				}
			}
		}
	}
	
	return module;
}

/**
* Function which returns an array of modules who are matching "moduleName"
*
*@param string moduleName the name of the modules we want to find
*@param array list of modules
*/
WebClient.prototype.getModulesByName = function(moduleName)
{
	var modules = new Array();
	for(var moduleID in this.modules){
		if (this.modules[moduleID] && this.modules[moduleID].getModuleName() == moduleName){
			modules.push(this.modules[moduleID]);
		}
	}
	return modules;
}

/**
 * Function which adds a module to the modules list.
 * @param object module module object
 * @return integer module id
 * @todo
 * - Clean up the modules list, after a while, otherwise the modules list is long (could be memory leak)    
 */ 
WebClient.prototype.addModule = function(module)
{
	var moduleID = 0;

	while(typeof(this.modules[this.modulePrefix + "_" + moduleID]) != "undefined")
	{
		moduleID++;
	}
	
	this.modules[this.modulePrefix + "_" + moduleID] = module;

	return moduleID;
}

/**
 * Function which delets a module.
 * @param object module module object 
 */ 
WebClient.prototype.deleteModule = function(module)
{
	if(this.modules[this.modulePrefix + "_" + module.id]) {
		if(module.destructor) {
			// Destructor
			module.destructor();
		}
		// Delete element in layout manager
		this.layoutmanager.deleteElement(module.id);
		
		/**
		 * Since module.destructor also destroys view by calling view.destructor(), so 
		 * making webclient.getModule(id) useful in views.
		 */
		this.modules[this.modulePrefix + "_" + module.id] = false;
		// Delete module on server
		this.xmlrequest.deleteModule(this.modulePrefix + "_" + module.id);
	}
}

/**
 * Function which opens a new window.
 * @param object moduleObject module object
 * @param string name window name
 * @param string url url
 * @param integer width width of the window
 * @param integer height height of the window
 * @param resizable boolean true - window is resizable, false - window is not resizable     
 */ 
WebClient.prototype.openWindow = function(moduleObject, name, url, width, height, resizable, callback, callbackdata, windowdata) 
{
	this.clearDialogs();

	var windowwidth = 780;
	if(typeof(width)!="undefined") {
		windowwidth = width;
	}
	
	var windowheight = 560;
	if(typeof(height)!="undefined") {
		windowheight = height;
	}
	
	var windowresizable = 1;
	if(typeof(resizable)!="undefined") {
		windowresizable = resizable?1:0;
	}

	// Make a unique name for the dialog
	name = window.name + "_" + name + "_" + this.dialogid;
	this.dialogid++;

	var windowObj = dialog(url, name, "toolbar=0,scrollbars=0,location=0,status=1,menubar=0,resizable=" + windowresizable + ",width=" + windowwidth + ",height=" + windowheight + ",top=" + ((screen.height / 2) - (windowheight / 2)) + ",left=" + ((screen.width / 2) - (windowwidth / 2)), false, callback, callbackdata, windowdata);

	this.registerDialog(moduleObject, name, windowObj);

	return windowObj;
}

/**
 * Function which opens a modal dialog.
 * @param object moduleObject module object
 * @param string name window name
 * @param string url url
 * @param integer width width of the window
 * @param integer height height of the window
 */ 
WebClient.prototype.openModalDialog = function(moduleObject, name, url, width, height, callback, callbackdata, windowdata) 
{
	this.clearDialogs();

	// Make a unique name for the dialog
	name = window.name + "_" + name + "_" + this.dialogid;
	this.dialogid++;

	var windowObj = modal(url, name, "toolbar=0,scrollbars=0,location=0,status=1,menubar=0,resizable=0,width=" + width + ",height=" + height + ",dialogWidth=" + width + " px,dialogHeight=" + (height + 50) + " px,top=" + ((screen.height / 2) - (height / 2)) + ",left=" + ((screen.width / 2) - (width / 2)), callback, callbackdata, windowdata);

	this.registerDialog(moduleObject, name, windowObj);

	return windowObj;
}

/**
 * Registers the dialog to the webclient so it can be used to relay server responses to the modules 
 * within the dialogs.
 * @param Object moduleObj Module Object or Module ID
 * @param String name The name of the dialog
 * @param Object windowObj Window Object
 * @return Object The object that contains the Module Object and the Window Object.
 */
WebClient.prototype.registerDialog = function(moduleObj, name, windowObj){
	this.dialogs[name] = new Object();
	this.dialogs[name]["module"] = moduleObj;
	this.dialogs[name]["window"] = windowObj;
	return this.dialogs[name];
}

WebClient.prototype.clearDialogs = function()
{
	var name;

	for(name in webclient.dialogs){
		try {
			if (!webclient.dialogs[name].window || webclient.dialogs[name].window.name!=name){
				delete webclient.dialogs[name];
			}
		} catch(e) {
		//	alert(e.description);
			delete webclient.dialogs[name];
		}
	}
}

WebClient.prototype.requestReload = function()
{
	this.xmlrequest.xmlbuilder.addReloadRequest();
	this.xmlrequest.hasData = true;
}

WebClient.prototype.reload = function()
{
	this.window.location = this.base_url; 
}

/**
 * Called when hierarchy module has selected a folder
 */
WebClient.prototype.onHierarchyChangeFolder = function(storeid, folder)
{
	if (webclient.settings.get("global/startup/folder","inbox")=="last"){	
		webclient.settings.set("global/startup/folder_lastopened",folder["entryid"]);
	}else if (webclient.settings.get("global/startup/folder","inbox")=="today"){	
		webclient.settings.set("global/startup/folder_lastopened", this.hierarchy.defaultstore.subtree_entryid);
	}

	if(!this.folderOpened) {
    	this.openFolder(storeid, folder);
    }

    // Any next request should trigger openFolder
    this.folderOpened = false;
}

/**
 * Open a folder by showing it in a listmodule
 */
WebClient.prototype.openFolder = function(storeid, folder)
{
	var data = new Object;
	var name;
	
	if(storeid)
    	data["storeid"] = storeid;
    if(folder) {
    	data["entryid"] = folder["entryid"];

        var moduleName = folder["container_class"].substring(folder["container_class"].indexOf(".") + 1).toLowerCase();
        if(moduleName.indexOf(".") != -1) {
            // if IPF.Note.OutlookHomepage
            moduleName = moduleName.substring(0, moduleName.lastIndexOf("."));
        }

        moduleName = moduleName + "list";
        name = folder["display_name"];
		if (typeof folder["userfullname"] != 'undefined' && folder["userfullname"].trim().length > 0) name = folder["userfullname"] +" - "+ folder["display_name"];
    } else {
        moduleName = "maillist";
        name = _("Inbox");
		//Here we get the inbox folder data from settings to get its display settings.
		data["storeid"] = webclient.settings.get("folders/entryid_inbox/storeid","");
 		data["entryid"] = webclient.settings.get("folders/entryid_inbox/entryid","");
    }

	// if folder is an appointmentlist, load the folder via the datepicker
	if (moduleName == "appointmentlist") {
		if (!webclient.datepicker){
			webclient.datepicker = webclient.loadModule("datepickerlist", _("Date picker"), "left", data, BOX_LAYOUT, INSERT_ELEMENT_AT_TOP);
		}
		data['title'] = name;
		webclient.datepicker.loadFolder(folder, data);
	} else {
		// this is not an appointment list, so remove (if exists) the datepicker
		if (webclient.datepicker){
			webclient.deleteModule(webclient.datepicker);
			webclient.datepicker = null;
			dragdrop.updateTargets("folder");
		}
		if (typeof(folder) != "undefined" && webclient.hierarchy.isDefaultStore(folder.storeid) && webclient.hierarchy.isRootFolder){
			// open view when click on root folder(inbox-username).
			moduleName = "todaymodule";
			webclient.loadModule(moduleName, _("Today"), "main", data, BORDER_LAYOUT);
		} else {
			webclient.loadModule(moduleName, name, "main", data, BORDER_LAYOUT);
		}
	}
}

/**
 * Event: Function which resizes the webclient. 
 */ 
function eventWebClientResize(moduleObject, element, event)
{
	var windowWidth = document.documentElement.clientWidth;
	var windowHeight = document.documentElement.clientHeight;
	
	var top = dhtml.getElementById("top");
	var footer = dhtml.getElementById("footer");

	// Setup left pane (height only)
	var left = dhtml.getElementById("left");
	if(left)
		left.style.height = (windowHeight - top.clientHeight - footer.clientHeight) - 10 + "px";
	
	var main = dhtml.getElementById("main");
	
	// Setup main pane
	if(main) {
		main.style.height = (windowHeight - top.clientHeight - footer.clientHeight) - 10 + "px";
	}
	
	if(webclient.layoutmanager)
		webclient.layoutmanager.updateAllElements();
		
	/**
	 * This function retrieves settings and positions the panes, 
	 * this function is called because updateElements() 
	 * overrides the default sizes.
	 */
	eventResizePanes(false, false, false);
}

/**
 * Event: Function which fires on unload. It sends the last XML requets to the server. 
 */
function eventWebClientUnload()
{
	// try to close any dialog that is still open
	for(name in webclient.dialogs){
		try{
			webclient.dialogs[name].window.close();
			delete webclient.dialogs[name];
		}catch(e){
		}
	}

	webclient.xmlrequest.sendRequest();
	dhtml.removeEvents(document.body);
}

function eventBodyMouseDown(moduleObject, element, event)
{
	// For FF3.6 only
	var defaultSelection = eventCheckScrolling(event);
	if(defaultSelection)
		return true;
	// For FF only
	if(event.forceDefaultAction)
		return true;
	return false;
}

// Set the handler of an element to this to force the default browser action
// instead of the body default handler
function forceDefaultActionEvent(moduleObject, element, event)
{
    // IE6/7 code
    if(window.BROWSER_IE) {    
        event.cancelBubble = true;
        return true;
    } else {
        // FF code
        event.forceDefaultAction = true;
    }
}
