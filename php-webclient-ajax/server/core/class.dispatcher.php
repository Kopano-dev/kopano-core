<?php
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

?>
<?php
	/**
	* On-demand module loader
	*
	* The dispatcher is simply a class instance factory, returning an instance of a class. If
	* the source was not loaded yet, then the specified file is loaded.
	*
	* @package core
	*/
	class Dispatcher
	{
		function Dispatcher()
		{
			
		} 
		
		/**
		 * Load a module with a specific name
		 *
		 * If required, loads the source for the module, then instantiates a module of that type
		 * with the specified id and initial data. The $id and $data parameters are directly
		 * forwarded to the module constructor. 
		 *
		 * Source is loaded from server/modules/class.$modulename.php
		 *
		 * @param string $moduleName The name of the module which should be loaded (eg 'hierarchymodule')
		 * @param integer $id Unique id number which represents this module
		 * @param array $data Array of data which is received from the client
		 * @return object Module object on success, false on failed		 		 		 		 
		 */		 		
		function loadModule($moduleName, $id, $data)
		{
			$module = false;
			
			if(array_search($moduleName, $GLOBALS["availableModules"])!==false) {
				require_once("server/modules/class." . $moduleName . ".php");
				$module = new $moduleName($id, $data);
			}elseif(isset($GLOBALS["availablePluginModules"][ $moduleName ])) {
				require_once($GLOBALS["availablePluginModules"][ $moduleName ]['file']);
				$module = new $moduleName($id, $data);
			}else{
				trigger_error("Unknown module '".$moduleName."', id: '".$id."'", E_USER_WARNING);
			}
			return $module;
		}
	}
?>
