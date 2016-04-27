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
 * DHTML
 * This object contains some useful functions. Most of the function do someting
 * with HTML elements. 
 */ 
function DHTML()
{
	// a counter used to create unique IDs
	this.addEvent.guid = 1;
	this.comboBoxes = new Array();
}

/**
* This function will do the same as "document.getElementById" but
* this would find the element more efficient than the DOM implementation,
* to make it faster than fast, specify a tagname and/or a root element to limit
* the search.
*
*@param id String The ID of the element you wish to find
*@param tagname String The tagname of the element you wish to find (optional)
*@param root Element The root element in which the search must find, defaults to "document"
*/
DHTML.prototype.getElementById = function(id, tagname, root)
{
	if (!tagname && !root)
		return document.getElementById(id);
		
    if (!tagname) tagname = '*';
	if (!root) root = document;

	if (typeof id != "string"){
		id = ""+id;
	}

    var elements = root.getElementsByTagName(tagname);
	var i = elements.length;
	while(i--){
        if (elements[i].id == id){
            return elements[i];
		}
    }

    return null;
}

/**
 * Function which adds a new element to the DOM tree.
 * @param object parent the parent element
 * @param string type the type of the new element (div, span, ...)
 * @param string className the class name of the new element
 * @param string id the id of the new element
 * @param string value the value of the new element
 * @return object the new element 
 */ 
DHTML.prototype.addElement = function(parent, type, className, id, value, windowObj)
{
	windowObj = windowObj || window;
	var element = windowObj.document.createElement(type);
	
	if(parent) {
		parent.appendChild(element);
	}
	
	if(className) {
		element.className = className;
	}
	
	if(id) {
		element.id = id;
	}
	
	if(value || value == 0) {
		element.appendChild(windowObj.document.createTextNode(value));
	}
	
	return element;
}

/**
 * Function which registers an event on an element.
 * @param integer moduleID the moduleID, can be -1 if event is not for a module
 * @param object element the element
 * @param string type the event type (click, mouseover, ...) without the on
 * @param function handler callback function   
 * @param object windowobj this window object is used when the event is defined on element in another window   
 */ 
DHTML.prototype.addEvent = function(moduleID, element, type, handler, windowObj)
{
	// assign each event handler a unique ID
	if (!handler.$$guid) {
		handler.$$guid = this.addEvent.guid++;
	}

	// create a hash table of event types for the element
	if (!element.events) {
		element.events = {};
	}
	
	// create a hash table of event handlers for each element/event pair
	var handlers = element.events[type];
	if (!handlers) {
		handlers = element.events[type] = {};
		// store the existing event handler (if there is one)
		if (element["on" + type]) {
			handlers[0] = element["on" + type];
		}
	}

	// store the event handler in the hash table
	handlers[handler.$$guid] = new Object();
	handlers[handler.$$guid]["moduleID"] = moduleID;
	handlers[handler.$$guid]["handler"] = handler;
	
	// assign a global event handler to do all the work
	element["on" + type] = handleEvent;
	
	// Set the window object the element belongs to if necessary.
	// This can be used when assigning events in other windows.
	if(typeof windowObj == "object"){
		element.windowObj = windowObj;
	}
}

/**
 * Copies all event handlers and settings from src to dest
 */
DHTML.prototype.copyEvents = function(dest, src)
{
	dest.events = src.events;
	for(var eventname in src.events) {
		dest["on" + eventname] = src["on" + eventname];
	}
}

/**
 * Function adds a text node to the given element.
 * @param object parent the element
 * @param string value the value of the text node  
 */ 
DHTML.prototype.addTextNode = function(parent, value)
{
	if (parent){
		parent.appendChild(document.createTextNode(value));
	}
}

/**
 * Function will check if the elements has an firstChild.nodeValue
 * @param element: html element
 * @param value(optional): string that will be returned when firstChild.nodeValue does not exist
 * @return string of firstChild.nodeValue
 */
DHTML.prototype.getTextNode = function(element, value)
{
	var result = null;
	if(typeof value != "undefined"){
		result = value;
	}
	if(element && element.firstChild && (typeof element.firstChild.nodeValue != "undefined")){
		result = element.firstChild.nodeValue;
	}
	return result;
}

