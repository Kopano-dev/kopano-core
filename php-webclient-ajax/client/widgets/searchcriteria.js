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
 * --SearchCriteria Widget--  
 * @type	Widget
 * @classDescription	 This widget can be used to create search restriction creator dialog
 * 
 * HOWTO USE:
 * var searchcriteria = new SearchCriteria(CONTAINER_ELEMENT);
 * searchcriteria.initSearchCriteria(SELECTED_MESSAGE_TYPE_VALUE, SELECTED_MESSAGE_TYPE_TITLE);
 * 
 * DEPENDS ON:
 * -----> dhtml.js
 * -----> constants.js
 * -----> tabswidget.js
 * -----> restriction.js
 * -----> searchcriteria.css
 * -----> webclient.js
 * -----> date-picker.css
 * -----> date-picker.js
 * -----> date-picker-language.js
 * -----> date-picker-setup.js
 */
SearchCriteria.prototype = new Widget;
SearchCriteria.prototype.constructor = SearchCriteria;
SearchCriteria.superclass = Widget.prototype;

/**
 * @constructor
 * initialize all variables
 * @param		Object			moduleObject		object of module which will use this widget
 * @param		HTMLElement		contentElement		container of this widget
 */
function SearchCriteria(moduleObject, contentElement)
{
	this.contentElement = dhtml.addElement(contentElement, "div", "search_criteria", "search_criteria_" + this.widgetID);
	this.elementsCreated = false;
	this.moduleObject = moduleObject;
	this.errorInCreatingRestrictions = false;		// used to specify that there was an error in creating restrictions, so module should abort the search process

	// combobox options for searchcriteria widget
	this.timeOptionsAll = {
						"anytime": _("anytime"),
						"yesterday": _("yesterday"), "today": _("today"), "tomorrow": _("tomorrow"),
						"last_7_days": _("in the last 7 days"), "next_7_days": _("in the next 7 days"),
						"last_week": _("last week"), "this_week": _("this week"), "next_week": _("next week"),
						"last_month": _("last month"), "this_month": _("this month"), "next_month": _("next month"),
						"select_date_range": _("select date range")
						};

	this.timeOptionsWithoutFuture = {
						"anytime": _("anytime"),
						"yesterday": _("yesterday"), "today": _("today"),
						"last_7_days": _("in the last 7 days"),
						"last_week": _("last week"), "this_week": _("this week"),
						"last_month": _("last month"), "this_month": _("this month"),
						"select_date_range": _("select date range")
						};
}

/**
 * Function will initialize widget and create tabs and HTML elements
 * for selected message type
 * value of selectedMessageType can be appointments, contacts, messages, notes, tasks
 * @param		String		selectedMessageType			selected message type
 * @param		String		selectedMessageTitle		selected message type's title
 */
SearchCriteria.prototype.initSearchCriteria = function(selectedMessageType, selectedMessageTitle)
{
	if(!selectedMessageType && !selectedMessageTitle) {
		selectedMessageType = "messages";
		selectedMessageTitle = _("Messages");
	}

	// store selected message type information
	this.selectedMessageType = selectedMessageType;
	this.selectedMessageTitle = selectedMessageTitle;

	// create tabs using tabs widget
	var tabs = new Object();
	tabs["default"] = {"title" : selectedMessageTitle};
	tabs["more_choices"] = {"title" : _("More choices")};

	this.tabBar = new TabsWidget(this.contentElement, tabs, "default");
	this.tabBar.createTabPages();

	// create html elements for selected message type
	var tabPageElement = this.tabBar.getTabPage("default");
	this.createHTMLElements(selectedMessageType, tabPageElement);
}

/**
 * Function will create restriction object based on currently selected options
 */
SearchCriteria.prototype.createSearchRestrictionObject = function()
{
	// create restriction using current input values
	var fullRestriction = new Object();
	var tempRes1 = new Array();
	var tempRes2 = new Array();
	var tempRes3 = new Array();
	this.errorInCreatingRestrictions = false;		// reinitialize variable for new search request

	if(typeof this.selectedMessageType == "undefined" || !this.selectedMessageType) {
		// abort if message type is not present
		return false;
	}

	// check match case option is selected or not
	var caseSensitivityCheckbox = dhtml.getElementById("case_sensitivity_checkbox_" + this.widgetID, "input", this.contentElement);
	if(caseSensitivityCheckbox.checked) {
		this.matchCase = true;
	} else {
		this.matchCase = false;
	}

	/************ restriction for default tab *************/
	var msgClassRes = this.createRestrictionForMessageClass(this.selectedMessageType);
	if(msgClassRes) {
		tempRes3.push(msgClassRes);
	}

	var defaultPartRes = this.createRestrictionForDefaultPart();
	if(defaultPartRes) {
		tempRes1.push(defaultPartRes);
	}

	var attendeePartRes = this.createRestrictionForAttendeePart(this.selectedMessageType);
	if(attendeePartRes) {
		tempRes2.push(attendeePartRes);
	}

	var timePartRes = this.createRestrictionForTimePart(this.selectedMessageType);
	if(timePartRes) {
		tempRes1.push(timePartRes);
	}
	/************ end of restriction for default tab *************/

	/************ restriction for more choices tab *************/
	var categoryPartRes = this.createRestrictionForCategoryPart();
	if(categoryPartRes) {
		tempRes2.push(categoryPartRes);
	}

	var selectionPartRes = this.createRestrictionForSelectionPart();
	if(selectionPartRes) {
		tempRes2.push(selectionPartRes);
	}

	var sizePartRes = this.createRestrictionForSizePart();
	if(sizePartRes) {
		tempRes2.push(sizePartRes);
	}
	/************ end of restriction for more choices tab *************/

	// now combine all restrictions
	if(tempRes1.length > 0) {
		tempRes3.push((tempRes1.length == 1) ? tempRes1.pop() : Restriction.createResAnd(tempRes1));
	}

	if(tempRes2.length > 0) {
		tempRes3.push((tempRes2.length == 1) ? tempRes2.pop() : Restriction.createResAnd(tempRes2));
	}

	// check if any error is found when creating restrictions
	if(this.errorInCreatingRestrictions === true) {
		return false;
	}

	if(tempRes3.length > 0) {
		fullRestriction = Restriction.createResAnd(tempRes3);
		return fullRestriction;
	} else {
		/**
		 * if there isn't any option selected in any_item tab
		 * then also we have to send empty restriction to start searching, this will get all items in store
		 */
		return Restriction.createResAnd(new Array());
	}
}

/**
 * Function will create restriction for message classes
 * @param		String		messageType			selected message type
 */
SearchCriteria.prototype.createRestrictionForMessageClass = function(messageType)
{
	var msgClassArray = new Array();
	var msgClassRes = new Array();
	switch(messageType) {
		case "any_item":
			// no message class restrictions
			break;
		case "appointments":
			msgClassArray.push("IPM.Appointment");
			msgClassArray.push("IPM.Schedule");
			break;
		case "contacts":
			msgClassArray.push("IPM.Contact");
			msgClassArray.push("IPM.DistList");
			break;
		case "notes":
			msgClassArray.push("IPM.StickyNote");
			break;
		case "tasks":
			msgClassArray.push("IPM.Task");
			msgClassArray.push("IPM.TaskRequest");
			break;
		case "messages":
		default:
			msgClassArray.push("IPM.Appointment");
			msgClassArray.push("IPM.Contact");
			msgClassArray.push("IPM.Task");
			msgClassArray.push("IPM.Activity");
			msgClassArray.push("IPM.StickyNote");
			break;
	}

	if(msgClassArray.length > 0) {
		for(var index in msgClassArray) {
			msgClassRes.push(Restriction.dataResContent("PR_MESSAGE_CLASS", new Array(FL_PREFIX, FL_IGNORECASE), msgClassArray[index]));
		}
	}

	if(msgClassRes.length > 0) {
		msgClassRes = Restriction.createResOr(msgClassRes);
		if(messageType == "messages") {
			msgClassRes = Restriction.createResNot(msgClassRes);
		}
		return msgClassRes;
	} else {
		return false;
	}
}

/**
 * Function will create restriction for default part
 */
SearchCriteria.prototype.createRestrictionForDefaultPart = function()
{
	var subjectElement = dhtml.getElementById("subject_" + this.widgetID, "input", this.contentElement);
	var defaultPartRes = new Array();
	if(subjectElement.value != "") {
		// only create this restriction if user has entered anything
		var defaultPartRes = new Array();
		var propTags = new Array();

		var searchFilterTarget = dhtml.getElementById("searchfiltertarget_" + this.widgetID, "select", this.contentElement);
		switch(searchFilterTarget.options[searchFilterTarget.selectedIndex].value) {
			case "subject_only":
				propTags.push("PR_SUBJECT");
				break;
			case "subject_with_body":
			case "subject_notes":
				propTags.push("PR_SUBJECT");
				propTags.push("PR_BODY");
				break;
			case "fileas_only":
				propTags.push("0x80B5001E");	//fileas
				break;
			case "name_only":
				propTags.push("0x80B5001E");	//fileas
				propTags.push("PR_DEPARTMENT_NAME");
				propTags.push("PR_COMPANY_NAME");
				propTags.push("PR_DISPLAY_NAME_PREFIX");
				propTags.push("PR_TITLE");
				propTags.push("PR_ASSISTANT");
				propTags.push("PR_NICKNAME");
				propTags.push("PR_MANAGER_NAME");
				//propTags.push("PR_CHILDRENS_NAMES");
				propTags.push("PR_SPOUSE_NAME");
				//propTags.push("PR_PREFERRED_BY_NAME");
				//propTags.push("PR_ORIGINAL_DISPLAY_NAME");
				propTags.push("PR_DISPLAY_NAME");
				break;
			case "company_only":
				propTags.push("PR_COMPANY_NAME");
				break;
			case "address_only":
				// these tags are different then OL2007
				propTags.push("0x80CB001E");		// business_address
				propTags.push("0x80CA001E");		// home_address
				propTags.push("0x80CC001E");		// other_address
				break;
			case "email_only":
				propTags.push("0x8154001E");
				propTags.push("0x8144001E");
				propTags.push("0x8134001E");
				break;
			case "phone_only":
				propTags.push("PR_TTYTDD_PHONE_NUMBER");
				propTags.push("PR_ISDN_NUMBER");
				propTags.push("PR_TELEX_NUMBER");
				propTags.push("PR_COMPANY_MAIN_PHONE_NUMBER");
				propTags.push("PR_OTHER_TELEPHONE_NUMBER");
				propTags.push("PR_ASSISTANT_TELEPHONE_NUMBER");
				propTags.push("PR_RADIO_TELEPHONE_NUMBER");
				propTags.push("PR_PAGER_TELEPHONE_NUMBER");
				propTags.push("PR_CALLBACK_TELEPHONE_NUMBER");
				propTags.push("PR_CAR_TELEPHONE_NUMBER");
				propTags.push("PR_CELLULAR_TELEPHONE_NUMBER");
				propTags.push("PR_HOME2_TELEPHONE_NUMBER");
				propTags.push("PR_HOME_TELEPHONE_NUMBER");
				propTags.push("PR_BUSINESS2_TELEPHONE_NUMBER");
				propTags.push("PR_OFFICE_TELEPHONE_NUMBER");
				propTags.push("PR_PRIMARY_TELEPHONE_NUMBER");
				break;
			case "contents_only":
				propTags.push("PR_BODY");
				break;
		}

		for(var index = 0; index < propTags.length; index++) {
			if(this.matchCase) {
				defaultPartRes.push(Restriction.dataResContent(propTags[index], FL_SUBSTRING, subjectElement.value));
			} else {
				defaultPartRes.push(Restriction.dataResContent(propTags[index], new Array(FL_SUBSTRING, FL_IGNORECASE), subjectElement.value));
			}
		}

		if(defaultPartRes.length > 0) {
			return Restriction.createResOr(defaultPartRes);
		} else {
			return false;
		}
	} else {
		return false;
	}
}

