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
 * XMLBuilder
 * Responsible for creating a correct XML document, using DOM. 
 */ 
function XMLBuilder()
{
	// XML Document Object
	this.xmlDoc = null;
	
	// XML DocumentElement
	this.xml = null;
	
	// Create Document
	this.createDocument();
}

/**
 * Function which creates a XML document.
 */ 
XMLBuilder.prototype.createDocument = function()
{
	// Firefox
	if(document.implementation.createDocument) {
	    // HACK ALERT
	    // FF3 will nog allow us to create a document with createDocument with UTF-8 encoding, and defaults to iso-8859-1. So
	    // this is the only way to create a 'new' UTF-8 document ...
	    this.xmlDoc = new DOMParser().parseFromString("<?xml version='1.0' encoding='UTF-8'?><zarafa/>","text/xml"); 	
		this.xml = this.xmlDoc.documentElement;
	} else { // IE
		this.xmlDoc = new ActiveXObject("Microsoft.XMLDOM");
		var root = this.xmlDoc.createDocumentFragment();
		
		this.xml = this.xmlDoc.createElement("zarafa");
		root.appendChild(this.xml);
		this.xmlDoc.appendChild(root);
	}
}

/**
 * Function which adds a <module> tag to the XML document
 * @param string name module name
 * @param string id module id
 * @return object module tag element   
 */ 
XMLBuilder.prototype.addModule = function(name, id)
{
	var module = this.xmlDoc.createElement("module");
	module.setAttribute("name", name);
	module.setAttribute("id", id);
	this.xml.appendChild(module);
	
	return module;
}

/**
 * Function which adds a <action> tag to the XML document
 * @param object module tag element
 * @param string type the action type
 * @return object action tag element   
 */ 
XMLBuilder.prototype.addAction = function(module, type)
{
	var action = this.xmlDoc.createElement("action");
	action.setAttribute("type", type);
	module.appendChild(action);
	
	return action;
}

/**
 * Function which adds data to the XML document
 * @param object parent the parent tag element
 * @param object data data which will be added to the XML document
 * @param object parentProperty used for attributes   
 */ 
XMLBuilder.prototype.addData = function(parent, data, parentProperty)
{
	// If 'data' has a property 'attributes', set these attributes. Uses
	// 'parentProperty' for tagName.
	if(parentProperty) {
		if(data["attributes"]) {
			if(typeof data["_content"] != "undefined") {
				this.addNode(parent, parentProperty, data["_content"], data["attributes"]);
			} else {
				parent = this.addNode(parent, parentProperty, false, data["attributes"]);
			}
		}
	}

	for(var property in data)
	{
		if(property != "attributes" && property != "_content") {
			if(data[property] != null && typeof(data[property]) == "object") {
				if(data[property].length) {
					for(var i in data[property])
					{
						if(typeof(data[property][i]) == "object") {
							if(data[property][i]["attributes"]) {
								this.addData(parent, data[property][i], property);
							} else {
								var node = this.addNode(parent, property);
								this.addData(node, data[property][i]);
							}
						} else {
							this.addNode(parent, property, data[property][i]);
						}
					}
				} else {
					var node = this.addNode(parent, property);
					this.addData(node, data[property]);
				}
			} else {
				this.addNode(parent, property, data[property]);
			}
		}
	}
}

/**
 * Function which adds a node to the XML document
 * @param object parent the parent tag element
 * @param string childName the tag name
 * @param string value the tag value
 * @param object attributes the tag attributes  
 * @return object the node element   
 */ 
XMLBuilder.prototype.addNode = function(parent, childName, value, attributes)
{
	var child = this.xmlDoc.createElement(childName);

	if(typeof value != "undefined" && value !== false && value !== null) {
		/**
		 * Prevent the point in a float to be transformed into a comma when the 
		 * language/location settings of the OS says that they have to use a 
		 * comma in decimal numbers. Somehow in MSIE they think they need to 
		 * change it in the createTextNode method of the ActiveXObject Microsoft.XMLDOM.
		 */
		/**
		 * Boolean values need different treatment in IE, convert boolean values to 
		 * string and then pass in xml, otherwise IE converts true to -1 and false to blank string
		 */
		if(typeof value == "number" || typeof value == "boolean"){
			value = String(value);
		}

		var textNode = this.xmlDoc.createTextNode(value);
		child.appendChild(textNode);
	}
	
	if(attributes && typeof(attributes) == "object") {
		for(var attribute in attributes)
		{
			child.setAttribute(attribute, attributes[attribute]);
		}
	}

	parent.appendChild(child);
	return child;
}

/**
 * Function adds a reset tag to the XML document. This is used to
 * reset the server (delete all modules on the server). 
 */ 
XMLBuilder.prototype.addReset = function()
{
	this.xml.appendChild(this.xmlDoc.createElement("reset"));
}

/**
 * Function which adds the delete module tags to the XML document.
 * These modules will be deleted on the server. 
 */ 
XMLBuilder.prototype.deleteModules = function(modules)
{
	var deletemodules = this.xmlDoc.createElement("deletemodule");
	this.xml.appendChild(deletemodules);
	
	for(var i in modules) 
	{
		this.addNode(deletemodules, "module", modules[i]);
	}
}

/**
 * Function which returns the XML Document.
 * @return object XML Document 
 */ 
XMLBuilder.prototype.getXML = function()
{
	return this.xmlDoc;
}

/**
 * Function which resets this object
 */ 
XMLBuilder.prototype.reset = function()
{
	this.xml = null;
	this.xmlDoc = null;

	this.createDocument();
}

/**
 * Add keep alive packet
 */
XMLBuilder.prototype.addKeepAlive = function()
{
	this.xml.appendChild(this.xmlDoc.createElement("keepalive"));
}

/**
 * Add reload request tag
 */
XMLBuilder.prototype.addReloadRequest = function()
{
	this.xml.appendChild(this.xmlDoc.createElement("request_webaccess_reload"));
}
