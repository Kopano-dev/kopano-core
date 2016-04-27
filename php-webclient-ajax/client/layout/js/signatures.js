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
 * initSignatures
 * 
 * initializing the dialog by retrieving the data and setting up the layout.
 */
function initSignatures(){
	// Set global Data Object, this will contain all up-to-date data.
	data = new Object();

	// Get signature data from settings and put them into the global Data Object.
	var signatures = getSignaturesFromSettings();
	data["signatures"] = signatures;
	data["select_sig_newmsg"] = webclient.settings.get("createmail/signatures/select_sig_newmsg","");
	data["select_sig_replyfwd"] = webclient.settings.get("createmail/signatures/select_sig_replyfwd","");

	// Setup signature list using a TableWidget.
	signatureTable = new TableWidget(dhtml.getElementById("signatureslist_container"), false);
	signatureTable.addColumn("signature_name", _("Name"), false, 1);
	signatureTable.addRowListener(selectSignature, "select");
	signatureTable.addRowListener(deselectSignature, "deselect");

	// Check to see if the old signature needs to be converted.
	checkLegacySignature();

	// Fill signature lists with data from the global Data Object.
	populateSignatureList(data["signatures"]);
}

/**
 * getSignaturesFromSettings
 * 
 * Retrieve the settings from the the Settings Object and sort them before 
 * returning.
 * @return object Lookup table of signature data.
 */
function getSignaturesFromSettings(){
	signatures = new Array();
	sigLookupTbl = new Object();

	// Get signature IDs 
	var id_lookup = String(webclient.settings.get("createmail/signatures/id_lookup",""));
	if(id_lookup != ""){
		var signatureIDs = id_lookup.split(";");

		// Get the signature data
		for(var i=0;i<signatureIDs.length;i++){
			signatures[ i ] = new Object();
			signatures[ i ]["id"] = signatureIDs[i];
			signatures[ i ]["name"] = webclient.settings.get("createmail/signatures/"+signatureIDs[i]+"/name","");
			signatures[ i ]["content"] = webclient.settings.get("createmail/signatures/"+signatureIDs[i]+"/content","");
			signatures[ i ]["type"] = webclient.settings.get("createmail/signatures/"+signatureIDs[i]+"/type","");
		}

		// Sort the signatures
		signatures.sort(function(a, b){
			if(a["name"] > b["name"])		return  1;
			else if(a["name"] < b["name"])	return -1;
			else							return  0;
		});

		// Transform array to lookup table
		for(var i=0;i<signatures.length;i++){
			sigLookupTbl[ signatures[i]["id"] ] = new Object();
			sigLookupTbl[ signatures[i]["id"] ]["id"]      = signatures[i]["id"];
			sigLookupTbl[ signatures[i]["id"] ]["name"]    = signatures[i]["name"];
			sigLookupTbl[ signatures[i]["id"] ]["content"] = signatures[i]["content"];
			sigLookupTbl[ signatures[i]["id"] ]["type"]    = signatures[i]["type"];  // Set type of signature
		}
	}
	return sigLookupTbl;
}

/**
 * populateSignatureList
 * 
 * Filling the lists and tables that contain the signature data with the latest 
 * data from the global Data Object. The old data is removed and then the new 
 * data is added.
 * Note that this function needs to be called in the scope of this window and in
 * the scope of another window as the function getSignatureFromEditor() is 
 * looking in the DOM tree.
 */
