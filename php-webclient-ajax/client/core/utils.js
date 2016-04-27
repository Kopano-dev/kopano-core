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
* Adding trim function to the String class
*/
String.prototype.trim = function() 
{ 
	return this.replace(/^[\s\xA0]+|[\s\xA0]+$/g, ""); 
}

String.prototype.isArgument = function()
{
	return /^([a-zA-Z]){1,}=([0-9]){1,}$/.test(this);
}

String.prototype.htmlEntities = function()
{
	var tmp = this.replace(/&/g, "&amp;");
	tmp = tmp.replace(/</g, "&lt;");
	tmp = tmp.replace(/>/g, "&gt;");
	tmp = tmp.replace(/\'/g, "&lsquo;");
	return tmp;
}

/**
 * Returns an string with the extra characters/words "broken".
 *
 * maxLength  maximum amount of characters per line
 * breakWith  string that will be added whenever it's needed to break the line
 * cutWords   if true, the words will be cut, so the line will have exactly "maxLength" characters, otherwise the words won't be cut
 *
 * Original by Jonas Raoni Soares Silva (http://jsfromhell.com/string/wordwrap)
 * Modified by nkallen (http://snippets.dzone.com/posts/show/869)
 */
String.prototype.wordWrap = function(maxLength, breakWidth, cutWords){
	var i, j, s, result = this.split("\n");
	if(maxLength > 0) for(i in result){
		for(s = result[i], result[i] = ""; s.length > maxLength;
				j = cutWords ? maxLength : (j = s.substr(0, maxLength).match(/\S*$/)).input.length - j[0].length
				|| maxLength,
				result[i] += s.substr(0, j) + ((s = s.substr(j)).length ? breakWidth : "")
		   );
		result[i] += s;
	}
	return result.join("\n");
}

String.repeat = function(str, times)
{
	var result = "";
	for(var i=0;i<times;i++){
		result += str;
	}
	return result;
}

/**
* This function is a javascript implementation of sprintf.
* Original made by Jan Moesen, and modified by Michael Erkens for Zarafa.
*
* Original disclaimer:
*
*   This code is in the public domain. Feel free to link back to http://jan.moesen.nu/
*/
String.prototype.sprintf = function()
{
	if (!arguments || arguments.length < 1 || !RegExp) {
		return;
	}

	var str = this;
	var re = /([^%]*)%('.|0|\x20)?(-)?(\d+)?(\.\d+)?(%|b|c|d|u|f|o|s|x|X)(.*)/;
	var a = b = [], numSubstitutions = 0, numMatches = 0;
	while (a = re.exec(str)) {
		var leftpart = a[1], pPad = a[2], pJustify = a[3], pMinLength = a[4];
		var pPrecision = a[5], pType = a[6], rightPart = a[7];

		numMatches++;
		if (pType == '%') {
			subst = '%';
		} else {
			numSubstitutions++;
			if (numSubstitutions > arguments.length) {
				console.log('Error! Not enough function arguments (' + (arguments.length) + ')\nfor the number of substitution parameters in string (' + numSubstitutions + ' so far).');
			}
			var param = arguments[numSubstitutions-1];
			var pad = '';

			if (pPad && pPad.substr(0,1) == "'")
				pad = leftpart.substr(1,1);
			else if (pPad)
				pad = pPad;
			var justifyRight = true;
			if (pJustify && pJustify === "-") 
				justifyRight = false;
			var minLength = -1;
			if (pMinLength) 
				minLength = parseInt(pMinLength);
			var precision = -1;
			if (pPrecision && pType == 'f') 
				precision = parseInt(pPrecision.substring(1));
			var subst = param;
			if (pType == 'b') 
				subst = parseInt(param).toString(2);
			else if (pType == 'c') 
				subst = String.fromCharCode(parseInt(param));
			else if (pType == 'd') 
				subst = parseInt(param) ? parseInt(param) : 0;
			else if (pType == 'u') 
				subst = Math.abs(param);
			else if (pType == 'f') 
				subst = (precision > -1) ? Math.round(parseFloat(param) * Math.pow(10, precision)) / Math.pow(10, precision): parseFloat(param);
			else if (pType == 'o') 
				subst = parseInt(param).toString(8);
			else if (pType == 's') 
				subst = param;
			else if (pType == 'x') 
				subst = ('' + parseInt(param).toString(16)).toLowerCase();
			else if (pType == 'X') 
				subst = ('' + parseInt(param).toString(16)).toUpperCase();
		}
		str = leftpart + subst + rightPart;
	}
	return str;
}

/**
 * create an unique array.
 * and return it.
 */
function uniqueArray(array){
	var obj = new Object();
	var i, arrElem;
	for (i = 0; arrElem = array[i]; i++) {
		obj[arrElem] = 1;
	}
	var resultArr = new Array();
	for (arrElem in obj) {
		resultArr.push (arrElem);
	}
	return resultArr;
}

// sorting select boxes
function sortSelectBox(element)
{
	var txt = new Array();
	var val = new Object();
	
	// store info in temp array's
	for(i=0; i<element.length; i++)  {
	  txt[i] = element.options[i].text;
	  val[txt[i]] = element.options[i].value;
	}

	txt.sort();

	for(i=0; i<txt.length; i++)  {
	  element.options[i].text = txt[i];
	  element.options[i].value = val[txt[i]];
	}
}

// Function will check wheather any type of menu is Open 
function isMenuOpen()
{
	var result = false;

	//context menu
	var contextmenu = dhtml.getElementById("contextmenu");
	if(contextmenu && contextmenu != "undefined"){
		result = true;
	}
	
	//defaultmenu
	var defaultmenu = dhtml.getElementById("defaultmenu");
	if(defaultmenu && defaultmenu.style.display == "block"){
		result = true;
	}

	//combobox
	var comboBoxes = dhtml.comboBoxes;
	for(var i in comboBoxes){
		var combobox = dhtml.getElementById(comboBoxes[i]);
		if(combobox && combobox.style.display == "block"){
			result = true;
		}
	}

	return result;
}

/**
 * Function will make long notation of e-mail address
 * @param name = "string" like: "John Doe"
 * @param email = "string" like: "john.doe@foo.com"
 * @param html = "bool" true will return &lg;john.doe@foo.com&gt; 
 * @return "string" like: "John Doe <john.doe@foo.com>" 
 */
function nameAndEmailToString(name,email,objecttype,html)
{
	var result = "";

	if(objecttype == MAPI_DISTLIST) {
		// Group
		result = "[" + name + "]";
	} else {
		// User
		if(name == email || name == ""){
			result = email;
		} else {
			if(html == true){
				result = name+" &lt;"+email+"&gt;";
			} else {
				result = name+" <"+email+">";
			}
		}
	}
	return result;
}

/**
 * Parse email address into component parts
 * @param address string to parse
 * @return Returns array with "fullname", "emailaddress", "objecttype"
 */
function stringToEmail(address)
{
	var posLeft = address.indexOf("<");
	var posRight = address.indexOf(">");
	var result = new Object;

	if(posLeft >= 0 && posRight >= 0) {
		result["fullname"] = address.substring(0,posLeft).trim();
		result["emailaddress"] = address.substring(posLeft+1,posRight).trim();
		result["objecttype"] = MAPI_MAILUSER;

		return result;
	}

	posLeft = address.indexOf("[");
	posRight = address.indexOf("]");

	if(posLeft >= 0 && posRight >= 0) {
		result["emailaddress"] = address.substring(posLeft+1,posRight).trim();
		result["fullname"] = result["emailaddress"];
		result["objecttype"] = MAPI_DISTLIST;

		return result;
	}

	result["fullname"] = address;
	return result;
}

/**
 * Function will round "value" with "decimals" 
 * NOTE: Math is a native object and can not be prototyped
 */ 
Math.roundDecimal = function(value,decimals)
{
	decimals = Math.pow(10,decimals);
	return Math.round(value*decimals)/decimals;
}

/**
* convert decimals to hex string
*/
function dec2hex(dec) 
{
	var hexChars = "0123456789ABCDEF";
	var a = dec % 16;
	var b = (dec - a)/16;
	return hexChars.charAt(b) + hexChars.charAt(a); 
}

/**
* wrapper for parseInt(,16)
*/
function hex2dec(hex)
{
	return parseInt(hex, 16);
}

/**
* binary string to hex string converter
*/
function bin2hex(bin)
{
	var result = "";
	for(var i=0; i<bin.length;i++){
		result += dec2hex(bin.charCodeAt(i));
	}
	return result;
}

/**
* hex string to binary string converter
*/
function hex2bin(hex)
{
	var result = "";
	for(var i=0; i<hex.length; i+=2){
		result += String.fromCharCode(hex2dec(hex.charAt(i)+hex.charAt(i+1)));
	}
	return result;
}

/**
 * Function will create window.console.log function for debugging
 * Use for firefox the "FireBug" extension, for more options
 */ 
if (!window.console){
	function Console()
	{
	}

	Console.prototype.log = function(msg)
	{
		alert("console.log: "+msg);
	}

	Console.prototype.info = function(msg)
	{
		alert("console.info: "+msg);
	}
	Console.prototype.trace = function()
	{
	}

	window.console = new Console;
}

function escapeHtml(text)
{
	if (text == null) return "";
	if (typeof text != "string") text = text.toString();
	return text	.replace(/&/g,"&amp;")
				.replace(/</g,"&lt;")
				.replace(/>/g,"&gt;");
}

function escapeJavascript(text)
{
	if (text == null) return "";
	if (typeof text != "string") text = text.toString();
	return text	.replace(/\\/g,"\\\\")
				.replace(/\'/g,"\\\'")
				.replace(/\"/g,"\\\"")
				.replace(/\n/g,"\\n")
				.replace(/\r/g,"\\r");
}


function getType(variable)
{
	if (variable == null)
		return "null";

	if (variable.constructor){
		var str = variable.constructor.toString();
		str = str.replace(/\/\/.*\n/g,"\n");
		str = str.replace(/\/\*[^*]*\*+([^\/\*][^*]*\*+)*\//g,"");
	
		var match = /function\s*([a-zA-Z_$][\w$]*)?\s*\(([^)]*)\)/.exec(str);
		return match[1].toLowerCase();
	}

	return typeof(variable);
}


/**
* Converts a DOM object to a simple JavaScript array
* Attributes are ignored
*
*@param object element The DOM element to convert
*@returns Object-array
*/
function dom2array(element)
{
	var result_array = new Object;
	var hasChildTags = false;
	var textValue = "";
	
	for(var i=0; i<element.childNodes.length; i++){
		var child = element.childNodes[i];
		switch (child.nodeType){
			case 1: // tag
				hasChildTags = true;
				if (typeof(child.tagName)!="undefined"){
					if (typeof(result_array[child.tagName])!="undefined"){ // when more tags with the name exists, put them in an array
						if (getType(result_array[child.tagName])!="array"){
							result_array[child.tagName] = new Array(result_array[child.tagName]);
						}
						result_array[child.tagName].push(dom2array(child));
					}else{
						result_array[child.tagName] = dom2array(child);
					}
				}
				break;
			case 3: // textnode
				textValue += child.nodeValue.trim();
				break;
		}
	}
	if (hasChildTags){ // ignore textnodes when we have tags (textnodes contain only whitespace in that case)
		return result_array;
	}else{
		return textValue;
	}
}

function xml2text(element,depth)
{
	var result = "";
	
	if (typeof depth == "undefined") {
		depth = 0;
	}

	for(var i=0; i<element.childNodes.length; i++){
		var child = element.childNodes[i];
		switch (child.nodeType){
			case 1: 
				result += "\n" + String.repeat("\t",depth) + "<"+child.tagName;

				if (child.attributes.length>0){
					for(var j=0;j<child.attributes.length;j++){
						var attribute = child.attributes[j];
						result += " "+attribute.name+"=\""+attribute.value+"\"";
					}
				}
				result += ">\n";

				depth++;
				result += String.repeat("\t", depth)+ xml2text(child, depth).trim();
				depth--;

				result += "\n" + String.repeat("\t", depth);
				result += "</"+child.tagName+">";
				break;
			case 3: 
				result += String.repeat("\t", depth) + child.nodeValue.trim();
				break;
		}
	}
	return result;
}
/**
 * Function which opens a new window or modal window.
 * @param string url the url
 * @param string name name of the window
 * @param string window properties (width, height, etc.)
 * @param boolean isModal true - window is modal, false - window is not modal
 * @param object resultCallback callback function for modal windows
 * @return object window object 
 */ 
function dialog(url, name, feature, isModal, resultCallBack, callBackData, windowData)
{
	var result = null;
	if(url == null) {
		return false;
	}
	
	if(name == null){
		name = "";
	}
	
	if(feature == null){
		feature = "";
	}
	
	if(resultCallBack == null) {
		resultCallBack = false;
	}
	
	if(window.showModelessDialog) { // IE
		if(isModal) {
			feature = feature.replace(/,/g, ";");
			feature = feature.replace(/=/g, ":");
			
			dialogArgs = new Object;
			dialogArgs.dialogName = name;
			dialogArgs.parentWindow = self;
			dialogArgs.resultCallBack = resultCallBack;
			dialogArgs.callBackData = callBackData;
			dialogArgs.windowData = windowData;

			result = window.showModalDialog(url, dialogArgs, feature);
		} else {
			result = window.open(url, name, feature);

			if(resultCallBack) {
				result.resultCallBack = resultCallBack;

				if(callBackData) 
					result.callBackData = callBackData;
			}
			if(windowData){
				result.windowData = windowData;
			}

		}
	} else { // not IE
		if(document.getBoxObjectFor && isModal) {
			var Modal = window.open(url, name, "modal=1," + feature);
			result = Modal;
			
			var ModalFocus = function() {
				if (Modal){
					if(!Modal.closed) {
						Modal.focus();
					} else {
						Modal = null;
						window.removeEventListener("focus", ModalFocus,false);
						ModalFocus = null;
					}
				}
			}
			
			window.addEventListener("focus", ModalFocus, false);
		} else { 
			result = window.open(url,name,feature);
		}

		if(resultCallBack) {
			result.resultCallBack = resultCallBack;

			if(callBackData) 
				result.callBackData = callBackData;
		}
		if(windowData){
			result.windowData = windowData;
		}
	}
/*
	if (!result)
		alert(_("You are using a pop-up blocker. You must disable the pop-up blocker before you can use this function of the WebAccess"));
*/
	return result;
}

/**
 * Function which opens a modal dialog.
 * @param string url the url
 * @param string name name of the window
 * @param string window properties (width, height, etc.)
 * @return object window object
 */ 
function modal(url, name, feature, callback, callbackdata, windowdata)
{
	return dialog(url, name, feature, true, callback, callbackdata, windowdata);
}

/**
* returns the window object for the given window, if window doesn't exists, the window will be created
*/
function getWindowByName(name)
{	
	var result = window.open("", name);
/*
	if (!result)
		alert(_("You are using a pop-up blocker. You must disable the pop-up blocker before you can use this function of the WebAccess"));
*/
	return result;
}

function getSizeOfObject(obj)
{
	var counter = 0;
	for(var i in obj){
		counter++;
	}
	return counter;
}

/**
* Function will validate email address field 
*/
function validateEmailAddress(fieldString, forceSingleAddress, suppressPopupMsg)
{
	var result = "";
	if(!forceSingleAddress){
		var data = fieldString.split(";");
	}else{
		var data = new Array();
		data[0] = fieldString;
	}
	for(i in data){
		// check e-mail address
		var email = data[i].trim();
		// RFC 5322 specifies the format an email address
		var filter = new RegExp(/^([^<]*<){0,1}( *([a-z0-9\.\!\#\$\%\&\'\*\+\-\/\=\?\^\_\`\{\|\}\~])+\@(([a-z0-9\-])+\.)+([a-z0-9]{2,5})+) *>{0,1}$|^\[[^\]]+\]$/i);
		if(email.length > 0 && !filter.test(email)){
			result += "\n"+email;
		}
	}
	if(result.length > 0){
		if (!suppressPopupMsg){
			alert(_("Please input a valid email address!")+result);
		}
		return false;
	} else {
		return true;
	}
}

function parseEmailAddress(address)
{
	// This filter is almost the same as in validateEmailAddress, and could
	// probably be used in both. But because I'm scared of breaking things,
	// here is the modified one. The only difference is that it discards
	// quotes and spaces from the fullname
	
	var filter = new RegExp(/^("?([^<"]*)"? ?<){0,1}( *([a-z0-9=_\+\.\-])+\@(([a-z0-9\-])+\.)+([a-z0-9]{2,5})+) *>{0,1}$/i); 
	
	// It also breaks coloring in my editor, so here is a quote to stop the runaway string "
	
	var matches = filter.exec(address);
	var result;
	
	if(matches) {
		result = new Object;
		result.displayname = matches[2];
		result.emailaddress = matches[3];

		if(result.displayname)
			result.displayname = result.displayname.trim();
		if(result.emailaddress)
			result.emailaddress = result.emailaddress.trim();

	} else {
		result = false;
	}	
	
	return result;
	
}

function iconIndexToClassName(icon_index,messageClass, isRead, isStub)
{
	if(isStub && parseInt(isStub,10))
		return "icon_stubbed";

	var className = "";
	switch(parseInt(icon_index,10)){
		case 2:
				className += "icon_stubbed";
			break;
		case 261:
			if (isRead) {
				className += "icon_mail_replied";
			}else{
				className += "icon_mail_replied_unread";
			}
			break;
		case 262:
			if (isRead) {
				className += "icon_mail_forwarded";
			}else{
				className += "icon_mail_forwarded_unread";
			}
			break;
		case 771:
			className += "icon_stickynote_yellow";
			break;
		case 770:
			className += "icon_stickynote_pink";
			break;
		case 768:
			className += "icon_stickynote_blue";
			break;
		case 769:
			className += "icon_stickynote_green";
			break;
		case 772:
			className += "icon_stickynote_white";
			break;
		case 1280:
			className += "icon_task";
			break;
		case 1281:
			className += "icon_task_recurring";
			break;
		case 1282:
			className += "icon_taskrequest_assigneecopy";
			break;
		case 1283:
			className += "icon_taskrequest_assignercopy";
			break;
		case 512:
			className += "icon_contact";
			break;
		case 514:
			className += "icon_distributionlist";
			break;
		case 1024:
			className += "icon_appointment";
			break;
		case 1025:
			switch(messageClass){
				case "IPM.Schedule.Meeting.Request":
					className += "icon_meetingrequest";
					break;
				case "IPM.Schedule.Meeting.Canceled":
					className += "icon_meetingrequest_canceled";
					break;
				case "IPM.Schedule.Meeting.Resp.Neg":
					className += "icon_meetingrequest_neg";
					break;
				case "IPM.Schedule.Meeting.Resp.Pos":
					className += "icon_meetingrequest_pos";
					break;
				case "IPM.Schedule.Meeting.Resp.Tent":
					className += "icon_meetingrequest_tent";
					break;
				default:
					className += "icon_appointment_recurring";
					break;
			}
			break;
		case 1027:
			className += "icon_meeting_recurring";		
			break;
		case 1028:
			className += "icon_meetingrequest";
			break;
		case 1029:
			className += "icon_meetingrequest_pos";
			break;
		case 1030:
			className += "icon_meetingrequest_neg";
			break;
		case 1031:
			className += "icon_meetingrequest_tent";
			break;
		case 1032:
			className += "icon_meetingrequest_canceled";		
			break;
		case 1033:
			className += "icon_meetingrequest_outofdate";
			break;
		default:
			switch(messageClass){
				case "IPM.Appointment":
					className += "icon_appointment";
					break;
				case "IPM.Task":
					className += "icon_task";
					break;
				case "IPM.TaskRequest":
				case "IPM.TaskRequest.Update":
					className += "icon_taskrequest";
					break;
				case "IPM.TaskRequest.Decline":
					className += "icon_taskrequest_decline";
					break;
				case "IPM.TaskRequest.Accept":
					className += "icon_taskrequest_accepted";
					break;
				case "IPM.StickyNote":
					className += "icon_stickynote_yellow";
					break;
				case "IPM.Contact":
					className += "icon_contact";
					break;
				case "IPM.DistList":
					className += "icon_distributionlist";
					break;
				case "IPM.DistList.Organization":
					className += "icon_company";
					break;
				case "IPM.Schedule.Meeting.Request":
					className += "icon_meetingrequest";
					break;
				case "IPM.Schedule.Meeting.Resp.Pos":
					className += "icon_meetingrequest_pos";
					break;
				case "IPM.Schedule.Meeting.Resp.Tent":
					className += "icon_meetingrequest_tent";
					break;
				case "IPM.Schedule.Meeting.Resp.Neg":
					className += "icon_meetingrequest_neg";
					break;
				case "IPM.Schedule.Meeting.Canceled":
					className += "icon_meetingrequest_canceled";
					break;
				case "REPORT.IPM.Note.IPNNRN":
					className += "icon_report_decline";
					break;
				case "REPORT.IPM.Note.IPNRN":
					className += "icon_report_accept";
					break;
				case "REPORT.IPM.Note.NDR":
					className += "icon_report_ndr";
					break;
				case "IPM.Note.StorageQuotaWarning":
					className += "icon_newmail";
					break;
				default:
					if (isRead){
						className += "icon_mail";
					}else{
						className += "icon_newmail";
					}
					break;
			}
			break;
	}
	return className;
}

/**
 * This function is used to get class name based on display_type property of users in GAB
 * this is used only as a hack to show different icons for gab users,
 * the actual value for display_type and display_type_ex for gab users are different
 */
function displayTypeToClassName(displayType)
{
	var className = "";
	switch(parseInt(displayType, 10)) {
		case DT_MAILUSER:
			className += "icon_contact";
			break;
		case DT_REMOTE_MAILUSER:
			className += "icon_gab_contact";
			break;
		case DT_EQUIPMENT:
			className += "icon_equipment";
			break;
		case DT_ROOM:
			className += "icon_room";
			break;
		case DT_DISTLIST:
		case DT_SEC_DISTLIST:
		case DT_AGENT:
			className += "icon_distributionlist";
			break;
		case DT_ORGANIZATION:
			className += "icon_company";
			break;
	}

	return className;
}

/**
 * This function can be used to get message type based on message class of an item
 * and this message type can be used when opening a dialog box for that item
 */
function messageClassToMessageType(messageClass)
{
	var messageType = false;

	switch(messageClass) {
		case "ipm_note":
		case "ipm_appointment":
		case "ipm_contact":
		case "ipm_task":
		case "ipm_distlist":
		case "ipm_stickynote":
			messageType = messageClass.substring(4);
			break;
		case "ipm_schedule_meeting":
		case "ipm_schedule_meeting_request":
		case "ipm_schedule_meeting_resp":
		case "ipm_schedule_meeting_resp_pos":
		case "ipm_schedule_meeting_resp_tent":
		case "ipm_schedule_meeting_resp_neg":
		case "ipm_schedule_meeting_canceled":

		case "ipm_post":
		case "report_ipm_note_ndr":
		case "report_ipm_note_ipnnrn":
		case "report_ipm_note_ipnrn":
			messageType = "note";
			break;
	}

	return messageType;
}

function convertPlainToHtml(content)
{
	content = content.replace(/&/g, "&amp;");
	content = content.replace(/</g, "&lt;");
	content = content.replace(/>/g, "&gt;");
				
	// replace in body

	// simple text markup *bold* and _underlined_ text
	content = content.replace(/(^|\s)(\*[^\*_\n\r\s][^\*_\n\r]+[^\*_\n\r\s]\*)($|\s)/ig, "$1<strong>$2</strong>$3"); // bold
	content = content.replace(/(^|\s)(_[^_\*\n\r\s][^_\*\n\r]+[^_\*\n\r\s]_)($|\s)/ig, "$1<span style=\"text-decoration: underline\">$2</span>$3"); // underline
				
	// e-mail
	content = content.replace(/(^|\b|\s)(mailto:){0,1}([a-z0-9_'+*$%\^&!\.\-]+\@(?:[a-z0-9\-]+\.)+[a-z]{2,})(\?subject=[a-z0-9\+\%]+){0,1}($|[\s,;>\]\)])/ig, "$1<a href=\"mailto:$3$4\" onclick=\"parent.eventReadmailClickEmail(false,this,\'click\');return false;\">$3$4</a>$5");

	// url
	content = content.replace(/(((https{0,1}|ftp):\/\/([a-z0-9\-\_]+@){0,1}|www\.)([a-z0-9\-]+[a-z0-9\.]*)+\.?[a-z0-9]+(:[0-9]{1,5}){0,1}(\/([a-z0-9\-_#@\?=%+;:\.\[\]~\$!\*'\(\),\{\}|\^`]|&amp;)*)*)/ig, "<a href=\"$1\" target=\"_blank\">$1</a>");
	content = content.replace(/(<a href\=\"www)/ig, "<a href=\"http://www");
		
	return content;
}

function convertAnchors(content)
{
	/**
	 * TODO: enhance this regular expression
	 * It matches all anchor tags instead it should only match if href is specified that starts with '#' tag
	 * and it should also preserve name, target, class etc attributes.
	 */
	content = content.replace(/<a(.*?)(href=\"[ \s]*#[\s]*(.*?)\"(.*?))>/gi, "<a $1 target=\"_self\" href=\"javascript:parent.DHTML.prototype.scrollFrame('html_body', '$3');\" $4>");

	return content;
}

function removeTagFromSource(content, tagName, closingTag, saveDocOnNoClosingTag){
	var openIndex, closeIndex;
	// We look for the tag that matches tagName exactly. The "[ >]" at the end of the RegExp will 
	// match <head> or <head arg="1">, but will not match <header>.
	while((openIndex = content.search(new RegExp("<"+tagName+"[ >]", "gim"))) && openIndex >= 0){
		if(closingTag){
			closeIndex = content.search(new RegExp("</"+tagName+"[ >]", "gim"))
			if(closeIndex >= 0){
				closeIndex = content.indexOf(">", closeIndex) + 1;
			// No closing tag found, use end of string
			}else if(saveDocOnNoClosingTag){
				closeIndex = content.length;
			// Fetch index after tag
			}else{
				closeIndex = content.indexOf(">", openIndex) + 1;
			}
		// No closing tag, only remove single tag
		}else{
			// Fetch index after tag
			closeIndex = content.indexOf(">", openIndex) + 1;
		}
		content = content.slice(0, openIndex) + content.slice(closeIndex, content.length);
	}
	return content;
}

/* 
 * This function collapses a DOM XML describing a rule into a much more readable
 * and easier-to-use javascript object model. It does this by:
 *
 * 1) Putting items that are not arrays into objects with property 'value' containing the nodeValue and property 'attributes' being an object with attrname => attrvalues
 * 2) Putting items that are arrays into javascript Arrays
 *
 * This means we do not support:
 * 1) Arrays with attributes
 * 2) Arrays with content
 * 3) Multiple attributes with the same name
 * .. and possibly some other gnarly XML DOM constructs
 *
 * But it makes it possible to do this
 *
 * rule.restriction[1].property.attributes.proptag = 0x60000003;
 *
 * Instead of
 *
 * getXMLNode(getXMLNode(getXMLNode(rule, "restriction", 1), "property").attributes, "proptag").nodeValue = 0x60000003;
 */

function collapseXML(xmlroot) {
	var result = new Object();
	if(!xmlroot.childNodes)
		return result;
		
	for(var i=0;i<xmlroot.childNodes.length;i++) {
		var isArray = false;
		var node = xmlroot.childNodes.item(i);
	
		switch(node.nodeName) {
			case 'rule_actions':
				isArray = true;
				break;
			case 'restriction':
				if(result.restype && (result.restype.value == "RES_AND" || result.restype.value == "RES_OR"))
					isArray = true;
				break;
			case 'property':
				if(result.restype && (result.restype.value == "RES_COMMENT") || xmlroot.nodeName == "address" )
					isArray = true;

		}

		var isTextNode = node.childNodes.length == 1 && node.childNodes.item(0).nodeType == 3;

		if(isTextNode) {
			child = new Object;
			if(node.firstChild)
				child.value = node.firstChild.nodeValue;

			var attributes = new Object;
			if(node.attributes) {
				for(var j=0;j<node.attributes.length;j++) {
					attributes[node.attributes.item(j).name] = node.attributes.item(j).value;
				}
			}

			child.attributes = attributes;
		} else {
			child = collapseXML(node);
		}
		
		if(isArray) {
			if(typeof(result[node.nodeName]) != "object")
				result[node.nodeName] = new Array();
			result[node.nodeName].push(child);
		} else {
			if(node.nodeName != '#text')
				result[node.nodeName] = child;
		}
	}
	return result;
}

/**
 * Converts a javascript object model to xmlbuilder-compatible
 * objects.
 *
 */
function buildXML(root)
{
	var result = new Object();
	
	if(typeof(root) != "object") {
		return result;
	}

	var key;

	for(key in root) {
		if(typeof(root[key])!="undefined" && typeof(root[key].value) != "object" && typeof(root[key].value) != "undefined") {
			// entry is value, convert to single-item array
			var entry = new Array;
			entry.push(new Object);
			entry[0]["_content"] = root[key].value;
			if(root[key].attributes)
				entry[0]["attributes"] = root[key].attributes;
			else
				entry[0]["attributes"] = new Array();
			
			result[key] = entry;
		} else if(typeof(root[key]) == "object" && typeof(root[key].length) != "undefined") {
			// entry is array
			var entry = new Array();
			for(var i=0;i<root[key].length;i++) {
				if(typeof(root[key][i].value) != "object" && typeof(root[key][i].value) != "undefined") {
					// array of values, convert to multi-value array
					var valentry = new Object;
					valentry["_content"] = root[key][i].value;
					if(root[key][i].attributes)
						valentry["attributes"] = root[key][i].attributes;
					else
						valentry["attributes"] = new Array();
					entry.push(valentry);
				} else {
					// array of objects (array of arrays is not possible), convert to array of subobjects
					entry.push(buildXML(root[key][i]));
				}
			}
			result[key] = entry;
		} else {
			// entry is single object, convert to single-entry array
			var entry = new Array;
			entry.push(buildXML(root[key]));
			result[key] = entry;
		}
	}
	return result;
}

/**
 * Converts a javascript object model to DOM
 * objects.
 *
 */
function buildDOM(root, type)
{
	var dom = new XMLRequest("pre");
	var xml = buildXML(root);
	
	var container = new Object;
	container[type] = xml;
	
	dom.addData(module, type, container);
	
	return dom.xmlbuilder.getXML();
}

/**
 * Function which returns true, if object is Array()
 * @param object Object
 * @return boolean true if object is Array otherwise false.
 */
function isArray(object)
{
	if (object == "undefined" || object.constructor.toString().indexOf("Array") == -1)
		return false;
	else
		return true;
}
/**
 * Function which returns true, if needle is found in the array
 * @param mixed needle
 * @param array haystack
 * @return boolean true needle is found in haystack
 */
function inArray(needle, haystack)
{
	if (typeof needle != "undefined" && isArray(haystack)){
		for(var i in haystack){
			if (haystack[i] === needle) return true;
		}
	}
	return false;
}
/** 
 * Function which searches the array for a given value 
 * and returns the corresponding key if successful 
 * @param mixed needle 
 * @param array haystack 
 * @param boolean strict 
 * @return boolean true needle is found in haystack 
 */ 
function array_search(needle, haystack, strict) 
{ 
	var strict = !!strict; 
	var key = ''; 

	for(key in haystack){ 
		if( (strict && haystack[key] === needle) || (!strict && haystack[key] == needle) ){ 
			return key; 
		} 
	} 
	return false; 
} 
/**
 * Function which return next element value for a given key
 * if key itself is last element then return first element of array.
 * @param mixed needle
 * @param array haystack
 * @return mixed next element
 */
function array_next(needle, haystack)
{
	var returnNext = false;
	var key = array_indexOf(needle, haystack);
	
	for (var i in haystack){
		if (returnNext) return haystack[i];
		if (i == key) returnNext = true;
	}
	for (var i in haystack) return haystack[i];
}
/**
 * Function which return previous element value for a given key
 * if key itself is first element then return last element of array.
 * @param mixed needle
 * @param array haystack
 * @return mixed next element
 */
function array_prev(needle, haystack)
{
	var result = false;
	var key = array_indexOf(needle, haystack);
	
	for (var i in haystack){
		if (i == key) break;
		result = haystack[i];
	}
	if (!result){
		for (var i in haystack) result = haystack[i];
	}
	return result;
}
/**
 * Function which index for given value
 * @param mixed needle
 * @param array haystack
 * @return mixed index
 */
function array_indexOf(needle, haystack)
{
	for (var i in haystack){
		if (haystack[i] == needle) return i;
	}
	return false;
}
/**
 * Function which generates random strings with default length of 8.
 * @param integer length -length of random string to be generated
 * @return string -random string
 */
function generateRandomString(length) {
	var chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXTZabcdefghiklmnopqrstuvwxyz";
	var randomstring = "";
	for (var i=0; i<length; i++) {
		var rnum = Math.floor(Math.random() * chars.length);
		randomstring += chars.substring(rnum,rnum+1);
	}
	return randomstring;
}

/**
 * Function which converts HTML text to PLAIN text
 * @param string content -html text
 * @return string -plain text
 */
function convertHtmlToPlain(content)
{
	//strip all unwanted tags
	content = strip_tags( content, '<title><hr><h1><h2><h3><h4><h5><h6><div><p><pre><sup><ul><ol><dl><dt><table><caption><tr><li><dd><th><td><a><area><img><form><input><textarea><button><select><option>' );
	//remove all options from select inputs
	content = content.replace( /<option[^>]*>[^<]*/i, '');
	//remove all breaklines
	content = content.replace( /<br>[^<]*/i, '\n');
	//replace all tags with their text equivalents
	content = content.replace( /<(h|div|p)[^>]*>/i, "\n\n");
	content = content.replace( /<(ul|ol|br|dl|dt|table|caption|\/textarea|tr[^>]*>\s*<(td|th))[^>]*>/i, "\n");
	content = content.replace( /<li[^>]*>/i, "\n ");
	content = content.replace( /<dd[^>]*>/i, "\n\t");
	content = content.replace( /<(th|td)[^>]*>/i, "\t");
	content = content.replace( /<a[^>]* href=(\"((?!\"|#|javascript:)[^\"#]*)(\"|#)|'((?!'|#|javascript:)[^'#]*)('|#)|((?!'|\"|>|#|javascript:)[^#\"'> ]*))[^>]*>/i, " ");
	//convert html entities
	content = html_entity_decode(content);
	//remove remaining html entities
	content = content.replace( /([&][a-zA-Z]*[;])/g, "");	
	//remove extra newlines
	content = content.replace( /[\n]+/g, "\n");

	return content;
}

/**
 * Function which strips specified tags
 * @param string str  -html text
 * @param string tags -tags
 * @return string -string after removing tags
 */
function strip_tags(str, tags) {
	
	var key = '', tag = '', allowed = false;
	var matches = allowed_array = new Array();
	var allowed_keys = new Object();
	
	var replacer = function(search, replace, str) {
		var tmp_arr = new Array();
		tmp_arr = str.split(search);
		return tmp_arr.join(replace);
	};
	
	// Build allowes tags associative array
	if (tags) {
		tags  = tags.replace(/[^a-zA-Z,]+/g, '');;
		allowed_array = tags.split(',');
	}
	
	// Match tags
	matches = str.match(/(<\/?[^>]+>)/gi);
	
	// Go through all HTML tags 
	for (key in matches) {
		if (isNaN(key)) {
			// IE7 Hack
			continue;
		}
		
		// Save HTML tag
		html = matches[key].toString();
		
		// Is tag not in allowed list? Remove from str!
		allowed = false;
		
		// Go through all allowed tags
		for (k in allowed_array) {
			// Init    
			allowed_tag = allowed_array[k];
			i = -1;
			
			if (i != 0) { i = html.toLowerCase().indexOf('<'+allowed_tag+'>');}
			if (i != 0) { i = html.toLowerCase().indexOf('<'+allowed_tag+' ');}
			if (i != 0) { i = html.toLowerCase().indexOf('</'+allowed_tag)   ;}
			
			// Determine
			if (i == 0) {
				allowed = true;
				break;
			}
		}
		
		if (!allowed) {
			str = replacer(html, "\n", str); // Custom replace. No regexing
		}
	}
	
	return str;
}


/**
 * Function which converts HTML entities
 * into plain text.
 * @param String string
 * @return String with 
 */
function html_entity_decode( string ) {
	var histogram = new Array();
	var histogram_r = new Array();
	var entity = chr = '', code = 0;;

	histogram['34'] = 'quot';
	histogram['38'] = 'amp';
	histogram['60'] = 'lt';
	histogram['62'] = 'gt';
	histogram['153'] = 'trade';
	histogram['160'] = 'nbsp';
	histogram['161'] = 'iexcl';
	histogram['162'] = 'cent';
	histogram['163'] = 'pound';
	histogram['164'] = 'curren';
	histogram['165'] = 'yen';
	histogram['166'] = 'brvbar';
	histogram['167'] = 'sect';
	histogram['168'] = 'uml';
	histogram['169'] = 'copy';
	histogram['170'] = 'ordf';
	histogram['171'] = 'laquo';
	histogram['172'] = 'not';
	histogram['173'] = 'shy';
	histogram['174'] = 'reg';
	histogram['175'] = 'macr';
	histogram['176'] = 'deg';
	histogram['177'] = 'plusmn';
	histogram['178'] = 'sup2';
	histogram['179'] = 'sup3';
	histogram['180'] = 'acute';
	histogram['181'] = 'micro';
	histogram['182'] = 'para';
	histogram['183'] = 'middot';
	histogram['184'] = 'cedil';
	histogram['185'] = 'sup1';
	histogram['186'] = 'ordm';
	histogram['187'] = 'raquo';
	histogram['188'] = 'frac14';
	histogram['189'] = 'frac12';
	histogram['190'] = 'frac34';
	histogram['191'] = 'iquest';
	histogram['192'] = 'Agrave';
	histogram['193'] = 'Aacute';
	histogram['194'] = 'Acirc';
	histogram['195'] = 'Atilde';
	histogram['196'] = 'Auml';
	histogram['197'] = 'Aring';
	histogram['198'] = 'AElig';
	histogram['199'] = 'Ccedil';
	histogram['200'] = 'Egrave';
	histogram['201'] = 'Eacute';
	histogram['202'] = 'Ecirc';
	histogram['203'] = 'Euml';
	histogram['204'] = 'Igrave';
	histogram['205'] = 'Iacute';
	histogram['206'] = 'Icirc';
	histogram['207'] = 'Iuml';
	histogram['208'] = 'ETH';
	histogram['209'] = 'Ntilde';
	histogram['210'] = 'Ograve';
	histogram['211'] = 'Oacute';
	histogram['212'] = 'Ocirc';
	histogram['213'] = 'Otilde';
	histogram['214'] = 'Ouml';
	histogram['215'] = 'times';
	histogram['216'] = 'Oslash';
	histogram['217'] = 'Ugrave';
	histogram['218'] = 'Uacute';
	histogram['219'] = 'Ucirc';
	histogram['220'] = 'Uuml';
	histogram['221'] = 'Yacute';
	histogram['222'] = 'THORN';
	histogram['223'] = 'szlig';
	histogram['224'] = 'agrave';
	histogram['225'] = 'aacute';
	histogram['226'] = 'acirc';
	histogram['227'] = 'atilde';
	histogram['228'] = 'auml';
	histogram['229'] = 'aring';
	histogram['230'] = 'aelig';
	histogram['231'] = 'ccedil';
	histogram['232'] = 'egrave';
	histogram['233'] = 'eacute';
	histogram['234'] = 'ecirc';
	histogram['235'] = 'euml';
	histogram['236'] = 'igrave';
	histogram['237'] = 'iacute';
	histogram['238'] = 'icirc';
	histogram['239'] = 'iuml';
	histogram['240'] = 'eth';
	histogram['241'] = 'ntilde';
	histogram['242'] = 'ograve';
	histogram['243'] = 'oacute';
	histogram['244'] = 'ocirc';
	histogram['245'] = 'otilde';
	histogram['246'] = 'ouml';
	histogram['247'] = 'divide';
	histogram['248'] = 'oslash';
	histogram['249'] = 'ugrave';
	histogram['250'] = 'uacute';
	histogram['251'] = 'ucirc';
	histogram['252'] = 'uuml';
	histogram['253'] = 'yacute';
	histogram['254'] = 'thorn';
	histogram['255'] = 'yuml';
 
	// Reverse table. Cause for maintainability purposes, the histogram is
	// identical to the one in htmlentities.
	for (code in histogram) {
		entity = histogram[code];
		histogram_r[entity] = code;
	}
 
	return (string+'').replace(/(\&([a-zA-Z]+)\;)/g, function(full, m1, m2){
		if (m2 in histogram_r) {
			return String.fromCharCode(histogram_r[m2]);
		} else {
			return m2;
		}
	});
}
/**
 * Function which converts keyCode to char, also for special
 * keys like Ctrl,Alt and Shift.
 */
Number.prototype.fromCharCode = function()
{
	var keyCode = parseInt(this);
	var customKeyStrokes = { 8:"BACKSPACE",
							 9:"TAB",
							13:"ENTER",
							16:"SHIFT",
							17:"CTRL",
							18:"ALT",
							27:"ESCAPE",
							33:"PAGEUP",
							34:"PAGEDOWN",
							35:"END",
							36:"HOME",
							37:"LA",
							38:"UA",
							39:"RA",
							40:"DA",
							45:"INSERT",
							46:"DEL",
						   112:"F1",
						   113:"F2",
						   114:"F3",
						   115:"F4",
						   116:"F5",
						   117:"F6",
						   118:"F7",
						   119:"F8",
						   120:"F9",
						   121:"F10",
						   122:"F11",
						   123:"F12",
						   190:".",
						   188:",",
						   219:"[",
						   221:"]"
	};

	if (customKeyStrokes[keyCode]){
		return customKeyStrokes[keyCode];
	} else {
		return String.fromCharCode(keyCode);
	}
}

/**
 * compareEntryIds
 * Compares two entryIds. It is possible to have two different entryIds that should match as they 
 * represent the same object (in multiserver environments)
 * @param String entryid1 EntryID
 * @param String entryid2 EntryID
 * @return Boolean Result of the comparison 
 */
function compareEntryIds(entryid1, entryid2){
	// data type checking
	if(typeof entryid1 != "string" || typeof entryid2 != "string") {
		return false;
	}

	var eid1 = createEntryIdObj(entryid1);
	var eid2 = createEntryIdObj(entryid2);

	if(eid1.version == eid2.version){

		if(eid1.length != eid2.length)
			return false;

		if(eid1.length < eid1.MIN_LENGTH)
			return false;

		if(eid1.version == '00000000'){
			if(eid1.id != eid2.id)
				return false;
		}else{
			if(eid1.exid != eid2.exid)
				return false;
		}

	}else{

		if(eid1.length < eid1.MIN_LENGTH || eid2.length < eid2.MIN_LENGTH)
			return false;

		if(eid1.id != eid2.id)
			return false;
	}

	if(eid1.guid != eid2.guid)
		return false;

	if(eid1.type != eid2.type)
		return false;

	return true;
}
/**
 * createEntryIdObj
 * Creates an object that has split up all the components of an entryID.
 * @param String entryid Entryid
 * @return Object EntryID object
 */
function createEntryIdObj(entryid){
	var eidObj = {
		flags: '',  	// BYTE[4],   4 bytes,  8 hex characters
		guid: '',   	// GUID,     16 bytes, 32 hex characters
		version: '',	// ULONG,     4 bytes,  8 hex characters
		type: '',   	// ULONG,     4 bytes,  8 hex characters
		id: '',     	// ULONG,     4 bytes,  8 hex characters
		exid: '',   	// TCHAR[1],  1 byte,   2 hex characters
		padding: '',	// TCHAR[3],  3 bytes,  6 hex characters

		length: entryid.length,
		MIN_LENGTH: 72
	}

	eidObj.flags = entryid.substr(0,8);
	eidObj.guid = entryid.substr(8,32);
	eidObj.version = entryid.substr(40,8);
	eidObj.type = entryid.substr(48,8);
	eidObj.id = entryid.substr(56,8);
	eidObj.exid = entryid.slice(64,-6);
	eidObj.padding = entryid.slice(-6);

	return eidObj;
}

function checkFieldsForKeycontrol(event)
{
	var returnValue = true;
	var element = event.target;
	if (inArray(element.tagName.toLowerCase(), new Array("input", "textarea"))) {
		returnValue = (element.getAttribute("type") == "checkbox") ? true : false;
	}
	return returnValue;
}

function getTaskRecurrencePattern(recurr)
{
	// convert all to integer
	for (var i in recurr)
		recurr[i] = parseInt(recurr[i], 10);

	var recurtext = "";
	var startDate = new Date(recurr.start * 1000);

	switch(parseInt(recurr.type)) {
		case 10:
			if(recurr.everyn == 1) {
				recurtext += _("Due every weekday effective %s").sprintf(startDate.print(_("%d-%m-%Y")));
			} else if(recurr.everyn == 1440 && !recurr.regen) {
				recurtext += _("Due every day effective %s").sprintf(startDate.print(_("%d-%m-%Y")));
			} else if (recurr.regen) {
				if ((recurr.everyn / 1440) == 1)
					recurtext += _("Due %d day after this task is completed effective %s").sprintf(1, startDate.print(_("%d-%m-%Y")));
				else
					recurtext += _("Due %d days after this task is completed effective %s").sprintf((recurr.everyn / 1440), startDate.print(_("%d-%m-%Y")));
			} else {
				recurtext += _("Due every %d days effective %s").sprintf((recurr.everyn / 1440), startDate.print(_("%d-%m-%Y")));
			}
			break;
		case 11:
			if (recurr.regen) {
				if((recurr.everyn / (1440 * 7)) == 1)
					recurtext += _("Due every week after this task is completed effective %s").sprintf(startDate.print(_("%d-%m-%Y")));
				else {
					recurtext += _("Due %s weeks after this task is completed effective %s").sprintf(recurr.everyn / (1440 * 7), startDate.print(_("%d-%m-%Y")));
				}
			} else {
				if (recurr.everyn > 1)
					recurtext += _("Due every %d weeks on").sprintf(recurr.everyn) +" ";
				else
					recurtext += _("Due every") +" ";

				var week_days = [];
				if (recurr.weekdays & 1) week_days.push(DAYS[0]);
				if (recurr.weekdays & 2) week_days.push(DAYS[1]);
				if (recurr.weekdays & 4) week_days.push(DAYS[2]);
				if (recurr.weekdays & 8) week_days.push(DAYS[3]);
				if (recurr.weekdays & 16) week_days.push(DAYS[4]);
				if (recurr.weekdays & 32) week_days.push(DAYS[5]);
				if (recurr.weekdays & 64) week_days.push(DAYS[6]);

				if (week_days.length > 1) {
					recurtext += (week_days.slice(0, week_days.length - 1)).join(", ");
					recurtext += " "+ _("and") +" "+ week_days[week_days.length - 1];
				} else {
					recurtext += week_days;
				}
			}
			break;
		case 12:
			if (!recurr.regen) {
				if (recurr.subtype == 2)
					recurtext += _("Due day %d every").sprintf(recurr.monthday) +" ";
				else
					recurtext += _("Due the %d %d of every").sprintf(getNDay(recurr.nday), getWeekDays(recurr.weekdays)) +" ";
			}

			if(recurr.everyn == 1)
				recurtext += _("month");
			else
				recurtext += _("%d months").sprintf(recurr.everyn);

			if (recurr.regen) recurtext += " "+ _("after this task is completed");

			recurtext =+ " "+ _("effective %s").sprintf(startDate.print(_("%d-%m-%Y")));
			break;
		case 13:
			if (!recurr.regen) {
				if (recurr.subtype == 2)
					recurtext += _("Due every %d %d").sprintf(((recurr.month == 0) ? MONTHS[0] : MONTHS[Math.floor(recurr.month / 44640) + 1]), recurr.monthday);
				else
					recurtext += _("Due the %d %d of %d").sprintf(getNDay(recurr.nday), getWeekDays(recurr.weekdays), ((recurr.month == 0) ? MONTHS[0] : MONTHS[Math.floor(recurr.month / 44640) + 1]));
			} else {
				if(recurr.everyn <= 12)
					recurtext += _("Due every year");
				else
					recurtext += _("Due %d years").sprintf(recurr.everyn/12);
			}
			recurtext =+ " "+ _("effective %s").sprintf(startDate.print(_("%d-%m-%Y")));
			break;
	}

	return recurtext;

	function getNDay(nday)
	{
		var recurtext = "";
		if (nday == 1) recurtext += _("first");
		else if (nday == 2) recurtext += _("second");
		else if (nday == 3) recurtext += _("third");
		else if (nday == 4) recurtext += _("fourth");
		else if (nday == 5) recurtext += _("last");

		return recurtext;
	}

	function getWeekDays(weekdays)
	{
		var recurtext = "";
		if (weekdays == 127) recurtext += _("day");
		else if (weekdays == 62) recurtext += _("weekday");
		else if (weekdays == 65) recurtext += _("weekend day");
		else if (weekdays == 1) recurtext += _("Sunday");
		else if (weekdays == 2) recurtext += _("Monday");
		else if (weekdays == 4) recurtext += _("Tuesday");
		else if (weekdays == 8) recurtext += _("Wednesday");
		else if (weekdays == 16) recurtext += _("Thursday");
		else if (weekdays == 32) recurtext += _("Friday");
		else if (weekdays == 64) recurtext += _("Saturday");

		return recurtext;
	}
}

/**
 * Funtion recursively scans dom to get text nodes which contain email addresses or URLs so we can 
 * replace them with an anchor tag.
 * @param HTMLElement node The parent node that will be examined to find the child text nodes
 */
function linkifyDOM(node){
	var emailPattern = /([a-z0-9_'+*$%\^&!\.\-]+\@(?:[a-z0-9\-]+\.)+[a-z]{2,})/gi;
	var linkPattern = /((ftp|http|https):\/\/(\w+:{0,1}\w*@)?(\S+)(:[0-9]+)?(\/|\/([\w#!:.?+=&%@!\-\/]))?)/gi;

	for(var i = 0; i < node.childNodes.length; i++) {
		var cnode = node.childNodes[i];
		if(cnode.nodeType == 1) { // Tag-node
			if(cnode.nodeName != 'A') {
				linkifyDOM(cnode);
			}
		} else if(cnode.nodeType == 3) { // Text-node
			if(cnode.nodeValue.trim().length > 0) {
				// check if this text node is email address
				if(cnode.nodeValue.search(emailPattern) != -1 || cnode.nodeValue.search(linkPattern) != -1) {
					linkifyDOMNode(cnode, node);
				}
			}
		}
	}
}

/**
 * Function will replace text nodes with element nodes which contains anchor tag.
 * @param HTMLElement node The node that has to be examined for links or emails
 * @parem HTMLElement parentNode The parent of the passed node
 */
function linkifyDOMNode(node, parentNode) {
	var linkPattern = /((ftp|http|https):\/\/(\w+:{0,1}\w*@)?(\S+)(:[0-9]+)?(\/|\/([\w#!:.?+=&%@!\-\/]))?)/gi;
	var emailPattern = /([a-z0-9_'+*$%\^&!\.\-]+\@(?:[a-z0-9\-]+\.)+[a-z]{2,})/gi;

	var str = node.nodeValue;

	// Split the strings up in pieces that are normal text and pieces that contain an URL
	// We do this before checking for email addresses as an ftp-URL with username/password within will otherwise be seen as an email address
	var lookupParts = splitStringByPattern(str, linkPattern);
	var parts = [];
	// Now loop through all the pieces split them up based on whether they contain an email address
	for(var i=0;i<lookupParts.length;i++){
		// Do not examine an piece that already contains a link
		if(lookupParts[i].search(linkPattern) == -1){
			// Spit the pieces up based on whether they contain a link
			var tmpParts = splitStringByPattern(lookupParts[i], emailPattern);
			parts.push.apply(parts, tmpParts);
		}else{
			parts.push(lookupParts[i]);
		}
	}

	// Create a container node to append all the textnodes and anchor nodes to
	var containerNode = dhtml.addElement(null, 'span');
	for(var i=0;i<parts.length;i++){
		// Create the node for a normal link
		if(parts[i].search(linkPattern) != -1){
			// Create a new anchor-node.
			var anchorNode = dhtml.addElement(containerNode, 'a');
			anchorNode.setAttribute('href', parts[i]);
			anchorNode.setAttribute('target', '_blank');

			// Add text node to the new anchor node.
			dhtml.addTextNode(anchorNode, parts[i]);
		// Create the node for an email link
		}else if(parts[i].search(emailPattern) != -1){
			// Create a new anchor-node.
			var anchorNode = dhtml.addElement(containerNode, 'a');
			anchorNode.setAttribute('href', 'mailto: ' + parts[i]);
			anchorNode.setAttribute('target', '_blank');
			anchorNode.setAttribute('onclick', 'parent.webclient.openWindow(this, "createmail", "index.php?load=dialog&task=createmail_standard&to=' + parts[i] + '"); return false;');

			// Add text node to the new anchor node.
			dhtml.addTextNode(anchorNode, parts[i]);
		// Just text, so lets make a text node
		}else{
			dhtml.addTextNode(containerNode, parts[i]);
		}
	}

	// Replace the original text node under the parent with the new anchor nodes and split up text nodes.
	for(var i=0, count=containerNode.childNodes.length;i<count;i++){
		// We remove the childNode from the parent by using this line so every loop we can add the first as the list shrinks
		parentNode.insertBefore(containerNode.childNodes.item(0), node);
	}
	// Remove the original node
	parentNode.removeChild(node);
}

/** 
 * Split a string in pieces based on whether each piece matches the passed 
 * pattern. It returns both the pieces that match and that do not match the 
 * pattern. 
 * @param String str The input string to be split up
 * @param RegExp pattern The regex pattern used to be split the string
 * @return Array The array of pieces
 */
function splitStringByPattern(str, pattern){
	var parts = [];
	var cutOffPoints = [0];
	var found;
	// Find the cutOffPoints in the str
	while((found = pattern.exec(str)) !== null){
		if(found.index!=0){
			cutOffPoints.push(found.index);
		}
		if(pattern.lastIndex < str.length){
			cutOffPoints.push(pattern.lastIndex);
		}
	}
	// Cut the string up into the pieces based on the cutOffPoints
	var parts = [];
	if(cutOffPoints.length > 1){
		for(var i=0;i<cutOffPoints.length;i++){
			var length;
			// Use the current and the next cutOffPoint to calculate the number of character we need to extract.
			if(cutOffPoints[i+1] != undefined){
				length = cutOffPoints[i+1]-cutOffPoints[i];
			// If there is no next cutOffPoint we have to calculate the number of characters using the length of the entire string
			}else{
				length = str.length - cutOffPoints[i];
			}
			parts.push(str.substr(cutOffPoints[i], length));
		}
	}else{
		parts = [str];
	}
	return parts;
}

/**
 * Function will return true if meeting is in past.
 *
 * @param Date endtime of the meeting.
 * @return boolean will return true if meeting is in past, false otherwise.
 */
function isMeetingInPast(endtime) {
	if(endtime)
		return endtime.getTime() < (new Date().getTime())

	return false;
}

/**
 * Function will return true if meeting is canceled by organizer.
 *
 * @param Object meetingItem item object of the meeting (should include meeting_status property).
 * @return boolean will return true if meeting is canceled, false otherwise.
 */
function isMeetingCanceled(meetingItem) {
	if(meetingItem && typeof meetingItem.meeting != 'undefined') {
		var meetingStatus = parseInt(meetingItem.meeting, 10);
		if (meetingStatus === olMeetingCanceled || meetingStatus === olMeetingReceivedAndCanceled)
			return true;
	}

	return false;
}

/**
 * Will check whether a string used in recipient input fields has a semicolon after the last 
 * recipient. It will return true if that is the case, false otherwise. If the string, apart from 
 * spaces, is empty it will also return true as the next recipient can be added directly. 
 * If the string is not empty it will check whether the last character is the semicolon. It will 
 * allow the semicolon to be followed by spaces, but no other characters are allowed. So it will 
 * accept "RECIPIENT ;  " and return true, but it will not accept "RECIPIENT ; a".
 * @param String str The recipient input string
 * @return Boolean Will return true if the string is closed otherwise false
 */
function isLastRecipientClosedWithSemicolon(str){
	/* We check the length of the trimmed string to see if the string is empty and we use the 
	* regular expression that checks for a semicolon followed by 0 to n spaces until the end of the 
	* string. If that semicolon can not be found it will return -1. So by checking whether it 
	* matches -1 we can see that the string is not closed with a semicolon.
	*/
	if(str.trim().length > 0 && str.search(/;[\s]*$/) == -1){
		return false;
	}else{
		return true;
	}
}