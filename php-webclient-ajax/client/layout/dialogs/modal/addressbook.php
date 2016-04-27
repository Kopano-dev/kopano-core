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

function getModuleName() { 
	return "addressbooklistmodule"; 
}

function getModuleType() {
	return "list"; 
}

function getDialogTitle() {
	return _("Address Book");
}

function getIncludes(){
	return array(
			"client/modules/".getModuleName().".js",
			"client/layout/js/addressbook.js"
		);
}

function getJavaScript_onload(){ ?>
					var data = new Object();
					data["storeid"] = <?=get("storeid","false", "'", ID_REGEX)?>;
					data["dest"] = "<?=get("dest","anonymous", false, STRING_REGEX)?>";
					data["type"] = "<?=get("type","fullemail", false, STRING_REGEX)?>";
					data["source"] = "<?=get("source", "all", false, STRING_REGEX)?>"; // possible values: all, gab
					data["showsendmail"] = "<?=get("showsendmail","false", false, STRING_REGEX)?>"; // Show button for sending mail in addressbook.
					data["detailsonopen"] = ("<?=get("detailsonopen","false", false, STRING_REGEX)?>"=="true"); // Opens the GAB details when opening the GAB details

					if(typeof window.windowData != "undefined") {
						// for GAB & addresslists
						data["hide_companies"] = window.windowData["hide_companies"] || false;		// true, false
						data["hide_users"] = window.windowData["hide_users"] || false;				// active, non_active, room, equipment, contact, system
						data["hide_groups"] = window.windowData["hide_groups"] || false;			// normal, security, dynamic, everyone
						data["detailsonopen"] = window.windowData["detailsonopen"] || data["detailsonopen"];	// Opens the GAB details when opening the GAB details, can override the setting of the URL
					} else {
						// for contacts folders
						data["groups"] = "<?=get("groups", "yes", false, STRING_REGEX)?>";			// yes, no
					}

					// If dialog is opened from topmenubar of webaccess then show "New Message for Contact" button.
					if (data["showsendmail"] == "true")
						data["has_no_menu"] = true;

					module.init(moduleID, dhtml.getElementById("items"), false, data);
					module.setData(data);
					module.retrieveHierarchy();

					// Set module level GAB Pagination varialble whether it is available or not.
					<? if(defined('ENABLE_GAB_ALPHABETBAR') && (ENABLE_GAB_ALPHABETBAR == true)) { ?>
							module.enableGABPagination = true; 
                    <? } else { ?>
							module.enableGABPagination = false;
					<? } ?>

					dhtml.addEvent(module.id, dhtml.getElementById("name"), "keyup", addressBookSearchKeyInput);

					// Put focus on search field
					dhtml.getElementById("name").focus();

					changeAddressBookFolder();
<?
					// Get the array of items that should be shown at the bottom of the screen (To, Cc, Bcc, etc)
					$recipients = array();
					if (isset($_GET["fields"]) && is_array($_GET["fields"])){
						$recipients = $_GET["fields"];
					}
					
					echo "fields = new Array();\n";
					foreach($recipients as $key=>$value){
						if(preg_match_all(STRING_REGEX, $key, $matches)) {
							echo "fields.push('".addslashes($key)."');\n";
						}
					}

					echo "module.addEventHandler('openitem', false, onOpenItem);\n";
?>
					getAddressBookRecipients(fields);

					// If GAB pagination is set then show pagination.
					if(module.enableGABPagination)
						initPagination();
<?php } // getJavaScript_onload						

