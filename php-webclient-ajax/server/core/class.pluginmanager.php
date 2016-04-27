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
* Managing component for all plugins
*
* This class handles all the plugin interaction with the webaccess on the server side.
*
* @package core
*/
class PluginManager
{
	// List of all plugins and their data
	var $plugindata;

	/**
	 * List of all hooks registered by plugins. 
	 * [eventID][] = plugin
	 */
	var $hooks;

	/**
	 *  List of all plugin objects
	 * [pluginname] = pluginObj
	 */
	var $plugins;

	/**
	 * List of sessiondata from plugins. 
	 * [pluginname] = sessiondata
	 */
	var $sessionData;

	/**
	 * Constructor
	 */
	function PluginManager()
	{
		$this->plugindata = Array();
		$this->hooks = Array();
		$this->plugins = Array();
		$this->sessionData = false;
	} 

	/**
	 * pluginsEnabled
	 * 
	 * Checks whether the plugins have been enabled by checking if the proper 
	 * configuration keys are set.
	 * @return boolean Returns true when plugins enabled, false when not.
	 */
	function pluginsEnabled(){
		return (!defined('ENABLE_PLUGINS') || !defined('PATH_PLUGIN_DIR') || !ENABLE_PLUGINS )===false;
	}
	
	/**
	 * detectPlugins
	 * 
	 * Detecting the installed plugins either by using the already ready data 
	 * from the state object or otherwise read in all the data and write it into
	 * the state.
	 */
	function detectPlugins(){
		if(!$this->pluginsEnabled())
			return false;


		// Get the plugindata from the state.
		$pluginState = new State('plugin');
		$pluginState->open();
		$this->plugindata = $pluginState->read("plugindata");
		$disabledPlugins = Array();
		if(defined('DISABLED_PLUGINS_LIST')) $disabledPlugins = explode(';', DISABLED_PLUGINS_LIST);

		// If no plugindata has been stored yet, get it from the plugins dir.
		if(!$this->plugindata || DEBUG_PLUGINS){
			$this->plugindata = Array();
			if(is_dir(PATH_PLUGIN_DIR)) {
				$pluginsdir = opendir(PATH_PLUGIN_DIR);
				if($pluginsdir) {
					while(($plugin = readdir($pluginsdir)) !== false){
						if ($plugin != '.' && $plugin != '..' && !in_array($plugin, $disabledPlugins)){
							if(is_dir(PATH_PLUGIN_DIR.'/'.$plugin)){
								if(is_file(PATH_PLUGIN_DIR.'/'.$plugin.'/manifest.xml')){
									$this->processPlugin($plugin);
								}
							}
						}
					}
				}
			}
			// Write the plugindata to the state
			$pluginState->write("plugindata", $this->plugindata);
		}
		// Free the state again.
		$pluginState->close();
	}

	/**
	 * initPlugins
	 * 
	 * This function includes the server plugin classes, instantiate and 
	 * initialize them.
	 * 
	 */
	function initPlugins(){
		if(!$this->pluginsEnabled())
			return false;

		// Inlcude the root files of all the plugins and instantiate the plugin
		foreach($this->plugindata as $plugName => $plugData){
			$this->includePluginServerFiles($plugName, 'plugin');

			if(class_exists('Plugin' . $plugName)){
				$pluginClassName = 'Plugin' . $plugName;
				$this->plugins[$plugName] = new $pluginClassName;
				$this->plugins[$plugName]->setPluginName($plugName);
				$this->plugins[$plugName]->init();
			}
		}
	}

	/**
	 * processPlugin
	 * 
	 * Read in the manifest and get the files that need to be included 
	 * for placing hooks, defining modules, etc. 
	 * 
	 * @param $dirname string name of the directory of the plugin
	 */
	function processPlugin($dirname){
		// Read XML manifest file of plugin
		$handle = fopen(PATH_PLUGIN_DIR.'/'.$dirname.'/manifest.xml', 'rb');
		$xml = '';
		if($handle){
			while (!feof($handle)) { 
				$xml .= fread($handle, 8192);
			}
			fclose($handle);
		}

		$plugindata = $this->extractPluginDataFromXML($xml);
		if($plugindata){
			$this->plugindata[$dirname] = Array(
				'pluginname' => $dirname,
				'serverfiles' => (isset($plugindata['serverfiles'])?$plugindata['serverfiles']:Array()), 
				'clientfiles' => (isset($plugindata['clientfiles'])?$plugindata['clientfiles']:Array()), 
				'translationsdir' => (isset($plugindata['translationsdir'])?$plugindata['translationsdir']:null),
				'dialogs' => (isset($plugindata['dialogs'])?$plugindata['dialogs']:Array()),
			);
		}else{
			if(DEBUG_PLUGINS) dump('[PLUGIN ERROR] Plugin "'.$dirname.'" has an invalid manifest.');
		}
	}

