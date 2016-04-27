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

function getDialogTitle() {
	return _("Open Shared Folders");
}

function getIncludes(){
	return array(
			"client/layout/css/sharedfolder.css",
			"client/layout/js/sharedfolder.js"
		);
}

function getJavaScript_onload(){ ?>
		initSharedFolder();
		dhtml.getElementById("username").focus();
<?php } // getJavaScript_onload

function getBody(){ ?>
		<div id="shared_folder">
			<table>
				<tr>
					<th><input type="button" value="<?=_("Name")?>..." onclick="openSelectUserDialog(<?=get("storeid","",true, ID_REGEX)?>);"></th>
					<td><input type="text" value="" id="username" onchange="userNameChange()"></td>
				</tr>
				<tr>
					<th><legend><?=_("Folder type")?></legend></th>
					<td><select id="foldertype"></select></td>
				</tr>
				<tr>
					<td colspan="2">
						<input type="checkbox" id="subfolders_checkbox"></input>
						<label id="subfolders_label" for="subfolders_checkbox">
							<?=_("Show subfolders")?>
						</label>
					</td>
				</tr>
			</table>
			<?=createConfirmButtons("submitSharedFolder()")?>
		</div>
<?php } // getBody
?>
