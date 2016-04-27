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

buttonsmodule.prototype = new Module;
buttonsmodule.prototype.constructor = buttonsmodule;
buttonsmodule.superclass = Module.prototype;

function buttonsmodule(id, element)
{
	if(arguments.length > 0) {
		this.init(id, element);
	}
}

buttonsmodule.prototype.init = function(id, element, title, data)
{
	if(data) {
		for(var property in data)
		{
			this[property] = data[property];
		}
	}
	
	this.buttonSize = webclient.settings.get("buttons/button_size", "large");
	this.setSize(this.buttonSize);
	
	buttonsmodule.superclass.init.call(this, id, element, title, data);
}

buttonsmodule.prototype.execute = function(type, action)
{
	switch(type)
	{
		case "list":
			this.element.id = "buttons";
			this.element.style.cursor = "row-resize";
			this.folders = action.getElementsByTagName("folders")[0];
			this.createButtons();
			break;
	}
}

buttonsmodule.prototype.createButtons = function()
{
	var resize_button = dhtml.addElement(this.element, "div", "default_buttons_top", "resize_button");
	dhtml.addEvent(this.id, resize_button, "mousedown", eventButtonsResizeMouseDown);
	var large = 0;
	
	//As resizing of buttonsmodule has been implemented, container for small buttons is always needed.
	var buttonContainer = dhtml.addElement(this.element, "div", "default_button small_button_container", "buttonContainer");
	
	var buttonCount = 0;
	this.button = new Array();
	for(var i = 0; i < this.folders.childNodes.length; i++)
	{
		var folder = this.folders.childNodes[i];
		
		if(folder && folder.hasChildNodes()) {
			var button = new Object();
			
			for(var j = 0; j < folder.childNodes.length; j++)
			{
				var buttonData = folder.childNodes[j];
						
				if(buttonData.firstChild) {
					button[buttonData.nodeName] = buttonData.firstChild.nodeValue;
				}
			}
			
			if(button["entryid"] && button["title"] && button["icon"]) {
				buttonCount++;
				var buttonElement;
				if (this.buttonSize == "large"){
					buttonElement = this.createLargeButtons(button, false); 
				} else if (this.buttonSize == "small"){
					buttonElement = this.createSmallButtons(button, false);
				} else {
					if (large < this.buttonSize){
						buttonElement = this.createLargeButtons(button, false);
						large++;
					} else {
						if (!buttonContainer.parentNode){
							this.element.appendChild(buttonContainer);
						}
						buttonElement = this.createSmallButtons(button, false);
					}
				}
				buttonElement.setAttribute("accesskey", buttonCount);
				dhtml.addEvent(this.id, buttonElement, "click", eventButtonsClick);
				
				//Store button info, as it is needed in other functions.
				this.button.push(button);
			}
		}
	}
	
	// Update total no. of buttons
	this.totalButtons = this.button.length;
	this.setSize(this.buttonSize);
	
	//add buttonContainer for small buttons at end in moduleObject.element
	this.element.appendChild(buttonContainer);
	
	webclient.layoutmanager.updateElements("left");
}

buttonsmodule.prototype.list = function()
{
	webclient.xmlrequest.addData(this, "list");
}

buttonsmodule.prototype.resize = function()
{
	
}

buttonsmodule.prototype.setSize = function(buttonSize)
{
	this.buttonSize = buttonSize;
	
	switch(buttonSize)
	{
		case "large":
			this.elementHeight = 152;
			this.large = this.totalButtons;
			break;
		case "small":
			this.elementHeight = 29;
			this.large = 1;
			break;
		default:	

			//Calculate height of element, if has both small and large buttons.	
		 	this.large = this.buttonSize;
		 	this.updateElementHeight();
		 	break;
	}
}

/**
 * Function which calculates height of element, if has both small and large buttons.	
 * @return integer -height of element
 */
buttonsmodule.prototype.updateElementHeight = function()
{
	if (this.large <= 0){
		this.elementHeight = 29;
		this.large = 0;
	} else if (this.large >= this.totalButtons){
		this.elementHeight = 152;
		this.large = this.totalButtons;
	} else {
		this.elementHeight = (this.large * 29) + 30;
	}
}


/**
 * Function which creates large button
 * @param	array	-button info that is to be created
 * @param	element	-element before which to insert large button
 * @return	element	-large button just created.
 */
buttonsmodule.prototype.createLargeButtons = function(button, insertBeforeElement)
{
	var buttonElement = dhtml.addElement(false, "a", "default_button", "button" + button["entryid"]);
				
	var buttonIcon = dhtml.addElement(buttonElement, "span", "default_button_icon folder_icon_" + button["icon"]);
	buttonIcon.innerHTML = NBSP;
	dhtml.addElement(buttonElement, "span", false, false, button["title"]);
	
	if (insertBeforeElement) {
		this.element.insertBefore(buttonElement, insertBeforeElement);
	} else {
		this.element.appendChild(buttonElement);
	}

	dhtml.addEvent(this.id, buttonElement, "mouseover", eventButtonsMouseOver);
	dhtml.addEvent(this.id, buttonElement, "mouseout", eventButtonsMouseOut);
	dhtml.addEvent(this.id, buttonElement, "click", eventButtonsClick);
	
	return buttonElement;
}

