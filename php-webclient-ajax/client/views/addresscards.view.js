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
 * --Addresscards View--
 * @type	View
 * @classDescription	This view can be used for contact
 * list module to display the contact items
 * 
 * +----------------------------+   +---+
 * |John Doe                    |   |...|
 * +----------------------------+   +---+
 * |Work:  0(031)123456789      |   | a |
 * |Email: john.doe@foo.com     |   +---+
 * +----------------------------+   | b |
 *                                  +---+
 * 
 * DEPENDS ON:
 * |------> view.js
 * |----+-> *listmodule.js
 * |    |----> listmodule.js
 */

AddressCardsView.prototype = new View;
AddressCardsView.prototype.constructor = AddressCardsView;
AddressCardsView.superclass = View.prototype;

function AddressCardsView(moduleID, element, events, data)
{
	this.element = element;
	this.moduleID = moduleID;
	this.events = events;
	this.data = data;
	
	this.initView();
}

AddressCardsView.prototype.destructor = function()
{
	// Unregister module from InputManager
	webclient.inputmanager.removeObject(webclient.getModule(this.moduleID));

	if(this.pagingTool) {
		this.pagingTool.destructor();
		dhtml.deleteAllChildren(dhtml.getElementById("pageelement_"+ this.id));
	}

	this.element.innerHTML = "";
}

AddressCardsView.prototype.initView = function()
{
	this.contentElement = dhtml.addElement(this.element, "div", "addresscards");
	this.alfabetElement = dhtml.addElement(this.element, "div", "alfabet");
	
	if(this.events["alfabet"]) {
		var alfabet = new Array("...", "123", "a", "b", "c", "d", "e", "f", 
								"g", "h", "i", "j", "k", "l", "m", "n", "o", 
								"p", "q", "r", "s", "t", "u", "v", "w", "x", 
								"y", "z");
		
		for(var i = 0; i < alfabet.length; i++)
		{
			var className = "character";
			if(this.data["character"] == alfabet[i]) {
				className += " characterover";
			}
			
			var character = dhtml.addElement(this.alfabetElement, "div", className, "character_" + alfabet[i], alfabet[i]);
			dhtml.setEvents(this.moduleID, character, this.events["alfabet"]);
		}
	} else {
		alfabetElement.style.width = "0px";
	}

	// quick hack for supporting next/prev page
	this.hackPaging = new Object();

	// add keyboard event
	var module = webclient.getModule(this.moduleID);
	webclient.inputmanager.addObject(module, module.element);
	webclient.inputmanager.bindEvent(module, "keydown", eventAddressCardsViewKeyboard);
	if(typeof eventTableViewKeyboard != "undefined") {
		webclient.inputmanager.unbindEvent(module, "keydown", eventTableViewKeyboard);
	}
	webclient.inputmanager.bindKeyControlEvent(module, module.keys["select"], "keyup", eventCardsViewKeyCtrlSelectAll, true);
}

AddressCardsView.prototype.showEmptyView = function(message)
{
	if(typeof message == "undefined") {
		message = _("There are no contacts to be shown in this view. Click in the right of this screen to view the contacts by letter.");
	}

	var messageElement = dhtml.getElementById("empty_view_message", "div", this.contentElement);

	if(messageElement) {
		// remove previously created message element
		dhtml.deleteElement(messageElement);
	}

	dhtml.addElement(this.contentElement, "div", "empty_view_message", null, message);
}

AddressCardsView.prototype.resizeView = function()
{
	this.contentElement.style.height = (this.element.clientHeight - 1) + "px";
	this.contentElement.style.width = (this.element.clientWidth - this.alfabetElement.clientWidth - (document.all?2:1)) + "px";

	this.alfabetElement.style.height = this.element.clientHeight + "px";
	this.alfabetElement.style.top = this.element.offsetTop + "px";

	var seperators = dhtml.getElementsByClassName("contact_seperator", "div");
	for(var i = 0; i < seperators.length; i++)
	{
		dhtml.deleteElement(seperators[i]);
	} 

	var items = dhtml.getElementsByClassName("contact", "div");
	var posX = 0;
	var posY = 0;
	
	// Position the items
	for(var i = 0; i < items.length; i++)
	{
		var contact = items[i];
		
		if(contact.id) {
			// Position contact item
			if((posY + contact.clientHeight + 7) > this.contentElement.clientHeight) {
				posX += contact.offsetWidth + 5;
				contact.style.top = "0px";
				posY = contact.offsetHeight + 7;
				
				var seperator = dhtml.addElement(this.contentElement, "div", "contact_seperator");
				seperator.style.left = posX + "px";
				seperator.style.height = (this.contentElement.clientHeight - 25) + "px";
				
				posX += 15;
			} else {
				contact.style.top = posY + "px";
				posY += contact.clientHeight + 7;
			}
			
			contact.style.left = posX + "px";
		}
	}
	
	var seperator = dhtml.addElement(this.contentElement, "div", "contact_seperator");
	seperator.style.background = "#FFFFFF";
	seperator.style.left = (posX + 202) + "px";
	seperator.style.height = (this.contentElement.clientHeight - 25) + "px";

	// mousedown event will generate focusid which is used to 
	// execute every other events on icons for first time
	dhtml.executeEvent(this.element, "mousedown");
}

