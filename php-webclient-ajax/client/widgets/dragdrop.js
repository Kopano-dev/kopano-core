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

DragDrop.prototype = new Widget;
DragDrop.prototype.constructor = DragDrop;
DragDrop.superclass = Widget.prototype;

function DragDrop()
{
	this.targets = new Object();
	this.draggable = false;
	this.targetover = false;
	this.hoverTimeout;
}

/**
 * addTarget
 * When a draggable is dragged it can be dropped on one of the available targets.
 * These target are registered with this function.
 *  
 * param parentElement The parent element off the target, used for the scrollleft and the 
 *                     scrolltop properties
 * param element       The target element.
 * param group         The group which the target belongs to.      
 * param isFixed	   The target is a 'fixed' target
 * param isNotPositioned At the moment, the target is not yet positioned. updateTargets() must be called later
 */ 
DragDrop.prototype.addTarget = function(scrollElement, element, group, isFixed, isNotPositioned, isNotAllowed)
{
	if(!this.targets[group]) {
		this.targets[group] = new Object();
	}
	
	var target = new Object();
	target["element"] = element;
	target["notallowed"] = isNotAllowed;

	if(!isNotPositioned) {
		var topleft = dhtml.getElementTopLeft(element);
		target["x"] = topleft[0];
		target["y"] = topleft[1];
	}

	if(!this.targets[group]["targets"]) {
		this.targets[group]["targets"] = new Array();
		this.targets[group]["isfixed"] = isFixed;
	}
	
	if(!this.targets[group]["elementidmap"])
		this.targets[group]["elementidmap"] = new Array();
	
	this.targets[group]["scrollelement"] = scrollElement;
	dhtml.addEvent(-1, this.targets[group]["scrollelement"], "mouseout", eventDragDropMouseOutDraggable);

	// Add an element ID -> array ID mapping for reverse lookups later
	var id = this.targets[group]["targets"].push(target)-1;
	this.targets[group]["elementidmap"][element.id] = id;

}

/**
 * addDraggable
 * This function registeres a draggable. This element can be dragged around the window 
 * and can be dropped on one off the targets
 * 
 * param element The draggable.
 * param group   The group which the draggable belongs to.     
 */ 
DragDrop.prototype.addDraggable = function(element, group, isFixed, resizable, moduleId)
{
	// Check to see if a moduleObject can be associated with this draggable.
	if(webclient.getModule(moduleId)){
		var moduleObj = webclient.getModule(moduleId);
	}else{
		var moduleObj = -1;
	}
	dhtml.addEvent(moduleObj, element, "mousedown", eventDragDropMouseDownDraggable);
	element.setAttribute("group", group);

	if(isFixed) {
		element.setAttribute("isFixed", "true");
		dhtml.addEvent(moduleObj, element, "mousemove", eventDragDropMouseMoveFixedDraggable);
		element.setAttribute("resizable", resizable?"true":"false");
	}

	if(moduleId){
		element.setAttribute("moduleID", moduleId);
	}
}

/**
 * This function de-registeres a draggable element, and cannot be dragged around the window.
 * @param {HTMLElement} element the draggable element.
 */
DragDrop.prototype.removeDraggable = function(element){
	// Remove draggable attributes.
	element.removeAttribute("group");
	element.removeAttribute("isFixed");
	element.removeAttribute("resizable");
	element.removeAttribute("moduleID");
}

DragDrop.prototype.registerHoverElement = function(element, callback, scope, data){
	if(element){
		element.hoverEvent = new Object();
		element.hoverEvent.callback = callback;
		element.hoverEvent.scope = scope;
		element.hoverEvent.data = data;
		dhtml.addEvent(this, element, "mousemove", eventDragDropHoverMouseMove);
		dhtml.addEvent(this, element, "mouseout", eventDragDropHoverMouseOut);
	}
}
DragDrop.prototype.unregisterHoverElement = function(element){
	if(element){
		delete element.hoverEvent;
		dhtml.removeEvent(element, "mousemove", eventDragDropHoverMouseMove);
	}
}


DragDrop.prototype.deleteGroup = function(group)
{
	delete this.targets[group];
}

