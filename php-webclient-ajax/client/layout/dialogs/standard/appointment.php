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

require("client/layout/tabbar.class.php");

function initWindow(){
	global $tabbar, $tabs;
	if(get("entryid","false","'", ID_REGEX) != "false"){
		$tabs = array("appointment" => _("Appointment"), "scheduling" => _("Scheduling"), "tracking" => _("Tracking"));//@TODO
	}else{
		$tabs = array("appointment" => _("Appointment"), "scheduling" => _("Scheduling"));
	}
	$tabbar = new TabBar($tabs, key($tabs));
}

function getModuleName(){
	return "appointmentitemmodule";
}

function getModuleType(){
	return "item";
}

function getDialogTitle(){
	return _("Appointment");
}

function getIncludes(){
	return array(
			"client/layout/css/appointment.css",
			"client/layout/css/calendar.css",
			"client/layout/css/tabbar.css",
			"client/layout/js/tabbar.js",
			"client/layout/js/appointment.js",
			"client/layout/js/date-picker.js",
			"client/layout/js/date-picker-language.js",
			"client/layout/js/date-picker-setup.js",
			"client/layout/css/date-picker.css",
			"client/modules/".getModuleName().".js",
			"client/widgets/suggestionlist.js",
			"client/modules/suggestemailaddressmodule.js",
			"client/layout/css/suggestionlayer.css",
			"client/layout/css/freebusymodule.css",
			"client/modules/freebusymodule.js",
			"client/widgets/datetimepicker.js",
			"client/widgets/combineddatetimepicker.js",
			"client/widgets/dragdrop.js",
			"client/widgets/datepicker.js",
			"client/widgets/timepicker.js",
			"client/widgets/tablewidget.js"
		);
}

function getJavaScript_onresize()
{ ?>
	if(fb_module){
		resizeFreeBusyContainer();
		fb_module.resize();
	}
	resizeBody();
	if(window.propNewTime_tableWidget) window.propNewTime_tableWidget.resize();
	<?php
}

function getJavaScript_other(){ ?>
			var tabbarControl;
			var fb_module = null;
			var appoint_dtp;
			var tableWidget = null;
			var FREEBUSY_DAYBEFORE_COUNT;
			var FREEBUSY_NUMBEROFDAYS_COUNT;
			var mrSetupNormal = 0; // New MR setup
			var mrSetupOrganiser = 1; // Oraganizer MR setup
			var mrSetupAttendee = 2; // Attendee MR setup

<?php } // getJavaScript_other

