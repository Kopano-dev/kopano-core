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

function getModuleName() {
	return 'readmailitemmodule';
}

function getModuleType() {
	return 'item';
}

function getDialogTitle() {
	return _("Read Mail");
}

function getIncludes(){
	return array(
			"client/modules/".getModuleName().".js",
			"client/layout/js/readmail.js"
		);
}

function getJavaScript_onload(){ ?>
					var data = new Object();
					module.init(moduleID);
					module.setData(<?=get("storeid","false","'", ID_REGEX)?>, <?=get("parententryid","false","'", ID_REGEX)?>);
					
					var attachNum = false;
					<? if(isset($_GET["attachNum"]) && is_array($_GET["attachNum"])) { ?>
						attachNum = new Array();
					
						<? foreach($_GET["attachNum"] as $attachNum) { 
							if(preg_match_all(NUMERIC_REGEX, $attachNum, $matches)) {
							?>
								attachNum.push(<?=intval($attachNum)?>);
						<?	}
						} ?>
					
					<? } ?>
					
					module.open(<?=get("entryid","false","'", ID_REGEX)?>, <?=get("rootentryid","false","'", ID_REGEX)?>, attachNum);
					
					/**
					 * Can use callback option of getMenuButtons(),
					 * but wanted to get moduleObject within this
					 * event function.
					 */
					var not_current = dhtml.getElementById("not_current");
					if (not_current){
						dhtml.addEvent(module, not_current, "click", eventPreviewItemNotCurrentClick);
					}

					resizeBody();
<?php } // getJavaScript_onload						

function getBody(){ ?>
		
		<div class="properties">
			<input id="entryid" type="hidden" value="">
			<input id="ismeetingrequest" type="hidden" value="">
			<input id="ismeetingcancel" type="hidden" value="">

			<!-- properties for flags -->
			<input id="flag_status" type="hidden" value="">
			<input id="flag_icon" type="hidden" value="">
			<input id="reminder_time" type="hidden" value="">
			<input id="reminder_set" type="hidden" value="">
			<input id="flag_request" type="hidden" value="">
			<input id="flag_due_by" type="hidden" value="">
			<input id="flag_complete_time" type="hidden" value="">
			<input id="reply_requested" type="hidden" value="">
			<input id="reply_time" type="hidden" value="">
			<input id="response_requested" type="hidden" value="">

			<div id="conflict"></div>
			<div id="extrainfo"></div>
			
			<table width="100%" cellpadding="2" cellspacing="0">
				<tr>
					<td class="propertybold propertywidth">
						<?=_("From")?>:
					</td>
					<td width="50%" nowrap>
						<div id="from"></div>
					</td>
					<td class="propertybold propertywidth">
						<?=_("Sent")?>:
					</td>
					<td nowrap>
						<div id="client_submit_time"></div>
					</td>
				</tr>
			</table>
			
			<table class="emailheader" width="100%" cellpadding="2" cellspacing="0">
				<tr>
					<td class="propertybold propertywidth" valign="top">
						<?=_("To")?>:
					</td>
					<td valign="top">
						<div id="to" class="recipient"></div>
					</td>
				</tr>
				<tr>
					<td class="propertybold propertywidth" valign="top">
						<?=_("CC")?>:
					</td>
					<td valign="top">
						<div id="cc" class="recipient"></div>
					</td>
				</tr>
				<tr>
					<td class="propertybold propertywidth" valign="top">
						<?=_("BCC")?>:
					</td>
					<td valign="top">
						<div id="bcc" class="recipient"></div>
					</td>
				</tr>
				<tr>
					<td class="propertybold propertywidth">
						<?=_("Subject")?>:
					</td>
					<td>
						<div id="subject"></div>
					</td>
				</tr>
			</table>
			
			<table width="100%" cellpadding="2" cellspacing="0">
				<tr>
					<td class="propertybold propertywidth" valign="top">
						<?=_("Attachments")?>:
					</td>
					<td valign="top">
						<div id="attachments"></div>
					</td>
				</tr>
			</table>
			
			<div id="meetingrequest">
				<table width="100%" cellpadding="2" cellspacing="0">
					<tr id="meetingrequest_startdate_row">
						<td class="propertybold propertywidth">
							<?=_("Start date")?>:
						</td>
						<td>
							<div id="startdate"></div>
						</td>
					</tr>
					<tr id="meetingrequest_duedate_row">
						<td class="propertybold propertywidth">
							<?=_("End date")?>:
						</td>
						<td>
							<div id="duedate"></div>
						</td>
					</tr>
					<tr id="meetingrequest_when_row">
						<td class="propertybold propertywidth">
							<?=_("When")?>:
						</td>
						<td>
							<div id="when"></div>
						</td>
					</tr>
					<tr id="meetingrequest_proposed_row">
						<td class="propertybold propertywidth">
							<?=_("Proposed")?>:
						</td>
						<td>
							<span id="proposed_start_whole"></span> - <span id="proposed_end_whole"></span>
						</td>
					</tr>
					<tr>
						<td class="propertybold propertywidth">
							<?=_("Location")?>:
						</td>
						<td>
							<div id="location"></div>
						</td>
					</tr>
				</table>
			</div>
		</div>

		<script type="text/javascript">
			// Adding a scroller element to the page for iPad users
			var scrollerStartHTML = '<div id="scroller" class="ipadscroller">';
			var scrollerEndHTML = '</div>';
			// The javascript in the src attribute is to suppress the security warning in IE when using SSL
			if (window.BROWSER_IE){
				document.write(scrollerStartHTML+"<iframe id='html_body' onload='linkifyDOM(this.contentDocument);' width='100%' height='150' frameborder='0' src=\"javascript:document.open();document.write('<html></html>');document.close();\"></iframe>"+scrollerEndHTML);
			}else{
				document.write(scrollerStartHTML+"<iframe id='html_body' onload='linkifyDOM(this.contentDocument);' width='100%' height='150' frameborder='0'></iframe>"+scrollerEndHTML);
			}
		</script>
<?php } // getBody

