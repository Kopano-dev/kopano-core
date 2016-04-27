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

function addressbook_settings_title(){
	return _("Addressbook");
}

function addressbook_settings_order(){
	return 4;
}

function addressbook_settings_html(){ ?>
	<fieldset>
		<legend><?=_("Addressbook")?></legend>

		<table class="options">
			<tr>
				<th><label for="addressbook_folder"><?=_("Default selected folder/addressbook")?></label></th>
				<td>
					<select id="addressbook_folder" class="input_text">
					</select>
				</td>
			</tr>
		</table>
	</fieldset>

<?php } ?>
