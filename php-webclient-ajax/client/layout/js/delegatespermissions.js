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

/**
 * initDelegatePermissions
 * 
 * initializes permissions dialog for delegates.
 */
function initDelegatePermissions(user)
{
	delegate = user[0];
	// Add delegate's name to window title.
	if (editDelegates.length > 1){
		window.document.title = _("Delegate Permissions: ") + _("Multiple Delegates");
	} else {
		window.document.title = _("Delegate Permissions: ") + delegate["fullname"];
	}
	// Profile template
	var profileTemplate = new Object();
	profileTemplate[_("Owner")]		= ecRightsTemplate[_("Owner")];
	profileTemplate[_("Secretary")]	= ecRightsTemplate[_("Secretary")];
	profileTemplate[_("Only read")]	= ecRightsTemplate[_("Only read")];
	profileTemplate[_("None")]		= 0;

	// Loading profiles.
	for (var foldername in delegate["permissions"]){
		// Profile element.
		var element = dhtml.getElementById(foldername);
		if (element) {
			dhtml.deleteAllChildren(element);
			for(var title in profileTemplate){
				var option = dhtml.addElement(null, "option");
				option.text = title;
				option.value = profileTemplate[title];
				element.options[element.length] = option;
			}
			// Finally set profile according to permission a delegate has.
			element.value = delegate["permissions"][foldername];

			// toggle delegate meeting rule option
			if(element.id == "calendar") {
				toggleDelegateMeetingRuleOption(element);
			}
		}
	}

	// Set flags for private items.
	var see_private = dhtml.getElementById("see_private");
	if (parseInt(delegate["see_private"])){
		see_private.checked = true;
	}

	// Set flag for delegate meeting rule.
	var delegate_meeting_rule_checkbox = dhtml.getElementById("delegate_meeting_rule_checkbox");
	// if calendar permission is not set as owner then don't set this flag
	var calendar_folder_element = dhtml.getElementById("calendar");
	if(parseInt(delegate["delegate_meeting_rule"]) && (calendar_folder_element.value == ecRightsTemplate[_("Owner")] || calendar_folder_element.value == ecRightsTemplate[_("Secretary")])) {
		delegate_meeting_rule_checkbox.checked = true;
	}
}
/**
 * submitDelegatePermissions
 *
 * Called when user has done with giving permissions to delegate and
 * clicks 'OK' to save permissions.
 */
function submitDelegatePermissions()
{
	// Retrieve permissions for all folders.
	for (var foldername in delegate["permissions"]){
		var element = dhtml.getElementById(foldername);
		if (element) {
			delegate["permissions"][foldername] = element.value;
		}
	}
	
	// Retrieve permission for private items.
	delegate["see_private"] = "0";
	var see_private = dhtml.getElementById("see_private");
	if (see_private.checked){
		delegate["see_private"] = "1";
	}

	// Retrieve flag for delegate meeting rule.
	delegate["delegate_meeting_rule"] = "0";
	var delegate_meeting_rule_checkbox = dhtml.getElementById("delegate_meeting_rule_checkbox");
	if(delegate_meeting_rule_checkbox.checked) {
		delegate["delegate_meeting_rule"] = "1";
	}
	
	for (var i = 0; i < editDelegates.length; i++){
		editDelegates[i]["permissions"] = delegate["permissions"];
		editDelegates[i]["see_private"] = delegate["see_private"];
		editDelegates[i]["delegate_meeting_rule"] = delegate["delegate_meeting_rule"];
	}

	// Save delegate permissions.
	return window.resultCallBack(editDelegates, window.windowData["newDelegate"], window.callBackData);
}

/**
 * toggleDelegateMeetingRuleOption
 *
 * This function is used to enable/disable delegate meeting rule checkbox,
 * according to selected profile
 */
function toggleDelegateMeetingRuleOption(element)
{
	if(element && element.value) {
		if(element.value == ecRightsTemplate[_("Owner")] || element.value == ecRightsTemplate[_("Secretary")]) {
			var cb = dhtml.getElementById("delegate_meeting_rule_checkbox");
			cb.disabled = false;

			var label = cb.nextSibling.nextSibling;
			dhtml.removeClassName(label, "disabled_text");
		} else {
			var cb = dhtml.getElementById("delegate_meeting_rule_checkbox");
			cb.checked = false;		// clear previous value
			cb.disabled = true;

			var label = cb.nextSibling.nextSibling;
			dhtml.addClassName(label, "disabled_text");
		}
	}
}