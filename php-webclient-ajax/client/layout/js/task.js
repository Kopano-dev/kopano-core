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

function initTask()
{
	// Status
	dhtml.setValue(dhtml.getElementById("select_status"), dhtml.getElementById("status").value);
	
	// Priority
	dhtml.setValue(dhtml.getElementById("select_priority"), dhtml.getElementById("importance").value);

	// Duedate
	dhtml.setDate(dhtml.getElementById("commonend"));
	
	// Startdate
	dhtml.setDate(dhtml.getElementById("commonstart"));
	
	// Complete
	var complete = dhtml.getElementById("complete");
	if(complete && complete.value == "1") {
		dhtml.setValue(dhtml.getElementById("select_status"), 2);
	}
	
	// Percent Complete
	var percent_complete = dhtml.getElementById("percent_complete");
	if(percent_complete) {
		var text_percent_complete = dhtml.getElementById("text_percent_complete");
		
		if(text_percent_complete) {
			text_percent_complete.value = (percent_complete.value * 100) + "%";
		}
	}
	
	// Reminder
	var reminder = dhtml.getElementById("reminder");
	if(reminder) {
		if(reminder.value == "1") {
			dhtml.setValue(dhtml.getElementById("checkbox_reminder"), true);
			
			var text_reminderdate = dhtml.getElementById("text_reminderdate");
			text_reminderdate.disabled = false;
			text_reminderdate.style.background = "#FFFFFF";
			
			var text_reminderdate_time = dhtml.getElementById("text_reminderdate_time");
			text_reminderdate_time.disabled = false;
			text_reminderdate_time.style.background = "#FFFFFF";
		}
		
		dhtml.setDate(dhtml.getElementById("reminderdate"));
		dhtml.setTime(dhtml.getElementById("reminderdate"));
	}
	
	// Date Completed
	dhtml.setDate(dhtml.getElementById("datecompleted"));
	
	// Total Work
	var totalwork = dhtml.getElementById("totalwork");
	if(totalwork) {
		var text_totalwork = dhtml.getElementById("text_totalwork");
		
		if(text_totalwork) {
			totalwork.value /= 60;
			text_totalwork.value = totalwork.value + " " + ((totalwork.value==1) ? _("hour") : _("hours"));
			dhtml.executeEvent(text_totalwork, "change");
		}
	}
	
	// Actual Work
	var actualwork = dhtml.getElementById("actualwork");
	if(actualwork) {
		var text_actualwork = dhtml.getElementById("text_actualwork");
		
		if(text_actualwork) {
			actualwork.value /= 60;
			text_actualwork.value = actualwork.value + " " + ((actualwork.value==1) ? _("hour") : _("hours"));
		}
	}
	
	// Private
	if(dhtml.getElementById("sensitivity").value == "2") {
		dhtml.setValue(dhtml.getElementById("checkbox_private"), true);
	}

}

