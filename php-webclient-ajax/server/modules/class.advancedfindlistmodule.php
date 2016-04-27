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
	class AdvancedFindListModule extends ListModule
	{
		/**
		 * Constructor
		 * @param		int			$id			unique id.
		 * @param		array		$data		list of all actions.
		 */
		function AdvancedFindListModule($id, $data)
		{
			parent::ListModule($id, $data, array(OBJECT_SAVE, TABLE_SAVE, TABLE_DELETE));

			$this->sort = array();
		}

		/**
		 * Executes all the actions in the $data variable.
		 * @return		boolean					true on success or false on failure.
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
						case "search":
							// get properties and column info
							$this->getColumnsAndPropertiesForMessageType($store, $entryid, $action);
							$result = $this->search($store, $entryid, $action);
							break;
						case "updatesearch":
							$result = $this->updatesearch($store, $entryid, $action);
							break;
						case "stopsearch":
							$result = $this->stopSearch($store, $entryid, $action);
							break;
						case "delete":
							$result = $this->delete($store, $parententryid, $entryid, $action);
							break;
						case "delete_searchfolder":
							$result = $this->deleteSearchFolder($store, $entryid, $action);
							break;
						case "save":
							$result = $this->save($store, $parententryid, $action);
							break;
					}
				}
			}

			return $result;
		}

		/**
		 *	Function will create search restriction based on restriction array
		 *	@param		Array				$action		the action data, sent by the client
		 *	@return		restrictionObject				restriction array which will be used for searching
		 */
		function parseSearchRestriction($action)
		{
			if(isset($action["restriction"])) {
				if(isset($action["restriction"]["start"])) {
					// Set start variable
					$this->start = (int) $action["restriction"]["start"];
				}

				if(isset($action["restriction"]["search"]) && $action["restriction"]["search"]["attributes"]["type"] == "json") {
					if(function_exists("json_decode")) {
						$restriction = json_decode($action["restriction"]["search"]["_content"], true);

						$errorInDecoding = false;
						/*if(function_exists("json_last_error")) {
							if(json_last_error($restriction)) {
								// error in decoding json data
								$errorInfo = array();
								$errorInfo["error_message"] = _("Error in decoding JSON data") . ".";
								$errorInfo["original_error_message"] = "Error in decoding JSON data.";
								$this->searchRestriction = false;
								$errorInDecoding = true;

								$this->sendSearchErrorToClient($store, $entryid, $action, $errorInfo);
							}
						}*/

						if(isset($restriction) && !$errorInDecoding) {
							$this->searchRestriction = Conversion::json2restriction($restriction);
						}
					} else {
						// no JSON extension installed
						$errorInfo = array();
						$errorInfo["error_message"] = _("The JSON extention for PHP is required to use this feature. Please inform your system administrator.") . ".";
						$errorInfo["original_error_message"] = "JSON extension for PHP is not installed.";
						$this->searchRestriction = false;

						$this->sendSearchErrorToClient($store, $entryid, $action, $errorInfo);
					}
				}
			}
		}

		/**
		 * Function will set properties and table columns for particular message class
		 * @param		Object		$store		MAPI Message Store Object
		 * @param		HexString	$entryid	entryid of the folder
		 * @param		Array		$action		the action data, sent by the client
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

						// enable columns that are by default disabled, this can be overriden by user settings
						$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "message_delivery_time", "visible", true);
						break;
				}

				// List columns visible based on user settings
				if(is_array($entryid)) {
					$GLOBALS["TableColumns"]->parseVisibleColumnsFromSettings($this->tablecolumns, bin2hex($entryid[0]));
				} else {
					$GLOBALS["TableColumns"]->parseVisibleColumnsFromSettings($this->tablecolumns, bin2hex($entryid));
				}

				// default columns that will be shown while ignoring user settings
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "sent_representing_name", "visible", true);
				$GLOBALS["TableColumns"]->changeColumnPropertyValue($this->tablecolumns, "display_to", "visible", true);

				// add folder name column
				if($GLOBALS["TableColumns"]->getColumn($this->tablecolumns, "parent_entryid") === false) {
					$GLOBALS["TableColumns"]->addColumn($this->tablecolumns, "parent_entryid", true, 6, _("In Folder"), _("Sort Folder"), 90, "folder_name");
				}
			}
		}

		/**
		 * Function will delete search folder
		 * @param		object			$store		MAPI Message Store Object
		 * @param		hexString		$entryid	entryid of the folder
		 * @param		array			$action		the action data, sent by the client
		 * @return		boolean						true on success or false on failure
		 */
		function deleteSearchFolder($store, $entryid, $action)
		{
			if($entryid && $store) {
				$storeProps = mapi_getprops($store, array(PR_FINDER_ENTRYID));

				$finderFolder = mapi_msgstore_openentry($store, $storeProps[PR_FINDER_ENTRYID]);

				if(mapi_last_hresult() != NOERROR){
					return;
				}

				$hierarchyTable = mapi_folder_gethierarchytable($finderFolder);

				$restriction = array(RES_CONTENT,
										array(
											FUZZYLEVEL	=> FL_FULLSTRING,
											ULPROPTAG	=> PR_ENTRYID,
											VALUE		=> array(PR_ENTRYID => $entryid)
											)
									);

				mapi_table_restrict($hierarchyTable, $restriction, TBL_BATCH);

				// entryids are unique so there would be only one matching row, 
				// so only fetch first row
				$folders = mapi_table_queryrows($hierarchyTable, array(PR_ENTRYID), 0, 1);

				// delete search folder
				if(is_array($folders) && is_array($folders[0])) {
					mapi_folder_deletefolder($finderFolder, $folders[0][PR_ENTRYID]);
				}

				// reset variables
				$this->searchFolderEntryId = false;
				$this->searchCriteriaCheck = false;

				return true;
			}

			return false;
		}
	}
?>