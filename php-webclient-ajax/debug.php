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
* This file is only used for debugging purpose, this file isn't needed in
* the release version.
*/

/**
* Set this to let zarafa.php exports the XML
* but please note that you must create the DEBUG_XMLOUT_DIR directory and that
* it is writable by PHP
*/
define("DEBUG_XMLOUT", true);
define("DEBUG_XMLOUT_DIR", "debug_xml/");
define("DEBUG_XMLOUT_GZIP", false);
define("DEBUG_XMLOUT_FORMAT_INPUT", true);
define("DEBUG_XML_INDENT", true);
define("DEBUG_PLUGINS", true);

// dump file, must be writable
define("DEBUG_DUMP_FILE", "debug.txt");

// if true, webaccess won't be combining JS en CSS files and disables caching
define("DEBUG_LOADER", true);

// if false, webaccess would never use GZIP compression
define("DEBUG_GZIP", true);

// show SVN version if possible
define("DEBUG_SHOW_SVN_VERSION", true);

// if true, show server url (if not localhost) in title of webaccess, or when this is a string, show that string
define("DEBUG_SHOW_SERVER", true);

// log php errors into apache log
ini_set("log_errors", true);

// end config


//	This PHP error handler, only dumps the error to our dump file, including mapi_last_hresult
error_reporting(E_ALL);
set_error_handler("zarafa_error_handler");

// get server url
if (DEBUG_SHOW_SERVER){
	if (DEBUG_SHOW_SERVER===true){
		if (preg_match_all("|http://([^:/]+).*|", DEFAULT_SERVER, $matches)){
			if ($matches[1][0]!="localhost"){ // other than localhost
				define("DEBUG_SERVER_ADDRESS", $matches[1][0]);
			}
		} else {
			// other non http server locations
			define("DEBUG_SERVER_ADDRESS", str_replace("file://","",DEFAULT_SERVER)); // show location
		}
	}else{ // show that string
		define("DEBUG_SERVER_ADDRESS", DEBUG_SHOW_SERVER);
	}
}

// get SVN build
if (DEBUG_SHOW_SVN_VERSION && is_dir(".svn")){
	$svnversion = @shell_exec("svnversion .");
	if (!empty($svnversion)){
		define("SVN", $svnversion);
	}
}

/**
* Custom error handler, here we check to see if it is a MAPI error and dump all info
* to the dump file, and finally we redirect the error back to PHP
*/
function zarafa_error_handler($errno, $errstr, $errfile, $errline, $errcontext)
{	
	$error = array("msg"=>$errstr, "file"=>$errfile.":".$errline);

	if($errno == E_STRICT)
	    return;

	if (strpos($errstr,"mapi_")!==false) {
			$error["mapi"] = get_mapi_error_name();
	}
	dump($error, "ERROR", true);

	switch($errno){
		case E_WARNING:
			$errno = E_USER_WARNING;
			break;
		case E_STRICT:
		case E_NOTICE:
			$errno = E_USER_NOTICE;
			break;
		case E_ERROR:
			$errno = E_USER_ERROR;
			break;
	}
	trigger_error($errstr." - ".$errfile.":".$errline, $errno);
}
if (!defined("E_STRICT"))
	define("E_STRICT", E_NOTICE);

/**
* This function "dumps" the contents of $variable to debug.txt (and when requested also a backtrace)
*/
function dump($variable, $title="", $backtrace=false)
{
	$file = fopen(DEBUG_DUMP_FILE, "a+");
	$date = strftime("%d-%b-%Y");
	$time = strftime("%H:%M:%S");
	fwrite($file, ("[" . $date . " " . $time . "] " . $title. " - " . var_export($variable, true) . "\r\n"));
	if ($backtrace){
		dump(_debug_backtrace(false));
	}
}

// use this function when you want to dump an array of MAPI properties
function dump_props($variable,$title=""){
	global $_debug_propList;
	if ($_debug_propList===false){
		// caching
		foreach(get_defined_constants() as $key=>$value){
			if (substr($key,0,3)=='PR_'){
				$_debug_propList[$key] = $value;
			}
		}
	}

	foreach($variable as $key=>$value){
		$prop = array_keys($_debug_propList,$key);
		if (count($prop)>0){
			foreach($prop as $k=>$v){
				$variable["0x".str_pad(strtoupper(dechex($key)),8, '0', STR_PAD_LEFT).' '.$v] = $value;
			}
		}else{
			$variable["0x".str_pad(strtoupper(dechex($key)),8, '0', STR_PAD_LEFT)] = $value;
		}
		unset($variable[$key]);
	}
	dump($variable,$title);
}
$_debug_propList = false;