function getJavaScript_onresize(){ ?>
		var tableContentElement = dhtml.getElementById("divelement");

		// Get all dialog elemnts to get their offsetHeight.
		var titleElement = dhtml.getElementById("windowtitle");
		var menubarTitleElement = dhtml.getElementById("menubar");
		var searchElement = dhtml.getElementById("search");
		var tableHeaderElement = dhtml.getElementById("columnbackground");

		// If GAB pagination is set then show alfabet bar column.
		if(module.enableGABPagination) {
			var addressbookElement = dhtml.getElementById("addressbook");
			var alfabetbarElement = dhtml.getElementById("alfabet-bar");
			addressbookElement.style.width = document.documentElement.offsetWidth - 40 + "px";
			var availableSpace = document.documentElement.offsetHeight - alfabetbarElement.offsetHeight - titleElement.offsetHeight - menubarTitleElement.offsetHeight - 8;
			var alfabetbarElementTop = 54;
			
			if (availableSpace > 0)
				alfabetbarElementTop += availableSpace/2;

			alfabetbarElementTop = (alfabetbarElementTop > 100) ? 100 : alfabetbarElementTop;

			// Show alfabetbar column in middle of dialog.
			alfabetbarElement.style.top = alfabetbarElementTop + "px";
		}

		var dialogContentElement = dhtml.getElementById("dialog_content");
		//var toButtonElement = dhtml.getElementsByClassNameInElement(dialogContentElement, "button", "input");
		var recipientsInputTableElement = dhtml.getElementById("recipientsinputtable");
		var confirmButtonsElement = dhtml.getElementsByClassNameInElement(dialogContentElement, "confirmbuttons", "div");

		// Count the height for table contents.
		var tableContentElementHeight = document.documentElement.offsetHeight - titleElement.offsetHeight - menubarTitleElement.offsetHeight - searchElement.offsetHeight - tableHeaderElement.offsetHeight - confirmButtonsElement[0].offsetHeight - recipientsInputTableElement.offsetHeight - 60;
		
		if(tableContentElementHeight < 50)
			tableContentElementHeight = 50;

		// Set the height for table contents.
		tableContentElement.style.height = tableContentElementHeight +"px";
<?php } // getJavaScript_onresize	

function getJavaScript_other(){ ?>
	var fields;
	var searchKeyInputTimer;
	
	
<?php } // getJavaScript_other

function getBody(){ ?>
		<div id="addressbook">
			<table id="search" width="100%" border="0" cellspacing="0" cellpadding="0">
				<tr>
					<td class="propertybold" colspan="2" nowrap><?=_("Type Name")?>:</td>
					<td class="propertybold" nowrap><?=_("Show names from the")?>:</td>
				</tr>
				<tr>
					<td width="150">
						<input id="name" class="fieldsize" type="text" value="" size="29" autocomplete="off">
					</td>
					<td width="400">
						<div class="searchbutton" onmouseover="this.className+=' searchbuttonover';" onmouseout="this.className='searchbutton';" onclick="searchAddressBook();">&nbsp;</div>
					</td>
					<td>
						<select id="addressbookfolders" class="comboboxwidth" onchange="changeAddressBookFolder(this);">
						</select>
					</td>
				</tr>
			</table>
			
			<div id="items" onmousedown="return eventCheckScrolling(event);">
				
			</div>
			
			<table id="recipientsinputtable" width="100%" cellspacing="0" cellpadding="1">
				<?	
					$recipients = array();
					if (isset($_GET["fields"]) && is_array($_GET["fields"])){
						$fields = $_GET["fields"];
						foreach($fields as $key=>$value){
							/**
							 * Here the value of fields[key] comes in high characters (due to translations), 
							 * which is not able to be passed through STRING_REGEX, so we are using a REGEX 
							 * which will stop XSS and pass the value in regular expression match.
							 * @TODO: // create a REGEX for high characters parsing.
							 */
							if(preg_match_all(STRING_REGEX, $key, $matches) && preg_match_all(FILENAME_REGEX, $value, $matches))
								$recipients[$key] = $value;
						}
					}else{
						$recipients = array("anonymous" => "anonymous");
					}


					foreach($recipients as $key => $recipient)
					{
					    if($key != "anonymous") {
				?>
					<tr>
						<td class="propertynormal propertywidth" align="center">
							<input class="button" type="button" value="<?=htmlentities($recipient, ENT_QUOTES, 'UTF-8')?>..." onclick="addSelectedContacts('<?=addslashes($key)?>');">
						</td>
						<td>
							<input id="<?=htmlentities($key, ENT_QUOTES, 'UTF-8')?>" class="field" type="text">
						</td>
					</tr>
				<?
				        } else {
				            ?>
				            <input id="<?=htmlentities($key, ENT_QUOTES, 'UTF-8')?>" class="field" type="hidden">
				            <?
                        }
					}
				?>
			</table>
			<div id="alfabet-bar" class="alfabet alfabet_bar"></div>
			
			<?=createConfirmButtons("addressBookSubmit();window.close();")?>
		</div>
<?php } // getBody

function getMenuButtons(){
	return array(
			array(
				'id'=>"newmessagecontact",
				'name'=>'',
				'title'=>_("New Message for Contact"),
				'callback'=>'function(){eventABSendMailTo();}'
			)
		);
} // getMenuButtons
?>
