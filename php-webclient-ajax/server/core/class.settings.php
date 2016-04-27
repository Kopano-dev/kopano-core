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
	* Generic settings class
	*
    * This class allows access to various user settings which are normally
    * configured by the user in the settings dialog. Settings can be set and retrieved
    * via this class. Default values must be provided at retrieval time.	
	*
	* Settings have a path-like structure, for example:
	* <code>
	* path/to/setting => 5
	* </code>
	*
	* @package core
	*/
	
	class Settings
	{
		var $store;
		var $settings;
		var $init = false;
			
		/**
		* Constructor
		*/
		function Settings()
		{
		} 
	
        /**
        * Initialise the settings class
        *
        * Opens the default store and gets the settings. This is done only once. Therefore
        * changes written to the settings after the first Init() call will be invisible to this
        * instance of the Settings class
        * @access private
        */	  	
		function Init()
		{
			if($this->init) {
				return;
			} else {
				$this->store = $GLOBALS["mapisession"]->getDefaultMessageStore();
				$this->retrieveSettings();
				$this->init = true;
			}		
		}
	
		/**
		* Get a setting from the settings repository
		*
		* Retrieves the setting at the path specified. If the setting is not found, and no $default value
		* is passed, returns null.
		*
		* @param string $path Path to the setting you want to get, separated with slashes.
		* @param string $default If the setting is not found, and this parameter is passed, then this value is returned
		* @return string Setting data, or $default if not found or null of no default is found
		*/
		function get($path=null, $default=null)
		{
			$this->Init();
			
			if ($path==null) return $this->settings;
	
			$path = explode("/", $path);
			$tmp = $this->settings;
			foreach($path as $pointer){
				if (!empty($pointer)){
					if (!isset($tmp[$pointer])){
						return $default;
					}
					$tmp = $tmp[$pointer];
				}
			}
			return $tmp;
		}
	
		/**
		* Store a setting
		*
		* Overwrites a setting at a specific settings path with the value passed.
		*
		* @param string $path Path to the setting you want to set, separated with slashes.
		* @param mixed $value New value for the setting
		*/
		function set($path, $value)
		{
			$this->Init();
			$path = explode("/", $path);
			$pointer = end($path);
			$tmp = $value;
			while($pointer){
				if (!empty($pointer)){
					$tmp = array($pointer=>$tmp);
					$pointer = prev($path);
				}
			}
			$this->settings = array_merge_recursive_overwrite($this->settings, $tmp);
			$this->saveSettings();
		}

        /**
        * Delete a setting
        *
        * Deletes the setting references by $path
        *
        * @param string $path Path to the setting you want to delete
        */	
		function delete($path)
		{

			$this->Init();
			$path = explode("/", $path);
			$tmp =& $this->settings;
			// We have to get the second to last level to unset the value through a reference.
			$prevEntry = null;

			foreach($path as $pointer){
				if (!empty($pointer)){
					if (!isset($tmp[$pointer])){
						return;
					}
					$prevEntry =& $tmp;
					$tmp =& $tmp[$pointer];
				}
			}

			/**
			 * If we do unset($tmp) the reference is removed and not the value 
			 * it points to. If we do $prevEntry[$pointer] we change a value 
			 * inside the reference. In that case it will work.
			 */
			unset($prevEntry[$pointer]);

			$this->saveSettings();
		}
	
		/**
		* Get all settings as a Javascript script
		*
		* This function will output all settings as javascript, allowing easy inclusing in client-side javascript.
		*
		* @param string $indenting optional, this string will added in front of every javascript line
		* @param mixed $current part of the settings, used in recursion
		* @param string $base part of the output, used in recursion
		*/
		function getJavaScript($indenting = "\t", $current = null, $base="settings")
		{
			$this->Init();

			$output = "";
			$declare = false;
			if ($current===null){
				$current = $this->settings;
				$declare = true;
			}
			foreach ($current as $key=>$value) {
				if (!preg_match("/^([1-9][0-9]*|0)$/", $key)){
					// escape quotes and line ends
					// We have to escape < as well. When they are loaded in the main window, a 
					// </script>-tag will end the javascript block prematurely. Replaced to "\x3C".
					$key = str_replace(array("\\","\"","\n", "\r", "<"),array("\\\\","\\\"","\\n", "\\r", "\\x3C"),$key);
				}

				if (is_array($value)) {
					$output .= $this->getJavaScript($indenting, $value, $base."[\"".$key."\"]");
				} else {
					if (!preg_match("/^([1-9][0-9]*|0)$/", $value)){
						// escape quotes and line ends
						// We have to escape < as well. When they are loaded in the main window, a 
						// </script>-tag will end the javascript block prematurely. Replaced to "\x3C".
						$value = "\"".str_replace(array("\\","\"","\n", "\r", "<"),array("\\\\","\\\"","\\n", "\\r", "\\x3C"),$value)."\"";
					}
					$output .= $indenting.$base."[\"".$key."\"]"." = ".$value.";\n";
				}
			}
			return $indenting.($declare ? "var " : "").$base." = new Object();\n".$output;
		}
	
		/**
		* Get settings from store
		*
		* This function retrieves the actual settings from the store. They are stored in the string property 
		* PR_EC_WEBACCESS_SETTINGS in the store. It may have two formats:
		*
		* 1. XML format
		* 2. php serialize() format
		*
		* Format 1 was used in previous versions, while format 2 is used in newer Zarafa webaccess versions. Both
		* are attempted here, although if format 2 is found, format 1 is not attempted.
		*
		* This means that we are backward-compatible, but store the settings in the newest format we can find.
		*
		* Additionally, there are also settings in PR_EC_OUTOFOFFICE_* which are retrieved in this function also.
		*
		* This function returns nothing, but populates the 'settings' property of the class.
		* @access private
		*/
		function retrieveSettings()
		{	
			$this->settings = array();
			// first retrieve the "external" settings
			$this->retrieveExternalSettings();
			// read the settings property
			$stream = mapi_openpropertytostream($this->store, PR_EC_WEBACCESS_SETTINGS);
			if ($stream == false) {
				return ;
			}

			$stat = mapi_stream_stat($stream);
			mapi_stream_seek($stream, 0, STREAM_SEEK_SET);
			$settings_string = '';
			for($i=0;$i<$stat['cb'];$i+=1024){
				$settings_string .= mapi_stream_read($stream, 1024);
			}
			
			// suppress php notice in case unserializing fails
			$settings = @unserialize($settings_string);

			if (!$settings){ // backwards compatible with old saving method using XML
				$xml = new XMLParser();
				$settings = $xml->getData($settings_string);
			}

			if (is_array($settings) && isset($settings['settings']) && is_array($settings['settings'])){
				$this->settings = array_merge_recursive_overwrite($settings['settings'],$this->settings);
			}
		}
	
		/**
		* Save settings to store
		*
		* This function saves all settings to the store's PR_EC_WEBACCESS_SETTINGS property, and to the
		* PR_EC_OUTOFOFFICE_* properties.
		* @todo Why do we re-read settings after saving?
		*/
		function saveSettings()
		{
			$this->Init();

			$this->saveExternalSettings();
			
			$settings = serialize(array("settings"=>$this->settings));
	
	
			$stream = mapi_openpropertytostream($this->store, PR_EC_WEBACCESS_SETTINGS, MAPI_CREATE | MAPI_MODIFY);
			mapi_stream_setsize($stream, strlen($settings));
			mapi_stream_seek($stream, 0, STREAM_SEEK_SET);
			mapi_stream_write($stream, $settings);
			mapi_stream_commit($stream);
	
			mapi_savechanges($this->store);
	
			// reload settings from store...
			$this->retrieveSettings();
		}
	
	
		/**
		* Read 'external' settings from PR_EC_OUTOFOFFICE_*
		*
		* Internal function to retrieve the "external" settings from the store, these settings are normal properties on the store
		* @access private
		*/
		function retrieveExternalSettings()
		{
			$props = mapi_getprops($this->store, array(PR_EC_OUTOFOFFICE, PR_EC_OUTOFOFFICE_MSG, PR_EC_OUTOFOFFICE_SUBJECT));
			if (! isset($props[PR_EC_OUTOFOFFICE_MSG])) {
				$stream = mapi_openpropertytostream($this->store, PR_EC_OUTOFOFFICE_MSG);
				if ($stream) {
					$stat = mapi_stream_stat($stream);
					$props[PR_EC_OUTOFOFFICE_MSG] = mapi_stream_read($stream, $stat["cb"]);
				}
			}
			$this->settings["outofoffice"]["set"] = isset($props[PR_EC_OUTOFOFFICE]) ? ($props[PR_EC_OUTOFOFFICE] ? "true" : "false") : "false";
			$this->settings["outofoffice"]["message"] = windows1252_to_utf8(isset($props[PR_EC_OUTOFOFFICE_MSG]) ? $props[PR_EC_OUTOFOFFICE_MSG] : "");
			$this->settings["outofoffice"]["subject"] = windows1252_to_utf8(isset($props[PR_EC_OUTOFOFFICE_SUBJECT]) ? $props[PR_EC_OUTOFOFFICE_SUBJECT] : "");
		}
		
		/**
		* Internal function to save the "external" settings to the correct properties on the store
		*
		* Writes some properties to the PR_EC_OUTOFOFFICE_* properties
		* @access private
		*/
		function saveExternalSettings()
		{
			$this->Init();

			$props = array();
			$props[PR_EC_OUTOFOFFICE] = $this->settings["outofoffice"]["set"] == "true";
			$props[PR_EC_OUTOFOFFICE_MSG] = utf8_to_windows1252($this->settings["outofoffice"]["message"]);
			$props[PR_EC_OUTOFOFFICE_SUBJECT] = utf8_to_windows1252($this->settings["outofoffice"]["subject"]);
	
			mapi_setprops($this->store, $props);
			mapi_savechanges($this->store);
			
			// remove external settings so we don't save the external settings to PR_EC_WEBACCESS_SETTINGS
			unset($this->settings["outofoffice"]);
		}
	
		/**
		* Get session-wide settings
		*
		* Returns two explicit settings in an associative array:
		*
		* 'lang' -> setting('global/language')
		* 'color' -> setting('global/theme_color')
		*
		* @return array Associative array with 'lang' and 'color' entries.
		*/
		function getSessionSettings(){
			$this->Init();

			return array(
						"lang"=>$this->get("global/language",LANG),
						"color"=>$this->get("global/theme_color",THEME_COLOR)
				);
		}
	}
?>