DHTML.prototype.getXMLValue = function(xml, tag, value)
{
	var result = null;
	if(typeof value != "undefined"){
		result = value;
	}

	var element = this.getXMLNode(xml, tag);
	if(element) {
		if(typeof(element.textContent) != "undefined") {
			// use 'textContent' in FF, since 'nodeValue' is capped at 4k with multiple nodes
			result = element.textContent;
		} else if (element.firstChild && typeof(element.firstChild.nodeValue)!="undefined"){
			result = element.firstChild.nodeValue;
		}
    }
	return result;
}

DHTML.prototype.getXMLNode = function(xml, tag, n)
{
	if(!n)
		n = 0;
		
	var result = false;
	var element = xml.getElementsByTagName(tag);
	if (element && element[n]){
		result = element[n];
	}
	return result;
}

/**
 * Function which deletes all the children in the given element.
 * @param object parentNode the element 
 */ 
DHTML.prototype.deleteAllChildren = function(parentNode)
{
	if (parentNode){
		while(parentNode.hasChildNodes())
		{
			var child = parentNode.childNodes[0];
			if (child.hasChildNodes()){
				this.deleteAllChildren(child.childNodes[0]);
			}
			this.deleteElement(child);
		}
	}
}

/**
 * Function which deletes the given element.
 * @param object element the element which will be deleted 
 */ 
DHTML.prototype.deleteElement = function(element)
{
	if(element && element.parentNode) {
		element.parentNode.removeChild(element);
	}
}

/**
 * Function which executes an event. It is used to fake an event, which never
 * occurred.
 * @param object element the element on which the event will take place
 * @param string type the event (click, mouseover, ...) wothout the on   
 */  
DHTML.prototype.executeEvent = function(element, type)
{
	if(element.dispatchEvent) {
		var evt = document.createEvent("MouseEvents");
		evt.initMouseEvent(
			type, 						// Event Type
			true, 						// Bubble
			true, 						// Event can be cancelled
			document.defaultView, 		// Default view
			1, 							// One click or more
			0, 0, 0, 0, 				// Coordinates
			false, false, false, false, // Key modifiers
			0, 							// 0-left 1-middle 2-right cick
			null);						// Target
		element.dispatchEvent(evt);
	} else if(element.fireEvent) {
		element.fireEvent("on" + type);
	}
}

/**
 * Function which returns the elements with the given class name.
 * @param string needle the class name
 * @param string tagname the tag name
 * @return array list of elements   
 */ 
DHTML.prototype.getElementsByClassName = function(needle, tagname)
{
	return this.getElementsByClassNameInElement(document, needle, tagname);
}

/**
 * Function which returns the elements with the given class name in a specific element.
 * @param object element the element 
 * @param string needle the class name
 * @param string tagname the tag name
 * @return array list of elements   
 */ 
DHTML.prototype.getElementsByClassNameInElement = function(element, needle, tagname, nonrecursive)
{
	var s,i,e,r = [];

	if (!tagname) {
		tagname = '*';
	}
	
	if (!nonrecursive) {
		nonrecursive = false;
	}
	
	var re = new RegExp('(^|\\s)' + needle + '(\\s|$)');

	if(!nonrecursive) {
		s = element.getElementsByTagName(tagname);
	} else {
		s = element.childNodes;
	}
	
	for(i = 0; i < s.length; i++)
	{
		e = s[i];
		
		if (e.tagName.toLowerCase == tagname.toLowerCase && e.className && re.test(e.className)) {
			r.push(e);
		}
	}
	
	return r;
}

/**
 * Function which returns the absolute top position of the given element.
 * @param object element the element
 * @return integer the absolute top position  
 */  
DHTML.prototype.getElementTop = function(element)
{
	var yPos = element.offsetTop;
	var parentElement = element.offsetParent;
	
	while (parentElement != null) {
		yPos += parentElement.offsetTop;
		parentElement = parentElement.offsetParent;
	}
	
	return yPos;
}

/**
 * Function which returns the absolute left position of the given element.
 * @param object element the element
 * @return integer the absolute left position  
 */  
DHTML.prototype.getElementLeft = function(element)
{
	var xPos = element.offsetLeft;
	var parentElement = element.offsetParent;
	
	while (parentElement != null) 
	{
		xPos += parentElement.offsetLeft;
		parentElement = parentElement.offsetParent;
	}
	
	return xPos;
}

