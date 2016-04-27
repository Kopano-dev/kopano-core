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
	$user = htmlentities($_GET["user"]);
	header("Content-type: text/html; charset=utf-8");

	// Get the arguments from the arguments passed in the REQUEST URI/$_GET
	$actionReqURI = getActionRequestURI('&');

?><!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<html>
	<head>
		<title>Zarafa WebAccess</title>
		<link rel="stylesheet" type="text/css" href="client/layout/css/login.css">
		<link rel="icon" href="client/layout/img/favicon.ico"  type="image/x-icon">
		<link rel="shortcut icon" href="client/layout/img/favicon.ico" type="image/x-icon">	
		<script type="text/javascript">
			window.onload = function(){
				if (document.getElementById("username").value == ""){
					document.getElementById("username").focus();
				}else if (document.getElementById("password").value == ""){
					document.getElementById("password").focus();
				}
			}
		</script>
	</head>
	<body class="login">
		<table id="layout">
			<tr><td>
				<div id="login_main">
					<form action="index.php?logon<?=($user)?'&user='.$user:''?><?=($actionReqURI)?$actionReqURI:''?>" method="post">
						<div id="login_data">
							<p><?=_("Please logon")?>.</p>
							<p class="error"><?php

	if (isset($_SESSION) && isset($_SESSION["hresult"])) {
		switch($_SESSION["hresult"]){
			case MAPI_E_LOGON_FAILED:
			case MAPI_E_UNCONFIGURED:
				if (isset($_POST["username"]))
					error_log("User \"".$_POST["username"]."\": authentication failure at MAPI");
				echo _("Logon failed, please check your name/password.");
				break;
			case MAPI_E_NETWORK_ERROR:
				echo _("Cannot connect to the Zarafa Server.");
				break;
			default:
				echo "Unknown MAPI Error: ".get_mapi_error_name($_SESSION["hresult"]);
		}
		unset($_SESSION["hresult"]);
	}else if (isset($_GET["logout"]) && $_GET["logout"]=="auto"){
		echo _("You have been automatically logged out");
	}else{
		echo "&nbsp;";
	}
							?></p>
							<table id="form_fields">
								<tr>
									<th><label for="username"><?=_("Name")?>:</label></th>
									<td><input type="text" name="username" id="username" class="inputelement"
									<?php
									 if (defined("CERT_VAR_TO_COMPARE_WITH") && $_SERVER ) {
										echo " value='".$_SERVER[CERT_VAR_TO_COMPARE_WITH]."' readonly='readonly'";
									 } elseif ($user) {
									 	echo " value='".$user."'";
									 }
									?>></td>
								</tr>
								<tr>
									<th><label for="password"><?=_("Password")?>:</label></th>
									<td><input type="password" name="password" id="password" class="inputelement"></td>
								</tr>
								<tr>
									<th><label for="language"><?=_("Language")?>:</label></th>
									<td>
										<select name="language" id="language" class="inputelement">
											<option value="last"><?=_("Last used language")?></option>
<?php
  function langsort($a, $b) { return strcasecmp($a, $b); } 	
  $langs = $GLOBALS["language"]->getLanguages();
  uasort($langs, 'langsort');
  foreach($langs as $lang=>$title){ 
?>											<option value="<?=$lang?>"><?=$title?></option>
<?php   } ?>
										</select>
									</td>
								</tr>
								<tr>
									<td>&nbsp;</td>
									<td><input id="submitbutton" type="submit" value=<?=_("Logon")?>></td>
								</tr>
							</table>
						</div>
					</form>
					<span id="version"><?=defined("DEBUG_SERVER_ADDRESS")?"Server: ".DEBUG_SERVER_ADDRESS." - ":""?><?=phpversion("mapi")?><?=defined("SVN")?"-svn".SVN:""?></span>
				</div>
			</td></tr>
		</table>
	</body>
</html>