/**
* This function is used for the in/output by zarafa.php to store the XML to disk
*/
function dump_xml($xml, $prefix){
	global $debug_xml_id;

	if (DEBUG_XMLOUT){
		if (!isset($debug_xml_id) || empty($debug_xml_id)){
				$debug_xml_id = strftime("%Y%m%d%H%M%S").uniqid("_");
		}

		if (DEBUG_XMLOUT_FORMAT_INPUT && $prefix == "in"){
			$parser = new XMLParser();
			$data = $parser->getData($xml);
			$builder = new XMLBuilder();
			$xml = $builder->build($data);
		}
		
		if (is_dir(DEBUG_XMLOUT_DIR)){
				$fh = fopen(DEBUG_XMLOUT_DIR.$prefix."_".$debug_xml_id.".xml".(DEBUG_XMLOUT_GZIP?".gz":""), "w");
				fwrite($fh, (DEBUG_XMLOUT_GZIP?gzencode($xml):$xml));
				fclose($fh);
		}
	}
}

/**
* internal function to generate a backtrace
*/
function _debug_backtrace($html=true){
  $output = $html?"<br/>\n":"\n";
  foreach(debug_backtrace() as $t){
    if (isset($t['file']) && $t['file']!=__FILE__){
      $output .= $html?'<strong>@</strong> ':'@ ';
      if(isset($t['file'])) {
        $output .= basename($t['file']) . ':' . $t['line'];
      } else {
       $output .= '[PHP inner-code]';
      }
      $output .= ' - ';
      if(isset($t['class'])) $output .= $t['class'] . $t['type'];
      $output .= $t['function'];
      if(isset($t['args']) && sizeof($t['args']) > 0) {
        $output .= '(...)';
      } else {
        $output .= '()';
      }
      $output .= $html?"<br/>\n":"\n";
    }
  }
  return $output;
}

/**
* This function is used for dumping client side restrictions in user readable form
*/
function dump_restriction($restriction) {
	$variable = simplify_restriction($restriction);

	$file = fopen(DEBUG_DUMP_FILE, "a+");
	$date = strftime("%d-%b-%Y");
	$time = strftime("%H:%M:%S");
	fwrite($file, ("[" . $date . " " . $time . "] Restrictions - " . var_export($variable, true) . "\r\n"));
}

/**
 * This function is used to covert all constants of restriction into a human readable strings
 */
