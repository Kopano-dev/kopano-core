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
* Widget base class
* this class has no function, but it provides all widgets an unique id
* which can be used for creating HTML elements and it also provides
* a destructor
*/

Widget.prototype.constructor = Widget;

function Widget()
{
	this.init(this);
}

Widget.prototype.init = function(widgetObject)
{
	widgetObject.widgetID = widgetCounter++;
}

Widget.prototype.destructor = function(widgetObject)
{
}

/**
 * Add an event handler for internal event type 'eventname'. If 'object' is not null, the method
 * will be called in the context of that object. The parameters passed to the method are down
 * to the event source
 *
 * @param eventname name of the event to handle (eg 'openitem')
 * @param method method to call when event is triggered
 * @param object object if the method is to be called in an object's context
 */
Widget.prototype.addEventHandler = function(eventname, method, object)
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
Widget.prototype.sendEvent = function()
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
		var object =  this.internalEvents[eventname][i].object || window;
		this.internalEvents[eventname][i].method.apply(object, args);
	}
}

// global widget counter
var widgetCounter = 0;

