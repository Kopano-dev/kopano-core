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
 * Recipient Input Widget
 */
//TODO: unbind from inputmanager in destructor

RecipientInputWidget.prototype = new Widget;
RecipientInputWidget.prototype.constructor = RecipientInputWidget;
RecipientInputWidget.superclass = Widget.prototype;

function RecipientInputWidget(){
	this.STATUS_PENDING = 1;
	this.STATUS_RESOLVED = 2;
	this.STATUS_MULTI_RESOLVE_OPTIONS = 3;
	this.STATUS_INVALID = 4;

	this.parentElement = false;
	this.element = false;

	this.lastInternalID = 1;
	this.data = new Object();
	this.dataLookup = new Array(); // Used to easily find next/previous/first/last recipient.
	this.selection = false;
	this.resolveMethod = false;

	this.htmlRecipContainer = new Object();
	this.htmlRecipBlocks = new Object();
	this.htmlRecipListItem = new Object();
	this.htmlInputField = false;

	this.KEY_LEFT = 37;
	this.KEY_UP = 38;
	this.KEY_RIGHT = 39;
	this.KEY_DOWN = 40;
	this.KEY_ENTER = 13;
	this.KEY_DELETE = 46;
	this.KEY_BACKSPACE = 8;
	this.KEY_TAB = 9;
	this.KEY_HOME = 36;
	this.KEY_END = 35;
	this.KEY_PAGEUP = 33;
	this.KEY_PAGEDOWN = 34;
	this.KEY_ESC = 27;
}

RecipientInputWidget.prototype.init = function(parentElement, windowObj){
	RecipientInputWidget.superclass.init(this);

	this.parentElement =  parentElement;
	this.windowObj = windowObj || window;

	this._controlInitWidget();
}

/** API **/
RecipientInputWidget.prototype.addRecipient = function(displayname, status, data){
	var internalID = this._controlAddNewRecipient(displayname, status, data);
	return internalID;
}
RecipientInputWidget.prototype.removeRecipient = function(internalID){
	return this._controlRemoveRecipient(internalID);
}
RecipientInputWidget.prototype.getAllRecipients = function(){
	return this._controlGetAllRecipientInternalIDs();
}
RecipientInputWidget.prototype.getRecipientData = function(internalID){
	return this._controlGetRecipientData(internalID);
}
RecipientInputWidget.prototype.setRecipientData = function(internalID, data, overwrite){
	return this._controlSetRecipientData(internalID, data, overwrite);
}

RecipientInputWidget.prototype.setResolveMethod = function(method, scope){
	return this._controlSetResolveMethod(method, scope);
}
RecipientInputWidget.prototype.notifyResolveResult = function(result){
	return this._controlHandleResolveResult(result);
}
RecipientInputWidget.prototype.getSelection = function(){
	return this._controlGetSelection();
}
RecipientInputWidget.prototype.searchRecipient = function(searchData){
	return this._controlSearchRecipient(searchData);
}
RecipientInputWidget.prototype.forceInterpretInput = function(internalID){
	if(internalID){
		return this._controlInterpretEntry(internalID);
	}else{
		return this._controlInterpretInput();
	}
}



/** CONTROL **/
RecipientInputWidget.prototype._controlInitWidget= function(){
	this._htmlSetupBaseInterface();
}
RecipientInputWidget.prototype._controlGenerateInternalID = function(){
	return this.lastInternalID++;
}
RecipientInputWidget.prototype._controlAddNewRecipient = function(displayname, status, data){
	// We need a new internal ID for this recipient
	var internalID = this._controlGenerateInternalID();

	// Add Recipient to DATA
	this.data[ internalID ] = new Object();
	this.data[ internalID ]["internalID"] = internalID;
	this.data[ internalID ]["displayname"] = displayname;
	this.data[ internalID ]["status"] = status;
	this.data[ internalID ]["data"] = data;
	this.dataLookup.push(internalID);

	// Lets show this block!
	this._htmlAddRecipientBlock(internalID, displayname, status);

	return internalID;
}
RecipientInputWidget.prototype._controlRemoveRecipient = function(internalID){
	// Remove the data of this recipient
	delete this.data[ internalID ];
	var lookupIndex = this._controlFindRecipientLookupIndex(internalID);
	if(lookupIndex !== false)
		this.dataLookup.splice(lookupIndex, 1);

	if(this.selection == internalID)
		this.selection = false;

	// Remove recipient block
	this._htmlRemoveRecipientBlock(internalID);

	// Trigger internal event
	this.sendEvent("entry_deleted", this, internalID);
}

