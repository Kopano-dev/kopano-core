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

function getDialogTitle() {
	return _("Attachments");
}

// Include SWFUpload library.
function getIncludes(){
	$includes = array(
			"client/layout/js/attachments.js");
			
	$swfupload = array(
			"client/widgets/swfupload/swfupload.js",
			"client/widgets/swfupload/swfupload.queue.js",
			"client/widgets/swfupload/swfupload.cookies.js",
			"client/widgets/swfupload/swfupload.swfobject.js"
		);
		
	if(ENABLE_MULTI_UPLOAD) {
		return array_merge($includes,$swfupload);
	} else {
		return $includes;
	}
}

function getJavaScript_onload(){ ?>
					<?
						if($_SERVER["REQUEST_METHOD"] == "POST" && isset($_SERVER["CONTENT_LENGTH"]) && ((int) $_SERVER["CONTENT_LENGTH"] > getMaxUploadSize(false))) {
							/** 
							 * @TODO to exactly check which limit has exceeded (POST_MAX_SIZE or UPLOAD_MAX_FILESIZE)
							 * we can additionally check $_POST is empty array or not, if it is empty then POST_MAX_SIZE
							 * has exceeded otherwise UPLOAD_MAX_FILESIZE is exceeded
							 * but user normally don't care about which limit is exceeded so not implementing it
							 */
							// show error to user about incomplete upload of attachment
							?> alert(_("Your attachment size exceeds maximum attachment size of %s.").sprintf("<?=getMaxUploadSize(true);?>")); <?
						}
					?>

					if(parentwindow.module) {
						var filelist = dhtml.getElementById("filelist");

						var deletedattachments = new Object();
						
						<?
							// Get Attachment data from state
							$attachment_state = new AttachmentState();
							$attachment_state->open();

							$deleted = $attachment_state->getDeletedAttachments($_REQUEST["dialog_attachments"]);
							if($deleted) {
								foreach($deleted as $attach_num)
								{
									?>
										deletedattachments["<?=$attach_num?>"] = "deleted";
									<?
								}
							}
						?>
						
						var attachments = parentwindow.module.attachments;
						for(var i = 0; i < attachments.length; i++){
							var attachment = attachments[i];
							
							if(typeof(deletedattachments[attachment["attach_num"]]) == "undefined" && !attachment["hidden"] && (typeof parentwindow.module.inlineimages[attachment["attach_num"]] == "undefined")) {
								var option = dhtml.addElement(filelist, "option");
								option.value = attachment["attach_num"];
								option.text = attachment["name"];
								option.className = "attachmentsourcetype_default";
							}
						}

						var newattachments = new Array();
						<?
							$files = $attachment_state->getAttachmentFiles($_REQUEST["dialog_attachments"]);
							if($files) {
								foreach($files as $tmpname => $file)
								{
									?>
										var tmpname = "<?=rawurlencode($tmpname)?>";
										var messageclass = "<?=windows1252_to_utf8($file['message_class'])?>";
										// Show attachment if it is not an inline attachment.
										if (typeof(parentwindow.module.inlineimages[tmpname]) == "undefined"){
											var option = dhtml.addElement(filelist, "option", 
											"attachmentsourcetype_<?=($file['sourcetype'])?$file['sourcetype']:'default'?>");
											option.value = "<?=rawurlencode($tmpname)?>";
											option.text = "<?=windows1252_to_utf8($file["name"])?>";
											
											// check the message type of the attached item to display proper icons
											if(messageclass){
												dhtml.addClassName(option,iconIndexToClassName(false, messageclass, false));
											}

											var attachment = new Object();
											attachment["attach_num"] = "<?=rawurlencode($tmpname)?>"
											attachment["name"] = "<?=windows1252_to_utf8($file["name"])?>";
											attachment["size"] = <?=$file["size"]?>;
											attachment["filetype"] = "<?=$file["type"]?>";
											
											if(parentwindow.module.messageAction == "forwardmultiple"){
												attachment["entryid"] = "<?=bin2hex($file["entryid"])?>";
												attachment["attach_method"] = "5";
											}
											newattachments.push(attachment);
										}
									<?
								}
							}
						?>
						
						/** 
						 * We cannot set the list of attachments directly into the property of the 
						 * module in the parentwindow. If we would do this we would not set a copy, 
						 * but rather set a reference to the lists in this dialog. After closing 
						 * this window the references will point to non-existing objects. Therefor 
						 * we will use the setAttachmentData function to de-reference (read: clone)
						 * the objects first before setting them in the module. This has to be done 
						 * in the module itself.
						 */
						parentwindow.module.setAttachmentData(newattachments, deletedattachments);
						parentwindow.module.setAttachments();
						// Set inline options after attachment is loaded.
						parentwindow.module.setAttachmentbarAttachmentOptions();
					}

					var enableMultiUpload = <?=ENABLE_MULTI_UPLOAD ? 'true' : 'false'?>;
					
					// Non-IE browsers do not use the flash uploader since it has various issues
					if(!window.BROWSER_IE)
						enableMultiUpload = false;
					initUploadObject(enableMultiUpload);

					webclient.menu.showMenu();

					<?
						$attachment_state->close();
					?>
<?php } // getJavaScript_onload						

