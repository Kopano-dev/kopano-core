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
function getModuleName(){
	return "delegatesmodule";
}
function getDialogTitle(){
	return _("Delegate Permissions");
}
function getIncludes(){
	$includes = array(
		"client/modules/".getModuleName().".js",
		"client/layout/js/delegatespermissions.js",
		"client/layout/css/delegates.css",
		"client/widgets/tablewidget.js"
	);
	return $includes;
}

function getJavaScript_onload(){
?>
	module.init(moduleID);
	if (window.windowData){
		var user = window.windowData["delegate"];
		editDelegates = user;
		if (window.windowData["newDelegate"] == true)
			module.getNewUserPermissions(user[0]["entryid"]);
		initDelegatePermissions(user);
	}
<?php } // getJavaScript_onload
function getJavaScript_other(){
?>
	var delegate;
	var editDelegates;
	
<?php }
function getBody(){ ?>
<div id="delegatespermissions">
	<fieldset>
		<legend><?=_("This delegate has the following permissions")?></legend>
		<table cellpadding="8" cellspacing="0" width="100%">
			<tr>
				<td class="delegatesfolders" style="padding-bottom: 0px;"><label><?=_("Calendar")?>:</label></td>
				<td style="padding-bottom: 0px;">
					<select id="calendar" onchange="toggleDelegateMeetingRuleOption(this);" onkeyup="toggleDelegateMeetingRuleOption(this);"></select>
				</td>
			</tr>
			<tr>
				<td colspan="2" style="padding: 0px 0px 0px 32px;">
					<input type="checkbox" id="delegate_meeting_rule_checkbox" class="checkbox" disabled />
					<label for="delegate_meeting_rule_checkbox" class="disabled_text"><?=_("Delegate receives copies of meeting-related messages sent to me")?></label>
				</td>
			</tr>
			<tr>
				<td class="delegatesfolders"><label><?=_("Tasks")?>:</label></td>
				<td>
					<select id="tasks"></select>
				</td>
			</tr>
			<tr>
				<td class="delegatesfolders"><label><?=_("Inbox")?>:</label></td>
				<td>
					<select id="inbox"></select>
				</td>
			</tr>
			<tr>
				<td class="delegatesfolders"><label><?=_("Contacts")?>:</label></td>
				<td>
					<select id="contacts"></select>
				</td>
			</tr>
			<tr>
				<td class="delegatesfolders"><label><?=_("Notes")?>:</label></td>
				<td>
					<select id="notes"></select>

				</td>				
			</tr>
			<tr>
				<td class="delegatesfolders"><label><?=_("Journal")?>:</label></td>
				<td>
					<select id="journal"></select>
				</td>				
			</tr>
		</table>
	</fieldset>
</div>
<div>
	<input id="see_private" type="checkbox" class="checkbox">
	<label for="see_private"><?=_("Delegate can see my private items")?></label>
</div>
<?
	print (createConfirmButtons("submitDelegatePermissions();window.close();"));
} ?>