RecipientInputWidget.prototype._controlGetRecipientData = function(internalID){
	if(this.data[ internalID ]){
		return this.data[ internalID ]["data"];
	}else{
		return false;
	}
}
RecipientInputWidget.prototype._controlSetRecipientData = function(internalID, data, overwrite){
	if(this.data[ internalID ]){
		if(overwrite){
			this.data[ internalID ]["data"] = data;
		}else{
			if(typeof data == "object" && typeof this.data[ internalID ]["data"] == "object"){
				if(typeof this.data[ internalID ]["data"] != "object"){
					this.data[ internalID ]["data"] = new Object();
				}
				// The data will only be merged on the top level
				for(var i in data){
					this.data[ internalID ]["data"][i] = data[i];
				}
			}else{
				return false;
			}
		}
	}else{
		return false;
	}
}
RecipientInputWidget.prototype._controlSetResolveMethod = function(method, scope){
	if(typeof method == "function"){
		this.resolveMethod = new Object();
		this.resolveMethod["method"] = method;
		this.resolveMethod["scope"] = scope || this.windowObj;
	}else{
		return false;
	}
}
RecipientInputWidget.prototype._controlHandleResolveResult = function(result){
	if(typeof result == "object"){
		for(var i in result){
			var internalID = result[i]["internalID"] || false;
			var status = result[i]["status"] || false;
			var resolveOptions = result[i]["options"] || false;
			if(internalID && status && this.data[ internalID ]){
				this.data[ internalID ]["status"] = status;
				if(resolveOptions){
					this.data[ internalID ]["options"] = resolveOptions;
				}
				this._htmlUpdateRecipientBlock(internalID, this.data[ internalID ]["displayname"], status);
			}
		}
	}
}
RecipientInputWidget.prototype._controlHandleDisplaynameUpdate = function(internalID, displayname){
	if(this.data[ internalID ]){
		this.data[ internalID ]["status"] = this.STATUS_PENDING;
		this.data[ internalID ]["displayname"] = displayname;
		this._htmlUpdateRecipientBlock(internalID, displayname, this.STATUS_PENDING);
		this._controlInterpretEntry(internalID);
	}
}
RecipientInputWidget.prototype._controlGetSelection = function(){
	return this.selection;
}
RecipientInputWidget.prototype._controlGetAllRecipientInternalIDs = function(){
	return this.dataLookup;
}
RecipientInputWidget.prototype._controlSearchRecipient = function(searchData){
	var result = new Array();
	// Loop through all recipient entries
	for(var internalID in this.data){
		var entry = this.data[ internalID ];
		var matchingEntry = true;
		// Loop and match each search criteria
		for(var searchKey in searchData){
			if(searchKey == "displayname"){
				if(entry["displayname"] != searchData[ searchKey ]){
					matchingEntry = false;
					break;
				}
			}else if(searchKey == "status"){
				if(entry["status"] != searchData[ searchKey ]){
					matchingEntry = false;
					break;
				}
			// Other fields than displayname and status are search in data object.
			}else if(typeof entry["data"] == "object"){
				if(entry["data"][ searchKey ] != searchData[ searchKey ]){
					matchingEntry = false;
					break;
				}
			}else{
				matchingEntry = false;
				break;
			}
		}

		// When all criteria match add them to the result list
		if(matchingEntry)
			result.push(internalID);
	}
	return result;
}
RecipientInputWidget.prototype._controlInterpretInput = function(){
	var input = this._htmlGetUserInput();

	if(input.trim() != ""){
		var resolveData = new Array();

		var chunks = input.split(";");
		for(var i=0;i<chunks.length;i++){
			if(chunks[i].trim() != ""){
				var internalID = this._controlAddNewRecipient(chunks[i], this.STATUS_PENDING);

				resolveData.push({
					internalID: internalID,
					displayname: chunks[i].trim()
				});
			}
		}

		if(this.resolveMethod && chunks.length > 0){
			this.resolveMethod["method"].call(this.resolveMethod["scope"], this, resolveData);
		}

		this._htmlSetUserInput("");
	}
}
RecipientInputWidget.prototype._controlInterpretEntry = function(internalID){
	if(this.data[ internalID ]){
		if(this.resolveMethod){
			var resolveData = new Array();
			resolveData.push({
				internalID: internalID,
				displayname: this.data[ internalID ]["displayname"]
			});
			this.resolveMethod["method"].call(this.resolveMethod["scope"], this, resolveData);
		}
	}
}