/**
 * Function which returns the absolute top-left position of the given element in (x,y) coordinates
 * @param object element the element
 * @return integer the absolute left position  
 */  
DHTML.prototype.getElementTopLeft = function(element)
{
	var xPos = element.offsetLeft;
	var yPos = element.offsetTop;
	
	var parentElement = element.offsetParent;
	
	while (parentElement != null) 
	{
		xPos += parentElement.offsetLeft;
		yPos += parentElement.offsetTop;
		
		parentElement = parentElement.offsetParent;
	}
	
	return new Array(xPos, yPos);
}

/**
 * Function which removes all the events on the given element.
 * @param object element the element
 * @todo:
 * - Test this function. Don't know if this function works.   
 */ 
DHTML.prototype.removeEvents = function(element)
{
	if(element && element.clientY) {
		element = document.body;
	}

	if(element.childNodes) {
		for(var i = 0; i < element.childNodes.length; i++)
		{
			var childElement = element.childNodes[i];
	
			if(childElement.events) {
				for(var eventType in childElement.events)
				{
					for(var guid in childElement.events[eventType])
					{
						for(var property in childElement.events[eventType][guid])
						{
							childElement.events[eventType][guid][property] = null;
						}
						
						childElement.events[eventType][guid] = null;
					}
					
					childElement.events[eventType] = null;
					
					if(childElement.removeEventListener) {
						childElement.removeEventListener(eventType, handleEvent, false);
					}
					
					if(childElement.detachEvent) {
						childElement.detachEvent("on" + eventType, handleEvent);
					}
				}
				
				childElement.events = null;
			}
			
			if (childElement.childNodes && childElement.childNodes.length > 0)
				dhtml.removeEvents(childElement);
		}
	}
}

/**
 * Function which removes an event on the given element.
 * @param object element the element
 * @param type the event (click, mouseover, ...) without the on
 * @param function handler the callback function   
 */  
DHTML.prototype.removeEvent = function(element, type, handler)
{
	// delete the event handler from the hash table
	if (element.events && element.events[type]) {
		delete element.events[type][handler.$$guid];
	}
}

/**
 * Function which selects a range (used if shift()- key is used).
 * @param string startElementID the start element id, start of the range
 * @param string endElementID the end element id, end of the range
 * @param string className the className which will be added to each selected element
 */ 
DHTML.prototype.selectRange = function(startElementID, endElementID, className)
{
	var selectedElements = new Array();
	
	var startElement = this.getElementById(startElementID);
	var endElement = this.getElementById(endElementID);

	var elements = startElement.parentNode.childNodes;
	var betweenElement = false;

	if(startElement != endElement) {
		for(i=0; i<elements.length; i++)
		{
			if(elements[i] == startElement || elements[i] == endElement || betweenElement == true) {
				if(elements[i].id) {
					this.selectElement(elements[i].id, className);
					selectedElements.push(elements[i].id);
		
					if ((startElement == elements[i] || endElement == elements[i]) && betweenElement == true) {
						betweenElement = false;
					} else {
						betweenElement = true;
					}
				}
			}
		}
	} else {
		this.selectElement(startElement.id, className);
		selectedElements.push(startElement.id);
	}
	
	return selectedElements;
}

/**
 * Function which deselects the given elements
 * @param array elements list of element ids which will be deselected
 * @param string className the className which will be removed from each selected element
 */ 
DHTML.prototype.deselectElements = function(elements, className)
{
	if(elements instanceof Array) {
		for(var i = 0; i < elements.length; i++) 
		{
			this.deselectElement(elements[i], className);
		}
	}
}

/**
 * Function which selects an element
 * @param string id the id of the element
 * @param string className the className which will be add to the element
 */ 
DHTML.prototype.selectElement = function(id, className)
{
	var element = this.getElementById(id);

	if(element) {
		element.className += " " + className;
	}
}

/**
 * Function which deselects an element
 * @param string id the id of the element
 * @param string className the className which will be removed from the element
 */ 
DHTML.prototype.deselectElement = function(id, className)
{
	var element = this.getElementById(id);

	if (element && element.className.indexOf(className) > 0)
		element.className = element.className.substring(0, element.className.indexOf(className));
}

