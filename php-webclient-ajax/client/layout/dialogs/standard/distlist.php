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
require("client/layout/tabbar.class.php");

function getModuleName(){
	return "distlistmodule";
}

function getModuleType(){
	return "list";
}

function getDialogTitle(){
	return _("Distribution List");
}

function getIncludes(){
	return array(
			"client/layout/css/tabbar.css",
			"client/layout/css/distlist.css",
			"client/layout/js/tabbar.js",
			"client/layout/js/distlist.js",
			"client/modules/itemmodule.js",
			"client/modules/".getModuleName().".js"
		);
}

function initWindow(){
	global $tabbar, $tabs;

	$tabs = array("members" => _("Members"), "notes" => _("Notes"));
	$tabbar = new TabBar($tabs, key($tabs));
}

function getJavaScript_onload(){
	global $tabbar;
	
	$tabbar->initJavascript("tabbar", "\t\t\t\t\t"); 
?>
					var data = new Object;
					data.storeid = "<?=get("storeid", "", false, ID_REGEX)?>";
					data.parententryid = "<?=get("parententryid", "", false, ID_REGEX)?>";
					data.entryid = "<?=get("entryid", "", false, ID_REGEX)?>";
					data.has_no_menu = true; // hack
					module.init(moduleID, dhtml.getElementById("tableview"), false, data);
					module.setData(data);
					module.list();
				
					resizeBody();
					module.resize();
					
					dhtml.addEvent(false, dhtml.getElementById("fileas"), "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(false, dhtml.getElementById("html_body"), "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(module, dhtml.getElementById("categories"), "blur", eventFilterCategories);

					// Set focus on fileas field.
					dhtml.getElementById("fileas").focus();
<?php } // getJavaSctipt_onload						

function getJavaScript_onresize(){ ?>
					module.resize();
<?php } // getJavaScript_onresize	

function getBody() {
	global $tabbar, $tabs;
	
	$tabbar->createTabs();
	$tabbar->beginTab("members");
?>
	<input id="entryid" type="hidden">
	<input id="parent_entryid" type="hidden">
	<input id="message_class" type="hidden" value="IPM.DistList">
	<input id="icon_index" type="hidden" value="514">
	<input id="display_name" type="hidden">
	<input id="dl_name" type="hidden">
	<input id="sensitivity" type="hidden" value="0">
	<input id="private" type="hidden" value="-1">
	<input id="subject" type="hidden">
	
	<table border="0">
		<tr>
			<td><label for="fileas"><?=_("Name")?>:</label></td>
			<td id="fileas_container"><input type="text" id="fileas"></td>
		</tr>
	</table>
	
	<div id="distlist_actions">
		<button onclick="webclient.openModalDialog(module, 'addressbook', DIALOG_URL+'task=addressbook_modal&storeid='+module.storeid+'&type=all', 800, 500, distlist_addABCallback);"><?=_("Select Members")?>...</button>
		<button onclick="webclient.openModalDialog(-1, 'addemail', DIALOG_URL+'task=emailaddress_modal', 300,150, distlist_addNewCallback);"><?=_("Add new")?>...</button>
		<button onclick="module.removeItems();"><?=_("Remove")?>...</button>
	</div>
	
	<div id="tableview"></div>


		<div id="categoriesbar">
			<table width="100%" border="0" cellpadding="2" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<input class="button" type="button" value="<?=_("Categories")?>:" onclick="webclient.openModalDialog(module, 'categories', DIALOG_URL+'task=categories_modal', 350, 370, distlist_categoriesCallback);">
					</td>
					<td>
						<input id="categories" class="field" type="text">
					</td>
					<td width="20" nowrap>
						<label for="checkbox_private"><?=_("Private")?></label>
					</td>
					<td width="10">
						<input id="checkbox_private" type="checkbox">
					</td>
				</tr>
			</table>
		</div>

<?php 
	$tabbar->endTab();
	
	$tabbar->beginTab("notes");
?>
	<textarea id="html_body"></textarea>
<?php
	$tabbar->endTab();
} // getBody

function getMenuButtons(){
	return array(
			array(
				'id'=>"save",
				'name'=>_("Save"),
				'title'=>_("Save"),
				'callback'=>'saveDistList'
			),
		);
}

?>
