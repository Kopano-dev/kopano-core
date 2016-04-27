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
	return _("Check Names"); // use gettext here
}

function getJavaScript_onload(){ ?>
					var keyword = "<?=get("keyword","", false, STRING_REGEX)?>";
					var type = "<?=get("type","", false, STRING_REGEX)?>";
					var module_id = "<?=get("module_id","", false, STRING_REGEX)?>";
					var resolveList = dhtml.getElementById("resolve_list");
					
					module = window.opener.webclient.getModule(module_id);
					
					var names = module.resolveQue[module.fieldCounter]["names"][module.keywordCounter]["items"];
					for(var i=0;i<names.length;i++){
						var fullname = names[i]["fullname"];
						var email = names[i]["emailaddress"];
						if(names[i].objecttype == MAPI_DISTLIST || names[i].message_class == "IPM.DistList"){
							fullname = "[" + fullname + "]";
						}else{
							fullname +=" <"+email+">";
						}
						var optionElement = dhtml.addElement(resolveList,"option","","",fullname);
						
						optionElement.value = i;
					}
<?php } // getJavaScript_onload						
		
function getJavaScript_onresize(){ ?>
	
<?php } // getJavaScript_onresize						

function getJavaScript_other(){ ?>
			function submitCheckNames(){
				var resolveList = dhtml.getElementById("resolve_list");
				if(!resolveList.value){
					resolveList.value = 0;
				}
				var item = module.resolveQue[module.fieldCounter]["names"][module.keywordCounter]["items"][resolveList.value];
				module.verifiedName(item["nametype"],item["nameid"],item);
			}
<?php } // getJavaScript_other
	
function getBody() { ?>
	<span><?= _("The WebAccess") ?> <?= _("found more than one") ?> 
		"<span id="search_key"><?=isset($_GET["keyword"])?htmlentities(get("keyword", false, false, STRING_REGEX)):''; ?></span>".
	</span>
	<br>
	<br>
	<span><?= _("Select the address to use") ?>:</span>
	<br>
	<select size="8" id="resolve_list">
	</select>
	<br>
	<br>
	<?=createConfirmButtons("submitCheckNames()")?>
<?php } // getBody
?>
