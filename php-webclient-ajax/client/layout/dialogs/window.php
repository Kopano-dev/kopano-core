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
	include("client/loader.php");

	if (function_exists("initWindow")){
		initWindow();
	}

	header("Content-type: text/html; charset=utf-8");
?><!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<html>
	<head>
		<title><?=getDialogTitle()?></title>
		<!--[if IE]>
		<script type="text/javascript">
			window.BROWSER_IE=true;
			if(document.documentMode == 8 || (/trident\/\d/i.test(navigator.userAgent))) // IE8 but can be in compatible mode
				window.BROWSER_IE8 = true;
			window.BROWSER_IE6 = ( document.all && (/msie 6./i).test(navigator.appVersion) && window.ActiveXObject );
			try { document.execCommand("BackgroundImageCache", false, true); } catch(err) {}
		</script>
		<![endif]-->
<?php
		includeFiles("css", array(
								"client/layout/css/style.css",
								"client/layout/css/icons.css",
								"client/layout/css/dialog.css"
							)
		);
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

	$pluginCSSFiles = $GLOBALS['PluginManager']->getClientFiles('css', Array('all','dialog'));
	if(count($pluginCSSFiles) > 0){
		includeFiles('css', $pluginCSSFiles);
	}

	$pluginCustomCssFiles = Array();
	$GLOBALS['PluginManager']->triggerHook('server.dialog.general.include.cssfiles', Array('task' => $task, 'plugin' => $plugin, 'files'=> & $pluginCustomCssFiles));
	if(is_array($pluginCustomCssFiles)){
		foreach($pluginCustomCssFiles as $filename){
			echo "\t\t<link rel=\"stylesheet\" type=\"text/css\" href=\"$filename\">\n";
		}
	}
?>
		<script type="text/javascript" src="index.php?load=translations.js&lang=<?=$GLOBALS["language"]->getSelected()?>"></script>
<?php

	includeFiles("js", array(
							"client/core/constants.js",
							"client/core/fixedsettings.js",
							"client/core/utils.js",
							"client/views/view.js",
							"client/views/table.view.js",
							"client/widgets/widget.js",
							"client/widgets/menu.js",
							"client/layout/js/dialog.js",
							"client/modules/module.js",
							"client/modules/dialogmodule.js"
						)
	);

	includeJavaScriptFilesFromDir("core", array("constants.js", "utils.js"));

	if (function_exists('getModuleName')){
		includeFiles("js", array(
								"client/modules/listmodule.js", 
								"client/modules/itemmodule.js"
							)
		);
	}
	
	$includes = array();
	if (function_exists('getIncludes'))
		$includes = getIncludes();

	$GLOBALS['PluginManager']->triggerHook("server.dialog.general.setup.getincludes", array("task" => $task, 'plugin' => $plugin, "includes" => &$includes ));
	
	if (!empty($includes)){
		$css = array();
		$js = array();
		foreach($includes as $file){
			if (substr($file,-3) == "css"){
				$css[] = $file;
			}else if (substr($file,-2) == "js"){
				$js[] = $file;
			}
		}
		if (count($css)>0){
			includeFiles("css", $css);
		}
		if (count($js)>0){
			includeFiles("js", $js);
		}
	}

	$pluginJsFiles = $GLOBALS['PluginManager']->getClientFiles('js', Array('all','dialog'));
	if(count($pluginJsFiles) > 0){
		includeFiles('js', $pluginJsFiles);
	}
	$pluginModuleFiles = $GLOBALS['PluginManager']->getClientFiles('module', Array('all','dialog'));
	if(count($pluginModuleFiles) > 0){
		includeFiles('js', $pluginModuleFiles);
	}

	$pluginCustomJsFiles = Array();
	$GLOBALS['PluginManager']->triggerHook('server.dialog.general.include.jsfiles', Array('task' => $task, 'plugin' => $plugin, 'files'=> & $pluginCustomJsFiles));
	if(is_array($pluginCustomJsFiles)){
		foreach($pluginCustomJsFiles as $filename){
?>
		<script type="text/javascript" src="<?=$filename?>"></script>
<?php
		}
	}

	include("client/layout/themes.php");
