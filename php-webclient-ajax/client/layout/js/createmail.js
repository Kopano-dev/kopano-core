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

function mailOptionsCallBack(result)
{
    setImportance(result.importance);
    dhtml.getElementById("sensitivity").value = result.sensitivity;
	toggleReadReceipt(!result.readreceipt);
}

function autosave(){
	if(window.changedSinceLastAutoSave && !isMessageBodyEmpty()){
		submit_createmail(false);
		window.changedSinceLastAutoSave = false;
	}
}

function setCursorPosition(message_action) {
	var htmlFormat = dhtml.getElementById("use_html").value == "true" ? true : false;
	var htmlBody = dhtml.getElementById("html_body");
	var cursorPos = webclient.settings.get("createmail/cursor_position", "start");
	var currentPos;

	switch(message_action) {
		case "reply":
		case "replyall":
		case "forward":
			if(htmlFormat == true) {
				// FCKEditor is installed
				if(typeof(FCKeditorAPI) != "undefined" && (fckEditor = FCKeditorAPI.GetInstance("html_body"))) {
					if(cursorPos == "start") {
						if(message_action != "forward")
							fckEditor.Focus();
					} else {
						if(fckEditor.EditorWindow.document && module.signature != ""){
							// Create a element and place at the end of editor
							var pEle = dhtml.addElement(fckEditor.EditorWindow.document.body, 'p', false, false, null, fckEditor.EditorWindow);
							if(message_action != "forward")
							{
								// Select newly inserted element.
								fckEditor.Selection.SelectNode(pEle);
								fckEditor.Selection.Save();
							}
							// Update scrolling
							fckEditor.EditingArea.Document.documentElement.scrollTop = fckEditor.EditingArea.Document.body.lastChild.offsetTop;
							fckEditor.EditingArea.Document.documentElement.scrollLeft = 0;

							// Add signature at the end if cursor is positioned at the end.
							var signatureElement = dhtml.addElement(fckEditor.EditorWindow.document.body, 'div', false, false, null, fckEditor.EditorWindow);
							signatureElement.innerHTML = module.signature;

							// Now we can put focus on editor.
							if(message_action != "forward")
								fckEditor.Focus();
						}else{
							break;
						}
					}
				}
			} else {
				// FCKEditor is not installed
				if(cursorPos == "start") {
					dhtml.setSelectionRange(htmlBody, 0, 0);
				} else {
					if(htmlBody && typeof htmlBody == "object") {
						currentPos = htmlBody.value.length;
						// Add signature at the end if cursor is positioned at the end.
						htmlBody.value += module.signature;
						dhtml.setSelectionRange(htmlBody, currentPos, currentPos);
					}
				}
			}
			if(message_action == "forward") dhtml.getElementById("to").focus();
			break;
		default:
			dhtml.getElementById("to").focus();
	}
}
/**
 * Function which check if message body is empty.
 *@return boolean -return true if message body is empty, false if not empty.
 */
function isMessageBodyEmpty()
{
	// Check for HTML format
	if (typeof FCKeditorAPI != "undefined" && (fckEditor = FCKeditorAPI.GetInstance("html_body"))){
		var content = fckEditor.GetXHTML();
		if (content.length > 0) return false;
		else return true;
	} else {
		// Check for PLAIN format
		var body = dhtml.getElementById("html_body");
		if (body && body.value.trim().length > 0) return false;
		else return true;
	}
}
/**
 * Callback function for checknames in mail.
 * @param Object resolveObj obj of the resolved names
 */
function checkNamesCallBackCreateMail(resolveObj)
{
	checkNamesCallBack(resolveObj, true);

	//Send mail
	if(window.resolveForSendingMessage === true){
		submit_createmail(true);
	}
}

function eventCreateMailItemKeyCtrlSubmit(moduleObject, element, event)
{
	switch(event.keyCombination)
	{
		case this.keys["mail"]["save"]:
			submit_createmail();
			break;
		case this.keys["mail"]["send"]:
			submit_createmail(true);
			break;
	}
}

/**
 * Function to set/unset the read reciept for email sending option
 * @param String value previous value of readreceipt setting
 */
function toggleReadReceipt(value)
{
   var read_receipt = dhtml.getElementById("read_receipt_requested");
   var read_receipt_button = dhtml.getElementById('read_receipt_button');
   if(read_receipt) {
		var checked = false;
		if(value == "false" || value == false) {
			read_receipt.value = "true";
			dhtml.addClassName(read_receipt_button, "menubuttonselected");
			checked = true;
		}else{
			read_receipt.value = "false";
			dhtml.removeClassName(read_receipt_button, "menubuttonselected");
		}
		read_receipt.checked = checked;
	}
}
