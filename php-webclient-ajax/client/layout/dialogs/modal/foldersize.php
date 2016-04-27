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
	return "foldersizemodule";
}

function getDialogTitle()
{
	return _("Folder Size");
}

function getIncludes(){
	return array("client/layout/css/tabbar.css",
			"client/widgets/widget.js",
			"client/widgets/tablewidget.js",
			"client/layout/js/foldersize.js",
			"client/modules/".getModuleName().".js"
			);
}

function getJavaScript_onload()
{
?>					var data = new Object();
					data["store"] = <?=get("storeid","false", "'", ID_REGEX)?>;
					data["entryid"] = <?=get("entryid","false", "'", ID_REGEX)?>;

					module.init(moduleID, dhtml.getElementById("folder_size"), false, data);
					initFolderSize(module);
					module.getFolderSize();

<?
}					

function getBody(){
?>
<table id="folder_size">
	<tbody>
		<tr>
			<td><?=_("Folder Name")?>:</td>
			<td id="folder_name">&nbsp;</td>
		</tr>
		<tr>
			<td><?=_("Size (without subfolders)")?>:</td>
			<td id="size_excl">&nbsp;</td>
		</tr>
		<tr>
			<td><?=_("Total size (including subfolders)")?>:</td>
			<td id="size_incl">&nbsp;</td>
		</tr>
	</tbody>
</table>
<hr />
<div id="foldersize_table"></div>
<hr />
<?php 

	print (createCloseButton("window.close();"));
} // getBody
?>