RecipientInputWidget.prototype._controlGetRecipientCount = function(){
	return this.dataLookup.length;
}

/**
 * @return boolean will return TRUE if the event should continue, FALSE if the event should stop
 */
RecipientInputWidget.prototype._controlHandleKeyDown = function(keypress){
	var selection = this._controlGetSelection();
	switch(keypress.code){
		case this.KEY_LEFT:
			if(this._htmlGetUserInput() == ""){
				this._controlMoveSelectionToLeft(keypress);
				return false;	// Return false to prevent further keyhandling
			}
			break;
		case this.KEY_RIGHT:
			if(this._htmlGetUserInput() == ""){
				this._controlMoveSelectionToRight(keypress);
				return false;	// Return false to prevent further keyhandling
			}
			break;
		// These keys should be in this case to prevent them falling in the default case.
		case this.KEY_UP:
		case this.KEY_DOWN:
		case this.KEY_ENTER:
			break;
		case this.KEY_DELETE:
			if(this._htmlGetUserInput() == ""){
				if(selection){
					var lookupIndex = this._controlFindRecipientLookupIndex(selection);
					this._controlRemoveRecipient(selection);

					// By removing the recipient the next entry will get this lookupindex
					if(this.dataLookup[ lookupIndex ]){
						this._controlSelectRecipient( this.dataLookup[ lookupIndex ] );
						return false;	// Return false to prevent further keyhandling
					}else{
						this._htmlFocusOnInput();
					}
				}
			}
			break;
		case this.KEY_BACKSPACE:
			if(this._htmlGetUserInput() == ""){
				this._controlRemovePreviousRecipient(keypress);
			}
			break;
		case this.KEY_TAB:
			this._controlHandleBlur();

			/**
			 * When tabbing away to another button/input field the change in focus is not registered
			 * by the inputmanager. Therefore we need to manually change the focus to prevent any 
			 * other keypress events to be registered to this object. 
			 * 
			 * Example if this was not the case: User tabs away to the next input field; The focus 
			 * is not changed; The user starts typing; The keypress events register to the default 
			 * case of this swith statement; The default case moves focus back to the input field of
			 * this widget; The characters the user types are added to this input field.
			 */
			webclient.inputmanager.changeFocus(null);

			break;
		default:
			/**
			 * Only when a recipient block has been selected we should put the focus on the input 
			 * field. When no selection has been made, putting the focus on the input field will 
			 * quickly show a shift back and forth in the container DIV in IE. 
			 */
			if(this.selection){
				this._controlDeselectRecipient(this._controlGetSelection());
				this._htmlFocusOnInput();
			}
			break;
	}
}