/**
 * Function will create restriction for attendee part
 * @param		String		messageType			selected message type
 */
SearchCriteria.prototype.createRestrictionForAttendeePart = function(messageType)
{
	var attendeePartRes = new Array();

	switch(messageType) {
		case "appointments":
			// for organizer field
			var organizerField = dhtml.getElementById("organizer_" + this.widgetID, "input", this.contentElement);

			if(organizerField.value) {
				var subRestriction = new Array();
				var andRes = new Array();
				var tempRes = new Array();
				var recipientName = "";
				var recipientEmailAddress = "";

				// break email addressses and create restriction for each email address
				var recipients = organizerField.value.split(";");
				for(var index = 0; index < recipients.length; index++) {
					tempRes = new Array();
					recipientName = "";
					recipientEmailAddress = "";

					recipientEmailAddress = recipients[index].substring(recipients[index].indexOf("<") + 1, recipients[index].indexOf(">")).trim();
					if(recipientEmailAddress == "") {
						recipientEmailAddress = recipients[index].trim();
					} else {
						recipientName = recipients[index].substring(0, recipients[index].indexOf("<")).trim();
					}

					if(recipientName == "" && recipientEmailAddress == "") {
						continue;
					}

					var orRes = new Array();
					if(recipientName != "") {
						orRes.push(Restriction.dataResContent("PR_DISPLAY_NAME", new Array(FL_SUBSTRING, FL_IGNORECASE), recipientName));
					}
					if(recipientEmailAddress != "") {
						orRes.push(Restriction.dataResContent("PR_EMAIL_ADDRESS", new Array(FL_SUBSTRING, FL_IGNORECASE), recipientEmailAddress));
					}

					tempRes.push(Restriction.dataResBitmask("PR_RECIPIENT_FLAGS", BMR_NEZ, "00000002"));
					tempRes.push(Restriction.createResOr(orRes));

					andRes.push(Restriction.createResAnd(tempRes));
				}

				subRestriction = Restriction.createResOr(andRes);

				attendeePartRes.push(Restriction.createResSubRestriction("PR_MESSAGE_RECIPIENTS", subRestriction));
			}

			// for attendees field
			var attendeesField = dhtml.getElementById("attendees_" + this.widgetID, "input", this.contentElement);

			if(attendeesField.value) {
				var subRestriction = new Array();
				var orRes = new Array();
				var recipientName = "";
				var recipientEmailAddress = "";

				// break email addressses and create restriction for each email address
				var recipients = attendeesField.value.split(";");
				for(var index = 0; index < recipients.length; index++) {
					recipientName = "";
					recipientEmailAddress = "";

					recipientEmailAddress = recipients[index].substring(recipients[index].indexOf("<") + 1, recipients[index].indexOf(">")).trim();
					if(recipientEmailAddress == "") {
						recipientEmailAddress = recipients[index].trim();
					} else {
						recipientName = recipients[index].substring(0, recipients[index].indexOf("<")).trim();
					}

					if(recipientName == "" && recipientEmailAddress == "") {
						continue;
					}

					var tempRes = new Array();
					if(recipientName != "") {
						tempRes.push(Restriction.dataResContent("PR_DISPLAY_NAME", new Array(FL_SUBSTRING, FL_IGNORECASE), recipientName));
					}
					if(recipientEmailAddress != "") {
						tempRes.push(Restriction.dataResContent("PR_EMAIL_ADDRESS", new Array(FL_SUBSTRING, FL_IGNORECASE), recipientEmailAddress));
					}

					orRes.push(Restriction.createResOr(tempRes));
				}

				subRestriction.push(Restriction.createResOr(orRes));

				attendeePartRes.push(Restriction.createResSubRestriction("PR_MESSAGE_RECIPIENTS", Restriction.createResOr(subRestriction)));
			}

			if(attendeePartRes.length > 1) {
				return Restriction.createResAnd(attendeePartRes);
			} else if(attendeePartRes.length == 1) {
				return attendeePartRes.pop();
			} else {
				return false;
			}
			break;
		case "contacts":
			var propsArray = new Array("0x8150001E", "0x8140001E", "0x8130001E", "0x8154001E", "0x8144001E", "0x8134001E",
										"0x8153001E", "0x8143001E", "0x8133001E");
			var emailField = dhtml.getElementById("email_" + this.widgetID, "input", this.contentElement);

			if(emailField.value) {
				// create separate restriction for each email address
				var recipients = emailField.value.split(";");
				var contactRes = new Array();

				for(var indexOuter = 0; indexOuter < recipients.length; indexOuter++) {
					for(var indexInner = 0; indexInner < propsArray.length; indexInner++) {
						if(recipients[indexOuter].trim() != "") {
							contactRes.push(Restriction.dataResContent(propsArray[indexInner], new Array(FL_SUBSTRING, FL_IGNORECASE), recipients[indexOuter].trim()));
						}
					}
				}

				return Restriction.createResOr(contactRes);
			} else {
				return false;
			}
			break;
		case "notes":
			return false;
			break;
		case "tasks":
			var statusComboboxElement = dhtml.getElementById("status_combobox_" + this.widgetID, "select", this.contentElement);

			if(statusComboboxElement != null) {
				var statusComboboxValue = statusComboboxElement.options[statusComboboxElement.selectedIndex].value;

				switch(statusComboboxValue) {
					case "no_matter":
						break;
					case "not_started":
						attendeePartRes.push(Restriction.dataResProperty("0x80710003", RELOP_EQ, 0));
						break;
					case "in_progress":
						attendeePartRes.push(Restriction.dataResProperty("0x80710003", RELOP_EQ, 1));
						break;
					case "completed":
						var andRes1 = new Array();
						var andRes2 = new Array();
						var orRes = new Array();

						orRes.push(Restriction.createResNot(Restriction.dataResExist("PR_FLAG_ICON")));
						orRes.push(Restriction.dataResProperty("PR_FLAG_ICON", RELOP_EQ, 0));
						orRes = Restriction.createResOr(orRes);

						andRes1.push(orRes);
						andRes1.push(Restriction.dataResProperty("PR_FLAG_STATUS", RELOP_EQ, 1));
						andRes1.push(Restriction.dataResExist("PR_FLAG_STATUS"));
						andRes1 = Restriction.createResAnd(andRes1);

						andRes2.push(Restriction.dataResProperty("0x80710003", RELOP_EQ, 2));
						andRes2.push(Restriction.dataResExist("0x80710003"));
						andRes2 = Restriction.createResAnd(andRes2);

						attendeePartRes.push(Restriction.createResOr(new Array(andRes1, andRes2)));
						break;
					case "waiting":
						attendeePartRes.push(Restriction.dataResProperty("0x80710003", RELOP_EQ, 3));
						break;
					case "deferred":
						attendeePartRes.push(Restriction.dataResProperty("0x80710003", RELOP_EQ, 4));
						break;
				}
			}

			// for from field
			var fromField = dhtml.getElementById("from_" + this.widgetID, "input", this.contentElement);

			if(fromField.value) {
				var recipients = fromField.value.split(";");
				var tempRes = new Array();
				var recipientRes = new Object();

				for(var index = 0; index < recipients.length; index++) {
					if(recipients[index].trim() != "") {
						// "0x8091001E" ==> Delegate
						var delegatorName = recipients[index].substring(0, recipients[index].indexOf("<"));
						tempRes.push(Restriction.dataResContent("0x8091001E", new Array(FL_SUBSTRING, FL_IGNORECASE), delegatorName.trim()));
					}
				}

				attendeePartRes.push(Restriction.createResOr(tempRes));
			}

			// for sent_to field
			var sentToField = dhtml.getElementById("sent_to_" + this.widgetID, "input", this.contentElement);

			if(sentToField.value) {
				var recipients = sentToField.value.split(";");
				var tempRes = new Array();

				for(var index = 0; index < recipients.length; index++) {
					if(recipients[index].trim() != "") {
						// "0x808F001E" ==> Owner
						var ownerName = recipients[index].substring(0, recipients[index].indexOf("<"));
						tempRes.push(Restriction.dataResContent("0x808F001E", new Array(FL_SUBSTRING, FL_IGNORECASE), ownerName.trim()));
					}
				}

				attendeePartRes.push(Restriction.createResAnd(tempRes));
			}

			if(attendeePartRes.length > 1) {
				return Restriction.createResAnd(attendeePartRes);
			} else if(attendeePartRes.length == 1) {
				return attendeePartRes.pop();
			} else {
				return false;
			}
			break;
		case "any_item":
		case "messages":
		default:
			// for owner place
			var ownerPlaceCheckbox = dhtml.getElementById("owner_place_checkbox_" + this.widgetID, "input", this.contentElement);
			var ownerPlaceElement = dhtml.getElementById("owner_place_combobox_" + this.widgetID, "select", this.contentElement);

			if(ownerPlaceCheckbox != null && ownerPlaceCheckbox.checked) {
				var ownerPlaceRes = new Object();
				ownerPlaceValue = ownerPlaceElement.options[ownerPlaceElement.selectedIndex].value;

				switch(ownerPlaceValue) {
					case "only_person_on_to_line":
						ownerPlaceRest = Restriction.dataResContent("PR_DISPLAY_TO", FL_FULLSTRING, webclient.fullname);
						break;
					case "on_to_line":
						var andRes = new Array();
						andRes.push(Restriction.dataResProperty("PR_RECIPIENT_TYPE", RELOP_EQ, 1));
						andRes.push(Restriction.dataResContent("PR_DISPLAY_NAME", FL_FULLSTRING, webclient.fullname));
						andRes = new Array(Restriction.createResAnd(andRes));

						ownerPlaceRest = Restriction.createResSubRestriction("PR_MESSAGE_RECIPIENTS", Restriction.createResOr(andRes));
						break;
					case "on_cc_line":
						var andRes = new Array();
						andRes.push(Restriction.dataResProperty("PR_RECIPIENT_TYPE", RELOP_EQ, 2));
						andRes.push(Restriction.dataResContent("PR_DISPLAY_NAME", FL_FULLSTRING, webclient.fullname));
						andRes = new Array(Restriction.createResAnd(andRes));

						ownerPlaceRest = Restriction.createResSubRestriction("PR_MESSAGE_RECIPIENTS", Restriction.createResOr(andRes));
						break;
				}

				attendeePartRes.push(ownerPlaceRest);
			}

			// for from field
			var fromField = dhtml.getElementById("from_" + this.widgetID, "input", this.contentElement);

			if(fromField.value) {
				var recipients = fromField.value.split(";");
				var tempRes = new Array();
				var recipientName = "";
				var recipientEmailAddress = "";

				for(var index = 0; index < recipients.length; index++) {
					recipientName = "";
					recipientEmailAddress = "";

					recipientEmailAddress = recipients[index].substring(recipients[index].indexOf("<") + 1, recipients[index].indexOf(">")).trim();
					if(recipientEmailAddress == "") {
						recipientEmailAddress = recipients[index].trim();
					} else {
						recipientName = recipients[index].substring(0, recipients[index].indexOf("<")).trim();
					}

					if(recipientName == "" && recipientEmailAddress == "") {
						continue;
					}

					if(recipientName != "") {
						tempRes.push(Restriction.dataResContent("PR_SENT_REPRESENTING_NAME", new Array(FL_SUBSTRING, FL_IGNORECASE), recipientName));
					}
					if(recipientEmailAddress != "") {
						tempRes.push(Restriction.dataResContent("PR_SENT_REPRESENTING_EMAIL_ADDRESS", new Array(FL_SUBSTRING, FL_IGNORECASE), recipientEmailAddress));
					}
				}

				attendeePartRes.push(Restriction.createResOr(tempRes));
			}

			// for sent_to field
			var sentToField = dhtml.getElementById("sent_to_" + this.widgetID, "input", this.contentElement);

			if(sentToField.value) {
				var recipients = sentToField.value.split(";");
				var tempRes = new Array();
				var recipientName = "";
				var recipientEmailAddress = "";

				for(var index = 0; index < recipients.length; index++) {
					recipientName = "";
					recipientEmailAddress = "";

					recipientEmailAddress = recipients[index].substring(recipients[index].indexOf("<") + 1, recipients[index].indexOf(">")).trim();
					if(recipientEmailAddress.length == 0) {
						recipientEmailAddress = recipients[index].trim();
					} else {
						recipientName = recipients[index].substring(0, recipients[index].indexOf("<")).trim();
					}

					if(recipientName == "" && recipientEmailAddress == "") {
						continue;
					}

					var orRes = new Array();
					if(recipientName != "") {
						orRes.push(Restriction.dataResContent("PR_DISPLAY_NAME", new Array(FL_SUBSTRING, FL_IGNORECASE), recipientName));
					}
					if(recipientEmailAddress != "") {
						orRes.push(Restriction.dataResContent("PR_EMAIL_ADDRESS", new Array(FL_SUBSTRING, FL_IGNORECASE), recipientEmailAddress));
					}

					tempRes.push(Restriction.createResOr(orRes));
				}

				attendeePartRes.push(Restriction.createResSubRestriction("PR_MESSAGE_RECIPIENTS", Restriction.createResOr(tempRes)));
			}

			if(attendeePartRes.length > 1) {
				return Restriction.createResAnd(attendeePartRes);
			} else if(attendeePartRes.length == 1) {
				return attendeePartRes.pop();
			} else {
				return false;
			}
			break;
	}
}