AddressCardsView.prototype.execute = function(items, properties, action)
{
	var entryids = new Array();
	var posX = 0;
	var posY = 0;
	var moduleObject = webclient.getModule(this.moduleID);

	if (items.length==0 && moduleObject.searchInProgress !== true){
		this.showEmptyView();
	}else {
		for(var i = 0; i < items.length; i++)
		{
			var item = items[i];
			if(item && item.childNodes) {
				var entryid = item.getElementsByTagName("entryid")[0];
				
				if(entryid && entryid.firstChild) {
					entryids[i] = entryid.firstChild.nodeValue;
					var contact = this.createContactItem(dhtml.addElement(false, "div"), i, item);
					this.contentElement.appendChild(contact);			
				}
			}
		}
	}

	this.resizeView();	
	return entryids;
}

/**
 * Function which adds an item in the view.
 * @param array item the item which is going to be added in view
 * @param array properties property list
 * @param object action the XML action
 * @return object entry of the item
 */
AddressCardsView.prototype.addItem = function(item, properties, action)
{
	if(item && item.childNodes) {
		// @TODO contact item is not added in sorted order automatically
		var pagingElement = dhtml.getElementById("pageelement_" + this.moduleID);
		if(pagingElement) {
			var pagingNextButton = dhtml.getElementById("page_next", "td", pagingElement);
			if(pagingNextButton && !dhtml.hasClassName(pagingNextButton, "nobutton")) {
				// only add item in view when you are at last page
				return true;
			}
		}

		var selectedCharacter = webclient.getModule(this.moduleID).character;
		var fileasValue = dhtml.getXMLValue(item, "fileas", false);

		// only add contact when letter filtering matches with the contact's fileas field
		// if selectedCharacter is "..." then add contact without checking fileas field
		if(fileasValue && selectedCharacter != "..." && selectedCharacter != "123") {
			if(fileasValue.substring(0, 1) != selectedCharacter) {
				return true;
			}
		} else if(fileasValue && selectedCharacter == "123"){
			if(parseInt(fileasValue.substring(0, 1), 10) < 0 || parseInt(fileasValue.subString(0, 1), 10) > 9) {
				return true;
			}
		}

		var entry = new Object();
		var entryid = dhtml.getXMLValue(item, "entryid", false);
		var contactElements = dhtml.getElementsByClassNameInElement(this.contentElement, "contact", "div");
		var elementId = 0;

		if(contactElements.length != 0) {
			elementId = contactElements[contactElements.length - 1].id;
			elementId = parseInt(elementId, 10) + 1;
		} else {
			// view is empty. then remove empty view message
			dhtml.deleteAllChildren(this.contentElement);
		}

		if(entryid) {
			var contact = this.createContactItem(dhtml.addElement(false, "div"), elementId, item);
			this.contentElement.appendChild(contact);

			entry["id"] = elementId;
			entry["entryid"] = entryid;
		}

		this.resizeView();
		return entry;
	} else {
		return true;
	}
}

AddressCardsView.prototype.deleteItems = function(items)
{
	return false;
}

AddressCardsView.prototype.updateItem = function(element, item, properties)
{
	dhtml.deleteAllChildren(element);
	this.createContactItem(element, element.id, item);
	this.resizeView();
}