/**
 * setOnDropGroup
 * This function registeres the callback function for a specific group. The callback
 * function will be triggered if a draggable is dropped on a target.
 * 
 * param group            The group which the callback function is for.
 * param moduleID         Id of a module.
 * param callbackfunction Callbackfunction which will be triggered.      
 */ 
DragDrop.prototype.setOnDropGroup = function(group, moduleID, callbackfunction)
{
	if(!this.targets[group]) {
		this.targets[group] = new Object();
	}
	
	this.targets[group]["moduleID"] = moduleID;
	this.targets[group]["callbackfunction"] = callbackfunction;
}

DragDrop.prototype.updateTarget = function(group, elementid, x, y)
{
	if(this.targets[group] && this.targets[group]["elementidmap"][elementid]) {
		var targetnr = this.targets[group]["elementidmap"][elementid];
		this.targets[group]["targets"][targetnr]["x"] = x;
		this.targets[group]["targets"][targetnr]["y"] = y;
	}
}

/**
 * updateTargets
 * This function updates the "x" and "y" properties of the targets. It calculates the 
 * offsetLeft and offsetTop from the target elements.
 * 
 * param group The group which should be updated.   
 */ 
DragDrop.prototype.updateTargets = function(group)
{
	if(this.targets[group] && this.targets[group]["targets"]) {
		for(var i=0;i<this.targets[group]["targets"].length;i++)
		{
			if (this.targets[group]["targets"][i] && this.targets[group]["targets"][i]["element"] && this.targets[group]["targets"][i]["element"].parentNode!=null){
				var topleft = dhtml.getElementTopLeft(this.targets[group]["targets"][i]["element"]);
				
				this.targets[group]["targets"][i]["x"] = topleft[0];
				this.targets[group]["targets"][i]["y"] = topleft[1];
			}else{
				delete this.targets[group]["targets"][i];
			}
		}
	}
}

/**
 * moveFixedDraggable
 * This function is used to move a fixed draggable around. It checks if a the element
 * is over a fixed target, then it the element will jump to that target.
 * 
 * There are three possibilities in this function, namely:
 * - resize-top:    resize an element at the top.
 * - resize-bottom:	resize an element at the bottom.
 * - move:          move an element.       
 */ 
DragDrop.prototype.moveElement = function(event)
{
	var isFixed = (this.draggable.getAttribute("isFixed") == undefined || this.draggable.getAttribute("isFixed")=="false"?false:true);
	// Check which action should be applied.
	var action = (this.draggable.getAttribute("action") == undefined || this.draggable.getAttribute("action")=="false"?false:this.draggable.getAttribute("action"));

	var mouseDraggableTopDistance = 0;
	if(action) {
		// This is the distance between the mouse cursor and the top of the element.
		// This distance is not used when action is 'resize-bottom', because other wise
		// the element wil jump to an half an hour and then to the mouse cursor. 
		mouseDraggableTopDistance = (action != "resize-bottom"?parseInt(this.draggable.getAttribute("mouseDraggableTopDistance")):0);
	}

	for(var group in this.targets)
	{
		if(group == this.draggable.getAttribute("group")) {
			var scrollElement = this.targets[group]["scrollelement"];

			for(var i in this.targets[group]["targets"]) {
				var target = this.targets[group]["targets"][i];
				
				if(target && (!this.targetover || this.targetover != target)) {
					if(target["x"] > 0 && target["y"] > 0) {
						if(event.clientX - dragdrop.relClickX  + dragdrop.dragPointX >= (target["x"] - scrollElement.scrollLeft) && 
							event.clientX - dragdrop.relClickX + dragdrop.dragPointX <= (target["x"] + target["element"].offsetWidth - scrollElement.scrollLeft) &&
							event.clientY - dragdrop.relClickY + dragdrop.dragPointY >= (target["y"] - scrollElement.scrollTop) &&
							event.clientY - dragdrop.relClickY + dragdrop.dragPointY <= (target["y"] + target["element"].offsetHeight - scrollElement.scrollTop)){
							
							/**
							 * Get scrollelement hight and width and its left and top
							 * to check whether mousemove event is fired on scrollelement or outside.
							 */
							var topleft = dhtml.getElementTopLeft(scrollElement);
							var scrollWidth = scrollElement.clientWidth + topleft[0];
							var scrollHeight = scrollElement.clientHeight + topleft[1];

							var scrollTop = scrollHeight - scrollElement.clientHeight;
							var scrollLeft = scrollWidth - scrollElement.clientWidth;

							// If event is fired in the scrollelement then set the target otherwise clear the selection.
							if(scrollHeight > event.clientY && scrollTop < event.clientY && scrollWidth > event.clientX && scrollLeft < event.clientX) {
								if(isFixed) {
									this.moveFixedDraggable(target, action, scrollElement);
								} else {
									this.moveDraggable(target);
								}
								this.targetover = target;
							} else {
								// Clear dragdrop target selection.
								eventDragDropMouseOutDraggable();
							}
						}
					}
				}
			}
		}
	}
}