function getMenuButtons(){
	return array(
			array(
				'id'=>"accept_proposal",
				'name'=>_("Accept Proposal"),
				'title'=>_("Accept Proposal"),
				'callback'=>"function(){ openMeeting();}"
			),
			array(
				'id'=>"view_all_proposals",
				'name'=>_("View All Proposals"),
				'title'=>_("View All Proposals"),
				'callback'=>"function(){ openMeeting(true);}"
			),
			array(
				'id'=>"accept",
				'name'=>_("Accept"),
				'title'=>_("Accept"),
				'callback'=>"function(){webclient.openModalDialog(module, 'sendMRMailConfirmation', DIALOG_URL+'task=sendMRMailConfirmation_modal', 320, 280, sendMRMailConfirmationCallback, 'accept');}"
			),
			array(
				'id'=>"tentative",
				'name'=>_("Tentative"),
				'title'=>_("Tentative"),
				'callback'=>"function(){webclient.openModalDialog(module, 'sendMRMailConfirmation', DIALOG_URL+'task=sendMRMailConfirmation_modal', 320, 280, sendMRMailConfirmationCallback, 'tentative');}"
			),
			array(
				'id'=>"decline",
				'name'=>_("Decline"),
				'title'=>_("Decline"),
				'callback'=>"function(){webclient.openModalDialog(module, 'sendMRMailConfirmation', DIALOG_URL+'task=sendMRMailConfirmation_modal', 320, 280, sendMRMailConfirmationCallback, 'decline');}"
			),
			array(
				'id'=>"proposenewtime",
				'name'=>_("Propose New Time"),
				'title'=>_("Propose New Time"),
				'callback'=>"function(){proposeNewTime();}"
			),
			array(
				'id'=>"removefromcalendar",
				'name'=>_("Remove from Calendar"),
				'title'=>_("Remove from Calendar"),
				'callback'=>"function(){window.module.removeFromCalendar();window.close();}"
			),
			array(
				'id'=>"not_current",
				'name'=>_("Not Current"),
				'title'=>_("Not Current"),
				'callback'=>'false'
			),
			array(
				'id'=>"seperator",
				'name'=>"meetingrequest",
				'title'=>"",
				'callback'=>''
			),
			array(
				'id'=>"reply",
				'name'=>_("Reply"),
				'title'=>_("Reply"),
				'callback'=>"function(){respondToMail('reply');}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>''
			),
			array(
				'id'=>"replyall",
				'name'=>_("Reply All"),
				'title'=>_("Reply All"),
				'callback'=>"function(){respondToMail('replyall');}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>''
			),
			array(
				'id'=>"forward",
				'name'=>_("Forward"),
				'title'=>_("Forward"),
				'callback'=>"function(){respondToMail('forward');}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>''
			),
			array(
				'id'=>'options',
				'name'=>_("Options"),
				'title'=>_("Options"),
				'callback'=>'function(){webclient.openModalDialog(module, "options", DIALOG_URL+"task=messageoptions_modal&storeid=" + module.storeid + "&parententryid=" + module.parententryid + "&entryid=" + module.messageentryid, 460, 420);}'
			),
			array(
				'id'=>"print",
				'name'=>"",
				'title'=>_("Print"),
				'callback'=>"function() {openPrintItemDialog();}"
			),
			array(
				'id'=>"flag_status_red",
				'name'=>"",
				'title'=>_("Flag"),
				'callback'=>"function(){webclient.openModalDialog(module, 'flag', DIALOG_URL+'task=flag_modal', 350, 210);}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>''
			),
			array(
				'id'=>"delete",
				'name'=>"",
				'title'=>_("Delete"),
				'callback'=>'function(){delete_item();}'
			),
			/*array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>''
			),
			array(
				'id'=>"previous_item",
				'name'=>"",
				'title'=>_("Previous Item"),
				'callback'=>"function(){changeItem('previous');}"
			),
			array(
				'id'=>"next_item",
				'name'=>"",
				'title'=>_("Next Item"),
				'callback'=>"function(){changeItem('next')}"
			)*/
		);
}  // getMenuButtons
?>
