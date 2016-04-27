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
 * Module
 * The super class obejct for every module.
 */ 

Module.prototype.constructor = Module;

function Module(id, element, title)
{
	if(arguments.length > 0) {
		this.init(id, element, title);
	}
}

Module.prototype.destructor = function()
{
}

/**
 * Function which intializes the module.
 * @param integer id id
 * @param object element the element for the module
 * @param string title the title of the module   
 */ 
Module.prototype.init = function(id, element, title)
{
	this.id = id;
	
	if(element) {
		this.element = element;

		// If another module then this module changes this specific element,
		// the destructor of this is called. The destructor is sepcified elsewhere.
		this.element.moduleID = this.id;
	}
	
	this.contentElement = false;
	this.title = title;
	this.keys = new Array();
}

/**
 * @todo: Remove this function and implement in view objects
 */  
Module.prototype.loadMessage = function()
{
	var element = this.element;
	if(this.contentElement) {
		element = this.contentElement;
	}

	dhtml.removeEvents(element);
	dhtml.deleteAllChildren(element);
	
	element.innerHTML = "<center>" + _("Loading") + "...</center>";
	document.body.style.cursor = "wait";
}

/**
 * @todo: Remove this function and implement in view objects
 */  
Module.prototype.deleteLoadMessage = function()
{
	if(this.contentElement) {
		dhtml.deleteAllChildren(this.contentElement);
	} else {
		dhtml.deleteAllChildren(this.element);
	}
	
	document.body.style.cursor = "default";
}

/**
 * Function will put "element" in right coner of the main view
 * element = dhtml element for example: "span" element
 */ 
Module.prototype.setExtraTitle = function(element)
{
	var extraTitle = dhtml.getElementById("page_"+this.id);
	dhtml.deleteAllChildren(extraTitle);
	extraTitle.className = "zarafa_extra_title";
	extraTitle.appendChild(element);
}

/**
 * Function which sets the title for the module
 * @param string title the title
 * @param string subtitle the subtitle
 * @param boolean page true - use page element
 * @param array viewOptions the view options. user can select one of these views to change the view.
 * @param string selectOption the selected view 
 */ 
Module.prototype.setTitle = function(title, subtitle, page, viewOptions, selectedOption)
{
	var titleElement = dhtml.addElement(this.element, "div", "title");
	dhtml.addElement(titleElement, "div", "zarafa_background");
	var zarafaTitle = dhtml.addElement(titleElement, "div", "zarafa_title");
	dhtml.addElement(zarafaTitle, "span", false, false, title);

	if (this.layoutmenu){
		dhtml.addEvent(this.id, titleElement, "contextmenu", eventModuleLayoutContextMenu); 
	}
	
	if(subtitle) {
		var subtitleElement = dhtml.addElement(this.element, "div", "subtitle");
		dhtml.addElement(subtitleElement, "div", "subtitle_zarafa_background");
		dhtml.addElement(subtitleElement, "span", "zarafa_subtitle", false, subtitle);
	}
	
	if(page) {
		var pageElement = dhtml.addElement(titleElement, "div", "page", "page_"+this.id);

		// add moduleid to pagecombobox element to avoid duplicate element ids
		dhtml.addElement(pageElement, "div", "", "pageelement_"+this.id);
	}
	
	if(viewOptions) {
		var comboboxElement = dhtml.addElement(zarafaTitle, "div", "view");
		comboboxElement.style.left = (zarafaTitle.firstChild.offsetWidth + 15) + "px";
		var combobox = new ComboBox("view", eventListChangeView, this.id);
		dhtml.comboBoxes.push("view");
	
		if(selectedOption) {
			var options = new Array();
			for(var i in viewOptions)
			{
				var option = new Object();
				option["id"] = i;
				option["value"] = viewOptions[i];
				
				options.push(option);
			}
		
			combobox.createComboBox(comboboxElement, options, selectedOption, 150);
		}
	}
}

