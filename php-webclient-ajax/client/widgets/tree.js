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
 * Tree View
 */
 
Tree.prototype = new Widget;
Tree.prototype.constructor = Tree;
Tree.superclass = Widget.prototype;

function Tree(moduleID, contentElement, events, multipleSelection)
{
	this.moduleID = moduleID;
	this.contentElement = contentElement;

	this.root = false;
	this.nodes = new Array();

	this.events = events;

	this.FOLDER_STATE_OPEN = 1;
	this.FOLDER_STATE_CLOSED = 2;

	// allow multiple selection of folder nodes
	if(typeof multipleSelection != "undefined" && multipleSelection) {
		this.multipleSelection = multipleSelection;
	} else {
		this.multipleSelection = false;
	}
}

Tree.prototype.createNode = function(parentID, nodeID, isRoot, nodeValue, nodeIcon, nodeHasChildNodes, nodeOpen, nodeEvents, nodeExtra, nodeAttributes, nodeDropNotAllowed)
{
	var node = new Object();
	node["parentid"] = parentID;
	node["id"] = nodeID;
	node["value"] = nodeValue;
	node["icon"] = nodeIcon;
	node["hasChildNodes"] = nodeHasChildNodes;
	node["open"] = nodeOpen;
	node["events"] = nodeEvents;
	node["extra"] = nodeExtra;
	node["attributes"] = nodeAttributes;
	node["dropNotAllowed"] = nodeDropNotAllowed;

	if(isRoot) {
		this.root = node;
	} else {
		this.nodes.push(node);
	}
	
	return node;
}

Tree.prototype.buildTree = function(callbackfunction)
{
	var childNodes = this.node(this.contentElement, this.root);
	
	if(childNodes) {
		this.addNodes(childNodes, this.root);
	}
}

Tree.prototype.addNodes = function(element, parent, callbackfunction)
{
	for(var i = 0; i < this.nodes.length; i++)
	{
		if(this.nodes[i]["parentid"] == parent["id"]) {
			var childNodes = this.node(element, this.nodes[i]);

			if(childNodes) {
				this.addNodes(childNodes, this.nodes[i]);
			}
		}
	}
}

Tree.prototype.node = function(element, node)
{
	var nodeElement = dhtml.addElement(element, "div", "folder", node["id"]);
	
	if(node["attributes"]) {
		for(attr in node["attributes"]) {
			nodeElement[attr] = node["attributes"][attr];
		}
	}
	
	var nodeOpenClose = dhtml.addElement(nodeElement, "div", "folderstate " + (node["hasChildNodes"] == "1"?"folderstate_" + (node["open"]?"open":"close"):""), false, false);
	nodeOpenClose.innerHTML = "&nbsp;";
	nodeElement.folderstateNode = nodeOpenClose;	// For easy finding

	if(node["hasChildNodes"] == "1") {
		dhtml.addEvent(this.moduleID, nodeOpenClose, "click", this.events["ShowBranch"]);
		dhtml.addEvent(this.moduleID, nodeOpenClose, "click", this.events["SwapFolder"]);
		
		if(typeof dragdrop != "undefined"){
			dragdrop.registerHoverElement(nodeElement, this.hoverOnNode, this);
		}
	}
	
	if(this.multipleSelection) {
		var nodeSelector = dhtml.addElement(null, "input", "folder_select", "folder_select");
		nodeSelector.setAttribute("type", "checkbox");
		nodeElement.appendChild(nodeSelector);
		dhtml.addEvent(this.moduleID, nodeSelector, "click", this.events["SelectMultipleFolder"]);
	}

	if(this.multipleSelection) {
		var nodeTitle = dhtml.addElement(nodeElement, "div", "folder_icon_multiple folder_icon_" + node["icon"]);
	} else {
		var nodeTitle = dhtml.addElement(nodeElement, "div", "folder_icon folder_icon_" + node["icon"]);
	}
	dhtml.setEvents(this.moduleID, nodeTitle, node["events"]);

	var nodeName = dhtml.addElement(nodeTitle, "span", "folder_title", false, escapeHtml(node["value"]));
	nodeName.innerHTML = "&nbsp;" + escapeHtml(node["value"]) + "&nbsp;";

	if (node["extra"]){
		dhtml.addElement(nodeName, "span", node["extra"]["class"], false, node["extra"]["text"]);
		dhtml.addClassName(nodeName, "folder_highlight");
	}
	
	if(typeof dragdrop != "undefined") dragdrop.addTarget(this.contentElement, nodeName, "folder", false, false, node["dropNotAllowed"]);
	
	if(node["hasChildNodes"] == "1") {
		var subfolders = dhtml.addElement(element, "div", "branch", "branch" + node["id"]);
		if(node["open"]) {
			subfolders.style.display = "block";
		}
		
		return subfolders;
	} else {
		return false;
	}
}

