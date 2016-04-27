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

	header("Expires: ".gmdate( "D, d M Y H:i:s")." GMT");
	header("Last-Modified: ".gmdate( "D, d M Y H:i:s")." GMT");
	header("Cache-Control: no-cache, must-revalidate");
	header("Pragma: no-cache");

	header("Content-type: text/html; charset=utf-8");

	include("client/loader.php");
?><!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<html>
	<head>
		<title><?=defined("DEBUG_SERVER_ADDRESS")?DEBUG_SERVER_ADDRESS." - ":""?>Zarafa Webaccess</title>
		<!--[if IE]>
		<script type="text/javascript">
			window.BROWSER_IE=true;
			try { document.execCommand("BackgroundImageCache", false, true); } catch(err) {}
		</script>
		<![endif]-->
<?php
		includeFiles("css", array(
								"client/layout/css/style.css",
								"client/layout/css/icons.css",
								"client/layout/css/tree.css",
								"client/layout/css/calendar.css",
								"client/layout/css/date-picker.css",
								"client/layout/css/today.css"
							)
		);
		if(defined('MUC_AVAILABLE') && MUC_AVAILABLE == true){
			includeFiles("css", array("client/layout/css/multiusercalendar.css"));
		}
?>
		<!--[if IE]>
		<link rel="stylesheet" type="text/css" href="client/layout/css/style.ie.css">
		<![endif]-->
<?php
	// Apply an iPad specific stylesheet when needed
	if(strpos($_SERVER['HTTP_USER_AGENT'], 'iPad') !== false){
?>
		<link rel="stylesheet" type="text/css" href="client/layout/css/style.ipad.css">
<?php
	}
?>
		<link rel="icon" href="client/layout/img/favicon.ico"  type="image/x-icon">
		<link rel="shortcut icon" href="client/layout/img/favicon.ico" type="image/x-icon">	
<?php
		$pluginCSSFiles = $GLOBALS['PluginManager']->getClientFiles('css', Array('all','main'));
		if(count($pluginCSSFiles) > 0){
			includeFiles('css', $pluginCSSFiles);
		}

		$pluginCustomCssFiles = Array();
		$GLOBALS['PluginManager']->triggerHook('server.main.include.cssfiles', Array('files'=> & $pluginCustomCssFiles));
		if(is_array($pluginCustomCssFiles)){
			foreach($pluginCustomCssFiles as $filename){
				echo "\t\t<link rel=\"stylesheet\" type=\"text/css\" href=\"$filename\">\n";
			}
		}
?>
		
<?php include("client/layout/themes.php"); ?>

		<script type="text/javascript" src="index.php?version=<?=phpversion("mapi")?>&load=translations.js&lang=<?=$GLOBALS["language"]->getSelected()?>"></script>
<?php
		includeFiles("js", array(
							"client/core/constants.js", 
							"client/core/fixedsettings.js", 
							"client/core/utils.js", 
							"client/widgets/widget.js", 
							"client/views/view.js", 
							"client/views/table.view.js",
							"client/modules/module.js", 
							"client/modules/listmodule.js", 
							"client/modules/todaylistmodule.js", 
							"client/modules/itemmodule.js",
							"client/layout/js/date-picker.js",							
							"client/layout/js/date-picker-language.js",
    						"client/layout/js/date-picker-setup.js"
						)
		);

		includeJavaScriptFilesFromDir("core", array("constants.js", "utils.js"));
		includeJavaScriptFilesFromDir("widgets", array("widget.js", "tabswidget.js", "searchcriteria.js"));
		includeJavaScriptFilesFromDir("views", array("table.view.js", "view.js", "print.view.js", 
													 "print.calendar.dayview.js", "print.calendar.weekview.js", 
													 "print.calendar.monthview.js", "print.calendar.listview.js"));
		$modules = includeJavaScriptFilesFromDir("modules", array("module.js", "listmodule.js", "itemmodule.js",
																  "printlistmodule.js", "advancedfindlistmodule.js", "todaylistmodule.js"));

		$pluginJsFiles = $GLOBALS['PluginManager']->getClientFiles('js', Array('all','main'));
		if(count($pluginJsFiles) > 0){
			includeFiles('js', $pluginJsFiles);
		}
		$pluginModuleFiles = $GLOBALS['PluginManager']->getClientFiles('module', Array('all','main'));
		if(count($pluginModuleFiles) > 0){
			includeFiles('js', $pluginModuleFiles);
		}
		$pluginCustomJsFiles = Array();
		$GLOBALS['PluginManager']->triggerHook('server.main.include.jsfiles', Array('files'=> & $pluginCustomJsFiles));
		if(is_array($pluginCustomJsFiles)){
			foreach($pluginCustomJsFiles as $filename){
?>
		<script type="text/javascript" src="<?=$filename?>"></script>
<?php
			}
		}
