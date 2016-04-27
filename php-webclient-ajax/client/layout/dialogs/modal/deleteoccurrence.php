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
	return _("Confirm Delete");
}

function getJavaScript_onload(){ ?>
<?php } // getJavaScript_onload						

function getIncludes(){
	return array(
		"client/layout/css/occurrence.css"
	);
}

function getJavaScript_other(){ ?>
			function deleteAppointment(){
				var openOcc = dhtml.getElementById("occ");

				var entryid = dhtml.getElementById("entryid").value;
				var storeid = dhtml.getElementById("storeid").value;
				var basedate = dhtml.getElementById("basedate").value;
				var parententryid = dhtml.getElementById("parententryid").value;
				var meeting = dhtml.getElementById("meeting").value;
				var elementid = dhtml.getElementById("elementid").value;

				var uri = DIALOG_URL+"task=appointment_standard&storeid=" + storeid + "&entryid=" + entryid + "&parententryid=" + parententryid;
				var dialogname = window.name;
				if(!dialogname) {
					dialogname = window.dialogArguments.dialogName;
				}
				
				var parentModuleType = window.windowData.parentModuleType;
				if(parentModuleType && parentModuleType == "item"){
					//it handles cancelinvitation for an item module appointments
					var parentModule = window.windowData.parentModule;
					parentModule.deleteMessage(openOcc.checked ? basedate : false);
					window.resultCallBack();
				} else {
					//it handles cancleinvitation for list module appointments
					var parentModule = window.windowData.parentModule;
					parentModule.deleteAppointment(entryid, openOcc.checked ? basedate : false, elementid);
				}
			}

			function cancelDelete(){
				var parentModuleType = window.windowData.parentModuleType;
				if(parentModuleType && parentModuleType == "item"){
					// Oraganizer MR setup
					parentwindow.meetingRequestSetup(1);
				}
			}

<? } // javascript other
                                                
function getBody(){ ?>
		<div id="occurrence">
			<?=_("Do you want to delete all occurrences of this recurring appointment or just this one?");?>
			<p>
			<ul>
			<li><input id="occ" name="occurrence" class="fieldsize" type="radio" value="occurrence" checked><label for="occ"><?=_("Delete this occurrence");?></label></li>
			<li><input id="series" name="occurrence" class="fieldsize" type="radio" value="series"><label for="series"><?=_("Delete the series");?></label></li>
			<input id="entryid" type="hidden" name="entryid" value="<?=htmlentities(get("entryid", false, false, ID_REGEX))?>">
			<input id="parententryid" type="hidden" name="parententryid" value="<?=htmlentities(get("parententryid", false, false, ID_REGEX))?>">
			<input id="storeid"  type="hidden" name="storeid"  value="<?=htmlentities(get("storeid", false, false, ID_REGEX))?>">
			<input id="basedate" type="hidden" name="basedate" value="<?=htmlentities(get("basedate", false, false, ID_REGEX))?>">
			<input id="meeting" type="hidden" name="meeting" value="<?=htmlentities($_GET["meeting"])?>">
			<input id="elementid" type="hidden" name="elementid" value="<?=htmlentities(get("elementid", false, false, ID_REGEX))?>">
			</ul>

			<div class="confirmbuttons">
				<?=createButtons(array("title"=>_("Ok"),"handler"=>"deleteAppointment();window.close();"), array("title"=>_("Cancel"),"handler"=>"cancelDelete();window.close();"))?>
			</div>
		</div>
<?php } // getBody
?>
