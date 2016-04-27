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
	return "stickynoteitemmodule";
}

function getModuleType(){
	return "item";
}

function getDialogTitle(){
	return _("Note");
}

function getIncludes(){
	return array(
			"client/layout/js/stickynote.js",
			"client/modules/".getModuleName().".js"
		);
}

function getJavaScript_onload(){ ?>
					module.init(moduleID);
					module.setData(<?=get("storeid","false","'", ID_REGEX)?>, <?=get("parententryid","false","'", ID_REGEX)?>);

					var attachNum = false;
					<? if(isset($_GET["attachNum"]) && is_array($_GET["attachNum"])) { ?>
						attachNum = new Array();
					
						<? foreach($_GET["attachNum"] as $attachNum) { 
							if(preg_match_all(NUMERIC_REGEX, $attachNum, $matches)) {
							?>
								attachNum.push(<?=intval($attachNum)?>);
						<?	}
						} ?>
					
					<? } ?>
					module.open(<?=get("entryid","false","'", ID_REGEX)?>, <?=get("rootentryid","false","'", ID_REGEX)?>, attachNum);
					
					resizeBody();
					
					dhtml.addEvent(false, dhtml.getElementById("html_body"), "contextmenu", forceDefaultActionEvent);

					// check if we need to send the request to convert the selected message as stickyNotes
					if(window.windowData && window.windowData["action"] == "convert_item") {
						module.sendConversionItemData(windowData);
					}

					//set focus on body of the note
					dhtml.getElementById("html_body").focus();
<?php } // getJavaSctipt_onload
			
function getBody() { ?>
		<input id="entryid" type="hidden">
		<input id="parent_entryid" type="hidden">
		<input id="message_class" type="hidden" value="IPM.StickyNote">
		<input id="icon_index" type="hidden" value="771">
		<input id="color" type="hidden" value="3">
		<input id="subject" type="hidden" value="">
		
		<div id="conflict"></div>
		
		<div class="stickynote_color">
			<table border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertybold" width="60" nowrap>
						<?=_("Color")?>:
					</td>
					<td>
						<select id="select_color" class="combobox" onchange="onChangeColor();">
							<option value="0"><?=_("Blue")?></option>
							<option value="1"><?=_("Green")?></option>
							<option value="2"><?=_("Pink")?></option>
							<option value="3" selected><?=_("Yellow")?></option>
							<option value="4"><?=_("White")?></option>
						</select>
					</td>
				</tr>
			</table>
		</div>
		
		<textarea id="html_body" class="stickynote_yellow" cols="60" rows="12"></textarea>
<?php } // getBody

function getMenuButtons(){
	return array(
			array(
				'id'=>"save",
				'name'=>_("Save"),
				'title'=>_("Save"),
				'callback'=>'submitStickyNote'
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"delete",
				'name'=>"",
				'title'=>_("Delete"),
				'callback'=>"function(){delete_item()}"
			)
		);
}

?>
