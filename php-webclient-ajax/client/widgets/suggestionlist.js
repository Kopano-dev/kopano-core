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


suggestionList.prototype = new Widget;
suggestionList.prototype.constructor = suggestionList;
suggestionList.superclass = Widget.prototype;

//PUBLIC
/**
 * @constructor This widget will create 1 suggestion list for an input field.
 * @param {Int} id
 * @param {HtmlElement} element
 * @param {String} sourceModule
 */
function suggestionList(id, inputfld, sourceModule, callBackFunction){
	if(arguments.length > 1) {
		this.init(id, inputfld, sourceModule, callBackFunction);
	}
}

suggestionList.prototype.init = function(id, inputfld, sourceModule, callBackFunction){
	this.id = id;
	this.inputfld = inputfld;
	this.sourceModule = sourceModule;
	this.lastSearched = false;
	this.selected = -1;
	this.focusSuggestions = new Array();
	this.callBackFunction = callBackFunction ? callBackFunction : false;

	try{
		// Disable autocomplete functionality of an input field
		if(this.inputfld.type == "text") {
			this.inputfld.setAttribute("autocomplete", "off");
		}

		// Add events to the input field
		dhtml.addEvent(this, this.inputfld, "keyup", eventSuggestionListOnTyping);
		dhtml.addEvent(this, this.inputfld, "keydown", eventSuggestionListOnKeyDown);
		dhtml.addEvent(this, this.inputfld, "blur", eventSuggestionListOnBlur);
		dhtml.addEvent(this, this.inputfld, "click", eventSuggestionListItemOnMouseClick);
	}
	catch(e){
		this.inputfld = null;
	}
}


suggestionList.prototype.getFocussedEmailAddress = function(){
	var l_iCaretPos = dhtml.getCaretPosAtTextField(this.inputfld);

	if(l_iCaretPos >= 0){
		/**
		 * Get the entire string of what is written between the ; where the cursor is located.
		 * Example: ( | = cursor position)
		 * field content: "test ; hello wo|rld ; blub"
		 * return: " hello world "
		 **/
		var l_iEmailIndex = (this.inputfld.value.substr(0, l_iCaretPos).split(";").length - 1)
		var l_aParts = this.inputfld.value.split(";");
		if(typeof l_aParts[l_iEmailIndex] == "string" && l_aParts[l_iEmailIndex].replace(" ", "").length > 0){
			return l_aParts[l_iEmailIndex];
		}
	}
	return undefined;
}

suggestionList.prototype.getRecipientList = function(str){
	// Only get list when there is a source module and the input string has content.
	if(this.sourceModule != null && typeof str == "string" && str.length > 0){
		this.sourceModule.getList(this.id, str);
		this.lastSearched = str;
	}else{
		this.removeSuggestionList();
	}
}

suggestionList.prototype.handleResult = function(str, result){
	this.render(result);
	// Automagically select the first if there are any results.
	if(result.length > 0){
		this.moveSelection("next");
	}
}

suggestionList.prototype.render = function(suggestions){
	this.selected = -1;
	this.numResults = suggestions.length;
	this.focusSuggestions = suggestions;
	try{
		if(this.numResults == 0){
			throw "no_results";
		}

		// Create suggestionlayer if it not already exists
		if(typeof this.suggestlayerElement != "object"){
			this.suggestlayerElement = 
				dhtml.addElement(document.body, "div", "suggestlayer", "suggestlayer[" + this.id + "]");
		}
		// Remove content of suggestion layer
		while(this.suggestlayerElement.childNodes.length){
			// Remove all events from suggestion layer items first.
			dhtml.removeEvents(this.suggestlayerElement.firstChild);
			this.suggestlayerElement.removeChild(this.suggestlayerElement.firstChild);
		}

		// Add list with suggestions
		var list = dhtml.addElement(this.suggestlayerElement, "ul", "suggestlayer_list");
		for(var i=0;i<this.focusSuggestions.length;i++){
			var item = dhtml.addElement(list, "li", "suggestlayer_item", "suggestlayer_item[" + this.id + "]["+i+"]", this.focusSuggestions[i]);
			item.setAttribute("itemID", i);
			dhtml.addEvent(this, item, "click", eventSuggestionListItemOnClick);
			dhtml.addEvent(this, item, "mouseover", eventSuggestionListItemOnMouseOver);
		}
		this.resizeSuggestionList();
	}
	catch (e){
		this.removeSuggestionList();
	}
}

