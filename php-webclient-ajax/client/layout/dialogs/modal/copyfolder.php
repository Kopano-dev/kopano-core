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
	return _("Copy/Move folder");
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
					
					dhtml.getElementById("sourcefolder").appendChild(document.createTextNode(parentModule.getFolder(source_entryid).display_name));
					
<?php } // getJavaSctipt_onload	

function getJavaScript_other(){ ?>
			var source_entryid = "<?=get("source_entryid","none", false, ID_REGEX)?>";
			var parent_entryid;
			var parentModule;
			
			function submit(type)
			{
				if (!type) type = "copy";
				
				var target_entryid = module.selectedFolder;
				var target_storeid = false;
				if(dhtml.getElementById(module.selectedFolder)){
					target_storeid = dhtml.getElementById(module.selectedFolder).storeid;
				}

				parentModule.copyFolder(target_entryid, target_storeid, type, source_entryid);
				
				window.close();
			}
<?php }

function getBody(){
?>
		<dl id="copymovefolder" class="folderdialog">
			<dt><?=_("Folder")?>:</dt>
			<dd><span id="sourcefolder"></span></dd>
			
			<dt><?=_("Destination folder")?>:</dt>
			<dd id="targetfolder" class="dialog_hierarchy" onmousedown="return false;"></dd>

		
		
		</dl>

	<?=createButtons(array("title"=>_("Copy"),"handler"=>"submit('copy');"), array("title"=>_("Move"),"handler"=>"submit('move');"), array("title"=>_("Cancel"),"handler"=>"window.close();"))?>

<?
}
?>
