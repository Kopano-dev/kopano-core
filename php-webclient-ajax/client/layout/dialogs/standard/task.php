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

	$tabs = array("task" => _("Task"), "disabledtask" => _("Task"),"details" => _("Details"));
	$tabbar = new TabBar($tabs, key($tabs));
}

function getModuleName(){
	return "taskitemmodule";
}

function getModuleType(){
	return "item";
}

function getDialogTitle(){
	return _("Task");
}

function getIncludes(){
	return array(
			"client/layout/css/tabbar.css",
			"client/layout/js/tabbar.js",
			"client/layout/js/task.js",
			"client/layout/js/date-picker.js",
			"client/layout/js/date-picker-language.js",
			"client/layout/js/date-picker-setup.js",
			"client/layout/css/date-picker.css",
			"client/layout/css/suggestionlayer.css",
			"client/widgets/suggestionlist.js",
			"client/modules/suggestemailaddressmodule.js",
			"client/modules/".getModuleName().".js"
		);
}

function getJavaScript_onload(){
	global $tabbar;
	
	$tabbar->initJavascript("tabbar", "\t\t\t\t\t");
 ?>
					tabbarControl = tabbar;
					window.taskrequest = "<?=get("taskrequest","false")?>";
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
				
					Calendar.setup({
						inputField	:	"text_commonend",				// id of the input field
						ifFormat	:	_('%d-%m-%Y'),					// format of the input field
						button		:	"text_duedate_button",		// trigger for the calendar (button ID)
						step		:	1,							// show all years in drop-down boxes (instead of every other year as default)
						weekNumbers	:	false
					});
					
					Calendar.setup({
						inputField	:	"text_commonstart",			// id of the input field
						ifFormat	:	_('%d-%m-%Y'),					// format of the input field
						button		:	"text_startdate_button",	// trigger for the calendar (button ID)
						step		:	1,							// show all years in drop-down boxes (instead of every other year as default)
						weekNumbers	:	false
					});
					
					Calendar.setup({
						inputField	:	"text_reminderdate",		// id of the input field
						ifFormat	:	_('%d-%m-%Y'),					// format of the input field
						button		:	"text_reminderdate_button",	// trigger for the calendar (button ID)
						step		:	1,							// show all years in drop-down boxes (instead of every other year as default)
						weekNumbers	:	false,
						dependedElement: "checkbox_reminder"
					});
					
					Calendar.setup({
						inputField	:	"text_datecompleted",		// id of the input field
						ifFormat	:	_('%d-%m-%Y'),					// format of the input field
						button		:	"text_datecompleted_button",// trigger for the calendar (button ID)
						step		:	1,							// show all years in drop-down boxes (instead of every other year as default)
						weekNumbers	:	false
					});
					
					var sendbutton = dhtml.getElementById("send");
					if(sendbutton) {
						sendbutton.style.display = "none";
					}
					
					var cancelassigntaskbutton = dhtml.getElementById("cancelassigntask");
					if(cancelassigntaskbutton) {
						cancelassigntaskbutton.style.display = "none";
					}

					// Owner
					if(dhtml.getElementById("owner").value == "") {
						dhtml.getElementById("owner").value = webclient.fullname;
					}
					
					resizeBody();
					
					// Set all inputs to do normal events
					var inputElements = window.document.getElementsByTagName("input");
					
                    for(i=0 ; i < inputElements.length; i++) {
                        dhtml.addEvent(false, inputElements[i], "contextmenu", forceDefaultActionEvent);
                    }
   					dhtml.addEvent(false, dhtml.getElementById("html_body"), "contextmenu", forceDefaultActionEvent);
   					
   					//Set events for filtering categories.
   					var categories = dhtml.getElementById("categories");
   					dhtml.addEvent(module, categories, "change", eventFilterCategories);

					// check if we need to send the request to convert the selected message as task
					if(window.windowData && window.windowData["action"] == "convert_item") {
						module.sendConversionItemData(windowData);
					}
   					
					// Set up buttons
					setTaskRequestMode(window.taskrequest == "true" ? tdsOWNNEW : tdsNOM, window.taskrequest == "true" ? tdmtTaskReq : tdmtNothing);

					var suggestEmailModule = webclient.dispatcher.loadModule("suggestEmailAddressModule");
					if(suggestEmailModule != null) {
						var suggestEmailModuleID = webclient.addModule(suggestEmailModule);
						suggestEmailModule.init(suggestEmailModuleID);
						// Setup TO field
						suggestlistTO = new suggestionList("taskrequest", dhtml.getElementById("to"), suggestEmailModule);
						suggestEmailModule.addSuggestionList(suggestlistTO);
					}
					setChangeHandlers(dhtml.getElementById("to"));
					setChangeHandlers(dhtml.getElementById("subject"));
					setChangeHandlers(dhtml.getElementById("text_commonend"));
					setChangeHandlers(dhtml.getElementById("text_commonstart"));
					setChangeHandlers(dhtml.getElementById("select_status"));
					setChangeHandlers(dhtml.getElementById("select_priority"));
					setChangeHandlers(dhtml.getElementById("text_percent_complete"));
					setChangeHandlers(dhtml.getElementById("text_datecompleted"));
					setChangeHandlers(dhtml.getElementById("text_datecompleted"));
					setChangeHandlers(dhtml.getElementById("text_totalwork"));
					setChangeHandlers(dhtml.getElementById("mileage"));
					setChangeHandlers(dhtml.getElementById("text_actualwork"));
					setChangeHandlers(dhtml.getElementById("billinginformation"));
					setChangeHandlers(dhtml.getElementById("companies"));
					
					//explicitly added onchange event on status selection list
					dhtml.addEvent(false, dhtml.getElementById("select_status"), "change", onChangeStatus);

					//explicitly added onchange event on every datepicker object, to validate date entered
					dhtml.addEvent(false, dhtml.getElementById("text_commonend"), "change", eventDateInputChange);
					dhtml.addEvent(false, dhtml.getElementById("text_commonstart"), "change", eventDateInputChange);
					dhtml.addEvent(false, dhtml.getElementById("text_datecompleted"), "change", eventDateInputChange);
					dhtml.addEvent(false, dhtml.getElementById("text_reminderdate"), "change", eventDateInputChange);

					// Set focus on subject or recipient selection field.
					if(window.taskrequest == "true")
						dhtml.getElementById("to").focus();
					else
						dhtml.getElementById("subject").focus();

<?php } // getJavaSctipt_onload