DragDrop.prototype.moveDraggable = function(target)
{
	if(this.targetover) {
		dhtml.removeClassName(this.targetover["element"], "dragover");
	}
	if(target["notallowed"]){
		dhtml.addClassName(target["element"], "dragover_not_allowed");
	}else{
		dhtml.addClassName(target["element"], "dragover");
	}
}

DragDrop.prototype.moveFixedDraggable = function(target, action, scrollElement)
{
	// Execute the correct action
	switch(action)
	{
		case "resize-top":
			// This calculation is used to check if the element is already an half an hour, so
			// the element won't jump downwards.
			var bottomOffsetTop = this.draggable.offsetTop + this.draggable.clientHeight;
			if((bottomOffsetTop - target["element"].offsetTop + 4) > target["element"].clientHeight) {
				// Set the correct height and top position.
				this.draggable.style.height = bottomOffsetTop - target["element"].offsetTop - 4 + "px";
				this.draggable.style.top = target["element"].offsetTop + "px";
			}
			break;
		case "resize-bottom":
			// This calculation is used to check if the height is not 0 (zero). So it stays at a 
			// minimum of an half an hour.
			var height = (target["element"].offsetTop + target["element"].clientHeight - this.draggable.offsetTop - 7); 
			if(height > 0) {
				// Set the height.
				this.draggable.style.height = height + "px";
			}
			break;
		case "move":
			// @todo - create a way to add hanlder and execute it add this point 
			//         delegate the code below to that hanlder
			
			// this "if" function is a workarround
			if(dhtml.hasClassName(target["element"],"month_day") || dhtml.hasClassName(target["element"],"week_view")){
				// on moving a month view item
				target["element"].appendChild(this.draggable);
			} else {
				// on moving a day/week/workweek view item
				
				// Check if the element has not reached the bottom (23:30 hours) of the page. 
				if((this.draggable.clientHeight + target["element"].offsetTop) < scrollElement.scrollHeight) {
					// Check if the parent is different from the draggable.
					// Move the element to this parent.
					if(this.draggable.parentNode != target["element"].parentNode) {
						var element = this.draggable;
						this.addDraggable(element, this.draggable.getAttribute("group"), this.draggable.getAttribute("isFixed"), this.draggable.getAttribute("resizable")); 
						target["element"].parentNode.appendChild(element);
					}
					
					// Change the top position.
					this.draggable.style.top = target["element"].offsetTop + "px";
					
					// This attribute is used for saving
					this.draggable.setAttribute("starttime", target["element"].id);
				}
			}
			break;
	}
}

