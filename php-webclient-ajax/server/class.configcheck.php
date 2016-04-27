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
 * This class will check the server configuration and it stops the 
 * webaccess from working until this is fixed
 */

class ConfigCheck 
{
	var $result;

	function ConfigCheck($haltOnError = true)
	{
		$this->haltOnError = $haltOnError;

		$this->result = true;	

		// here we check our settings, changes to the config and
		// additonal checks must be added/changed here
		$this->checkPHP("4.3", "You must upgrade PHP");
		$this->checkExtension("mapi", "6.00-9379", "If you have upgraded zarafa, please restart Apache");
		$this->checkExtension("gettext", null, "Install the gettext extension for PHP");
		$this->checkInclude("mapi/mapitags.php","Can't find/open the MAPI include files or safe mode restrictions in effect. Check your PHP config or check your include path in config.php.");
		$this->checkWindows("With Windows you must use a http/https server connection.");
		$this->checkPHPsetting("register_globals", "off", "Modify this setting in '%s'");
		$this->checkPHPsetting("magic_quotes_gpc", "off", "Modify this setting in '%s'");
		$this->checkPHPsetting("magic_quotes_runtime", "off", "Modify this setting in '%s'");
		$this->checkPHPsetting("short_open_tag", "on", "Modify this setting in '%s'");
		$this->checkPHPsetting("session.auto_start", "off", "Modify this setting in '%s'");
		$this->checkPHPsetting("output_handler", "", "With this option set, it is unsure if the webaccess will work correctly");
		$this->checkPHPsetting("zlib.output_handler", "", "With this option set, it is unsure if the webaccess will work correctly");
		$this->checkPHPsetting("zlib.output_compression", "off", "With this option set, it could occure that XMLHTTP-requests will fail");

		if (CONFIG_CHECK_COOKIES_HTTP && version_compare(phpversion(), "5.2") > -1) {
			$this->checkPHPsecurity("session.cookie_httponly", "on", "Modify this setting in '%s'");
		}
		if (CONFIG_CHECK_COOKIES_SSL) {
			$this->checkPHPsecurity("session.cookie_secure", "on", "Modify this setting in '%s'");
		}

		$this->checkDirectory(TMP_PATH, "rw", "Please make sure this directory exists and is writable for PHP/Apache");
		$this->checkFunction("iconv", "Install the 'iconv' module for PHP, or else you don't have euro-sign support.");
		$this->checkFunction("gzencode", "You don't have zlib support: <a href=\"http://php.net/manual/en/ref.zlib.php#zlib.installation\">http://php.net/manual/en/ref.zlib.php#zlib.installation</a>");

		// check if there were *any* errors and we need to stop the webaccess
		if (!$this->result && $this->haltOnError){
			?>
				<p style="font-weight: bold;">Zarafa WebAccess can't start because of incompatible configuration.</p>
				<p>Please correct above errors, a good start is by checking your '<tt><?php echo $this->get_php_ini(); ?></tt>' file.</p>
				<p>Or if you wish, you can disable this config check by editing the file '<tt><?php echo dirname($_SERVER["SCRIPT_FILENAME"]) ?>/config.php</tt>', but this is not recommend.</p>
			<?php
			exit;
		}

	}
		

	/**
	 * This function throws all the errors, make sure that the check-function 
	 * will call this in case of an error.
	 */
	function error($string, $help)
	{
		if ($this->haltOnError) {
			printf("<div style=\"color: #f00;\">%s</div><div style=\"font-size: smaller; margin-left: 20px;\">%s</div>\n",$string, $help);
		}else{
			trigger_error(strip_tags($string), E_USER_NOTICE);
		}
		$this->result = false;
	}

	/**
	 * See error()
	 */
	function error_version($name, $needed, $found, $help)
	{
		$this->error(sprintf("<strong>Version error:</strong> %s %s found, but %s needed.",$name, $needed, $found), $help);
	}

	/**
	 * See error()
	 */
	function error_notfound($name, $help)
	{
		$this->error(sprintf("<strong>Not Found:</strong> %s not found", $name), $help);
	}

	/**
	 * See error()
	 */
	function error_config($name, $needed, $help)
	{
		$help = sprintf($help, "<tt>".$this->get_php_ini()."</tt>");
		$this->error(sprintf("<strong>PHP Config error:</strong> %s must be '%s'", $name, $needed), $help);
	}

 	/**
 	 * See error()
 	 */
	function error_security($name, $needed, $help)
	{
		$help = sprintf($help, "<tt>" . $this->get_site_config() . "</tt>");
		$this->error("<strong>PHP Security Config error:</strong> $name must be '$needed'", $help);
	}

	/**
	 * See error()
	 */
	function error_directory($dir, $msg, $help)
	{
		$this->error("<strong>Directory Error:</strong> $dir $msg", $help);
	}

	/**
	 * Retrieves the location of the apache site config
	 */
	function get_site_config()
	{
		if (isset($this->siteconfig)) {
			return $this->siteconfig;
		}

		// This is not 100% accurate, so it needs to be improved a bit.
		$result = "sites-enabled" . DIRECTORY_SEPARATOR . "zarafa-webapp";

		ob_start();
		phpinfo(INFO_MODULES);
		$phpinfo = ob_get_contents();
		ob_end_clean();

		preg_match("/<td class=\"e\">[\s]*Server Root[\s]*<\/td>[\s]*<td class=\"v\">[\s]*(.*)[\s]*<\/td>/i", $phpinfo, $matches);
		if (isset($matches[1])){
			$result = trim($matches[1]) . DIRECTORY_SEPARATOR . $result;
		}
		$this->siteconfig = $result;
		return $result;
 	}