function getJavaScript_other(){
?>
		function abCallBack(recips)
		{
			for(key in recips) {
				if (key!="multiple" && key!="value"){
					var fieldElement = dhtml.getElementById(key);
					fieldElement.value = recips[key].value;
				}
			}

			var selectionElement = dhtml.getElementById("to");
			selectionElement.select();
			selectionElement.focus();
		}

		function setChangeHandlers(element)
		{
			if (element){
				dhtml.addEvent(-1, element, "change", setMessageChanged);
				dhtml.addEvent(-1, element, "keypress", setMessageChanged);
			}
		}

		function setMessageChanged()
		{
			dhtml.getElementById("messagechanged").value = 1;
		}

<?php }
function getBody() {
	global $tabbar, $tabs;
	
	$tabbar->createTabs();
	$tabbar->beginTab("task");
?>
		<input id="entryid" type="hidden">
		<input id="parent_entryid" type="hidden">
		<input id="message_class" type="hidden" value="IPM.Task">
		<input id="icon_index" type="hidden" value="1280">
		<input id="status" type="hidden" value="0">
		<input id="duedate" type="hidden" value="">
		<input id="startdate" type="hidden" value="">
		<input id="importance" type="hidden" value="1">
		<input id="complete" type="hidden" value="0">
		<input id="percent_complete" type="hidden" value="0">
		<input id="flagdueby" type="hidden" value="">
		<input id="previousReminderValue" type="hidden" value="0">
		<input id="reminder" type="hidden" value="0">
<!--		<input id="reminder_time" type="hidden" value="">  -->
		<input id="reminderdate" type="hidden" value="">
		<input id="totalwork" type="hidden" value="">
		<input id="actualwork" type="hidden" value="">
		<input id="datecompleted" type="hidden" value="">
		<input id="sensitivity" type="hidden" value="0">
		<input id="private" type="hidden" value="-1">
		<input id="contacts_string" type="hidden" value="">
		<input id="commonstart" type="hidden" value="">
		<input id="commonend" type="hidden" value="">
		<input id="commonassign" type="hidden" value="0">
		<input id="messagechanged" type="hidden" value="0">

		<input id="taskstate" type="hidden" value="1">
		<input id="taskhistory" type="hidden" value="0">
		<input id="tasklastdelegate" type="hidden" value="0">
		<input id="assignedtime" type="hidden" value="0">
		<input id="taskaccepted" type="hidden" value="0">
		<input id="taskmode" type="hidden" value="0">
		<input id="ownership" type="hidden" value="0">
		<input id="delegationstate" type="hidden" value="0">

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

		<div id="extrainfo"></div>
		<div id="recurtext"></div>
		<div id="conflict"></div>
		
		<div class="properties">
			<div id="taskrequest_recipient">
				<table width="100%" border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td class="propertynormal propertywidth">
							<input type="button" class="button" value="<?=_("To")?>:" onclick="webclient.openModalDialog(module, 'addressbook', DIALOG_URL+'task=addressbook_modal&dest=to&fields[to]=<?=urlencode(_("To"))?>&storeid='+module.storeid, 800, 500, abCallBack);">
						</td>
						<td>
							<input id="to" name="recipient" class="field">
							<input id="cc" name="recipient" class="field" type="hidden">
							<input id="bcc" name="recipient" class="field" type="hidden">
						</td>
					</tr>
				</table>
			</div>
			
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Subject")?>:
					</td>
					<td>
						<input id="subject" class="field" type="text">
					</td>
				</tr>
			</table>
		</div>
		
		<div class="properties">
			<table border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth" nowrap>
						<?=_("Due Date")?>:
					</td>
					<td width="120">
						<input id="text_commonend" class="fieldsize" type="text" value="<?=_("None")?>" onchange="onChangeDate(this.id);">
					</td>
					<td width="30">
						<div id="text_duedate_button" class="datepicker">&nbsp;</div>
					</td>
					<td class="propertynormal" width="60">
            			<?=_("Status")?>:
                	</td>
                	<td colspan="4">
                		<select id="select_status" class="comboboxwidth" style="width:100%;">
                			<option value="0"><?=_("Not Started")?></option>
                            <option value="1"><?=_("In Progress")?></option>
                            <option value="2"><?=_("Complete")?></option>
                            <option value="3"><?=_("Wait for other person")?></option>
                            <option value="4"><?=_("Deferred")?></option>
                		</select>
                	</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth" nowrap>
						<?=_("Start Date")?>:
					</td>
					<td>
						<input id="text_commonstart" class="fieldsize" type="text" value="<?=_("None")?>" onchange="onChangeDate(this.id);">
					</td>
					<td>
						<div id="text_startdate_button" class="datepicker">&nbsp;</div>
					</td>
					<td class="propertynormal">
            			<?=_("Priority")?>:
                	</td>
                	<td width="85">
                		<select id="select_priority" class="combobox">
                			<option value="0"><?=_("Low")?></option>
                			<option value="1" selected><?=_("Normal")?></option>
                			<option value="2"><?=_("High")?></option>
                		</select>
                	</td>
                	<td class="propertynormal" width="80">
                		% <?=_("Complete")?>:
                	</td>
                	<td>
                		<input id="text_percent_complete" onchange="onChangeComplete();" class="fieldsize" type="text" size="2" value="0%">
                	</td>
                	<td width="18">
						<div class="spinner_up" onclick="completeSpinnerUp();">&nbsp;</div>
						<div class="spinner_down" onclick="completeSpinnerDown();">&nbsp;</div>
					</td>
				</tr>
			</table>
		</div>
		
		<div class="properties">
			<div id="remindervalues">
				<table width="99%" border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td width="10">
							<input id="checkbox_reminder" type="checkbox" onclick="onChangeReminder();">
						</td>
						<td class="propertynormal" width="72" nowrap>
							<label for="checkbox_reminder"><?=_("Reminder")?>:</label>
						</td>
						<td width="120">
							<input id="text_reminderdate" class="fieldsize" style="background:#DFDFDF;" type="text" value="<?=_("None")?>" disabled>
						</td>
						<td width="30">
							<div id="text_reminderdate_button" class="datepicker">&nbsp;</div>
						</td>
						<td width="40">
							<input id="text_reminderdate_time" class="fieldsize" style="background:#DFDFDF;" type="text" size="3" value="<?=_("None")?>" disabled>
						</td>
						<td width="40">
							<div class="spinner_up" onclick="if(dhtml.getElementById('checkbox_reminder').checked){timeSpinnerUp(dhtml.getElementById('text_reminderdate_time'));}">&nbsp;</div>
							<div class="spinner_down" onclick="if(dhtml.getElementById('checkbox_reminder').checked){timeSpinnerDown(dhtml.getElementById('text_reminderdate_time'));}">&nbsp;</div>
						</td>
						<td class="propertynormal" width="60" nowrap>
							<?=_("Owner")?>:
						</td>
						<td>
							<input id="owner" class="field" type="text">
						</td>
					</tr>
				</table>
			</div>
			
			<div id="taskrequest_settings">
				<table border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td width="10">
							<input id="taskupdates" type="checkbox" checked>
						</td>
						<td class="propertynormal" nowrap>
							<label for="taskupdates"><?=_("Keep copy of task in task list")?>.</label>
						</td>
					</tr>
					<tr>
						<td width="10">
							<input id="tasksoc" type="checkbox" checked>
						</td>
						<td class="tasksoc" nowrap>
							<label for="tasksoc"><?=_("Send me a status report when task is completed")?>.</label>
						</td>
					</tr>
				</table>
			</div>
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
						<input class="button" type="button" value="<?=_("Contacts")?>:" onclick="webclient.openModalDialog(module, 'addressbook', DIALOG_URL+'task=addressbook_modal&dest=contacts&fields[contacts]=<?=urlencode(_("Contacts"))?>&storeid='+module.storeid, 800, 500, abCallBack);">
					</td>
					<td>
						<input id="contacts" class="field" type="text">
					</td>
					<td class="propertynormal propertywidth">
						<input class="button" type="button" value="<?=_("Categories")?>:" onclick="webclient.openModalDialog(module, 'categories', DIALOG_URL+'task=categories_modal', 350, 370, categoriesCallBack);">
					</td>
					<td>
						<input id="categories" class="field" type="text" onchange="eventFilterCategories(this);">
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
	
	$tabbar->beginTab("disabledtask");
?>
		<div id="disabled_extrainfo"></div>
		<div class="properties">
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Subject")?>:
					</td>
					<td colspan="5">
						<div id="disabled_subject" class="disabledfield"></div>
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Due Date")?>:
					</td>
					<td colspan="5">
						<div id="disabled_duedate" class="disabledfield"></div>
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Status")?>:
					</td>
					<td width="110">
						<div id="disabled_status" class="disabledfield"></div>
					</td>
					<td class="propertynormal disabledproperty">
						<?=_("Priority")?>:
					</td>
					<td width="110">
						<div id="disabled_priority" class="disabledfield"></div>
					</td>
					<td class="propertynormal disabledproperty">
						% <?=_("Complete")?>:
					</td>
					<td>
						<div id="disabled_percentcomplete" class=""></div>
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Owner")?>:
					</td>
					<td colspan="5">
						<div id="disabled_owner" class="disabledfield"></div>
					</td>
				</tr>
			</table>
		</div>

		<textarea id="disabled_html_body" cols="60" rows="12" disabled></textarea>

<?php 
	$tabbar->endTab();
	
	$tabbar->beginTab("details");
?>
	
	<div class="properties">
			<table border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Date completed")?>:
					</td>
					<td width="120">
						<input id="text_datecompleted" class="fieldsize" type="text" onchange="onChangeDateCompleted();">
					</td>
					<td width="20">
						<div id="text_datecompleted_button" class="datepicker">&nbsp;</div>
					</td>
				</tr>
			</table>
			
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Total work")?>:
					</td>
					<td width="120">
						<input id="text_totalwork" class="fieldsize" type="text">
					</td>
					<td width="20">&nbsp;</td>
					<td class="propertynormal" width="100">
						<?=_("Mileage")?>:
					</td>
					<td>
						<input id="mileage" class="field" type="text">
					</td>
				</tr>
			</table>
			
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Actual work")?>:
					</td>
					<td width="120">
						<input id="text_actualwork" class="fieldsize" type="text">
					</td>
					<td width="20">&nbsp;</td>
					<td class="propertynormal" width="100" nowrap>
						<?=_("Billing information")?>:
					</td>
					<td>
						<input id="billinginformation" class="field" type="text">
					</td>
				</tr>
			</table>
			
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Companies")?>:
					</td>
					<td colspan="4">
						<input id="companies" class="field" type="text" style="width:99%">
					</td>
				</tr>
			</table>
		</div>

		<div>
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Update List")?>:
					</td>
					<td colspan="4">
						<input id="display_cc" class="field" type="text" disabled style="width:99%">
					</td>
				</tr>
				<tr>
					<td colspan="4">
						<input id="create_unassigned_copy" type="button" value="<?=_("Create Unassigned Copy")?>" disabled onclick="createUnassignedCopy();">
					</td>
				</tr>
			</table>
		</div>
	
<?php
	$tabbar->endTab();
} // getBody