function populateSignatureList(signatures){
	// Setting up the data for the tableWidget
	var tableData = new Array();
	for(var i in signatures){
		tableData.push({
			rowID: signatures[i]["id"],	// Defined as signatureID so we can preserve selection after reloading lists
			data: signatures[i],
			signature_name: {innerHTML: signatures[i]["name"]}
		});
	}

	// Get content from editor to preserve the typed content
	var tblRowID = signatureTable.getSelectedRowID(0);
	var content = "";
	if(tblRowID){
		content = getSignatureFromEditor();
	}

	// Let's generate!
	signatureTable.generateTable(tableData);

	// Reselect old row and put the content in the editor again
	if(tblRowID){
		// We can use the old rowID because we define the tableWidget rowID as 
		// our signatureID
		signatureTable.clearSelection();
		signatureTable.selectRow(tblRowID);
		startEditSignature(content, tblRowID);
	} else if(tableData.length > 0 && !USE_FCKEDITOR){
		// Select first signature if there is no signature selected yet
		signatureTable.selectRow(tableData[0].rowID);
	}

	// Now lets fill the option lists
	// First empty the lists...
	var sig_option_newmsg = dhtml.getElementById("newmsg_signature", "select");
	if(sig_option_newmsg){
		while(sig_option_newmsg.options.length > 1){ // clear select box
			sig_option_newmsg.remove(1);
		}
	}
	var sig_option_replyfwd = dhtml.getElementById("replyfwd_signature", "select");
	if(sig_option_replyfwd){
		while(sig_option_replyfwd.options.length > 1){ // clear select box
			sig_option_replyfwd.remove(1);
		}
	}
	// ...and then the fill the lists with the new values
	for(var i in signatures){
		if(sig_option_newmsg){
			var nxtIndex = sig_option_newmsg.options.length;
			sig_option_newmsg.options[nxtIndex] = new Option(signatures[i]["name"], signatures[i]["id"]);
			// Check if this signature is selected in the global Data Object.
			if(data["select_sig_newmsg"] == signatures[i]["id"]){
				sig_option_newmsg.selectedIndex = nxtIndex;
			}
		}
		if(sig_option_replyfwd){
			var nxtIndex = sig_option_replyfwd.options.length;
			sig_option_replyfwd.options[nxtIndex] = new Option(signatures[i]["name"], signatures[i]["id"]);
			if(data["select_sig_replyfwd"] == signatures[i]["id"]){
			// Check if this signature is selected in the global Data Object.
				sig_option_replyfwd.selectedIndex = nxtIndex;
			}
		}
	}
}

/**
 * startEditSignature
 * 
 * Puts the content in the signature editor. This function will also check if a 
 * RTE editor is used or just a plain textarea.
 * @param content string contents of the current signature.
 * @param shadow_sigID number ID of the signature that is selected in the signature table.
 */
function startEditSignature(content, shadow_sigID){
	// Check if HTML editor is loaded and set the body
	if (checkEditor() && (fckeditor = FCKeditorAPI.GetInstance("signature_editor"))){
		// If plain text signature, convert to html text.
		if (data["signatures"][shadow_sigID] && data["signatures"][shadow_sigID]["type"] == "plain") {
			content = convertPlainToHtml(content);
		}

		fckeditor.SetHTML(content.replace(/\n/g, "<br />")); 
	} else {
		if (data["signatures"][shadow_sigID] && data["signatures"][shadow_sigID]["type"] == "plain") {
			// plain text so no need to convert
			dhtml.getElementById("signature_editor", "textarea").value = content;
		} else {
			// convert to plain text
			dhtml.getElementById("signature_editor", "textarea").value = convertHtmlToPlain(content);
		}
	}
	dhtml.getElementById("signature_editor", "textarea").shadow_sigID = ((shadow_sigID)?shadow_sigID:false);
}

/**
 * getSignatureFromEditor
 * 
 * Get the content of the signature from the editor. This function will also 
 * check if a RTE editor is used or just a plain textarea.
 */
function getSignatureFromEditor(){
	// Check if HTML editor is loaded and get the body
	if (checkEditor() && (fckeditor = FCKeditorAPI.GetInstance("signature_editor"))){

		// Check the mode of fckeditor before saving the content and if editor is in plain editor mode replace < and > with html entities.
		if(fckeditor.EditorDocument && fckeditor.EditMode != FCK_EDITMODE_WYSIWYG){
			return fckeditor.GetHTML(); 
		}else{
			return fckeditor.GetHTML();
		}
	} else {
		var editor = dhtml.getElementById("signature_editor", "textarea");
		if(editor){
			return editor.value;
		}else{
			return "";
		}
	}
}

/**
 * getShadowSignatureIDFromEditor
 * 
 * Get the shadow signature ID from the editor. 
 * 
 */
