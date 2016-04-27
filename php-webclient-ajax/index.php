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
	* This is the entry point for every request that should return HTML
	* 
	* (one exception is that it also returns translated text for javascript)
	*/

	// load config file
	if (!file_exists("config.php")){
		die("<strong>config.php is missing!</strong>");
	}
	include("config.php");
	include("defaults.php");

	// This checks whether the Multi-User Calendar is present
	define('MUC_AVAILABLE', (is_file('client/modules/multiusercalendarmodule.js')));
	
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

	// Start the session
	session_name(COOKIE_NAME);

	if($_POST && array_key_exists(COOKIE_NAME, $_POST))
		session_id($_POST[COOKIE_NAME]);

	session_start();
	
	// check if config is correct
	if (defined("CONFIG_CHECK")){
		include("server/class.configcheck.php");
		new ConfigCheck(CONFIG_CHECK);
	}

	// Include the files
	require("mapi/mapi.util.php");
	require("mapi/mapicode.php");
	require("mapi/mapidefs.php");
	require("mapi/mapitags.php");
	require("mapi/mapiguid.php");
	require("server/util.php");

	require("server/core/constants.php");
	require("server/core/class.conversion.php");
	require("server/core/class.mapisession.php");
	require("server/core/class.entryid.php");
	
	require("server/core/class.request.php");
	require("server/core/class.settings.php");
	require("server/core/class.language.php");

	require("server/core/class.state.php");
	require("server/core/class.attachmentstate.php");

	require("server/core/class.pluginmanager.php");
	require("server/core/class.plugin.php");
	
	// Destroy session if an user loggs out
	if($_GET && array_key_exists("logout", $_GET)) {
		$_SESSION = array();
		
		if (isset($_COOKIE[session_name()])) {
			setcookie(session_name(), '', time()-42000, '/');
		}
		session_destroy();

		header("Location: index.php", true, 303);
		exit;
	}

	// Set the session variables if it is posted
	if($_POST && array_key_exists("username", $_POST) && array_key_exists("password", $_POST)) {
		$_SESSION["username"] = utf8_to_windows1252($_POST["username"]);
		$password = utf8_to_windows1252($_POST["password"]);
		// if user has openssl module installed
		if(function_exists("openssl_encrypt")) {
			// In PHP 5.3.3 the iv parameter was added
			if(version_compare(phpversion(), "5.3.3", "<")) {
				$_SESSION['password'] = openssl_encrypt($password,"des-ede3-cbc",PASSWORD_KEY,0);
			} else {
				$_SESSION['password'] = openssl_encrypt($password,"des-ede3-cbc",PASSWORD_KEY,0,PASSWORD_IV);
			}
		} else if (function_exists("mcrypt_encrypt")) {
			$_SESSION["password"] = base64_encode(mcrypt_encrypt(MCRYPT_TRIPLEDES, PASSWORD_KEY, $password, MCRYPT_MODE_CBC, PASSWORD_IV));
		} else {
			$_SESSION["password"] = $password;
		}
	}

	if(!DISABLE_REMOTE_USER_LOGIN){
		// REMOTE_USER is set when apache has authenticated the user
		if( ! $_POST && $_SERVER && array_key_exists("REMOTE_USER", $_SERVER)) {
			$_SESSION["username"] = utf8_to_windows1252($_SERVER['REMOTE_USER']);
			if (LOGINNAME_STRIP_DOMAIN) {
				$_SESSION["username"] = ereg_replace('@.*', '', $_SESSION["username"]);
			}
			$_SESSION["password"] = "";
		}
	}

	// Create global mapi object. This object is used in many other files
	$GLOBALS["mapisession"] = new MAPISession();
	if (isset($_SESSION["username"]) && isset($_SESSION["password"])) {
		$sslcert_file = defined('SSLCERT_FILE') ? SSLCERT_FILE : null;
		$sslcert_pass = defined('SSLCERT_PASS') ? SSLCERT_PASS : null;
		$hresult = $GLOBALS["mapisession"]->logon($_SESSION["username"], $_SESSION["password"], DEFAULT_SERVER, $sslcert_file, $sslcert_pass);
		if ($hresult != NOERROR){
			// login failed, remove session
			$_SESSION = array();
			$_SESSION["hresult"] = $hresult;
		}
	}

	// Check if user is authenticated
	if ($GLOBALS["mapisession"]->isLoggedOn()) {
		// Authenticated

		// Instantiate Plugin Manager
		$GLOBALS['PluginManager'] = new PluginManager();
		$GLOBALS['PluginManager']->detectPlugins();
		$GLOBALS['PluginManager']->initPlugins();
		
		// Create globals settings object
		$GLOBALS["settings"] = new Settings();
		
		// Create global language object
		$GLOBALS["language"] = new Language();

		// Set session settings (language & style)
		foreach($GLOBALS["settings"]->getSessionSettings() as $key=>$value){
			$_SESSION[$key] = $value;
		}
		
		// Get settings from post or session or settings
		if (isset($_REQUEST["language"]) && $GLOBALS["language"]->is_language($_REQUEST["language"])) {
			$lang = $_REQUEST["language"];
			$GLOBALS["settings"]->set("global/language", $lang);
		} else if(isset($_SESSION["lang"])) {
			$lang = $_SESSION["lang"];
		} else $lang = $GLOBALS["settings"]->get("global/language", LANG);
		
		$GLOBALS["language"]->setLanguage($lang);
		if($_GET && array_key_exists("logon", $_GET)) {
			$GLOBALS['PluginManager']->triggerHook("server.index.login.success");
			if(isset($_GET["action"]) && $_GET["action"] != "" ) {
				$actionReqURI = getActionRequestURI();

				// if action attributes are passed in GET variable then append it to URL for
				// further processing
				$url = "index.php" . $actionReqURI;

				header("Location: $url", true, 303);
				exit;
			} else {
				header("Location: index.php", true, 303);
				exit;
			}
		}

		// add extra header
		header("X-Zarafa: ".phpversion("mapi").(defined("SVN") ? "-".SVN:""));
		// Temporary fix for Internet Explorer 9, will make it work under IE8 compatibility mode
		header("X-UA-Compatible: IE=8");

		// external files who need our login
		if ($_GET && array_key_exists("load", $_GET)) {

			// Get Attachment data from state and put it into the $_SESSION
			switch ($_GET["load"]) {
				case "translations.js":
					$GLOBALS['PluginManager']->triggerHook("server.index.load.jstranslations.before");
					include("client/translations.js.php");
					$GLOBALS['PluginManager']->triggerHook("server.index.load.jstranslations.after");
					break;
				case "dialog":
					// If the dialog is called with attachid in URL we want to call the upload attachment file 
					// first to upload the files to the server, and add them to session files.
					if($_GET && array_key_exists("attachment_id", $_GET)){
						$GLOBALS['PluginManager']->triggerHook("server.index.load.upload_attachment.before");
						include("server/upload_attachment.php");
						$GLOBALS['PluginManager']->triggerHook("server.index.load.upload_attachment.after");
					}
					$GLOBALS['PluginManager']->triggerHook("server.index.load.dialog.before");
					include("client/dialog.php");
					$GLOBALS['PluginManager']->triggerHook("server.index.load.dialog.after");
					break;
				case "upload_attachment":
					$GLOBALS['PluginManager']->triggerHook("server.index.load.upload_attachment.before");
					include("server/upload_attachment.php");
					$GLOBALS['PluginManager']->triggerHook("server.index.load.upload_attachment.after");
					break;
				case "download_attachment":
					$GLOBALS['PluginManager']->triggerHook("server.index.load.download_attachment.before");
					include("client/download_attachment.php");
					$GLOBALS['PluginManager']->triggerHook("server.index.load.download_attachment.after");
					break;
				case "download_message":
					$GLOBALS['PluginManager']->triggerHook("server.index.load.download_message.before");
					include("client/download_message.php");
					$GLOBALS['PluginManager']->triggerHook("server.index.load.download_message.after");
					break;
				default:
					// These hooks are defined twice (also when no "load" argument is supplied)
					$GLOBALS['PluginManager']->triggerHook("server.index.load.main.before");
					include("client/webclient.php");
					$GLOBALS['PluginManager']->triggerHook("server.index.load.main.after");
					break;
			}
		} else {
			// Clean up old state files in tmp/session/
			$state = new State("index");
			$state->clean();

			// Clean up old attachments in tmp/attachments/
			$state = new AttachmentState();
			$state->clean();
					
			// clean search folders
			cleanSearchFolders();

			// These hooks are defined twice (also when there is a "load" argument supplied)
			$GLOBALS['PluginManager']->triggerHook("server.index.load.main.before");
			// Include webclient
			include("client/webclient.php");
			$GLOBALS['PluginManager']->triggerHook("server.index.load.main.after");
		}
	} else { // Not authenticated, goto login page
		// Create global language object
		$GLOBALS["language"] = new Language();
		$GLOBALS["language"]->setLanguage(LANG);

		include("client/login.php");
	}
?>