AddressCardsView.prototype.createContactItem = function(element, id, item)
{
	// Message class
	var messageClass = "ipm_contact";
	var message_class = item.getElementsByTagName("message_class")[0];
	if(message_class && message_class.firstChild) {
		messageClass = message_class.firstChild.nodeValue.replace(".", "_").toLowerCase();
	}

	// Set title of contact item
	var title = NBSP;
	var fileas = item.getElementsByTagName("fileas")[0];
	if(fileas && fileas.firstChild) {
		title += fileas.firstChild.nodeValue;
	}
	
	// Set attributes
	element.id = "" + id;
	element.className = "contact " + messageClass;
	dhtml.addTextNode(element, title);

	dragdrop.addDraggable(element, "folder");
	
	// Set events
	dhtml.setEvents(this.moduleID, element, this.events["row"]);
	
	// Set address
	var mailing_address = item.getElementsByTagName("mailing_adress")[0];
	if(mailing_address && mailing_address.firstChild) {
		var address = false;
		switch(mailing_address.firstChild.nodeValue)
		{
			case "1":
				address = "home_address";
				break;
			case "2":
				address = "business_address";
				break;
			case "3":
				address = "other_address";
				break;
		}

		if(address) {
			var address_info = item.getElementsByTagName(address)[0];
			
			if(address_info && address_info.firstChild) {
				dhtml.addElement(element, "div", "address", false, address_info.firstChild.nodeValue);
			}
		}
	}
	
	var table = new Array();
	table.push("<table class='data' width='100%' border='0' cellpadding='0' cellspacing='0'>");
	
	// Set phone numbers
	var telephone_numbers = new Object();
	telephone_numbers["office_telephone_number"] = _("Business");
	telephone_numbers["home_telephone_number"] = _("Home");
	telephone_numbers["cellular_telephone_number"] = _("Mobile");
	telephone_numbers["business_fax_number"] = _("Business Fax");
	
	for(var j in telephone_numbers)
	{
		var telephone_number = item.getElementsByTagName(j)[0];

		if(telephone_number && telephone_number.firstChild) {
			table.push("<tr><td class='info'>" + telephone_numbers[j] + ":</td><td class='value'>" + telephone_number.firstChild.nodeValue.htmlEntities() + "</td></tr>");
		}
	}
	
	// Set email addresses
	for(var j = 1; j <= 3; j++)
	{ 
		var email_address = item.getElementsByTagName("email_address_" + j)[0];
		
		if(email_address && email_address.firstChild) {
			table.push("<tr><td class='info'>" + _("Email") + (j > 1?" " + j:"") + ":</td><td class='value'>" + email_address.firstChild.nodeValue.htmlEntities() + "</td></tr>");
		}
	}
	
	table.push("</table>");
	element.innerHTML += table.join("");
	
	return element;
}

AddressCardsView.prototype.loadMessage = function()
{
	dhtml.removeEvents(this.contentElement);
	dhtml.deleteAllChildren(this.contentElement);

	this.contentElement.innerHTML = "<center>" + _("Loading") + "...</center>";
	document.body.style.cursor = "wait";
}

AddressCardsView.prototype.deleteLoadMessage = function()
{
	dhtml.deleteAllChildren(this.contentElement);
	document.body.style.cursor = "default";
}

AddressCardsView.prototype.pagingElement = TableView.prototype.pagingElement;
AddressCardsView.prototype.removePagingElement = TableView.prototype.removePagingElement;
/**
 * Function which return all card elements
 * @return array all card elements
 */
AddressCardsView.prototype.getAllCardElements = function()
{
	var cards = new Array();
	var elements = this.contentElement.getElementsByTagName("div");
	for (var i = 0; i < elements.length; i++){
		if (typeof elements[i].id != "undefined" && dhtml.hasClassName(elements[i], "contact")){
			cards.push(elements[i]);
		}
	}
	return cards;
}

// FIXME FIXME this is called with 'mobuleObject' referring not to this view oject, but to our parent module!
function eventAddressCardsViewKeyboard(moduleObject, element, event)
{
	// Set to TRUE if we have really selected the item, like pressing on an item with the mouse
	var openItem = false;
	
	if (typeof moduleObject != "undefined"){

		if (event.type == "keydown"){

			// get the right element
			if (moduleObject && moduleObject instanceof ListModule && typeof moduleObject.selectedMessages[0] != "undefined"){
				switch (event.keyCode){
					case 46: // DELETE
						// event.shiftKey for soft delete if Shift+Del
						moduleObject.deleteMessages(moduleObject.selectedMessages, event.shiftKey);
						break;
				}
			}
		}
	}
}
/**
 * Function which selects all items.
 */
function eventCardsViewKeyCtrlSelectAll(moduleObject, element, event)
{
	// Retrive all row elements
	var elements = moduleObject.viewController.viewObject.getAllCardElements();
	moduleObject.selectMessages(elements, "contactselected");
}