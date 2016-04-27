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
	return _("Copy/Move messages");
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
					var elem = dhtml.getElementById("targetfolder");
					module.init(moduleID, elem);
					module.list();
					var dialogname = window.name;

					if(!dialogname) {
						dialogname = window.dialogArguments.dialogName;
					}

					parentModule = windowData.parentModule;
					
					// This will set inbox folder as default selected folder
					module.selectFolder("<?=get("parent_entryid", "none", false, ID_REGEX)?>");

					var text;
					if (parentModule.selectedMessages.length>1){
						text = "<?=_("There are %s messages selected")?>";
					} else {
						text = "<?=_("There is one message selected")?>";
					}	
					text += ".";
					text = text.sprintf(parentModule.selectedMessages.length);
					var subtitle = dhtml.getElementById("subtitle");
					dhtml.deleteAllChildren(subtitle);
					subtitle.appendChild(document.createTextNode(text));
					
<?php } // getJavaSctipt_onload	

function getJavaScript_other(){ ?>
			var parent_entryid = "<?=get("parent_entryid","none", false, ID_REGEX)?>";
			var parentModule;
			
			function submit(type)
			{
				if (!type) type = "copy";

				var target_entryid = module.selectedFolder;
				var target_store = module.selectedFolderStoreId;
				
				if(!target_entryid){
					alert("<?=_("Please select the destination folder to copy or move the selected message(s) to.")?>");
				}else if(compareEntryIds(target_entryid, module.defaultstore.root.entryid)){
					// this check whether the target folder is a root folder or not
					alert("<?=_("Cannot move the items. The destination folder cannot contain messages/forms.")?>");
				}else{
					parentModule.copyMessages(target_store, target_entryid, type, false);
				}
				
				window.close();
			}
<?php }

function getBody(){
?>
		<dl id="copymovemessages" class="folderdialog">
			<dt><?=_("Copy/move selected messages to")?>:</dt>
			<dd id="targetfolder" class="dialog_hierarchy" onmousedown="return false;"></dd>

			<dd><span id="sourcemessages"></span></dd>
		</dl>

	<?=createButtons(array("title"=>_("Copy"),"handler"=>"submit('copy');"), array("title"=>_("Move"),"handler"=>"submit('move');"), array("title"=>_("Cancel"),"handler"=>"window.close();"))?>

<?
}
?>
