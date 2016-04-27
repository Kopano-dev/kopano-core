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
	return _("Modify folder");
}


function getIncludes(){
	return array(
		);
}

function getJavaScript_onload(){ ?>
					var dialogname = window.name;

					if(!dialogname) {
						dialogname = window.dialogArguments.dialogName;
					}

					parentModule = windowData.parentModule;
					
					dhtml.getElementById("foldername").value = parentModule.getFolder(entryid).display_name;
<?php } // getJavaSctipt_onload	

function getJavaScript_other(){ ?>
			var entryid = "<?=get("entryid","none", false, ID_REGEX)?>";
			var parentModule;
			
			function submit()
			{
				var name = dhtml.getElementById("foldername").value;
				if(name.length > 0 && name != parentModule.getFolder(entryid).display_name) {
					parentModule.modifyFolder(name, entryid);
				}
				window.close();
			}
<?php }

function getBody(){
?>
		<dl id="modifyfolder">
			<dt><?=_("New foldername")?>:</dt>
			<dd><input id="foldername" type="text"></dd>
		</dl>

	<?=createConfirmButtons("submit()")?>

<?
}
?>
