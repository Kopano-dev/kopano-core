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

function calendar_loadSettings(settings){
   	var value;
   	var field;
	
	value = settings.get("calendar/workdaystart",9*60);
	field = dhtml.getElementById("calendar_workdaystart");
	field.value = value;

	value = settings.get("calendar/workdayend",17*60);
	field = dhtml.getElementById("calendar_workdayend");
	field.value = value;
	
	value = settings.get("calendar/vsize", 2);
	field = dhtml.getElementById("calendar_size");
	field.value = parseInt(value);

	value = settings.get("calendar/appointment_time_size", 2);
	field = dhtml.getElementById("calendar_appointment_size");
	field.value = parseInt(value);

	value = settings.get("calendar/mucalendar/zoomlevel", 3);
	field = dhtml.getElementById("calendar_mucalendar_zoomlevel");
	field.value = parseInt(value);

	value = settings.get("calendar/mucalendar/numofdaysloaded", 5);
	field = dhtml.getElementById("calendar_mucalendar_numofdaysloaded");
	field.value = parseInt(value);

	value = settings.get("calendar/reminder","true");
	field = dhtml.getElementById("calendar_reminder");
	field.checked =(value=="true");

	value = settings.get("calendar/reminder_minutes", 15);
	field = dhtml.getElementById("calendar_reminder_minutes");
	field.value = parseInt(value);
	field.disabled = (dhtml.getElementById("calendar_reminder").checked)?false:true;	

	value = settings.get("calendar/calendar_refresh_button", "true");
	field = dhtml.getElementById("calendar_refresh_button");
	field.checked = (value == "true")?true:false;	
}

function calendar_saveSettings(settings){
	var value;
	var field;

	old_value = settings.get("calendar/workdaystart",9*60);
	field = dhtml.getElementById("calendar_workdaystart");
	settings.set("calendar/workdaystart",field.value);
	if (old_value != field.value){
		reloadNeeded = true;
	}

	old_value = settings.get("calendar/workdayend",17*60);
	field = dhtml.getElementById("calendar_workdayend");
	settings.set("calendar/workdayend",field.value);
	if (old_value != field.value){
		reloadNeeded = true;
	}
	
	old_value = settings.get("calendar/vsize", 2);
	field = dhtml.getElementById("calendar_size");
	settings.set("calendar/vsize",parseInt(field.value));
	if (old_value != parseInt(field.value)){
		reloadNeeded = true;
	}

	old_value = settings.get("calendar/appointment_time_size", 2);
	field = dhtml.getElementById("calendar_appointment_size");
	settings.set("calendar/appointment_time_size",parseInt(field.value));
	if (old_value != parseInt(field.value)){
		reloadNeeded = true;
	}

	old_value = settings.get("calendar/mucalendar/zoomlevel", 3);
	field = dhtml.getElementById("calendar_mucalendar_zoomlevel");
	settings.set("calendar/mucalendar/zoomlevel",parseInt(field.value));
	if (old_value != parseInt(field.value)){
		reloadNeeded = true;
	}
	
	old_value = settings.get("calendar/mucalendar/numofdaysloaded", 5);
	field = dhtml.getElementById("calendar_mucalendar_numofdaysloaded");
	settings.set("calendar/mucalendar/numofdaysloaded",parseInt(field.value));
	if (old_value != parseInt(field.value)){
		reloadNeeded = true;
	}

	old_value = settings.get("calendar/reminder","true");
	field = dhtml.getElementById("calendar_reminder");
	settings.set("calendar/reminder",field.checked?"true":"false");
	if (old_value != (field.checked?"true":"false")){
		reloadNeeded = true;
	}

	old_value = settings.get("calendar/reminder_minutes", 15);
	field = dhtml.getElementById("calendar_reminder_minutes");	
	settings.set("calendar/reminder_minutes",parseInt(field.value));
	if (old_value != parseInt(field.value)){
		reloadNeeded = true;
	}

	old_value = settings.get("calendar/calendar_refresh_button", "true");
	field = dhtml.getElementById("calendar_refresh_button");	
	settings.set("calendar/calendar_refresh_button", (field.checked)?"true":"false");
	if (old_value != (field.checked?"true":"false")){
		reloadNeeded = true;
	}
}

function calendar_checkworkhours(element)
{
	var beginEl = dhtml.getElementById("calendar_workdaystart");
	var endEl = dhtml.getElementById("calendar_workdayend");

	var begin = parseInt(beginEl.value,10);
	var end = parseInt(endEl.value,10);

	if (element==endEl) {
		if (end<=begin) {
			begin = end - 30;
		}
		if(begin<0){
			begin=0;
			end=30;
		}
	}else{
		if (begin>=end) {
			end = begin+30;
		}
		if (end>1410) {
			end = 1410;
			begin = end-30;
		}
	}
	beginEl.value = begin;
	endEl.value = end;
}

function calendar_checkremindersetting(element)
{
	var checkbox_reminder = dhtml.getElementById("calendar_reminder");
	
	if(checkbox_reminder) {
		var select_reminder_minutes = dhtml.getElementById("calendar_reminder_minutes");

		if(checkbox_reminder.checked) {
			select_reminder_minutes.disabled = false;
			select_reminder_minutes.style.background = "#FFFFFF";

		} else {
			select_reminder_minutes.disabled = true;
			select_reminder_minutes.style.background = "#DFDFDF";
		}
	}
}

function calendar_checkcalendarcellsize(element)
{
	var checkbox_size = dhtml.getElementById("calendar_size");
	var calendar_appointment_size = dhtml.getElementById("calendar_appointment_size");
	
	if(checkbox_size.value == 1){
		if(calendar_appointment_size.value == 1){
			alert(_("This combination of Vertical Size and Calendar resolution is invalid"));
			checkbox_size.value = webclient.settings.get("calendar/vsize", 2);
			calendar_appointment_size.value = webclient.settings.get("calendar/appointment_time_size", 2);
		}
	}
}