/**
 * Function will create restriction for time part
 * @param		String		messageType			selected message type
 */
SearchCriteria.prototype.createRestrictionForTimePart = function(messageType)
{
	var timePartRes = new Array();
	var timeCombobox1 = dhtml.getElementById("time_selection_property_" + this.widgetID, "select", this.contentElement);
	var timeCombobox2 = dhtml.getElementById("time_selection_values_" + this.widgetID, "select", this.contentElement);
	var timeCombobox1Value = timeCombobox1.options[timeCombobox1.selectedIndex].value;
	var timeCombobox2Value = timeCombobox2.options[timeCombobox2.selectedIndex].value;
	var dateStartRangeTextbox = dhtml.getElementById("range_start_date_" + this.widgetID, "input", this.contentElement);
	var dateEndRangeTextbox = dhtml.getElementById("range_end_date_" + this.widgetID, "input", this.contentElement);
	var upperLimit = false;
	var lowerLimit = false;

	var propTags = new Array();
	switch(timeCombobox1Value) {
		case "completed":
			propTags.push("0x807F0040");
			break;
		case "created":
			propTags.push("PR_CREATION_TIME");
			break;
		case "modified":
			propTags.push("PR_LAST_MODIFICATION_TIME");
			break;
		case "received":
			propTags.push("PR_MESSAGE_DELIVERY_TIME");
			break;
		case "sent":
			propTags.push("PR_CLIENT_SUBMIT_TIME");
			break;
		case "due":
			if(messageType == "messages") {
				propTags.push("PR_REPLY_TIME");
			} else if(messageType == "tasks") {
				propTags.push("0x80750040");
			}
			break;
		case "starts":
			propTags.push("0x8023000B");
			propTags.push("0x80350040");
			propTags.push("0x80360040");
			propTags.push("0x800D0040");
			break;
		case "ends":
			propTags.push("0x8023000B");
			propTags.push("0x80350040");
			propTags.push("0x80360040");
			propTags.push("0x800E0040");
			break;
		case "expires":
			propTags.push("PR_EXPIRY_TIME");
			break;
		case "none":
		default:
			break;
	}

	if(propTags.length == 0) {
		return false;
	}

	var todayTimeStamp = (new Date().getTime())/1000;		// in seconds
	todayTimeStamp = timeToZero(todayTimeStamp);

	// find unixtimestamp for date limits
	switch(timeCombobox2Value) {
		case "yesterday":
			upperLimit = todayTimeStamp;
			lowerLimit = addDaysToUnixTimeStamp(todayTimeStamp, -1);
			break;
		case "today":
			upperLimit = addDaysToUnixTimeStamp(todayTimeStamp, 1);
			lowerLimit = todayTimeStamp;
			break;
		case "tomorrow":
			upperLimit = addDaysToUnixTimeStamp(todayTimeStamp, 2);
			lowerLimit = addDaysToUnixTimeStamp(todayTimeStamp, 1);
			break;
		case "last_7_days":
			upperLimit = addDaysToUnixTimeStamp(todayTimeStamp, 1);
			lowerLimit = addDaysToUnixTimeStamp(todayTimeStamp, -6);
			break;
		case "next_7_days":
			upperLimit = addDaysToUnixTimeStamp(todayTimeStamp, 7);
			lowerLimit = todayTimeStamp;
			break;
		case "last_week":
			var currentDate = new Date(todayTimeStamp * 1000);
			dateRange = getDateRangeOfWeek(currentDate.getWeekNumber() - 1);
			upperLimit = dateRange["week_end_date"];
			lowerLimit = dateRange["week_start_date"];
			break;
		case "this_week":
			var currentDate = new Date(todayTimeStamp * 1000);
			dateRange = getDateRangeOfWeek();
			upperLimit = dateRange["week_end_date"];
			lowerLimit = dateRange["week_start_date"];
			break;
		case "next_week":
			var currentDate = new Date(todayTimeStamp * 1000);
			dateRange = getDateRangeOfWeek(currentDate.getWeekNumber() + 1);
			upperLimit = dateRange["week_end_date"];
			lowerLimit = dateRange["week_start_date"];
			break;
		case "last_month":
			var lastMonth = new Date(todayTimeStamp * 1000);
			lastMonth.addMonths(-1);

			// get start date of last month
			lastMonth.setDate(1);
			lowerLimit = lastMonth.getTime()/1000;

			// get end date of last month
			lastMonth.setDate(lastMonth.getDaysInMonth() + 1);
			upperLimit = lastMonth.getTime()/1000;
			break;
		case "this_month":
			var currentMonth = new Date(todayTimeStamp * 1000);

			// get start date of this month
			currentMonth.setDate(1);
			lowerLimit = currentMonth.getTime()/1000;

			// get end date of this month
			currentMonth.setDate(currentMonth.getDaysInMonth() + 1);
			upperLimit = currentMonth.getTime()/1000;
			break;
		case "next_month":
			var nextMonth = new Date(todayTimeStamp * 1000);
			nextMonth.addMonths(1);

			// get start date of last month
			nextMonth.setDate(1);
			lowerLimit = nextMonth.getTime()/1000;

			// get end date of last month
			nextMonth.setDate(nextMonth.getDaysInMonth() + 1);
			upperLimit = nextMonth.getTime()/1000;
			break;
		case "select_date_range":
			var tempDate = Date.parseDate(dateEndRangeTextbox.value, _("%d-%m-%Y"), true, true);
			if(tempDate) {
				upperLimit = tempDate.getTime()/1000;
			}

			tempDate = Date.parseDate(dateStartRangeTextbox.value, _("%d-%m-%Y"), true, true);
			if(tempDate) {
				lowerLimit = tempDate.getTime()/1000;
			}

			// date validations
			if(!upperLimit || !lowerLimit) {
				alert(_("Incorrect date range specified") + ".");
				this.errorInCreatingRestrictions = true;
				return false;
			}

			if(upperLimit < lowerLimit) {
				alert(_("Start date can not be greater then end date") + ".");
				this.errorInCreatingRestrictions = true;
				return false;
			}

			upperLimit = timeToZero(upperLimit);
			lowerLimit = timeToZero(lowerLimit);

			// add 24 hours because we want to take end day into calculation also
			upperLimit += 86400;
			break;
	}

	if(propTags.length == 1) {
		if(timeCombobox2Value == "anytime") {
			timePartRes.push(Restriction.dataResExist(propTags[0]));
		} else {
			if(upperLimit && lowerLimit) {
				timePartRes.push(Restriction.dataResProperty(propTags[0], RELOP_LT, upperLimit));
				timePartRes.push(Restriction.dataResProperty(propTags[0], RELOP_GE, lowerLimit));
			}
		}
	} if(propTags.length > 1) {
		// special case for appointments
		if(upperLimit && lowerLimit) {
			var tempRes1 = new Array();
			tempRes1.push(Restriction.dataResProperty(propTags[0], RELOP_EQ, true));
			tempRes1.push(Restriction.dataResProperty(propTags[1], RELOP_LT, upperLimit));
			tempRes1 = Restriction.createResAnd(tempRes1);

			var tempRes2 = new Array();
			tempRes2.push(tempRes1);
			tempRes2.push(Restriction.dataResProperty(propTags[3], RELOP_LT, upperLimit));
			tempRes2 = Restriction.createResOr(tempRes2);

			var tempRes3 = new Array();
			tempRes3.push(Restriction.dataResProperty(propTags[0], RELOP_EQ, true));
			tempRes3.push(Restriction.dataResProperty(propTags[2], RELOP_GE, lowerLimit));
			tempRes3 = Restriction.createResAnd(tempRes3);

			var tempRes4 = new Array();
			tempRes4.push(Restriction.dataResProperty(propTags[0], RELOP_EQ, true));
			tempRes4.push(Restriction.dataResProperty(propTags[2], RELOP_GE, lowerLimit));
			tempRes4 = Restriction.createResAnd(tempRes4);

			var tempRes5 = new Array();
			tempRes5.push(tempRes4);
			tempRes5.push(Restriction.dataResProperty(propTags[3], RELOP_GE, lowerLimit));
			tempRes5 = Restriction.createResOr(tempRes5);

			timePartRes.push(tempRes2);
			timePartRes.push(tempRes5);
		}
	}

	if(timePartRes.length > 0) {
		return Restriction.createResAnd(timePartRes);
	} else {
		return false;
	}
}

/**
 * Function will create restriction for categories part
 */
SearchCriteria.prototype.createRestrictionForCategoryPart = function()
{
	var categoryPartRes = new Array();
	var categories = dhtml.getElementById("categories_" + this.widgetID, "input", this.contentElement).value;
	var categoriesArray = new Array();

	if(categories != "") {
		// add categories to array
		categoriesArray = categories.split(";");
		// Remove white spaces in array
		var tmpParts = categoriesArray;
		categoriesArray = new Array();
		for(var key in tmpParts)
		{
			if(tmpParts[key].length > 0 && tmpParts[key].trim() != "") {
				categoriesArray.push(tmpParts[key].trim());
			}
		}

		var propIdKeywordsMV = NAMEDPROPS['PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords'];
		var propIdKeywords = NAMEDPROPS['PT_STRING8:PS_PUBLIC_STRINGS:Keywords'];
		for(var index in categoriesArray) {
			if(this.matchCase) {
				categoryPartRes.push(Restriction.dataResContent(propIdKeywordsMV, FL_FULLSTRING, categoriesArray[index], propIdKeywords));	// Keywords
			} else {
				categoryPartRes.push(Restriction.dataResContent(propIdKeywordsMV, new Array(FL_FULLSTRING, FL_IGNORECASE), categoriesArray[index], propIdKeywords));	// Keywords
			}
		}

		if(categoryPartRes.length > 0) {
			return Restriction.createResOr(categoryPartRes);
		} else {
			return false;
		}
	} else {
		return false;
	}
}

