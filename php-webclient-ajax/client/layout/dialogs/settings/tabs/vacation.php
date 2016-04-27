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

function vacation_settings_title(){
	return _("Out of Office");
}

function vacation_settings_order(){
	return 2;
}

function vacation_settings_html(){ ?>
	<fieldset>
		<legend><?=_("Out of Office Assistant")?></legend>
		<table class="options">
			<tr>
				<th colspan="2"><input id="vacation_notsignable" type="radio" name="vacation_option" onclick="vacation_signableChange();" class="checkbox"><label for="vacation_notsignable"><?=_("I am currently In the Office")?></label></th>
			</tr>
			<tr>
				<th colspan="2"><input id="vacation_signable" type="radio" name="vacation_option" onclick="vacation_signableChange();" class="checkbox"><label for="vacation_signable"><?=_("I am currently Out of the Office")?></label></th>
			</tr>
		</table>
		<table class="textinput" style="margin-top: 5px">
			<tr>
				<th><label for="vacation_subject"><?=_("Subject")?>:</label></th>
				<td><input id="vacation_subject" type="text" class="text"></td>
			</tr>
			<tr>
				<th colspan="2"><label for="vacation_message"><?=_("AutoReply only once to each sender with the following text")?>:</label></th>
			</tr>
			<tr>
				<th>&nbsp;</th>
				<td><textarea id="vacation_message" rows="6" cols="60"></textarea></td>
			</tr>
		</table>
	</fieldset>
<?php } ?>