/* send, accept and decline are all exclusive */
function submitTask(send)
{
	// Status
	dhtml.getElementById("status").value = dhtml.getValue(dhtml.getElementById("select_status"));
	
	// Priority
	dhtml.getElementById("importance").value = dhtml.getValue(dhtml.getElementById("select_priority"));

	// Validate dates
	onChangeDate();
	
	// Duedate
    var duedate = Date.parseDate(dhtml.getElementById("text_commonend").value, _("%d-%m-%Y"), true, true);

    if(duedate) {
        dhtml.getElementById("duedate").value = duedate.getTime()/1000;
        dhtml.getElementById("commonend").value = Date.parseDate(dhtml.getElementById("text_commonend").value, _("%d-%m-%Y"), false, true).getTime()/1000;

		var currentDate = Date.parseDate(new Date().toDate(), _("%d-%m-%Y"), false, true);

		if (send == "request" && currentDate.getTime() > duedate.getTime()) {
			if (!confirm(_("The due date for this task has passed. Are you sure you want to continue?"))) return false;
		}
    } else {
        dhtml.getElementById("duedate").value = "";
        dhtml.getElementById("commonend").value = "";
    }

	// Startdate
    var startdate = Date.parseDate(dhtml.getElementById("text_commonstart").value, _("%d-%m-%Y"), true, true);
    
    if(startdate) {
        dhtml.getElementById("startdate").value = startdate.getTime()/1000;
        dhtml.getElementById("commonstart").value = Date.parseDate(dhtml.getElementById("text_commonstart").value, _("%d-%m-%Y"), false, true).getTime()/1000;
    } else {
        dhtml.getElementById("startdate").value = "";
        dhtml.getElementById("commonstart").value = "";
    }
	
	// regular date expression to check whether entered value is date or not.
	var dateRegExPattern = /^((((0?[1-9]|[12]\d|3[01])[\.\-\/](0?[13578]|1[02])[\.\-\/]((1[6-9]|[2-9]\d)?\d{2}))|((0?[1-9]|[12]\d|30)[\.\-\/](0?[13456789]|1[012])[\.\-\/]((1[6-9]|[2-9]\d)?\d{2}))|((0?[1-9]|1\d|2[0-8])[\.\-\/]0?2[\.\-\/]((1[6-9]|[2-9]\d)?\d{2}))|(29[\.\-\/]0?2[\.\-\/]((1[6-9]|[2-9]\d)?(0[48]|[2468][048]|[13579][26])|((16|[2468][048]|[3579][26])00)|00)))|(((0[1-9]|[12]\d|3[01])(0[13578]|1[02])((1[6-9]|[2-9]\d)?\d{2}))|((0[1-9]|[12]\d|30)(0[13456789]|1[012])((1[6-9]|[2-9]\d)?\d{2}))|((0[1-9]|1\d|2[0-8])02((1[6-9]|[2-9]\d)?\d{2}))|(2902((1[6-9]|[2-9]\d)?(0[48]|[2468][048]|[13579][26])|((16|[2468][048]|[3579][26])00)|00))))$/;
	
	//duedate validation
	if(dhtml.getElementById("text_commonend").value.toLowerCase() != _("None").toLowerCase() && !dateRegExPattern.test(Date.parseDate(dhtml.getElementById("text_commonend").value, _("%d-%m-%Y"), true).toDate())){
		alert(_("Please enter correct date"));
		dhtml.getElementById("text_commonend").focus();
		return false;
	}
	//startdate validation
	if(dhtml.getElementById("text_commonstart").value.toLowerCase() != _("None").toLowerCase() && !dateRegExPattern.test(Date.parseDate(dhtml.getElementById("text_commonstart").value, _("%d-%m-%Y"), true).toDate())){
		alert(_("Please enter correct date"));
		dhtml.getElementById("text_commonstart").focus();
		return false;
	}

	// Complete
	if(dhtml.getElementById("status").value == "2") {
		dhtml.getElementById("complete").value = "1";
	} else {
		dhtml.getElementById("complete").value = "-1";
	}
	
	// Percent Complete
	var text_percent_complete = dhtml.getElementById("text_percent_complete");
	if(text_percent_complete) {
		var percent = text_percent_complete.value;
		if(percent.indexOf("%") >= 0) {
			percent = percent.substring(0, percent.indexOf("%"));
		}
		
		percent = parseInt(percent);
		
		if(percent >= 0) {
			dhtml.getElementById("percent_complete").value = percent / 100;
		}
	}
	
	// Reminder
	var reminderset = dhtml.getValue(dhtml.getElementById("checkbox_reminder"));
	if(reminderset) {
		dhtml.getElementById("reminder").value = "1";
	} else {
		dhtml.getElementById("reminder").value = "-1";
	}
	
	// Reminder Date
	if (dhtml.getElementById("text_reminderdate").value != "" && dhtml.getElementById("text_reminderdate").value.toLowerCase() != _("None").toLowerCase()){
		dhtml.getElementById("reminderdate").value =  Date.parseDate(dhtml.getElementById("text_reminderdate").value + " " + dhtml.getElementById("text_reminderdate_time").value, _("%d-%m-%Y") + " " + _("%H:%M")).getTime()/1000;
		dhtml.getElementById("flagdueby").value = dhtml.getElementById("reminderdate").value;		// flagdueby property used in reminders
	}

	// Date Completed
	if (dhtml.getElementById("text_datecompleted").value != "" && dhtml.getElementById("text_datecompleted").value.toLowerCase() != _("None").toLowerCase()){
		dhtml.getElementById("datecompleted").value = Date.parseDate(dhtml.getElementById("text_datecompleted").value, _("%d-%m-%Y"), true).getTime()/1000;
	}
	
	// Total Work
	var text_totalwork = dhtml.getElementById("text_totalwork");
	if(text_totalwork) {
		var totalwork = parseInt(text_totalwork.value);
		
		if(totalwork >= 0) {
			dhtml.getElementById("totalwork").value = totalwork * 60;
		}
	}

	// Actual Work
	var text_actualwork = dhtml.getElementById("text_actualwork");
	if(text_actualwork) {
		var actualwork = parseInt(text_actualwork.value);
		
		if(actualwork >= 0) {
			dhtml.getElementById("actualwork").value = actualwork * 60;
		}
	}
	
	// Private
	var checkbox_private = dhtml.getElementById("checkbox_private");
	if(checkbox_private.checked) {
		dhtml.getElementById("sensitivity").value = "2";
		dhtml.getElementById("private").value = "1";
	} else {
		dhtml.getElementById("sensitivity").value = "0";
		dhtml.getElementById("private").value = "-1";
	}
	
	// Contacts
	dhtml.getElementById("contacts_string").value = dhtml.getElementById("contacts").value;
	
	if (submit_task(send)) window.close();
}