function getShadowSignatureIDFromEditor(){
	var editor = dhtml.getElementById("signature_editor", "textarea");
	if(editor){
		return editor.shadow_sigID;
	}else{
		return false;
	}
}


/**
 * saveSignaturesInSettings
 * 
 * Saving the settings in the global Data Object using the Settings Object.
 */
function saveSignaturesInSettings(){
	// First those signatures that are not in the list anymore.
	var old_sigs = getSignaturesFromSettings();
	for(var i in old_sigs){
		// Check if old signature is not still in the current list.
		if(!data["signatures"][ old_sigs[i]["id"] ]){
			webclient.settings.deleteSetting("createmail/signatures/"+old_sigs[i]["id"]+"/name");
			webclient.settings.deleteSetting("createmail/signatures/"+old_sigs[i]["id"]+"/content");
			webclient.settings.deleteSetting("createmail/signatures/"+old_sigs[i]["id"]+"/type");
			webclient.settings.deleteSetting("createmail/signatures/"+old_sigs[i]["id"]+"");
		}
	}

	var signatureIDlist = new Array();
	for(var i in data["signatures"]){
		// Retrieve the signature IDs to combine them into the id_lookup string
		signatureIDlist.push(data["signatures"][i]["id"]);

		// Setting the name and content in the settings
		webclient.settings.set("createmail/signatures/"+data["signatures"][i]["id"]+"/name", data["signatures"][i]["name"]);
		webclient.settings.set("createmail/signatures/"+data["signatures"][i]["id"]+"/content", data["signatures"][i]["content"]);
		webclient.settings.set("createmail/signatures/"+data["signatures"][i]["id"]+"/type", data["signatures"][i]["type"]);
	}
	// Finally adding the id_lookup string to the settings
	webclient.settings.set("createmail/signatures/id_lookup", signatureIDlist.join(";"));

	// Let's not forget to add the selections for the newmsg and replyfwd signature as well.
	webclient.settings.set("createmail/signatures/select_sig_newmsg", data["select_sig_newmsg"]);
	webclient.settings.set("createmail/signatures/select_sig_replyfwd", data["select_sig_replyfwd"]);
	
	window.signatureChanged = false;
}

/**
 * checkSignatureContentModified
 * 
 * Check if the content of the signature has been modified and if present the 
 * option to the user to save the signature.
 */
function checkSignatureContentModified(){
	// Get ID from signature table and content from the editor.
	var id = getShadowSignatureIDFromEditor();

	// Check if the signature exists.
	if(data["signatures"][ id ]){
		// Check if the signature content has been changed and offer option 
		// to save the content.
		if(window.signatureChanged){
			if(window.confirm(_("The signature has been changed. Do you want to save the changes?"))){
				saveSignatureContentModification(id);
			}
		}
	}
}


/**
 * selectSignature
 * 
 * Called when user selects a row in the signature table.
 * @param tblWidget object reference to tablewidget
 * @param type string type of action (select)
 * @param selected array list of rowIDs of selected rows
 * @param select list of rowIDs of newly selected rows
 */
function selectSignature(tblWidget, type, selected, select){
	// Prevent selecting of any signatures till FCKeditor is loaded.
	if (checkEditor() && !window.FCKeditorLoaded) {
		window.setTimeout(function (){
			selectSignature(tblWidget, type, selected, select);
		}, 1000);
		return false;
	}

	// Prevent loss of unsaved data in modified signature.
	checkSignatureContentModified();

	// Retrieve data from row in signature table
	var rowData = tblWidget.getDataByRowID(select[0]);
	if(rowData){
		// Use ID to get the data from the global Data Object
		var signature = data["signatures"][ rowData["data"]["id"] ];
		if(signature){
			// Put the content of the signature in the editor.
			startEditSignature(signature["content"], rowData["data"]["id"]);
		}
	}
}

/**
 * deselectSignature
 * 
 * Called when user deselects a row in the signature table.
 * @param tblWidget object reference to tablewidget
 * @param type string  type of action (deselect)
 * @param selected array list of rowIDs of selected rows
 * @param deselect array list of deselected rowIDs 
 */