/**
 * Adds node in the tree view 
 * @param Object the node which is to be added in the tree
 * @param boolean states wheather the parentNode should expand the folder while adding the node
 */
Tree.prototype.addNode = function(node, expand)
{
	var parentNode = this.getNode(node["parentid"]);
	parentNode["open"] = expand? true : false;

	if(parentNode) {
		var parentSubNodes = dhtml.getElementById("branch" + parentNode["id"],"div", this.contentElement);

		if(parentSubNodes) {
			if(parentNode["open"]) {
				parentSubNodes.style.display = "block";
			}
			
			var subnodes = this.node(parentSubNodes, node);
			
			if(subnodes) {
				this.addNodes(subnodes, node);
			}
		} else {
			var parentElement = dhtml.getElementById(parentNode["id"], "div", this.contentElement);

			if(parentElement) {
				var nodeSubNodes = dhtml.addElement(false, "div", "branch", "branch" + parentNode["id"]);
				if(parentNode["open"]) {
					nodeSubNodes.style.display = "block";
				}
				
				var subnodes = this.node(nodeSubNodes, node);
				if(subnodes) {
					this.addNodes(subnodes, node);
				}

				if(parentElement.nextSibling) {
					parentElement.parentNode.insertBefore(nodeSubNodes, parentElement.nextSibling);
				} else {
					parentElement.parentNode.appendChild(nodeSubNodes);
				}
			}
		}
	}else{
		if (node["parentid"]){
			// when we got here we do have a parent, but that doesn't work because the parent is the root folder
			// TODO: rebuild this tree widget (see ticket #1001)
			return false;
		}
	}
	//update folderstate of the particular node in hierarchy
	var element = dhtml.getElementById(parentNode["id"], "div", this.contentElement);
	var state = parentNode["open"]?this.FOLDER_STATE_OPEN : this.FOLDER_STATE_CLOSED
	this.sendEvent('statechange', element, state);
	
	return true;
}

Tree.prototype.changeNode = function(node)
{
	var nodeElement = dhtml.getElementById(node["id"], "div", this.contentElement);
	
	if(nodeElement) {
		var nodeOpenClose = nodeElement.getElementsByTagName("div")[0];
		if(nodeOpenClose) {
			nodeOpenClose.className = "folderstate " + ((node["hasChildNodes"] == "1")?"folderstate_" + ((node["open"])?"open":"close"):"");
			
			if(node["hasChildNodes"] == "1" && !nodeOpenClose.events) {
				dhtml.addEvent(this.moduleID, nodeOpenClose, "click", this.events["ShowBranch"]);
				dhtml.addEvent(this.moduleID, nodeOpenClose, "click", this.events["SwapFolder"]);
			} else if(node["hasChildNodes"] == "0" && nodeOpenClose.events) {
				for(var eventType in nodeOpenClose.events)
				{
					for(var guid in nodeOpenClose.events[eventType])
					{
						dhtml.removeEvent(nodeOpenClose, eventType, nodeOpenClose.events[eventType][guid]["handler"]);
					}
				}
			}
			
			if(node["open"] && node["hasChildNodes"] == "1") {
				var subnodes = dhtml.getElementById("branch" + node["id"], "div", this.contentElement);
				if(subnodes) {
					subnodes.style.display = "block";
				}
			}
		}
		
		var nodeIcon = nodeElement.getElementsByTagName("div")[1];
		var nodeName = nodeIcon.getElementsByTagName("span")[0];
		if(nodeName) {
			nodeName.innerHTML = "&nbsp;" + escapeHtml(node["value"]) + "&nbsp;";
		}

		// Somehow a normal if(node["extra"]) fails :S
		if (Boolean(node["extra"])){
			dhtml.addElement(nodeName, "span", node["extra"]["class"], false, node["extra"]["text"]);
			dhtml.addClassName(nodeName, "folder_highlight");
		}else{
			dhtml.removeClassName(nodeName, "folder_highlight");
		}
	//update folderstate of the particular node in hierarchy
	var state = node["open"]?this.FOLDER_STATE_OPEN : this.FOLDER_STATE_CLOSED
	this.sendEvent('statechange', nodeElement, state);
	}
}

