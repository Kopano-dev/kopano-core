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

function compose_loadSettings(settings){
	var value;
	var field;
	
	// format
	value = settings.get("createmail/mailformat","html");
	field = dhtml.getElementById("compose_format");
	field.value = value;
	// font
	value = settings.get("createmail/maildefaultfont","arial");
	field = dhtml.getElementById("compose_font");
	field.value = value;

	// reply to
	value = settings.get("createmail/reply_to","");
	field = dhtml.getElementById("compose_replyto");
	field.value = value;

	// close on reply
	value = settings.get("createmail/close_on_reply","no");
	field = dhtml.getElementById("compose_close_on_reply");
	field.checked = (value=="yes");

	// request read receipt
	value = settings.get("createmail/always_readreceipt","false");
	field = dhtml.getElementById("compose_readreceipt");
	field.checked = (value=="true");

	// autosave unsent emails
	value = settings.get("createmail/autosave","false");
	field = dhtml.getElementById("compose_autosave");
	field.checked = (value=="true");
	value = settings.get("createmail/autosave_interval",3);
	field = dhtml.getElementById("compose_autosave_interval");
	field.value = value;

	/**
	 * When replying/forwarding a message whether original message
	 * should be added with the prefix or without prefix.
	 * Default is to add prefix i.e. "add_prefix".
	 * Other option is "include_original_message" to only include a message in reply without prefix.
	 */
	value = settings.get("createmail/on_message_replies","add_prefix");
	field = dhtml.getElementById("mail_reply_prefix");
	field.value = value;

	value = settings.get("createmail/on_message_forwards","add_prefix");
	field = dhtml.getElementById("mail_forward_prefix");
	field.value = value;

	// cursor position when replying to mail
	value = settings.get("createmail/cursor_position", "start");
	field = dhtml.getElementById("compose_cursorposition");
	field.value = value;

	// maximum height of TO/CC/BCC fields
	value = settings.get("createmail/toccbcc_maxrows", "3");
	field = dhtml.getElementById("compose_toccbcc_maxrows");
	field.value = value;

	// from address
	value = settings.get("createmail/from","");
	field = dhtml.getElementById("compose_email_from_address");
	while(field.options.length > 0){ // clear select box
		field.remove(0);
	}
	var email_addresses = value.split(",");
	for(var i=0;i<email_addresses.length;i++){
		if(email_addresses[i].trim().length > 0){
			field.options[field.options.length] = new Option(email_addresses[i], email_addresses[i]);
		}
	}
	sortSelectBox(field);

	var addButton = dhtml.getElementById("compose_email_add_from");
	dhtml.addEvent(-1, addButton, "mouseover", eventMenuMouseOverTopMenuItem);
	dhtml.addEvent(-1, addButton, "mouseout", eventMenuMouseOutTopMenuItem);

	var delButton = dhtml.getElementById("compose_email_del_from");
	dhtml.addEvent(-1, delButton, "mouseover", eventMenuMouseOverTopMenuItem);
	dhtml.addEvent(-1, delButton, "mouseout", eventMenuMouseOutTopMenuItem);
}

function compose_saveSettings(settings){
	var field;
	
	// format
	field   = dhtml.getElementById("compose_format");
	settings.set("createmail/mailformat",field.value);

	// font
	field = dhtml.getElementById("compose_font");
	settings.set("createmail/maildefaultfont",field.value);

	// reply to
	field = dhtml.getElementById("compose_replyto");
	settings.set("createmail/reply_to",field.value);

	// close on reply
	field = dhtml.getElementById("compose_close_on_reply");
	settings.set("createmail/close_on_reply",field.checked?"yes":"no");

	// request read receipt
	field = dhtml.getElementById("compose_readreceipt");
	settings.set("createmail/always_readreceipt",field.checked?"true":"false");
	
	// autosave unsent emails
	field = dhtml.getElementById("compose_autosave");
	settings.set("createmail/autosave",field.checked?"true":"false");
	field = dhtml.getElementById("compose_autosave_interval");
	settings.set("createmail/autosave_interval", Number(field.value));

	// On message replies
	field = dhtml.getElementById("mail_reply_prefix");
	settings.set("createmail/on_message_replies", field.value);

	// On message forwards
	field = dhtml.getElementById("mail_forward_prefix");
	settings.set("createmail/on_message_forwards", field.value);

	// cursor position
	field = dhtml.getElementById("compose_cursorposition");
	settings.set("createmail/cursor_position", field.value);

	// maximum height of TO/CC/BCC fields
	field = dhtml.getElementById("compose_toccbcc_maxrows");
	settings.set("createmail/toccbcc_maxrows", field.value);

	// from address
	field = dhtml.getElementById("compose_email_from_address");
	var values = new Array();
	for(var i=0;i<field.options.length;i++){
		if(field.options[i].value != ""){
			values[i] = field.options[i].value;
		}
	}
	var value = "";
	if(values.length > 0){
		value = values.join(",");
	}
	settings.set("createmail/from", value);
}

function compose_delFromAddress(){
	var field = dhtml.getElementById("compose_email_from_address");
	if(field.selectedIndex >= 0){
		for(var i=(field.options.length-1);i>=0;i--){
			if(field.options[i].selected){
				field.remove(i);
			}
		}
	}
}

function compose_addCallBack(result) {
	var name = result.name;
	var email = result.email;
	
	var field = dhtml.getElementById("compose_email_from_address");

	if (name.length<1){
		name = email;
	}

	name = name.replace("<", "");
	name = name.replace(">", "");

	field.options[field.options.length] = new Option(name + " <" + email + ">", name + " <" + email + ">");

	sortSelectBox(field);
	
}

function openEditSignatures(){
	webclient.openModalDialog(-1, 'signatures', DIALOG_URL+'&task=signatures_modal', 710, 550);
}

function disableFontOption(){
	var field = dhtml.getElementById("compose_format");
	for(var i=(field.options.length-1);i>=0;i--){
		var font_field = dhtml.getElementById("compose_font");

		if(field.options[i].selected && field.options[i].value == "plain"){
			font_field.disabled = true;
			font_field.style.background = "#DFDFDF";
		}else{
			font_field.disabled = false;
			font_field.style.background = "#FFFFFF";
		}
	}
}