/**
 * Function which sets a list of events on an element.
 * @param integer moduleID the module id
 * @param object element the element
 * @param object events the list of events which will be set 
 */  
DHTML.prototype.setEvents = function(moduleID, element, events)
{
	for(var eventType in events)
	{
		if(typeof events[eventType] == "object") {
			for(var i = 0; i < events[eventType].length; i++)
			{
				this.addEvent(moduleID, element, eventType, events[eventType][i]);
			}
		} else {
			this.addEvent(moduleID, element, eventType, events[eventType]);
		}
	}
}

/**
 * Function which verifies if the given element should hide. This function is used
 * to show or hide contextmenu, defaultmenu or combobox. 
 * @param object element the element
 * @param integer posX the x position of the mouse
 * @param integer posY the y position of the mouse
 * @param boolean removeElement true - remove element from DOM, false - hide element     
 */ 
DHTML.prototype.showHideElement = function(element, posX, posY, removeElement)
{
	var elementLeft = this.getElementLeft(element);
	var elementTop = this.getElementTop(element);
		
	if(!(posX > elementLeft && posX < (elementLeft + element.clientWidth) && posY > elementTop && posY < (elementTop + element.clientHeight))) {
		if(removeElement) {
			this.deleteElement(element);
		} else {
			element.style.display = "none";
		}
	}
}

/**
 * Function which sets a value on a inout or select element.
 * @param object element the input or select element
 * @param string value the value.
 * @return boolean indicates if the value is set properly  
 */ 
DHTML.prototype.setValue = function(element, value)
{
	var isValueSet = false;
	
	switch(element.tagName.toLowerCase())
	{
		case "select":
			for(var i = 0; i < element.options.length; i++)
			{
				if(element.options[i].value == value) {
					element.options[i].selected = true;
					isValueSet = true;
				}
			}
			break;
		case "input":
			switch(element.type)
			{
				case "checkbox":
					if(value) {
						element.checked = true;
					} else {
						element.checked = false;
					}
					
					isValueSet = true;
					break;
				case "radio":
					element.checked = true;
					break;
			}
			break;
	}
	
	return isValueSet;
}

/**
 * Function which returns the value of the given input or select element.
 * @param object element the input or select element
 * @return string the value  
 */ 
DHTML.prototype.getValue = function(element)
{
	var value = false;
	
	if(element) {
		switch(element.tagName.toLowerCase())
		{
			case "select":
				value = element.value;
				break;
			case "input":
				switch(element.type)
				{
					case "checkbox":
						value = element.checked;
						break;
					case "radio":
					
						break;
				}
				break;
		}
	}
	return value;
}

/**
 * Function which sets a date in an input field (dd-mm-yyyy).
 * @param object element the element which contains an unixtime attribute
 */  
DHTML.prototype.setDate = function(element)
{
	if(element) {
		var unixtime = element.getAttribute("unixtime");
		
		if(unixtime) {
			var date = new Date(unixtime * 1000);
			
			if(date) {
				var text_element = this.getElementById("text_" + element.id);
				
				if(text_element) {
					text_element.value = date.print(_("%d-%m-%Y"));
				}
				else{
					element.value = date.print(_("%d-%m-%Y"));
				}
			}
		}
	}
}

/**
 * Function which sets a time in an input field (hh:mm).
 * @param object element the element which contains an unixtime attribute
 */  
DHTML.prototype.setTime = function(element)
{
	if(element) {
		var unixtime = element.getAttribute("unixtime");
		
		if(unixtime) {
			var date = new Date(unixtime * 1000);
			
			if(date) {
				var text_element = this.getElementById("text_" + element.id + "_time");
				
				if(text_element) {
					text_element.value = date.print(_("%H:%M"));
				}
				else{
					element.value = date.print(_("%H:%M"));
				}
			}
		}
	}
}

/**
 * Function will add classname to element
 * @param element = html element
 * @param className = string of new classname  
 * @return true if successful
 */
DHTML.prototype.addClassName = function(element, className)
{
	var result = false;
	if(element && !this.hasClassName(element, className)){
		element.className += " "+className;
		result = true;
	}
	return result;
}

/**
 * Function will remove classname from element
 * @param element = html element
 * @param className = string of classname  
 * @return true if successful
 */
