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
<?

/* This is a simple wrapper to server static content (like .js and .png) to the client
 * so that we can supply good cache headers, without having to change Apache config settings
 * etc.
 */
 
include_once("config.php"); 

if(!isset($_GET["p"])) {
	print "Invalid input";
	return;
}

$files = $_GET["p"];
if (!is_array($files)){
	$files = array($files);
}

$fileext = false;

foreach($files as $fn){

	// Check if filename is allowed (allow  ( [alnum] "/" )* "/" ([ alnum | "_" ] ".") + [alpha ] )
	if((strpos($fn, 'client/') !== 0 && strpos($fn, PATH_PLUGIN_DIR) !== 0) || !preg_match('/^([0-9A-Za-z\-]+\\/)*(([0-9A-Za-z_\-])+\.)+([A-Za-z\-])+$/', $fn)){
		print "Invalid path";
		trigger_error("Invalid path", E_USER_NOTICE);
		return;
	}

	// Find extension 
	$pos = strrchr($fn, ".");

	if(!isset($pos)) {
		print "Invalid type";
		trigger_error("Invalid type", E_USER_NOTICE);
		return;
	}

	$ext = substr($pos, 1);

	switch($ext) {
		case "js": $type = "text/javascript; charset=utf-8"; break;
		case "png": $type = "image/png"; break;
		case "jpg": $type = "image/jpeg"; break;
		case "gif": $type = "image/gif"; break;
		case "css": $type = "text/css; charset=utf-8"; break;
		case "html": $type = "text/html; charset=utf-8"; break;
		// This is important: do not server any other files! (e.g. php files)
		default: 
			print "Invalid extension \"".$ext."\"";
			trigger_error("Invalid extension \"".$ext."\"", E_USER_NOTICE);
			return;
	}

	if (!$fileext) {
		$fileext = $ext;
	}else if ($ext!=$fileext){
		print "Can't combine files";
		trigger_error("Can't combine files", E_USER_NOTICE);
		return;
	}else if($fileext!="js" && $fileext!="css"){
		print "Can only combine javascript or css files";
		trigger_error("Can only combine javascript or css files", E_USER_NOTICE);
		return;
	}

	if (!is_file($fn) || !is_readable($fn)){
		print "File \"".$fn."\" not found";
		trigger_error("File \"".$fn."\" not found", E_USER_NOTICE);
		return;
	}
}

// Output the file
header("Content-Type: ".$type);


if (defined("DEBUG_LOADER") && !DEBUG_LOADER){
	// Disable caching in debug mode
	header('Expires: -1');
	header('Cache-Control: no-cache');
}else{

	$etag = md5($_SERVER["QUERY_STRING"]);
	header("ETag: \"".$etag."\"");

	header('Expires: '.gmdate('D, d M Y H:i:s',time() + EXPIRES_TIME).' GMT');
	header('Cache-Control: max-age=' . EXPIRES_TIME.',must-revalidate');

	// check if the requested file is modified
	if (function_exists("getallheaders")){ // only with php as apache module
		$headers = getallheaders();

		if (isset($headers["If-None-Match"]) && strpos($headers['If-None-Match'], $etag)!==false){
			// ETag found, file has not changed
			header('HTTP/1.1 304 Not Modified');
			exit;
		}
	}
}


// compress output
if(!defined("DEBUG_GZIP")||DEBUG_GZIP){
	ob_start("ob_gzhandler");
}

foreach($files as $fn){

	if ($fileext!="css"){
		readfile($fn);
	}else{
		// rewrite css files
		$dir = substr($fn, 0, strrpos($fn, "/"));

		$fh = fopen($fn, "r");
		while(!feof($fh)){
			$line = fgets($fh, 4096);
			echo preg_replace("/url\(/i", "url(".$dir."/", $line);
		}
	}
	echo "\n";
}
