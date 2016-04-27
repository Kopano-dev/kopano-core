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
* Dialog Module
* 
* This module is available in every dialog module and contains
* some general functions (requests) that can be used when there is 
* no other module available to do this.
*/

dialogmodule.prototype = new Module;
dialogmodule.prototype.constructor = dialogmodule;
dialogmodule.superclass = Module.prototype;

function dialogmodule(id)
{
	if(arguments.length > 0) {
		this.init(id);
	}
}

dialogmodule.prototype.init = function(id)
{
	this.callbackCounter = 0;
	this.callbackFunctions = new Object();

	dialogmodule.superclass.init.call(this, id);
}

dialogmodule.prototype.execute = function(type, action)
{
	var data = dom2array(action)
	if (data["callbackid"]){
		var callback = this.getCallback(data["callbackid"]);
		callback.call(callback, data);
	}
}

/**
* addCallback - this functions stores the callback function for future use
*               it returns the callbackid which should be send in the request
*/
dialogmodule.prototype.addCallback = function(callback)
{
	this.callbackFunctions[++this.callbackCounter] = callback;
	return this.callbackCounter;
}

/**
* getCallback - returns the callback function for the given callbackid, 
				please note that the reference to the callback function
				is removed after calling this function
*/
dialogmodule.prototype.getCallback = function(callbackid)
{
	var callback = this.callbackFunctions[callbackid];
	delete this.callbackFunctions[callbackid];
	return callback;
}

/**
* loadABHierachy - requests a list of addressbook folders
*
*
*/
dialogmodule.prototype.loadABHierarchy = function(hierarchy, callback)
{
	var data = new Object();
	data["callbackid"] = this.addCallback(callback);
	data["gab"] = "all";
	data["contacts"] = {stores:{store:new Array(),folder:new Array()}}
	for(var i=0;i<hierarchy.stores.length;i++){
		var store = hierarchy.stores[i];
		switch(store.foldertype){
			case "contact":
				data["contacts"]["stores"]["folder"].push(store.id);
				break;
			case "all":
				data["contacts"]["stores"]["store"].push(store.id);
				break;
		}
	}

	webclient.xmlrequest.addData(this, "abhierarchy",data);
	webclient.xmlrequest.sendRequest();
}
