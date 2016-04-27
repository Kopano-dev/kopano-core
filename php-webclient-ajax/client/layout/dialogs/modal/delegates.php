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
	return _("Delegates");
}
function getIncludes(){
	$includes = array(
		"client/modules/".getModuleName().".js",
		"client/layout/js/delegates.js",
		"client/layout/css/delegates.css",
		"client/widgets/tablewidget.js"
	);
	return $includes;
}
function getJavaScript_onload(){
?>
	var data = new Object();
	// We need storeid to open GAB.
	data["storeid"] = parentWebclient.hierarchy.defaultstore.id;
	module.init(moduleID, data);
	module.getDelegates();
	initDelegates();
	
	// add events
	dhtml.addEvent(module, dhtml.getElementById("add_delegate"), "click", eventAddDelegate);
	dhtml.addEvent(module, dhtml.getElementById("remove_delegate"), "click", eventRemoveDelegate);
	dhtml.addEvent(module, dhtml.getElementById("edit_permissions"), "click", eventEditDelegatePermissions);
<?php }  // getJavaScript_onload

function getJavaScript_other(){
?>
	var delegatesTable;
<?php } //getJavaScript_other(){

function getBody(){ ?>
	<div class="delegates delegates_info">
		<label><?=_("Delegates can send items on your behalf, and optionally view or edit items in your store. This is setup by the 'Edit Delegates...' button on the Preferences tab of the Settings window. To enable others to access your folders without giving send-on-behalf-of privileges, is configured on the Permissions tab of the folder Preferences (accessed by right clicking the folder).")?></label>
	</div>
	<div class="delegateslist">
		<div id="delegateslist_container" class="delegates delegateslist_container"></div>
	</div>
	<div class="delegates_options">
		<table cellpadding="0px" cellspacing="0px" width="100%" align="right">
			<tr>
				<td>
					<input id="add_delegate" type="button"  class="buttonsize" value="<?=_("Add")?>...">
				</td>
			</tr>
			<tr>
				<td>
					<input id="remove_delegate" type="button"  class="buttonsize" value="<?=_("Remove")?>...">
				</td>
			</tr>
			<tr>
				<td>
					<input id="edit_permissions" type="button"  class="buttonsize" value="<?=_("Permissions")?>...">
				</td>
			</tr>
		</table>
	</div>
	<div class="delegates">&nbsp;</div>
<?php
	print createConfirmButtons("submitDelegates();"); 
} // getBody
?>