function getMenuButtons(){
	return array(
			array(
				'id'=>"return_tasklist",
				'name'=>_("Return to Task List"),
				'title'=>_("Return to Task List"),
				'callback'=>'function(){reclaimOwnerShip();}'
			),
			array(
				'id'=>"accept",
				'name'=>_("Accept"),
				'title'=>_("Accept"),
				'callback'=>'function(){submitTask("accept");}'
			),
			array(
				'id'=>"decline",
				'name'=>_("Decline"),
				'title'=>_("Decline"),
				'callback'=>'function(){submitTask("decline");}'
			),
			array(
				'id'=>"send",
				'name'=>_("Send"),
				'title'=>_("Send"),
				'callback'=>'function(){submitTask("request");}'
			),
			array(
				'id'=>"save",
				'name'=>_("Save"),
				'title'=>_("Save"),
				'callback'=>'function(){submitTask();}'
			),
			array(
				'id'=>"seperator",
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
				'id'=>"checknames",
				'name'=>"",
				'title'=>_("Check Names"),
				'callback'=>'function(){checkNames(checkNamesCallBackTask);}'
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"recurrence",
				'name'=>_("Recurrence"),
				'title'=>_("Recurrence"),
				'callback'=>"function(){webclient.openModalDialog(module, 'recurrence', DIALOG_URL+'task=recurrence_modal&store=' + module.storeid + '&entryid=' + (module.messageentryid?module.messageentryid:'') + '&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value + '&taskrecurrence=true', 550, 405, callBackTaskRecurrence);}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"assigntask",
				'name'=>_("Assign Task"),
				'title'=>_("Assign Task"),
				'callback'=>"function(){assignTask();}"
			),
			array(
				'id'=>"cancelassigntask",
				'name'=>_("Cancel Assignment"),
				'title'=>_("Cancel Assignment"),
				'callback'=>"function(){cancelAssignTask();}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"taskcomplete",
				'name'=>"",
				'title'=>_("Mark Complete"),
				'callback'=>"function(){setTaskCompleted()}"
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
				'callback'=>"function(){delete_task_item()}"
			)
		);
}

?>