?>		
		<script type="text/javascript">
			var CLIENT_TIMEOUT = <?=CLIENT_TIMEOUT?>;
			var DIALOG_URL = "<?=DIALOG_URL?>";
			var THEME_COLOR = "<?=THEME_COLOR?>";
			var DND_FILEUPLOAD_URL = <?=(defined('DND_FILEUPLOAD_URL')&&DND_FILEUPLOAD_URL)?'"'.DND_FILEUPLOAD_URL.'"':'false'?>;
			var parentwindow;
			var parentWebclient;
			var webclient;
			var dhtml;
			var module;
			var dialoghelper;
			var BASE_URL = "<?=BASE_URL?>";
			var NAMEDPROPS = {
				'PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords' : "<?=convertStringToHexNamedPropId('PT_MV_STRING8:PS_PUBLIC_STRINGS:Keywords')?>",
				'PT_STRING8:PS_PUBLIC_STRINGS:Keywords' : "<?=convertStringToHexNamedPropId('PT_STRING8:PS_PUBLIC_STRINGS:Keywords')?>"
			};
			
			window.onload = function()
			{

				parentwindow = window.opener;
				if(!parentwindow) {
					if(window.dialogArguments) {
						// IE modal dialog
						
						parentwindow = window.dialogArguments.parentWindow;
						window.opener = parentwindow;
						
						// In IE, the callback parameters are passed via 'dialogArguments' instead of directly
						// in the window object
						if(window.dialogArguments.resultCallBack)
							window.resultCallBack = window.dialogArguments.resultCallBack;
						if(window.dialogArguments.callBackData)
							window.callBackData = window.dialogArguments.callBackData;
						if(window.dialogArguments.windowData)
							window.windowData = window.dialogArguments.windowData;
					}
				}
				
				if(parentwindow && parentwindow.webclient) {
					parentWebclient = parentwindow.webclient;
					if(parentwindow.parentWebclient) {
						parentWebclient = parentwindow.parentWebclient;
					}
					
					dhtml = new DHTML();

					// disable context menu
					dhtml.addEvent(-1, document.body, "mouseup", checkMenuState);
					dhtml.addEvent(-1, document.body, "contextmenu", eventBodyMouseDown);

					var plugindata = new Array();
<?php
					$clientPluginData = $GLOBALS['PluginManager']->getClientPluginManagerData();
					foreach($clientPluginData as $singlePluginData){
						echo "\t\t\t\t\tplugindata.push({pluginname:\"".$singlePluginData['pluginname']."\"});\n";
					}
?>

					webclient = new WebClient();
					webclient.inputmanager = new InputManager();
					webclient.inputmanager.initKeyControl();
					webclient.setUserInfo(parentWebclient.username, parentWebclient.fullname, parentWebclient.userEntryid, parentWebclient.emailaddress );
					webclient.init(parentWebclient.base_url, (window.name)?window.name:"dialog",  Array("suggestEmailAddressModule.js"), null, plugindata);

<?php
	$buttons = array();
	if(function_exists('getMenuButtons'))  
		$buttons = getMenuButtons();
	
	$GLOBALS['PluginManager']->triggerHook("server.dialog.general.setup.getmenubuttons", array( "task" => $task, 'plugin' => $plugin, "buttons" => &$buttons ));
	
	if(!empty($buttons)) {
?>
					var menuItems = new Array();
<?php
		foreach($buttons as $button){
			if (isset($button['id'])){
				if(strstr($button['id'],"seperator")) {
					echo '					menuItems.push(webclient.menu.createMenuItem("'.$button['id'].'", ""));'."\n";
				} else if (isset($button['title']) && isset($button['name']) && isset($button['callback'])){
					$button["shortcut"] = isset($button['shortcut'])?'"'.$button['shortcut'].'"':"false";
					echo '					menuItems.push(webclient.menu.createMenuItem("'.$button['id'].'", "'.$button['name'].'", "'.$button['title'].'", '.$button['callback'].', '.$button['shortcut'].'));'."\n";
				}
			}
		}
?>
				webclient.menu.buildMenu(-1,menuItems);
<?php
	} // if buttons are available
?>
					dialoghelper = new dialogmodule();
					var dialoghelperID = webclient.addModule(dialoghelper);
					dialoghelper.init(dialoghelperID);
<?php 
	if(function_exists('getModuleName')) { 
		$moduleName = getModuleName();
		$GLOBALS['PluginManager']->triggerHook("server.dialog.general.setup.getmodulename", array( "task" => $task, 'plugin' => $plugin, "moduleName" => &$moduleName ));
	?>
					module = new <?=$moduleName?>();
					var moduleID = webclient.addModule(module);
<?php } // if function_exists ?>

					var data = new Object();
					data["task"] = "<?=$task?>";
					data["plugin"] = <?=(($plugin)?'"'.$plugin.'"':"false")?>;
					webclient.pluginManager.triggerHook("client.dialog.general.onload.before", data);

<?=(function_exists("getJavaScript_onload")?getJavaScript_onload():"")?>

					var data = new Object();
					data["task"] = "<?=$task?>";
					data["plugin"] = <?=(($plugin)?'"'.$plugin.'"':"false")?>;
					webclient.pluginManager.triggerHook("client.dialog.general.onload.after", data);
				}

				/**
				 * Set height of the dialog, If content of the dialog is not shown completely
				 * in the view, Suggest height of the dialog according to the content's
				 * position in the dialog, Add 10 pixels for better view.
				 */
				var dialog_content = dhtml.getElementById("dialog_content");
				var suggestedHeight = dialog_content.offsetTop + dialog_content.clientHeight + 10;
				var browserInnerSize = dhtml.getBrowserInnerSize();

				/**
				 * Change(Increase) height of the dialog only if height of the dialog is
				 * lesser then the suggest height, It should be possible to set deliberately
				 * more height for the dialog. And moreover this won't affect the dialogs
				 * which are working fine.
				 */
				if(browserInnerSize.y < suggestedHeight){
					if(window.dialogHeight) { // for modal dialogs in IE.
						window.dialogHeight = suggestedHeight + 'px';
					}else if(window.resizeBy){
						window.resizeBy(0, suggestedHeight - browserInnerSize.y);
					}
				}
			}

			window.onresize = function()
			{
				if(webclient){
					var data = new Object();
					data["task"] = "<?=$task?>";
					data["plugin"] = <?=(($plugin)?'"'.$plugin.'"':"false")?>;
					webclient.pluginManager.triggerHook("client.dialog.general.onresize.after", data);

<?php
	
	if(function_exists("getJavaScript_onresize")) {
		echo getJavaScript_onresize();
	} else {
		if(function_exists('getModuleName')) { 
			echo "\t\t\t\tresizeBody();\n";
		}
	}
?>
					var data = new Object();
					data["task"] = "<?=$task?>";
					data["plugin"] = <?=(($plugin)?'"'.$plugin.'"':"false")?>;
					webclient.pluginManager.triggerHook("client.dialog.general.onresize.before", data);
				}
			}

			window.onbeforeunload = function()
			{
<?php
			if(function_exists("getJavaScript_onbeforeunload")) {
				echo getJavaScript_onbeforeunload();
			}
?>
			}
<?php

	if (function_exists("getJavaScript_other")){
		echo getJavaScript_other();
	}
?>
		</script>
	</head>
	<body	class="dialog"
			scroll="no">

		<div class="title">
			<div id="windowtitle" class="zarafa_title"><?=getDialogTitle()?></div>
			<div class="zarafa_background"></div>
		</div>
<?php if (!empty($buttons)) { ?>
		<div id="menubar">
			<div id="menubar_left"></div>
			<div id="menubar_right"></div>
			<div id="zarafa_loader"></div>
		</div>
<?php } else { // if is_array?>
		<div class="subtitle">
			<div class="subtitle_zarafa_background"></div>
			<span id="subtitle" class="zarafa_title">&nbsp;</span>
		</div>
<?php } // else?>

		<div id="dialog_content">
<?php
$htmloutput = '';
$GLOBALS['PluginManager']->triggerHook("server.dialog.general.setup.getbody.before", array( "task" => $task, 'plugin' => $plugin, "html" => &$htmloutput ));
echo $htmloutput;
?>
<?=getBody()?>
		</div>
		<input id="dialog_attachments" type="hidden" value="<?=md5(uniqid(rand(),true))?>">
	</body>
</html>
