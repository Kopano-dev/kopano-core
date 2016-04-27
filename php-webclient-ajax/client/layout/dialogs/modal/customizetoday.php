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
function getModuleType(){
	return "item";
}

function getDialogTitle(){
	return _("Customize WebAccess Today");
}

function getIncludes(){
	return array(
			"client/layout/js/customizetoday.js"
		);
}

function getJavaScript_onload(){ ?>
	
	var todayModuleObject = window.windowData;
	init(todayModuleObject);

<?php } // getJavaSctipt_onload						
			
function getBody() { 

?>
		<div class="customize">
				
			<fieldset class="customizetoday_fieldset">
				<legend><?=_("Calendar")?></legend>
				
				<table border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td class="">
							<?=_("Show this number of days in my calendar")?> : &nbsp;
						</td>
						<td>
							<select id="select_days" class="combobox" onchange="onChangeDays();">
								<option value="1">1</option>
								<option value="2">2</option>
								<option value="3">3</option>
								<option value="4">4</option>
								<option value="5">5</option>
								<option value="6">6</option>
								<option value="7">7</option>
							</select>
						</td>
					</tr>
				</table>
			</fieldset>
	
			<fieldset class="customizetoday_fieldset">
				<legend><?=_("Tasks")?></legend>
				
				<table border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td class="">
							<?=_("Show this task by default")?> : &nbsp;
						</td>
						<td>
							<select id="select_tasks" class="combobox" onchange="onChangeTasks();">
							</select>
						</td>
					</tr>
				</table>
			</fieldset>
			
			<fieldset class="customizetoday_fieldset">
				<legend><?=_("Styles")?></legend>
				
				<table border="0" cellpadding="1" cellspacing="0">
					<tr>
						<td class="">
							<?=_("Show WebAccess Today in this style")?> : &nbsp;
						</td>
						<td>
							<select id="select_styles" class="combobox" onchange="onChangeStyles();">
								<option value="1">Standard</option>
								<option value="2">Standard(two column)</option>
								<option value="3">Standard(one column)</option>
							</select>
						</td>
					</tr>
				</table>
			</fieldset>
			
			<br/>
			<br/>
						
			<?=createConfirmButtons("customizeTodaySubmit();window.close();")   //see addressBookSubmit() for submition ?>  
		</div>

<?php
} // getBody 
?>
