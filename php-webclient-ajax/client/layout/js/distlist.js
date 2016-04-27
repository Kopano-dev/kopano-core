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

function initdistlist()
{
	// Private
	if(dhtml.getElementById("sensitivity").value == "2") {
		dhtml.setValue(dhtml.getElementById("checkbox_private"), true);
	}
}

function saveDistList()
{
	// Private
	var checkbox_private = dhtml.getElementById("checkbox_private");
	if(checkbox_private.checked) {
		dhtml.getElementById("sensitivity").value = "2";
		dhtml.getElementById("private").value = "1";
	} else {
		dhtml.getElementById("sensitivity").value = "0";
		dhtml.getElementById("private").value = "-1";
	}

	var fileas = dhtml.getElementById("fileas").value;
	if(fileas === "") {
	    alert(_('You must specify a name for the distribution list before saving it.'));
	    return;
	}
	dhtml.getElementById("dl_name").value = fileas;
	dhtml.getElementById("subject").value = fileas;
	dhtml.getElementById("display_name").value = fileas;
	module.save();
	window.close();
}

function distlist_categoriesCallback(categories){
	dhtml.getElementById("categories").value = categories;
}

function distlist_addABCallback(result) 
{
	if (result.multiple){
		for(var key in result){
			if (key!="multiple" && key!="value"){
				module.addItem(result[key]);
			}
		}
	}else{
		module.addItem(result);
	}
}

function distlist_addNewCallback(result)
{
	var name = result.name;
	var email = result.email;
	var internalId = result.internalId;

	if (name.length<1)
		name = email;

	name = name.replace("<", "");
	name = name.replace(">", "");
	
	var item = new Object;
	item["addrtype"] = "SMTP";
	item["display_name"] = name;
	item["email_address"] = email;
	item["entryid"] = false;

	if(internalId != "") {
		item["internalId"] = internalId;
	}
	module.addItem(item);
}
/**
 * Keycontrol function which saves distribution list.
 */
function eventDistListKeyCtrlSave(moduleObject, element, event, keys)
{
	switch(event.keyCombination)
	{
		case keys["save"]:
			saveDistList();
			break;
	}
}