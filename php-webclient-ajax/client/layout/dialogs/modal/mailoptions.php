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
	return _("Mail Options");
}

function getIncludes(){
	$includes = array(
			"client/layout/js/mailoptions.js"
	);
	return $includes;
}

function getJavaScript_onload(){ ?>
					getMailOptions();
<?php } // getJavaScript_onload						

function getBody(){ ?>
		<div>
			<div class="propertytitle"><?=_("Message Settings")?></div>
			<table cellpadding="2" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth" nowrap>
						<?=_("Importance")?>:
					</td>
					<td>
						<select id="importance" class="combobox">
							<option value="0"><?=_("Low")?></option>
							<option value="1" selected><?=_("Normal")?></option>
							<option value="2"><?=_("High")?></option>
						</select>
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth" nowrap>
						<?=_("Sensitivity")?>:
					</td>
					<td>
						<select id="sensitivity" class="combobox">
							<option value="0" selected><?=_("Normal")?></option>
							<option value="1"><?=_("Personal")?></option>
							<option value="2"><?=_("Private")?></option>
							<option value="3"><?=_("Confidential")?></option>
						</select>
					</td>
				</tr>
			</table>
			
			<div class="propertytitle"><?=_("Tracking Options")?></div>
			<table cellpadding="2" cellspacing="0">
				<tr>
					<td width="25">
						<input id="read_receipt" type="checkbox">
					</td>
					<td class="propertynormal" nowrap onclick="changeCheckBoxStatus('read_receipt');">
						<?=_("Request a read receipt for this message")?>.
					</td>
				</tr>
			</table>
			
			<?=createConfirmButtons("submitMailOptions();window.close();")?>
		</div>
<?php } // getBody
?>
