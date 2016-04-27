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
	return _("Field Chooser");
}

function getIncludes(){
	return array(
			"client/layout/js/fieldchooser.js",
		);
}

function getJavaScript_onload(){ ?>
					initFieldChooser();
<?php } // getJavaScript_onload						

function getBody(){ ?>
		<div id="fieldchooser">
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td width="38%">
						<?=_("Available fields")?>:
					</td>
					<td width="23%">&nbsp;</td>
					<td width="38%">
						<?=_("Selected fields")?>:
					</td>
				</tr>
				<tr>
					<td>
						<select id="columns" class="combobox" size="16" style="width:100%"></select>
					</td>
					<td valign="top" align="center">
						<input type="button" class="buttonsize add" value="<?=_("Add")?>" onclick="addColumn();">
						<input type="button" class="buttonsize delete" value="<?=_("Delete")?>" onclick="deleteColumn();">
					</td>
					<td>
						<select id="selected_columns" class="combobox" size="16" style="width:100%"></select>
					</td>
				</tr>
				<tr>
					<td>&nbsp;</td>
					<td>&nbsp;</td>
					<td align="center">
						<input type="button" class="buttonsize" value="<?=_("Up")?>" onclick="columnUp();">
						<input type="button" class="buttonsize" value="<?=_("Down")?>" onclick="columnDown();">
					</td>
				</tr>
			</table>
		</div>
		
		<?=createConfirmButtons("submitFields();window.close();")?>
<?php } // getBody
?>