Tree.prototype.deleteNode = function(nodeID, deleteFromNodeList)
{
	var nodeElement = dhtml.getElementById(nodeID, "div", this.contentElement);
	dhtml.deleteAllChildren(nodeElement);
	dhtml.deleteElement(nodeElement);

	var branchElement = dhtml.getElementById("branch"+nodeID, "div", this.contentElement);
	dhtml.deleteAllChildren(branchElement);
	dhtml.deleteElement(branchElement);

	if(deleteFromNodeList) {
		var nodeIndex = false;
		for(var i = 0; i < this.nodes.length; i++)
		{
			if(this.nodes[i]["id"] == nodeID) {
				nodeIndex = i;
			}
		}
		
		if(nodeIndex) {
			this.nodes.splice(nodeIndex, 1);
		}
	}
}

Tree.prototype.getChildren = function(node, includeSubNodes)
{

	var result = new Array();
	for(var i = 0; i < this.nodes.length; i++)
	{
		if (this.nodes[i]["parentid"] == node.id){
			result.push(this.nodes[i]);

			if (this.nodes[i].hasChildNodes && includeSubNodes){
				var cResult = this.getChildren(this.nodes[i], true);
				for(var k=0; k<cResult.length; k++){
					result.push(cResult[k]);
				}
			}
		}
	}
	return result;
}

/**
 * Moves a node from one folder to another in tree view 
 * @param Object the node which is to be added in the tree
 * @param boolean states wheather the parentNode should expand the folder while adding the node
 */
Tree.prototype.moveNode = function(node, expand)
{
	this.deleteNode(node["id"]);
	this.addNode(node, expand);
}

Tree.prototype.getNode = function(nodeID)
{
	for(var i = 0; i < this.nodes.length; i++)
	{
		if(this.nodes[i]["id"] == nodeID) {
			return this.nodes[i];
		}
	}
	
	return false;
}

Tree.prototype.destructor = function()
{
	dhtml.deleteAllChildren(this.contentElement);
	this.nodes = new Array();
	Tree.superclass.destructor(this);
}

Tree.prototype.toggleBranch = function(id)
{
	var objBranch = dhtml.getElementById("branch" + id).style;
	var display = "block";
	var state = this.FOLDER_STATE_OPEN;

	if(objBranch.display == "block") {
		display = "none";
		state = this.FOLDER_STATE_CLOSED;
	}
	
	objBranch.display = display;

	if(typeof dragdrop != "undefined") {
		dragdrop.updateTargets("folder");
	}

	return state;
}

Tree.prototype.toggleFolderState = function(element){
	if(element.className.indexOf("close") > -1) {
		dhtml.removeClassName(element, "folderstate_close");
		dhtml.addClassName(element, "folderstate_open");
	} else {
		dhtml.removeClassName(element, "folderstate_open");
		dhtml.addClassName(element, "folderstate_close");
	}
}

Tree.prototype.hoverOnNode = function(element){
	var state = this.FOLDER_STATE_OPEN;

	// expand folder only when it is closed.
	if(element.folderstateNode.className.indexOf("close") > 0){
		state = this.toggleBranch(element.id);
		this.toggleFolderState(element.folderstateNode);
	}

	this.sendEvent('statechange', element, state);
}

function eventTreeSwapFolder(moduleObject, element, event)
{
	Tree.prototype.toggleFolderState(element);
}

function eventTreeShowBranch(moduleObject, element, event)
{
	Tree.prototype.toggleBranch(element.parentNode.id);
}