suggestionList.prototype.moveSelection = function(changeInstruction, suggestionId){
	if(changeInstruction == "next" || changeInstruction == "previous"){
		// Move selection
		var newSelection = this.selected;
		if(this.selected <= 0){
			if(changeInstruction == "next"){
				newSelection++;
			}
		}else if(this.selected >= (this.numResults - 1)){
			if(changeInstruction == "previous"){
				newSelection--;
			}
		}else{
			switch(changeInstruction){
				case "next":
					newSelection++;
					break;
				case "previous":
					newSelection--;
					break;
			}
		}
	}else{
		var newSelection = parseInt(suggestionId);
	}
	// Visualize new selection
	if(this.selected != newSelection){
		try{
			// First remove old...
			var OldSelectedItem = dhtml.getElementById("suggestlayer_item[" + this.id + "]["+this.selected+"]", "li", this.suggestlayerElement);
			dhtml.removeClassName(OldSelectedItem, "suggestlayer_item_selected");
			// ...then update selection index...
			this.selected = newSelection;
			// ...and finally select the new list item.
			var NewSelectedItem = dhtml.getElementById("suggestlayer_item[" + this.id + "]["+this.selected+"]", "li", this.suggestlayerElement);
			dhtml.addClassName(NewSelectedItem, "suggestlayer_item_selected");
		}
		catch(e){}
	}
}

suggestionList.prototype.selectSuggestion = function(selection){
	// Check if suggestion list has a selection.
	if(this.focusSuggestions[selection]){
		var l_sEmailLine = this.inputfld.value;
		// Caret position is the position of the cursor in the field
		var l_iCaretPos = this.caretPos || dhtml.getCaretPosAtTextField(this.inputfld);
		// Get position of the last ; before the position of the cursor
		var l_aEmailSegments = l_sEmailLine.substr(0, l_iCaretPos).split(";");

		// Remove empty strings or strings with only spaces from the array.
		for (var i = 0; i < l_aEmailSegments.length; i++) {
			if (l_aEmailSegments[i].trim().length == 0) l_aEmailSegments.splice(i, 1);
		}

		var l_iPreStringChars = 0;
		for(var i=0;i<(l_aEmailSegments.length-1);i++){
			l_iPreStringChars += l_aEmailSegments[i].length + 1; // The +1 is to pass the ;
		}
		// Get the position of the first ; after the position of cursor
		l_iSelectionEndPos = l_sEmailLine.substr(l_iCaretPos, l_sEmailLine.length).indexOf(";");
		if(l_iSelectionEndPos < 0){
			l_iSelectionEndPos = l_sEmailLine.length;
		}else{
			l_iSelectionEndPos += l_iCaretPos;
		}

		// Select the whole section between the both ;  where the cursor is located in between
		dhtml.setSelectionRange(this.inputfld, l_iPreStringChars, l_iSelectionEndPos);
		// Replace the typed input and replace it with the selected suggestion.
		dhtml.textboxReplaceSelection(this.inputfld, (l_iPreStringChars > 0 ? " " : "") + this.focusSuggestions[selection]);

		// Test is the last element of the input field has content (= characters excl. spaces)
		if(this.inputfld.value.split(";").pop().replace(" ", "").length > 0){
			this.inputfld.value += "; ";
		}

		// Move cursor to the end
		dhtml.setSelectionRange(this.inputfld, this.inputfld.value.length, this.inputfld.value.length);
		if(this.inputfld.type == "textarea") {
			this.inputfld.scrollTop = this.inputfld.scrollHeight;
		}

		this.removeSuggestionList();

		// Fire change event on inputfield when selected suggestion is added in field.
		if(this.inputfld)
			dhtml.executeEvent(this.inputfld, "change");

		if (this.callBackFunction){
			this.callBackFunction();
		}

		return true;
	}else{
		return false;
	}
}

