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

function vacation_loadSettings(settings){
	var field;
	var value;
	
	// signable
	value = settings.get("outofoffice/set","false");
	field = dhtml.getElementById("vacation_signable");
	field.checked = (value=="true");
	field = dhtml.getElementById("vacation_notsignable");
	field.checked = (value!="true");
	vacation_signableChange();

	// subject, set with default value
	value = settings.get("outofoffice/subject", _("Out of Office") );
	field = dhtml.getElementById("vacation_subject");
	field.value = value;


	// message, set with default value
	value = settings.get("outofoffice/message", _("User is currently out of office") +".");
	field = dhtml.getElementById("vacation_message");
	field.value = value;
}

function vacation_saveSettings(settings){
	var field;

	// signable
	field = dhtml.getElementById("vacation_signable");
	settings.set("outofoffice/set",field.checked?"true":"false");
	// Store current sessionid in settings, to get when setting for out of office was saved.
	settings.set("outofoffice_change_id", parentWebclient.sessionid);

	// subject
	field = dhtml.getElementById("vacation_subject");
	settings.set("outofoffice/subject",field.value);

	// message
	field = dhtml.getElementById("vacation_message");
	settings.set("outofoffice/message",field.value);
}

function vacation_signableChange(){
	var signable = dhtml.getElementById("vacation_signable").checked;	

	dhtml.getElementById("vacation_subject").disabled = !signable;
	dhtml.getElementById("vacation_message").disabled = !signable;
}