function getJavaScript_other(){ ?>
		var uploadObj;
		var FILE_UPLOAD_LIMIT = <?= defined('FILE_UPLOAD_LIMIT') ? FILE_UPLOAD_LIMIT : 50 ?>;
		var FILE_QUEUE_LIMIT = <?= GetMaxFileUploads() ?>;
		function checkAttachmentList(){
			// Check whether there are any files uploaded or, in upload queue.
			if(dhtml.getElementById('filelist').options.length == 0 && totalFiles == 0)
				alert(_("You have not uploaded any attachments"));
		}

		window.onbeforeunload = function(){
			// If all files are not uploaded using upload multiple attachments.
			if (totalFiles > uploadedFiles){
				// Update attachments list in attachment-bar of parentwindow.
				parentwindow.module.getAttachments();
				return "<?=_("Not all attachments have been uploaded. If you continue some attachments may be omitted.")?>";
			}
		}
		/**
		 * Function which download attachment(s)
		 * @param boolean downloadAllFlag Flag if true represents to download all messages else download only one.
		 */
		function downloadAttachments(downloadAllFlag){
			var item_type = new Array();
			var downloadAllAttachmentsUri = 'index.php?load=download_attachment';
			downloadAllAttachmentsUri += '&store=<?=htmlentities(get("store", false, false, ID_REGEX))?>';
			downloadAllAttachmentsUri += '&entryid=<?=htmlentities(get("entryid", false, false, ID_REGEX))?>';
			downloadAllAttachmentsUri += '&openType=attachment';
			downloadAllAttachmentsUri += '&dialog_attachments=<?=htmlentities($_REQUEST["dialog_attachments"])?>';
			if(downloadAllFlag){
				// keep the count of file added as attachments
				var fileAttachmentCount = 0;
				
				downloadAllAttachmentsUri += '&downloadType=downloadAll';
				// add the subject part
				var subject = parentwindow.module.itemProps.subject || (window.opener.dhtml.getElementById("subject").value || '');
				downloadAllAttachmentsUri += '&mailSubject=' + subject.replace(/\s/g, '_').substring(0, 20);

				// add all attachNum to url
				var downAttachElemOpt = dhtml.getElementById('filelist').options;
				for(var j = 0; j < downAttachElemOpt.length; j++){
					var item_type = downAttachElemOpt[j].className.split(/_|\s/g);
					if(item_type[1] == "default"){// checks the type to attachment ie. file or message item
						fileAttachmentCount++;
						downloadAllAttachmentsUri += '&attachNum[]=' + downAttachElemOpt[j].value;
					}
				}
				// if all attached items are message item then we cannot download them
				if(fileAttachmentCount == 0){
					alert(_("Embedded messages cannot be downloaded as zip archive") + ".");
					return;
				}
			}else{
				var fileList = dhtml.getElementById('filelist');
				var item_type = fileList.options[fileList.selectedIndex].className.split(/_|\s/g);
				if(fileList.selectedIndex >= 0){
					// check the type of selected item, if selected items is message type then show alert message.
					if(item_type[1] == "items"){
						alert(_("Selected item cannot be downloaded") + ".");
						return;
					}else{
						downloadAllAttachmentsUri += '&attachNum[]=' + fileList.options[fileList.selectedIndex].value;
					}
				}else{
					alert(_("Please select a file to download") + ".");
					return;
				}
			}
			location.href = downloadAllAttachmentsUri;
		}
<? } // javascript other

function getBody(){ ?>
		<div id="attachments">
			<form id="upload" action="<?=$_SERVER["REQUEST_URI"]?>" method="POST" enctype="multipart/form-data">
<?php
	$htmloutput = '';
	$GLOBALS['PluginManager']->triggerHook("server.dialog.attachments.setup.getbody.uploadformhtml", array( "html" => &$htmloutput ));
	echo $htmloutput;
?>
				<fieldset>
					<legend><?=_("Add attachments")?></legend>

					<div id="swfupload-control" class="swfupload-control">
						<input type="button" id="upload_button" />
						<div class="upload_size_message"> (<?= _("max filesize") ?>: <?= getMaxUploadSize(true) ?>) </div>
						<div id="upload_progress_message">&nbsp;</div>
					</div>
					<div id="normal-upload-control">
						<dl>
							<dt><label for="attachment"><?=_("Filename")?>:</label></dt>
							<dd><input type="file" multiple id="attachment" name="attachments[]" onchange="onChangeAttachment();" /></dd>
							<dd class="filesize_block"> (<?= _("max filesize") ?>: <?= getMaxUploadSize(true) ?>)</dd>
						</dl>
					</div>


					<input type="hidden" id="max_file_size" name="MAX_FILE_SIZE" value="<?= getMaxUploadSize(false) ?>" />
					<input type="hidden" name="dialog_attachments" value="<?=$_REQUEST["dialog_attachments"]?>" />
				</fieldset>
			</form>

			<fieldset id="files">
				<legend><?=_("Files currently attached")?></legend>
				<form id="action" action="<?=$_SERVER["REQUEST_URI"]?>" method="POST">
					<div>
						<div class="filelist">
							<select id="filelist" size="10" onchange="attachmentSelect();"></select>
						</div>
						<div class="attach_buttons">
								<input id="delete" class="button_delete" type="button" value="<?=_("Delete")?>" title="<?=_("Delete selected file")?>" onclick="dhtml.getElementById('action').submit();" /><br/>
								<input id="save" class="button_save" type="button" value="<?=_("Download")?>" title="<?=_("Download selected file")?>" onclick="downloadAttachments(false)" />
								<input id="saveall" class="button_save" type="button" value="<?=_("Download All")?>" title="<?=_("Download all attachments as a zip archive")?>" onclick="downloadAttachments(true)" />
								<input type="hidden" name="dialog_attachments" value="<?=$_REQUEST["dialog_attachments"]?>" />
								<input id="deleteattachment" type="hidden" name="deleteattachment" value="" />
								<input id="type" type="hidden" name="type" value="" />
						</div>
					</div>
				</form>
			</fieldset>
			
			<?=createCloseButton("window.close();")?>
		</div>
<?php } // getBody
?>
