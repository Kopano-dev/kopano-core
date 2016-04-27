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
function getModuleName() {
	return "printlistmodule";
}

function getModuleType(){
	return "list";
}

function getDialogTitle() {
	return _("Print preview");
}

function getIncludes(){
	return array(
			"client/layout/css/calendar-print.css",
			"client/modules/".getModuleName().".js",
			"client/views/calendar.day.view.js",
			"client/views/calendar.datepicker.view.js",
			"client/views/print.view.js",
			"client/views/print.calendar.listview.js",
			"client/views/print.calendar.dayview.js",
			"client/views/print.calendar.weekview.js",
			"client/views/print.calendar.monthview.js"
		);
}

function getJavaScript_onload(){ ?>
					var data = new Object();
					data["store"] = <?=get("storeid", "false",  "'", ID_REGEX)?>;
					data["entryid"] = <?=get("entryid", "false", "'", ID_REGEX)?>;
					data["view"] = windowData["view"];
					// "has_no_menu" is passed to create menu,
					// otherwise listmodule will not create the menu
					data["has_no_menu"] = true;
					data["moduleID"] = moduleID;
					data["restriction"] = new Object();
					data["restriction"]["startdate"] = windowData["startdate"];
					data["restriction"]["duedate"] = windowData["duedate"];
					data["restriction"]["selecteddate"] = windowData["selecteddate"];

					module.init(moduleID, dhtml.getElementById("print_calendar"), false, data);
					webclient.menu.showMenu();
<?php } // getJavaScript_onload						

function getJavaScript_onresize() {?>
	module.resize();
<?php } // onresize

function getBody(){ ?>
	<div id="print_header"></div>
	<div id="print_calendar"></div>
	<div id="print_footer"></div>
<?php } // getBody

function getMenuButtons(){
	return array(
			"close"=>array(
				'id'=>"close",
				'name'=>_("Close"),
				'title'=>_("Close preview"),
				'callback'=>'function(){window.close();}'
			),
			"print"=>array(
				'id'=>'print',
				'name'=>_("Print"),
				'title'=>_("Print"),
				'callback'=>'function(){window.module.printIFrame();}'
			),
		);
}
?>