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
	return isset($_GET["title"]) ? htmlentities(get("title", false, false, STRING_REGEX)) : _("Select folder");
}


function getIncludes(){
	return array(
			"client/widgets/tree.js",
			"client/layout/css/tree.css",
			"client/modules/hierarchymodule.js",
			"client/modules/".getModuleName().".js",
			"client/layout/js/selectfolder.js"
		);
}

function getModuleName(){
	return "hierarchyselectmodule";
}

function getJavaScript_onload(){ ?>
					var elem = dhtml.getElementById("targetfolder");

					// check that this dialog should implement multiple selection or not
					var multipleSelection = "<?=get("multipleSelection", false, false, STRING_REGEX)?>";
					var validatorType = "<?=get("validatorType", false, false, STRING_REGEX)?>";

					module.init(moduleID, elem, false, multipleSelection, validatorType);
					module.list();

					var dialogname = window.name;
					if(!dialogname) {
						dialogname = window.dialogArguments.dialogName;
					}

					var subtitle = dhtml.getElementById("subtitle");
					dhtml.deleteAllChildren(subtitle);
					subtitle.appendChild(document.createTextNode("<?=get("subtitle", "", false, STRING_REGEX)?>" ));

					var entryid = "<?=get("entryid", "none", false, ID_REGEX)?>";
					if(entryid == '' ||  entryid == 'none' || entryid == 'undefined'){
						entryid = false;
					}
					if(!entryid && typeof window.windowData != "undefined") {
						entryid = window.windowData.entryid;
					}

					if(module.multipleSelection && module.validatorType == "search") {
						var subfoldersElement = dhtml.getElementById("subfolders");
						subfoldersElement.style.display = "block";
						subfoldersElement.style.visibility = "visible";

						if(typeof window.windowData != "undefined" && typeof window.windowData["subfolders"] != "undefined") {
							// check subfolders option
							var subFoldersCheckboxElem = dhtml.getElementById("subfolders_checkbox");
							if(subFoldersCheckboxElem != null) {
								subFoldersCheckboxElem.checked = Boolean(window.windowData["subfolders"]);
							}
						}
					}

					setTimeout(function(){
						if(module.multipleSelection) {
							module.selectFolderCheckbox(entryid);
						} else {
							module.selectFolder(entryid);
						}
					}, 1000);

					
<?php } // getJavaSctipt_onload	

function getJavaScript_other(){ ?>
<?php }

function getBody(){
?>
		<dl id="copymovemessages" class="folderdialog">
			<dt><?=_("Please select a folder")?>:</dt>
			<dd id="targetfolder" class="dialog_hierarchy" onmousedown="return false;"></dd>

			<dd><span id="sourcemessages"></span></dd>
		</dl>
		<div id="subfolders">
			<input type="checkbox" id="subfolders_checkbox" disabled>
			<label id="subfolders_label" class="disabled_text" for="subfolders_checkbox"><?=_("Search subfolders")?></label>
		</div>

		<?=createConfirmButtons("if(submitFolder()) window.close; else window.focus();")?>
<?
}
?>
