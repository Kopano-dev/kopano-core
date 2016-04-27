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
	return 'printitemmodule';
}

function getDialogTitle() {
	return _("Print preview");
}

function getIncludes(){
	return array(
			"client/modules/".getModuleName().".js",
			"client/modules/printmessageitem.js",
		);
}

function getJavaScript_onload(){ ?>
					var data = new Object();
					var elem = dhtml.getElementById("printing_frame");
					module.init(moduleID, elem);

					module.setData(<?=get("storeid","false","'", ID_REGEX)?>, <?=get("parententryid","false","'", ID_REGEX)?>);
					
					var attachNum = false;
					<? if(isset($_GET["attachNum"]) && is_array($_GET["attachNum"])) { ?>
						attachNum = new Array();
					
						<? foreach($_GET["attachNum"] as $attachNum) { 
							if(preg_match_all(NUMERIC_REGEX, $attachNum, $matches)) {
							?>
								attachNum.push(<?=intval($attachNum)?>);
						<?	}
					} ?>
					
					<? } ?>
					var entryid = "<?=get("entryid","", "", ID_REGEX)?>";
					if(entryid != ""){
						module.open(entryid, entryid, attachNum);
					}else{
						// if entryid is not there that means, the page is called to show unsaved data in printpreview.
						module.printFromUnsaved(parentwindow);
					}

					window.onresize();
<?php } // getJavaScript_onload						

function getJavaScript_onresize() {?>
	var top = dhtml.getElementById("dialog_content").offsetTop;
	var frame = dhtml.getElementById("printing_frame");

	var window_height = window.innerHeight;
	if(!window_height){ // fix for IE
		window_height = document.body.offsetHeight;
	}
	frame.height = (window_height - top - 10) + "px";
<?php } // onresize

function getBody(){ ?>
	<iframe id="printing_frame" name="printing_frame" width="100%" height="150" frameborder="0" scrolling="auto" style="border: 1px solid #000;"></iframe>
<?php } // getBody

function getMenuButtons(){
	return array(
			"close"=>array(
				'id'=>"close",
				'name'=>_("Close"),
				'title'=>_("Close preview"),
				'callback'=>'function(){window.close();}'
			),
			
			"print"=>array(
				'id'=>'print',
				'name'=>_("Print"),
				'title'=>_("Print"),
				'callback'=>'function(){printing_frame.focus();printing_frame.print();}'
			),
			
		);
}

?>
