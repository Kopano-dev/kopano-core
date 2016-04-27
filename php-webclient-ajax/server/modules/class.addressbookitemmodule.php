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
	 * Read Mail ItemModule
	 */
	class AddressbookItemModule extends ItemModule
	{
		var $userDetailProperties = null;
		var $abObjectDetailProperties = null;
		var $groupDetailProperties = null;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function AddressbookItemModule($id, $data)
		{
			parent::ItemModule($id, $data);
		}

		/**
		 * Function which opens an item.
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure 
		 */
		function open($store, $entryid, $action)
		{
			$result = false;
			
			if($store && $entryid) {
				$data = array();
				$data["attributes"] = array("type" => "item");

				$abentry = mapi_ab_openentry($GLOBALS["mapisession"]->getAddressbook(), $entryid);

				if(mapi_last_hresult() == NOERROR && $abentry){
					$objecttypeprop = mapi_getprops($abentry, Array(PR_OBJECT_TYPE));

					if(isset($objecttypeprop[PR_OBJECT_TYPE])){

						// Get the properties for a MAILUSER object and process those MAILUSER specific props that require some more actions
						if($objecttypeprop[PR_OBJECT_TYPE] == MAPI_MAILUSER){
							$messageprops = mapi_getprops($abentry, $this->userDetailProperties);
							$props = Conversion::mapMAPI2XML($this->userDetailProperties, $messageprops);

							// Get the properties of the manager
							$managerProps = $this->getManagerDetails($messageprops);
							if($managerProps!==false){
								$props['ems_ab_manager'] = Conversion::mapMAPI2XML($this->abObjectDetailProperties, $managerProps);
							}

							// Set the home2 telephone numbers list properly with the selected item
							if(isset($messageprops[$this->userDetailProperties['home2_telephone_number_mv']])){
								if(count($messageprops[$this->userDetailProperties['home2_telephone_number_mv']] > 0)){
									// Add the list of home2_telephone_numbers in the correct format to the $props list to be send to the client.
									$props['home2_telephone_numbers'] = Array('home2_telephone_numbers_entry' => $messageprops[$this->userDetailProperties['home2_telephone_number_mv']]);

									// Check if the home2_telephone_number as that could be the case
									if(isset($messageprops[$this->userDetailProperties['home2_telephone_number']])){
										$props['home2_telephone_number'] = $messageprops[$this->userDetailProperties['home2_telephone_number']];
									}
								}
							}

							// Set the business2_telephone_numbers list properly with the selected item
							if(isset($messageprops[$this->userDetailProperties['business2_telephone_number_mv']])){
								if(count($messageprops[$this->userDetailProperties['business2_telephone_number_mv']] > 0)){
									// Add the list of business2_telephone_numbers in the correct format to the $props list to be send to the client.
									$props['business2_telephone_numbers'] = Array('business2_telephone_numbers_entry' => $messageprops[$this->userDetailProperties['business2_telephone_number_mv']]);

									// Check if the business2_telephone_number as that could be the case
									if(isset($messageprops[$this->userDetailProperties['business2_telephone_number']])){
										$props['business2_telephone_number'] = $messageprops[$this->userDetailProperties['business2_telephone_number']];
									}
								}
							}

							// Get the properties of the "direct reports"
							$directReportsList = $this->getDirectReportsDetails($messageprops);
							if(count($directReportsList) > 0){
								// Add the list of proxy_addresses in the correct format to the $props list to be send to the client.
								$props['ems_ab_reports'] = Array('ems_ab_reports_entry' => $directReportsList);
							}

						// Get the properties for a DISTLIST object and process those DISTLIST specific props that require some more actions
						}else{
							$messageprops = mapi_getprops($abentry, $this->groupDetailProperties);
							$props = Conversion::mapMAPI2XML($this->groupDetailProperties, $messageprops);

							// Get the properties of the owner
							$ownerProps = $this->getOwnerDetails($messageprops);
							if($ownerProps!==false){
								// We can use the same properties as we use for the manager in a MAILUSER
								$props['ems_ab_owner'] = Conversion::mapMAPI2XML($this->abObjectDetailProperties, $ownerProps);
							}

							// Get the list of members for this DISTLSIT
							$props['members'] = Array('member' => $this->getMembersDetails($abentry));
						}

						// Get the proxy addresses list, this property exsists in both MAILUSER and DISTLIST
						$proxyAddresses = $this->getProxyAddressesDetails($messageprops);
						// Remove the MV-flagged property
						if(count($proxyAddresses) > 0){
							// Add the list of proxy_addresses in the correct format to the $props list to be send to the client.
							$props['ems_ab_proxy_addresses'] = Array('ems_ab_proxy_address' => $proxyAddresses);
						}

						// Get the properties of the group membership, this property exsists in both MAILUSER and DISTLIST
						$memberOfList = $this->getMemberOfDetails($messageprops);
						if(count($memberOfList) > 0){
							// Add the list of proxy_addresses in the correct format to the $props list to be send to the client.
							$props['ems_ab_is_member_of_dl'] = Array('ems_ab_is_member_of_dl_entry' => $memberOfList);
						}

						// Remove the MV-flagged properties
						unset($props['home2_telephone_number_mv']);
						unset($props['business2_telephone_number_mv']);
						unset($props['ems_ab_proxy_addresses_mv']);
						unset($props['ems_ab_reports_mv']);
						unset($props['ems_ab_member_mv']);

						// Allowing to hook in and add more properties
						$GLOBALS['PluginManager']->triggerHook("server.module.addressbookitemmodule.open.props", array(
							'moduleObject' =>& $this,
							'abentry' => $abentry,
							'object_type' => $objecttypeprop[PR_OBJECT_TYPE],
							'messageprops' => $messageprops,
							'props' =>& $props
						));

						$data["item"] = $props;

					}else{
						// Handling error: not able to handle this type of object
						$data["attributes"] = array("type" => "error");
						$data["error"] = array();
						$data["error"]["message"] = _("Could not handle this type of object.");
					}
				}else{
					// Handle not being able to open the object
					$data["attributes"] = array("type" => "error");
					$data["error"] = array();
					$data["error"]["hresult"] = mapi_last_hresult();
					$data["error"]["hresult_name"] = get_mapi_error_name(mapi_last_hresult());
					$data["error"]["message"] = _("Could not open this object.");
				}

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);
				
				$result = true;
			}
			
			return $result;
		}

		/**
		 * Get Proxy Addresses in the messageprops array when it is set in the 
		 * PR_EMS_AB_PROXY_ADDRESSES. This property is poorly documented and in Outlook it checks 
		 * the property with and without the MV flag. The one without a MV flag can contain only one
		 * entry and the one with MV flag can contain a list. It then merges both into one list. 
		 * This function has the same behavior and sets the list in the $messageprops.
		 * @param $messageprops Array Details properties of an user entry.
		 * @return Array List of addresses
		 */
		function getProxyAddressesDetails($messageprops)
		{
			$list = Array();
			if(isset($messageprops[$this->userDetailProperties['ems_ab_proxy_addresses']])){
				$list[] = $messageprops[$this->userDetailProperties['ems_ab_proxy_addresses']];
			}
			if(isset($messageprops[$this->userDetailProperties['ems_ab_proxy_addresses_mv']])){
				$list = array_merge($list, $messageprops[$this->userDetailProperties['ems_ab_proxy_addresses_mv']]);
			}
			
			return $list;
		}

		/**
		 * Get the information of the manager from the GAB details of a MAPI_MAILUSER. Will use the 
		 * entryid to get the properties. If no entryid if found false is returned.
		 * @param $messageprops Array Details properties of an user entry.
		 * @return Boolean|Array List of properties or false if no manager is set
		 */
		function getManagerDetails($messageprops)
		{
			if(isset($messageprops[$this->userDetailProperties['ems_ab_manager']])){
				$entryidMananager = $messageprops[$this->userDetailProperties['ems_ab_manager']];

				$managerEntry = mapi_ab_openentry($GLOBALS["mapisession"]->getAddressbook(), $entryidMananager);
				$managerProps = mapi_getprops($managerEntry, $this->abObjectDetailProperties);
				
				return $managerProps;
			}
			return false;
		}

		/**
		 * Get the list of users that have been set in the PR_EMS_AB_REPORTS property in the 
		 * $messageprops array. This property is poorly documented and in Outlook it checks 
		 * the property with and without the MV flag. The one without a MV flag can contain only one
		 * entry and the one with MV flag can contain a list. It then merges both into one list. 
		 * This function has the same behavior and sets the list in the $messageprops.
		 * @param $messageprops Array Details properties of an user entry.
		 * @return Boolean|Array List of properties or false if no manager is set
		 */
		function getDirectReportsDetails($messageprops)
		{
			/*
			 * Get the entryIds from the PR_EMS_AB_REPORTS property (with and without MV flag as a 
			 * fallback) and put the entryIds in a list.
			 */
			$entryids = Array();
			if(isset($messageprops[$this->userDetailProperties['ems_ab_reports']])){
				$entryids[] = $messageprops[$this->userDetailProperties['ems_ab_reports']];
			}
			if(isset($messageprops[$this->userDetailProperties['ems_ab_reports_mv']])){
				$entryids = array_merge($entryids, $messageprops[$this->userDetailProperties['ems_ab_reports_mv']]);
			}

			$result = Array();
			// Convert the entryIds in an array of properties of the AB entryies
			for($i=0;$i<count($entryids);$i++){
				// Get the properies from the AB entry
				$entry = mapi_ab_openentry($GLOBALS["mapisession"]->getAddressbook(), $entryids[$i]);
				$props = mapi_getprops($entry, $this->abObjectDetailProperties);
				// Convert the properties for each entry and put it in an array
				$result[] = Conversion::mapMAPI2XML($this->abObjectDetailProperties, $props);
			}
			return $result;
		}

		/**
		 * Get the list of users that have been set in the PR_EMS_AB_MEMBER_OF_DL property in the 
		 * $messageprops array. This property is poorly documented and in Outlook it checks 
		 * the property with and without the MV flag. The one without a MV flag can contain only one
		 * entry and the one with MV flag can contain a list. It then merges both into one list. 
		 * This function has the same behavior and sets the list in the $messageprops.
		 * @param $messageprops Array Details properties of an user entry.
		 * @return Boolean|Array List of properties or false if no manager is set
		 */
		function getMemberOfDetails($messageprops)
		{
			$result = Array();
			// Get the properties of the group membership
			if(isset($messageprops[$this->userDetailProperties['ems_ab_is_member_of_dl']])){
				$entryids = $messageprops[$this->userDetailProperties['ems_ab_is_member_of_dl']];
				// Get the properties from every entryid in the memberOf list
				for($i=0;$i<count($entryids);$i++){
					// Get the properies from the AB entry
					$entry = mapi_ab_openentry($GLOBALS["mapisession"]->getAddressbook(), $entryids[$i]);
					$props = mapi_getprops($entry, $this->abObjectDetailProperties);
					// Convert the properties for each entry and put it in an array
					$result[] = Conversion::mapMAPI2XML($this->abObjectDetailProperties, $props);
				}
			}
			return $result;
		}

		/**
		 * Get the information of the owner from the GAB details of a MAPI_DISTLIST. Will use the 
		 * entryid to get the properties. If no entryid if found false is returned.
		 * @param $messageprops Array Details properties of an distlist entry.
		 * @return Boolean|Array List of properties or false if no owner is set
		 */
		function getOwnerDetails($messageprops)
		{
			if(isset($messageprops[$this->groupDetailProperties['ems_ab_owner']])){
				$entryidOwner = $messageprops[$this->groupDetailProperties['ems_ab_owner']];

				$ownerEntry = mapi_ab_openentry($GLOBALS["mapisession"]->getAddressbook(), $entryidOwner);
				$ownerProps = mapi_getprops($ownerEntry, $this->abObjectDetailProperties);
				
				return $ownerProps;
			}
			return false;
		}

		/**
		 * Get the information of the members from the GAB details of a MAPI_DISTLIST. The 
		 * information can be found in the contentstable of the AB entry opened by the user. 
		 * @param $abentry Resource Reference to the user-opened AB entry
		 * @return Boolean|Array List of members
		 */
		function getMembersDetails($abentry)
		{
			$result = Array();

			$table = mapi_folder_getcontentstable($abentry, MAPI_DEFERRED_ERRORS);
			$count = mapi_table_getrowcount($table);

			/*
			 * To prevent loading a huge list that the browser cannot handle, it is possible to 
			 * limit the maximum number of shown items.
			 */
			if(ABITEMDETAILS_MAX_NUM_DISTLIST_MEMBERS > 0 && $count > ABITEMDETAILS_MAX_NUM_DISTLIST_MEMBERS){
				$count = ABITEMDETAILS_MAX_NUM_DISTLIST_MEMBERS;
			}

			$rows = mapi_table_queryrows($table, $this->abObjectDetailProperties, 0, $count);
			for($i=0;$i<count($rows);$i++){
				$result[] = Conversion::mapMAPI2XML($this->abObjectDetailProperties, $rows[$i]);
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
			$this->userDetailProperties = $GLOBALS["properties"]->getAddressBookItemMailuserProperties($store);
			$this->abObjectDetailProperties = $GLOBALS["properties"]->getAddressBookItemABObjectProperties($store);
			$this->groupDetailProperties = $GLOBALS["properties"]->getAddressBookItemDistlistProperties($store);
		}
	}
?>