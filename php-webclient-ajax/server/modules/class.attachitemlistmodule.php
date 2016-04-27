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
	class AttachItemListModule extends ListModule
	{
		/**
		 * Constructor
		 * @param int $id nique id.
		 * @param array		$data list of all actions.
		 */
		function AttachItemListModule($id, $data)
		{
			parent::ListModule($id, $data, array(OBJECT_SAVE, TABLE_SAVE, TABLE_DELETE));

			$this->sort = array();
		}

		/**
		 * Executes all the actions in the $data variable.
		 * @return boolean true on success or false on failure.
		 */
		function execute()
		{
			$result = false;
			
			foreach($this->data as $action)
			{
				if(isset($action["attributes"]) && isset($action["attributes"]["type"])) {
					$store = $this->getActionStore($action);
					$parententryid = $this->getActionParentEntryID($action);
					$entryid = $this->getActionEntryID($action);

					switch($action["attributes"]["type"])
					{
						case "list":
							// get properties and column info
							$this->getColumnsAndPropertiesForMessageType($store, $entryid, $action);
							// @TODO:	get the sort data from sort settings
							//			not working now, as of sorting is not suppported by table widget.
							$this->sort[$this->properties["subject"]] = TABLE_SORT_ASCEND;
	
							$result = $this->messageList($store, $entryid, $action);
							break;
						
						case "attach_items":
							$this->attach($store, $parententryid, $action);
							break;
					}
				}
			}
			return $result;
		}

		

		/**
		 * Function will set properties and table columns for particular message class
		 * @param Object $store MAPI Message Store Object
		 * @param HexString $entryid entryid of the folder
		 * @param Array $action the action data, sent by the client
		 */
		function getColumnsAndPropertiesForMessageType($store, $entryid, $action)
		{
			if(isset($action["container_class"])) {
				$messageType = $action["container_class"];

				switch($messageType) {
					case "IPF.Appointment":
						$this->properties = $GLOBALS["properties"]->getAppointmentProperties();
						$this->tablecolumns = $GLOBALS["TableColumns"]->getAppointmentListTableColumns();
						break;
					case "IPF.Contact":
						$this->properties = $GLOBALS["properties"]->getContactProperties();
						$this->tablecolumns = $GLOBALS["TableColumns"]->getContactListTableColumns();
						break;
					case "IPF.Journal":		// not implemented
						break;
					case "IPF.Task":
						$this->properties = $GLOBALS["properties"]->getTaskProperties();
						$this->tablecolumns = $GLOBALS["TableColumns"]->getTaskListTableColumns();
						break;
					case "IPF.StickyNote":
						$this->properties = $GLOBALS["properties"]->getStickyNoteProperties();
						$this->tablecolumns = $GLOBALS["TableColumns"]->getStickyNoteListTableColumns();
						break;
					case "IPF.Note":
					default:
						$this->properties = $GLOBALS["properties"]->getMailProperties();
						$this->tablecolumns = $GLOBALS["TableColumns"]->getMailListTableColumns();

						// enable columns that are by default disabled
						$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "message_delivery_time", "visible", true);
						$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "sent_representing_name", "visible", true);
						break;
				}

				// add folder name column
				if($GLOBALS["TableColumns"]->getColumn($this->tablecolumns, "parent_entryid") === false) {
					$GLOBALS["TableColumns"]->addColumn($this->tablecolumns, "parent_entryid", true, 6, _("In Folder"), _("Sort Folder"), 90, "folder_name");
				}
			}
		}

		/**
		 * Function which sets selected messages as attachment to the item
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure 
		 */
		function attach($store, $parententryid, $action)
		{
			$result = false;
			
			if($store){
				if(isset($action["entryids"]) && $action["entryids"]){
					
					if(!is_array($action["entryids"])){
						$action["entryids"] = array($action["entryids"]);
					}
					if($action["type"] == "attachment"){
						// add attach items to attachment state
						$attachments = $GLOBALS["operations"]->setAttachmentInSession($store, $action["entryids"], $action["dialog_attachments"]);

						$data = array();
						$data["attributes"] = array("type" => "attach_items");
						$data["item"] = array();
						$data["item"]["parententryid"] = bin2hex($parententryid);
						$data["item"]["attachments"] = $attachments;
						array_push($this->responseData["action"], $data);
						$GLOBALS["bus"]->addData($this->responseData);
						$result = true;
					}else{
						$data = array();
						$data["attributes"] = array("type" => "attach_items_in_body");
						$data["item"] = array();

						foreach($action["entryids"] as $key => $item){
							$message = mapi_msgstore_openentry($store, hex2bin($item));
							$items = array();
							$items = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);

							array_push($data["item"], $items);
						}
						array_push($this->responseData["action"], $data);
						$GLOBALS["bus"]->addData($this->responseData);
						$result = true;
					}
				}
			}
			return $result;
		}
	}
?>