function eventDragDropMouseDownDraggable(moduleObject, element, event)
{
	// Call event notification method of the moduleObject to let the module know the drag event has been started.
	if(moduleObject.dragEventDown){
		moduleObject.dragEventDown();
	}

	var group = element.getAttribute("group");

	// If no groups is there then item is not draggable, so skip dragging operation.
	if(!group)
		return false;

	var isFixed = (element.getAttribute("isFixed") == undefined || element.getAttribute("isFixed")=="false"?false:true);

	// Remember where the client clicked when the drag started so we can see how far the object has been dragged
	dragdrop.clickX = event.clientX;
	dragdrop.clickY = event.clientY;
	
	// Remember where the user clicked within the element
	var topleft = dhtml.getElementTopLeft(element);
	// Hackish: have to get the scroll value here
	dragdrop.relClickX = event.clientX + element.parentNode.parentNode.scrollLeft - topleft[0];
	dragdrop.relClickY = event.clientY + element.parentNode.parentNode.scrollTop - topleft[1];
	
	// There is also the 'dragPoint', which is the point within the element which is being dragged. In a 'fixed' drag,
	// this point is used to find a target element. When the dragpoint is over a target, then the target is selected.
	
	if(isFixed) {
		var resizable = (element.getAttribute("resizable") == undefined || element.getAttribute("resizable")=="false"?false:true);
		var clientY = event.clientY + element.parentNode.parentNode.scrollTop - dhtml.getElementTop(element.parentNode.parentNode);
	
		if(resizable && (clientY - element.offsetTop) < 4)	{
			element.setAttribute("action", "resize-top");
			dragdrop.dragPointX = element.offsetWidth/2;
			dragdrop.dragPointY = 0;
		} else if (resizable && ((element.offsetTop + element.offsetHeight) - clientY) <= 8) { 
			element.setAttribute("action", "resize-bottom");
			dragdrop.dragPointX = element.offsetWidth/2;
			dragdrop.dragPointY = element.offsetHeight;
		} else { 
			element.setAttribute("action", "move");
			dragdrop.dragPointX = element.offsetWidth/2;
			dragdrop.dragPointY = 10; // This is a hack; Moving items in the calendar actually moves the 'start'. We position halfway down the first 30-minute bit
		} 
	
		var bottomOffsetTop = element.offsetTop + element.clientHeight;
		element.setAttribute("mouseDraggableTopDistance", (element.clientHeight - (bottomOffsetTop - clientY)));
		dragdrop.draggable = element;
	} else {
		if(dragdrop.draggable) {
			dhtml.deleteElement(dragdrop.draggable);
			dragdrop.attached = false;
		}

		dragdrop.draggable = document.createElement("div");
		dragdrop.draggable.className = "draggable";
	
		var clone = element.cloneNode(true);
		clone.id = "";
		clone.style.position = "relative";
		clone.style.top = "0px";
		clone.style.left = "0px";
		
		dragdrop.draggable.elementid = element.id;
		dragdrop.draggable.setAttribute("group", group);
		
		if(clone.className.indexOf("selected") > 0) {
			clone.className = clone.className.substring(0, clone.className.indexOf("selected"));
		}

		if(typeof moduleObject.getSelectedMessages == "function" && moduleObject.getSelectedMessages().length > 1){
			dragdrop.draggable.innerHTML = '<div class="dragdrop_draggable_multiple"></div>';
		}else{
			dragdrop.draggable.innerHTML = '<div class="dragdrop_draggable_single"></div>';
		}

		dragdrop.draggable.style.left = event.clientX+"px";
		// Because this is an icon, position the icon just next to the cursor. We're dragging the center.
		dragdrop.relClickX = 8;
		dragdrop.relClickY = 8;
		dragdrop.dragPointX = 8;
		dragdrop.dragPointY = 8;
	}

	dragdrop.draggable.moduleID = moduleObject.id;

	return false;
}

function eventDragDropMouseOutDraggable(moduleObject, element, event)
{
	dhtml.removeClassName(dragdrop.targetover["element"], "dragover");
	dragdrop.targetover = false;
}

function eventDragDropMouseMoveDraggable(moduleObject, element, event)
{
	if(!event) {
		event = window.event;
	}

	if(typeof(dragdrop)!="undefined" && dragdrop.draggable) {
		var isFixed = (dragdrop.draggable.getAttribute("isFixed") == undefined || dragdrop.draggable.getAttribute("isFixed")=="false"?false:true);
		
		// Only activate dragdrop if more than 10 pixels has been dragged
		if(Math.abs(dragdrop.clickX - event.clientX) + Math.abs(dragdrop.clickY - event.clientY) > 10) {
    		if(!isFixed) {
				dragdrop.draggable.style.display = "block";
				dragdrop.draggable.style.top = ((event.clientY-dragdrop.relClickY)+20) + "px";
				dragdrop.draggable.style.left = ((event.clientX - dragdrop.relClickX)+20) + "px";

				if(!dragdrop.attached) {
					document.body.appendChild(dragdrop.draggable);
					dragdrop.attached = true;
				}
			}		
    		dragdrop.moveElement(event);
		} 
		
	}

	return false;
}

