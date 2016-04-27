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

// Maintains Arrays for unuploaded files.
var fileUploadMazSizeErrorArray = new Array();
var fileUploadHTTPErrorArray = new Array();

// Maintain uploaded and total files count.
var totalFiles = 0;
var uploadedFiles = 0;

/**
 * Function will initialise global SWFUpload Object.
 */
function initUploadObject(enable)
{
	// Create SWFUpload object with required settings.
	if(enable) {
		// Create Settings array for SWFUpload Object.
		var uploadObjSettings = {
					upload_url: BASE_URL + "index.php?load=upload_attachment",
					flash_url : "client/widgets/swfupload/swfupload.swf",

					// Apply file size limit in Bytes
					file_size_limit : (dhtml.getElementById("max_file_size").value) + " B",
					file_post_name: 'attachments',
					file_upload_limit : FILE_UPLOAD_LIMIT,
					file_queue_limit: FILE_QUEUE_LIMIT,

					// Button info
					button_text : '<span class="attach_files">' + _("Attach files") + '</span>',
					button_text_style : ".attach_files { color: #475FC6; text-decoration:underline; font-weight: bold; font-size: 14pt; font-family: Arial;}",
					button_width : 150,
					button_height : 20,
					button_window_mode : SWFUpload.WINDOW_MODE.TRANSPARENT,
					button_cursor : SWFUpload.CURSOR.HAND,
					button_placeholder : document.getElementById('upload_button'),

					// Handlers
					swfupload_loaded_handler : swfupLoaded,
					file_queued_handler : fileIsQueued,
					file_queue_error_handler : fileQueuedErrorHandlerFunction,
					queue_complete_handler : queueCompletedHandlerFunction,
					file_dialog_complete_handler: fileDialogCompleteHandlerFunction,
					upload_complete_handler : uploadCompleteHandlerFunction,
					swfupload_pre_load_handler: swfuploadPreLoad,
					swfupload_load_failed_handler : swfupLoadedFailed,
					upload_error_handler : uploadErrorHandlerFunction

					/**
					 * Some Extra parameters which may need in future.
					 * button_image_url : 'client/widgets/swfupload/wdp_buttons_upload_114x29.png',
					 * 
					 * Allowed file types
					 * file_types : "*.*",
					 * Description in upload dialog in Files of type field.
					 * file_types_description : "Each files",
					 * 
					 * provides debug information in attachments dialog itself.
					 * debug: true
					 */
				};

		uploadObj = new SWFUpload(uploadObjSettings);
	} else {
		// SWF upload not enabled, which is the same as the initialization failing. Trigger
		// the event handler for a failed SWF initialization.
		swfupLoadedFailed();
	}
}

/**
 * Function will be called when SWFUpload object is completely loaded.
 * So function will add POST parameters in uploadObj object,
 * It will add some cookies and dialog_attachments
 */
function swfupLoaded()
{
	/**
	 * add dialog_attachments COOKIES in POST parameters 
	 * so that flash object will send these in request while uploading files.
	 * 
	 * Whenever flash object is created and sends request first time to the server,
	 * It creates a new session because flash object doesn't have WA's COOKIES,
	 * so, here flashObject will try interact with server with different(new) COOKIE
	 * so, we have to pass WA COOKIE to the server explicitly to start proper session
	 * otherwise WA will not recognize proper session and it will not authenticate user
	 * to upload a file.
	 */
	uploadObj.addPostParam("dialog_attachments", parentwindow.dhtml.getElementById("dialog_attachments").value);
	uploadObj.refreshCookies(true);
}

/**
 * Function will be called when swfuploadPreLoad event is fired.
 * The swfuploadPreLoad event is fired as soon as the minimum version of Flash Player is found.
 */
function swfuploadPreLoad()
{
	/**
	 * Flash is detected in user's browser,
	 * Show flash object for enabling multiple uploads.
	 */
	dhtml.getElementById("swfupload-control").style.visibility = "visible";
	dhtml.getElementById("swfupload-control").style.display = "block";
}

/**
 * Function will be called when swfuploadLoadFailed event is fired,
 * swfuploadLoadFailed event is only fired if the minimum version of Flash Player is not met.
 */
function swfupLoadedFailed()
{
	/**
	 * Flash is not detected in user's browser,
	 * show backward compatibility option for uploading a file.
	 */
	dhtml.getElementById("normal-upload-control").style.visibility = "visible";
	dhtml.getElementById("normal-upload-control").style.display = "block";
}