/**
 * Function which returns the module name.
 * @return string module name 
 * @todo: this function is not correct here. other developers can override this and
 *        call antother module on the server, for example the appointmentlistmodule calls
 *        the taskitemmodule on the server. So move this function to a global implementation
 *        or move it back to the XMLRequest object.
 *        
 *		  We need to make it as hard as possible for developers to call another module
 *		  on the server then the client one.       
 */ 
Module.prototype.getModuleName = function()
{
	return getType(this);
}

/**
 * Add an event handler for internal event type 'eventname'. If 'object' is not null, the method
 * will be called in the context of that object. The parameters passed to the method are down
 * to the event source
 *
 * @param eventname name of the event to handle (eg 'openitem')
 * @param object object if the method is to be called in an object's context
 * @param method method to call when event is triggered
 */
Module.prototype.addEventHandler = function(eventname, object, method)
{
	if(!this.internalEvents) 
		this.internalEvents = new Array();
		
	if(!this.internalEvents[eventname])
		this.internalEvents[eventname] = new Array();
		
	handlerinfo = new Object();
	handlerinfo.object = object;
	handlerinfo.method = method;
	
	this.internalEvents[eventname].push(handlerinfo);
}

/**
 * Send an event to all listeners
 *
 * @param eventname
 * @param paramN all other parameters are sent to the event handler.
 */
Module.prototype.sendEvent = function()
{
	var args = new Array;
	
	// Convert 'arguments' into a real Array() so we can do shift()
	for(var i=0; i< arguments.length;i++) {
		args.push(arguments[i]);
	}

	var eventname = args.shift();

	if(!this.internalEvents)
		return true;
		
	if(!this.internalEvents[eventname])
		return true;
		
	for(var i=0; i< this.internalEvents[eventname].length; i++) {
		var object =  this.internalEvents[eventname][i].object;
		if(typeof(object) == "object")
			this.internalEvents[eventname][i].method.apply(object, args);
		else
			this.internalEvents[eventname][i].method(arguments);
	}
}

function eventModuleLayoutContextMenu(moduleObject, element, event){
	webclient.menu.buildContextMenu(moduleObject.id, element.id, moduleObject.layoutmenu, event.clientX, event.clientY);
}


function eventFilterCategories(moduleObject, element, event)
{
	moduleObject.filtercategories(element, element.value);
}

/**
* Checks and assign the unique categories to input field type...
*@param element element		-input field element for insertrow that contains the selected categories
*@param string  categories	-list of selected catergories from the category popup window
*@param element available_categories	-list of available_categories catergories
*/
Module.prototype.filtercategories = function (element, categories, available_categories)
{
	var tempcategories = categories.split(";");
	var categoriesInLowerCase = categories.toLowerCase();
	categoriesInLowerCase = categoriesInLowerCase.split(";");
	var categories = new Array();
	
	for(var i in categoriesInLowerCase) {
		categoriesInLowerCase[i] = categoriesInLowerCase[i].trim();
		flag = 0;
		for (var j in categories) {
			categories[j] = categories[j].trim();
			if (categories[j].toLowerCase() == categoriesInLowerCase[i]) {
				
				flag = 1;
			}
		}
		
		if (flag == 0 && categoriesInLowerCase[i].length != 0) {
			categories.push(tempcategories[i]);	
		}
	}
	
	if (available_categories){
		for (var category in categories){
			for(var j = 0; j < available_categories.length; j++){
				var option = available_categories.options[j];
				category = category.toLowerCase();
					if(category.indexOf(option.text.toLowerCase()) < 0) 
						category = option.text;
			}
		}
	}
	element.value = categories.join("; ") + ";";
}

/**
 * Function will show an error message returned by server.
 * @param XMLNode action the action tag
 */
Module.prototype.handleError = function(action)
{
	// show error message to user
	var errorMessage = dhtml.getXMLValue(action, "message", false);

	if(errorMessage) {
		alert(errorMessage);
	}
}