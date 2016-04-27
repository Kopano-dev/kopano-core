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
	return _("Add/Edit rule");
}

function getIncludes(){
	return array(
			"client/layout/js/rule.js",
			"client/layout/css/rule.css",
		);
}

function getJavaScript_onload(){ 
	$internalid = get("internalid", false, false, ID_REGEX);
	if ($internalid !== false){
?>
	var rule = window.opener.module.getRule(<?=intval($internalid)?>);

	if(rule)
		showRule(rule);
<?php 
	} // if $internalid
?>
	dhtml.addEvent(-1, dhtml.getElementById("action_move"), "click", eventActionSelectionChanged);
	dhtml.addEvent(-1, dhtml.getElementById("action_copy"), "click", eventActionSelectionChanged);
	dhtml.addEvent(-1, dhtml.getElementById("action_delete"), "click", eventActionSelectionChanged);
	dhtml.addEvent(-1, dhtml.getElementById("action_redirect"), "click", eventActionSelectionChanged);
	dhtml.addEvent(-1, dhtml.getElementById("action_forward"), "click", eventActionSelectionChanged);
<?php 
} // getJavaScript_onload						

function getBody(){ ?>
		<fieldset>
			<legend><?=_("Rule")?></legend>
			<table class="options">
				<tr>
					<th>
						<label for="name"><?=_("Rule name")?></label>
					</th>
					<td>
						<input id="name" type="text" class="inputwidth" />
					</td>
					<td class="buttonspace"></td>
				</tr>
			</table>
		</fieldset>
		<fieldset>
			<legend><?=_("With the following properties")?></legend>
			<table class="options">
				<tr>
					<th>
						<label for="cond_from"><?=_("From field contains")?></label>
					</th>
					<td>
						<input id="cond_from" type="text" class="inputwidth" />
					</td>
					<td class="buttonspace">
						<input type="button" class="addressbookbutton" onclick="getFromAddressBook('cond_from', 'email_single')" />
					</td>
				</tr>
				<tr>
					<th>
						<label for="cond_subject"><?=_("Subject contains")?></label>
					</th>
					<td>
						<input id="cond_subject" type="text" class="inputwidth" />
					</td>
					<td class="buttonspace"></td>
				</tr>
				<tr>
					<th>
						<label for="cond_priority"><?=_("Priority")?>
					</th>
					<td>
						<select id="cond_priority" class="combobox">
							<option value="-1" selected></option>
							<option value="0"><?=_("Low")?></option>
							<option value="1"><?=_("Normal")?></option>
							<option value="2"><?=_("High")?></option>
						</select>
					</td>
					<td class="buttonspace"></td>
				</tr>
			</table>
		</fieldset>
		<fieldset>
			<legend><?=_("Sent to")?></legend>
			<table class="options">
				<tr>
					<th>
						<label for="cond_sent_to"><?=_("Contact or Distribution List")?></label>
					</th>
					<td>
						<input id="cond_sent_to" type="text" class="inputwidth" />
					</td>
					<td class="buttonspace">
						<input type="button" class="addressbookbutton" onclick="getFromAddressBook('cond_sent_to')" />
					</td>
				</tr>
				<tr>
					<th>
						<label for="cond_sent_to_me"><?=_("Sent only to me")?></label>
					</th>
					<td>
						<input id="cond_sent_to_me" type="checkbox"/>
					</td>
					<td class="buttonspace"></td>
				</tr>
			</table>
		</fieldset>
		<fieldset>
			<legend><?=_("Perform the following action")?></legend>
			<table>
			<tr>
				<th colspan="3">
					<input type="radio" name="action" id="action_move"/>
					<label for="action_move"><?=_("Move to")?> 
						<a href="#" onclick="selectFolder('action_move_folder')">
							<span id="action_move_folder"><?=_("folder")?></span>
						</a>
					</label>
				</th>
			</tr>
			<tr>
				<th colspan="3">
					<input type="radio" name="action" id="action_copy"/>
					<label for="action_copy"><?=_("Copy to")?> 
						<a href="#" onclick="selectFolder('action_copy_folder')">
							<span id="action_copy_folder"><?=_("folder")?></span>
						</a>
					</label>
				</th>
			</tr>
			<tr>
				<th colspan="3">
					<input type="radio" name="action" id="action_delete"/>
					<label for="action_delete"><?=_("Delete the message")?></label>
				</th>
			</tr>
			<tr>
				<th>
					<input type="radio" name="action" id="action_redirect"/>
					<label for="action_redirect"><?=_("Redirect to")?></label>
				</th>
				<td>
					<input id="action_redirect_address" type="text" class="inputwidth" />
				</td>
				<td class="buttonspace">
					<input type="button" class="addressbookbutton" onclick="getFromAddressBook('action_redirect_address')" />
				</td>
			</tr>
			<tr>
				<th>
					<input type="radio" name="action" id="action_forward"/>
					<label for="action_forward"><?=_("Forward to")?></label>
				</th>
				<td>
					<input id="action_forward_address" type="text" class="inputwidth" />
				</td>
				<td class="buttonspace">
					<input type="button" class="addressbookbutton" onclick="getFromAddressBook('action_forward_address')" />
				</td>
			</tr>
			<tr>
				<th>
					<input type="radio" name="action" id="action_forward_attach"/>
					<label for="action_forward_attach"><?=_("Forward as Attachment to")?></label>
				</th>
				<td>
					<input id="action_forward_attach_address" type="text" class="inputwidth" />
				</td>
				<td class="buttonspace">
					<input type="button" class="addressbookbutton" onclick="getFromAddressBook('action_forward_attach_address')" />
				</td>
			</tr>
			<tr>
				<th colspan="3">
					<input type="checkbox" id="stop_processing_more_rules"/>
					<label for="stop_processing_more_rules"><?=_("Stop processing more rules")?></label>
				</th>
			</tr>
			</table>
		</fieldset>
		<input type="hidden" id="sequence"/>
		<?=createConfirmButtons("if(submitRule()) window.close; else window.focus();")?>
<?php } // getBody
?>