RecipientInputWidget.prototype._controlMoveSelectionToLeft = function(keypress){
	var selection = this._controlGetSelection();
	if(!this._htmlIsInputCatetAtStartPosition())
		return;
	/**
	 * There are four possible cases for changing the selection after pressing LEFT key.
	 * 1. No recipients are added.
	 *    - Do nothing.
	 * 2. No current selection.
	 *    - Select last recipient entry.
	 * 3. First recipient entry selected.
	 *    - Do nothing since there are no recipient left of selected entry.
	 * 4. Any recipient entry (other than the first) selected.
	 *    - Find the previous recipient entry and select that one.
	 */
	// No recipients
	if(this._controlGetRecipientCount() == 0)
		return;

	if(selection){
		var lookupIndex = this._controlFindRecipientLookupIndex(selection);
		// Any other than first recipient is selected
		if(lookupIndex > 0){
			lookupIndex--;
			this._controlSelectRecipient(this.dataLookup[lookupIndex]);
		}else{
			/**
			 * We should be doing nothing when the selection is already at the 
			 * first item, but to make sure IE works properly we have to select 
			 * the recipient block again. This way we prevent the scrolling of 
			 * the container DIV.
			 */
			this._controlSelectRecipient(this.dataLookup[lookupIndex]);
		}
	// No current selection.
	}else{
		// Find last entry in lookup table and use that internalID.
		this._controlSelectRecipient(this.dataLookup[ this.dataLookup.length-1 ]);
	}
}
RecipientInputWidget.prototype._controlMoveSelectionToRight = function(keypress){
	var selection = this._controlGetSelection();
	/**
	 * There are four possible cases for changing the selection after pressing LEFT key.
	 * 1. No recipients are added.
	 *    - Do nothing.
	 * 2. No current selection.
	 *    - Do nothing.
	 * 3. Last recipient entry selected.
	 *    - Move focus to input field.
	 * 4. Any recipient entry (other than the last) selected.
	 *    - Find the next recipient entry and select that one.
	 */
	// No recipients
	if(this._controlGetRecipientCount() == 0)
		return;

	if(selection){
		var lookupIndex = this._controlFindRecipientLookupIndex(selection);
		// Selected entry is not the last entry
		if(lookupIndex != (this.dataLookup.length-1)){
			lookupIndex++;
			this._controlSelectRecipient(this.dataLookup[lookupIndex]);
		}else{
			this._controlDeselectRecipient(this.dataLookup[lookupIndex]);
			this._htmlFocusOnInput();
		}
	}	// Do nothing when on current selection.
}
RecipientInputWidget.prototype._controlRemovePreviousRecipient = function(keypress){
	var selection = this._controlGetSelection();
	/**
	 * There are four possible cases for changing the selection after pressing LEFT key.
	 * 1. No recipients are added.
	 *    - Do nothing.
	 * 2. No current selection.
	 *    - When the caret in input field is on the first position remove the last entry.
	 * 3. First recipient entry selected.
	 *    - Do nothing.
	 * 4. Any recipient entry (other than the first) selected.
	 *    - Find the previous recipient entry and select that one.
	 */
	// No recipients
	if(this._controlGetRecipientCount() == 0)
		return;

	if(selection){
		var lookupIndex = this._controlFindRecipientLookupIndex(selection);
		// Any other than first recipient is selected
		if(lookupIndex > 0){
			lookupIndex--;
			this._controlRemoveRecipient(this.dataLookup[lookupIndex]);
			this._htmlShowRecipientBlock(this._controlGetSelection());
		}else{
			// Do nothing when the selection is already at the first item.
			return;
		}
	// No current selection.
	}else{
		if(this._htmlIsInputCatetAtStartPosition()){
			this._controlRemoveRecipient(this.dataLookup[ this.dataLookup.length-1 ]);
			this._htmlFocusOnInput();
		}
	}
}


// Triggered by HTML events (non-api user input)
RecipientInputWidget.prototype._controlSelectRecipient = function(internalID){
console.log("select: "+internalID);
	if(this.selection){
		this._htmlDeselectRecipientBlock(this.selection);
	}
	this.selection = internalID;
	this._htmlSelectRecipientBlock(internalID);
	this._htmlShowRecipientBlock(internalID);
}
RecipientInputWidget.prototype._controlDeselectRecipient = function(internalID){
	this.selection = false;
	this._htmlDeselectRecipientBlock(internalID);
}
/**
 * _controlFindRecipientLookupIndex
 *
 * Part of the CONTROL component in the widget. Searches in the lookup table of the recipient data 
 * for the matching lookup index.
 * @param internalID number Internal ID used to identify a recipient
 * @return boolean Index of an entry in the lookup table. FALSE if no entry is found
 */