DHTML.prototype.removeClassName = function(element, className)
{
	var result = false;
	if(element && this.hasClassName(element, className)){
		var oldName = " "+element.className+" ";
		element.className = oldName.replace(" "+className+" "," ").trim();
		result = true;
	}
	return result
}

/**
 * Function will remove an old classname and add a new classname from element
 * @param element = html element
 * @param oldClass = string of the old classname  
 * @param newClass = string of the new classname  
 * @return true if successful
 */
DHTML.prototype.swapClassNames = function(element, oldClass, newClass)
{
	var result = false;
	if (this.removeClassName(element, oldClass)){
		result = this.addClassName(element, newClass);
	}
	return result;
}

/**
 * Function will toggle a classname
 * @param element = html element
 * @param className = string of classname  
 * @return true if classname is added, else false
 */
DHTML.prototype.toggleClassName = function(element, className)
{
	var result = false;
	if (this.hasClassName(element, className)){
		this.removeClassName(element, className);
	}else{
		result = this.addClassName(element, className);
	}
	return result;
}

/**
 * Function will check if classname exists in element
 * @param element = html element
 * @param className = string of classname
 * @return true if exists else false 
 */
DHTML.prototype.hasClassName = function(element, className)
{
	var result = false;
	if(element){
		var currentName = " "+element.className+" ";
		if (currentName.indexOf(" "+className+" ")!=-1){
			result = true;
		}
	}
	return result;
}

/**
 * Function will get the index of the position of the cursor in the textfield.
 * @param obj string|obj  reference to the textfield or the id of the textfield
 * @return int index of position, -1 if object is invalid
 */
DHTML.prototype.getCaretPosAtTextField = function(obj){
	// Convert string to object
	if(typeof obj == "string"){
		obj = document.getElementById(obj);
	}
	if(obj && typeof obj == "object"){
		// MSIE has to use selection object in document, FF does not have this object
		if(document.selection) { // MSIE
			/**
			 * In MSIE the position is calculated by first creating a selection 
			 * from the cursors position to the start of the string and 
			 * calculating the length of this selection string. This number is 
			 * the actual position of cursor.
			 */
			obj.focus();
			var range = document.selection.createRange().duplicate();
			range.moveStart("character", -obj.value.length);
			return range.text.length;
		}else if (obj.selectionStart || (obj.selectionStart == "0")) { // Mozilla/Netscape…
			// Just use the kickass selectionStart property in FF.
			return obj.selectionStart;
		}
	}
	return -1;
}


/**
 * Function makes a selection in the textfield
 * @param obj            string|obj reference to the textfield or the id of the textfield
 * @param selectionStart int        index of the starting position of the selection
 * @param selectionEnd   int        index of the ending position of the selection
 */
DHTML.prototype.setSelectionRange = function(obj, selectionStart, selectionEnd) {
	// Convert string to object
	if(typeof obj == "string"){
		obj = document.getElementById(obj);
	}
	if (obj && typeof obj == "object" && obj.setSelectionRange) {	// Mozilla/Netscape…
		obj.focus();
		obj.setSelectionRange(selectionStart, selectionEnd);
	}else if (obj.createTextRange) {	// MSIE
		var range = obj.createTextRange();
		range.collapse(true);
		range.moveEnd('character', selectionEnd);
		range.moveStart('character', selectionStart);
		range.select();
	}
}
/**
 * Function replaces selection in the textfield
 * @param obj    string|obj  Reference to the textfield or the id of the textfield
 * @param sText  string      Text that should replace selection
 */
DHTML.prototype.textboxReplaceSelection = function(obj, sText) {
	// Convert string to object
	if(typeof obj == "string"){
		obj = document.getElementById(obj);
	}
	if(obj && typeof obj == "object"){
		if(document.selection) { // MSIE
			var oRange = document.selection.createRange();
			oRange.text = sText;
			oRange.collapse(true);
			oRange.select();
		}else if (obj.selectionStart || (obj.selectionStart == "0")) { // Mozilla/Netscape…
			var iStart = obj.selectionStart;
			obj.value = obj.value.substring(0, iStart) + sText + obj.value.substring(obj.selectionEnd, obj.value.length);
			obj.setSelectionRange(iStart + sText.length, iStart + sText.length);
		}
		obj.focus();
	}
}


