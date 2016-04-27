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
	// Constants for regular expressions which are used in get method to verify the input string
	define("ID_REGEX", "/^[a-z0-9_]+$/im");
	define("STRING_REGEX", "/^[a-z0-9_\s()@]+$/im");
	define("ALLOWED_EMAIL_CHARS_REGEX", "/^[-a-z0-9_\.@!#\$%&'\*\+/=\?\^_`\{\|\}~]+$/im");
	define("TIMESTAMP_REGEX", "/^[0-9]+$/im");
	define("NUMERIC_REGEX", "/^[0-9]+$/im");
	// Don't allow "\/:*?"<>|" characters in filename.
	define("FILENAME_REGEX", "/^[^\/\:\*\?\"\<\>\|\\\]+$/im");

	/**
	* Function to retrieve a $_GET variable to prevent XSS
	*
	* $var = varibale requested
	* $default = default result when $var doesn't exist
	* $usequote = if $var must be surrounded by quotes, note that $default isn't surrounded by quotes even if this is set here!
	* $regex = To prevent unusual hackers attack / validate the values send from client.
	*/	
	function get($var, $default="", $usequote=false, $regex = false){
		$result = $default;
		if (isset($_GET[$var])){
			$result = addslashes($_GET[$var]);
			if($regex) {
				$match = preg_match_all($regex, $result, $matches);
				if(!$match){
					$result = false;
					$usequote = false;
				}
			}
			if ($usequote===true) 
				$usequote = "'";
			if ($usequote!==false)
				$result = $usequote.$result.$usequote;
		}
		return $result;
	}


	function createConfirmButtons($onclick)
	{
		$buttons = "<div class=\"confirmbuttons\">\n";
		$buttons .= "<input class=\"buttonsize\" type=\"button\" value=\"" . _("Ok") . "\" onclick=\"" . $onclick . "\">\n";
		$buttons .= "<input class=\"buttonsize\" type=\"button\" value=\"". _("Cancel") . "\" onclick=\"window.close();\">\n";
		$buttons .= "</div>\n";
		
		return $buttons;
	}
	
	function createCloseButton($onclick)
	{
		$buttons = "<div class=\"closebutton\">\n";
		$buttons .= "<input class=\"buttonsize\" type=\"button\" value=\"" . _("Close") . "\" onclick=\"" . $onclick . "\">\n";
		$buttons .= "</div>\n";
		
		return $buttons;
	}
	
	/**
	* Function to get some buttons
	*
	* one button is specified as an array argument, use multiple arguments for more buttons
	*
	* array("title"=>"Ok", "handler"=>"submit();", "shortcut"=>"S")
	*/	
	function createButtons()
	{
		$argc = func_num_args();
		$argv = func_get_args();
		
		$buttons = "<div class=\"buttons\">\n";
		for($i=0; $i<$argc; $i++){
			$title   = isset($argv[$i]["title"])   ? $argv[$i]["title"]   : _("Button");
			$handler = isset($argv[$i]["handler"]) ? $argv[$i]["handler"] : "alert('Not implemented');";
			$shortcut= isset($argv[$i]["shortcut"])? " accesskey=\"".$argv[$i]["shortcut"]."\"": "";

			
			$buttons .= "<input class=\"buttonsize\" type=\"button\" value=\"" . $title . "\" onclick=\"" . $handler . "\"".$shortcut.">\n";
		}
		$buttons .= "</div>\n";
		
		return $buttons;
	}
?>