RecipientInputWidget.prototype._controlFindRecipientLookupIndex = function(internalID){
	for(var i=0;i<this.dataLookup.length;i++){
		if(this.dataLookup[i] == internalID)
			return i;
	}
	return false;
}
RecipientInputWidget.prototype._controlHandleFocus = function(){
console.log("RIW: _controlHandleFocus");
	var selection = this._controlGetSelection();
	if(selection)
		this._controlDeselectRecipient(selection);
}
RecipientInputWidget.prototype._controlHandleBlur = function(){
console.log("RIW: _controlHandleBlur");
	var selection = this._controlGetSelection();
	if(selection){
		this._controlDeselectRecipient(selection);
	}
	this._controlInterpretInput();
}

/**
 * The focus event of the input field is not propagated upwards. This prevents 
 * the inputmanager from detecting the shift in focus to another field that 
 * could be outside the focussed area. Since it is unaware of that shift we have
 * to notify the inputmanager of the change of focus.
 */
RecipientInputWidget.prototype._controlHandleInputFieldFocus = function(){
	if(!webclient.inputmanager.hasFocus(this)){
		webclient.inputmanager.changeFocus(this.htmlInputField);
	}
}

RecipientInputWidget.prototype._controlGenerateRecipientBlockContextMenu = function(internalID, menuItems){
	this.sendEvent("entry_contextmenu", this, internalID, menuItems);
}

RecipientInputWidget.prototype._controlGenerateMultipleResolveOptionsList = function(internalID){
	if(this.data[ internalID ]){
		var nameOptions = this.data[ internalID ]["options"];
		this._htmlBuildMultipleResolveOptionsMenu(internalID, nameOptions);
	}
}


/** HTML CONTROLLER **/
/**
 * The HTML tree looks like the following.
 * +---widget element-------------------------------------------------------------------------+
 * | +--UL-container list-------------------------------------------------------------------+ |
 * | | +-LI-recipient block----------+ +-LI-recipient block-----------+ +-LI-input block--+ | |
 * | | | +-RECIPIENT BLOCK WIDGET--+ | | +-RECIPIENT BLOCK WIDGET---+ | | +-input-------+ | | |
 * | | | |                         | | | |                          | | | |             | | | |
 * | | | +-------------------------+ | | +--------------------------+ | | +-------------+ | | |
 * | | +-----------------------------+ +------------------------------+ +-----------------+ | |
 * | +--------------------------------------------------------------------------------------+ |
 * +------------------------------------------------------------------------------------------+
 */
RecipientInputWidget.prototype._htmlSetupBaseInterface = function(){
	// Adding own element in parent
	this.element = dhtml.addElement(this.parentElement, "div", "recipientinputwidget", "recipientinputwidget"+this.widgetID, null, this.windowObj);
	this.element.style.width = "100%";
	this.element.style.height = "15px";
	this.element.style.overflow = "hidden";

	webclient.inputmanager.addObject(this, this.element);
	webclient.inputmanager.bindEvent(this, "keydown", this._eventWidgetKeyDown);
	webclient.inputmanager.bindEvent(this, "focus", this._eventWidgetFocus);
	webclient.inputmanager.bindEvent(this, "blur", this._eventWidgetBlur);

	dhtml.addEvent(this, this.element, "click", this._eventWidgetClick, this.windowObj);

	this.htmlRecipContainer = dhtml.addElement(this.element, "ul", "recipientcontainer", "recipientinputwidget"+this.widgetID+"_container", null, this.windowObj);

	this.htmlInputContainer = dhtml.addElement(this.htmlRecipContainer, "li", "recipientinput", null, null, this.windowObj);
	this.htmlInputField = dhtml.addElement(this.htmlInputContainer, "input", "recipientinput", null, null, this.windowObj);
	dhtml.addEvent(this, this.htmlInputField, "focus", this._eventInputFocus, this.windowObj);
	dhtml.addEvent(this, this.htmlInputField, "blur", this._eventInputBlur, this.windowObj);
}