	/**
	 * Retrieves the location of php.ini
	 */
	function get_php_ini()
	{
		if (isset($this->phpini)){
			return $this->phpini;
		}

		$result = "php.ini";
	
		ob_start();
		phpinfo(INFO_GENERAL);
		$phpinfo = ob_get_contents();
		ob_end_clean();

		preg_match("/<td class=\"v\">(.*php[45]?\.ini)/i", $phpinfo, $matches);
		if (isset($matches[0])){
			$result = $matches[0];
		}
		$this->phpini = $result;
		return $result;
	}

	/**********************************************************\
	*  Check functions                                         *
	\**********************************************************/


	/**
	 * Checks for the PHP version
	 */
	function checkPHP($version, $help_msg="")
	{
		$result = true;
		if (version_compare(phpversion(), $version) == -1){
			$this->error_version("PHP",phpversion(), $version, $help_msg);
			$result = false;
		}
		return $result;
	}

	/**
	 * Check if extension is loaded and if the version is what we need
	 */
	function checkExtension($name, $version="", $help_msg="")
	{
		$result = true;
		if (extension_loaded($name)){
			if (version_compare(phpversion($name), $version) == -1){
				$this->error_version("PHP ".$name." extension",phpversion($name), $version, $help_msg);
				$result = false;
			}
		}else{
			$this->error_notfound("PHP ".$name." extension", $help_msg);
			$result = false;
		}
		return $result;
	}

	/**
	 * Check if a function exists
	 */
	function checkFunction($name, $help_msg="")
	{
		$result = true;
		if (!function_exists($name)){
			$this->error_notfound("PHP function '".$name."' ", $help_msg);
			$result = false;
		}
		return $result;
	}
	
	/**
	 * This function checks if a specific php setting (php.ini) is set to a 
	 * value we need, for example register_globals
	 */
	function checkPHPsetting($setting,$value_needed, $help_msg=""){
		$result = true;
		// convert $value_needed
		switch($value_needed){
			case "on":
			case "yes":
			case "true":
				$value = 1;
				break;

			case "off":
			case "no":
			case "false":
				$value = 0;
				break;

			default:
				$value = $value_needed;
		}

		if (ini_get($setting) != $value){
			$this->error_config($setting, $value_needed, $help_msg);
			$result = false;
		}
		return $result;
	}

	/**
	 * This function checks if a specific php setting (php.ini) is set to a 
	 * value we need, for example register_globals
	 */
	function checkPHPsecurity($setting,$value_needed, $help_msg=""){
		$result = true;
		// convert $value_needed
		switch($value_needed){
			case "on":
			case "yes":
			case "true":
				$value = 1;
				break;

			case "off":
			case "no":
			case "false":
				$value = 0;
				break;

			default:
				$value = $value_needed;
		}

		if (ini_get($setting) != $value){
			$this->error_security($setting, $value_needed, $help_msg);
 			$result = false;
 		}
 		return $result;
 	}

	/**
	 * This functions checks if a directory exists and if requested also if
	 * this directory is readable/writable specified with the $states parameter
	 *
	 * $states is a string which can contain these chars:
	 *      r  - check if directory is readable
	 *      w  - check if directory is writable
	 */
	function checkDirectory($dir, $states="r", $help_msg="")
	{
		$result = true;

		if (!is_dir($dir)){
			@mkdir($dir); // try to create the directory
		}

		if (is_dir($dir)){
			$states = strtolower($states);
			if (strpos($states,"r")!==FALSE){
				if (!is_readable($dir)){
					$this->error_directory($dir, "isn't readable", $help_msg);
					$result = false;
				}
			}
			if (strpos($states,"w")!==FALSE){
				if (!is_writable($dir)){
					$this->error_directory($dir, "isn't writable", $help_msg);
					$result = false;
				}
			}
		}else{
			$this->error_directory($dir, "doesn't exists", $help_msg);
			$result = false;
		}
		return $result;
	}

	function checkInclude($filename, $help_msg="")
	{
		$result = false;

		if (!file_exists($filename)){
			$paths = explode(PATH_SEPARATOR, get_include_path());
			foreach ($paths as $path) {
				$fullpath = $path . "/" . $filename;
				if (file_exists($fullpath)) {
					$result = true;
					break;
				}
			}
		}else{
			$result = true;
		}

		if (!$result){
			$this->error_notfound("MAPI include files", $help_msg);
		}

		return $result;
	}

	function checkWindows($help_msg="")
	{
		$result = true;
		$regex = "'file://\\\\\\[a-z0-9_\.]+\\\\pipe\\\\.*'i";
		if (strtoupper(substr(PHP_OS, 0, 3)) == "WIN" && (substr(DEFAULT_SERVER,0,4)!="http" && preg_match($regex, DEFAULT_SERVER))){
			$this->error("<strong>Invalid server connection:</strong> ".DEFAULT_SERVER,$help_msg);
			$result = false;
		}
		return $result;
	}
}

?>