function onChangeStatus()
{
	var select_status = dhtml.getElementById("select_status");
	if(select_status) {
		var selected = select_status.options[select_status.selectedIndex].value;
		
		if(selected == "2") {
			dhtml.getElementById("text_percent_complete").value = "100%";
			//check reminder and turn it off
			setReminderOnComplete();
			showHideRecurrenceMenu(true);
		}else {
			dhtml.getElementById("text_percent_complete").value = "0%";
			//check reminder and turn it on
			setReminderOnComplete(true);
			showHideRecurrenceMenu(false);
		}
		
		dhtml.getElementById("status").value = selected;
	}
}

function completeSpinnerUp()
{
	var text_percent_complete = dhtml.getElementById("text_percent_complete");
	if(text_percent_complete) {
		var percent = text_percent_complete.value;
		
		if(percent.indexOf("%") >= 0) {
			percent = percent.substring(0, percent.indexOf("%"));
		}
		
		percent = parseInt(percent);
		if(percent >= 0) {
			if(percent >= 0 && percent <= 24) {
				percent = 25;
			} else if(percent >= 25 && percent <= 49) {
				percent = 50;
			} else if(percent >= 50 && percent <= 74) {
				percent = 75;
			} else if(percent >= 75 && percent <= 100) {
				percent = 100;
			} else {
				percent = dhtml.getElementById("percent_complete").value * 100;
			}
			
			dhtml.getElementById("percent_complete").value = (percent / 100);
		} else {
			percent = dhtml.getElementById("percent_complete").value * 100;
		}
		
		text_percent_complete.value = percent + "%";
		
		if(percent == 100) {
			dhtml.setValue(dhtml.getElementById("select_status"), 2);
			//check reminder and turn it off
			setReminderOnComplete();
		} else if(percent < 100) {
			if(dhtml.getValue(dhtml.getElementById("select_status")) == "0" || 
			   dhtml.getValue(dhtml.getElementById("select_status")) == "2") {
				dhtml.setValue(dhtml.getElementById("select_status"), 1);
			}
		}
	}
}