function deselectSignature(tblWidget, type, selected, deselect){
	// Prevent deselecting of any signatures till FCKeditor is loaded.
	if (checkEditor() && !window.FCKeditorLoaded){
		window.setTimeout(function (){
			deselectSignature(tblWidget, type, selected, deselect);
		}, 1000);
		return false;
	}

	// Prevent loss of unsaved data in modified signature.
	checkSignatureContentModified();

	// Empty editor
	startEditSignature("");
}

/**
 * removeSelectedSignature
 * 
 * After removing the entry from the global Data Object it repopulates the 
 * signature lists and empties the editor.
 */
function removeSelectedSignature(){
	// Retrieve data from row in signature table.
	var tblRowID = signatureTable.getSelectedRowID(0);
	var rowData = signatureTable.getDataByRowID(tblRowID);
	if(rowData){
		// Use ID to check if entry exists in the global Data Object.
		if(data["signatures"][ rowData["data"]["id"] ]){
			delete data["signatures"][ rowData["data"]["id"] ];
			// Empty the editor.
			startEditSignature("");
			// Reload the lists
			populateSignatureList(data["signatures"]);
		}
	}
}

/**
 * createSignature
 * 
 * Using the advprompt dialog to get the new name of the signature and then 
 * adding it in the callback function.
 */
function createSignature(){
	webclient.openModalDialog(-1, 'signature_createsig', DIALOG_URL+'task=advprompt_modal', 300,150, createSignature_namesig_callback, {
		winObj: window
		}, {
		windowname: _("Enter the signature name"),
		fields: [{
			name: "signame",
			label: _("Enter the signature name"),
			type: "normal",
			required: true,
			value: ""
		}
		]
	});
}
/**
 * createSignature_namesig_callback
 * 
 * Callback function of createSignature. Creating an uniqueID and adding the 
 * data to the global Data Object. Finally repopulate all the signature lists.
 * @param result object Contains the new signature name in the signame-property.
 * @param callbackData Contains the reference window as it is used to call the 
 *                     populateSignatureList() function within the scope of this
 *                     window as this is a callback function that is called 
 *                     within the scope of another window.
 * @return number New ID of the signature.
 */
function createSignature_namesig_callback(result, callbackData){
	if(result && result.signame){
		// Generate an unique ID that is not yet used in the global Data Object.
		var uniqueID = Math.round(((new Date()).getTime())/1000);
		while(data["signatures"][ uniqueID ]){
			uniqueID += "a";	// Appending non-unique ID to make it unique.
		}

		// Adding data to global Data Object.
		data["signatures"][ uniqueID ] = new Object();
		data["signatures"][ uniqueID ]["id"] = uniqueID;
		data["signatures"][ uniqueID ]["name"] = result.signame;
		data["signatures"][ uniqueID ]["content"] = "";
		data["signatures"][ uniqueID ]["type"] = (checkEditor())?"html":"plain";
		
		// Refresh the all the signature lists.
		// We use the callbackData.winObj to force the populateSignatureList to 
		// be called in the scope of this window instead of the scope of the 
		// advprompt window.
		populateSignatureList.call(callbackData.winObj, data["signatures"]);

		return uniqueID;
	}
}

/**
 * saveSelectedSignature
 * 
 * Get the selected signature from the signature table and save the modification.
 */
function saveSelectedSignature(){
	var selected = signatureTable.getSelectedRowData();
	if(selected[0]){
		saveSignatureContentModification(selected[0]["data"]["id"]);
		window.document.title = _("Signatures");
	}
}

/**
 * saveSignatureContentModification
 * 
 * Save the signature content in the editor.
 * @param signatureID number ID of the signature the content should be stored for.
 */
