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

function getDialogTitle(){
	return _("User group"); // use gettext here
}

function getModuleName() { 
	return "usergroupmodule"; 
}

function getIncludes() {
	$includes = array(
		"client/layout/js/usergroup.js",
		"client/modules/".getModuleName().".js"
	);
	return $includes;
}

function getJavaScript_onload(){ ?>
	module.init(moduleID);
	module.getGroups();

	var addButtonGroup = dhtml.getElementById("usergroup_dialog_usergroup_userlist_addgroup", "span");
	if(addButtonGroup){
		dhtml.addEvent(-1, addButtonGroup, "mouseover", eventMenuMouseOverTopMenuItem);
		dhtml.addEvent(-1, addButtonGroup, "mouseout", eventMenuMouseOutTopMenuItem);
	}

	var delButtonGroup = dhtml.getElementById("usergroup_dialog_usergroup_userlist_delgroup", "span");
	if(addButtonGroup){
		dhtml.addEvent(-1, delButtonGroup, "mouseover", eventMenuMouseOverTopMenuItem);
		dhtml.addEvent(-1, delButtonGroup, "mouseout", eventMenuMouseOutTopMenuItem);
	}

	var addButtonUser = dhtml.getElementById("usergroup_dialog_usergroup_userlist_adduser", "span");
	if(addButtonGroup){
		dhtml.addEvent(-1, addButtonUser, "mouseover", eventMenuMouseOverTopMenuItem);
		dhtml.addEvent(-1, addButtonUser, "mouseout", eventMenuMouseOutTopMenuItem);
	}

	var delButtonUser = dhtml.getElementById("usergroup_dialog_usergroup_userlist_deluser", "span");
	if(addButtonGroup){
		dhtml.addEvent(-1, delButtonUser, "mouseover", eventMenuMouseOverTopMenuItem);
		dhtml.addEvent(-1, delButtonUser, "mouseout", eventMenuMouseOutTopMenuItem);
	}
<?php } // getJavaScript_onload


function getBody() { ?>
	<div id="usergroup">
		<div class="usergroup_container">
			<div class="usergroupslist">
				<span class="usergroup_dialog_list_header"><?=_("Groups")?></span>
				<select id="usergroup_dialog_usergroup_list" multiple="multiple" size="5" class="usergroupslist"></select>
			</div>
			<div class="usergroup_options">
				<span class="menubutton icon icon_insert" id="usergroup_dialog_usergroup_userlist_addgroup" onclick="addGroup();" title="<?=_("Add Group")?>"></span>
				<span class="menubutton icon icon_remove" style="clear: both;" id="usergroup_dialog_usergroup_userlist_delgroup" onclick="removeGroup();" title="<?=_("Remove Group")?>"></span>
			</div>


			<div class="usergroup_content">
				<span class="usergroup_dialog_list_header"><?=_("Users")?></span>
				<select id="usergroup_dialog_usergroup_userlist" multiple="multiple" size="5" class="usergroupslist"></select>
			</div>
			<div class="usergroup_options">
				<span class="menubutton icon icon_insert" id="usergroup_dialog_usergroup_userlist_adduser" onclick="addUser();" title="<?=_("Add User")?>"></span>
				<span class="menubutton icon icon_remove" style="clear: both;" id="usergroup_dialog_usergroup_userlist_deluser" onclick="removeUser();" title="<?=_("Remove User")?>"></span>
			</div>

		</div>
		<div class="usergroup_footer_container">
			<?=createConfirmButtons("if(multiusercalendarSubmit()) window.close();")?>
		</div>
	</div>
<?php } // getBody
?>
