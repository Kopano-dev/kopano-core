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
	return _("Send Meeting Request Mail Confirmation");
}

function getIncludes(){
	return array(
			"client/layout/css/sendMRMailConfirmation.css",
			"client/layout/css/occurrence.css"
	);
}

function getJavaScript_onload(){ ?>
	var windowData = typeof window.windowData != "undefined" ? window.windowData : false;
	var sendMRMailConfirmationEle = dhtml.getElementById("sendMRMailConfirmation");
	var confirmDeleteEle = dhtml.getElementById("confirmDelete");

	if (windowData) {
		if (windowData.confirmDelete) {
			sendMRMailConfirmationEle.style.display = 'none';
			confirmDeleteEle.style.display = 'block';

			var delete_info_bar = dhtml.getElementById("delete_info_bar");
			var subjectString = (window.windowData["subject"].length > 30) ? window.windowData["subject"].substr(0, 30)+"..." : window.windowData["subject"];
			delete_info_bar.innerHTML = _("The meeting \"%s\" was already accepted").sprintf(subjectString).htmlEntities();
		} else {
			confirmDeleteEle.style.display = 'none';
		}
	}
<?php }


function getJavaScript_other(){ ?>
	function editResponse(){
		var body = dhtml.getElementById("body");
		if(dhtml.getElementById('editResponse').checked){
			body.removeAttribute("disabled");
		}else{
			body.setAttribute("disabled","disabled");
		}
	}

	function sendMRMailConfirmation(){
		if(dhtml.getElementById('editResponse').checked){
			var response = new Object;
			response.body = dhtml.getElementById('body').value;
			response.type = false;
			window.resultCallBack(response, window.callBackData);
		}else{
			window.resultCallBack(dhtml.getElementById('noResponse').checked, window.callBackData);
		}
	}

	function sendConfirmDelete() {
		window.resultCallBack(window.callBackData, dhtml.getElementById('sendResponseAsDelete').checked, window.windowData["basedate"] ? window.windowData["basedate"] : false, window.windowData);
	}
<?}

function getBody(){ ?>
		<div id="sendMRMailConfirmation">
			<div>
				<input type="radio" name="action" id="editResponse" onclick ="editResponse()" />
				<label for="editResponse"><?=_("Edit a response before Sending")?></label>
				<div id="responseBody">
					<label for="body"><?=_("Type Response Message").":"?></label><br/>
					<textarea cols=20 rows=5 name="body" id="body" disabled></textarea>
				</div>
			</div>
			<div>
				<input type="radio" name="action" id="sendResponse" onclick ="editResponse()" checked/>
				<label for="sendResponse"><?=_("Send a response")?></label>
			</div>
			<div>
				<input type="radio" name="action" id="noResponse" onclick ="editResponse()"/>
				<label for="noResponse"><?=_("Don't send a response")?></label>
			</div>
			<?=createConfirmButtons("sendMRMailConfirmation(); window.close();")?>
		</div>

		<div id="confirmDelete">
			<label id="delete_info_bar"></label>
			<p>
			<ul>
				<li><input type="radio" name="delete" id="sendResponseAsDelete" checked/><label for="sendResponseAsDelete"><?=_("Delete and send a response to the meeting organizer")."."?></label></li>
				<li><input type="radio" name="delete" id="deletenoResponse"/><label for="deletenoResponse"><?=_("Delete without sending a response")."."?></label></li>
			</ul>
			<?=createConfirmButtons("sendConfirmDelete(); window.close();")?>
		</div>
<?php } // getBody
?>