/**
 * Function to retrieve the size of the browser window
 *@param w  window object, if not given the current window will be used
 *@return object with x and y properties
 */
DHTML.prototype.getBrowserInnerSize = function(w) 
{
	var x,y;
	if (!w){
		w = window;
	}
	
	if (w.innerHeight){ 
		// FireFox
		x = w.innerWidth;
		y = w.innerHeight;
	} else if (w.document.documentElement && w.document.documentElement.clientHeight) {
		// IE Strict mode
		x = w.document.documentElement.clientWidth;
		y = w.document.documentElement.clientHeight;
	} else if (w.document.body) {
		// IE Normal mode
		x = w.document.body.clientWidth;
		y = w.document.body.clientHeight;
	}
	return {"x":x,"y":y};
}

/*
* Every event enters in this function. The 'this' object is from the element
* which the event was fired from.
*/
function handleEvent(event) 
{
	var returnValue = true;

	// Grab the event object (IE uses a global event object)
	// If window.event does not exist, try to see if the this.windowObj has been 
	// set. After that set event to false.
	event = (event?event:(window.event?fixEvent(window.event):((this.windowObj&&this.windowObj.event)?fixEvent(this.windowObj.event):false)));

	// get a reference to the hash table of event handlers
	// if event is not set, get de handlers for the event "click"
	if (this.events){
		var handlers = ((event&&this.events[event.type])?this.events[event.type]:this.events["click"]);

		// execute each event handler
		for (var i in handlers) 
		{
			if (typeof handlers[i] == "object" && typeof handlers[i]["handler"] != "undefined"){
				var handleEvent = handlers[i]["handler"];
			
				var module = (typeof webclient != "undefined")?webclient.getModule(handlers[i]["moduleID"]) : false;
				if (module==false && (typeof handlers[i]["moduleID"]) == "object"){
					// if moduleID == object, handle it as an object and don't return false
					module = handlers[i]["moduleID"];
				}
			
				if (handleEvent.call(module, module, this, event) === false) {
					returnValue = false;
				}
			}
		}
	}else{
		returnValue = false;
	}

	if ((typeof webclient != "undefined") && webclient.xmlrequest){
		webclient.xmlrequest.sendRequest(); // request is only send when xmlrequest has something to send.
	}
	return returnValue;
}

function fixEvent(event) 
{
	// add W3C standard event methods
	event.preventDefault = fixEvent.preventDefault;
	event.stopPropagation = fixEvent.stopPropagation;

	if (typeof event.target == "undefined"){
		event.target = event.srcElement;
	}

	return event;
}

fixEvent.preventDefault = function() 
{
	this.returnValue = false;
}

fixEvent.stopPropagation = function() 
{
	this.cancelBubble = true;
}

/**
* Everytime a click event occurs on the BODY element this function is executed.
* (This event is set in index.php in the body tag).
* 
* This function checks the following three things:
* - It checks if the contextmenu must be deleted.
* - It checks if the defaultmenu must be hidden.
* - It check if all the combo boxes must be hidden.
*/
function checkMenuState(moduleObject, element, event)
{
	var srcElement = (event.srcElement?event.srcElement:event.target);

	// Check contextmenu
	var contextmenu = dhtml.getElementById("contextmenu");
	
	if(contextmenu) {
		// for Linux-FF dont hide contextmenu on mouseup event of right mousebutton.
		if(typeof event.hideContextMenu == "undefined" || event.hideContextMenu != false)
			dhtml.showHideElement(contextmenu, event.clientX, event.clientY, true);
	}
	
	// Check defaultmenu
	if(srcElement.className && srcElement.className.indexOf("arrow") < 0) {
		var defaultmenu = dhtml.getElementById("defaultmenu");

		if(defaultmenu) {
			dhtml.showHideElement(defaultmenu, event.clientX, event.clientY);
		}
	}
	
	// Check ComboBox's
	if(srcElement.className && srcElement.className.indexOf("arrow") < 0) {
		var comboBoxes = dhtml.comboBoxes;
		
		for(var i in comboBoxes)
		{
			var combobox = dhtml.getElementById(comboBoxes[i]);
			
			if(combobox) {
				dhtml.showHideElement(combobox, event.clientX, event.clientY);
			}
		}
	}

	// check if there is any selection on body text, if then clear it
	dhtml.clearBodySelection();
}

