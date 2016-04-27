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

require("client/layout/tabbar.class.php");

function initWindow(){
	global $tabbar, $tabs;

	$tabs = array("general" => _("General"), "details" => _("Details"));
	$tabbar = new TabBar($tabs, key($tabs));
}


function getModuleName(){
	return "contactitemmodule";
}

function getModuleType(){
	return "item";
}

function getDialogTitle(){
	return _("Contact");
}

function getIncludes(){
	return array(
			"client/layout/css/tabbar.css",
			"client/layout/css/contact.css",
			"client/layout/js/tabbar.js",
			"client/layout/js/date-picker.js",
			"client/layout/js/date-picker-language.js",
			"client/layout/js/date-picker-setup.js",
			"client/layout/css/date-picker.css",
			"client/layout/js/contact.js",
			"client/modules/".getModuleName().".js"
		);
}

function getJavaScript_onload(){ 
	global $tabbar;
	
	$tabbar->initJavascript("tabbar", "\t\t\t\t\t");

?>
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
					module.contactDialogOpen = true;
					
					Calendar.setup({
						inputField	:	"text_birthday",				// id of the input field
						ifFormat	:	_('%d-%m-%Y'),					// format of the input field
						button		:	"text_birthday_button",		// trigger for the calendar (button ID)
						step		:	1,							// show all years in drop-down boxes (instead of every other year as default)
						weekNumbers	:	false
					});
					
					Calendar.setup({
						inputField	:	"text_wedding_anniversary",			// id of the input field
						ifFormat	:	_('%d-%m-%Y'),					// format of the input field
						button		:	"text_wedding_anniversary_button",	// trigger for the calendar (button ID)
						step		:	1,							// show all years in drop-down boxes (instead of every other year as default)
						weekNumbers	:	false
					});
					
					resizeBody();
					
					var inputElements = window.document.getElementsByTagName("input");
					for(var i=0 ; i < inputElements.length; i++) {
						dhtml.addEvent(false, inputElements[i], "contextmenu", forceDefaultActionEvent);
					}
					var textareaElements = window.document.getElementsByTagName("textarea");
					for(var i=0 ; i < textareaElements.length; i++) {
						dhtml.addEvent(false, textareaElements[i], "contextmenu", forceDefaultActionEvent);
					}
					dhtml.addEvent(false, dhtml.getElementById("html_body"), "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(module, dhtml.getElementById("categories"), "blur", eventFilterCategories);

					//explicitly added onchange event on every datepicker object, to validate date entered
					dhtml.addEvent(false, dhtml.getElementById("text_birthday"), "change", eventDateInputChange);
					dhtml.addEvent(false, dhtml.getElementById("text_wedding_anniversary"), "change", eventDateInputChange);

<?php	if (isset($_GET["address"])){ ?>
					var address = parseEmailAddress(decodeURI("<?=get("address", STRING_REGEX)?>"));
					if (address){
						if (typeof(address.displayname) == "string"){
							dhtml.getElementById("display_name").value = address.displayname.trim();
							setFileAs();
						}
						if (typeof(address.emailaddress) == "string"){
							dhtml.getElementById("email_address").value = address.emailaddress;
							onUpdateEmailAddress();
						}
					}
<?php	} ?>
					// check if we need to send the request to convert the selected message as contact
					if(window.windowData && window.windowData["action"] == "convert_item") {
						module.sendConversionItemData(windowData);
					}

					// Set focus on display name field.
					dhtml.getElementById("display_name").focus();

<?php } // getJavaSctipt_onload						

function getJavaScript_other(){
?>
	window.onbeforeunload = function(){
		// for IE flag is set to not open detailed dialog.
		module.contactDialogOpen = false;
		// before closing contacts dialog, close all child dialogs.
		for(var dialogname in webclient.dialogs) {
			if(!window.BROWSER_IE) {
				if(typeof webclient.dialogs[dialogname].window == "undefined" || webclient.dialogs[dialogname].window.closed == false) {
					webclient.dialogs[dialogname].window.close();
				}
			}
		}
	}
<?php 
}	// getJavaSctipt_other

function getBody() { 
	global $tabbar, $tabs;
	
	$tabbar->createTabs();
	$tabbar->beginTab("general");
?>
		<input id="entryid" type="hidden">
		<input id="parent_entryid" type="hidden">
		<input id="message_class" type="hidden" value="IPM.Contact">
		<input id="icon_index" type="hidden" value="512">
		<input id="fileas" type="hidden" value="">
		<input id="fileas_selection" type="hidden" value="-1">
		<input id="email_address_1" type="hidden" value="">
		<input id="email_address_display_name_1" type="hidden" value="">
		<input id="email_address_2" type="hidden" value="">
		<input id="email_address_display_name_2" type="hidden" value="">
		<input id="email_address_3" type="hidden" value="">
		<input id="email_address_display_name_3" type="hidden" value="">
		<input id="birthday" type="hidden" value="">
		<input id="wedding_anniversary" type="hidden" value="">
		<input id="sensitivity" type="hidden" value="0">
		<input id="private" type="hidden" value="-1">
		<input id="contacts_string" type="hidden" value="">

		<!-- hidden fields for detailed full name info -->
		<input id="given_name" type="hidden" value="">
		<input id="middle_name" type="hidden" value="">
		<input id="surname" type="hidden" value="">

		<!-- hidden fields for detailed address info -->
		<input id="business_address" type="hidden" value="">
		<input id="business_address_street" type="hidden" value="">
		<input id="business_address_city" type="hidden" value="">
		<input id="business_address_state" type="hidden" value="">
		<input id="business_address_postal_code" type="hidden" value="">
		<input id="business_address_country" type="hidden" value="">
		<input id="home_address" type="hidden" value="">
		<input id="home_address_street" type="hidden" value="">
		<input id="home_address_city" type="hidden" value="">
		<input id="home_address_state" type="hidden" value="">
		<input id="home_address_postal_code" type="hidden" value="">
		<input id="home_address_country" type="hidden" value="">
		<input id="other_address" type="hidden" value="">
		<input id="other_address_street" type="hidden" value="">
		<input id="other_address_city" type="hidden" value="">
		<input id="other_address_state" type="hidden" value="">
		<input id="other_address_postal_code" type="hidden" value="">
		<input id="other_address_country" type="hidden" value="">
		<input id="mailing_address" type="hidden" value="">

		<!-- hidden fields for detailed phone info -->
		<input id="assistant_telephone_number" type="hidden" value="">
		<input id="business2_telephone_number" type="hidden" value="">
		<input id="office_telephone_number" type="hidden" value="">
		<input id="callback_telephone_number" type="hidden" value="">
		<input id="car_telephone_number" type="hidden" value="">
		<input id="company_telephone_number" type="hidden" value="">
		<input id="home_telephone_number" type="hidden" value="">
		<input id="home2_telephone_number" type="hidden" value="">
		<input id="cellular_telephone_number" type="hidden" value="">
		<input id="other_telephone_number" type="hidden" value="">
		<input id="pager_telephone_number" type="hidden" value="">
		<input id="primary_telephone_number" type="hidden" value="">
		<input id="radio_telephone_number" type="hidden" value="">
		<input id="telex_telephone_number" type="hidden" value="">
		<input id="ttytdd_telephone_number" type="hidden" value="">
		<input id="isdn_number" type="hidden" value="">
		<input id="home_fax_number" type="hidden" value="">
		<input id="primary_fax_number" type="hidden" value="">
		<input id="business_fax_number" type="hidden" value="">
		<!-- by default selected phone types -->
		<input id="selected_phone_1" type="hidden" value="office">
		<input id="selected_phone_2" type="hidden" value="home">
		<input id="selected_phone_3" type="hidden" value="business_fax">
		<input id="selected_phone_4" type="hidden" value="cellular">
		<!-- hidden fields for special date appointment-->
		<input id="birthday_eventid" type="hidden" value="">
		<input id="anniversary_eventid" type="hidden" value="">

		<div id="conflict"></div>
		
		<div class="contact_left">
			<fieldset class="contact_fieldset">
				<legend><?=_("Name")?></legend>
				
				<table width="100%" border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td class="propertynormal propertywidth">
							<input id="fullname_button" type="button" class="button" value="<?=_("Full Name")?>:" onclick="eventShowDetailFullNameDialog(module);">
						</td>
						<td>
							<input id="display_name" class="field" type="text" onchange="module.parseDetailedInfo(this.value, 'full_name', false);">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("Function")?>:
						</td>
						<td>
							<input id="title" class="field" type="text">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("Company")?>:
						</td>
						<td>
							<input id="company_name" class="field" type="text" onchange="setFileAs();">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("File as")?>:
						</td>
						<td>
							<select id="select_fileas" class="combobox" style="width:100%"></select>
						</td>
					</tr>
				</table>
			</fieldset>
	
			<fieldset class="contact_fieldset">
				<legend><?=_("Phone numbers")?></legend>
				
				<table width="100%" border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td class="propertynormal propertywidth">
							<input id="telephone_button_1" type="button" class="combobutton_main" value="<?=_("Business")?>:" onclick="eventShowDetailPhoneNumberDialog(module, this);"><button type="button" class="combobutton_menu" onclick="eventShowPhoneTypeMenu(module, this, event);"><img src="client/layout/img/arrow.gif" /></button>
						</td>
						<td>
							<input id="telephone_number_1" class="field" type="text" onchange="updatePhoneNumberFields();">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<input id="telephone_button_2" type="button" class="combobutton_main" value="<?=_("Home")?>:" onclick="eventShowDetailPhoneNumberDialog(module, this);"><button type="button" class="combobutton_menu" onclick="eventShowPhoneTypeMenu(module, this, event);"><img src="client/layout/img/arrow.gif" /></button>
						</td>
						<td>
							<input id="telephone_number_2" class="field" type="text" onchange="updatePhoneNumberFields();">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<input id="telephone_button_3" type="button" class="combobutton_main" value="<?=_("Business Fax")?>:" onclick="eventShowDetailPhoneNumberDialog(module, this);"><button type="button" class="combobutton_menu" onclick="eventShowPhoneTypeMenu(module, this, event);"><img src="client/layout/img/arrow.gif" /></button>
						</td>
						<td>
							<input id="telephone_number_3" class="field" type="text" onchange="updatePhoneNumberFields();">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<input id="telephone_button_4" type="button" class="combobutton_main" value="<?=_("Mobile")?>:" onclick="eventShowDetailPhoneNumberDialog(module, this);"><button type="button" class="combobutton_menu" onclick="eventShowPhoneTypeMenu(module, this, event);"><img src="client/layout/img/arrow.gif" /></button>
						</td>
						<td>
							<input id="telephone_number_4" class="field" type="text" onchange="updatePhoneNumberFields();">
						</td>
					</tr>
				</table>
			</fieldset>
			
			<fieldset class="contact_fieldset">
				<legend><?=_("Addresses")?></legend>
				
				<table width="100%" border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td class="propertynormal propertywidth">
							<select id="select_address_combo" class="combobox" onchange="eventOnChangeAddressType(module);">
								<option value="home"><?=_("Home")?></option>
								<option value="business" selected><?=_("Business")?></option>
								<option value="other"><?=_("Other")?></option>
							</select>
							<input id="selected_address" type="hidden" value="2">
						</td>
						<td valign="top">
							<input id="checkbox_mailing_address" type="checkbox" onchange="onChangeMailingAddress(this);">
							<label for="checkbox_mailing_address"><?=_("Use this address for mailing")?>.</label>
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("Street")?>:
						</td>
						<td>
							<textarea id="address_street" class="contact_address_street"></textarea>
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("City")?>:
						</td>
						<td>
							<input id="address_city" class="field" type="text">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("State/Province")?>:
						</td>
						<td>
							<input id="address_state" class="field" type="text">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("ZIP/Postal code")?>:
						</td>
						<td>
							<input id="address_postal_code" class="field" type="text">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("Country/Region")?>:
						</td>
						<td>
							<input id="address_country" class="field" type="text">
						</td>
					</tr>
				</table>
			</fieldset>
		</div>
		
		<div class="contact_right">
			<fieldset class="contact_fieldset">
				<legend><?=_("Photo")?></legend>
				<table width="100%" border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td class="propertynormal propertywidth">
							<div id="contactphoto"></div>
						</td>
					</tr>
				</table>
			</fieldset>
			<fieldset class="contact_fieldset">
				<legend><?=_("Email")?></legend>
				
				<table width="100%" border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td class="propertynormal propertywidth">
							<select id="select_email_address" class="combobox" onchange="onChangeEmailAddress();">
								<option value="1"><?=_("Email")?></option>
								<option value="2"><?=_("Email") . " 2"?></option>
								<option value="3"><?=_("Email") . " 3"?></option>
							</select>
							
							<input id="selected_email_address" type="hidden" value="1">
						</td>
						<td>
							<input id="email_address" class="field" type="text" onchange="onUpdateEmailAddress();">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("Display Name")?>:
						</td>
						<td>
							<input id="email_address_display_name" class="field" type="text" onchange="onUpdateEmailAddressDisplayName();">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("Webpage")?>:
						</td>
						<td>
							<input id="webpage" class="field" type="text">
						</td>
					</tr>
					<tr>
						<td class="propertynormal propertywidth">
							<?=_("IM - Address")?>:
						</td>
						<td>
							<input id="im" class="field" type="text">
						</td>
					</tr>
				</table>
			</fieldset>
			
			<fieldset class="contact_fieldset">
				<legend><?=_("Additional Information")?></legend>
				
				<textarea id="html_body"></textarea>
			</fieldset>
			<div id = "attachfieldcontainer">
			<fieldset class="contact_fieldset">
				<legend><?=_("Attachments")?></legend>
				
				<table width="100%" border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td class="propertynormal propertywidth" valign="top">
							<input class="button" type="button" value="<?=_("Attachments")?>:" onclick="webclient.openWindow(module, 'attachments', DIALOG_URL+'task=attachments_modal&store=' + module.storeid + '&entryid=' + (module.messageentryid?module.messageentryid:'') + '&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value, 570, 425, '0');">
						</td>
						<td valign="top">
							<div id="itemattachments">&nbsp;</div>
						</td>
					</tr>
				</table>
			</fieldset>
			</div>
		</div>
		
		<div id="categoriesbar">
			<table width="100%" border="0" cellpadding="2" cellspacing="0" style="table-layout: fixed;">
				<tr>
					<td class="propertynormal propertywidth">
						<input class="button" type="button" value="<?=_("Contacts")?>:" onclick="webclient.openModalDialog(module, 'addressbook', DIALOG_URL+'task=addressbook_modal&dest=contacts&fields[contacts]=<?=urlencode(_("Contacts"))?>&storeid='+module.storeid, 800, 500, abCallBack);">
					</td>
					<td>
						<input id="contacts" class="field" type="text">
					</td>
					<td class="propertynormal propertywidth">
						<input class="button" type="button" value="<?=_("Categories")?>:" onclick="webclient.openModalDialog(module, 'categories', DIALOG_URL+'task=categories_modal', 350, 370, categoriesCallBack);">
					</td>
					<td>
						<input id="categories" class="field" type="text">
					</td>
					<td width="30" nowrap>
						<label for="checkbox_private"><?=_("Private")?></label>
					</td>
					<td width="16">
						<input id="checkbox_private" type="checkbox">
					</td>
				</tr>
			</table>
		</div>

<?php 
	$tabbar->endTab();
	
	$tabbar->beginTab("details");
?>

		<div class="properties">
			<table width="100%" border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Department")?>:
					</td>
					<td width="200">
						<input id="department_name" class="field" type="text">
					</td>
					<td width="10">&nbsp;</td>
					<td class="propertynormal propertywidth">
						<?=_("Manager Name")?>:
					</td>
					<td>
						<input id="manager_name" class="field" type="text">
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Office")?>:
					</td>
					<td>
						<input id="office_location" class="field" type="text">
					</td>
					<td width="10">&nbsp;</td>
					<td class="propertynormal propertywidth">
						<?=_("Assistant Name")?>:
					</td>
					<td>
						<input id="assistant" class="field" type="text">
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Profession")?>:
					</td>
					<td>
						<input id="profession" class="field" type="text">
					</td>
				</tr>
			</table>
		</div>
		
		<div class="properties">
			<table border="0" cellpadding="1" cellspacing="0">
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Nickname")?>:
					</td>
					<td width="200">
						<input id="nickname" class="field" type="text">
					</td>
					<td width="10">&nbsp;</td>
					<td class="propertynormal propertywidth">
						<?=_("Partner Name")?>:
					</td>
					<td width="140">
						<input id="spouse_name" class="field" type="text">
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Title")?>:
					</td>
					<td>
						<input id="display_name_prefix" class="field" type="text" onchange="updateDisplayName();">
					</td>
					<td width="10">&nbsp;</td>
					<td class="propertynormal propertywidth">
						<?=_("Birthday")?>:
					</td>
					<td>
						<input id="text_birthday" class="field" type="text" onchange="eventDatePickerInputChange()">
					</td>
					<td>
						<div id="text_birthday_button" class="datepicker">&nbsp;</div>
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth">
						<?=_("Suffix")?>:
					</td>
					<td>
						<input id="generation" class="field" type="text" onchange="updateDisplayName();">
					</td>
					<td width="10">&nbsp;</td>
					<td class="propertynormal propertywidth">
						<?=_("Special Date")?>:
					</td>
					<td>
						<input id="text_wedding_anniversary" class="field" type="text">
					</td>
					<td>
						<div id="text_wedding_anniversary_button" class="datepicker">&nbsp;</div>
					</td>
				</tr>
			</table>
		</div>

<?php 
	$tabbar->endTab();
} // getBody

function getMenuButtons(){
	return array(
			array(
				'id'=>"save",
				'name'=>_("Save"),
				'title'=>_("Save"),
				'callback'=>'function(){submitContact(module)}'
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"attachment",
				'name'=>"",
				'title'=>_("Add Attachments"),
				'callback'=>"function(){webclient.openWindow(module, 'attachments', DIALOG_URL+'task=attachments_modal&store=' + module.storeid + '&entryid=' + (module.messageentryid?module.messageentryid:'') + '&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value, 570, 425, '0');}"
			),
			array(
				'id'=>"attach_item",
				'name'=>"",
				'title'=>_("Attach item"),
				'callback'=>"function(){webclient.openModalDialog(module, 'attachitem', DIALOG_URL+'task=attachitem_modal&storeid=' + module.storeid + '&entryid=' + module.parententryid +'&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value, FIXEDSETTINGS.ATTACHITEM_DIALOG_WIDTH, FIXEDSETTINGS.ATTACHITEM_DIALOG_HEIGHT, false, false, {module : module});}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"newmessagecontact",
				'name'=>"",
				'title'=>_("New Message for Contact"),
				'callback'=>"eventContactItemSendMailTo"
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
