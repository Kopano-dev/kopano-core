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
	 * DistList ItemModule
	 * Module which openes, creates, saves and deletes an item. It 
	 * extends the Module class.
	 */
	class DistListModule extends ListModule
	{
		/**
		 * @var Array properties of distribution list item that will be used to get data
		 */
		var $properties = null;

		var $plaintext;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function DistListModule($id, $data)
		{
			$this->tablecolumns = $GLOBALS["TableColumns"]->getDistListTableColumns();

			$this->plaintext = true;
		
			parent::ListModule($id, $data, array());

			$this->sort = array();
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
				$store = $this->getActionStore($action);
				$parententryid = $this->getActionParentEntryID($action);
				$entryid = $this->getActionEntryID($action);

				$this->generatePropertyTags($store, $entryid, $action);

				switch($action["attributes"]["type"]) {
					case 'list':
						$result = $this->item($store, $entryid, $action);
						break;
					case 'save':
						$result = $this->save($store, $parententryid, $action);
						break;
				}
			}
			
			return $result;
		}

		/**
		 * Function which opens an item.
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function item($store, $entryid, $action)
		{
			$result = false;
			
			if($store && $entryid) {
				$data = array();
				$data["attributes"] = array("type" => "list");
				$message = $GLOBALS["operations"]->openMessage($store, $entryid);
				$data["props"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, true);

				// remove non-client props
				unset($data["props"]["members"]);
				unset($data["props"]["oneoff_members"]);
				
				// get members
				$messageProps = mapi_getprops($message, array($this->properties["members"], $this->properties["oneoff_members"]));
				$members = isset($messageProps[$this->properties["members"]])?$messageProps[$this->properties["members"]]:array();
				$oneoff_members = isset($messageProps[$this->properties["oneoff_members"]])?$messageProps[$this->properties["oneoff_members"]]:array();

				// parse oneoff members
				foreach($oneoff_members as $key=>$item){
					$oneoff_members[$key] = mapi_parseoneoff($item);
				}

				$items = array();
				$count = 0;
				foreach($members as $key=>$item){
					$parts = unpack("Vnull/A16guid/Ctype/A*entryid", $item);
					
					if ($parts["guid"]==hex2bin("812b1fa4bea310199d6e00dd010f5402")){
						$item = mapi_parseoneoff($item);
						$item["distlisttype"] = "ONEOFF";
						$item["entryid"] = "oneoff_".(++$count)."_".bin2hex($members[$key]);
						$item["icon_index"] = 512;
						$item["message_class"] = "IPM.DistListItem.OneOffContact";
					}else{
						$item = array();
						$item["name"] = $oneoff_members[$key]["name"];
						$item["type"] = $oneoff_members[$key]["type"];
						$item["address"] = $oneoff_members[$key]["address"];
						$item["entryid"] = array("attributes"=>array("type"=>"binary"), "_content"=>bin2hex($parts["entryid"]));
						
						// please note that the updated info isn't saved directly, but send to the client, when he submit the distlist again _then_ the dist list is really updated!
						$updated_info = $this->updateItem($store, $oneoff_members[$key], $parts);
						if ($updated_info){
							$item["name"] = $updated_info["name"];
							$item["type"] = $updated_info["type"];
							$item["address"] = $updated_info["email"];
						}else{
							$item["missing"] = "1";
						}

						switch($parts["type"]){
							case 0:
								$item["missing"] = "0";
								$item["distlisttype"] = "ONEOFF";
								$item["icon_index"] = 512;
								$item["message_class"] = "IPM.DistListItem.OneOffContact";
								break;
							case DL_USER:
								$item["distlisttype"] = "DL_USER";
								$item["icon_index"] = 512;
								$item["message_class"] = "IPM.Contact";
								break;
							case DL_USER2:
								$item["distlisttype"] = "DL_USER2";
								$item["icon_index"] = 512;
								$item["message_class"] = "IPM.Contact";
								break;
							case DL_USER3:
								$item["distlisttype"] = "DL_USER3";
								$item["icon_index"] = 512;
								$item["message_class"] = "IPM.Contact";
								break;
							case DL_USER_AB:
								$item["distlisttype"] = "DL_USER_AB";
								$item["icon_index"] = 512;
								$item["message_class"] = "IPM.DistListItem.AddressBookUser";
								break;
							case DL_DIST:
								$item["distlisttype"] = "DL_DIST";
								$item["icon_index"] = 514;
								$item["message_class"] = "IPM.DistList";
								break;
							case DL_DIST_AB:
								$item["distlisttype"] = "DL_DIST_AB";
								$item["icon_index"] = 514;
								$item["message_class"] = "IPM.DistListItem.AddressBookGroup"; // needed
								break;
						}
					}

					$item["name"] = w2u($item["name"]);
					$item["address"] = w2u($item["address"]);

					$item["message_flags"] = 1;
					$items[] = $item;
				}
				// if there is only one item and that also doesnt has entryid, dont send it to client
				if(count($items) > 1 || (count($items) == 1 && $items[0]["entryid"]["_content"] != ""))
					$data["item"] = $items;

				// List columns visible
				$GLOBALS["TableColumns"]->parseVisibleColumns($this->tablecolumns, $action);

				// Add columns to data
				$data["column"] = $this->tablecolumns;

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
				
				$result = true;
			}
			
			return $result;
		}
		
		function updateItem($store, $oneoff, $parts)
		{

			$result = false;
			$number = 1; // needed for contacts
			switch($parts["type"]){
				case DL_USER3:
					$number++;
				case DL_USER2:
					$number++;
				case DL_USER:
					$item = mapi_msgstore_openentry($store, $parts["entryid"]);
					if (mapi_last_hresult()==NOERROR){
						$properties = $GLOBALS["properties"]->getContactProperties();
						$props = mapi_getprops($item, $properties);
						if (is_int(array_search(($number-1), $props[$properties["address_book_mv"]])) &&
							isset($props[$properties["email_address_".$number]]) &&
							isset($props[$properties["email_address_display_name_".$number]]) &&
							isset($props[$properties["email_address_type_".$number]])){
							
							$result = array(
										"name"=>$props[$properties["email_address_display_name_".$number]],
										"email"=>$props[$properties["email_address_".$number]],
										"type"=>$props[$properties["email_address_type_".$number]],
									);
						}
					}
					break;
				case DL_DIST:
					$item = mapi_msgstore_openentry($store, $parts["entryid"]);
					if (mapi_last_hresult()==NOERROR){
						$props = mapi_getprops($item, array(PR_DISPLAY_NAME));
						$result = array(
									"name"=>$props[PR_DISPLAY_NAME],
									"email"=>$props[PR_DISPLAY_NAME],
									"type"=>"SMTP"
								);
					}
					break;
				case DL_USER_AB:
				case DL_DIST_AB:
					$ab = $GLOBALS["mapisession"]->getAddressBook();
					$item = mapi_ab_openentry($ab, $parts["entryid"]);
					if (mapi_last_hresult()==NOERROR){
						$props = mapi_getprops($item, array(PR_DISPLAY_NAME, PR_SMTP_ADDRESS, PR_ADDRTYPE));
						$result = array(
									"name"=>$props[PR_DISPLAY_NAME],
									"email"=>$props[PR_SMTP_ADDRESS],
									"type"=>$props[PR_ADDRTYPE]
								);
					}
					break;
			}
			return $result;
		}

		function save($store, $parententryid, $action)
		{
			$result = false;

			if($store && $parententryid && isset($action["props"])) {

				$props = Conversion::mapXML2MAPI($this->properties, $action["props"]);
				$deleteProps = array();

				// collect members
				if (isset($action["members"]) && isset($action["members"])){
					$members = array();
					$oneoff_members = array();

					$items = $action["members"]["item"];
					if (!is_array($items) || !isset($items[0]))
						$items = array($items);

					foreach($items as $item){
						if (!isset($item["deleted"])){
							$oneoff = mapi_createoneoff(u2w($item["name"]), $item["type"], u2w($item["address"]));
							if ($item["distlisttype"] == "ONEOFF"){
								$member = $oneoff;
							}else{
								$parts = array();
								$parts["guid"] = DL_GUID;
								$parts["type"] = constant($item["distlisttype"]);
								$parts["entryid"] = hex2bin($item["entryid"]);
								$member = pack("VA16CA*", 0, $parts["guid"], $parts["type"], $parts["entryid"]);
							}
							$oneoff_members[] = $oneoff;
							$members[] = $member;
						}
					}
					
					if (count($members)>0 && count($oneoff_members)>0){
						$props[($this->properties["members"] &~ MV_INSTANCE)] = $members;
						$props[($this->properties["oneoff_members"] &~ MV_INSTANCE)] = $oneoff_members;
					}else{
						$deleteProps[] = $this->properties["members"];
						$deleteProps[] = $this->properties["oneoff_members"];
					}
				}
				
				$messageProps = array();
				$result = $GLOBALS["operations"]->saveMessage($store, $parententryid, $props, false, "", $messageProps, false, false, $deleteProps);
	
				if($result) {
					$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_SAVE, $messageProps);
				}
			}
			
			return $result;

		}

		/**
		 * Function will generate property tags based on passed MAPIStore to use
		 * in module. These properties are regenerated for every request so stores
		 * residing on different servers will have proper values for property tags.
		 * @param MAPIStore $store store that should be used to generate property tags.
		 * @param Binary $entryid entryid of message/folder
		 * @param Array $action action data sent by client
		 */
		function generatePropertyTags($store, $entryid, $action)
		{
			$this->properties = $GLOBALS["properties"]->getDistListProperties($store);
		}
	}
?>
