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
	 * Buttons Module
	 */
	class ButtonsModule extends Module
	{
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param string $folderentryid Entryid of the folder. Data will be selected from this folder.
		 * @param array $data list of all actions.
		 */
		function ButtonsModule($id, $data)
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
						case "list":
							$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
							$inbox = mapi_msgstore_getreceivefolder($store);
							$inboxProps = array();
							if($inbox) {
								$inboxProps = mapi_getprops($inbox, array(PR_ENTRYID, PR_IPM_APPOINTMENT_ENTRYID, PR_IPM_CONTACT_ENTRYID, PR_IPM_TASK_ENTRYID, PR_IPM_NOTE_ENTRYID));
							}
							
							$data = array();
							$data["attributes"] = array("type" => "list");
							$data["folders"] = array();
							
							$button_counter = 1;
							foreach($inboxProps as $prop){
								$folder = mapi_msgstore_openentry($store, $prop);
								$folderProps = mapi_getprops($folder, array(PR_DISPLAY_NAME, PR_CONTAINER_CLASS));
								
								$data["folders"]["button".$button_counter] = array();
								$data["folders"]["button".$button_counter]["entryid"] = bin2hex($prop);
								$data["folders"]["button".$button_counter]["title"] = windows1252_to_utf8($folderProps[PR_DISPLAY_NAME]);
								
								$icon = "folder";
								if (isset($folderProps[PR_CONTAINER_CLASS])) {
									$icon = strtolower(substr($folderProps[PR_CONTAINER_CLASS],4));
								} else {
									// fix for inbox icon
									if ($prop == $inboxProps[PR_ENTRYID]){
										$icon = "inbox";
									}
								}
								$data["folders"]["button".$button_counter]["icon"] = $icon;
								$button_counter++;
							}
							
							array_push($this->responseData["action"], $data);
							$GLOBALS["bus"]->addData($this->responseData);
							$result = true;
							break;
					}
				}
			}
			
			return $result;
		}
	}
?>
