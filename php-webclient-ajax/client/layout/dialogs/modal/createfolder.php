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
	return _("New folder");
}


function getIncludes(){
	return array(
			"client/widgets/tree.js",
			"client/layout/css/tree.css",
			"client/modules/hierarchymodule.js",
			"client/modules/".getModuleName().".js"
		);
}

function getModuleName(){
	return "hierarchyselectmodule";
}

function getJavaScript_onload(){ ?>
					//set focus on foldername field for create folder dialog
					dhtml.getElementById("newfolder").focus();

					var elem = dhtml.getElementById("parentfolder");
					module.init(moduleID, elem, true);
					module.list();
					var dialogname = window.name;

					if(!dialogname) {
						dialogname = window.dialogArguments.dialogName;
					}

					parentModule = windowData.parentModule;

					module.selectFolder("<?=get("parent_entryid", "none", false, ID_REGEX)?>");

					if (parentWebclient && parentWebclient.hierarchy && module.selectedFolder){
						var parentFolder = parentWebclient.hierarchy.getFolder(module.selectedFolder);
						if (parentFolder && parentFolder.container_class){
							dhtml.getElementById("newfoldertype").value = parentFolder.container_class;
						}
					}

<?php } // getJavaSctipt_onload	

function getJavaScript_other(){ ?>
			var parent_entryid = "<?=get("parent_entryid","none", false, ID_REGEX)?>";
			var parentModule;
			
			function submit()
			{
				var name = dhtml.getElementById("newfolder").value;

				if(name.length > 0) {
					var type = dhtml.getElementById("newfoldertype").options[dhtml.getElementById("newfoldertype").selectedIndex].value;
					parent_entryid = module.selectedFolder;
					
					parentModule.createFolder(name, type, parent_entryid);	
				}
				window.close();
			}
<?php }

function getBody(){
?>
		<dl id="createfolder" class="folderdialog">
			<dt><?=_("New foldername")?>:</dt>
			<dd><input id="newfolder" type="text"></dd>
			
			<dt><?=_("Folder contains")?>:</dt>
			<dd>
				<select id="newfoldertype">
					<option value="IPF.Note" selected><?=_("E-Mail")?></option>
					<option value="IPF.Contact"><?=_("Contact")?></option>
					<option value="IPF.Task"><?=_("Task")?></option>
					<option value="IPF.Appointment"><?=_("Appointment")?></option>
					<option value="IPF.StickyNote"><?=_("Note")?></option>
				</select>
			</dd>
			<dt><?=_("Select where to place the new folder")?>:</dt>
			<dd id="parentfolder" class="dialog_hierarchy" onmousedown="return false;"></dd>

		
		
		</dl>

	<?=createConfirmButtons("submit()")?>

<?
}
?>