	/**
	 * loadSessionData
	 * 
	 * Loads sessiondata of the plugins from disk. 
	 * To improve performance the data is only loaded if a 
	 * plugin requests (reads or saves) the data.
	 * 
	 * @param $pluginname string Identifier of the plugin
	 */
	function loadSessionData($pluginname) {
		// lazy reading of sessionData
		if (!$this->sessionData) {
			$sessState = new State('plugin_sessiondata');
			$sessState->open();
			$this->sessionData = $sessState->read("sessionData");
			if (!isset($this->sessionData) || $this->sessionData == "")
				$this->sessionData = array();
			$sessState->close();
		}
		if ($this->pluginExists($pluginname)) {
			if (!isset($this->sessionData[$pluginname])) {
				$this->sessionData[$pluginname] = array();
			}
			$this->plugins[ $pluginname ]->setSessionData($this->sessionData[$pluginname]);
		}
	}

	/**
	 * saveSessionData
	 * 
	 * Saves sessiondata of the plugins to the disk. 
	 * 
	 * @param $pluginname string Identifier of the plugin
	 */	
	function saveSessionData($pluginname) {
		if ($this->pluginExists($pluginname)) {
			$this->sessionData[$pluginname] = $this->plugins[ $pluginname ]->getSessionData();
		}
		if ($this->sessionData) {
			$sessState = new State('plugin_sessiondata');
			$sessState->open();
			$sessState->write("sessionData", $this->sessionData);
			$sessState->close();
		}
	}
	
	/**
	 * includePluginServerFiles
	 * 
	 * Include the server plugin files. 
	 * 
	 * @param $pluginname string Identifier of the plugin
	 * @param $type string The type of file that is included.
	 */
	function includePluginServerFiles($pluginname, $type = 'plugin'){
		foreach($this->plugindata[ $pluginname ]['serverfiles'] as $i => $file){
			if($file['type'] == $type){
				include(PATH_PLUGIN_DIR.'/'.$pluginname.'/'.$file['file']);
			}
		}
	}

	/**
	 * getAvailablePluginModules
	 * 
	 * Construct a list of modules added by the plugins that are available to 
	 * the dispatcher.
	 * 
	 * @return array List of available modules added by the plugins.
	 */
	function getAvailablePluginModules(){
		$files = Array();
		foreach($this->plugindata as $pluginname => $singlePluginData){
			foreach($singlePluginData['serverfiles'] as $i => $file){
				if($file['type'] == 'module' && $file['module']){
					$files[ $file['module'] ] = Array(
						'file' => PATH_PLUGIN_DIR.'/'.$pluginname.'/'.$file['file']
						//'dependencies'
					);
				}
			}
		}
		return $files;
	}
	
	/**
	 * pluginExists
	 * 
	 * Checks if plugin exists.
	 * 
	 * @param $pluginname string Identifier of the plugin
	 * @return boolen True when plugin exists, false when it does not.
	 */
	function pluginExists($pluginname){
		if(isset($this->plugindata[ $pluginname ])){
			return true;
		}else{
			return false;
		}
	}

	/**
	 * getDialogFilePath
	 * 
	 * Get the path to the dialog file in the plugin dir.
	 * 
	 * @param $pluginname string Identifier of the plugin
	 * @param $dialogname string Name of the dialog
	 * @return string|boolean Path to the file in the plugin directory or FALSE indicating that there is not dialog.
	 */
	function getDialogFilePath($pluginname, $dialogname){
		if(isset($this->plugindata[ $pluginname ]) && isset($this->plugindata[ $pluginname ]['dialogs'][ $dialogname ])){
			return PATH_PLUGIN_DIR.'/'.$pluginname.'/'.$this->plugindata[ $pluginname ]['dialogs'][ $dialogname ]['file'];
		}else{
			return false;
		}
	}

	/**
	 * registerHook
	 * 
	 * This function allows the plugin to register their hooks.
	 * 
	 * @param $eventID string Identifier of the event where this hook must be triggered.
	 * @param $pluginName string Name of the plugin that is registering this hook.
	 */
	function registerHook($eventID, $pluginName){
		$this->hooks[ $eventID ][ $pluginName ] = $pluginName;
	}