function completeSpinnerDown()
{
	var text_percent_complete = dhtml.getElementById("text_percent_complete");
	if(text_percent_complete) {
		var percent = text_percent_complete.value;
		
		if(percent.indexOf("%") >= 0) {
			percent = percent.substring(0, percent.indexOf("%"));
		}
		
		percent = parseInt(percent);
		if(percent >= 0) {
			if(percent >= 0 && percent <= 25) {
				percent = 0;
			} else if(percent >= 26 && percent <= 50) {
				percent = 25;
			} else if(percent >= 51 && percent <= 75) {
				percent = 50;
			} else if(percent >= 76 && percent <= 100) {
				percent = 75;
			} else {
				percent = dhtml.getElementById("percent_complete").value * 100;
			}
			
			text_percent_complete.value = percent + "%";
			dhtml.getElementById("percent_complete").value = (percent / 100);
		} else {
			percent = dhtml.getElementById("percent_complete").value * 100;			
		}
		
		text_percent_complete.value = percent + "%";
		
		if(percent < 100) {
			dhtml.setValue(dhtml.getElementById("select_status"), 1);
			//check reminder and turn it on
			setReminderOnComplete(true);
		}
	}
}

function onChangeReminder()
{
	var checkbox_reminder = dhtml.getElementById("checkbox_reminder");
	
	if(checkbox_reminder) {
		var reminder = dhtml.getElementById("reminder");
		var text_reminderdate = dhtml.getElementById("text_reminderdate");
		var text_reminderdate_time = dhtml.getElementById("text_reminderdate_time");
		
		if(checkbox_reminder.checked) {
			text_reminderdate.disabled = false;
			text_reminderdate.style.background = "#FFFFFF";
			
			if(text_reminderdate.value == _("None") || text_reminderdate.value == "") {
				var date = new Date();
				
				var unixtime = Date.parseDate(dhtml.getElementById("text_commonend").value, _("%d-%m-%Y"), true).getTime()/1000;
				if(unixtime) {
					date = new Date(unixtime * 1000);
				}
				
				text_reminderdate.value = (date.getDate() < 10?"0" + date.getDate():date.getDate()) + "-" + (date.getMonth() + 1 < 10?"0" + (date.getMonth() + 1):(date.getMonth() + 1)) + "-" + date.getFullYear();
			}
			
			text_reminderdate_time.disabled = false;
			text_reminderdate_time.style.background = "#FFFFFF";
			
			if(text_reminderdate_time.value == _("None") || text_reminderdate_time.value == "") {
				text_reminderdate_time.value = "09:00";
			}
			
			reminder.value = "1";
		} else {
			text_reminderdate.disabled = true;
			text_reminderdate.style.background = "#DFDFDF";
			
			text_reminderdate_time.disabled = true;
			text_reminderdate_time.style.background = "#DFDFDF";
			
			reminder.value = "-1";
		}
	}
}

function assignTask()
{
	setTaskRequestMode(tdsOWNNEW, tdmtTaskReq);
}

function cancelAssignTask()
{
	setTaskRequestMode(tdsOWN, tdmtNothing);
}

