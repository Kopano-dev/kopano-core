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

function getModuleName(){
	return "";
}

function getModuleType(){
	return "item";
}

function getIncludes(){
	return array(
			"client/modules/".getModuleName().".js"
		);
}

function getDialogTitle(){
	return ""; // use gettext here
}


function getJavaScript_onload(){ ?>

<?php } // getJavaScript_onload						
		
function getJavaScript_onresize(){ ?>

<?php } // getJavaScript_onresize						

function getJavaScript_other(){ ?>

<?php } // getJavaScript_other
	
function getBody() { ?>

<?php } // getBody

function getMenuButtons(){
	return array(
			"save"=>array(
				'name'=>_("Save"),
				'title'=>_("Save"),
				'callback'=>'submit'
			)
		);
}

?>
