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
	* This file is the dispatcher of the whole application, every request for data enters 
	* here. XML is received and send to the client.
	*/
	
	// Include files
	include("config.php");
	include("defaults.php");
	include("server/util.php");
	require("server/PEAR/JSON.php");
	
	require("mapi/mapi.util.php");
	require("mapi/mapicode.php");
	require("mapi/mapidefs.php");
	require("mapi/mapitags.php");
	require("mapi/mapiguid.php");

	require("server/core/class.conversion.php");
	require("server/core/class.mapisession.php");
	require("server/core/class.entryid.php");
	
	include("server/core/constants.php");
	
	include("server/core/class.state.php");
	include("server/core/class.attachmentstate.php");
	include("server/core/class.request.php");
	include("server/modules/class.module.php");
	include("server/modules/class.listmodule.php");
	include("server/modules/class.itemmodule.php");
	include("server/core/class.operations.php");
	include("server/core/class.properties.php");
	include("server/core/class.tablecolumns.php");
	include("server/core/class.bus.php");
	include("server/core/class.settings.php");
	include("server/core/class.language.php");
	include("server/core/class.pluginmanager.php");
	include("server/core/class.plugin.php");

	ob_start();
	setlocale(LC_CTYPE, "en_US.UTF-8");

	// Set timezone
	if(function_exists("date_default_timezone_set")) {
		if(defined('TIMEZONE') && TIMEZONE) {
			date_default_timezone_set(TIMEZONE);
		} else if(!ini_get('date.timezone')) {
			date_default_timezone_set('Europe/London');
		}
	}

	// Get the available modules
	$GLOBALS["availableModules"] = getAvailableModules();

	// Callback function for unserialize
	// Module objects of the previous request are stored in the session. With this
	// function they are restored to PHP objects.
	ini_set("unserialize_callback_func", "sessionModuleLoader");
	
	// Start session
	session_name(COOKIE_NAME);
	session_start();
	
	// Create global mapi object. This object is used in many other files
	$GLOBALS["mapisession"] = new MAPISession();
	// Logon, the username and password are set in the "index.php" file. So whenever
	// an user enters this file, the username and password whould be set in the $_SESSION
	// variable
	if (isset($_SESSION["username"]) && isset($_SESSION["password"])) {
		$sslcert_file = defined('SSLCERT_FILE') ? SSLCERT_FILE : null;
		$sslcert_pass = defined('SSLCERT_PASS') ? SSLCERT_PASS : null;
		$hresult = $GLOBALS["mapisession"]->logon($_SESSION["username"], $_SESSION["password"], DEFAULT_SERVER, $sslcert_file, $sslcert_pass);
	}else{
		$hresult = MAPI_E_UNCONFIGURED;
	}

	if(isset($_SESSION["lang"])) {
		$session_lang = $_SESSION["lang"];
	}else{
		$session_lang = LANG;
	}
	
	// Close the session now, so we're not blocking other clients
	session_write_close();

	// Set headers for XML
	header("Content-Type: text/xml; charset=utf-8");
	header("Expires: ".gmdate( "D, d M Y H:i:s")."GMT");
	header("Last-Modified: ".gmdate( "D, d M Y H:i:s")."GMT");
	header("Cache-Control: no-cache, must-revalidate");
	header("Pragma: no-cache");
	header("X-Zarafa: ".phpversion("mapi").(defined("SVN") ? "-".SVN:""));
		
	// Check is the user is authenticated
	if ($GLOBALS["mapisession"]->isLoggedOn()) {
		// Authenticated
		// Execute request
		
		// Instantiate Plugin Manager
		$GLOBALS['PluginManager'] = new PluginManager();
		$GLOBALS['PluginManager']->detectPlugins();
		$GLOBALS['PluginManager']->initPlugins();
		// Get the available plugin modules
		$GLOBALS["availablePluginModules"] = $GLOBALS['PluginManager']->getAvailablePluginModules();

		// Create global operations object
		$GLOBALS["operations"] = new Operations();
		// Create global properties object
		$GLOBALS["properties"] = new Properties();
		// Create global tablecolumns object
		$GLOBALS["TableColumns"] = new TableColumns();
		// Create global settings object
		$GLOBALS["settings"] = new Settings();

		// Create global language object
		$GLOBALS["language"] = new Language();
		// Set the correct language
		$GLOBALS["language"]->setLanguage($session_lang);

		// Get the state information for this subsystem
		if(isset($_GET["subsystem"]))
			$subsystem = $_GET["subsystem"];
		else
			$subsystem = "anonymous"; // Currently should never happen	

		$state = new State($subsystem);
		
		// Lock the state of this subsystem
		$state->open();
		
		// Get the bus object for this subsystem
		$bus = $state->read("bus");

		if(!$bus)
			// Create global bus object
			$bus = new Bus();
		
		// Make bus global
		$GLOBALS["bus"] = $bus;
		
		// Reset any spurious information in the bus state
		$GLOBALS["bus"]->reset();
		
		// Create new request object
		$request = new Request();
		
		// Get the XML from the client
		$xml = readXML();
		if (function_exists("dump_xml")) dump_xml($xml,"in"); // debugging
		
		// Execute the request
		$xml = $request->execute($xml);

		/*
		 * If we get any errors with more unicode characters then also reject overly long
		 * 2 byte sequences, as well as characters above U+10000 and replace with ?
		
		$xml = preg_replace('/[\x00-\x08\x10\x0B\x0C\x0E-\x19\x7F]'.
		 '|[\x00-\x7F][\x80-\xBF]+'.
		 '|([\xC0\xC1]|[\xF0-\xFF])[\x80-\xBF]*'.
		 '|[\xC2-\xDF]((?![\x80-\xBF])|[\x80-\xBF]{2,})'.
		 '|[\xE0-\xEF](([\x80-\xBF](?![\x80-\xBF]))|(?![\x80-\xBF]{2})|[\x80-\xBF]{3,})/S',
		 '?', $xml );
		 */

		//reject overly long 3 byte sequences and UTF-16 surrogates and replace with ?
		$xml = preg_replace('/\xE0[\x80-\x9F][\x80-\xBF]'. // Remove strings like \xE0\x9F\xBF
		 '|\xEF[\xA0-\xBF][\x80-\xBF]'. // Remove strings like \xEF\xBF\xBF
		 '|\xED[\xA0-\xBF][\x80-\xBF]/S','?', $xml ); // Remove strings like \xED\xBF\xBF

		if (function_exists("dump_xml")) dump_xml($xml,"out"); // debugging

		// Check if we can use gzip compression
		if ((!defined("DEBUG_GZIP")||DEBUG_GZIP) && $GLOBALS["settings"]->get("global/use_gzip","true")=="true" && function_exists("gzencode") && isset($_SERVER["HTTP_ACCEPT_ENCODING"]) && strpos($_SERVER["HTTP_ACCEPT_ENCODING"], "gzip")!==false){
			// Set the correct header and compress the XML
			header("Content-Encoding: gzip");
			echo gzencode($xml);
		}else {
			echo $xml;
		}
		
		// Reset the BUS before saving to state information
		$GLOBALS["bus"]->reset();

		if(isset($GLOBALS["bus"]))
			$state->write("bus", $GLOBALS["bus"]);		

		// You can skip this as well because the lock is freed after the PHP script ends
		// anyway.
		$state->close();

	} else {
		echo "<zarafa>\n";
		echo "\t<error logon=\"false\" mapi=\"".get_mapi_error_name($hresult)."\">Logon failed</error>\n";
		echo "</zarafa>";
	}
?>
