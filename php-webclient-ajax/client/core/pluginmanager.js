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
 * PluginManager
 * 
 * This object handles all the plugin interaction with the webaccess on the client side.
 */
PluginManager.prototype.constructor = PluginManager;

function PluginManager()
{
	// List of all plugins and their data
	this.plugindata = new Array();

	/**
	 * List of all hooks registered by plugins. 
	 * [eventID][] = plugin
	 */
	this.hooks = new Array();

	/**
	 *  List of all plugin objects
	 * [pluginname] = pluginObj
	 */
	this.plugins = new Array();
}


/**
 * setPluginData
 * 
 * Set the data of all the plugins. This data should come from the server side PluginManager.
 * 
 * @param pluginData object List of all data of the installed plugins.
 */
PluginManager.prototype.setPluginData = function(pluginData){
	this.plugindata = pluginData;
}

/**
 * registerHook
 * 
 * This function allows the plugin to register their hooks.
 * 
 * @param eventID string Identifier of the event where this hook must be triggered.
 * @param pluginName string Name of the plugin that is registering this hook.
 */
PluginManager.prototype.init = function(){
	for(var i in this.plugindata){
		var pluginName = this.plugindata[ i ]["pluginname"]
		var pluginClass = "Plugin" + pluginName;
		// Test to see if the class is defined.
		if(pluginClass && typeof window[pluginClass] == "function"){
			var plugin;
			// Lets get down and eval...
			eval("plugin = new " + pluginClass + "();");
			// If the plugin is instantiated go ahead and initialize it.
			if(plugin){
				this.plugins[ pluginName ] = plugin;
				plugin.setPluginName(pluginName);
				plugin.init();
			}
		}
	}
}

/**
 * registerHook
 * 
 * This function allows the plugin to register their hooks.
 * 
 * @param eventID string Identifier of the event where this hook must be triggered.
 * @param pluginName string Name of the plugin that is registering this hook.
 */
PluginManager.prototype.registerHook = function(eventID, pluginName){
	if(!this.hooks[ eventID ])
		this.hooks[ eventID ] = new Array();

	this.hooks[ eventID ][ pluginName ] = pluginName;
}

/**
 * triggerHook
 * 
 * This function will call all the registered hooks when their event is triggered.
 * 
 * @param eventID string Identifier of the event that has just been triggered.
 * @param data mixed Usually an array of data that the callback function can modify.
 * @return mixed Data that has been changed by plugins.
 */
PluginManager.prototype.triggerHook = function(eventID, data){
	if(this.hooks[ eventID ] && isArray(this.hooks[ eventID ])){
		for(var i in this.hooks[ eventID ]){
			var pluginname = this.hooks[ eventID ][i];
			if(this.plugins[ pluginname ]){
				this.plugins[ pluginname ].execute(eventID, data);
			}
		}
	}
	return data;
}

/**
 * pluginExists
 * 
 * Checks if plugin exists.
 * 
 * @param pluginname string Identifier of the plugin
 * @return boolen True when plugin exists, false when it does not.
 */
PluginManager.prototype.pluginExists = function(pluginname){
	for(var i in this.plugindata){
		if(this.plugindata[i]["pluginname"] == pluginname){
			return true;
		}
	}
	return false;
}