?>
		<script type="text/javascript">
			// Setting the window name prevents an issue that the main window gets the same name
			// as an opened modal dialog that reloads the main window.
			window.name = 'Webaccess';

			if(navigator.userAgent.toLowerCase().indexOf('chrome') > -1){
				window.BROWSER_GOOGLECHROME = true;
			}else if(navigator.userAgent.toLowerCase().indexOf('apple') > -1){
					window.BROWSER_SAFARI = true;
			}else if(document.documentMode == 8){
				window.BROWSER_IE8 = true;
			}else{
				ua = navigator.userAgent.toLowerCase();
				window.BROWSER_OPERA = /opera/.test(ua);
				window.BROWSER_IE = !window.BROWSER_OPERA & /msie/.test(ua);
				window.BROWSER_IE7 = window.BROWSER_IE && /msie 7/.test(ua);
				window.BROWSER_IE8 = window.BROWSER_IE && /msie 8/.test(ua);
				// The check on IE8 is not that useful, but is just there to show the way the check works.
				window.BROWSER_IE6 = window.BROWSER_IE && !window.BROWSER_IE7 && !window.BROWSER_IE8;
			}
			var CLIENT_TIMEOUT = <?=CLIENT_TIMEOUT?>;
			var DIALOG_URL = "<?=DIALOG_URL?>";
			var MUC_AVAILABLE =<?=(defined('MUC_AVAILABLE')&&MUC_AVAILABLE==true)?'true':'false'?>;
			var SSO_LOGIN =<?=(isset($_SERVER['REMOTE_USER']))?'true':'false'?>;
			var NAMEDPROPS = {
				'PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords' : "<?=convertStringToHexNamedPropId('PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords')?>",
				'PT_STRING8:PS_PUBLIC_STRINGS:Keywords' : "<?=convertStringToHexNamedPropId('PT_STRING8:PS_PUBLIC_STRINGS:Keywords')?>"
			};

			<?php if(checkTrialVersion()){ ?>
			var ZARAFA_TRIAL_EXPIRE_PERIOD = <?=getDaysLeftOnTrialPeriod();?>; 
			<?php } ?>

			var webclient;
			var dhtml;
			var dragdrop;
			
			window.onload = function()
			{
				dhtml = new DHTML();
				webclient = new WebClient();
				dragdrop = new DragDrop();
				
<?=$GLOBALS["settings"]->getJavaScript("\t\t\t\t")?>
				
				var modules = new Array();
<?php
				foreach($modules as $module){
					echo "\t\t\t\tmodules.push(\"".$module."\");\n";
				}
				foreach($pluginModuleFiles as $module){
					echo "\t\t\t\tmodules.push(\"".basename($module)."\");\n";
				}
?>
				var plugindata = new Array();
<?php
				$clientPluginData = $GLOBALS['PluginManager']->getClientPluginManagerData();
				foreach($clientPluginData as $singlePluginData){
					echo "\t\t\t\tplugindata.push({pluginname:\"".$singlePluginData['pluginname']."\"});\n";
				}
?>
				// IE has some issues with executing events that are not on his "allowed" list.
				if(!window.BROWSER_IE){
					// Firing the initialize drag message functionality event to notify the ZarafaDnd extension
					dhtml.executeEvent(document.body, "ZarafaDnD:initDragMsgsToDesktop");
				}

				webclient.setUserInfo("<?=addslashes(windows1252_to_utf8($GLOBALS["mapisession"]->getUserName()))?>", "<?=addslashes(windows1252_to_utf8($GLOBALS["mapisession"]->getFullName()))?>", "<?=bin2hex($GLOBALS["mapisession"]->getUserEntryid())?>" , "<?=addslashes(windows1252_to_utf8($GLOBALS["mapisession"]->getEmail()))?>");

				// Store current sessionid in sessionid variable
				webclient.sessionid = "<?=session_id()?>";
				// Add a timestamp to the modulePrefix "webclient" to prevent conflicting moduleIDs with multiple tabs
				webclient.init("<?=BASE_URL?>", "webclient"+(new Date()).getTime(), modules, settings, plugindata);
				webclient.startClient();
			}
		</script>
	</head>
	<body scroll="no" onselectstart="return false;">
			
		<div id="top">
			<div id="topbar">
				<div class="title">
					<div class="zarafa_logo"></div>
					<div class="zarafa_background"></div>
				</div>
			</div>
			<div id="menubar">
				<div id="menubar_left"></div>
				<div id="menubar_right"></div>
				<div id="zarafa_loader"></div>
			</div>
		</div>
		<div id="left"></div>
		<div id="leftmain_resizebar" class="icon icon_resizebar_horizontal horizontal_resizebar" resizebar="1">&nbsp;</div>
		<div id="main"></div>
		<div id="mainright_resizebar" class="icon icon_resizebar_horizontal horizontal_resizebar" resizebar="1">&nbsp;</div>
		<div id="right"></div>
		<div id="footer">
			<div id="numberitems"></div>
			<div id="loggedon"></div>
			<div id="quota_footer"></div>
			<div id="trial_warning"></div>
		</div>
	</body>
</html>
