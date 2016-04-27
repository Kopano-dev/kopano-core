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

function getJavaScript_other(){
?>
		var signatureTable;
		var data = false;
		var FCKEDITOR_INSTALLED = <?=(FCKEDITOR_INSTALLED?"true":"false")?>;		// flag to indicate FCKEdiotr is installed
		var USE_FCKEDITOR;			// flag to indicate we should actually use FCKEditor or not
		window.FCKeditorLoaded = false;

		function setChangeHandlers(element)
		{
			if (element){
				dhtml.addEvent(-1, element, "change", setSignatureChanged);
				dhtml.addEvent(-1, element, "keypress", setSignatureChanged);
			}
		}
		
		function setSignatureChanged()
		{
			window.document.title = _("Signatures")+"*";
			window.signatureChanged = true;
		}

		function setFCKMessageChanged(editorInstance)
		{
			setSignatureChanged();
			return false;
		}
		
		function afterSetFCKMessageHTML(editorInstance)
		{
		window.document.title = _("Signatures");
			window.signatureChanged = false;
			return false;
		}

		function FCKeditor_OnComplete( editorInstance )
		{
		    editorInstance.Events.AttachEvent( 'OnSelectionChange', setFCKMessageChanged );
		    editorInstance.Events.AttachEvent( 'OnAfterSetHTML', afterSetFCKMessageHTML );
		    window.FCKeditorLoaded = true;

			// select first signature, when fckeditor is available
			if(typeof data["signatures"] == "object") {
				for(var index in data["signatures"]) {
					signatureTable.selectRow(data["signatures"][index]["id"]);
					break;
				}
			}
		}
<?php
}

function getJavaScript_onload(){ ?>
			/**
			 * check that FCKEditor is installed and mailformat is set to plain then only we have to 
			 * use it or else we have to show plain text signatures
			 */
			USE_FCKEDITOR = (FCKEDITOR_INSTALLED && webclient.settings.get("createmail/mailformat", "html") == "html") ? true : false;

			initSignatures();

			dhtml.addEvent(false, dhtml.getElementById("newmsg_signature"), "change", eventChangeSelectionSignatureOptions)
			dhtml.addEvent(false, dhtml.getElementById("replyfwd_signature"), "change", eventChangeSelectionSignatureOptions)
			
			setChangeHandlers(dhtml.getElementById("signature_editor"));

			//Add html editor if it is installed, and mailformat setting is html
			if (USE_FCKEDITOR){
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
				initEditor(true, "<?=FCKEDITOR_JS_PATH?>", "<?=$client_lang?>", <?=FCKEDITOR_SPELLCHECKER_ENABLED?"true":"false"?>, "signature_editor", 274);
			}
<?php
}

function getDialogTitle(){
//TODO: find a way to get this changed by Javascript
	return _("Signatures");
}

function getIncludes() {
	$includes = array(
		"client/layout/js/signatures.js",
		"client/layout/css/signatures.css",
		"client/widgets/tablewidget.js"
	);
	
	if (USE_FCKEDITOR){
		$includes[] = FCKEDITOR_JS_PATH."/fckeditor.js";
	}
	
	return $includes;
}

function getBody() { ?>
	<div class="signatureslist">
		<div id="signatureslist_container" class="signatureslist_container"></div>
		<div class="signaturelist_controllers">
			<input type="button" onclick="removeSelectedSignature();" value="<?=_('Remove')?>" class="buttonsize"/>
			<input type="button" onclick="createSignature();" value="<?=_('New')?>" class="buttonsize"/>
			<input type="button" onclick="saveSelectedSignature();" value="<?=_('Save')?>" class="buttonsize"/>
			<input type="button" onclick="renameSelectedSignature();" value="<?=_('Rename')?>..." class="buttonsize"/>
		</div>
	</div>
	<div class="general_signature_options">
		<table>
			<tr>
				<td class="option_label"><?=_('Signature for new messages')?>:</td>
				<td class="option_value">
					<select id="newmsg_signature">
						<option value="0">&lt;<?=_('None')?>&gt;</option>
					</select>
				</td>
			</tr>
			<tr>
				<td class="option_label"><?=_('Signature for replies and forwards')?>:</td>
				<td class="option_value">
					<select id="replyfwd_signature">
						<option value="0">&lt;<?=_('None')?>&gt;</option>
					</select>
				</td>
			</tr>
		</table>
	</div>
	<div class="signature_editor_container">
		<textarea id="signature_editor" rows="15" cols="100">
		</textarea>
	</div>
	<?=createConfirmButtons("checkSignatureContentModified();saveSignaturesInSettings();window.close();")?>
<?php } // getBody
?>