function setTaskRequestMode(taskstate, taskmode)
{
	taskstate = taskstate || dhtml.getElementById("taskstate").value;
	var taskhistory = dhtml.getElementById("taskhistory").value;
	var tasklastdelegate = dhtml.getElementById("tasklastdelegate").value;
	var assignedtime = dhtml.getElementById("assignedtime").getAttribute("unixtime");
	var taskaccepted = dhtml.getElementById("taskaccepted").value;
	taskmode = taskmode || dhtml.getElementById("taskmode").value;

	var taskrequest_recipient = dhtml.getElementById("taskrequest_recipient");
	var taskrequest_settings = dhtml.getElementById("taskrequest_settings");
	var sendbutton = dhtml.getElementById("send");
	var savebutton = dhtml.getElementById("save");
	var assigntaskbutton = dhtml.getElementById("assigntask");
	var taskcompletebutton = dhtml.getElementById("taskcomplete");
	var cancelassigntaskbutton = dhtml.getElementById("cancelassigntask");
	var remindervalues = dhtml.getElementById("remindervalues");
	var extrainfo = dhtml.getElementById("extrainfo");
	var acceptbutton = dhtml.getElementById("accept");
	var declinebutton = dhtml.getElementById("decline");
	var checknamesbutton = dhtml.getElementById("checknames");
	var recurbutton = dhtml.getElementById("recurrence");
	var rettasklistbutton = dhtml.getElementById("return_tasklist");

	var assignee = true;

	taskrequest_recipient.style.display = "none";
	taskrequest_settings.style.display = "none";
	sendbutton.style.display = "none";
	rettasklistbutton.style.display = "none";
	cancelassigntaskbutton.style.display = "none";
	assigntaskbutton.style.display = "none";
	cancelassigntaskbutton.nextSibling.style.display = "none";	// Separator
	taskcompletebutton.style.display = "none";
	taskcompletebutton.nextSibling.style.display = "none";		// Separator
	remindervalues.style.display = "none";
	savebutton.style.display = "none";
	acceptbutton.style.display = "none";
	declinebutton.style.display = "none";
	checknamesbutton.style.display = "none";
	recurbutton.style.display = "none";
	recurbutton.nextSibling.style.display = "none";
	dhtml.addClassName(dhtml.getElementById("tab_disabledtask"), "tab_hide");

	if(taskstate == tdsOWNNEW) {
		if(taskmode == tdmtTaskReq) {
			// Unsent assigned task
			assignee = false;
			taskrequest_recipient.style.display = "block";
			taskrequest_settings.style.display = "block";
			sendbutton.style.display = "block";
			cancelassigntaskbutton.style.display = "block";
			checknamesbutton.style.display = "block";
		} else {
			// Normal task
			savebutton.style.display = "block";
			assigntaskbutton.style.display = "block";
			cancelassigntaskbutton.nextSibling.style.display = "block";	// Separator
			remindervalues.style.display = "block";
			taskcompletebutton.style.display = "block";
			taskcompletebutton.nextSibling.style.display = "block";
		}
		recurbutton.style.display = "block";
		recurbutton.nextSibling.style.display = "block";
	} else if(taskstate == tdsACC || taskstate == tdsNOM) {
		// Assigner's view of task
		assignee = false;
		taskrequest_recipient.style.display = "block";
		taskrequest_settings.style.display = "block";

		changeTaskEditingState(true, taskstate);
	} else if(taskstate == tdsDEC) {
		// Assigner's view of task, assignee declined
		assignee = false;

		// Assigner's is reassigning this decline task request
		if (taskmode == tdmtTaskReq) {
			sendbutton.style.display = "block";
			checknamesbutton.style.display = "block";
			cancelassigntaskbutton.style.display = "block";
			cancelassigntaskbutton.nextSibling.style.display = "block";	// Separator
			taskrequest_recipient.style.display = "block";
			taskrequest_settings.style.display = "block";

			changeTaskEditingState(false, tdsACC);
		} else {
			rettasklistbutton.style.display = "block";
			assigntaskbutton.style.display = "block";
			cancelassigntaskbutton.nextSibling.style.display = "block";	// Separator

			changeTaskEditingState(true, taskstate);
		}
	} else if(taskstate == tdsOWN) {
		// Assignee's view of task
		if(!parseInt(taskaccepted)) {
			acceptbutton.style.display = "block";
			declinebutton.style.display = "block";
			changeTaskEditingState(true, taskstate);
		} else {
			savebutton.style.display = "block";
			assigntaskbutton.style.display = "block";
			cancelassigntaskbutton.nextSibling.style.display = "block";	// Separator
			remindervalues.style.display = "block";

			taskcompletebutton.style.display = "block";
			taskcompletebutton.nextSibling.style.display = "block";
		}
	}

	var taskhistorydesc;

	if(assignee || (dhtml.getElementById("taskstate").value == tdsOWN)) {
		taskhistorydesc = { 	1 : _("Accepted by %u on %d"),
								2 : _("Declined by %u on %d"),
								3 : _("Last update was sent by %u on %d"),
								5 : _("Assigned by %u on %d") };
	} else {
		taskhistorydesc = { 	1 : _("Accepted by %u on %d"),
								2 : _("Declined by %u on %d"),
								3 : _("Last update was received from %u on %d"),
								5 : _("Waiting for response from recipient") };
	}

	if(taskhistorydesc[taskhistory]) {
		var history = taskhistorydesc[taskhistory];

		var time = new Date(assignedtime*1000);

		history = history.replace("%u", tasklastdelegate);
		history = history.replace("%d", time.strftime(_("%d/%m/%Y %H:%M")));

		extrainfo.style.display = "block";
		extrainfo.innerHTML = "<p>" + history + "</p>";
		copyExtraInfo();
	}
	
	resizeBody();
}