function getJavaScript_onload(){ 
	global $tabbar;
	
	$tabbar->initJavascript("tabbar", "\t\t\t\t\t");
?>
					var rootentryid = false;
					var attachNum = false;

					//general
					tabbar.addExternalHandler(onAppointmentTabChange);
					dragdrop = new DragDrop;
																				
					//first tab
					module.init(moduleID);
					module.setData(<?=get("storeid","false","'", ID_REGEX)?>, <?=get("parententryid","false","'", ID_REGEX)?>);
					
					var appoint_start = new DateTimePicker(dhtml.getElementById("appoint_start"),"<?=_("Start time")?>");
					var appoint_end = new DateTimePicker(dhtml.getElementById("appoint_end"),"<?=_("End time")?>");			
					appoint_dtp = new combinedDateTimePicker(appoint_start,appoint_end);
					
					var entryid = <?=get("entryid","false","'", ID_REGEX)?>;
					if(entryid) {

						// If attachNum is set then it is embedded message
						<? if(isset($_GET["attachNum"]) && is_array($_GET["attachNum"])) { ?>
							rootentryid = <?=get("rootentryid", "false", "'", ID_REGEX)?>;
							attachNum = new Array();
						
							<? foreach($_GET["attachNum"] as $attachNum) { 
								if(preg_match_all(NUMERIC_REGEX, $attachNum, $matches)) {
								?>
									attachNum.push(<?=intval($attachNum)?>);
							<?	}
							} ?>
						<? } ?>

						module.open(entryid, entryid, attachNum, <?=(isset($_GET["basedate"]))?('new Array(\'' . intval(get("basedate", false, false, TIMESTAMP_REGEX)) .'\')') :"false"?>);
					} else {
						var startDateTime = <?=isset($_GET["startdate"]) ? "new Date(".intval(get("startdate", false, false, TIMESTAMP_REGEX))."*1000)":"''"?>; 
						var endDateTime = <?=isset($_GET["enddate"]) ? "new Date(".intval(get("enddate", false, false, TIMESTAMP_REGEX))."*1000)":"''"?>; 
						initDate(startDateTime, endDateTime);
						webclient.menu.showMenu();
					}
															
					var sendbutton = dhtml.getElementById("send");
					if(sendbutton) {
						sendbutton.style.display = "none";
					}
					
					var cancelinviteattendeesbutton = dhtml.getElementById("cancelinviteattendees");
					if(cancelinviteattendeesbutton) {
						cancelinviteattendeesbutton.style.display = "none";
					}

					// Hide print button for new apointments and MRs
					var printButton = dhtml.getElementById("print");
					if(printButton) {
						printButton.style.display = "none";
					}
					
					if (window.location.search.indexOf("meeting=true")>0){
						meetingRequestSetup(mrSetupOrganiser);
					}else{
						meetingRequestSetup(mrSetupNormal);
					}
					resizeBody();
					tabbarControl = tabbar; // FIXME: this is a hack for the tabbar to make it global
					
					dhtml.addEvent(false, dhtml.getElementById("subject"), "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(false, dhtml.getElementById("location"), "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(false, dhtml.getElementById("html_body"), "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(module, dhtml.getElementById("categories"), "blur", eventFilterCategories);

					if (window.location.search.indexOf("allDayEvent=true")>0){
						onChangeAllDayEvent(true);
					}

					//checks for default reminder setting  
					if(parentWebclient.settings.get("calendar/reminder","true") == 'true' && !entryid){
						
						// open the reminder default checkbox on.
						dhtml.setValue(dhtml.getElementById("checkbox_reminder"), true);

						//get the value of reminder time and set it to reminder_minutes
						var value = parentWebclient.settings.get("calendar/reminder_minutes",15);
						dhtml.setValue(dhtml.getElementById("select_reminder_minutes"), parseInt(value));

						onChangeReminder();
					}
					FREEBUSY_DAYBEFORE_COUNT = <?= defined('FREEBUSY_DAYBEFORE_COUNT') ? FREEBUSY_DAYBEFORE_COUNT : 7 ?>;
					FREEBUSY_NUMBEROFDAYS_COUNT = <?= defined('FREEBUSY_NUMBEROFDAYS_COUNT') ? FREEBUSY_NUMBEROFDAYS_COUNT : 90 ?>;

					var suggestEmailModule = webclient.dispatcher.loadModule("suggestEmailAddressModule");
					if(suggestEmailModule != null) {
						var suggestEmailModuleID = webclient.addModule(suggestEmailModule);
						suggestEmailModule.init(suggestEmailModuleID);
						// Setup TO field
						suggestlistTO = new suggestionList("meetingrequest_to_fld", dhtml.getElementById("toccbcc"), suggestEmailModule, suggestionListCallBackAppointment);
						suggestEmailModule.addSuggestionList(suggestlistTO);
					}

					if (<?=get("counterproposal","false","'", NUMERIC_REGEX)?>) {
						module.counterProposal = <?=get("counterproposal", "false", false, NUMERIC_REGEX)?>;
						module.proposedStartDate = <?=get("proposedstartdate", "false", false, TIMESTAMP_REGEX)?>;
						module.proposedEndDate = <?=get("proposedenddate", "false", false, TIMESTAMP_REGEX)?>;
						module.viewAllProposals = <?=get("viewallproposals", "false", false, NUMERIC_REGEX)?>;
					}
					// check if we need to send the request to convert the selected message as appointment
					if(window.windowData && window.windowData["action"] == "convert_item") {
						module.sendConversionItemData(windowData);
					}

					// check whether we are draggging contact item to calendar folder
					if(window.windowData && window.windowData["action"] == "convert_contact") {
						dhtml.getElementById("toccbcc").value = window.windowData["emails"];
						dhtml.getElementById("subject").focus();
					}

					// check whether we are draggging scheduled meeting to calendar folder
					if(window.windowData && window.windowData["action"] == "convert_meeting") {
						module.setBodyFromItemData(window.windowData["data"]);
					}

					// set focus on appointment dialogs
					if (window.location.search.indexOf("meeting=true")>0)
						// set focus on to field if meeting
						dhtml.getElementById("toccbcc").focus();
					else
						// set focus on subject field if normal appointment
						dhtml.getElementById("subject").focus();
<?php } // getJavaScript_onload						
					
function getBody() { 
	global $tabbar, $tabs;
	
	$tabbar->createTabs();
	$tabbar->beginTab("appointment");
?>
		<input id="entryid" type="hidden">
		<input id="parent_entryid" type="hidden">
		<input id="message_class" type="hidden" value="IPM.Appointment">
		<input id="icon_index" type="hidden" value="1024">
		<input id="label" type="hidden" value="0">
		<input id="busystatus" type="hidden" value="2">
		<input id="duedate" type="hidden" value="">
		<input id="startdate" type="hidden" value="">
		<input id="basedate" type="hidden" value="">
		<input id="alldayevent" type="hidden" value="-1">
		<input id="reminder" type="hidden" value="-1">
		<input id="reminder_time" type="hidden" value="">
		<input id="reminder_minutes" type="hidden" value="">		
		<input id="flagdueby" type="hidden" value="">
		<input id="importance" type="hidden" value="1">
		<input id="sensitivity" type="hidden" value="0">
		<input id="private" type="hidden" value="-1">
		<input id="contacts_string" type="hidden" value="">
		<input id="duration" type="hidden" value="">
		<input id="commonstart" type="hidden" value="">
		<input id="commonend" type="hidden" value="">
		<input id="commonassign" type="hidden" value="0">
		<input id="sent_representing_email_address" type="hidden" value="">
		<input id="sent_representing_entryid" type="hidden" value="">
		<input id="sent_representing_name" type="hidden" value="">
		<input id="sent_representing_addrtype" type="hidden" value="">
		<input id="sent_representing_search_key" type="hidden" value="">
		<input id="meeting" type="hidden" value="0">
		
		<!-- Recurring information -->
		
		<input id="recurring" type=hidden value="0">
		<input id="recurring_reset" type=hidden value="0">
		<input id="startocc" type=hidden value="">
		<input id="endocc" type=hidden value="">
		<input id="start" type=hidden value="">
		<input id="end" type=hidden value="">
		<input id="term" type=hidden value="">
		<input id="regen" type=hidden value="">
		<input id="everyn" type=hidden value="">
		<input id="subtype" type=hidden value="">
		<input id="type" type=hidden value="">
		<input id="weekdays" type=hidden value="">
		<input id="month" type=hidden value="">
		<input id="monthday" type=hidden value="">
		<input id="nday" type=hidden value="">
		<input id="numoccur" type=hidden value="">
		<input id="recurring_pattern" type="hidden" value="">
		<input id="recur_type" type="hidden" value="">
		
		<!-- Timezone for new recurring items -->
		
		<input id="timezone" type=hidden value="">
		<input id="timezonedst" type=hidden value="">
		<input id="dststartmonth" type=hidden value="">
		<input id="dststartweek" type=hidden value="">
		<input id="dststartday" type=hidden value="">
		<input id="dststarthour" type=hidden value="">
		<input id="dstendmonth" type=hidden value="">
		<input id="dstendweek" type=hidden value="">
		<input id="dstendday" type=hidden value="">
		<input id="dstendhour" type=hidden value="">
		
		<!-- Meeting request items -->
		<input id="responsestatus" type=hidden value="">
		
		<div id="conflict"></div>
		<div id="extrainfo"></div>
		
		<div id="meetingrequest_recipient" class="properties">
			<div id="meetingrequest_responses">
				
			</div>
		
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<input type="button" class="button" value="<?=_("To")?>:" onclick="webclient.openModalDialog(module, 'addressbook', DIALOG_URL+'task=addressbook_modal&dest=to&fields[to]=<?=urlencode(_("To"))?>&storeid='+module.storeid, 800, 500, abCallBackRecipients);">
					</td>
					<td>
						<input id="toccbcc" name="recipient" class="field" type="text" onchange="syncRecipientFields();">
						<input id="to" name="recipient" class="field" type="hidden" onchange="syncRecipientFields();">
						<input id="cc" name="recipient" class="field" type="hidden" onchange="syncRecipientFields();">
						<input id="bcc" name="recipient" class="field" type="hidden" onchange="syncRecipientFields();">
					</td>
				</tr>
			</table>
		</div>
			
		<div class="properties">
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tr id="meetingrequest_organiser">
					<td class="propertynormal propertywidth">
						<?=_("Organiser");?>:
					</td>
					<td colspan="3">
						<span id="meetingrequest_organiser_name"></span>
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Subject")?>:
					</td>
					<td colspan="3">
						<input id="subject" class="field" type="text">
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Location")?>:
					</td>
					<td>
						<input id="location" class="field" type="text">
					</td>
					<td class="propertynormal propertywidth">
						<?=_("Label")?>:
					</td>
					<td width="120">
						<select id="select_label" class="combobox" style="width:94%;">
							<option value="0" class="label_none" selected><?=_("None")?></option>
							<option value="1" class="label_important"><?=_("Important")?></option>
							<option value="2" class="label_work"><?=_("Business")?></option>
							<option value="3" class="label_personal"><?=_("Personal")?></option>
							<option value="4" class="label_holiday"><?=_("Vacation")?></option>
							<option value="5" class="label_required"><?=_("Must Attend")?></option>
							<option value="6" class="label_travel_required"><?=_("Travel Required")?></option>
							<option value="7" class="label_prepare_required"><?=_("Needs Preparation")?></option>
							<option value="8" class="label_birthday"><?=_("Birthday")?></option>
							<option value="9" class="label_special_date"><?=_("Anniversary")?></option>
							<option value="10" class="label_phone_interview"><?=_("Phone Call")?></option>
						</select>
					</td>
				</tr>
			</table>
		</div>
		<div id="startend">
			<table border="0" cellpadding="0" cellspacing="0">
				<tbody>
					<tr>
						<td>
							<div id="appoint_start"></div>
						</td>
						<td>					
							<input id="checkbox_alldayevent" type="checkbox" onclick="onChangeAllDayEvent();">
							<label for="checkbox_alldayevent"><?=_("All Day Event")?></label>
						</td>
					</tr>
					<tr>
						<td><div id="appoint_end"></div></td>
						<td></td>
					</tr>
				</tbody>
			</table>
		</div>
		<div id="recur" style="display: none">
			<p>
			<table border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td>
						<?=_('Recurrence')?>: <span id="recurtext"></span>
					</td>
				</tr>
			</table>
			<p>
		</div>
		<div class="properties"></div>
		<div class="properties">
			<table border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td width="10">
						<input id="checkbox_reminder" type="checkbox" onclick="onChangeReminder();">
					</td>
					<td class="propertynormal" width="72" nowrap>
						<label for="checkbox_reminder"><?=_("Reminder")?>:</label>
					</td>
					<td width="120">
						<select id="select_reminder_minutes" class="combobox" style="width:95%;background:#DFDFDF;" disabled>
							<option value="0">0 <?=_("minutes")?></option>
							<option value="5">5 <?=_("minutes")?></option>
							<option value="10">10 <?=_("minutes")?></option>
							<option value="15" selected>15 <?=_("minutes")?></option>
							<option value="30">30 <?=_("minutes")?></option>
							<option value="60">1 <?=_("hour")?></option>
							<option value="120">2 <?=_("hours")?></option>
							<option value="240">4 <?=_("hours")?></option>
							<option value="480">8 <?=_("hours")?></option>
							<option value="720">0,5 <?=_("day")?></option>
							<option value="1080">18 <?=_("hours")?></option>
							<option value="1440">1 <?=_("day")?></option>
							<option value="2880">2 <?=_("days")?></option>
							<option value="4320">3 <?=_("days")?></option>
							<option value="5760">4 <?=_("days")?></option>
							<option value="7200">5 <?=_("days")?></option>
							<option value="10080">1 <?=_("week")?></option>
							<option value="20160">2 <?=_("weeks")?></option>
							<option value="43200">1 <?=_("month")?></option>
						</select>
					</td>
					<td>
						<?=_("Busy Status")?>:
					</td>
					<td width="120">
						<select id="select_busystatus" class="combobox" style="width:100%;">
							<option value="0"><?=_("Free")?></option>
							<option value="1"><?=_("Tentative")?></option>
							<option value="2" selected><?=_("Busy")?></option>
							<option value="3"><?=_("Out of Office")?></option>
						</select>
					</td>
				</tr>
			</table>
		</div>
		
		<div class="properties">
			<table width="99%" border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth" valign="top">
						<input class="button" type="button" value="<?=_("Attachments")?>:" onclick="webclient.openWindow(module, 'attachments', DIALOG_URL+'task=attachments_modal&store=' + module.storeid + '&entryid=' + (module.messageentryid?module.messageentryid:'') + '&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value, 570, 425, '0');">
					</td>
					<td valign="top">
						<div id="itemattachments">&nbsp;</div>
					</td>
				</tr>
			</table>
		</div>
		
		<textarea id="html_body" cols="60" rows="12"></textarea>
		
		<div id="categoriesbar">
			<table width="100%" border="0" cellpadding="2" cellspacing="0" style="table-layout: fixed;">
				<tr>
					<td class="propertynormal propertywidth">
						<input class="button" type="button" value="<?=_("Contacts")?>:" onclick="webclient.openModalDialog(module, 'addressbook', DIALOG_URL+'task=addressbook_modal&dest=contacts&fields[contacts]=<?=urlencode(_("Contacts"))?>&storeid='+module.storeid, 800, 500, abCallBackContacts);">
					</td>
					<td>
						<input id="contacts" class="field" type="text">
					</td>
					<td class="propertynormal propertywidth">
						<input class="button" type="button" value="<?=_("Categories")?>:" onclick="webclient.openModalDialog(module, 'categories', DIALOG_URL+'task=categories_modal', 350, 370, categoriesCallBack);">
					</td>
					<td>
						<input id="categories" class="field" type="text">
					</td>
					<td width="30" nowrap>
						<label for="checkbox_private"><?=_("Private")?></label>
					</td>
					<td width="16">
						<input id="checkbox_private" type="checkbox">
					</td>
				</tr>
			</table>
		</div>
<?php 
	$tabbar->endTab();
	
	$tabbar->beginTab("scheduling");

	require("freebusy.php");

	$tabbar->endTab();
	
	if(get("entryid","false","'", ID_REGEX) != "false"){// check whether the user is owner of the appointment or not.
		$tabbar->beginTab("tracking");
?>
		<div id="tracking_container" class="tracking_container">
			<div><?= _("The following responses to this meeting have been received:");?></div>
			<div id="tracking_table" class="trackingtabtable">
			</div>
		</div>
<?php
		$tabbar->endTab();
	}
	 
} // getBody

function getMenuButtons(){
	return array(
			array(
				'id'=>"accept",
				'name'=>_("Accept"),
				'title'=>_("Accept"),
				'callback'=>'function(){submitAppointment(false, "accept");}'
			),
			array(
				'id'=>"tentative",
				'name'=>_("Tentative"),
				'title'=>_("Tentative"),
				'callback'=>'function(){submitAppointment(false, "tentative");}'
			),
			array(
				'id'=>"decline",
				'name'=>_("Decline"),
				'title'=>_("Decline"),
				'callback'=>'function(){submitAppointment(false, "decline");}'
			),
			array(
				'id'=>"proposenewtime",
				'name'=>_("Propose New Time"),
				'title'=>_("Propose New Time"),
				'callback'=>'function(){eventAppointmentDialogProposeNewTimeClick(module);}'
			),
			array(
				'id'=>"send",
				'name'=>_("Send"),
				'title'=>_("Send"),
				'callback'=>'function(){submitAppointment(true)}'
			),
			array(
				'id'=>"save",
				'name'=>_("Save"),
				'title'=>_("Save"),
				'callback'=>'function(){submitAppointment(false)}'
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"print",
				'name'=>"",
				'title'=>_("Print"),
				'callback'=>'function(){eventOpenPrintAppointmentItemDialog(module);}'
			),
			array(
				'id'=>"seperator0",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"attachment",
				'name'=>"",
				'title'=>_("Add Attachments"),
				'callback'=>"function(){webclient.openWindow(module, 'attachments', DIALOG_URL+'task=attachments_modal&store=' + module.storeid + '&entryid=' + (module.messageentryid?module.messageentryid:'') + '&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value, 570, 425, '0');}"
			),
			array(
				'id'=>"attach_item",
				'name'=>"",
				'title'=>_("Attach item"),
				'callback'=>"function(){webclient.openModalDialog(module, 'attachitem', DIALOG_URL+'task=attachitem_modal&storeid=' + module.storeid + '&entryid=' + module.parententryid +'&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value, FIXEDSETTINGS.ATTACHITEM_DIALOG_WIDTH, FIXEDSETTINGS.ATTACHITEM_DIALOG_HEIGHT, false, false, {module : module});}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"checknames",
				'name'=>"",
				'title'=>_("Check Names"),
				'callback'=>'function(){checkNames(checkNamesCallBackAppointment);}'
			),
			array(
				'id'=>"seperator1",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"recurrence",
				'name'=>_("Recurrence"),
				'title'=>_("Recurrence"),
				'callback'=>"function(){webclient.openModalDialog(module, 'recurrence', DIALOG_URL+'task=recurrence_modal&store=' + module.storeid + '&entryid=' + (module.messageentryid?module.messageentryid:'') + '&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value, 550, 405, callBackRecurrence);}"
			),
			array(
				'id'=>"seperator2",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"inviteattendees",
				'name'=>_("Invite Attendees"),
				'title'=>_("Invite Attendees"),
				'callback'=>"function(){meetingRequestSetup(mrSetupOrganiser);}"
			),
			array(
				'id'=>"cancelinviteattendees",
				'name'=>_("Cancel Invitation"),
				'title'=>_("Cancel Invitation"),
				'callback'=>"function(){cancelInvitation();}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"priority_high",
				'name'=>"",
				'title'=>_("Priority").": "._("High"),
				'callback'=>"function(){setImportance(dhtml.getElementById('importance').value!=2?2:1);}"
			),
			array(
				'id'=>"priority_low",
				'name'=>"",
				'title'=>_("Priority").": "._("Low"),
				'callback'=>"function(){setImportance(dhtml.getElementById('importance').value!=0?0:1);}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"delete",
				'name'=>"",
				'title'=>_("Delete"),
				'callback'=>"deleteAppointment"
			)
		);
}
?>
