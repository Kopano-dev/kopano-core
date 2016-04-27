<?php
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

?>
<?php

function getModuleName() { 
	return "createmailitemmodule"; 
}
function getModuleType() {
	return "item"; 
}
function getDialogTitle() {
	return _("Create E-Mail"); 
}

function getIncludes(){
	$includes = array(
			"client/modules/".getModuleName().".js",
			"client/widgets/elastictextarea.js",
			"client/widgets/suggestionlist.js",
			"client/modules/suggestemailaddressmodule.js",
			"client/layout/css/suggestionlayer.css",
			"client/layout/js/createmail.js"
	);
	
	if (FCKEDITOR_INSTALLED){
		$includes[] = FCKEDITOR_JS_PATH."/fckeditor.js";
	}
	
	return $includes;
}

function getJavaScript_onload(){ ?>

					window.waitForSaveResponse = false;
					var rootentryid = false;
					var attachNum = false;

					var data = new Object();
					data["storeid"] = <?=get("storeid", "false", "'", ID_REGEX)?>;
					data["parententryid"] = <?=get("parententryid","false", "'", ID_REGEX)?>;
					data["message_action"] = <?=get("message_action", "false", "'", STRING_REGEX)?>;
					data["message_action_entryid"] = <?=(isset($_GET["message_action"]) && isset($_GET["entryid"]))?get("entryid", "false", "'", ID_REGEX):"false"?>;
					data["entryid"] = <?=get("entryid", "false", "'", ID_REGEX)?>;

					// If attachNum is set then it is embedded message
					<? if(isset($_GET["attachNum"]) && is_array($_GET["attachNum"])) { ?>
						rootentryid = <?=get("rootentryid", "false", "'", ID_REGEX)?>;
						attachNum = new Array();
					
						<? foreach($_GET["attachNum"] as $attachNum) { 
							if(preg_match_all(NUMERIC_REGEX, $attachNum, $matches)) {
							?>
								attachNum.push(<?=intval($attachNum)?>);
						<?	}
						} ?>
					<? } ?>

					module.init(moduleID);
					module.setData(data);
					
					// If message_action is forwardasattachment then multiple items is been selected to be added as attachment to the new mail
					if(data["message_action"] == "forwardasattachment"){
						module.forwardMultipleItems(windowData);
					}else{
						module.open(<?=get("entryid", "false", "'", ID_REGEX)?>, rootentryid, attachNum);
					}

					// store references of email fields so we don't have to get it every time
					var emailAddressElements = {
						"to" : dhtml.getElementById("to"),
						"cc" : dhtml.getElementById("cc"),
						"bcc" : dhtml.getElementById("bcc"),
						"subject" : dhtml.getElementById("subject"),
						"from" : dhtml.getElementById("from")
					};

					// Load Suggest Email Address module for the TO, CC and BCC fields
					var suggestEmailModule = webclient.dispatcher.loadModule("suggestEmailAddressModule");
					if(suggestEmailModule != null) {
						var suggestEmailModuleID = webclient.addModule(suggestEmailModule);
						suggestEmailModule.init(suggestEmailModuleID);
						// Setup TO field
						suggestlistTO = new suggestionList("create_email_to_fld", emailAddressElements["to"], suggestEmailModule);
						suggestEmailModule.addSuggestionList(suggestlistTO);
						// Setup CC field
						suggestlistCC = new suggestionList("create_email_cc_fld", emailAddressElements["cc"], suggestEmailModule);
						suggestEmailModule.addSuggestionList(suggestlistCC);
						// Setup BCC field
						suggestlistBCC = new suggestionList("create_email_bcc_fld", emailAddressElements["bcc"], suggestEmailModule);
						suggestEmailModule.addSuggestionList(suggestlistBCC);
					}

					if (FCKEDITOR_INSTALLED && parentWebclient.settings.get("createmail/mailformat","html")=="html"){
<?php
							// check if user language is supported by FCKEditor

							if (isset($_SESSION["lang"])){
								$client_lang = $_SESSION["lang"];
							}else{
								$client_lang = LANG;
							}

							$client_lang = str_replace("_","-",strtolower(substr($client_lang,0,5)));

							if (!file_exists(FCKEDITOR_JS_PATH."/editor/lang/".$client_lang.".js")){
								$client_lang = substr($client_lang,0,2);
								if (!file_exists(FCKEDITOR_JS_PATH."/editor/lang/".$client_lang.".js")){
									$client_lang = "en"; // always fall back to English
								}
							}
?>
						initEditor(true, "<?=FCKEDITOR_JS_PATH?>", "<?=$client_lang?>", <?=FCKEDITOR_SPELLCHECKER_ENABLED?"true":"false"?>);
					}else{
						dhtml.getElementById("use_html").value = "false";
					}

					if(parentWebclient.settings.get("createmail/from", "false") != "false"){
						var settingsFromFieldValue = parentWebclient.settings.get("createmail/from", "false");
						if(settingsFromFieldValue.length > 0){
							var settingsFromFieldValues = settingsFromFieldValue.split(",");
						}else{
							var settingsFromFieldValues = new Array();
						}
						var fromSelect = emailAddressElements["from"];
						if(fromSelect){
							for(var i=0;i<settingsFromFieldValues.length;i++){
								fromSelect.options[fromSelect.options.length] = new Option(settingsFromFieldValues[i], settingsFromFieldValues[i]);
							}
						}
					}else{
						var fromRow = dhtml.getElementById("from_row", "tr");
						if(fromRow){
							fromRow.style.display = "none";
						}
					}

					if (parentWebclient.settings.get("createmail/always_readreceipt", "false")=="true"){
						dhtml.getElementById("read_receipt_requested").value = "true";
						dhtml.addClassName(dhtml.getElementById('read_receipt_button'), "menubuttonselected");
					}

					if(data["entryid"] === false) {
						// check is this a new message then only set signature in body, 
						// otherwise this will be handled by module
						module.setBody(false, false, false);
					}

					// Fetch all the attributes that are passed in the URL
					<?	if (isset($_GET["to"])){ // for security reasons we need to urlencode this value for javascript?>
							emailAddressElements["to"].value = decodeURIComponent("<?=rawurlencode(stripslashes(get("to")))?>");
					<?	} ?>

					<? if(isset($_GET["cc"])) { ?>
							emailAddressElements["cc"].value = decodeURIComponent("<?=rawurlencode(stripslashes(get("cc")))?>");
					<?  } ?>

					<? if(isset($_GET["bcc"])) { ?>
							emailAddressElements["bcc"].value = decodeURIComponent("<?=rawurlencode(stripslashes(get("bcc")))?>");
					<?  } ?>

					<? if(isset($_GET["subject"])) { ?>
							emailAddressElements["subject"].value = decodeURIComponent("<?=rawurlencode(get("subject"))?>");
					<?  } ?>

					if(dhtml.getElementById("use_html").value == "false") {
						<? if(isset($_GET["body"])) { ?>
								dhtml.getElementById("html_body").value = decodeURIComponent("<?=rawurlencode(get("body"))?>");
						<?  } ?>
						if (window.BROWSER_IE){
							dhtml.addEvent(-1, dhtml.getElementById("html_body"), "keydown", IE_tracCursorPosition);
							dhtml.addEvent(-1, dhtml.getElementById("html_body"), "click", IE_tracCursorPosition);
						}
					}

					if(emailAddressElements["from"]){
						dhtml.addEvent(-1, emailAddressElements["from"], "change", changeFromAddress);
					}

					setChangeHandlers(emailAddressElements["to"]);
					setChangeHandlers(emailAddressElements["cc"]);
					setChangeHandlers(emailAddressElements["bcc"]);
					setChangeHandlers(emailAddressElements["subject"]);
					setChangeHandlers(dhtml.getElementById("html_body"));
					setChangeHandlers(emailAddressElements["from"]);

					resizeBody();

					/**
					 * If setting is enabled to close opening readmail dialog when replying do so. 
					 * This also checks if the user is actually replying/forwarding and has not just
					 * clicked on the email address (hence the message_action check).
					 */
					if (parentWebclient.settings.get("createmail/close_on_reply", "no")=="yes" && opener.parentWebclient && data["message_action"]){
						opener.close();
					}

					emailAddressElements["to"].focus();
					
					// Specifically enable default action on textfields / textareas
					dhtml.addEvent(false, emailAddressElements["to"], "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(false, emailAddressElements["cc"], "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(false, emailAddressElements["bcc"], "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(false, emailAddressElements["from"], "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(false, emailAddressElements["subject"], "contextmenu", forceDefaultActionEvent);
					dhtml.addEvent(false, dhtml.getElementById("html_body"), "contextmenu", forceDefaultActionEvent);
					
					// add functionality of auto expanding textareas
					var elasticTextareaTo = new ElasticTextarea(emailAddressElements["to"], "createmail/toccbcc_maxrows", resizeBody);
					var elasticTextareaCc = new ElasticTextarea(emailAddressElements["cc"], "createmail/toccbcc_maxrows", resizeBody);
					var elasticTextareaBcc = new ElasticTextarea(emailAddressElements["bcc"], "createmail/toccbcc_maxrows", resizeBody);

					// disable spell checking of firefox
					emailAddressElements["to"].spellcheck = false;
					emailAddressElements["cc"].spellcheck = false;
					emailAddressElements["bcc"].spellcheck = false;

					// Set flag to take necessary decisions on attachments 
					module.messageAction = data["message_action"];

					// Activate automatic saving to drafts folder
					if(parentWebclient.settings.get("createmail/autosave", "false") != "false"){
						var autosaveIntervalMin = (parentWebclient.settings.get("createmail/autosave_interval", 3));
						setInterval(function(){
							autosave();
						}, 60 * autosaveIntervalMin * 1000);
					}

					// Notify the ZarafaDnD Firefox extension that this dialog accepts dragged files
					allowDnDFiles();

					// check if we need to send the request to convert the selected message as mail
					if(window.windowData && window.windowData["action"] == "convert_item") {
						module.sendConversionItemData(windowData);
					}

					// check whether we are draggging contact item to inbox folder
					if(window.windowData && window.windowData["action"] == "convert_contact") {
						dhtml.getElementById("to").value = window.windowData["emails"];
						dhtml.getElementById("subject").focus();
					}
					
					
					// Checks if creatmail dialog is called with an attachmentid in URL, this is the case when a 
					// document is send as attachment via any openOffice application
					var attachment_id = decodeURIComponent("<?=rawurlencode(get("attachment_id"))?>").trim();
					if(attachment_id){
						var newattachments = new Array();
					<?
						// check for the uploaded document in session variable to display it in the createmail dialog
						// Get Attachment data from state
						$attachment_state = new AttachmentState();
						$attachment_state->open();

						if(isset($_GET["attachment_id"])) {
							$files = $attachment_state->getAttachmentFiles($_GET["attachment_id"]);
							if($files) {
								foreach($files as $tmpname => $file) {
									?>
										var attachment = new Object();
										attachment["attach_num"] = "<?=$tmpname?>";
										attachment["name"] = "<?=$file["name"]?>";
										attachment["size"] = <?=$file["size"]?>;
										attachment["filetype"] = "<?=$file["type"]?>";
										newattachments.push(attachment);
								<? } ?>

								/**
								 * As every dialog has a unique dialog_attachmentid with the help of which is 
								 * refers its uploaded attachments, here to handle differnt types of attachments 
								 * like manually  attached items or from the url, in the same defined way;
								 * we,assign the unique attachid  value recived in the url to dialog_attachments
								 */ 
								dhtml.getElementById("dialog_attachments").value = attachment_id;
							<? } else {
								// if the createmail dialog is refresh the session files should not contain the uploaded attachments
								$attachment_state->clearAttachmentFiles($_GET["attachment_id"]);
							}
						}

						$attachment_state->close();
						?>
							module.setAttachmentData(newattachments);
							module.setAttachments();
					}
				
<?php } // getJavaScript_onload

function getJavaScript_other(){
?>		
			var FCKEDITOR_INSTALLED = <?=(FCKEDITOR_INSTALLED?"true":"false")?>;
			var suggestlist;

			window.messageChanged = false;

			window.onbeforeunload = function(){
				if (window.messageChanged){
					return "<?=_("You can lose changes when you continue.")?>";
				}
			}

			function setChangeHandlers(element)
			{
				if (element){
					dhtml.addEvent(-1, element, "change", setMessageChanged);
					dhtml.addEvent(-1, element, "keypress", setMessageChanged);
				}
			}

			function setMessageChanged()
			{
				window.document.title = _("Create E-Mail")+"*";
				window.messageChanged = true;
				window.changedSinceLastAutoSave = true;
			}

			function setFCKMessageChanged(editorInstance)
			{
				setMessageChanged();
				if (window.BROWSER_IE) IE_tracCursorInEditor();
				return false;
			}

			function FCKeditor_OnComplete( editorInstance )
			{
				// Make the editor instance available for the setContentInBodyArea() call.
				document.fckEditor = editorInstance;
				// When the XML data is already available we can set the body.
				if(document.fckEditorContent){
					setContentInBodyArea();
					// This timeout will get the DOM tree of FCKeditor,thus we can add a 'p' element to DOM
					window.setTimeout(function() {
						setCursorPosition(module.messageAction);
					},500);
				}

				editorInstance.Events.AttachEvent( 'OnSelectionChange', setFCKMessageChanged ) ;

				//set the default font-family for editorarea
				var font_family = parentWebclient.settings.get("createmail/maildefaultfont","Arial");
				document.fckEditor.EditorDocument.body.style.fontFamily = font_family;
				
				// set content of body if it is passed in URL
				<? if(isset($_GET["body"])) { ?>
					editorInstance.SetHTML(decodeURIComponent("<?=rawurlencode(htmlentities($_GET["body"]))?>"));
				<? } ?>
				if (module)	{
				
				   /**
					* This settimeout is used to get the usable DOM tree of FCKeditor so that we can 
					* retrieve the image tag from editor Document to built the inline image array which is 
					* send to user while replying the mail.
					*/
					window.setTimeout(function() {
						module.retrieveInlineImagesFromBody(editorInstance.EditorDocument);
					},500);
					
					// Set inline options after message is completely loaded into editor.
					module.setAttachmentbarAttachmentOptions();
				}

				/**
				 * Note: In IE, 'OnSelectionChange' event does not fire on every keystroke,
				 * but only on some random keystrokes.So add keydown and mousedown event to trac selection in editor.
				 *
				 * Also added few events for handling shortcuts in FCKeditor. DHTML version could be used by that doesnt
				 * work so adding events using attachEvent/addEventListener.
				 */
				var iframeWindow = dhtml.getElementById("html_body___Frame").contentWindow.document.getElementsByTagName('iframe')[0].contentWindow;
				if (window.BROWSER_IE) {
					editorInstance.EditorDocument.attachEvent("onkeydown", IE_tracCursorInEditor);
					editorInstance.EditorDocument.attachEvent("onmousedown", IE_tracCursorInEditor);

					// Save initial selection in editor.
					IE_tracCursorInEditor();

					// Events for shortcuts
					editorInstance.EditorDocument.attachEvent("onkeyup", handleFCKeditorEvent);
					editorInstance.EditorDocument.attachEvent("onkeydown", handleFCKeditorEvent);
				} else {
					// Events for shortcuts
					iframeWindow.addEventListener("keydown", handleFCKeditorEvent, true);
					iframeWindow.addEventListener("keyup", handleFCKeditorEvent, true);
				}
			}
			
			var abSelection = "to";
			function abCallBack(recips)
			{
				for(key in recips) {
					if (key!="multiple" && key!="value"){
						var fieldElement = dhtml.getElementById(key);
						fieldElement.value = recips[key].value;
						// fire on change event so textareas can be auto expanded
						dhtml.executeEvent(fieldElement, "change");
					}
				}

				var selectionElement = dhtml.getElementById(abSelection);
				selectionElement.select();
				selectionElement.focus();
			}
			
			function handleFCKeditorEvent(event)
			{
				/**
				 * As DHTML version is not working properly in attaching and catching events in FCKeditor,
				 * key events are registered using browser defaults, so we also need to handle global event object here.
				 */
				event = (event?event:(window.event?fixEvent(window.event):((this.windowObj&&this.windowObj.event)?fixEvent(this.windowObj.event):false)));

				// Set event target
				if (typeof event.target == "undefined"){
					event.target = event.srcElement;
				}

				switch(event.type)
				{
					case "keyup":
						eventInputManagerKeyControlKeyUp(webclient.inputmanager, event.target, event);
						break;
					case "keydown":
						eventInputManagerKeyControlKeyDown(webclient.inputmanager, event.target, event);
						break;
				}
			}
<?php 
}


function getBody(){ 
	?>
		<input id="entryid" type="hidden">
		<input id="parent_entryid" type="hidden">
		<input id="message_class" type="hidden" value="IPM.Note">
		<input id="importance" type="hidden" value="1">
		<input id="use_html" type="hidden" value="true">
		<input id="sensitivity" type="hidden" value="0">
		<input id="read_receipt_requested" type="hidden" value="false">
		<input id="sent_representing_name" type="hidden" value="">
		<input id="sent_representing_email_address" type="hidden" value="">
		<input id="sent_representing_addrtype" type="hidden" value="SMTP">

		<!-- properties for flags -->
		<input id="flag_status" type="hidden" value="">
		<input id="flag_icon" type="hidden" value="">
		<input id="reminder_time" type="hidden" value="">
		<input id="reminder_set" type="hidden" value="false">
		<input id="flag_request" type="hidden" value="">
		<input id="flag_due_by" type="hidden" value="">
		<input id="flag_complete_time" type="hidden" value="">
		<input id="reply_requested" type="hidden" value="">
		<input id="reply_time" type="hidden" value="">
		<input id="response_requested" type="hidden" value="">
		
		<div id="conflict"></div>

		<div class="properties">
			<table class="fixed_table_layout" width="100%" border="0" cellpadding="1" cellspacing="0">

				<tr id="from_row">
					<td class="propertynormal propertywidth" align="center"><?=_("From")?>:</td>
					<td>
						<select id="from" class="field from_select">
							<option></option>
						</select>
					</td>
				</tr>

				<?
					$recipients = array("to" => _("To"), "cc" => _("CC"), "bcc" => _("BCC"));

					$fields = "";					
					foreach($recipients as $key => $name) {
						$fields .= "fields[" . urlencode($key) . "]=" . urlencode($name) . "&";
					}
					
					foreach($recipients as $key => $recipient)
					{
				?>
					<tr>
						<td class="propertynormal propertywidth" align="center" valign="top">
							<input class="button" type="button" value="<?=$recipient?>..." onclick="abSelection='<?=$key?>'; webclient.openModalDialog(module, 'addressbook', DIALOG_URL+'task=addressbook_modal&storeid=' + module.storeid + '&<?=$fields?>dest=<?=$key?>', 800, 500, abCallBack);">
						</td>
						<td>
							<textarea id="<?=$key?>" class="field"></textarea>
						</td>
					</tr>
				<?
					}
				?>
				
				<tr>
					<td class="propertynormal propertywidth" align="center">
						<?=_("Subject")?>:
					</td>
					<td>
						<input id="subject" class="field" type="text">
					</td>
				</tr>
				<tr>
					<td class="propertynormal propertywidth" valign="top">
						<input class="button" type="button" value="<?=_("Attachments")?>:" onclick="webclient.openWindow(module, 'attachments', DIALOG_URL+'task=attachments_modal&store=' + module.storeid + '&entryid=' + (module.messageentryid?module.messageentryid:'') + '&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value, 570, 425, '0');">
					</td>
					<td valign="top">
						<div id="itemattachments">&nbsp;</div>
					</td>
				</tr>
			</table>
		</div>
		
		<textarea id="html_body" cols="60" rows="12"></textarea>
<?php } // getBody


function getMenuButtons(){
	return array(
			array(
				'id'=>"send",
				'name'=>_("Send"),
				'title'=>_("Send new Mail"),
				'callback'=>'function(){submit_createmail(true);}',
				'shortcut'=>"S"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"save",
				'name'=>"",
				'title'=>_("Save"),
				'callback'=>'function(){submit_createmail(false);}'
			),
			array(
				'id'=>"print",
				'name'=>"",
				'title'=>_("Print"),
				'callback'=>'function(){webclient.openModalDialog(this, "printing", DIALOG_URL+"task=printitem_modal", 600, 600)}'
			),
			array(
				'id'=>"attachment",
				'name'=>"",
				'title'=>_("Add Attachments"),
				'callback'=>"function(){setMessageChanged();webclient.openWindow(module, 'attachments', DIALOG_URL+'task=attachments_modal&store=' + module.storeid + '&entryid=' + (module.messageentryid?module.messageentryid:'') + '&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value, 570, 425, '0');}"
			),
			array(
				'id'=>"attach_item",
				'name'=>"",
				'title'=>_("Attach item"),
				'callback'=>"function(){webclient.openModalDialog(module, 'attachitem', DIALOG_URL+'task=attachitem_modal&storeid=' + module.storeid + '&entryid=' + module.parententryid +'&dialog_attachments=' + dhtml.getElementById('dialog_attachments').value, FIXEDSETTINGS.ATTACHITEM_DIALOG_WIDTH, FIXEDSETTINGS.ATTACHITEM_DIALOG_HEIGHT, false, false, {module : module});}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"addsignature",
				'name'=>"",
				'title'=>_("Add Signature"),
				'callback'=>'eventMenuAddSignature'
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"checknames",
				'name'=>"",
				'title'=>_("Check Names"),
				'callback'=>'function(){checkNames(checkNamesCallBackCreateMail);}'
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
			array(
				'id'=>"priority_high",
				'name'=>"",
				'title'=>_("Priority: High"),
				'callback'=>"function(){setMessageChanged();setImportance(dhtml.getElementById('importance').value!=2?2:1);}"
			),
			array(
				'id'=>"priority_low",
				'name'=>"",
				'title'=>_("Priority: Low"),
				'callback'=>"function(){setMessageChanged();setImportance(dhtml.getElementById('importance').value!=0?0:1);}"
			),
			array(
				'id'=>"seperator",
				'name'=>"",
				'title'=>"",
				'callback'=>""
			),
            array(
				'id'=>"read_receipt_button",
				'name'=>"",
				'title'=>_("Set/Unset Read Receipt"),
				'callback'=>"function(){setMessageChanged();toggleReadReceipt(dhtml.getElementById('read_receipt_requested').value);}"
            ),
            array(
               'id'=>"seperator",
               'name'=>"",
               'title'=>"",
               'callback'=>""
            ),
			array(
				'id'=>"options",
				'name'=>_("Options"),
				'title'=>_("Options"),
				'callback'=>"function(){setMessageChanged();webclient.openModalDialog(module, 'options', DIALOG_URL+'task=mailoptions_modal', 310, 220, mailOptionsCallBack);}"
			),
			array(
				'id'=>"flag_status_red",
				'name'=>"",
				'title'=>_("Flag"),
				'callback'=>"function(){webclient.openModalDialog(module, 'flag', DIALOG_URL+'task=flag_modal&store_values=true', 350, 210);}"
			)
		);
}  // getMenuButtons
?>