/**
 * When any file is not able to upload because of some HTTP error at that time this function is called.
 * 
 * ERRORS							ERROR CODES
 * HTTP_ERROR						: -200
 * MISSING_UPLOAD_URL				: -210
 * IO_ERROR							: -220
 * SECURITY_ERROR					: -230
 * UPLOAD_LIMIT_EXCEEDED			: -240
 * UPLOAD_FAILED					: -250
 * SPECIFIED_FILE_ID_NOT_FOUND		: -260
 * FILE_VALIDATION_FAILED			: -270
 * FILE_CANCELLED					: -280
 * 
 * @param Object file, file Object, contains file information.
 * @param Integer errorCode - upload error code
 * @param String errorMessage - upload error message
 */
function uploadErrorHandlerFunction(file, errorCode, message)
{
	if (file && file.name)
		fileUploadHTTPErrorArray.push(file.name);
}
/**
 * Function will handle queue.
 * 
 * @param Object file - An Object which is having info about next file in the queue(ready to upload).
 * It will start uploading file using uploadObject and file information.
 */
function fileIsQueued(file)
{
	// Update message
	showUploadInfo();
	uploadObj.startUpload(file.fileID);
}

/**
 * Function will be called when uploading of a file is completed in "upload_progress_message" div element.
 */
function uploadCompleteHandlerFunction(file) 
{
	uploadedFiles++;
	showUploadInfo();
}

/**
 * When any file is not able to upload at that time this error function is called
 * 
 * ERRORS						ERROR CODES
 * QUEUE_LIMIT_EXCEEDED			: -100
 * FILE_EXCEEDS_SIZE_LIMIT		: -110
 * ZERO_BYTE_FILE				: -120
 * INVALID_FILETYPE				: -130
 * 
 * @param Object file, file Object, contains file information.
 * @param Integer errorCode - upload error code
 * @param String errorMessage - upload error message
 */
function fileQueuedErrorHandlerFunction(file, errorCode, errorMessage) {
	switch (errorCode)
	{
		case -100: // More no of files than FILE_UPLOAD_LIMIT at a time.
			alert(_("You cannot upload more than %s files at once.").sprintf(FILE_UPLOAD_LIMIT));
		break;
		case -110: // More than max upload size files.
			if (file && file.name)
				fileUploadMazSizeErrorArray.push(file.name);
		break;
		case -120: // Zero Byte files.
			if (file && file.name)
				alert(_("The file named %s is 0 bytes, so will not be attached.").sprintf(file.name));
		break;
	}
}

/**
 * Function will refresh/update attachment dialog after all files are uploaded,
 * 
 * @param Object file, file Object, contains file information.
 */
function queueCompletedHandlerFunction(file)
{
	// All files are uploaded now refresh the dialog to update the list of files.
	if(fileUploadHTTPErrorArray.length > 0)
		alert(_("The file(s) below could not be uploaded due to an HTTP error.")+"\n" + fileUploadHTTPErrorArray.join(", "));
	fileUploadHTTPErrorArray = [];
	location.reload();
}

/**
 * Function will be called after file upload dialog closed.
 * Here we can check that how many files are not able to upload because of maximum file-size limit.
 * and can show user an alert message about not uploaded files.
 * 
 * @param Integer numFilesSelected Total number of files selected from file upload dialog.
 * @param Integer numFilesQueued Number of files have bee queued.
 * @param Integer numFilesInQueue Total number of files in queue.
 */
function fileDialogCompleteHandlerFunction(numFilesSelected, numFilesQueued, numFilesInQueue)
{
	totalFiles += numFilesQueued;
	showUploadInfo();
	if(fileUploadMazSizeErrorArray.length > 0)
		alert(_("The file(s) below exceed(s) the allowed file size limit and were therefore not uploaded.") + "\n" + fileUploadMazSizeErrorArray.join(", "));
	fileUploadMazSizeErrorArray = [];
}

/**
 * Function will show information about uploaded files.
 */
function showUploadInfo()
{
	var Msgstring = _("%s of %s files are uploaded.").sprintf(uploadedFiles, totalFiles);

	var progressMsgElem = dhtml.getElementById("upload_progress_message");
	dhtml.deleteAllChildren(progressMsgElem);
	dhtml.addTextNode(progressMsgElem, Msgstring);
}

/**
 * Function will be called after non swf file upload dialog closed.
 */
function onChangeAttachment()
{
	var attachment = dhtml.getElementById('attachment');
	var maxfiles = Math.min(FILE_UPLOAD_LIMIT, FILE_QUEUE_LIMIT);

	if(attachment && attachment.files && maxfiles != 0 && attachment.files.length > maxfiles) {
		alert(_("You cannot upload more than %s files at once.").sprintf(maxfiles));
		dhtml.getElementById('upload').reset()
		return false;
	} else
		return dhtml.getElementById('upload').submit();

}