function saveSignatureContentModification(signatureID){
	// Check if the signature exists.
	if(data["signatures"][ signatureID ]){
		var content = getSignatureFromEditor();
		
		if (checkEditor()) {
			// Save content in global Data Object.
			data["signatures"][ signatureID ]["content"] = cleanHTML(content); // for copy&paste from WORD, apply cleaning of HTML
			data["signatures"][ signatureID ]["type"] = "html";
			saveSignaturesInSettings();
		} else {
			// Prompt user that modifing html signature, will remove text formatting.
			if (data["signatures"][ signatureID ]["type"] && data["signatures"][ signatureID ]["type"] == "html") {
				if (confirm(_("Your signature is edited in plain text mode. When saving you will lose the original text formatting of your HTML signature. \n\nDo you want to proceed?"))) {
					// Save content in global Data Object.
					data["signatures"][ signatureID ]["content"] = cleanHTML(content); // for copy&paste from WORD, apply cleaning of HTML
					data["signatures"][ signatureID ]["type"] = "plain";
					saveSignaturesInSettings();
				}
			} else {
				// Save content in global Data Object.
				data["signatures"][ signatureID ]["content"] = cleanHTML(content); 
				data["signatures"][ signatureID ]["type"] = "plain";
				saveSignaturesInSettings();
			}
		}
	}
}

/**
 * renameSelectedSignature
 * 
 * Using the advprompt to get the user to supply a new name for the signature. 
 * The callbackfunction actually changes the name.
 */
function renameSelectedSignature(){
	var selected = signatureTable.getSelectedRowData();
	if(selected[0]){
		webclient.openModalDialog(-1, 'signature_renamesig', DIALOG_URL+'task=advprompt_modal', 300,150, renameSelectedSignature_namesig_callback, 
			{
				renameSigID: selected[0]["data"]["id"]  // Data available in callback function
			}, {
			windowname: _("Enter new signature name"),
			fields: [{
				name: "signame",
				label: _("Enter the signature name"),
				type: "normal",
				required: true,
				value: ""
			}
			]
		});
	}
}
/**
 * renameSelectedSignature_namesig_callback
 * 
 * Callback function of renameSelectedSignature. Changing the data in the global
 * Data Object and repopulating the signature lists. 
 */
function renameSelectedSignature_namesig_callback(result, callbackData){
	if(result && result.signame && callbackData.renameSigID){
		// Only change the name in the global Data Object.
		if(data["signatures"][ callbackData.renameSigID ]){
			data["signatures"][ callbackData.renameSigID ]["name"] = result.signame;
		}

		// Refresh the all the signature lists.
		populateSignatureList(data["signatures"]);
	}
}

/**
 * eventChangeSelectionSignatureOptions
 * 
 * Fired when user changes the selected signature. 
 * Stores the selection in the global Data Object.
 */
function eventChangeSelectionSignatureOptions(moduleObject, element, event){
	var sig_option_newmsg = dhtml.getElementById("newmsg_signature", "select");
	data["select_sig_newmsg"] = sig_option_newmsg.options[sig_option_newmsg.selectedIndex].value;
	var sig_option_replyfwd = dhtml.getElementById("replyfwd_signature", "select");
	data["select_sig_replyfwd"] = sig_option_replyfwd.options[sig_option_replyfwd.selectedIndex].value;
}

/**
 * checkLegacySignature
 * 
 * Check if the old signature needs to be converted to the new signature, do 
 * that and delete the old setting.
 */
function checkLegacySignature(){
	// Get old signature value
	oldsig_content = webclient.settings.get("createmail/signature/text", false);
	if(oldsig_content && oldsig_content != ""){
		// Use create signature callback function to add new signature.
		// Pass the result object with the new signame and a callbackdata object
		// with reference to the window object.
		var sigID = createSignature_namesig_callback({signame: _("Default Signature")}, {winObj: window});
		data["signatures"][ sigID ]["content"] = oldsig_content;
		// Set default signature to be added when this setting is set in the legacy version.
		if(webclient.settings.get("createmail/signature/use","false")=="true"){
			data["select_sig_newmsg"] = sigID;
			data["select_sig_replyfwd"] = sigID;
		}
		// Set old signature to empty
		webclient.settings.deleteSetting("createmail/signature/text");
		// Save it!
		saveSignaturesInSettings();
	}
}

/**
 * checkEditor
 * 
 * Check if editor is loaded. and also check we should be using fckediotr or not
 * @return boolean returns true when editor is loaded, otherwise returns false.
 */
function checkEditor(){
	// Check if HTML editor API is defined.
	if(typeof FCKeditorAPI != "undefined" && USE_FCKEDITOR){
		return true;
	}else{
		return false;
	}
}