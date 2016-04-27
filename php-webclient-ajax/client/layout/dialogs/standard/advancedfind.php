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
function initWindow(){

}

function getModuleName(){
	return "AdvancedFindListModule";
}

function getModuleType(){
	return "list";
}

function getDialogTitle(){
	return _("Advanced Find");
}

function getIncludes(){
	return array(
			"client/layout/css/tabbar.css",
			"client/layout/css/advancedfind.css",
			"client/layout/css/searchcriteria.css",
			"client/layout/css/date-picker.css",
			"client/layout/js/date-picker.js",
			"client/layout/js/date-picker-language.js",
			"client/layout/js/date-picker-setup.js",
			"client/widgets/pagination.js",
			"client/widgets/tablewidget.js",
			"client/widgets/tabswidget.js",
			"client/widgets/searchcriteria.js",
			"client/layout/js/advancedfind.js",
			"client/modules/".strtolower(getModuleName()).".js"
		);
}

function getJavaScript_other(){ ?>

<?php } // getJavaScript_other

function getJavaScript_onload(){ ?>
					// general
					var data = new Object();
					data["storeid"] = "<?=get("storeid", "false", false, ID_REGEX)?>";
					data["entryid"] = "<?=get("entryid", "false", false, ID_REGEX)?>";

					module.init(moduleID, dhtml.getElementById("advanced_find"), false, data);
					module.setData(data);

					// create paging element
					advFindCreatePagingElement(moduleID, dhtml.getElementById("advanced_find"));

					resizeBody();
<?php } // getJavaScript_onload

function getJavaScript_onbeforeunload(){ ?>
						if(module.destructor) {
							// Call destructor before unloading dialog
							module.destructor();
						}
<?php } // getJavaScript_onbeforeunload

function getBody() { ?>
	<div id="advanced_find">
		<div id="search_target_selector">
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tbody>
					<tr id="type_location_selector">
						<td width="10%">
							<?=_("Look for");?>:
						</td>
						<td width="20%">
							<select id="message_type_selector" class="combobox" onchange="eventAdvFindChangeMessageType(module, this, event);">
								<option value="any_item"><?=_("Any type of item")?></option>
								<option value="appointments"><?=_("Appointments and Meetings")?></option>
								<option value="contacts"><?=_("Contacts")?></option>
								<option value="messages"><?=_("Messages")?></option>
								<option value="notes"><?=_("Notes")?></option>
								<option value="tasks"><?=_("Tasks")?></option>
							</select>
						</td>
						<td width="4%" align="center">
							<?=_("In")?>:
						</td>
						<td width="66%">
							<div id="search_location">
						</td>
					</tr>
				</tbody>
			</table>
		</div>

		<div id="advanced_find_searchwidget"></div>

		<div id="action_buttons">
			<table width="100%" border="0" cellpadding="0" cellspacing="1">
				<tbody>
					<tr>
						<td>
							<button id="folder_selector_button" onclick="eventAdvFindShowFolderSelectionDialog(module, this, event);" class="button action_button">
								<?=_("Browse");?>...
							</button>
						</td>
					</tr>
					<tr>
						<td>
							<button id="search_start_button" onclick="eventAdvFindStartSearch(module, this, event);" class="button action_button">
								<?=_("Find Now");?>
							</button>
						</td>
					</tr>
					<tr>
						<td>
							<button id="search_stop_button" onclick="eventAdvFindStopSearch(module, this, event);" disabled class="button action_button">
								<?=_("Stop");?>
							</button>
						</td>
					</tr>
					<tr>
						<td>
							<button id="search_reset_button" onclick="eventAdvFindNewSearch(module, this, event);" class="button action_button">
								<?=_("New Search");?>
							</button>
						</td>
					</tr>
					<tr>
						<td>
							<div id="search_indicator"></div>
						</td>
					</tr>
				</tbody>
			</table>
		</div>

		<div id="advanced_find_view">
			<div id="advanced_find_paging"></div>
			<div id="advanced_find_view_resizebar"></div>
			<div id="advanced_find_tablewidget"></div>
			<div id="advanced_find_itemcount"></div>
		</div>
	</div>
<?php
} // getBody
?>