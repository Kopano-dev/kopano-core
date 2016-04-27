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

function compose_settings_title(){
	return _("Compose mail");
}

function compose_settings_order(){
	return 1;
}

function compose_settings_html(){ ?>
	<fieldset>
		<legend><?=_("General")?></legend>
		<table class="textinput">
			<tr>
				<th><label for="compose_replyto"><?=_("Reply-to address")?></label></th>
				<td><input id="compose_replyto" type="text" class="text"></td>
			</tr>
		</table>
		<table class="options">
			<tr>
				<th><label for="compose_format"><?=_("Compose mail in this format")?></label></th>
				<td>
					<select id="compose_format" onchange="disableFontOption(this)">
						<option value="html"><?=_("HTML")?></option>
						<option value="plain"><?=_("Plain text")?></option>
					</select>
				</td>
			</tr>
			

			<tr>
				<th colspan="2"><input id="compose_close_on_reply" type="checkbox" class="checkbox"><label for="compose_close_on_reply"><?=_("Close original message on reply or forward")?></label></th>
			</tr>
			
			<tr>
				<th colspan="2"><input id="compose_readreceipt" type="checkbox" class="checkbox"><label for="compose_readreceipt"><?=_("Always request a read receipt")?></label></th>
			</tr>
			<tr>
				<th colspan="2"><input id="compose_autosave" type="checkbox" class="checkbox"><label for="compose_autosave"><?=_("AutoSave unsent every:")?></label> <input id="compose_autosave_interval" type="text" class="text" style="width: 25px;"> <?=_('minutes')?></th>
			</tr>

			<tr>
				<th><label for="mail_reply_prefix"><?=_("When replying a message")?></label></th>
				<td>
					<select id="mail_reply_prefix">
						<option value="include_original_message"><?=_("Include original message text")?></option>
						<option value="add_prefix"><?=_("Prefix each line of the original message")?></option>
					</select>
				</td>
			</tr>
			<tr>
				<th><label for="mail_forward_prefix"><?=_("When forwarding a message")?></label></th>
				<td>
					<select id="mail_forward_prefix">
						<option value="include_original_message"><?=_("Include original message text")?></option>
						<option value="add_prefix"><?=_("Prefix each line of the original message")?></option>
					</select>
				</td>
			</tr>

			<tr>
				<th><label for="compose_cursorposition"><?=_("Cursor position when replying")?></label></th>
				<td>
					<select id="compose_cursorposition">
						<option value="start"><?=_("Start of body")?></option>
						<option value="end"><?=_("End of body")?></option>
					</select>
				</td>
			</tr>

			<tr>
				<th><label for="compose_toccbcc_maxrows"><?=_("Maximum height of TO/CC/BCC fields")?></label></th>
				<td>
					<select id="compose_toccbcc_maxrows">
						<option value="1">1</option>
						<option value="2">2</option>
						<option value="3">3</option>
						<option value="4">4</option>
						<option value="5">5</option>
					</select>
				</td>
			</tr>

			<tr>
				<th><label for="compose_font"><?=_("Default font")?></label></th>
				<td>
					<select id="compose_font">
						<option value="Arial"><?=_("Arial")?></option>
						<option value="Comic Sans MS"><?=_("Comic Sans MS")?></option>
						<option value="Courier New"><?=_("Courier New")?></option>
						<option value="Tahoma"><?=_("Tahoma")?></option>
						<option value="Times New Roman"><?=_("Times New Roman")?></option>
						<option value="Verdana"><?=_("Verdana")?></option>
					</select>
				</td>
			</tr>
		</table>
	</fieldset>

	<fieldset>
		<legend><?=_("From Email Addresses")?></legend>
		<table class="textinput">
			<tr>
				<th><label for="compose_email_from_address"><?=_("Set FROM email addresses")?></label>
				<td>
					<table>
						<tr>
							<td style="width:425px"><select id="compose_email_from_address" class="input_text" multiple="multiple" size="5"></select></td>
							<td style="width:20px">
								<span class="menubutton icon icon_insert" id="compose_email_add_from" onclick="webclient.openModalDialog(-1, 'addemail', DIALOG_URL+'task=emailaddress_modal', 300,150, compose_addCallBack);" title="<?=_("Add address")?>"></span>
								<span class="menubutton icon icon_remove" style="clear: both;" id="compose_email_del_from" onclick="compose_delFromAddress();" title="<?=_("Remove address")?>"></span>
							</td>
						</tr>
					</table>
				</td>
			</tr>
		</table>
	</fieldset>

	<fieldset>
		<legend><?=_("Signature")?></legend>
		<table class="options">
			<tr>
				<th colspan="2">
					<input type="button" onclick="openEditSignatures();" value="<?=_('Edit signatures')?>..." class="inline_button"/>
				</th>
			</tr>
		</table>
	</fieldset>
<?php } 


?>
