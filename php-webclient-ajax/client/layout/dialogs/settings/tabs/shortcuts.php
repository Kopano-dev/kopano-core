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

function shortcuts_settings_title(){
	return _("Shortcuts");
}

function shortcuts_settings_order(){
	return 5;
}

function shortcuts_settings_html(){ ?>
		<table class="shortcuts">
			<tr>
				<th><label><?=_("Keyboard Shortcuts")."  :"?></label></th>
				<td><input id="shortcuts_off" type="radio" name="shortcuts_options"><?=_("OFF")?><br><input id="shortcuts_on" type="radio" name="shortcuts_options"><?=_("ON")?></td>
			</tr>
		</table>
		<table >
			<tr><th><label><?=_('Press keys together')?></label></th></tr>
			<tr><th><?=_('For example, to open shared folder hold Z while pressing F')?></th></tr>
		</table>
	<fieldset>
		<legend><?=_("For basic navigation")?></legend>
		<table class="shortcuts">
			<tr>
				<th><label><?=_("Open Shared Folder")."  :"?></label></th>
				<td><div id="open_shared_folder"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Open Shared Store")."  :"?></label></th>
				<td><div id="open_shared_store"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Open Inbox")."  :"?></label></th>
				<td><div id="open_inbox"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Open Calendar")."  :"?></label></th>
				<td><div id="open_calendar"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Open Contacts")."  :"?></label></th>
				<td><div id="open_contact"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Open Notes")."  :"?></label></th>
				<td><div id="open_note"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Open Multi User Calendar")."  :"?></label></th>
				<td><div id="open_muc"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Change to Previous View")."  :"?></label>
				<td><div id="view_prev"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Change to Next View")."  :"?></label>
				<td><div id="view_next"></div></td>
			</tr>
		</table>
	</fieldset>

	<fieldset>
		<legend><?=_("For creating an item")?></legend>
		<table class="shortcuts">
			<tr>
				<th><label><?=_("New Item")."  :"?></label>
				<td><div id="new_item"></div></td>
			</tr>
			<tr>
				<th><label><?=_("New Mail")."  :"?></label>
				<td><div id="new_mail"></div></td>
			</tr>
			<tr>
				<th><label><?=_("New Appointment")."  :"?></label>
				<td><div id="new_appointment"></div></td>
			</tr>
			<tr>
				<th><label><?=_("New Meeting Request")."  :"?></label>
				<td><div id="new_meeting_request"></div></td>
			</tr>
			<tr>
				<th><label><?=_("New Contact")."  :"?></label>
				<td><div id="new_contact"></div></td>
			</tr>
			<tr>
				<th><label><?=_("New Distribution List")."  :"?></label>
				<td><div id="new_distlist"></div></td>
			</tr>
			<tr>
				<th><label><?=_("New Task")."  :"?></label>
				<td><div id="new_task"></div></td>
			</tr>
			<tr>
				<th><label><?=_("New Task Request")."  :"?></label>
				<td><div id="new_taskrequest"></div></td>
			</tr>
			<tr>
				<th><label><?=_("New Note")."  :"?></label>
				<td><div id="new_note"></div></td>
			</tr>
			<tr>
				<th><label><?=_("New Folder")."  :"?></label>
				<td><div id="new_folder"></div></td>
			</tr>
		</table>
	</fieldset>

	<fieldset>
		<legend><?=_("For All items (List View)")?></legend>
		<table class="shortcuts">
			<tr>
				<th><label><?=_("Open")."  :"?></label>
				<td><div id="item">ENTER</div></td>
			</tr>
			<tr>
				<th><label><?=_("Copy")."  :"?></label>
				<td><div id="edit_item_copy"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Move")."  :"?></label>
				<td><div id="edit_item_move"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Toggle Read/Unread")."  :"?></label>
				<td><div id="edit_item_toggle_read"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Categorize")."  :"?></label>
				<td><div id="edit_item_categorize"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Toggle Red/Complete Flag")."  :"?></label>
				<td><div id="edit_item_toggle_flag"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Delete")."  :"?></label>
				<td><div id="delete">DEL</div></td>
			</tr>
			<tr>
				<th><label><?=_("Print")."  :"?></label>
				<td><div id="edit_item_print"></div></td>
			</tr>
		</table>
	</fieldset>
	
	<fieldset>
		<legend><?=_("For Mail")?></legend>
		<table class="shortcuts">
			<tr>
				<th><label><?=_("Save")."  :"?></label>
				<td><div id="mail_save"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Send")."  :"?></label>
				<td><div id="mail_send"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Reply")."  :"?></label>
				<td><div id="respond_mail_reply"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Reply All")."  :"?></label>
				<td><div id="respond_mail_replyall"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Forward")."  :"?></label>
				<td><div id="respond_mail_forward"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Toggle Reading Pane")."  :"?></label>
				<td><div id="readingpane_toggle"></div></td>
			</tr>
		</table>
	</fieldset>
	
	<fieldset>
		<legend><?=_("For Calendar")?></legend>
		<table class="shortcuts">
			<tr>
				<th><label><?=_("Accept Meeting Request")."  :"?></label>
				<td><div id="respond_meeting_accept"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Tentatively accept Meeting Request")."  :"?></label>
				<td><div id="respond_meeting_tentative"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Decline Meeting Request")."  :"?></label>
				<td><div id="respond_meeting_decline"></div></td>
			</tr>
		</table>
	</fieldset>
	
	<fieldset>
		<legend><?=_("Find Items")?></legend>
		<table class="shortcuts">
			<tr>
				<th><label><?=_("Enable/Disable Search Options")."  :"?></label>
				<td><div id="search_normal"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Open Advanced Search Dialog")."  :"?></label>
				<td><div id="search_advanced"></div></td>
			</tr>
		</table>
	</fieldset>
	
	<fieldset>
		<legend><?=_("For Folder")?></legend>
		<table class="shortcuts">
			<tr>
				<th><label><?=_("Refresh")."  :"?></label>
				<td><div id="refresh_folder"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Select All items")."  :"?></label>
				<td><div id="select_all_items"></div></td>
			</tr>
		</table>
	</fieldset>
	
	<fieldset>
		<legend><?=_("Multi User Calendar Options")?></legend>
		<table class="shortcuts">
			<tr>
				<th><label><?=_("Add User")."  :"?></label>
				<td><div id="muc_add_user"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Remove User")."  :"?></label>
				<td><div id="muc_remove_user"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Load Group")."  :"?></label>
				<td><div id="muc_load_group"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Previous Period")."  :"?></label>
				<td><div id="muc_previous_period"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Previous Day")."  :"?></label>
				<td><div id="muc_previous_day"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Next Day")."  :"?></label>
				<td><div id="muc_next_day"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Next Period")."  :"?></label>
				<td><div id="muc_next_period"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Zoom In")."  :"?></label>
				<td><div id="muc_zoom_in"></div></td>
			</tr>
			<tr>
				<th><label><?=_("Zoom Out")."  :"?></label>
				<td><div id="muc_zoom_out"></div></td>
			</tr>
			
		</table>
	</fieldset>
<?php }
?>
