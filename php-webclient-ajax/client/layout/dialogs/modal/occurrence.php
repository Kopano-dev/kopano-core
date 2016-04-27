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
	return _("Recurring appointment");
}

function getJavaScript_onload(){ ?>
<?php } // getJavaScript_onload						

function getIncludes(){
	return array(
		"client/layout/css/occurrence.css"
	);
}

function getJavaScript_other(){ ?>
        function openAppointment(){
                var openOcc = dhtml.getElementById("occ");

				var entryid = dhtml.getElementById("entryid").value;
				var storeid = dhtml.getElementById("storeid").value;
				var basedate = dhtml.getElementById("basedate").value;
				var parententryid = dhtml.getElementById("parententryid").value;

				var uri = DIALOG_URL+"task=appointment_standard&storeid=" + storeid + "&entryid=" + entryid + "&parententryid=" + parententryid;

                if (openOcc.checked) {
                	// Open the occurrence
                	uri += "&basedate=" + basedate;
			        parentWebclient.openWindow(this, "appointment", uri);				
				} else {
					// Open the series
			        parentWebclient.openWindow(this, "appointment", uri);				
				}
        }
<? } // javascript other

                                                
function getBody(){ ?>
		<div id="occurrence">
			<?=_("This is a recurring appointment. Do you want to open only this occurrence or the series?");?>
			<p>
			<ul>
			<li><input id="occ" name="occurrence" class="fieldsize" type="radio" value="occurrence" checked><label for="occ"><?=_("Open this occurrence");?></label></li>
			<li><input id="series" name="occurrence" class="fieldsize" type="radio" value="series"><label for="series"><?=_("Open the series");?></label></li>
			<input id="entryid" type="hidden" name="entryid" value="<?=htmlentities(get("entryid", false, false, ID_REGEX))?>">
			<input id="parententryid" type="hidden" name="parententryid" value="<?=htmlentities(get("parententryid", false, false, ID_REGEX))?>">
			<input id="storeid"  type="hidden" name="storeid"  value="<?=htmlentities(get("storeid", false, false, ID_REGEX))?>">
			<input id="basedate" type="hidden" name="basedate" value="<?=htmlentities(get("basedate", false, false, ID_REGEX))?>">
			</ul>
			
			<?=createConfirmButtons("openAppointment(); window.close();")?>
		</div>
<?php } // getBody
?>
