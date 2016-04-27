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
	return "ruleslistmodule";
}

function getModuleType(){
	return "list";
}

function getDialogTitle() {
	return _("Rules");
}

function getIncludes(){
	return array(
			"client/layout/js/rules.js",
			"client/layout/css/rules.css",
			"client/modules/".getModuleName().".js"
		);
}

function getJavaScript_onload(){ ?>
		var data = new Object;
		data.storeid = "<?=get("storeid", "", false, ID_REGEX)?>";
		module.init(moduleID, dhtml.getElementById("rules"), false, data);
		module.setData(data);
		module.list();

		module.addEventHandler("openitem", null, eventRulesOpenItem);
		
<?php } // getJavaScript_onload

function getJavaScript_onresize(){ ?>
		var tableContentElement = dhtml.getElementById("divelement");

		// Get all dialog elemnts to get their offsetHeight.
		var titleElement = dhtml.getElementById("windowtitle");
		var subTitleElement = dhtml.getElementById("subtitle");
		var tableHeaderElement = dhtml.getElementById("columnbackground");

		// Count the height for table contents.
		var tableContentElementHeight = dhtml.getBrowserInnerSize()["y"] - titleElement.offsetHeight - subTitleElement.offsetHeight - tableHeaderElement.offsetHeight - 80;

		if(tableContentElementHeight < 110)
			tableContentElementHeight = 110;

		// Set the height for table contents.
		tableContentElement.style.height = tableContentElementHeight +"px";
<?php } // getJavaScript_onresize

function getBody(){ ?>
		<div id="ruleslist">
			<?=_("The following rules have been defined:")?>
			<table>
				<tr>
					<td width="90%">
					<div id="rules" onmousedown="return false;">
					</div>
					</td>
					<td id="rulebuttons" width="10%">
					<input class="buttonsize rulebutton" type="button" value="<?=_("New") . "..."?>" onclick="addRule();"/><br/>
					<input class="buttonsize rulebutton" type="button" value="<?=_("Edit") . "..."?>" onclick="editRule();"/><br/>
					<input class="buttonsize rulebutton" type="button" value="<?=_("Delete")?>" onclick="deleteRule();"/><br/>
					<p>
					<input class="buttonsize rulebutton" type="button" value="<?=_("Up")?>" onclick="moveUp();"/><br/>
					<input class="buttonsize rulebutton" type="button" value="<?=_("Down")?>" onclick="moveDown();"/><br/>
					</td>
				</tr>
			</table>
		</div>
		
		<?=createConfirmButtons("if(submitRules()) window.close(); else window.focus();")?>
<?php } // getBody
?>
