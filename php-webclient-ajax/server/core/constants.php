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
	 * This file contains only some constants used anyware in the WebApp
	 */

	// These are the events, which a module can register for.
	define("OBJECT_SAVE", 				0);
	define("OBJECT_DELETE", 			1);
	define("TABLE_SAVE",				2);
	define("TABLE_DELETE",				3);
	define("REQUEST_START",				4);
	define("REQUEST_END",				5);
	define("DUMMY_EVENT",				6);

	// dummy entryid, used for the REQUEST events
	define("REQUEST_ENTRYID",			"dummy_value");
	
	// used in operations->getHierarchyList
	define("HIERARCHY_GET_ALL",			0);
	define("HIERARCHY_GET_DEFAULT",		1);
	define("HIERARCHY_GET_SPECIFIC",	2);

	// check to see if the HTML editor is installled
	if (is_dir(FCKEDITOR_PATH.'/editor') && is_file(FCKEDITOR_PATH.'/editor/fckeditor.html')){
		define('FCKEDITOR_INSTALLED',true);
	}else{
		define('FCKEDITOR_INSTALLED',false);
	}
    
	// used with fields/columns to specify a autosize column
	define("PERCENTAGE", "percentage");


	// used by distribution lists
	define("DL_GUID",    pack("H*", "C091ADD3519DCF11A4A900AA0047FAA4"));
	define("DL_USER",    0xC3);		//	195
	define("DL_USER2",   0xD3);		//	211
	define("DL_USER3",   0xE3);		//	227
	define("DL_DIST",    0xB4);		//	180
	define("DL_USER_AB", 0xB5);		//	181
	define("DL_DIST_AB", 0xB6);		//	182

				

?>
