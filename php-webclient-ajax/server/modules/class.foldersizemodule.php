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
	class FolderSizeModule extends Module
	{
		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param string $folderentryid Entryid of the folder. Data will be selected from this folder.
		 * @param array $data list of all actions.
		 */
		function FolderSizeModule($id, $data)
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
						case "foldersize":
							$subfolderdata = array();
							$store = $GLOBALS["mapisession"]->openMessageStore(hex2bin($action["store"]));
							$storeprops = mapi_getprops($store, array(PR_IPM_SUBTREE_ENTRYID, PR_DISPLAY_NAME));
							
							$folder = mapi_msgstore_openentry($store, hex2bin($action["entryid"]));
							$folderprops = mapi_getprops($folder, array(PR_ENTRYID, PR_DISPLAY_NAME));
							
							if ($folderprops[PR_ENTRYID] == $storeprops[PR_IPM_SUBTREE_ENTRYID]) {
								$foldername = w2u($storeprops[PR_DISPLAY_NAME]);
							} else {
								$foldername = w2u($folderprops[PR_DISPLAY_NAME]);
							}

							// Get the folder size
							$sizemainfolder = $GLOBALS["operations"]->calcFolderMessageSize($folder, false)/1024;
							
							// Get the subfolders in depth
							$subfoldersize = $this->getFolderSize($store, $folder, null, $subfolderdata);
							
							// Sort the folder names
							usort($subfolderdata, array("FolderSizeModule", "cmp_sortfoldername"));
							
							$data = array( 
										"mainfolder" => array(
													"name" => $foldername,
													"size" => round($sizemainfolder),
													"totalsize" => round($sizemainfolder + $subfoldersize)),
										"subfolders" => array("folder" => $subfolderdata) );
							
							// return response
							$data["attributes"] = array("type"=>"foldersize");
							array_push($this->responseData["action"], $data);
							$GLOBALS["bus"]->addData($this->responseData);
							$result = true;
							break;

					}
				}
			}
			
			return $result;
		}

		/**
		* Calculate recursive folder sizes with the folder name and subfolder sizes
		*
		* @param object $store store of the selected folder
		* @param object $folder selected folder 
		* @param string $parentfoldernames recursive the folder names seperated with a slash
		* @param array $subfolderdata array with folder information
		* @return integer total size of subfolders 
		*/
		function getFolderSize($store, $folder, $parentfoldernames, &$subfolderdata)
		{
			$totalsize = 0;
			
			$hierarchyTable = mapi_folder_gethierarchytable($folder);
			if (mapi_table_getrowcount($hierarchyTable) == 0) {
				return $totalsize;
			}
			
			mapi_table_sort($hierarchyTable, array(PR_DISPLAY_NAME => TABLE_SORT_ASCEND));

			$rows = mapi_table_queryallrows($hierarchyTable, Array(PR_ENTRYID, PR_SUBFOLDERS, PR_DISPLAY_NAME));
			foreach($rows as $subfolder)
			{
				$foldernames = (($parentfoldernames==null)?"":"$parentfoldernames \\ ") .w2u($subfolder[PR_DISPLAY_NAME]);
				$subfoldersize = 0;

				$folderObject = mapi_msgstore_openentry($store, $subfolder[PR_ENTRYID]);
				if ($folderObject) {
					$foldersize = $GLOBALS["operations"]->calcFolderMessageSize($folderObject, false)/1024;

					if($subfolder[PR_SUBFOLDERS]) {
						$subfoldersize = $this->getFolderSize($store, $folderObject, $foldernames, $subfolderdata);
					}
				}
				$subfolderdata[] = array(
									"name" => $foldernames,
									"size" => round($foldersize),
									"totalsize" => round($subfoldersize + $foldersize));
									
				$totalsize += $subfoldersize + $foldersize;
			}
			
			return $totalsize;
		}

		/**
		 * Sort the folder names
		 */
		function cmp_sortfoldername($a, $b)
		{
			return strnatcmp($a["name"], $b["name"]);
		}

	}
?>
