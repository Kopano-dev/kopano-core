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
 * Dispatcher
 * This object creates and returns a module object. It contains a list 
 * of all available modules.
 * @todo
 * - implement dynamic loading of .js files from the server     
 */ 
function Dispatcher(availableModules)
{
	this.availableModules = availableModules;
}

/**
 * Function which loads a new module object.
 * @param string moduleName the name of the module
 * @return object module object
 */ 
Dispatcher.prototype.loadModule = function(moduleName)
{
	var module = false;
	
	if(moduleName == "notelist" || moduleName == "imaplist") {
		moduleName = "maillist";
	}
	
	for(var i = 0; i < this.availableModules.length; i++)
	{
		if(this.availableModules[i].indexOf(moduleName) == 0)
		{
			module = this.availableModules[i];
		}
	}

	if(module) {
		eval("module = new " + module.substring(0, module.indexOf(".")) + "();");
	}
	
	return module;
}