function onChangeDateCompleted()
{
	var text_datecompleted = dhtml.getElementById("text_datecompleted");
	
	if(text_datecompleted) {
		var unixtime = Date.parseDate(text_datecompleted.value, _("%d-%m-%Y"), true).getTime()/1000;
		
		if(unixtime && unixtime > 0) {
			dhtml.setValue(dhtml.getElementById("select_status"), "2");
			dhtml.getElementById("text_percent_complete").value = "100%";
		} else {
			dhtml.setValue(dhtml.getElementById("select_status"), dhtml.getElementById("select_status").value);
			dhtml.getElementById("text_percent_complete").value = (dhtml.getElementById("percent_complete").value * 100) + "%";
		}
	}
}

function setTaskCompleted()
{
	dhtml.setValue(dhtml.getElementById("select_status"), "2");
	dhtml.getElementById("text_percent_complete").value = "100%";

	// check reminder and turn it off.
	setReminderOnComplete();
	submitTask();
}

/**
 * Function which turn off/on the reminde on basis of percentage of task completion.
 * 100% task complete means the reminder should be turn off else check if that was on then turn on.
 * @params boolean forceFlag boolean which forcefully turns on the reminder, used when somebody 
									 is playing via task completion (percentage)
 */
function setReminderOnComplete(forceFlag){
	var reminderCheck = dhtml.getElementById("checkbox_reminder");
	if(reminderCheck.checked && !forceFlag){
		dhtml.executeEvent(reminderCheck, "click");
		reminderCheck.checked = false;
	}else if(!reminderCheck.checked && forceFlag){
		if(dhtml.getElementById("previousReminderValue").value != "0"){
		// called from completion box
			dhtml.executeEvent(reminderCheck, "click");
			reminderCheck.checked = true;
		}
	}
	
}

function categoriesCallBack(categories)
{
	moduleObject = webclient.getModulesByName("taskitemmodule")[0];
	moduleObject.filtercategories(dhtml.getElementById("categories"), categories);
}

function abCallBack(recipients)
{
	dhtml.getElementById("contacts").value = recipients['contacts'].value;
}