/**
 * Function will create restriction for selection part
 */
SearchCriteria.prototype.createRestrictionForSelectionPart = function()
{
	var selectionPartRes = new Array();
	var selectionArray = new Array("read_status", "attachment", "importance", "flag");

	for(var index in selectionArray) {
		var checkboxElement = dhtml.getElementById(selectionArray[index] + "_checkbox_" + this.widgetID, "input", this.contentElement);
		var selectionBoxElement = dhtml.getElementById(selectionArray[index] + "_combobox_" + this.widgetID, "select", this.contentElement);
		var selectedOptionValue = selectionBoxElement.options[selectionBoxElement.selectedIndex].value;

		if(checkboxElement.checked) {
			switch(selectionArray[index]) {
				case "read_status":
					var readStatusRes = new Array();
					if(selectedOptionValue == "unread") {
						readStatusRes.push(Restriction.dataResBitmask("PR_MESSAGE_FLAGS", BMR_EQZ, "00000001"));
						readStatusRes.push(Restriction.dataResBitmask("0x10970003", BMR_NEZ, "00000001"));
						readStatusRes = Restriction.createResOr(readStatusRes);
					} else {
						readStatusRes = Restriction.dataResBitmask("PR_MESSAGE_FLAGS", BMR_NEZ, "00000001");
					}

					selectionPartRes.push(readStatusRes);
					break;
				case "attachment":
					if(selectedOptionValue == "without_attachments") {
						var property1 = "0x81B4000B";
						var property2 = "PR_HASATTACH";
					} else {
						var property1 = "PR_HASATTACH";
						var property2 = "0x81B4000B";
					}

					var hasAttachRes = new Array();
					hasAttachRes.push(Restriction.dataResExist(property1));
					hasAttachRes.push(Restriction.dataResProperty(property1, RELOP_EQ, true));
					hasAttachRes = Restriction.createResAnd(hasAttachRes);
					var tempRes = new Array();
					tempRes.push(Restriction.dataResExist(property2));
					tempRes.push(Restriction.dataResProperty(property2, RELOP_EQ, true));
					tempRes = Restriction.createResAnd(tempRes);
					tempRes = Restriction.createResNot(tempRes);

					if(selectedOptionValue == "without_attachments") {
						var attachmentRes = Restriction.createResOr(new Array(tempRes, hasAttachRes));
					} else {
						var attachmentRes = Restriction.createResAnd(new Array(tempRes, hasAttachRes));
					}

					selectionPartRes.push(attachmentRes);
					break;
				case "importance":
					var importanceRes = Restriction.dataResProperty("PR_IMPORTANCE", RELOP_EQ, selectedOptionValue);
					selectionPartRes.push(importanceRes);
					break;
				case "flag":
					var flagRes = new Array();
					var tempRes2 = new Array();
					tempRes2.push(Restriction.createResNot(Restriction.dataResExist("PR_FLAG_ICON")));
					tempRes2.push(Restriction.dataResProperty("PR_FLAG_ICON", RELOP_EQ, 0));
					tempRes2 = Restriction.createResOr(tempRes2);
					var tempRes3 = new Array();
					tempRes3.push(tempRes2);
					tempRes3.push(Restriction.dataResExist("PR_FLAG_STATUS"));
					tempRes3.push(Restriction.dataResProperty("PR_FLAG_STATUS", RELOP_EQ, 2));
					tempRes3 = Restriction.createResAnd(tempRes3);
					var tempRes4 = new Array();
					tempRes4.push(tempRes2);
					tempRes4.push(Restriction.dataResExist("PR_FLAG_STATUS"));
					tempRes4.push(Restriction.dataResProperty("PR_FLAG_STATUS", RELOP_EQ, 1));
					tempRes4 = Restriction.createResAnd(tempRes4);
					var tempRes5 = new Array();
					tempRes5.push(Restriction.createResNot(Restriction.dataResExist("PR_FLAG_ICON")));
					tempRes5.push(Restriction.dataResProperty("PR_FLAG_ICON", RELOP_EQ, 0));
					tempRes5 = Restriction.createResAnd(tempRes5);
					var tempRes8 = new Array();
					tempRes8.push(Restriction.dataResExist("0x80710003"));
					tempRes8.push(Restriction.dataResProperty("0x80710003", RELOP_EQ, 2));
					tempRes8 = Restriction.createResAnd(tempRes8);

					if(selectedOptionValue == "marked_completed") {
						flagRes = Restriction.createResOr(new Array(tempRes4, tempRes8));
					} else if(selectedOptionValue == "flagged_by_other") {
						flagRes = Restriction.createResAnd(new Array(tempRes3));
					} else if(selectedOptionValue == "no_flag") {
						var tempRes1 = new Array();
						tempRes1.push(Restriction.dataResExist("0x0E2B0003"));
						tempRes1.push(Restriction.dataResBitmask("0x0E2B0003", BMR_NEZ, "00000001"));
						tempRes1 = Restriction.createResAnd(tempRes1);
						
						// combine all flag restrictions
						flagRes.push(tempRes1);
						flagRes.push(tempRes3);
						flagRes.push(tempRes4);
						flagRes.push(tempRes5);
						// only for IPM.Task
						flagRes.push(Restriction.dataResContent("PR_MESSAGE_CLASS", new Array(FL_PREFIX, FL_IGNORECASE), "IPM.Task"));
						flagRes.push(Restriction.dataResContent("PR_MESSAGE_CLASS", new Array(FL_FULLSTRING, FL_IGNORECASE), "IPM.Task"));
						flagRes = Restriction.createResNot(Restriction.createResOr(flagRes));
					} else if(selectedOptionValue == "flagged_by_me") {
						var tempRes7 = Restriction.createResAnd(new Array(tempRes4));
						var tempRes9 = Restriction.createResNot(Restriction.createResOr(new Array(tempRes7, tempRes8)));

						var tempRes10 = new Array();
						tempRes10.push(Restriction.dataResProperty("0x8078000B", RELOP_NE, true));
						tempRes10.push(Restriction.dataResProperty("0x80830003", RELOP_EQ, 2));
						tempRes10 = Restriction.createResNot(Restriction.createResAnd(tempRes10));

						// only for IPM.Task
						var tempRes11 = new Array();
						tempRes11.push(Restriction.dataResContent("PR_MESSAGE_CLASS", new Array(FL_PREFIX, FL_IGNORECASE), "IPM.Task"));
						tempRes11.push(Restriction.dataResContent("PR_MESSAGE_CLASS", new Array(FL_FULLSTRING, FL_IGNORECASE), "IPM.Task"));
						tempRes11 = Restriction.createResOr(tempRes11);

						var tempRes12 = Restriction.createResOr(new Array(tempRes10, tempRes11));

						var tempRes13 = new Array();
						tempRes13.push(Restriction.dataResExist("0x0E2B0003"));
						tempRes13.push(Restriction.dataResBitmask("0x0E2B0003", BMR_NEZ, "00000001"));
						tempRes13 = Restriction.createResAnd(tempRes13);

						var tempRes14 = Restriction.createResOr(new Array(tempRes12, tempRes5, tempRes13));

						// combine all flag restrictions
						flagRes = Restriction.createResAnd(new Array(tempRes9, tempRes14));
					}

					selectionPartRes.push(flagRes);
					break;
			}
		}
	}

	// create AND restriction of all selection restrictions
	if(selectionPartRes.length > 0) {
		return Restriction.createResAnd(selectionPartRes);
	} else {
		return false;
	}
}

/**
 * Function will create restriction for size part
 */
SearchCriteria.prototype.createRestrictionForSizePart = function()
{
	var sizePartRes = new Array();
	var sizeCombobox = dhtml.getElementById("size_combobox_" + this.widgetID, "select", this.contentElement);
	var sizeComboboxValue = sizeCombobox.options[sizeCombobox.selectedIndex].value;
	var sizeValue1 = dhtml.getElementById("size_value_1_" + this.widgetID, "input", this.contentElement).value;		// value in KBs
	var sizeValue2 = dhtml.getElementById("size_value_2_" + this.widgetID, "input", this.contentElement).value;		// value in KBs
	var upperLimit = false;
	var lowerLimit = false;

	if(sizeValue1 == "") {
		sizeValue1 = 0;				// by default take 0 as limit
	}

	if(sizeValue2 == "") {
		sizeValue2 = 0;				// by default take 0 as limit
	}

	// convert values to bytes
	sizeValue1 = parseInt(sizeValue1) * 1024;
	sizeValue2 = parseInt(sizeValue2) * 1024;

	switch(sizeComboboxValue) {
		case "equals":
		case "between":
			if(sizeComboboxValue == "equals") {
				if(isNaN(sizeValue1)) {
					alert(_("The values you entered for Size are not valid") + ".");
					this.errorInCreatingRestrictions = true;
					return false;
				}

				lowerLimit = sizeValue1;
				upperLimit = sizeValue1 + 1024;
			} else if(sizeComboboxValue == "between") {
				if(isNaN(sizeValue1)) {
					alert(_("The values you entered for Size are not valid") + ".");
					this.errorInCreatingRestrictions = true;
					return false;
				}

				if(isNaN(sizeValue2)) {
					alert(_("The values you entered for Size are not valid") + ".");
					this.errorInCreatingRestrictions = true;
					return false;
				}

				lowerLimit = sizeValue1;
				upperLimit = sizeValue2;
			}

			// create restriction for lower limit
			var tempRes1 = new Array();
			tempRes1.push(Restriction.dataResExist("0x83250003"));
			tempRes1.push(Restriction.dataResProperty("0x83250003", RELOP_GE, lowerLimit));
			tempRes1 = Restriction.createResAnd(tempRes1);

			var tempRes2 = new Array();
			tempRes2.push(Restriction.createResNot(Restriction.dataResExist("0x83250003")));
			tempRes2.push(Restriction.dataResExist("PR_MESSAGE_SIZE"));
			tempRes2.push(Restriction.dataResProperty("PR_MESSAGE_SIZE", RELOP_GE, lowerLimit));
			tempRes2 = Restriction.createResAnd(tempRes2);

			var tempRes3 = Restriction.createResOr(new Array(tempRes1, tempRes2));

			// create same restriction for upper limit
			var tempRes1 = new Array();
			tempRes1.push(Restriction.dataResExist("0x83250003"));
			tempRes1.push(Restriction.dataResProperty("0x83250003", RELOP_LE, upperLimit));
			tempRes1 = Restriction.createResAnd(tempRes1);

			var tempRes2 = new Array();
			tempRes2.push(Restriction.createResNot(Restriction.dataResExist("0x83250003")));
			tempRes2.push(Restriction.dataResExist("PR_MESSAGE_SIZE"));
			tempRes2.push(Restriction.dataResProperty("PR_MESSAGE_SIZE", RELOP_LE, upperLimit));
			tempRes2 = Restriction.createResAnd(tempRes2);

			var tempRes4 = Restriction.createResOr(new Array(tempRes1, tempRes2));

			// combine both restrictions
			sizePartRes = Restriction.createResAnd(new Array(tempRes3, tempRes4));
			break;
		case "less_than":
			if(isNaN(sizeValue1)) {
				alert(_("The values you entered for Size are not valid") + ".");
				this.errorInCreatingRestrictions = true;
				return false;
			}

			upperLimit = sizeValue1;

			var tempRes1 = new Array();
			tempRes1.push(Restriction.dataResExist("0x83250003"));
			tempRes1.push(Restriction.dataResProperty("0x83250003", RELOP_LT, upperLimit));
			tempRes1 = Restriction.createResAnd(tempRes1);

			var tempRes2 = new Array();
			tempRes2.push(Restriction.createResNot(Restriction.dataResExist("0x83250003")));
			tempRes2.push(Restriction.dataResExist("PR_MESSAGE_SIZE"));
			tempRes2.push(Restriction.dataResProperty("PR_MESSAGE_SIZE", RELOP_LT, upperLimit));
			tempRes2 = Restriction.createResAnd(tempRes2);

			// combine both restrictions
			sizePartRes = Restriction.createResOr(new Array(tempRes1, tempRes2));
			break;
		case "greater_than":
			if(isNaN(sizeValue1)) {
				alert(_("The values you entered for Size are not valid") + ".");
				this.errorInCreatingRestrictions = true;
				return false;
			}

			lowerLimit = sizeValue1;

			var tempRes1 = new Array();
			tempRes1.push(Restriction.dataResExist("0x83250003"));
			tempRes1.push(Restriction.dataResProperty("0x83250003", RELOP_GT, lowerLimit));
			tempRes1 = Restriction.createResAnd(tempRes1);

			var tempRes2 = new Array();
			tempRes2.push(Restriction.createResNot(Restriction.dataResExist("0x83250003")));
			tempRes2.push(Restriction.dataResExist("PR_MESSAGE_SIZE"));
			tempRes2.push(Restriction.dataResProperty("PR_MESSAGE_SIZE", RELOP_GT, lowerLimit));
			tempRes2 = Restriction.createResAnd(tempRes2);

			sizePartRes = Restriction.createResOr(new Array(tempRes1, tempRes2));
			break;
		case "any_size":
		default:
			sizePartRes = false;
			break;
	}

	return sizePartRes;
}

