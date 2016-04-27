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

function emailAddressSubmit() {
	var name = (dhtml.getElementById("name", "input").value).trim();
	var email = (dhtml.getElementById("email", "input").value).trim();
	var internalId = dhtml.getElementById("internalId").value;
	if (email==""){
		alert(_("Please input a valid email address!"));
		return false;
	}else if (validateEmailAddress(email, true)){
		var result = new Object;
		result.name = name;
		result.email = email;
		result.internalId = internalId;
		
		window.resultCallBack(result, window.callBackData);
		return true;
	}
	dhtml.getElementById("email", "input").focus();
	return false;
}