function onChangeComplete()
{
	var text_percent_complete = dhtml.getElementById("text_percent_complete");
	if(text_percent_complete) {
		var percent = text_percent_complete.value;
		
		// Parse the number the best we can (remove trailing % sign)
		if(percent.indexOf("%") >= 0) {
			percent = percent.substring(0, percent.indexOf("%"));
		}
		
		// Set status to complete if completion level is 100% or more
		if(parseInt(percent) >= 100) {
			dhtml.setValue(dhtml.getElementById("select_status"), 2);
			// check reminder and turn it off
			setReminderOnComplete();
        } else if(parseInt(percent) <= 0) {
			dhtml.setValue(dhtml.getElementById("select_status"), 0);
        } else {
			dhtml.setValue(dhtml.getElementById("select_status"), 1);
        }
	}
}
/**
 * Keycontrol function which saves task.
 */
function eventTaskListKeyCtrlSave(moduleObject, element, event, keys)
{
	switch(event.keyCombination)
	{
		case keys["save"]:
			submitTask();
			break;
	}
}

function callBackTaskRecurrence(recurrence) {
	module.setRecurrence(recurrence);
}

function showHideRecurrenceMenu(hide) {
	var recur_menu = dhtml.getElementById('recurrence');
	if (recur_menu) {
		recur_menu.style.display = hide ? 'none' : 'block';
		recur_menu.nextSibling.style.display = hide ? 'none' : 'block';		//Separator
	}
}

function delete_task_item()
{
	if (dhtml.getElementById("messagechanged").value == 1) {
		if( confirm("This item has changed. Are you sure you want to delete it?") == false)
			return;
	}

	if (module.messageentryid) {
		var taskstate = dhtml.getElementById("taskstate").value;
		var complete = dhtml.getElementById("complete").value;
		var windowData = new Object();
		windowData['subject'] = module.itemProps['subject'];
		windowData['parentModule'] = module;

		if ((taskstate == 2 && complete != 1) || (parseInt(module.itemProps['recurring']) && !parseInt(module.itemProps['dead_occurrence'], 10))) {
			if (taskstate == 2) windowData['taskrequest'] = true;

			webclient.openModalDialog(module, "deletetaskoccurrence", DIALOG_URL+"task=deletetaskoccurrence_modal&entryid="+ module.messageentryid +"&storeid="+ module.storeid +"&parententryid="+ module.parententryid, 300, 220, null, null, windowData);
		} else {
			module.deleteMessage(false);
		}
	} else {
		window.close();
	}

}

function showTaskRecurrence()
{
	var recurring = dhtml.getElementById("recurring").value;
	if (recurring == 1) {
		var recurr = {
			startocc : dhtml.getElementById("startocc").value,
			endocc : dhtml.getElementById("endocc").value,
			start : dhtml.getElementById("start").value,
			end : dhtml.getElementById("end").value,
			term : dhtml.getElementById("term").value,
			regen : dhtml.getElementById("regen").value,
			everyn : dhtml.getElementById("everyn").value,
			type : dhtml.getElementById("type").value,
			subtype : dhtml.getElementById("subtype").value,
			weekdays : dhtml.getElementById("weekdays").value,
			month : dhtml.getElementById("month").value,
			monthday : dhtml.getElementById("monthday").value,
			nday : dhtml.getElementById("nday").value,
			numoccur : dhtml.getElementById("numoccur").value
		};
		var recurtext = getTaskRecurrencePattern(recurr);
		var extrainfo = dhtml.getElementById("recurtext");

		extrainfo.style.display = "block";
		extrainfo.innerHTML = recurtext;
		copyExtraInfo();
	}
}

function checkNamesCallBackTask(resolveObj)
{
	checkNamesCallBack(resolveObj, true);

	//Send Task
	if(window.resolveForSendingMessage === true){
		if (submit_task("request")) window.close();
	}
}

/**
 * Function which enables/disables editing of fields in task dialog.
 *@param boolean disable true if disable editing else false
 *@param integer taskstate denotes state of task whether own or assigned
 */
