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

function initWindow()
{
	global $tabbar, $tabs;

	if(get("disable_permissions", "0", false, NUMERIC_REGEX) == "1") {
		// for public root folder and favorites folder, don't show permissions tab
		$tabs = array("general" => _("General"));
	} else {
		$tabs = array("general" => _("General"), "permissions" => _("Permissions"));
	}
	
	$tabbar = new TabBar($tabs, key($tabs));
}

function getModuleName(){
	return "propertiesmodule";
}

function getDialogTitle()
{
	return _("Properties");
}

function getIncludes(){
	return array("client/layout/css/tabbar.css",
			"client/layout/js/tabbar.js",
			"client/layout/js/date-picker.js",
			"client/layout/js/date-picker-language.js",
			"client/layout/js/date-picker-setup.js",
			"client/widgets/widget.js",
			"client/widgets/quota.js",
			"client/layout/js/properties.js",
			"client/layout/css/properties.css",
			"client/modules/".getModuleName().".js"
			);
}

function getJavaScript_onload()
{
	global $tabbar;
?>					var data = new Object();
					data["store"] = <?=get("storeid","false", "'", ID_REGEX)?>;
					data["entryid"] = <?=get("entryid","false", "'", ID_REGEX)?>;

<?	$tabbar->initJavascript("tabbar", "\t\t\t\t\t"); ?>
					module.init(moduleID, dhtml.getElementById("folder_properties"), false, data);
					module.getFolderProps(data, setFolderProperties);
					initFolderProperties(module);

<?
}					

function getBody(){
	global $tabbar, $tabs;
	
	$tabbar->createTabs();
	$tabbar->beginTab("general");
 ?>
<table id="folder_properties">
	<tbody>
		<tr>
			<td><div id="folder_icon"></div></td>
			<td id="display_name">&nbsp;</td>
		</tr>
		<tr>
			<td colspan="2"><hr></td>
		</tr>
		<tr>
			<td><?=_("Type")?>:</td>
			<td id="container_class">&nbsp;</td>
		</tr>
		<tr>
			<td><?=_("Location")?>:</td>
			<td id="parent_display_name">&nbsp;</td>
		</tr>
		<tr>
			<td><?=_("Description")?>:</td>
			<td><textarea cols="35" rows="2" id="comment"></textarea></td>
		</tr>
		<tr>
			<td colspan="2"><hr></td>
		</tr>
		<tr>
			<td><?=_("Items")?>:</td>
			<td id="content_count">&nbsp;</td>
		</tr>
		<tr>
			<td><?=_("Unread")?>:</td>
			<td id="content_unread">&nbsp;</td>
		</tr>
		<tr>
			<td><?=_("Size")?>:</td>
			<td id="message_size">&nbsp;<?=_("MB")?></td>
		</tr>
	</tbody>
</table>
<br/>
<input type="button" name="foldersize" value="<?=_('Folder Size')?>..." onclick="webclient.openModalDialog(module, 'foldersize', DIALOG_URL+'task=foldersize_modal&storeid=<?=get("storeid","", false, ID_REGEX)?>&entryid=<?=get("entryid","", false, ID_REGEX)?>', 350, 370);" />
<?php 
	$tabbar->endTab(); // general
	$tabbar->beginTab("permissions");
?>
<div id="permissions">
	<div class="level_list">
		<select id="userlist" size="5">
		</select>
	</div>
	<div class="action_buttons"> 
		<button id="add_user"><?=_("Add")?>...</button>
		<button id="del_user"><?=_("Remove")?></button>
	</div>
	
	<fieldset>
		<legend><?=_("Permissions")?></legend>
		<table>
			<tr>
				<td colspan="3">
					<label><?=_("Profile")?>:</label>
					<select id="profile">
					</select>
				</td>
			</tr>

			<tr>
				<td><input type="checkbox" id="ecRightsCreate" value="2"><label><?=_("Create items")?></label></td>
				<td><input type="checkbox" id="ecRightsFolderAccess" value="256"><label><?=_("Folder permissions")?></label></td>
			</tr>
			<tr>
				<td><input type="checkbox" id="ecRightsReadAny" value="1"><label><?=_("Read items")?></label></td>
				<td><input type="checkbox" id="ecRightsFolderVisible" value="1024"><label><?=_("Folder visible")?></label></td>
			</tr>
			<tr>
				<td><input type="checkbox" id="ecRightsCreateSubfolder" value="128"><label><?=_("Create subfolders")?></label></td>
				<td>&nbsp;</td>
			</tr>

			<tr>
				<td>
					<fieldset>
						<legend><?=_("Edit items")?></legend>
						<ul>
							<li><input type="radio" name="edit_items" id="ecRightsEditNone"><label><?=_("None")?></label></li>
							<li><input type="radio" name="edit_items" id="ecRightsEditOwned" value="8"><label><?=_("Own")?></label></li>
							<li><input type="radio" name="edit_items" id="ecRightsEditAny" value="40"><label><?=_("All")?></label></li>
						</ul>
					</fieldset>
				</td>
				<td>
					<fieldset>
						<legend><?=_("Delete items")?></legend>
						<ul>
							<li><input type="radio" name="del_items" id="ecRightsDeleteNone"><label><?=_("None")?></label></li>
							<li><input type="radio" name="del_items" id="ecRightsDeleteOwned" value="16"><label><?=_("Own")?></label></li>
							<li><input type="radio" name="del_items" id="ecRightsDeleteAny" value="80"><label><?=_("All")?></label></li>
						</ul>
					</fieldset>
				</td>
			</tr>
		</table>
		<input type="hidden" id="username">
	</fieldset>
</div>
<?
	$tabbar->endTab(); // permissions



	print (createConfirmButtons("submitProperties();"));
} // getBody
?>
