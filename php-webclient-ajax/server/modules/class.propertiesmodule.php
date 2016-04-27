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

	/**
	 * Folder Properties Module
	 */
	class PropertiesModule extends Module
	{
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param string $folderentryid Entryid of the folder. Data will be selected from this folder.
		 * @param array $data list of all actions.
		 */
		function PropertiesModule($id, $data)
		{
			parent::Module($id, $data);
		}
		
		/**
		 * Executes all the actions in the $data variable.
		 * @return boolean true on success of false on fialure.
		 */
		function execute()
		{
			$result = false;
			foreach($this->data as $action)
			{
				if(isset($action["attributes"]) && isset($action["attributes"]["type"])) {
					switch($action["attributes"]["type"])
					{
						case "folderprops":
							$store = $GLOBALS["mapisession"]->openMessageStore(hex2bin($action["store"]));
							$folder = mapi_msgstore_openentry($store, hex2bin($action["entryid"]));
							
							$data = $this->getFolderProps($store, $folder);

							// return response
							$data["attributes"] = array("type"=>"folderprops");
							array_push($this->responseData["action"], $data);
							$GLOBALS["bus"]->addData($this->responseData);
							$result = true;
							break;

						case "save":
							$store = $GLOBALS["mapisession"]->openMessageStore(hex2bin($action["store"]));
							$folder = mapi_msgstore_openentry($store, hex2bin($action["entryid"]));

							$result = $this->save($folder,$action);

							if($result != NOERROR) {
								// return error
								$data = array();
								$data["attributes"] = array("type" => "error");
								$data["error"] = array();
								$data["error"]["hresult"] = $result;
								$data["error"]["hresult_name"] = get_mapi_error_name($result);
								if($result == (int) MAPI_E_NO_ACCESS) {
									$data["error"]["message"] = _("You have insufficient permissions to set permissions.");
								} else if($result == (int) MAPI_W_PARTIAL_COMPLETION) {
									$data["error"]["message"] = _("Unable to set some permissions. Permissions are only allowed on users and security groups.");
								} else {
									$data["error"]["message"] = _("Unable to set all permissions.");
								}

								array_push($this->responseData["action"], $data);
								$GLOBALS["bus"]->addData($this->responseData);
								$result = true;
							} else {
								$data = array();
								$data["attributes"] = array("type" => "saved");

								array_push($this->responseData["action"], $data);
								$GLOBALS["bus"]->addData($this->responseData);
							}
							break;
					}
				}
			}
			
			return $result;
		}

		/**
		* returns properties of a folder, used by the properties dialog
		*/
		function getFolderProps($store, $folder)
		{
			$data = $GLOBALS["operations"]->getProps($store, $folder, $GLOBALS["properties"]->getFolderProperties());	
							
			// adding container_class if missing
			if (!isset($data["container_class"])){
				$data["container_class"] = "IPF.Note";
			}

			// adding missing comment
			if (!isset($data["comment"])){
				$data["comment"] = "";
			}
							
			// replace "IPM_SUBTREE" with the display name of the store, and use the store message size
			if ($data["display_name"] == "IPM_SUBTREE"){
				$store_props = mapi_getprops($store, array(PR_DISPLAY_NAME, PR_MESSAGE_SIZE_EXTENDED, PR_QUOTA_WARNING_THRESHOLD, PR_QUOTA_SEND_THRESHOLD, PR_QUOTA_RECEIVE_THRESHOLD));
				$data["display_name"] = windows1252_to_utf8($store_props[PR_DISPLAY_NAME]);
				$data["message_size"] = round($store_props[PR_MESSAGE_SIZE_EXTENDED]/1024). " "._("kb");
				$data["store_size"] = round($store_props[PR_MESSAGE_SIZE_EXTENDED]/1024);

				if (isset($store_props[PR_QUOTA_WARNING_THRESHOLD]))
					$data["quota_warning"] = round($store_props[PR_QUOTA_WARNING_THRESHOLD]);
				if (isset($store_props[PR_QUOTA_SEND_THRESHOLD]))
					$data["quota_soft"] = round($store_props[PR_QUOTA_SEND_THRESHOLD]);
				if (isset($store_props[PR_QUOTA_RECEIVE_THRESHOLD]))
					$data["quota_hard"] = round($store_props[PR_QUOTA_RECEIVE_THRESHOLD]);
			}

			// retrieve parent folder name
			if (is_array($data["parent_entryid"]) && isset($data["parent_entryid"]["_content"])){
				$data["parent_entryid"] = $data["parent_entryid"]["_content"];
			}
			$parent_folder = mapi_msgstore_openentry($store, hex2bin($data["parent_entryid"]));
			$parent_props = mapi_getprops($parent_folder, array(PR_DISPLAY_NAME));
			$data["parent_display_name"] = "";
			if (isset($parent_props[PR_DISPLAY_NAME])){
				$data["parent_display_name"] = windows1252_to_utf8($parent_props[PR_DISPLAY_NAME]);
				if ($data["parent_display_name"] == "IPM_SUBTREE"){
					// this must be the root folder, so get the name of the store
					$store_props = mapi_getprops($store, array(PR_DISPLAY_NAME));
					$data["parent_display_name"] = windows1252_to_utf8($store_props[PR_DISPLAY_NAME]);
				}
			}
			
			// calculating missing message_size
			if (!isset($data["message_size"])){
				$data["message_size"] = round($GLOBALS["operations"]->calcFolderMessageSize($folder, false)/1024). " "._("kb");
			}
			
			// retrieving folder permissions
			$data["permissions"] = $this->getFolderPermissions($folder);

			return $data;
		}

		/**
		 * Function which saves changed properties to a folder.
		 * @param object $folder MAPI object of the folder
		 * @param array $props the properties to save
		 * @return boolean true on success or false on failure		 		 
		 */
		function save($folder, $action)
		{
			mapi_setprops($folder, array(PR_COMMENT=>utf8_to_windows1252($action["comment"])));
			$result = mapi_last_hresult();
			
			if (isset($action["permissions"])){
				$returnValue = $this->setFolderPermissions($folder, $action["permissions"]);
				if($returnValue != NOERROR) {
					$result = $returnValue;
				}
			}
			
			mapi_savechanges($folder);
			$result = ($result == NOERROR) ? mapi_last_hresult() : $result;

			return $result;
		}


		function getFolderPermissions($folder)
		{
			// check if folder is rootFolder, then we need the permissions from the store
			$folderProps = mapi_getprops($folder, array(PR_DISPLAY_NAME, PR_STORE_ENTRYID));

			$store = $GLOBALS["mapisession"]->openMessageStore($folderProps[PR_STORE_ENTRYID]);
			if ($folderProps[PR_DISPLAY_NAME] == "IPM_SUBTREE"){
				$folder = $store; 
			}

			$grants = mapi_zarafa_getpermissionrules($folder, ACCESS_TYPE_GRANT);
			foreach($grants as $id=>$grant){
				unset($grant["type"]);
				unset($grant["state"]);

				// The mapi_zarafa_getpermissionrules returns the entryid in the userid key
				$userinfo = $this->getUserInfo($grant["userid"]);
				unset($grant["userid"]);

				$grant["username"] = $userinfo["username"];
				$grant["fullname"] = $userinfo["fullname"];
				$grant["usertype"] = $userinfo["type"];
				$grant["entryid"] = $userinfo["entryid"];

				$grants[$id] = $grant;
			}

			$result = array("grant"=>$grants);
			return $result;			
		}

		function setFolderPermissions($folder, $permissions)
		{
			// first, get the current permissions because we need to delete all current acl's 
			$current_perms = $this->getFolderPermissions($folder);

			$folderProps = mapi_getprops($folder, array(PR_DISPLAY_NAME, PR_STORE_ENTRYID, PR_ENTRYID));
			$store = $GLOBALS["mapisession"]->openMessageStore($folderProps[PR_STORE_ENTRYID]);
			$storeProps = mapi_getprops($store, array(PR_IPM_SUBTREE_ENTRYID));

			// check if the folder is the default calendar, if so we also need to set the same permissions on the freebusy folder
			$inbox = mapi_msgstore_getreceivefolder($store);
			// public store can not have inbox folder
			if(isset($inbox) && $inbox) {
				$inboxProps = mapi_getprops($inbox, array(PR_IPM_APPOINTMENT_ENTRYID, PR_FREEBUSY_ENTRYIDS));
				if ($folderProps[PR_ENTRYID] == $inboxProps[PR_IPM_APPOINTMENT_ENTRYID]){
					if(isset($inboxProps[PR_FREEBUSY_ENTRYIDS]) && isset($inboxProps[PR_FREEBUSY_ENTRYIDS][3])){
						$freebusy = mapi_msgstore_openentry($store, $inboxProps[PR_FREEBUSY_ENTRYIDS][3]);
					}
				}
			}

			// check if folder is rootFolder, then we need the permissions from the store
			if ($folderProps[PR_ENTRYID] == $storeProps[PR_IPM_SUBTREE_ENTRYID]){
				$folder = $store; 
			}

			// Collect old/current permissions and mark them as delete
			$delete_acls = array();
			foreach($current_perms as $cur_tmp=>$cur_perms){
				foreach($cur_perms as $i=>$cur_perm){
					$acls =	array(
								"userid" => hex2bin($cur_perm["entryid"]),
								"type" => ACCESS_TYPE_GRANT,
								"rights" => 0,
								"state" => RIGHT_DELETED|RIGHT_AUTOUPDATE_DENIED
							);
					// Make sure index for $delete_acls is string
					$delete_acls[$cur_perm["entryid"]] = $acls;
				}
			}

			$acls = array();
			if (is_array($permissions) && count($permissions) > 0){
				foreach($permissions as $type=>$perms){
					switch($type){
						case "denied":
							$type = ACCESS_TYPE_DENIED;
							break;
						case "grant":
						default:
							$type = ACCESS_TYPE_GRANT;
							break;
					}

					// when there is just one user in the permission, wrap it in an array
					if (!is_array($perms[key($perms)])){
						$perms = array($perms);
					}
	
					foreach($perms as $i=>$perm){
						$acl = array(
							"type" => (int)$type,
							"userid" => hex2bin($perms[$i]["entryid"]),
							"rights" => (int)$perms[$i]["rights"],
							"state" => RIGHT_NEW | RIGHT_AUTOUPDATE_DENIED
						);
						$acls[$perms[$i]["entryid"]] = $acl;
					}
				}
			}

			// Merge new permissions in to old permissions
			$acls = array_merge($delete_acls, $acls);

			if(count($acls) > 0) {
				mapi_zarafa_setpermissionrules($folder, $acls);
				if(mapi_last_hresult() != NOERROR) {
					// unable to set permissions
					return mapi_last_hresult(); 
				}
			}

			// $freebusy is only set when the calendar folder permissions is updated
			if (isset($freebusy)){
				// set permissions on free/busy message
				foreach($acls as $key=>$value) {
					if( $acls[$key]["type"] == ACCESS_TYPE_GRANT && ($acls[$key]["rights"] & ecRightsEditOwned))
						$acls[$key]["rights"] |= ecRightsEditAny;
				}
				mapi_zarafa_setpermissionrules($freebusy, $acls);
				if(mapi_last_hresult() != NOERROR) {
					// unable to set permissions
					return mapi_last_hresult(); 
				}
			}

			// no error in setting permissions
			return NOERROR;
		}

		function getUserInfo($entryid){

			// default return stuff
			$result = array("fullname"=>_("Unknown user/group"),
							"username"=>_("unknown"),
							"entryid"=>null,
							"type"=>MAPI_MAILUSER,
							"id"=>$entryid
							);

			// open the addressbook
			$ab = $GLOBALS["mapisession"]->getAddressbook();

			$user = mapi_ab_openentry($ab, $entryid);

			if ($user){
				$props = mapi_getprops($user, array(PR_ACCOUNT, PR_DISPLAY_NAME, PR_OBJECT_TYPE));
				$result["username"] = windows1252_to_utf8($props[PR_ACCOUNT]);
				$result["fullname"] = windows1252_to_utf8($props[PR_DISPLAY_NAME]);
				$result["entryid"] = bin2hex($entryid);
				$result["type"] = $props[PR_OBJECT_TYPE];
			}
			return $result;
		}
	}
?>