function simplify_restriction($restriction) {
	if (!is_array($restriction)){
		return $restriction;
	}

	switch($restriction[0]){
		case RES_AND:
			$restriction[0] = "RES_AND";
			if(isset($restriction[1][0]) && is_array($restriction[1][0])) {
				foreach(array_keys($restriction[1]) as $key) {
					$restriction[1][$key] = simplify_restriction($restriction[1][$key]);
				}
			} else if(isset($restriction[1]) && $restriction[1]) {
				$restriction[1] = simplify_restriction($restriction[1]);
			}
			break;
		case RES_OR:
			$restriction[0] = "RES_OR";
			if(isset($restriction[1][0]) && is_array($restriction[1][0])) {
				foreach(array_keys($restriction[1]) as $key) {
					$restriction[1][$key] = simplify_restriction($restriction[1][$key]);
				}
			} else if(isset($restriction[1]) && $restriction[1]) {
				$restriction[1] = simplify_restriction($restriction[1]);
			}
			break;
		case RES_NOT:
			$restriction[0] = "RES_NOT";
			$restriction[1][0] = simplify_restriction($restriction[1][0]);
			break;
		case RES_COMMENT:
			$restriction[0] = "RES_COMMENT";
			$res = simplify_restriction($restriction[1][RESTRICTION]);
			$props = $restriction[1][PROPS];

			foreach(array_keys($props) as $key) {
				$propTag = $props[$key][ULPROPTAG];
				$propValue = $props[$key][VALUE];

				unset($props[$key]);

				$props[$key]["ULPROPTAG"] = is_string($propTag) ? $propTag : Conversion::getPropertyName($propTag);
				$props[$key]["VALUE"] = is_array($propValue) ? $propValue[$propTag] : $propValue;
			}

			unset($restriction[1]);

			$restriction[1]["RESTRICTION"] = $res;
			$restriction[1]["PROPS"] = $props;
			break;
		case RES_PROPERTY:
			$restriction[0] = "RES_PROPERTY";
			$propTag = $restriction[1][ULPROPTAG];
			$propValue = $restriction[1][VALUE];
			$relOp = $restriction[1][RELOP];

			unset($restriction[1]);

			// relop flags
			$relOpFlags = "";
			if($relOp == RELOP_LT) {
				$relOpFlags = "RELOP_LT";
			} else if($relOp == RELOP_LE) {
				$relOpFlags = "RELOP_LE";
			} else if($relOp == RELOP_GT) {
				$relOpFlags = "RELOP_GT";
			} else if($relOp == RELOP_GE) {
				$relOpFlags = "RELOP_GE";
			} else if($relOp == RELOP_EQ) {
				$relOpFlags = "RELOP_EQ";
			} else if($relOp == RELOP_NE) {
				$relOpFlags = "RELOP_NE";
			} else if($relOp == RELOP_RE) {
				$relOpFlags = "RELOP_RE";
			}

			$restriction[1]["RELOP"] = $relOpFlags;
			$restriction[1]["ULPROPTAG"] = is_string($propTag) ? $propTag : Conversion::getPropertyName($propTag);
			$restriction[1]["VALUE"] = is_array($propValue) ? $propValue[$propTag] : $propValue;
			break;
		case RES_CONTENT:
			$restriction[0] = "RES_CONTENT";
			$propTag = $restriction[1][ULPROPTAG];
			$propValue = $restriction[1][VALUE];
			$fuzzyLevel = $restriction[1][FUZZYLEVEL];

			unset($restriction[1]);

			// fuzzy level flags
			$fuzzyLevelFlags = Conversion::fuzzylevelToString($fuzzyLevel);

			$restriction[1]["FUZZYLEVEL"] = $fuzzyLevelFlags;
			$restriction[1]["ULPROPTAG"] = is_string($propTag) ? $propTag : Conversion::getPropertyName($propTag);
			$restriction[1]["VALUE"] = is_array($propValue) ? $propValue[$propTag] : $propValue;
			break;
		case RES_COMPAREPROPS:
			$propTag1 = $restriction[1][ULPROPTAG1];
			$propTag2 = $restriction[1][ULPROPTAG2];

			unset($restriction[1]);

			$restriction[1]["ULPROPTAG1"] = is_string($propTag1) ? $proptag1 : Conversion::getPropertyName($proptag1);
			$restriction[1]["ULPROPTAG2"] = is_string($propTag2) ? $propTag2 : Conversion::getPropertyName($propTag2);
			break;
		case RES_BITMASK:
			$restriction[0] = "RES_BITMASK";
			$propTag = $restriction[1][ULPROPTAG];
			$maskType = $restriction[1][ULTYPE];
			$maskValue = $restriction[1][ULMASK];

			unset($restriction[1]);

			// relop flags
			$maskTypeFlags = "";
			if($maskType == BMR_EQZ) {
				$maskTypeFlags = "BMR_EQZ";
			} else if($maskType == BMR_NEZ) {
				$maskTypeFlags = "BMR_NEZ";
			}

			$restriction[1]["ULPROPTAG"] = is_string($propTag) ? $propTag : Conversion::getPropertyName($propTag);
			$restriction[1]["ULTYPE"] = $maskTypeFlags;
			$restriction[1]["ULMASK"] = $maskValue;
			break;
		case RES_SIZE:
			$restriction[0] = "RES_SIZE";
			$propTag = $restriction[1][ULPROPTAG];
			$propValue = $restriction[1][CB];
			$relOp = $restriction[1][RELOP];

			unset($restriction[1]);

			// relop flags
			$relOpFlags = "";
			if($relOp == RELOP_LT) {
				$relOpFlags = "RELOP_LT";
			} else if($relOp == RELOP_LE) {
				$relOpFlags = "RELOP_LE";
			} else if($relOp == RELOP_GT) {
				$relOpFlags = "RELOP_GT";
			} else if($relOp == RELOP_GE) {
				$relOpFlags = "RELOP_GE";
			} else if($relOp == RELOP_EQ) {
				$relOpFlags = "RELOP_EQ";
			} else if($relOp == RELOP_NE) {
				$relOpFlags = "RELOP_NE";
			} else if($relOp == RELOP_RE) {
				$relOpFlags = "RELOP_RE";
			}

			$restriction[1]["ULPROPTAG"] = is_string($propTag) ? $propTag : Conversion::getPropertyName($propTag);
			$restriction[1]["RELOP"] = $relOpFlags;
			$restriction[1]["CB"] = $propValue;
			break;
		case RES_EXIST:
			$propTag = $restriction[1][ULPROPTAG];

			unset($restriction[1]);

			$restriction[1]["ULPROPTAG"] = is_string($propTag) ? $propTag : Conversion::getPropertyName($propTag);
			break;
		case RES_SUBRESTRICTION:
			$propTag = $restriction[1][ULPROPTAG];
			$res = simplify_restriction($restriction[1][RESTRICTION]);

			unset($restriction[1]);

			$restriction[1]["ULPROPTAG"] = is_string($propTag) ? $propTag : Conversion::getPropertyName($propTag);
			$restriction[1]["RESTRICTION"] = $res;
			break;
	}

	return $restriction;
}
?>
