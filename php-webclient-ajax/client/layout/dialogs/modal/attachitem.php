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
function getDialogTitle(){
	return _("Attach Items");
}


function getIncludes(){
	return array(
			"client/widgets/tree.js",
			"client/layout/css/tree.css",
			"client/layout/css/attachitem.css",
			"client/widgets/tablewidget.js",
			"client/widgets/pagination.js",
			"client/layout/js/attachitem.js",
			"client/modules/hierarchymodule.js",
			"client/modules/hierarchyselectmodule.js",
			"client/modules/".getModuleName().".js"
		);
}

function getModuleName(){
	return "attachitemlistmodule";
}

function getModuleType(){
	return "list";
}

function getJavaScript_other(){ ?>

<?php } // getJavaScript_other

function getJavaScript_onload(){ ?>
					var data = new Object();
					data["storeid"] = "<?=get("storeid", "false", false, ID_REGEX)?>";
					window.dialog_attachment = "<?=rawurlencode(get("dialog_attachments"))?>";	
					
					module.init(moduleID, dhtml.getElementById("attach_item"), false ,data);
					module.setData(data);
					
					// create paging element
					attachItemCreatePagingElement(moduleID, dhtml.getElementById("attach_item"));

					resizeBody();
					
<?php } // getJavaScript_onload

function getBody() { ?>
	<div id="attach_item">
		<div id="attach_item_hierarchy">
			<div style="font-weight:bold;"><?=_("Look in")?>:</div>
			<div id="attach_item_targetfolder"></div>
			<div id="action_buttons">
				<div>
					<input class="buttonsize" type="button" value="<?=_("OK")?>" onclick="addAttachmentItems(module,
				 '<?=rawurlencode(get("dialog_attachments"))?>');" />
				</div>
				<div>
					<input class="buttonsize" type="button" value="<?=_("Cancel")?>" onclick="window.close();"/>
				</div>
				<fieldset id="select">
					<legend><?=_("Insert As")?></legend>
					<div>
						<input type="radio" id="text" name="group1">
						<label for="text"><?=_("Text Only")?></label><br>
						<input type="radio" id="attachment" name="group1" checked > 
						<label for="attachment"><?=_("Attachment")?></label>
					</div>
				</fieldset>
			</div>
		</div>
		<div class="clear"></div>
		<div id="attach_item_listview" >
			<div class="select_label"><?=_("Select from")?>:</div>
			<div id="attach_item_paging"></div>
			<div class="clear"></div>
			<div id="attach_item_tablewidget" class="table_layout"></div>
		</div>
	</div>
<?php
} // getBody
?>
