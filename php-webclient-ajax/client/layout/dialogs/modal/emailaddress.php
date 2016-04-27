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

function getDialogTitle(){
	return _("New e-mail address");
}

function getIncludes() {
	$includes = array("client/layout/js/emailaddress.js");
	return $includes;
}

function getJavaScript_onload(){ ?>
	var internalId = "<?=get("internalid", "", false, ID_REGEX)?>";
	if(internalId != "") {
		var module = parentwindow.module;
		dhtml.getElementById("name").value = module.itemProps[internalId].name;
		dhtml.getElementById("email").value = module.itemProps[internalId].address;
		dhtml.getElementById("internalId").value = internalId;
	}
<?php } // getJavaScript_onload

function getBody() { ?>
	<input id="internalId" type="hidden" value="">
	<table class="options">
		<tr>
			<th><?=_("Name")?></th>
			<td><input type="text" class="text" id="name"></td>
		</tr>

		<tr>
			<th><?=_("Email Address")?></th>
			<td><input type="text" class="text" id="email"></td>
		</tr>
	</table>
	<?=createConfirmButtons("if(emailAddressSubmit()) window.close();")?>
<?php } // getBody
?>
