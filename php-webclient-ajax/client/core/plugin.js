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
 * Plugin
 * The super class obejct for every plugin.
 */ 

Plugin.prototype.constructor = Plugin;

function Plugin(){
	// Identifying name of the plugin.
	var pluginname = false;
}

/**
 * setPluginName
 * 
 * Sets the identifying name of the plugin in a member variable.
 *
 *@param name string Identifying name of the plugin
 */
Plugin.prototype.setPluginName = function(name){
	this.pluginname = name;
}

/**
 * getPluginName
 * 
 * Gets the identifying name of the plugin.
 *
 *@return string Identifying name of the plugin
 */
Plugin.prototype.getPluginName = function(){
	return this.pluginname;
}

/**
 * registerHook
 * 
 * This functions calls the PluginManager to register a hook for this plugin.
 * 
 * @param eventID string Identifier of the event where this hook must be triggered.
 */
Plugin.prototype.registerHook = function(eventID){
	if(this.getPluginName()){
		webclient.pluginManager.registerHook(eventID, this.getPluginName());
	}
}

// Placeholder functions
Plugin.prototype.execute = function(){}
Plugin.prototype.init = function(){}

