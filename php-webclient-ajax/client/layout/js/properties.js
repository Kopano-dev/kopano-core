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

function initFolderProperties(module)
{
	var elements = new Object();
	elements["profile"] = dhtml.getElementById("profile");
	elements["ecRightsCreate"] = dhtml.getElementById("ecRightsCreate");
	elements["ecRightsReadAny"] = dhtml.getElementById("ecRightsReadAny");
	elements["ecRightsCreateSubfolder"] = dhtml.getElementById("ecRightsCreateSubfolder");
	elements["ecRightsFolderAccess"] = dhtml.getElementById("ecRightsFolderAccess");
	elements["ecRightsFolderVisible"] = dhtml.getElementById("ecRightsFolderVisible");

	elements["edit_items"] = document.getElementsByName("edit_items");	
	for(var i=0;i<elements["edit_items"].length;i++){
		elements[elements["edit_items"][i].id] = elements["edit_items"][i];
	}

	elements["del_items"] = document.getElementsByName("del_items");	
	for(var i=0;i<elements["del_items"].length;i++){
		elements[elements["del_items"][i].id] = elements["del_items"][i];
	}

	elements["userlist"] = dhtml.getElementById("userlist");
	dhtml.addEvent(module.id, elements["userlist"], "change", eventPermissionsUserlistChange);

	for(var title in ecRightsTemplate){
		var option = dhtml.addElement(null, "option");
		option.text = title;
		option.value = ecRightsTemplate[title];
		elements["profile"].options[elements["profile"].length] = option;
	}
	var option = dhtml.addElement(null, "option");
	option.text = _("Other");
	option.value = -1;
	elements["profile"].options[elements["profile"].length] = option;
	dhtml.addEvent(module.id, elements["profile"], "change", eventPermissionsProfileChange);


	for(var name in elements){
		if (name.substr(0, 8) == "ecRights"){
			dhtml.addEvent(module.id, elements[name], "click", eventPermissionChange);
		}
	}

	module.permissionElements = elements;

	dhtml.addEvent(module.id, dhtml.getElementById("add_user"), "click", eventPermissionAddUser);
	dhtml.addEvent(module.id, dhtml.getElementById("del_user"), "click", eventPermissionDeleteUser);
	dhtml.addEvent(module.id, dhtml.getElementById("username"), "change", eventPermissionUserElementChanged);
}



/**
 * Function will set all varables in the properties dialog
 */
function setFolderProperties(properties)
{
	dhtml.getElementById("display_name").firstChild.nodeValue = properties["display_name"];

	// set container class
	var container_class = "";
	switch (properties["container_class"]){
		case "IPF.Note":
			container_class = _("Mail and Post");
			break;
		case "IPF.Appointment":
			container_class = _("Calendar");
			break;
		case "IPF.Contact":
			container_class = _("Contact");
			break;
		case "IPF.Journal":
			container_class = _("Journal");
			break;
		case "IPF.StickyNote":
			container_class = _("Note");
			break;
		case "IPF.Task":
			container_class = _("Task");
			break;
		default:
			container_class = properties["container_class"];
	}
	dhtml.getElementById("container_class").firstChild.nodeValue = _("Folder containing")+" "+container_class+" "+_("Items");
	
	// set folder icon
	var folder_icon = properties["container_class"].substr(4).replace(/\./,"_").toLowerCase();
	dhtml.getElementById("folder_icon").className = "folder_big_icon folder_big_icon_folder folder_big_icon_"+folder_icon;

	dhtml.getElementById("parent_display_name").firstChild.nodeValue = properties["parent_display_name"];
	dhtml.getElementById("comment").value = properties["comment"];
	dhtml.getElementById("content_count").firstChild.nodeValue = properties["content_count"];
	dhtml.getElementById("content_unread").firstChild.nodeValue = properties["content_unread"];
	dhtml.getElementById("message_size").firstChild.nodeValue = properties["message_size"];
}

function submitProperties()
{
	if (module){
		var props = new Object();
		props["comment"] = dhtml.getElementById("comment").value;
		
		//Update permissions if changed
		if (module.permissionChanged == true) {
			props["permissions"] = module.getPermissionData();
		}
		module.save(props);
	} else {
		window.close();
	}
}