function changeTaskEditingState(disable, taskstate)
{
	// Disable all input fields of 'Details' Tab
	var details_tab = dhtml.getElementById("details_tab");
	var elements = details_tab.getElementsByTagName("input");
	for(var i=0; i < elements.length; i++) {
		elements[i].disabled = disable;
	}

	// Switch to Disabled Task tab
	if (disable) {
		// Hide original 'Task' tab and show disabled task tab which shows only properties
		dhtml.addClassName(dhtml.getElementById("tab_task"), "tab_hide");
		dhtml.addClassName(dhtml.getElementById("tab_task"), "selectedtab");

		// Hide datepicker trigger for datecompleted field in 'Details' tab
		dhtml.getElementById("text_datecompleted_button").style.display = "none";

		tabbarControl.change("disabledtask");
	} else {
		dhtml.removeClassName(dhtml.getElementById("tab_task"), "tab_hide");
		dhtml.addClassName(dhtml.getElementById("tab_disabledtask"), "tab_hide");

		dhtml.getElementById("text_datecompleted_button").style.display = "block";
		tabbarControl.change("task");
	}

	if (typeof taskstate != "undefined" && taskstate == tdsACC) dhtml.getElementById("create_unassigned_copy").disabled = !disable;
	dhtml.getElementById("display_cc").disabled = true;
}

/**
 * Function which creates unassigned copy of already sent/assigned task.
 */
function createUnassignedCopy()
{
	if (confirm(_("If you create an unassigned copy of this task, you will own the copy, and you will no longer receive updates for the task you assigned."))) {
		dhtml.getElementById("taskstate").value = tdsOWNNEW;
		dhtml.getElementById("taskhistory").value = thNone;
		dhtml.getElementById("ownership").value = olNewTask;
		dhtml.getElementById("delegationstate").value = olTaskNotDelegated;

		// Owner
		dhtml.getElementById("owner").value = webclient.fullname;

		// Subject
		var subject = dhtml.getElementById("subject");
		subject.value = subject.value +" (copy)";

		submit_task("unassign");
		setTaskRequestMode();
		changeTaskEditingState(false);
	}
}

/**
 * Function which reclaims ownership of a declined task
 */
function reclaimOwnerShip()
{
	if (module) {
		dhtml.getElementById('taskstate').value = tdsOWNNEW;
		dhtml.getElementById('taskhistory').value = thNone;
		dhtml.getElementById('ownership').value = olNewTask;
		dhtml.getElementById('delegationstate').value = olTaskNotDelegated;
		dhtml.getElementById('icon_index').value = 1280;
		dhtml.getElementById('owner').value = webclient.fullname;
		dhtml.getElementById("to").value = "";

		submit_task("reclaim");
		setTaskRequestMode();
		changeTaskEditingState(false);
	}
}

function copyExtraInfo()
{
	var disabledExtraInfo = dhtml.getElementById("disabled_extrainfo");
	disabledExtraInfo.style.display = "block";
	disabledExtraInfo.innerHTML = dhtml.getElementById("extrainfo").innerHTML;
	disabledExtraInfo.innerHTML += dhtml.getElementById("recurtext").innerHTML;
}


function eventTaskWorkChange(moduleObject, element, event)
{
	var value = element.value;
	var work = parseFloat(value);

	if (value.indexOf(_("hour")) != -1 || value.indexOf(_("hours")) != -1 || value.indexOf("hour") != -1 || value.indexOf("hours") != -1) {
		if (work >= 10) convertToDays();
	} else if (value.indexOf(_("day")) != -1 || value.indexOf(_("days")) != -1 || value.indexOf("day") != -1 || value.indexOf("days") != -1) {
		if (work/5 >= 1) convertToWeeks(work);
	}

	function convertToDays() {
		if (work/8 >= 5) convertToWeeks(work/8);
		else element.value = Math.round((work/8)*10)/10 +" "+ (work/8 == 1 ? _("day") : _("days"));
	}

	function convertToWeeks(days) {
		element.value = Math.round((days/5)*10)/10 +" "+ (days/5 == 1 ? _("week") : _("weeks"));
	}
}