suggestionList.prototype.removeSuggestionList = function(){
	this.selected = -1;
	this.numResults = 0;
	try{
		if(typeof this.suggestlayerElement == "object"){
			// Move div out of the viewport
			this.suggestlayerElement.style.left = "-999px";
			this.suggestlayerElement.style.top = "-999px";
			this.suggestlayerElement.style.width = "10px";

			// Remove content of suggestion layer
			while(this.suggestlayerElement.childNodes.length){
				// Remove all events from suggestion layer items first.
				dhtml.removeEvents(this.suggestlayerElement.firstChild);
				this.suggestlayerElement.removeChild(this.suggestlayerElement.firstChild);
			}
		}
	}
	catch(e){}
}

suggestionList.prototype.removeRecipient = function(selection){
	this.sourceModule.deleteRecipient(this.id, this.focusSuggestions[selection]);
	this.getRecipientList(this.getFocussedEmailAddress());
}

suggestionList.prototype.resizeSuggestionList = function(){
	try{
		// Set coordinates and width of suggestion layer
		var coords = dhtml.getElementTopLeft(this.inputfld);
		this.suggestlayerElement.style.left = coords[0] + "px";
		this.suggestlayerElement.style.top = (coords[1] + this.inputfld.offsetHeight) + "px";
		this.suggestlayerElement.style.width = this.inputfld.offsetWidth + "px";
	}
	catch (e){}
}

// Events on input field
function eventSuggestionListOnTyping(moduleObj, element, event){
	// Send request only after 1000ms te prevent flooding the server.
	var currentValue = element.value;
	if(currentValue.length == 0){
		moduleObj.removeSuggestionList();
	}else{
		/**
		 * Save the caret position because when clicking on a suggestion item 
		 * MSIE browsers lose focus on the textfield. This resets the pos to 0.
		 **/
		moduleObj.caretPos = dhtml.getCaretPosAtTextField(element);
		switch(event.keyCode){
			case 37:	// Left
			case 39:	// Right
			case 38:	// Up
			case 40:	// Down
			case 27:	// Esc
				break;
			default:
				clearTimeout(this.timeout);
				this.timeout = setTimeout(function(){
					if(currentValue == element.value){
						moduleObj.getRecipientList(moduleObj.getFocussedEmailAddress());
					}
				}, 200);
		}
	}
}

function eventSuggestionListOnKeyDown(moduleObj, element, event){
	switch(event.keyCode){
		case 38:	// Up
			if(this.numResults > 0 && this.selected !== -1) {
				moduleObj.moveSelection("previous");
				// prevent default action of up key
				event.preventDefault();
			}
			break;
		case 40:	// Down
			if(this.numResults > 0 && this.selected !== -1) {
				moduleObj.moveSelection("next");
				// prevent default action of down key
				event.preventDefault();
			}
			break;
		case 9:	// Tab
			/**
			 * When the widget can select an item from the suggestion list it 
			 * returns true, otherwise it returns false.
			 **/
			if(moduleObj.selectSuggestion(moduleObj.selected)){
				// Event handler returns false to disable effect of TAB key.
				event.preventDefault();
			}
			break;
		case 13:	// Enter
			moduleObj.selectSuggestion(moduleObj.selected);
			// Don't allow newline character in email address field
			event.preventDefault();
			break;
		case 27:	// Esc
		case 37:	// Left
		case 39:	// Right
			moduleObj.removeSuggestionList();
			break;
		case 46:	// Del
			// If any items is selected from suggestion list then remove it.
			if(typeof(moduleObj.selected) != "undefined" && moduleObj.selected >= 0)
				moduleObj.removeRecipient(moduleObj.selected);
			break;
	}
	return undefined;
}

function eventSuggestionListOnBlur(moduleObj, element, event){
	setTimeout(function(){
		moduleObj.removeSuggestionList();
	}, 300);
}

function eventSuggestionListItemOnMouseClick(moduleObj, element, event){
	moduleObj.removeSuggestionList();
}

// Events on list items
function eventSuggestionListItemOnMouseOver(moduleObj, element, event){
	moduleObj.moveSelection("mouse", element.getAttribute("itemID"));
}

function eventSuggestionListItemOnClick(moduleObj, element, event){
	moduleObj.selectSuggestion(element.getAttribute("itemID"));
}



