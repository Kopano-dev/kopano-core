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
	return _("Add to favorite folder");
}


function getIncludes(){
	return array(
			"client/layout/css/addtofavorite.css",
			"client/modules/hierarchymodule.js",
			"client/modules/".getModuleName().".js"
		);
}

function getModuleName(){
		return "hierarchyselectmodule";
}

function getJavaScript_onload(){ ?>
	var dialogname = window.name;

	if(!dialogname) {
		dialogname = window.dialogArguments.dialogName;
	}

	parentModule = windowData.parentModule;
	dhtml.getElementById("foldername").value="<?=get("foldername", "none", false)?>"
	
<?php } // getJavaSctipt_onload

function getJavaScript_other(){ ?>
	var folder_entryid = "<?=get("entryid","none", false, ID_REGEX)?>";
	var parentModule;

	function changeSubfolders() {
		if(dhtml.getElementById("addsubfolders").checked == true)
		{
			dhtml.getElementById("addfoldertype_one").disabled=false;
			dhtml.getElementById("addfoldertype_sub").disabled=false;
		} else {
			dhtml.getElementById("addfoldertype_one").disabled=true;
			dhtml.getElementById("addfoldertype_sub").disabled=true;
		}
	}
	
	function submitAddToFavorite() {
		var favoritename = dhtml.getElementById("favoritename").value.trim();
		var flags = 0;
		
		if (dhtml.getElementById("addsubfolders").checked == true){
			if (dhtml.getElementById("addfoldertype_one").checked == true){
				flags = dhtml.getElementById("addfoldertype_one").value;
			}else{
				flags = dhtml.getElementById("addfoldertype_sub").value;
			}
		}
		
		parentModule.addToFavorite(folder_entryid, favoritename, flags);
		
		window.close();
	}
<?php }

function getBody(){
?>
		<div>
			<table>
				<tr>
					<td><?=_("Public folder name")?>:</td>
					<td><input type="text" value="" id="foldername" disabled></td>
				</tr>
				<tr>
					<td><?=_("Favorite folder name")?>:</td>
					<td><input type="text" value="" id="favoritename"></td>
				</tr>
				
				<tr>
					<td colspan="2">
						<fieldset>
							<input type="checkbox" id="addsubfolders" onclick="changeSubfolders()"><label for="addsubfolders"><?=_("Add subfolders of this folder")?></label>
							<ul>
								<li><input type="radio" name="addfoldertype" id="addfoldertype_one" value="1" disabled checked><label for="addfoldertype_one"><?=_("Add immediate subfolders only")?></label></li>
								<li><input type="radio" name="addfoldertype" id="addfoldertype_sub" value="2" disabled><label for="addfoldertype_sub"><?=_("Add all subfolders")?></label></li>
							</ul>
						</fieldset>
					</td>
				</tr>
				
				
			</table>
			<?=createButtons(array("title"=>_("Add"),"handler"=>"submitAddToFavorite();"), array("title"=>_("Cancel"),"handler"=>"window.close();"))?>
		</div>
<?
}
?>