RecipientInputWidget.prototype._htmlAddRecipientBlock = function(internalID, displayname, status){
	// Add an item to the list before the last item (inputfield)
	this.htmlRecipListItem[ internalID ] = dhtml.addElement(this.htmlRecipContainer, "li", null, null, null, this.windowObj);
	this.htmlRecipContainer.insertBefore( this.htmlRecipListItem[ internalID ], this.htmlInputContainer );

	var block = new RecipientInputBlockWidget(this, this.htmlRecipListItem[ internalID ], this.windowObj);
	block.init(internalID, displayname, status);
	this.htmlRecipBlocks[ internalID ] = block;

	block.addEventHandler("remove", this._eventBlockRemoveRecipientClick, this);
	block.addEventHandler("more_options", this._eventBlockShowMultipleOptionsClick, this);
	block.addEventHandler("contextmenu", this._eventBlockContextmenu, this);
	block.addEventHandler("displayname_changed", this._eventBlockDisplaynameChanged, this);
	block.addEventHandler("editmode_enabled", this._eventBlockEditmodeEnabled, this);
}

RecipientInputWidget.prototype._htmlUpdateRecipientBlock = function(internalID, displayname, status){
	if(this.htmlRecipBlocks[ internalID ]){
		this.htmlRecipBlocks[ internalID ].update(displayname, status);
	}
}
RecipientInputWidget.prototype._htmlRemoveRecipientBlock = function(internalID){
	if(this.htmlRecipBlocks[ internalID ]){
		this.htmlRecipBlocks[ internalID ].destructor();
		delete this.htmlRecipBlocks[internalID];
	}
}

