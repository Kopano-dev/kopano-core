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
function preferences_settings_title(){
	return _("Preferences");
}

function preferences_settings_order(){
	return 0;
}

function preferences_settings_html(){ ?>
	<fieldset>
		<legend><?=_("General")?></legend>
		<table class="options">
			<tr>
				<th><label for="preferences_language"><?=_("Language")?></label></th>
				<td>
				<select id="preferences_language">
<?php 	
	function langsort($a, $b) { return strcasecmp($a, $b); } 	
	$langs = $GLOBALS["language"]->getLanguages();
	uasort($langs, 'langsort');
		foreach($langs as $lang=>$title){ 
?>				<option value="<?=$lang?>"><?=$title?></option>
<?php	}	?>
				
				</select>
				</td>
			</tr>
			
			<tr>
				<th><label for="preferences_theme"><?=_("Color theme")?></label></th>
				<td>
				<select id="preferences_theme">
<?php
	foreach(preferences_get_themes() as $theme){
		// The theme name needs to be converted because otherwise the page will not load.
?>
					<option value="<?=$theme['id']?>"><?=u2w($theme['name'])?></option>
<?php	} ?>
				</select>
				</td>
			</tr>

			<tr>
				<th><label for="preferences_autologout"><?=_("Automatic logout")?></label></th>
				<td>
				<select id="preferences_autologout">
					<option value="0"><?=_("Never")?></option>
					<option value="900000"><?=sprintf(_("After %s minutes"),15)?></option>
					<option value="1800000"><?=sprintf(_("After %s minutes"),30)?></option>
					<option value="2700000"><?=sprintf(_("After %s minutes"),45)?></option>
					<option value="3600000"><?=sprintf(_("After %s minutes"),60)?></option>
					<option value="5400000"><?=sprintf(_("After %s minutes"),90)?></option>
					<option value="7200000"><?=sprintf(_("After %s minutes"),120)?></option>
				</select>
				</td>
			</tr>
		</table>
	</fieldset>

	<fieldset>
		<legend><?=_("Folder Options")?></legend>
		<table class="options">
			<tr>
				<th><label for="preferences_startupfolder"><?=_("Startup folder")?></label></th>
				<td>
					<select id="preferences_startupfolder">
						<option value="inbox"><?=_("Inbox")?></option>
						<option value="today"><?=_("Today view")?></option>
						<option value="last"><?=_("Last opened folder")?></option>
					</select>
				</td>
			</tr>

			<tr>
				<th><label for="preferences_rowcount"><?=_("Number of items to display per page")?></label></th>
				<td>
					<select id="preferences_rowcount">
						<option value="10">10</option>
						<option value="15">15</option>
						<option value="20">20</option>
						<option value="25">25</option>
						<option value="30">30</option>
						<option value="35">35</option>
						<option value="40">40</option>
						<option value="45">45</option>
						<option value="50">50</option>
						<option value="75">75</option>
						<option value="100">100</option>
					</select>
				</td>
			</tr>
			
			<tr>
				<th><label for="preferences_previewpane"><?=_("Reading pane visible")?></label></th>
				<td>
					<select id="preferences_previewpane">
						<option value="right"><?=_("Right")?></option>
						<option value="bottom"><?=_("Bottom")?></option>
						<option value="off"><?=_("Off")?></option>
					</select>
				</td>
			</tr>
		</table>
	</fieldset>
	<fieldset>
		<legend><?=_("Incoming Mail")?></legend>
		<table class="options">
			<tr>
				<th><label for="preferences_mailcheck"><?=_("Check new mail interval")?></label></th>
				<td>
				<select id="preferences_mailcheck">
					<option value="15000">15 <?=_("seconds")?></option>
					<option value="30000">30 <?=_("seconds")?></option>
					<option value="60000">1 <?=_("minute")?></option>
					<option value="150000">2,5 <?=_("minutes")?></option>
					<option value="300000">5 <?=_("minutes")?></option>
					<option value="default"><?=_("Server default")?></option>
				</select>
				</td>
			</tr>

			<tr>
				<th colspan="2"><label><?=_("How to respond to requests for read receipts")?></label></th>
			</tr>
			<tr>
				<th colspan="2"><input id="preferences_readreceipt_always" type="radio" name="preferences_readreceipt" class="checkbox"><label for="preferences_readreceipt_always"><?=_("Always send a response")?></label></th>
			</tr>
			<tr>
				<th colspan="2"><input id="preferences_readreceipt_never" type="radio" name="preferences_readreceipt" class="checkbox"><label for="preferences_readreceipt_never"><?=_("Never send a response")?></label></th>
			</tr>
			<tr>
				<th colspan="2"><input id="preferences_readreceipt_ask" type="radio" name="preferences_readreceipt" class="checkbox"><label for="preferences_readreceipt_ask"><?=_("Ask me before sending a response")?></label></th>
			</tr>
			<tr>
				<th>
					<label for="preferences_mail_readflagtimer"><?=_("Mark mails as read after (in seconds)")?></label>
				</th>
				<td>
					<input id="preferences_mail_readflagtimer" type="text" class="text">
				</td>
			</tr>
		</table>
	</fieldset>
	<fieldset>
		<legend><?=_("Delegates")?></legend>
		<table class="options">
			<tr>
				<th colspan="2">
					<input type="button" onclick="webclient.openModalDialog(-1, 'delegates', DIALOG_URL+'&task=delegates_modal', 485, 374);" value="<?=_('Edit delegates')?>..." class="inline_button"/>
				</th>
			</tr>
		</table>
	</fieldset>
	<fieldset>
		<legend><?=_("Advanced Find")?></legend>
		<table class="options">
			<tr>
				<th>
					<label for="preferences_refresh_searchresults"><?=_("Results refresh every (X) seconds [0: disabled]")?></label>
				</th>
				<td>
					<input id="preferences_refresh_searchresults" type="text" class="text">
				</td>
			</tr>
		</table>
	</fieldset>
<?php } 

function preferences_get_themes(){
	$themes = array(
		array(
			'id'=>'default',
			'name'=>_("classic")
		)
	);
	$dir = "client/layout/themes";
	if (is_dir($dir)){
		$dh = opendir($dir);
		while(($entry = readdir($dh))!==false){
			if (is_dir($dir."/".$entry) && is_file($dir."/".$entry."/theme.css")){
				$themes[] = Array(
					'id' => $entry,
					'name' => $entry
				);
			}
		}
	}
	return $themes;
}


?>
