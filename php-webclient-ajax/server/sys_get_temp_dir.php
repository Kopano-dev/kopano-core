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
// Backwards compatibility for the function sys_get_temp_dir which was
// introduced in PHP 5.2.1
if ( !function_exists('sys_get_temp_dir') ) {
	// Reference http://php.net/manual/en/function.sys-get-temp-dir.php
	// Based on http://www.phpit.net/
	// article/creating-zip-tar-archives-dynamically-php/2/
	function sys_get_temp_dir()
	{
		// Try to get from environment variable
		if ( !empty($_ENV['TMP']) ) {
			return realpath( $_ENV['TMP'] );
		} else if ( !empty($_ENV['TMPDIR']) ) {
			return realpath( $_ENV['TMPDIR'] );
		} else if ( !empty($_ENV['TEMP']) ) {
			return realpath( $_ENV['TEMP'] );
		} else {
			// Detect by creating a temporary file
			// Try to use system's temporary directory
			// as random name shouldn't exist
			$temp_file = tempnam( md5(uniqid(rand(), TRUE)), '' );
			if ( $temp_file ) {
				$temp_dir = realpath( dirname($temp_file) );
				unlink( $temp_file );
				return $temp_dir;
			} else {
				return FALSE;
			}
		}
	}
}
?>