	/**
	 * triggerHook
	 * 
	 * This function will call all the registered hooks when their event is triggered.
	 * 
	 * @param $eventID string Identifier of the event that has just been triggered.
	 * @param $data mixed (Optional) Usually an array of data that the callback function can modify.
	 * @return mixed Data that has been changed by plugins.
	 */
	function triggerHook($eventID, $data = Array()){
		if(isset($this->hooks[ $eventID ]) && is_array($this->hooks[ $eventID ])){
			foreach($this->hooks[ $eventID ] as $key => $pluginname){
				$this->plugins[ $pluginname ]->execute($eventID, $data);
			}
		}
		return $data;
	}

	/**
	 * getClientFiles
	 * 
	 * Returning an array of paths to files that need to be included. The path 
	 * prepended so it starts at the root of the webaccess.
	 * 
	 * @param $type string Type of files that are requested.
	 * @param $load array Identifiers that determine where the files need to be 
	 *                    loaded. By this property it is possible to only load 
	 *                    certain files in the main window or only in a dialog.
	 * @return array List of paths to files.
	 */
	function getClientFiles($type, $load = Array('all')){
		$files = Array();
		foreach($this->plugindata as $pluginname => $plugin){
			for($i=0;$i<count($plugin['clientfiles']);$i++){
				if($plugin['clientfiles'][$i]['type'] == $type && in_array($plugin['clientfiles'][$i]['load'], $load)){
					$files[] = PATH_PLUGIN_DIR.'/'.$pluginname.'/'.$plugin['clientfiles'][$i]['file'];
				}
			}
		}
		return $files;
	}

	/**
	 * getClientPluginManagerData
	 * 
	 * Retuning the data needed by the clientside Plugin Manager.
	 * 
	 * @return array List of data needed by Plugin Manager.
	 */
	function getClientPluginManagerData(){
		$clientplugindata = Array();
		foreach($this->plugindata as $pluginname => $plugin){
			$clientplugindata[ $pluginname ] = Array(
				'pluginname' => $pluginname
			);
		}
		return $clientplugindata;
	}

	/**
	 * getTranslationFilePaths
	 * 
	 * Returning an array of paths to to the translations files. This will be 
	 * used by the gettext functionality.
	 * 
	 * @return array List of paths to translations.
	 */
	function getTranslationFilePaths(){
		$paths = Array();
		foreach($this->plugindata as $pluginname => $plugin){
			if($plugin['translationsdir']){
				$translationPath =  PATH_PLUGIN_DIR.'/'.$pluginname.'/'.$plugin['translationsdir'];
				if(is_dir($translationPath)){
					$paths[$pluginname] = $translationPath;
				}
			}
		}
		return $paths;
	}

	/**
	 * extractPluginDataFromXML
	 * 
	 * Extracts all the data from the Plugin XML manifest.
	 * 
	 * @param $xml string XML manifest of plugin
	 * @return array Data from XML converted into array that the PluginManager can use.
	 */
	function extractPluginDataFromXML($xml){
		$plugindata = Array(
			'serverfiles' => Array(), 
			'clientfiles' => Array(), 
			'translationsdir' => null,
			'dialogs' => Array(),
		);

		// Parse the XML data
		$this->xmlParser = new XMLParser(Array('language', 'server', 'client', 'dialogs', 'serverfile', 'clientfile', 'dialog'));
		$data = $this->xmlParser->getData($xml);
		if(isset($data['info']) && isset($data['resources'])){
			// Import server files
			if(isset($data['resources']) && isset($data['resources']['server']) && isset($data['resources']['server'][0]) && isset($data['resources']['server'][0]['serverfile'])){
				$plugindata['serverfiles'] = $this->getServerFileInfoFromXML($data['resources']['server'][0]['serverfile']);
			}
			// Import client files
			if(isset($data['resources']) && isset($data['resources']['client']) && isset($data['resources']['client'][0]) && isset($data['resources']['client'][0]['clientfile'])){
				$plugindata['clientfiles'] = $this->getClientFileInfoFromXML($data['resources']['client'][0]['clientfile']);
			}
			// Import dialog data
			if(isset($data['resources']) && isset($data['resources']['dialogs']) && isset($data['resources']['dialogs'][0]) && isset($data['resources']['dialogs'][0]['dialog'])){
				$plugindata['dialogs'] = $this->getDialogInfoFromXML($data['resources']['dialogs'][0]['dialog']);
			}
			// Import translations dir
			if(isset($data['resources']) && isset($data['resources']['language'])){
				$plugindata['translationsdir'] = $this->getTranslationsDirInfoFromXML($data['resources']['language']);
			}
			return $plugindata;
		}else{
			if(DEBUG_PLUGINS) dump('[PLUGIN ERROR] No plugin info and/or resources were found.');
			return false;
		}
	}