/**
 * Function will parse search restrictions and give values to the HTML elements
 */
SearchCriteria.prototype.loadSearchRestrictionObject = function()
{
	// use restriction end put values in to html elements
}

/**
 * changes message type
 * @param		String		messageType				selected message type
 * @param		String		messageTypeTitle		selected message type's title
 */
SearchCriteria.prototype.changeMessageType = function(messageType, messageTypeTitle)
{
	this.selectedMessageType = messageType;
	this.selectedMessageTitle = messageTypeTitle;

	// change tab title and change selection to default tab
	this.tabBar.changeTabTitle("default", messageTypeTitle);
	this.tabBar.changeTab("default");

	// change mesage type
	this.createHTMLElements(messageType);
}

/**
 * create HTML elements for different type of messages
 * it will also remove previously created elements
 * @param		String		messageType		selected message type
 */
SearchCriteria.prototype.createHTMLElements = function(messageType)
{
	var defaultTabPageElement = this.tabBar.getTabPage("default");

	// remove previously created elements in default tab (not on first time)
	if(this.elementsCreated == true) {
		this.removeHTMLElements(defaultTabPageElement);
	}

	// call different methods to create HTML elements for specific message type
	switch(messageType) {
		case "any_item":
			this.createHTMLElementsForAnyItem(defaultTabPageElement);
			break;
		case "appointments":
			this.createHTMLElementsForAppointments(defaultTabPageElement);
			break;
		case "contacts":
			this.createHTMLElementsForContacts(defaultTabPageElement);
			break;
		case "notes":
			this.createHTMLElementsForNotes(defaultTabPageElement);
			break;
		case "tasks":
			this.createHTMLElementsForTasks(defaultTabPageElement);
			break;
		case "messages":
		default:
			this.createHTMLElementsForMessages(defaultTabPageElement);
			break;
	}

	var tabPageElement = this.tabBar.getTabPage("more_choices");
	if(this.elementsCreated == false) {
		// create elements for more choices tab (only first time)
		this.createHTMLElementsForMoreChoices(tabPageElement);
	} else {
		// or reset element values if its already created
		this.resetFieldValues(tabPageElement);
	}

	// first enable elements which were previously disabled
	this.enableHTMLElementsForMoreChoices(tabPageElement);
	// then disable elements according to selected message type
	this.disableHTMLElementsForMoreChoices(tabPageElement, messageType);

	this.elementsCreated = true;
}

/**
 * creates HTML elements for any items tab
 * @param		HTMLElement		tabPageElement		default tab's page element
 */
