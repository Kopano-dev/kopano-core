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
function getModuleName(){
	return "restoreitemslistmodule";
}

function getModuleType(){
	return "list";
}

function getDialogTitle(){
	return _("Restore deleted Items");
}

function getIncludes(){
	return array(
			"client/layout/js/restoreitems.js",
			"client/widgets/tablewidget.js",
			"client/modules/".getModuleName().".js"
		);
}

function getJavaScript_onload(){ ?>
	
	// pass the last parameter [data] with has_no_menu = true 
	module.init(moduleID,null, null, {has_no_menu:true} );

	//sometimes the menu is there but in hidden mode, so use this method to show the main menu. [a workaround]
	webclient.menu.showMenu();
	
	var data = new Object();
	data["storeid"] = <?=get("storeid","false","'", ID_REGEX)?>;
	data["entryid"] = <?=get("parententryid","false","'", ID_REGEX)?>;
	module.setData(data);
	
	module.list();
	webclient.xmlrequest.sendRequest();
	
	//added event handler for radio buttons
	dhtml.addEvent(module, dhtml.getElementById("message"), "click", loadRecoverableData);
	dhtml.addEvent(module, dhtml.getElementById("folder"), "click", loadRecoverableData);
	
<?php } // getJavaSctipt_onload
function getJavaScript_other(){ ?>
	var tableWidget;
<?php }

function getBody(){ ?>
	<div class="restore_option_button ">
		<input  name="recover_options" type="radio" id="message" value="list" checked ><label for="message"><?=_("Message")?></label>
		<input  name="recover_options" type="radio" id="folder" value="folderlist"><label for="folder"><?=_("Folder")?></label>
	</div>
	<input id="entryid" type="hidden">
	<input id="parent_entryid" type="hidden">
	<div id="conflict"></div>
	<div id="restoreitemstable" class="restoreItemTable"></div>
	<div id="restoreitems_status"></div>
<?php }

function getMenuButtons(){
	$menuItems = array(
		array(
			'id'=>"restore_item",
			'name'=>_("Restore"),
			'title'=>_("Restore the selected items"),
			'callback'=>'restore_selected_items'
		),
		array(
			'id'=>"restoreall",
			'name'=>_("Restore All"),
			'title'=>_("Restore all"),
			'callback'=>'permanent_restore_all'
		)
	);

	// add delete buttons only when DISABLE_DELETE_IN_RESTORE_ITEMS config is set to false
	if(!DISABLE_DELETE_IN_RESTORE_ITEMS) {
		array_push($menuItems,
			array(
				'id'=>"purge",
				'name'=>_("Permanent Delete"),
				'title'=>_("Permanent delete selected items"),
				'callback'=>'permanent_delete_items'
			),
			array(
				'id'=>"deleteall",
				'name'=>_("Delete All"),
				'title'=>_("Permanent delete All"),
				'callback'=>'permanent_delete_all'
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			)
		);
	}

	array_push($menuItems,
		array(
			'id'=>"seperator",
			'name'=>"",
			'title'=>"",
			'callback'=>""
		),
		array(
			'id'=>"close",
			'name'=>_("Close"),
			'title'=>_("Close"),
			'callback'=>"function(){window.close();}"
		)
	);

	return $menuItems;
}
?>