	/**
	 * getServerFileInfoFromXML
	 * 
	 * Transform the server files info from a manifest XML to an usable array.
	 * 
	 * @param $fileData array Piece of manifest XML parsed into array.
	 * @return array List of server files.
	 */
	function getServerFileInfoFromXML($fileData){
		$files = Array();

		for($i=0;$i<count($fileData);$i++){
			$filename = false;
			$type = 'plugin';	// plugin | module
			$module = false;
			if(is_string($fileData[$i])){
				$filename = $fileData[$i];
			}elseif(isset($fileData[$i]['_content'])){
				$filename = $fileData[$i]['_content'];
				if(isset($fileData[$i]['attributes']) && isset($fileData[$i]['attributes']['type'])){
					$type = $fileData[$i]['attributes']['type'];
				}
				if(isset($fileData[$i]['attributes']) && isset($fileData[$i]['attributes']['module'])){
					$module = $fileData[$i]['attributes']['module'];
				}
			}else{
				if(DEBUG_PLUGINS) dump('[PLUGIN ERROR] Server file XML is invalid.');
			}

			if($filename){
				$files[] = Array('file' => $filename, 'type' => $type, 'module' => (($module)?$module:null) );
			}
		}

		return $files;
	}

	/**
	 * getClientFileInfoFromXML
	 * 
	 * Transform the client files info from a manifest XML to an usable array.
	 * 
	 * @param $fileData array Piece of manifest XML parsed into array.
	 * @return array List of client files.
	 */
	function getClientFileInfoFromXML($fileData){
		$files = Array();

		for($i=0;$i<count($fileData);$i++){
			$filename = false;
			$type = 'js';	// js | module | css
			$load = 'all';	// all | main | dialog
			if(is_string($fileData[$i])){
				$filename = $fileData[$i];
			}elseif(isset($fileData[$i]['_content'])){
				$filename = $fileData[$i]['_content'];
				if(isset($fileData[$i]['attributes']) && isset($fileData[$i]['attributes']['type'])){
					$type = $fileData[$i]['attributes']['type'];
				}
				if(isset($fileData[$i]['attributes']) && isset($fileData[$i]['attributes']['load'])){
					$load = $fileData[$i]['attributes']['load'];
				}
			}else{
				if(DEBUG_PLUGINS) dump('[PLUGIN ERROR] Client file XML is invalid.');
			}

			$files[] = Array('file' => $filename, 'type' => $type, 'load' => $load);
		}

		return $files;
	}

	/**
	 * getDialogInfoFromXML
	 * 
	 * Transform the dialog info from a manifest XML to an usable array.
	 * 
	 * @param $dialogData array Piece of manifest XML parsed into array.
	 * @return array List of dialog info.
	 */
	function getDialogInfoFromXML($dialogData){
		$dialogs = Array();

		for($i=0;$i<count($dialogData);$i++){
			if(isset($dialogData[$i]['name']) && isset($dialogData[$i]['file'])){
				if(is_string($dialogData[$i]['name']) && is_string($dialogData[$i]['file'])){
					$dialogs[ $dialogData[$i]['name'] ] = Array(
						'name' => $dialogData[$i]['name'],
						'file' => $dialogData[$i]['file']
					);
				}else{
					if(DEBUG_PLUGINS) dump('[PLUGIN ERROR] Dialog XML is invalid (contains invalid name and file info).');
				}
			}else{
				if(DEBUG_PLUGINS) dump('[PLUGIN ERROR] Dialog XML is invalid (does not contain name and file info).');
			}
		}

		return $dialogs;
	}

	/**
	 * getTranslationsDirInfoFromXML
	 * 
	 * Transform the language dir info from a manifest XML to an usable string.
	 * 
	 * @param $translationsDirData array Piece of manifest XML parsed into array.
	 * @return string Path to translations dir.
	 */
	function getTranslationsDirInfoFromXML($translationsData){
		$translationsDir = false;

		if(isset($translationsData[0])){
			if(is_array($translationsData[0]) && isset($translationsData[0]['_content'])){
				$translationsDir = $translationsData[0]['_content'];
			}elseif(is_string($translationsData[0]) && $translationsData[0] != ""){
				$translationsDir = $translationsData[0];
			}
		}
		return $translationsDir;
	}
}
?>