function eventDragDropMouseUpDraggable(moduleObject, element, event)
{
	var dropped = false;

	// Call event notification method of the moduleObject to let the module know the drag event has been ended.
	if(moduleObject.dragEventUp){
		moduleObject.dragEventUp();
	}
	var draggableElem = dragdrop.draggable;
	dragdrop.draggable = false;

	if(dragdrop.targetover) {
		if(dragdrop.targets[draggableElem.getAttribute("group")]) {
			var targetGroup = dragdrop.targets[draggableElem.getAttribute("group")];
			var target = dragdrop.targetover;

			dhtml.removeClassName(target["element"], "dragover");

			if(!target["notallowed"]){
				var draggableCallback = false;
				if(draggableElem.moduleID){
					var draggableModule = webclient.getModule(draggableElem.moduleID);

					if(typeof draggableModule.getDragDropTargetCallback == "function"){
						draggableCallback = draggableModule.getDragDropTargetCallback();
					}
				}
				if(typeof draggableCallback == "function"){
					draggableCallback(webclient.getModule(draggableElem.moduleID), target["element"], draggableElem, event);
				}else if(targetGroup["moduleID"] && targetGroup["callbackfunction"]) {
					targetGroup.callbackfunction(webclient.getModule(targetGroup["moduleID"]), target["element"], draggableElem, event);
				}

				// We dropped something
				dropped = true;
			}
		}
		
		dragdrop.targetover = false;
	}
	
	if(draggableElem) {
		var isFixed = (draggableElem.getAttribute("isFixed") == undefined || draggableElem.getAttribute("isFixed")=="false"?false:true);
		if(!isFixed) {
			dhtml.deleteElement(draggableElem);
		} else if(dropped){
			dragdrop.removeDraggable(draggableElem);
			dhtml.addElement(draggableElem, 'span', 'message_loader');
		}
		
		draggableElem = false;
		dragdrop.attached = false;
	}

	// Do not process other mouseup events if we did an actual drop
	if(dropped)
		return false;	
	return true;
}

function eventDragDropMouseMoveFixedDraggable(moduleObject, element, event)
{
	var resizable = (element.getAttribute("resizable") == undefined || element.getAttribute("resizable")=="false"?false:true);
	var clientY = event.clientY + element.parentNode.parentNode.scrollTop - dhtml.getElementTop(element.parentNode.parentNode);

	if(resizable && (clientY - element.offsetTop) < 4)	{
		// in resize area
		element.style.cursor = "n-resize";
	} else if (resizable && ((element.offsetTop + element.offsetHeight) - clientY) <= 4) { 
		// in resize area
		element.style.cursor = "n-resize";
	} else { 
		// in move area
		element.style.cursor = "move";
	} 
	
	return false;
}

function eventDragDropHoverMouseMove(widget, element, event){
	// When the user moves the mouse the timeout is reset meaning that 
	// the hover event will not yet be fired.
	if(typeof widget.hoverTimeout != "undefined"){
		window.clearTimeout(widget.hoverTimeout);
	}

	if(!dragdrop.draggable){
		return;
	}

	widget.hoverTimeout = window.setTimeout(function(){
		if(element && element.hoverEvent){
			if(typeof element.hoverEvent.callback == "function"){
				element.hoverEvent.callback.call(element.hoverEvent.scope, element, element.hoverEvent.data);
			}
		}
	},1000);
}
function eventDragDropHoverMouseOut(widget, element, event){
	// When the user moves the mouse the timeout is reset meaning that 
	// the hover event will not yet be fired.
	if(typeof widget.hoverTimeout != "undefined"){
		window.clearTimeout(widget.hoverTimeout);
	}
}