/**
 * Function which small large button
 * @param	array	-button info that is to be created
 * @param	element	-element before which to insert small button
 * @return	element	-small button just created.
 */
buttonsmodule.prototype.createSmallButtons = function(button, insertBeforeElement)
{
	var buttonContainer = dhtml.getElementById("buttonContainer");
	var buttonElement = dhtml.addElement(false, "a", "default_button_icon folder_icon_" + button["icon"], "button" + button["entryid"]);
	buttonElement.title = button["title"];
	buttonElement.innerHTML = NBSP;
	
	if (insertBeforeElement == "first") {
		buttonContainer.insertBefore(buttonElement, buttonContainer.firstChild);
	} else {
		buttonContainer.appendChild(buttonElement);
	}
	dhtml.addEvent(this.id, buttonElement, "click", eventButtonsClick);
	
	return buttonElement;
}

function eventButtonsMouseOver(moduleObject, element, event)
{
	element.className += " default_button_over";
}

function eventButtonsMouseOut(moduleObject, element, event)
{
	element.style.cursor = "pointer";
	dhtml.removeClassName(element, "default_button_over");
}

function eventButtonsClick(moduleObject, element, event)
{
	var folderElement = dhtml.getElementById(element.id.substring(6));
	var folderIcon = folderElement.getElementsByTagName("div")[1];
	
	if(folderIcon && moduleObject.hierarchy) {
		eventHierarchySelectFolder(moduleObject.hierarchy, folderIcon, event);
		eventHierarchyChangeFolder(moduleObject.hierarchy, folderIcon, event);
	}
}


/**
 * Function which create div over Webaccess and 
 * registers mouse event on that div for resizing buttons.
 */
function eventButtonsResizeMouseDown(moduleObject, element, event)
{
	var left = dhtml.getElementById("left");

	var divOverLeftPane = dhtml.addElement(document.body, "div", "divover", "divOverLeftPane");
		
	//Set size and position of div element
	divOverLeftPane.style.left = left.offsetLeft + "px";
	divOverLeftPane.style.top = left.offsetTop + "px";
	divOverLeftPane.style.width = left.clientWidth + "px";
	divOverLeftPane.style.height = left.clientHeight + "px";
	divOverLeftPane.style.cursor = "row-resize";
	
	//Set events which will call resizing events of buttons
	dhtml.addEvent(moduleObject.id, divOverLeftPane, "mousemove", eventDivOverLeftPaneMouseMove);
	dhtml.addEvent(moduleObject.id, divOverLeftPane, "mouseout", eventDivOverLeftPaneMouseOut);
	dhtml.addEvent(moduleObject.id, divOverLeftPane, "mouseup", eventDivOverLeftPaneMouseOut);
}

/**
 * Function which resizes buttons.
 */
function eventDivOverLeftPaneMouseMove(moduleObject, element, event)
{
	var left = dhtml.getElementById("left");
	var resize_button = dhtml.getElementById("resize_button");
	var buttonsElement = dhtml.getElementById("buttons");
	var buttonContainer = dhtml.getElementById("buttonContainer");
	
	if (buttonsElement) {
		
		if (event.clientY > (left.offsetTop + buttonsElement.offsetTop + 32)){
			moduleObject.large -= 1;
			
			//Check if any 
			if (moduleObject.button[moduleObject.large]){
				//Get large button and delete it
				var largeButton = dhtml.getElementById("button"+ moduleObject.button[moduleObject.large]["entryid"]);
				dhtml.deleteElement(largeButton);		
	
				//Create small button and register necessary events...
				var ele = moduleObject.createSmallButtons(moduleObject.button[moduleObject.large], "first");
				ele.setAttribute("accesskey", largeButton.accesskey);
			}
		} else if(event.clientY < (left.offsetTop + buttonsElement.offsetTop - 32)){

			if (moduleObject.button[moduleObject.large]){			
				//Get button and delete it.
				var smallButton = dhtml.getElementById("button"+ moduleObject.button[moduleObject.large]["entryid"]);
				dhtml.deleteElement(smallButton);
				
				var ele = moduleObject.createLargeButtons(moduleObject.button[moduleObject.large], buttonContainer);
				ele.setAttribute("accesskey", smallButton.accesskey);
				moduleObject.large += 1;
			}
		}
		//needs to update the height of button's element after changing/resizing any button
		moduleObject.updateElementHeight();
		webclient.layoutmanager.updateElements("left");
	}
}

/**
 * Function which saves buttons sizes in settings
 * whenever mouse get outside the div over left pane
 * or whenever there is mouseup event on div over left pane.
 */
function eventDivOverLeftPaneMouseOut(moduleObject, element, event)
{
	//Save buttonsize in settings
	if (moduleObject.large >= moduleObject.totalButtons){
		moduleObject.buttonSize = "large";
		webclient.settings.set("buttons/button_size", moduleObject.buttonSize);
	} else if (moduleObject.large <= 0){
		moduleObject.buttonSize = "small";
		webclient.settings.set("buttons/button_size", moduleObject.buttonSize);
	} else {
		webclient.settings.set("buttons/button_size", moduleObject.large);
	}
	
	var divOverLeftPane = dhtml.getElementById("divOverLeftPane");
	if (divOverLeftPane){
		dhtml.deleteElement(divOverLeftPane);
	}
}