RecipientInputWidget.prototype._htmlSelectRecipientBlock = function(internalID){
	if(this.htmlRecipBlocks[ internalID ]){
		this.htmlRecipBlocks[ internalID ].select();
	}
}
RecipientInputWidget.prototype._htmlDeselectRecipientBlock = function(internalID){
	if(this.htmlRecipBlocks[ internalID ]){
		this.htmlRecipBlocks[ internalID ].deselect();
	}
}
RecipientInputWidget.prototype._htmlShowRecipientBlock = function(internalID){
	if(this.htmlRecipBlocks[ internalID ]){
		var block = this.htmlRecipListItem[ internalID ];
		this._htmlScrollToListItem(block);
	}
}
RecipientInputWidget.prototype._htmlFocusOnInput = function(){
	console.log("HTML: _htmlFocusOnInput (widgetID: "+this.widgetID+")");

	this.element.scrollLeft = 0;
	this._htmlScrollToListItem(this.htmlInputContainer);
	this.htmlInputField.focus();

}
RecipientInputWidget.prototype._htmlBlurInput = function(){
	this.htmlInputField.blur();
}
RecipientInputWidget.prototype._htmlScrollToListItem = function(nodeLI){
	if(nodeLI){
		var block = {
			left: nodeLI.offsetLeft,
			right: nodeLI.offsetLeft + nodeLI.offsetWidth,
			width: nodeLI.offsetWidth
		}
		var viewport = {
			left: this.element.scrollLeft,
			right: this.element.scrollLeft + this.element.offsetWidth,
			width: this.element.offsetWidth
		}

		/**
		 * When a block is not larger than the viewport it can be in any of the 
		 * positions mentioned below.
		 *                       +-------------------+
		 *                       |     VIEWPORT      |
		 *                       +-------------------+
		 *          +-----+   +-----+   +-----+   +-----+   +-----+
		 * BLOCKS:  |  4  |   |  2  |   |  1  |   |  3  |   |  5  |
		 *          +-----+   +-----+   +-----+   +-----+   +-----+
		 * - 1: Fully inside the viewport.
		 * - 2: Partially inside the viewport on the left side.
		 * - 3: Partially inside the viewport on the right side.
		 * - 4: Fully outside the viewport on the left side.
		 * - 5: Fully outside the viewport on the right side.
		 */
var that = this;
that.nodeLI = nodeLI;
if(!window.a){
	window.a=window.setInterval(function(){var c=dhtml.getElementById("subject");
	var d="["+that.nodeLI.offsetLeft+"|"+that.element.scrollLeft+"]-"+c.value;
	c.value=d.substring(0,1000);},100);
}
console.log("- this.element.scrollLeft: " + this.element.scrollLeft);
console.log("- BLOCK> l:"+block.left + " r:" + block.right + " w:" + block.width);
console.log("- VIEWP> l:"+viewport.left + " r:" + viewport.right + " w:" + viewport.width);
		if(block.width < viewport.width){
			// Inside the viewport (fully or partially)
			if(block.left < viewport.right && block.right > viewport.left){
				// (2) Block is partially in viewport on the left side
				if(block.left < viewport.left && block.right > viewport.left){
console.log("scroll (2): inside on left");
//console.log("this.element.scrollLeft: " + this.element.scrollLeft);
//var el = this.element;
//var newleft = block.left;
//window.setTimeout(function(){console.log("this.element.scrollLeft: " + el.scrollLeft);}, 0);
//window.setTimeout(function(){console.log("this.element.scrollLeft: " + el.scrollLeft);}, 1000);
//window.setTimeout(function(){if(window.BROWSER_IE){el.scrollLeft = newleft;}}, 0);
//window.setTimeout(function(){console.log("this.element.scrollLeft: " + el.scrollLeft);}, 1000);
					// Move inside view with the block as first element in view
					this.element.scrollLeft = block.left;
//console.log("this.element.scrollLeft: " + this.element.scrollLeft);
				// (3) Block is partially in viewport on the right side
				}else if(block.left < viewport.right && block.right > viewport.right){
console.log("scroll (3): inside on right");
					// Move inside view with the block as last element in view
					this.element.scrollLeft = viewport.left + (block.right - viewport.right);
				// (1) Block is fully in view
				//}else{
				}
			// (4) Block is outside the viewport on the left side
			}else if(block.right <= viewport.left){
console.log("scroll (4): outside on left");
				// Move inside view with the block as first element in view
				this.element.scrollLeft = block.left;
console.log("this.element.scrollLeft: " + this.element.scrollLeft);
//var el = this.element;
//var newleft = block.left;
//window.setTimeout(function(){console.log("this.element.scrollLeft: " + el.scrollLeft);}, 0);
//window.setTimeout(function(){console.log("this.element.scrollLeft: " + el.scrollLeft);}, 1000);
//window.setTimeout(function(){if(window.BROWSER_IE){el.scrollLeft = newleft;}}, 0);
//window.setTimeout(function(){el.scrollLeft = newleft;}, 1000);
//window.setTimeout(function(){console.log("this.element.scrollLeft: " + el.scrollLeft);}, 1000);
			// (5) Block is outside the viewport on the right side
			}else if(block.left >= viewport.right){
console.log("scroll (5): outside on right");
				// Move inside view with the block as last element in view
				this.element.scrollLeft = viewport.left + (block.right - viewport.right);
			}
		// Block is bigger than the viewport
		}else{
			// Move inside view with the block as last element in view
			this.element.scrollLeft = viewport.left + (block.right - viewport.right);
		}

		if(this.element.scrollLeft == 0){
//			nodeLI.style.borderLeft = "10px solid #000";
console.log("X");
// Halt the browser and force IE to open the abort dialog
//for(var i=0;i<100000000;i++){
//var x=0;
//}
		}

/***
DESCRIPTION:
When the user presses left IE tries to put the cursor in the input field into the viewport and after that we quickly move the viewport to the focussed block.
***/

//var block = {
//	left: nodeLI.offsetLeft,
//	right: nodeLI.offsetLeft + nodeLI.offsetWidth,
//	width: nodeLI.offsetWidth
//}
//var viewport = {
//	left: this.element.scrollLeft,
//	right: this.element.scrollLeft + this.element.offsetWidth,
//	width: this.element.offsetWidth
//}
//setTimeout(function(){
//console.log("BLOCK> l:"+block.left + " r:" + block.right + " w:" + block.width);
//console.log("VIEWP> l:"+viewport.left + " r:" + viewport.right + " w:" + viewport.width);
//}, 1000);
//this.element.scrollLeft = 831;
	}
}
RecipientInputWidget.prototype._htmlGetUserInput = function(){
	return this.htmlInputField.value;
}
RecipientInputWidget.prototype._htmlSetUserInput = function(content){
	this.htmlInputField.value = content;
}
RecipientInputWidget.prototype._htmlIsInputCatetAtStartPosition = function(){
	var position = dhtml.getCaretPosAtTextField(this.htmlInputField);
	return (position == 0);
}