/**
 * Clones an object by copying its contents and returning the copied object
 *
 * @param object object Object to copy
 * @return object copied object
 */
DHTML.prototype.clone = function(object, deep)
{
    var dst = new Object();
    
    for(var key in object) {
        if(typeof object[key] == "object") {
            if(deep)
                dst[key] = clone(object[key]);
            else
                dst[key] = object[key];
        } else {
            dst[key] = object[key];
        }
    }
    
    return dst;
}

/**
 * converts values passed in URL to key value object pairs
 *
 * @param string part of the URL which contains GET variables
 * @return object key/value pair object (associative array)
 */
DHTML.prototype.URLToObject = function(url) {
	var keyValuePairs = new Object();
	var attributes = new Array();
	var key, value;

	if(url.length > 0) {
		attributes = url.split("&");

		if(attributes.length > 0) {
			for(var i in attributes) {
				if(attributes[i].indexOf("=") != -1) {
					key = attributes[i].split("=")[0];
					value = attributes[i].split("=")[1];
					keyValuePairs[key] = value;
				}
			}
		}
	}

	return keyValuePairs;
}

/**
 * Scrolls the specified iframe so that the specified anchor is
 * in view
 *
 * @param string iframe ID
 * @param string anchor name
 */
DHTML.prototype.scrollFrame = function(iframe, id)
{
	var frame = document.getElementById(iframe);
	var el = frame.contentWindow.document.getElementsByName(id)[0];
	
	if(!el || !frame)
		return;
		
	frame.contentWindow.document.body.scrollTop = el.offsetTop;
}

/**
 * similar to array.push function, but it can also remove entries
 * @param		Array			data_array			array to push elements in
 * @param		String/Number	data				data to push in array
 * @param		Boolean			removeIfExists		if true then remove from array
 * @param		Boolean			allowDuplicates		if true then allow duplicate entries
 * @return		Boolean								returns operation performed or not
 */
DHTML.prototype.array_push = function(data_array, data, removeIfExists, allowDuplicates)
{
	if(data_array.constructor != Array) {
		return false;
	}

	if(typeof removeIfExists == "undefined") {
		removeIfExists = false;
	}

	if(typeof allowDuplicates == "undefined") {
		allowDuplicates = false;
	}

	for(var key in data_array) {
		if(data_array[key] == data) {
			if(removeIfExists) {
				return data_array.splice(key, 1);
			} else {
				if(allowDuplicates) {
					break;
				} else {
					return false;
				}
			}
		}
	}

	data_array.push(data);
	return true;
}

/**
 * Clears the selection on body.
 */
DHTML.prototype.clearBodySelection = function()
{
	if(document.selection) {
		if(document.selection.type == "Text") {
			/**
			 * The selection object provides information about text and elements which are highlighted
			 * with the mouse,So here we only check for type as Text; and whether the element calling
			 * for clearBodySelection is a TextEdit Html element or not.So that we can deselect the Object. 
			 */
			var el =  window.event.srcElement.isTextEdit;
			if(!el && document.selection.createRange().text != "") document.selection.empty();
		}
	} else if (window.getSelection) {
		var selectionObject = window.getSelection();
		var start = selectionObject.focusOffset;
		if(!selectionObject.isCollapsed && start == 0) selectionObject.removeAllRanges();
	}
}

/**
 * Basically used for enabling scrolling in FF3.6
 * @param object event an event object.
 * @return Boolean returns true if event is occurred on scrollbar, false otherwise.
 */

function eventCheckScrolling(event)
{
	// explicitOriginalTarget is Mozilla-specific property.
	if(event && event.explicitOriginalTarget)
	{
		var topleft = dhtml.getElementTopLeft(event.explicitOriginalTarget);

		// Get clientWidht and Height of the elment.
		var clientWidth = event.explicitOriginalTarget.clientWidth;
		var clientHeight = event.explicitOriginalTarget.clientHeight;
		if(clientWidth > 0 && clientHeight >0 && (event.clientX-topleft[0] > clientWidth || event.clientY-topleft[1] > clientHeight)) {
			// Clicking outside viewable area -> must be a click in the scrollbar, allow default action.
			return true;
		}
	}
	return false;
}