SearchCriteria.prototype.createHTMLElementsForAnyItem = function(tabPageElement)
{
	// create html elements for messages
	var defaultPart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push(				_("Search for the words(s)") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='subject_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push(				_('In') + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<select id='searchfiltertarget_" + this.widgetID + "' class='combobox inputwidth'>");
	tableData.push("				<option value='subject_only' selected>" + _("subject field only") + "</option>");
	tableData.push("				<option value='subject_notes'>" + _("subject and notes fields") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	defaultPart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("subject_" + this.widgetID, "input", defaultPart), "contextmenu", forceDefaultActionEvent);

	var attendeePart = dhtml.addElement(tabPageElement, "div", "properties");

	tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			<button id='button_from_" + this.widgetID + "' type='button' class='button'>" + _("From") + "...</button>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='from_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			<button id='button_sent_to_" + this.widgetID + "' type='button' class='button'>" + _("Sent To") + "...</button>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='sent_to_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	attendeePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("from_" + this.widgetID, "input", attendeePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("sent_to_" + this.widgetID, "input", attendeePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("button_from_" + this.widgetID, "button", attendeePart), "click", eventSearchCriteriaAddressBookClick);
	dhtml.addEvent(this, dhtml.getElementById("button_sent_to_" + this.widgetID, "button", attendeePart), "click", eventSearchCriteriaAddressBookClick);

	var timePart = dhtml.addElement(tabPageElement, "div");

	tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _("Time") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='6'>");
	tableData.push("			<select id='time_selection_property_" + this.widgetID + "' class='time_selection_property combobox'>");
	tableData.push("				<option value='none' selected>" + _("none") + "</option>");
	tableData.push("				<option value='received'>" + _("received") + "</option>");
	tableData.push("				<option value='sent'>" + _("sent") + "</option>");
	tableData.push("				<option value='created'>" + _("created") + "</option>");
	tableData.push("				<option value='modified'>" + _("modified") + "</option>");
	tableData.push("			</select>");
	tableData.push("			<select id='time_selection_values_" + this.widgetID + "' class='time_selection_values combobox disabled' disabled>");
	tableData.push("				<option value='anytime' selected>" + _("anytime") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr style='display: none;'>");
	tableData.push("		<td class='labelwidth' align='right'>" + NBSP + "</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("Start date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_start_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_start_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("End date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_end_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_end_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	timePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("range_start_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("range_end_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaOnTimeSelectionChange);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_values_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);

	// setup datepickers for date range selection textboxes
	this.setupDatePickers();
}

/**
 * creates HTML elements for appointments tab
 * @param		HTMLElement		tabPageElement		default tab's page element
 */
SearchCriteria.prototype.createHTMLElementsForAppointments = function(tabPageElement)
{
	// create html elements for appointments
	var defaultPart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push(				_("Search for the words(s)") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='subject_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push(				_('In') + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<select id='searchfiltertarget_" + this.widgetID + "' class='combobox inputwidth'>");
	tableData.push("				<option value='subject_only' selected>" + _("subject field only") + "</option>");
	tableData.push("				<option value='subject_notes'>" + _("subject and notes fields") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	defaultPart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("subject_" + this.widgetID, "input", defaultPart), "contextmenu", forceDefaultActionEvent);

	var attendeePart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			<button id='button_organizer_" + this.widgetID + "' type='button' class='button'>" + _("Organized By") + "...</button>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='organizer_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			<button id='button_attendees_" + this.widgetID + "' type='button' class='button'>" + _("Attendees") + "...</button>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='attendees_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	attendeePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("organizer_" + this.widgetID, "input", attendeePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("attendees_" + this.widgetID, "input", attendeePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("button_organizer_" + this.widgetID, "button", attendeePart), "click", eventSearchCriteriaAddressBookClick);
	dhtml.addEvent(this, dhtml.getElementById("button_attendees_" + this.widgetID, "button", attendeePart), "click", eventSearchCriteriaAddressBookClick);

	var timePart = dhtml.addElement(tabPageElement, "div");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _("Time") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='6'>");
	tableData.push("			<select id='time_selection_property_" + this.widgetID + "' class='time_selection_property combobox'>");
	tableData.push("				<option value='none' selected>" + _("none") + "</option>");
	tableData.push("				<option value='starts'>" + _("starts") + "</option>");
	tableData.push("				<option value='ends'>" + _("ends") + "</option>");
	tableData.push("				<option value='created'>" + _("created") + "</option>");
	tableData.push("				<option value='modified'>" + _("modified") + "</option>");
	tableData.push("			</select>");
	tableData.push("			<select id='time_selection_values_" + this.widgetID + "' class='time_selection_values combobox disabled' disabled>");
	tableData.push("				<option value='anytime' selected>" + _("anytime") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr style='display: none;'>");
	tableData.push("		<td class='labelwidth' align='right'>" + NBSP + "</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("Start date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_start_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_start_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("End date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_end_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_end_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	timePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("range_start_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("range_end_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaOnTimeSelectionChange);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_values_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);

	// setup datepickers for date range selection textboxes
	this.setupDatePickers();
}

/**
 * creates HTML elements for contacts tab
 * @param		HTMLElement		tabPageElement		default tab's page element
 */
SearchCriteria.prototype.createHTMLElementsForContacts = function(tabPageElement)
{
	// create html elements for contacts
	var defaultPart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push(				_("Search for the words(s)") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='subject_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push(				_('In') + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<select id='searchfiltertarget_" + this.widgetID + "' class='combobox inputwidth'>");
	tableData.push("				<option value='fileas_only' selected>" + _("file as field only") + "</option>");
	tableData.push("				<option value='name_only'>" + _("name fields only") + "</option>");
	tableData.push("				<option value='company_only'>" + _("company field only") + "</option>");
	tableData.push("				<option value='address_only'>" + _("address fields only") + "</option>");
	tableData.push("				<option value='email_only'>" + _("email fields only") + "</option>");
	tableData.push("				<option value='phone_only'>" + _("phone number fields only") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	defaultPart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("subject_" + this.widgetID, "input", defaultPart), "contextmenu", forceDefaultActionEvent);

	var attendeePart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			<button id='button_email_" + this.widgetID + "' type='button' class='button'>" + _("Email") + "...</button>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='email_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	attendeePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("email_" + this.widgetID, "input", attendeePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("button_email_" + this.widgetID, "button", attendeePart), "click", eventSearchCriteriaAddressBookClick);

	var timePart = dhtml.addElement(tabPageElement, "div");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _("Time") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='6'>");
	tableData.push("			<select id='time_selection_property_" + this.widgetID + "' class='time_selection_property combobox'>");
	tableData.push("				<option value='none' selected>" + _("none") + "</option>");
	tableData.push("				<option value='created'>" + _("created") + "</option>");
	tableData.push("				<option value='modified'>" + _("modified") + "</option>");
	tableData.push("			</select>");
	tableData.push("			<select id='time_selection_values_" + this.widgetID + "' class='time_selection_values combobox disabled' disabled>");
	tableData.push("				<option value='anytime' selected>" + _("anytime") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr style='display: none;'>");
	tableData.push("		<td class='labelwidth' align='right'>" + NBSP + "</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("Start date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_start_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_start_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("End date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_end_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_end_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	timePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("range_start_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("range_end_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaOnTimeSelectionChange);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_values_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);

	// setup datepickers for date range selection textboxes
	this.setupDatePickers();
}

/**
 * creates HTML elements for notes tab
 * @param		HTMLElement		tabPageElement		default tab's page element
 */
SearchCriteria.prototype.createHTMLElementsForNotes = function(tabPageElement)
{
	// create html elements for sticky notes
	var defaultPart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _("Search for the word(s)") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='subject_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _('In') + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<select id='searchfiltertarget_" + this.widgetID + "' class='combobox inputwidth'>");
	tableData.push("				<option value='contents_only' selected>" + _("contents only") + "</option>");
	tableData.push("				<option value='subject_only'>" + _("subject field only") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	defaultPart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("subject_" + this.widgetID, "input", defaultPart), "contextmenu", forceDefaultActionEvent);

	var timePart = dhtml.addElement(tabPageElement, "div");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _('Time') + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='6'>");
	tableData.push("			<select id='time_selection_property_" + this.widgetID + "' class='time_selection_property combobox'>");
	tableData.push("				<option value='none' selected>" + _("none") + "</option>");
	tableData.push("				<option value='created'>" + _("created") + "</option>");
	tableData.push("				<option value='modified'>" + _("modified") + "</option>");
	tableData.push("			</select>");
	tableData.push("			<select id='time_selection_values_" + this.widgetID + "' class='time_selection_values combobox disabled' disabled>");
	tableData.push("				<option value='anytime' selected>" + _("anytime") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr style='display: none;'>");
	tableData.push("		<td class='labelwidth' align='right'>" + NBSP + "</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("Start date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_start_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_start_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("End date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_end_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_end_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	timePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("range_start_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("range_end_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaOnTimeSelectionChange);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_values_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);

	// setup datepickers for date range selection textboxes
	this.setupDatePickers();
}

/**
 * creates HTML elements for tasks tab
 * @param		HTMLElement		tabPageElement		default tab's page element
 */
SearchCriteria.prototype.createHTMLElementsForTasks = function(tabPageElement)
{
	// create html elements for tasks
	var defaultPart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _("Search for the word(s)") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='subject_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _('In') + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<select id='searchfiltertarget_" + this.widgetID + "' class='combobox inputwidth'>");
	tableData.push("				<option value='subject_only' selected>" + _("subject field only") + "</option>");
	tableData.push("				<option value='subject_notes'>" + _("subject and notes fields") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	defaultPart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("subject_" + this.widgetID, "input", defaultPart), "contextmenu", forceDefaultActionEvent);

	var attendeePart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _("Status") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<select id='status_combobox_" + this.widgetID + "' class='combobox inputwidth'>");
	tableData.push("				<option value='no_matter'>" + _("doesn't matter") + "</option>");
	tableData.push("				<option value='not_started'>" + _("not started") + "</option>");
	tableData.push("				<option value='in_progress'>" + _("in progress") + "</option>");
	tableData.push("				<option value='completed'>" + _("completed") + "</option>");
	tableData.push("				<option value='waiting'>" + _("waiting on someone else") + "</option>");
	tableData.push("				<option value='deferred'>" + _("deferred") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			<button type='button' id='button_from_" + this.widgetID + "' class='button'>" + _("From") + "...</button>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='from_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			<button type='button' id='button_sent_to_" + this.widgetID + "' class='button'>" + _("Sent To") + "...</button>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='sent_to_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	attendeePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("from_" + this.widgetID, "input", attendeePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("sent_to_" + this.widgetID, "input", attendeePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("button_from_" + this.widgetID, "button", attendeePart), "click", eventSearchCriteriaAddressBookClick);
	dhtml.addEvent(this, dhtml.getElementById("button_sent_to_" + this.widgetID, "button", attendeePart), "click", eventSearchCriteriaAddressBookClick);

	var timePart = dhtml.addElement(tabPageElement, "div");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _('Time') + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='6'>");
	tableData.push("			<select id='time_selection_property_" + this.widgetID + "' class='time_selection_property combobox'>");
	tableData.push("				<option value='none' selected>" + _("none") + "</option>");
	tableData.push("				<option value='received'>" + _("received") + "</option>");
	tableData.push("				<option value='sent'>" + _("sent") + "</option>");
	tableData.push("				<option value='due'>" + _("due") + "</option>");
	tableData.push("				<option value='expires'>" + _("expires") + "</option>");
	tableData.push("				<option value='created'>" + _("created") + "</option>");
	tableData.push("				<option value='modified'>" + _("modified") + "</option>");
	tableData.push("			</select>");
	tableData.push("			<select id='time_selection_values_" + this.widgetID + "' class='time_selection_values combobox disabled' disabled>");
	tableData.push("				<option value='anytime' selected>" + _("anytime") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr style='display: none;'>");
	tableData.push("		<td class='labelwidth' align='right'>" + NBSP + "</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("Start date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_start_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_start_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("End date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_end_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_end_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	timePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("range_start_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("range_end_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaOnTimeSelectionChange);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_values_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);

	// setup datepickers for date range selection textboxes
	this.setupDatePickers();
}

/**
 * creates HTML elements for messages tab
 * @param		HTMLElement		tabPageElement		default tab's page element
 */
SearchCriteria.prototype.createHTMLElementsForMessages = function(tabPageElement)
{
	// create html elements for messages
	var defaultPart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push(				_("Search for the words(s)") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='subject_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push(				_('In') + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<select id='searchfiltertarget_" + this.widgetID + "' class='combobox inputwidth'>");
	tableData.push("				<option value='subject_only' selected>" + _("subject field only") + "</option>");
	tableData.push("				<option value='subject_with_body'>" + _("subject field and message body") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	defaultPart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("subject_" + this.widgetID, "input", defaultPart), "contextmenu", forceDefaultActionEvent);

	var attendeePart = dhtml.addElement(tabPageElement, "div", "properties");

	tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			<button id='button_from_" + this.widgetID + "' type='button' class='button'>" + _("From") + "...</button>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='from_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			<button id='button_sent_to_" + this.widgetID + "' type='button' class='button'>" + _("Sent To") + "...</button>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='sent_to_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth'>");
	tableData.push("			<input type='checkbox' id='owner_place_checkbox_" + this.widgetID + "'>");
	tableData.push("				<label for='owner_place_checkbox_" + this.widgetID + "'>" + _("Where I am") + ":</label>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<select id='owner_place_combobox_" + this.widgetID + "' class='combobox disabled inputwidth' disabled>");
	tableData.push("				<option value='only_person_on_to_line' selected>" + _("the only person on the To line") + "</option>");
	tableData.push("				<option value='on_to_line'>" + _("on the To line with other persons") + "</option>");
	tableData.push("				<option value='on_cc_line'>" + _("on the CC line with other persons") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	attendeePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("from_" + this.widgetID, "input", attendeePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("sent_to_" + this.widgetID, "input", attendeePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("button_from_" + this.widgetID, "button", attendeePart), "click", eventSearchCriteriaAddressBookClick);
	dhtml.addEvent(this, dhtml.getElementById("button_sent_to_" + this.widgetID, "button", attendeePart), "click", eventSearchCriteriaAddressBookClick);
	dhtml.addEvent(this, dhtml.getElementById("owner_place_checkbox_" + this.widgetID, "input", attendeePart), "click", eventSearchCriteriaCheckboxStateChange);

	var timePart = dhtml.addElement(tabPageElement, "div");

	tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth' align='right'>");
	tableData.push("			" + _("Time") + ":");
	tableData.push("		</td>");
	tableData.push("		<td colspan='6'>");
	tableData.push("			<select id='time_selection_property_" + this.widgetID + "' class='time_selection_property combobox'>");
	tableData.push("				<option value='none' selected>" + _("none") + "</option>");
	tableData.push("				<option value='received'>" + _("received") + "</option>");
	tableData.push("				<option value='sent'>" + _("sent") + "</option>");
	tableData.push("				<option value='due'>" + _("due") + "</option>");
	tableData.push("				<option value='expires'>" + _("expires") + "</option>");
	tableData.push("				<option value='created'>" + _("created") + "</option>");
	tableData.push("				<option value='modified'>" + _("modified") + "</option>");
	tableData.push("			</select>");
	tableData.push("			<select id='time_selection_values_" + this.widgetID + "' class='time_selection_values combobox disabled' disabled>");
	tableData.push("				<option value='anytime' selected>" + _("anytime") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr style='display: none;'>");
	tableData.push("		<td class='labelwidth' align='right'>" + NBSP + "</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("Start date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_start_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_start_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("		<td align='right' width='10%'>");
	tableData.push("			" + _("End date") + ":" + NBSP);
	tableData.push("		</td>");
	tableData.push("		<td>");
	tableData.push("			<input id='range_end_date_" + this.widgetID + "' class='field inputwidth' type='text' />");
	tableData.push("		</td>");
	tableData.push("		<td width='4%'>");
	tableData.push("			<div id='range_end_date_button_" + this.widgetID + "' class='datepicker'>" + NBSP + "</div>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	timePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("range_start_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("range_end_date_" + this.widgetID, "input", timePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaOnTimeSelectionChange);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_property_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);
	dhtml.addEvent(this, dhtml.getElementById("time_selection_values_" + this.widgetID, "select", timePart), "change", eventSearchCriteriaToggleDateRangeFields);

	// setup datepickers for date range selection textboxes
	this.setupDatePickers();
}

/**
 * creates HTML elements for more choices tab
 * @param		HTMLElement		tabPageElement		more choices tab's page element
 */
SearchCriteria.prototype.createHTMLElementsForMoreChoices = function(tabPageElement)
{
	// create html elements for more choices tab
	var categoriesPart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td class='labelwidth'>");
	tableData.push("			<button id='categories_button_" + this.widgetID + "' class='button' type='button'>" + _("Categories") + "...</button>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='2'>");
	tableData.push("			<input id='categories_" + this.widgetID + "' class='field inputwidth' type='text'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	categoriesPart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("categories_button_" + this.widgetID, "button", categoriesPart), "click", eventSearchCriteriaCategoriesClick);
	dhtml.addEvent(this, dhtml.getElementById("categories_" + this.widgetID, "input", categoriesPart), "change", eventSearchCriteriaCategoriesChange);
	dhtml.addEvent(this, dhtml.getElementById("categories_" + this.widgetID, "input", categoriesPart), "contextmenu", forceDefaultActionEvent);

	var selectionPart = dhtml.addElement(tabPageElement, "div", "properties");

	var tableData = new Array();
	tableData.push("<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("	<tr>");
	tableData.push("		<td colspan='2' style='width: 40%;'>");
	tableData.push("			<input id='read_status_checkbox_" + this.widgetID + "' type='checkbox' />");
	tableData.push("				<label for='read_status_checkbox_" + this.widgetID + "'>" + _("Only items that are") + ":</label>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='3' style='width: 60%;'>");
	tableData.push("			<select id='read_status_combobox_" + this.widgetID + "' class='combobox disabled inputwidth' disabled>");
	tableData.push("				<option value='unread' selected>" + _("unread") + "</option>");
	tableData.push("				<option value='read'>" + _("read") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td colspan='2' style='width: 40%;'>");
	tableData.push("			<input id='attachment_checkbox_" + this.widgetID + "' type='checkbox' />");
	tableData.push("				<label for='attachment_checkbox_" + this.widgetID + "'>" + _("Only items with") + ":</label>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='3' style='width: 60%;'>");
	tableData.push("			<select id='attachment_combobox_" + this.widgetID + "' class='combobox disabled inputwidth' disabled>");
	tableData.push("				<option value='with_attachments' selected>" + _("one or more attachments") + "</option>");
	tableData.push("				<option value='without_attachments'>" + _("no attachments") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td colspan='2' style='width: 40%;'>");
	tableData.push("			<input id='importance_checkbox_" + this.widgetID + "' type='checkbox' />");
	tableData.push("				<label for='importance_checkbox_" + this.widgetID + "'>" + _("Whose importance is") + ":</label>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='3' style='width: 60%;'>");
	tableData.push("			<select id='importance_combobox_" + this.widgetID + "' class='combobox disabled inputwidth' disabled>");
	tableData.push("				<option value='1' selected>" + _("normal") + "</option>");
	tableData.push("				<option value='2'>" + _("high") + "</option>");
	tableData.push("				<option value='0'>" + _("low") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td colspan='2' style='width: 40%;'>");
	tableData.push("			<input id='flag_checkbox_" + this.widgetID + "' type='checkbox' />");
	tableData.push("				<label for='flag_checkbox_" + this.widgetID + "'>" + _("Only items which") + ":</label>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='3' style='width: 60%;'>");
	tableData.push("			<select id='flag_combobox_" + this.widgetID + "' class='combobox disabled inputwidth' disabled>");
	tableData.push("				<option value='marked_completed' selected>" + _("are marked completed") + "</option>");
	tableData.push("				<option value='flagged_by_other'>" + _("are flagged by someone else") + "</option>");
	tableData.push("				<option value='no_flag'>" + _("have no flag") + "</option>");
	tableData.push("				<option value='flagged_by_me'>" + _("are flagged by me") + "</option>");
	tableData.push("			</select>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("	<tr>");
	tableData.push("		<td colspan='2' style='width: 40%;'>");
	tableData.push("			<input id='case_sensitivity_checkbox_" + this.widgetID + "' type='checkbox' />");
	tableData.push("				<label for='case_sensitivity_checkbox_" + this.widgetID + "'>" + _("Match case") + ":</label>");
	tableData.push("		</td>");
	tableData.push("		<td colspan='3' style='width: 60%;'>");
	tableData.push("		</td>");
	tableData.push("	</tr>");
	tableData.push("</table>");
	selectionPart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("read_status_checkbox_" + this.widgetID, "input", selectionPart), "click", eventSearchCriteriaCheckboxStateChange);
	dhtml.addEvent(this, dhtml.getElementById("attachment_checkbox_" + this.widgetID, "input", selectionPart), "click", eventSearchCriteriaCheckboxStateChange);
	dhtml.addEvent(this, dhtml.getElementById("importance_checkbox_" + this.widgetID, "input", selectionPart), "click", eventSearchCriteriaCheckboxStateChange);
	dhtml.addEvent(this, dhtml.getElementById("flag_checkbox_" + this.widgetID, "input", selectionPart), "click", eventSearchCriteriaCheckboxStateChange);

	var sizePart = dhtml.addElement(tabPageElement, "div");

	var tableData = new Array();
	tableData.push("<fieldset class='search_criteria_fieldset'>");
	tableData.push("<legend>" + _("Size (kilobytes)") + "</legend>");
	tableData.push("	<table width='100%' border='0' cellpadding='2' cellspacing='0'>");
	tableData.push("		<tr>");
	tableData.push("			<td class='labelwidth' colspan='2'>");
	tableData.push("				<select id='size_combobox_" + this.widgetID + "' class='combobox inputwidth'>");
	tableData.push("					<option value='any_size' selected>" + _("doesn't matter") + "</option>");
	tableData.push("					<option value='equals'>" + _("equals (approximately)") + "</option>");
	tableData.push("					<option value='between'>" + _("between") + "</option>");
	tableData.push("					<option value='less_than'>" + _("less than") + "</option>");
	tableData.push("					<option value='greater_than'>" + _("greater than") + "</option>");
	tableData.push("				</select>");
	tableData.push("			</td>");
	tableData.push("			<td colspan='3'>");
	tableData.push("				<input id='size_value_1_" + this.widgetID + "' class='size_value field disabled' type='text' value='0' disabled />");
	tableData.push("					<span>" + NBSP + NBSP + _("and") + NBSP + NBSP + "</span>");
	tableData.push("				<input id='size_value_2_" + this.widgetID + "' class='size_value field disabled' type='text' value='0' disabled />");
	tableData.push("			</td>");
	tableData.push("		</tr>");
	tableData.push("	</table>");
	tableData.push("</fieldset>");
	sizePart.innerHTML = tableData.join("");

	// register events
	dhtml.addEvent(this, dhtml.getElementById("size_combobox_" + this.widgetID, "select", sizePart), "change", eventSearchCriteriaOnSizeSelectionChange);
	dhtml.addEvent(this, dhtml.getElementById("size_value_1_" + this.widgetID, "input", sizePart), "contextmenu", forceDefaultActionEvent);
	dhtml.addEvent(this, dhtml.getElementById("size_value_2_" + this.widgetID, "input", sizePart), "contextmenu", forceDefaultActionEvent);
}

/**
 * enables options that are previously diabled in more choices tab
 * @param		HTMLElement		tabPageElement		more choices tab's page element
 */
SearchCriteria.prototype.enableHTMLElementsForMoreChoices = function(tabPageElement)
{
	var checkboxElement = dhtml.getElementById("importance_checkbox_" + this.widgetID, "input", tabPageElement);
	if(checkboxElement.disabled == true) {
		checkboxElement.disabled = false;
		dhtml.removeClassName(checkboxElement.nextSibling.nextSibling, "disabled_text");
	}
	
	checkboxElement = dhtml.getElementById("attachment_checkbox_" + this.widgetID, "input", tabPageElement);
	if(checkboxElement.disabled == true) {
		checkboxElement.disabled = false;
		dhtml.removeClassName(checkboxElement.nextSibling.nextSibling, "disabled_text");
	}
}

/**
 * disables options in more choices tab
 * @param		HTMLElement		tabPageElement		more choices tab's page element
 * @param		String			messageType			selected message type
 */
SearchCriteria.prototype.disableHTMLElementsForMoreChoices = function(tabPageElement, messageType)
{
	switch(messageType) {
		case "contacts":
			// disable importance option
			var checkboxElement = dhtml.getElementById("importance_checkbox_" + this.widgetID, "input", tabPageElement);
			checkboxElement.disabled = true;
			dhtml.addClassName(checkboxElement.nextSibling.nextSibling, "disabled_text");
			break;
		case "notes":
			// disable attachment option
			var checkboxElement = dhtml.getElementById("attachment_checkbox_" + this.widgetID, "input", tabPageElement);
			checkboxElement.disabled = true;
			dhtml.addClassName(checkboxElement.nextSibling.nextSibling, "disabled_text");

			// disable importance option
			var checkboxElement = dhtml.getElementById("importance_checkbox_" + this.widgetID, "input", tabPageElement);
			checkboxElement.disabled = true;
			dhtml.addClassName(checkboxElement.nextSibling.nextSibling, "disabled_text");
			break;
		case "appointments":
		case "tasks":
		case "messages":
		default:
			// no elements disabled
			break;
	}
}

/**
 * resets field values if user has changed message type
 * @param		HTMLElement		containerElement		parent element whose child elements should be reset
 */
SearchCriteria.prototype.resetFieldValues = function(containerElement)
{
	if(typeof containerElement == "undefined" || containerElement == false || containerElement == null) {
		containerElement = this.contentElement;
	}

	// reset html element values
	var inputFields = containerElement.getElementsByTagName("input");
	for(var index = 0; index < inputFields.length; index++) {
		switch(inputFields[index].type) {
			case "checkbox":
			case "radio":
				if(typeof inputFields[index].events != "undefined" && typeof inputFields[index].events.click != "undefined") {
					// if click event is registered then execute it
					if(inputFields[index].checked == true) {
						dhtml.executeEvent(inputFields[index], "click");
					}
				} else {
					inputFields[index].checked = false;
				}
				break;
			case "text":
				if(inputFields[index].id == "size_value_1_" + this.widgetID || inputFields[index].id == "size_value_2_" + this.widgetID) {
					// special case for size textboxes whose default value is 0
					inputFields[index].value = "0";
				} else {
					inputFields[index].value = "";
				}
				break;
		}
	}
	
	// select boxes
	var selectFields = containerElement.getElementsByTagName("select");
	for(var index = 0; index < selectFields.length; index++) {
		selectFields[index].selectedIndex = 0;
		if(typeof selectFields[index].events != "undefined" && typeof selectFields[index].events.change != "undefined") {
			dhtml.executeEvent(selectFields[index], "change");
		}
	}
}

/**
 * removes HTML elements that are created by this widget from specified tab
 * @param		HTMLElement		tabPageElement		tab page element
 */
SearchCriteria.prototype.removeHTMLElements = function(tabPageElement)
{
	// remove events that are registered with particular element
	dhtml.removeEvents(tabPageElement);

	// remove already created elements
	dhtml.deleteAllChildren(tabPageElement);
}

/**
 * @destructor 
 * also calls destructor of tabbar widget
 */
SearchCriteria.prototype.destructor = function()
{
	// remove registered events
	dhtml.removeEvents(this.contentElement);

	// delete all html elements from all tabs
	this.removeHTMLElements(this.tabBar.getTabPage("default"));
	this.removeHTMLElements(this.tabBar.getTabPage("more_choices"));

	// call destructor of tabs widget
	this.tabBar.destructor();

	delete this.tabBar;

	SearchCriteria.superclass.destructor(this);
}

/**
 * Checks and assign the unique categories to input field type
 * @param		HTMLElement		element		input field element for insertrow that contains the selected categories
 * @param		String			categories	list of selected catergories from the category popup window
 */
SearchCriteria.prototype.filterCategories = function(element, categories)
{
	var tempcategories = categories.split(";");
	var categoriesInLowerCase = categories.toLowerCase();
	categoriesInLowerCase = categoriesInLowerCase.split(";");
	var categories = new Array();
	
	for(var outerIndex in categoriesInLowerCase) {
		categoriesInLowerCase[outerIndex] = categoriesInLowerCase[outerIndex].trim();
		flag = 0;
		for (var innerIndex in categories) {
			categories[innerIndex] = categories[innerIndex].trim();
			if (categories[innerIndex].toLowerCase() == categoriesInLowerCase[outerIndex]) {
				flag = 1;
			}
		}
		
		if (flag == 0 && categoriesInLowerCase[outerIndex].length != 0) {
			categories.push(tempcategories[outerIndex]);	
		}
	}
	element.value = categories.join("; ") + ";";
}

/**
 * creates options for time selection selectbox
 * @param		HTMLElement		selectBoxElement		select box
 * @param		Object			selectBoxOptions		options to add in select box
 * @param		String			selectedOptionValue		option value which should be selected
 */
SearchCriteria.prototype.createSelectBoxOptions = function(selectBoxElement, selectBoxOptions, selectedOptionValue)
{
	if(selectBoxElement.length !== 0) {
		// remove previous options
		dhtml.deleteAllChildren(selectBoxElement);
	}

	for(var optionValue in selectBoxOptions) {
		if(typeof selectedOptionValue != "undefined" && selectedOptionValue === selectBoxOptions[optionValue]) {
			selectBoxElement.options[selectBoxElement.options.length] = new Option(selectBoxOptions[optionValue], optionValue, true, true);
		} else {
			selectBoxElement.options[selectBoxElement.options.length] = new Option(selectBoxOptions[optionValue], optionValue, false, false);
		}
	}
}

/**
 * checks all fields for any changed values
 * @param		HTMLElement		containerElement	parent element
 * @return		Boolean			result				returns true if something has changed else false
 */
SearchCriteria.prototype.checkForChangedValues = function(containerElement)
{
	var result = false;

	if(!containerElement) {
		containerElement = this.contentElement;
	}

	// check for all input fields
	var inputFields = containerElement.getElementsByTagName("input");
	for(var index = 0; index < inputFields.length; index++) {
		switch(inputFields[index].type) {
			case "checkbox":
				if(inputFields[index].checked == true) {
					result = true;
					break;
				}
				break;
			case "text":
				if(inputFields[index].id == "size_value_1_" + this.widgetID || inputFields[index].id == "size_value_2_" + this.widgetID) {
					if(parseInt(inputFields[index].value, 10) != 0) {
						result = true;
						break;
					}
				} else if(inputFields[index].value != "") {
					result = true;
					break;
				}
		}
	}

	// now check for select boxes
	if(!result) {
		var selectFields = containerElement.getElementsByTagName("select");
		var checkSelectedIndex = false;
		for(var index = 0; index < selectFields.length; index++) {
			checkSelectedIndex = false;
			switch(selectFields[index].id) {
				case "time_selection_property_" + this.widgetID:
					checkSelectedIndex = true;
					break;
				case "searchfiltertarget_" + this.widgetID:
					checkSelectedIndex = true;
					break;
				case "size_combobox_" + this.widgetID:
					checkSelectedIndex = true;
					break;
				case "status_combobox_" + this.widgetID:
					checkSelectedIndex = true;
					break;
				default:
					checkSelectedIndex = false;
					break;
			}

			if(checkSelectedIndex && selectFields[index].selectedIndex != 0) {
				result = true;
				break;
			}
		}
	}

	return result;
}

/**
 * setup datepickers for date range selection
 */
SearchCriteria.prototype.setupDatePickers = function()
{
	Calendar.setup({
		inputField		:	"range_start_date_" + this.widgetID,				// id of the input field
		ifFormat		:	_('%d-%m-%Y'),					// format of the input field
		button			:	"range_start_date_button_" + this.widgetID,		// trigger for the calendar (button ID)
		step			:	1,								// show all years in drop-down boxes (instead of every other year as default)
		weekNumbers		:	false,
		dependedElement	:	"range_start_date_" + this.widgetID
	});
					
	Calendar.setup({
		inputField		:	"range_end_date_" + this.widgetID,				// id of the input field
		ifFormat		:	_('%d-%m-%Y'),					// format of the input field
		button			:	"range_end_date_button_" + this.widgetID,		// trigger for the calendar (button ID)
		step			:	1,								// show all years in drop-down boxes (instead of every other year as default)
		weekNumbers		:	false,
		dependedElement	:	"range_end_date_" + this.widgetID
	});
}

/**
 * function opens categories selection dialog when anyone clicks on
 * categories button
 * @param		Object			widgetObject		widget object
 * @param		HTMLElement		element				element on which event occured
 * @param		EventObject		event				event object
 */
function eventSearchCriteriaCategoriesClick(widgetObject, element, event) {
	var callBackData = new Object();
	callBackData["widgetObject"] = widgetObject;

	var uri = DIALOG_URL + "task=categories_modal";
	webclient.openModalDialog(widgetObject, "categories", uri, 350, 370, searchCriteriaCategoriesCallBack, callBackData);
}

/**
 * function is called as a callback function for categories selection dialog
 * @param		String		categories		semi-colon seperated categories
 * @param		Object		userData		user defined data that is passed in callback function
 */
function searchCriteriaCategoriesCallBack(categories, userData) {
	var widgetObject = userData["widgetObject"];
	var categoriesInputbox = dhtml.getElementById("categories_" + widgetObject.widgetID, "input", widgetObject.contentElement);
	widgetObject.filterCategories(categoriesInputbox, categories);
}

/**
 * function is called when user manually changes categories in categories input box
 * @param		Object			widgetObject		widget object
 * @param		HTMLElement		element				element on which event occured
 * @param		EventObject		event				event object
 */
function eventSearchCriteriaCategoriesChange(widgetObject, element, event) {
	if(element.value != "") {
		widgetObject.filterCategories(element, element.value);
	}
}

/**
 * function is called when user clicks on addressbook buttons
 * it will open addressbook dialog based on parameters passed
 * @param		Object			widgetObject		widget object
 * @param		HTMLElement		element				element on which event occured
 * @param		EventObject		event				event object
 */
function eventSearchCriteriaAddressBookClick(widgetObject, element, event) {
	var callBackData = new Object();
	callBackData["widgetObject"] = widgetObject;
	var uri = false;

	switch(element.id) {
		// for tasks & messages
		case "button_from_" + widgetObject.widgetID:
			uri = DIALOG_URL + "task=addressbook_modal&storeid=" + widgetObject.moduleObject.storeid + "&fields[from_" + widgetObject.widgetID + "]=" + _("From") + "&fields[sent_to_" + widgetObject.widgetID + "]=" + _("Sent To") + "&dest=from_" + widgetObject.widgetID;
			break;
		case "button_sent_to_" + widgetObject.widgetID:
			uri = DIALOG_URL + "task=addressbook_modal&storeid=" + widgetObject.moduleObject.storeid + "&fields[from_" + widgetObject.widgetID + "]=" + _("From") + "&fields[sent_to_" + widgetObject.widgetID + "]=" + _("Sent To") + "&dest=sent_to_" + widgetObject.widgetID;
			break;
		// for appointments
		case "button_organizer_" + widgetObject.widgetID:
			uri = DIALOG_URL + "task=addressbook_modal&storeid=" + widgetObject.moduleObject.storeid + "&fields[organizer_" + widgetObject.widgetID + "]=" + _("Organized By") + "&fields[attendees_" + widgetObject.widgetID + "]=" + _("Attendees") + "&dest=organizer_" + widgetObject.widgetID;
			break;
		case "button_attendees_" + widgetObject.widgetID:
			uri = DIALOG_URL + "task=addressbook_modal&storeid=" + widgetObject.moduleObject.storeid + "&fields[organizer_" + widgetObject.widgetID + "]=" + _("Organized By") + "&fields[attendees_" + widgetObject.widgetID + "]=" + _("Attendees") + "&dest=attendees_" + widgetObject.widgetID;
			break;
		// for contacts
		case "button_email_" + widgetObject.widgetID:
			uri = DIALOG_URL + "task=addressbook_modal&storeid=" + widgetObject.moduleObject.storeid + "&fields[email_" + widgetObject.widgetID + "]=" + _("Email") + "&dest=email_" + widgetObject.widgetID;
			break;
	}

	if(uri) {
		webclient.openModalDialog(widgetObject, 'addressbook', uri, 800, 500, searchCriteriaAddressBookCallBack, callBackData);
	}
}

/**
 * function is called as a callback function for addressbook dialog
 * @param		Object		recipients		recipients array
 * @param		Object		userData		user defined data that is passed in callback function
 */
function searchCriteriaAddressBookCallBack(recipients, userData) {
	for(var inputBoxId in recipients) {
		var inputBox = dhtml.getElementById(inputBoxId, "input", userData.widgetObject.contentElement);
		inputBox.value = recipients[inputBoxId].value;
	}
}

/**
 * function is called when user clicks on checkbox to enable or disable 
 * particular select box
 * @param		Object			widgetObject		widget object
 * @param		HTMLElement		element				element on which event occured
 * @param		EventObject		event				event object
 */
function eventSearchCriteriaCheckboxStateChange(widgetObject, element, event) {
	elementId = element.id;
	elementId = elementId.substring(0, elementId.lastIndexOf("_"));
	elementId = elementId.replace("_checkbox", "");

	// find corresponding combobox
	var targetElement = dhtml.getElementById(elementId + "_combobox_" + widgetObject.widgetID, "select", widgetObject.contentElement);
	if(targetElement) {
		if(targetElement.disabled) {
			targetElement.disabled = false;
			dhtml.removeClassName(targetElement, "disabled");
			if(!element.checked) {		// for IE8
				element.checked = true;
			}
		} else {
			targetElement.disabled = true;
			dhtml.addClassName(targetElement, "disabled");
			if(element.checked) {		// for IE8
				element.checked = false;
			}
		}
	}
}

/**
 * function is called when user changes option in first select box of 
 * time selection part
 * @param		Object			widgetObject		widget object
 * @param		HTMLElement		element				element on which event occured
 * @param		EventObject		event				event object
 */
function eventSearchCriteriaOnTimeSelectionChange(widgetObject, element, event) {
	var selectedOptionElement = element.options[element.selectedIndex];
	var valuesElement = dhtml.getElementById("time_selection_values_" + widgetObject.widgetID, "select", widgetObject.contentElement);

	// if selection is "none" then disable element or enable it
	if(selectedOptionElement.value === "none") {
		valuesElement.disabled = true;
		dhtml.addClassName(valuesElement, "disabled");
	} else {
		if(valuesElement.disabled == true) {
			valuesElement.disabled = false;
			dhtml.removeClassName(valuesElement, "disabled");
		}
	}

	switch(selectedOptionElement.value) {
		case "completed":
		case "created":
		case "modified":
		case "received":
		case "sent":
			// without future options
			widgetObject.createSelectBoxOptions(valuesElement, widgetObject.timeOptionsWithoutFuture, "anytime");
			break;
		case "none":
		case "due":
		case "starts":
		case "ends":
		case "expires":
			// with future options
			widgetObject.createSelectBoxOptions(valuesElement, widgetObject.timeOptionsAll, "anytime");
			break;
	}
}

/**
 * function is called when user selects "select date range" option then we have
 * to show date range selection textboxes
 * @param		Object			widgetObject		widget object
 * @param		HTMLElement		element				element on which event occured
 * @param		EventObject		event				event object
 */
function eventSearchCriteriaToggleDateRangeFields(widgetObject, element, event) {
	var valuesElement = dhtml.getElementById("time_selection_values_" + widgetObject.widgetID, "select", widgetObject.contentElement);
	var rangeStartDateTextBox = dhtml.getElementById("range_start_date_" + widgetObject.widgetID, "input", widgetObject.contentElement);
	var rangeEndDateTextBox = dhtml.getElementById("range_end_date_" + widgetObject.widgetID, "input", widgetObject.contentElement);
	var dateRangeRow = rangeStartDateTextBox.parentNode.parentNode;

	// need to display date fields only when "select date range" option is selected
	if(valuesElement.options[valuesElement.selectedIndex].value == "select_date_range") {
		dateRangeRow.style.display = "";

		widgetObject.moduleObject.resize();
	} else if(dateRangeRow.style.display == "") {
		dateRangeRow.style.display = "none";

		// remove previous values
		rangeStartDateTextBox.value = "";
		rangeEndDateTextBox.value = "";

		widgetObject.moduleObject.resize();
	}
}

/**
 * function is called when user changes option in size select box
 * @param		Object			widgetObject		widget object
 * @param		HTMLElement		element				element on which event occured
 * @param		EventObject		event				event object
 */
function eventSearchCriteriaOnSizeSelectionChange(widgetObject, element, event) {
	var selectedOptionElement = element.options[element.selectedIndex];
	var valuesElement1 = dhtml.getElementById("size_value_1_" + widgetObject.widgetID, "input", widgetObject.contentElement);
	var valuesElement2 = dhtml.getElementById("size_value_2_" + widgetObject.widgetID, "input", widgetObject.contentElement);

	switch(selectedOptionElement.value) {
		case "equals":
		case "less_than":
		case "greater_than":
			// disable second textbox
			valuesElement1.disabled = false;
			dhtml.removeClassName(valuesElement1, "disabled");
			valuesElement2.disabled = true;
			dhtml.addClassName(valuesElement2, "disabled");
			break;
		case "between":
			// enable both textboxes
			valuesElement1.disabled = false;
			dhtml.removeClassName(valuesElement1, "disabled");
			valuesElement2.disabled = false;
			dhtml.removeClassName(valuesElement2, "disabled");
			break;
		case "any_size":
		default:
			// disable both textboxes
			valuesElement1.disabled = true;
			dhtml.addClassName(valuesElement1, "disabled");
			valuesElement2.disabled = true;
			dhtml.addClassName(valuesElement2, "disabled");
			break;
	}
}