RecipientInputWidget.prototype._htmlBuildMultipleResolveOptionsMenu = function(internalID, nameOptions){
	var optionItems = new Array();
	for(var i in nameOptions){
		optionItems.push(webclient.menu.createMenuItem("recipientinput_options", nameOptions[i], false, function(widget, element, event){
			// Close the menu
			element.parentNode.style.display = "none";

			this._controlHandleDisplaynameUpdate(internalID, nameOptions[i]);
		}));
	}

	// Get coordinates
	var coordX = dhtml.getElementLeft(this.htmlRecipListItem[ internalID ]);
	var coordY = dhtml.getElementTop(this.htmlRecipListItem[ internalID ]) + this.htmlRecipListItem[ internalID ].offsetHeight;

	webclient.menu.buildContextMenu(this, "", optionItems, coordX,coordY);
}


/** HTML EVENTS **/
RecipientInputWidget.prototype._eventWidgetClick = function(widget, element, event){
	console.log("EVENTS: LEFT MOUSE CLICK ON WIDGET:"+widget.widgetID);

console.log("_eventWidgetClick::element != widget.htmlInputField",event.target,widget.htmlInputField);
	// If the widget does not have focus yet it will receive the IM focus event after a click. When the user clicks on the INPUT field that element will have focus. Only when the user clicks on another place in the widget when it already has the focus, we need to place the focus back on to the input field.
	// HMM: Seems to be needed for FF (first click on widget)
	if(webclient.inputmanager.hasFocus(widget) && event.target != widget.htmlInputField){
console.log("_eventWidgetClick::Focus on input");
		widget._htmlFocusOnInput();
	}
}


RecipientInputWidget.prototype._eventWidgetKeyDown = function(widget, element, event){
	var keypress = new Object();
	keypress["code"]  = event.keyCode;
	keypress["ctrl"]  = event.ctrlKey, 
	keypress["shift"] = event.shiftKey, 
	keypress["alt"]   = event.altKey
	return widget._controlHandleKeyDown(keypress);
}

RecipientInputWidget.prototype._eventWidgetFocus = function(widget, element, event){
	console.log("EVENTS: FOCUS ON WIDGET:"+widget.widgetID);
	widget._controlHandleFocus();
}
RecipientInputWidget.prototype._eventWidgetBlur = function(widget, element, event){
	console.log("EVENTS: BLUR ON WIDGET:"+widget.widgetID);
	widget._controlHandleBlur();
}
RecipientInputWidget.prototype._eventInputFocus = function(widget, element, event){
	console.log("EVENTS: FOCUS ON INPUT:"+widget.widgetID);
	widget._controlHandleInputFieldFocus();
}
RecipientInputWidget.prototype._eventInputBlur = function(widget, element, event){
	console.log("EVENTS: BLUR ON INPUT:"+widget.widgetID);
	widget._controlInterpretInput();
}
/** BLOCK EVENTS **/
RecipientInputWidget.prototype._eventBlockRemoveRecipientClick = function(widget, internalID){
	this._controlRemoveRecipient(internalID);
}
RecipientInputWidget.prototype._eventBlockShowMultipleOptionsClick = function(widget, internalID){
	console.log("Block widget wants to show options");
	this._controlGenerateMultipleResolveOptionsList(internalID);
}
RecipientInputWidget.prototype._eventBlockContextmenu = function(widget, internalID, menuItems){
	this._controlGenerateRecipientBlockContextMenu(internalID, menuItems);
}
RecipientInputWidget.prototype._eventBlockDisplaynameChanged = function(widget, internalID, displayname){
	this._controlHandleDisplaynameUpdate(internalID, displayname);
	this._controlHandleInputFieldFocus();
}
RecipientInputWidget.prototype._eventBlockEditmodeEnabled = function(widget, internalID){
	this._htmlScrollToListItem(this.htmlRecipListItem[ internalID ]);
}