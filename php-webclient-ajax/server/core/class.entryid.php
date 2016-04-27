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
	class EntryId
	{
		/* Bit definitions for abFlags[3] of ENTRYID */
		const KOPANO_FAVORITE = '01';

		/* GUID of root public folder */
		const STATIC_GUID_PUBLICFOLDER = '00000000000000000000000000000003';
		/* GUID of root favorite folder */
		const STATIC_GUID_FAVORITE = '00000000000000000000000000000002';
		/* GUID of ipm_subtree of public store*/
		const STATIC_GUID_FAVSUBTREE = '00000000000000000000000000000001';

		/* GUID for OneOff entryid */
		const MAPI_ONE_OFF_UID = '812B1FA4BEA310199D6E00DD010F5402';

		/**
		 * Default constructor
		 */
		public function EntryId()
		{
		}

		// Detect padding (max 3 bytes) from the entryId
		private function getPadding($entryId)
		{
			$padding = '';
			$offset = 0;


			for ($iterations = 4; $iterations > 0; $iterations--) {
				if (substr($entryId, strlen($entryId) - ($offset + 2), strlen($entryId) - $offset) == '00') {
					$padding += '00';
					$offset += 2;
				} else {
					// if non-null character found then break the loop
					break;
				}
			}

			return $padding;
		}

		// Entryid from version 6
		private function getEIDVersion($entryid)
		{
			// always make entryids in uppercase so comparison will be case insensitive
			$entryId = strtoupper($entryid);

			$res = array(
				'abFlags'	=> '',		// BYTE[4],   4 bytes,  8 hex characters
				'guid'		=> '',		// GUID,     16 bytes, 32 hex characters
				'version'	=> '',		// ULONG,     4 bytes,  8 hex characters
				'type'		=> '',		// ULONG,     4 bytes,  8 hex characters
				'uniqueId'	=> '',		// ULONG,    16 bytes,  32 hex characters
				'server'	=> '',		// CHAR,     variable length
				'padding'	=> '',		// TCHAR[3],  4 bytes,  8 hex characters (upto 4 bytes)
			);

			$res['length'] = strlen($entryId);
			$offset = 0;

			// First determine padding, and remove if from the entryId
			$res['padding'] = $this->getPadding($entryId);
			$entryId = substr($entryId, 0, strlen($entryId) - strlen($res['padding']));

			$res['abFlags'] = substr($entryId, $offset, 8);
			$offset =+ 8;

			$res['guid'] = substr($entryId, $offset, 32);
			$offset += 32;

			$res['version'] = substr($entryId, $offset, 8);
			$offset += 8;

			$res['type'] = substr($entryId, $offset, 8);
			$offset += 8;

			$res['uniqueId'] = substr($entryId, $offset, 32);
			$offset += 32;

			$res['server'] = substr($entryId, $offset);

			$res['min_length'] = 88;
			$res['name'] = 'EID';

			return $res;
		}

		// The entryid from the begin of zarafa till 5.20
		private function getEID_V0Version($entryid)
		{
			// always make entryids in uppercase so comparison will be case insensitive
			$entryId = strtoupper($entryid);

			$res = array(
				'abFlags'	=> '',		// BYTE[4],   4 bytes,  8 hex characters
				'guid'		=> '',		// GUID,     16 bytes, 32 hex characters
				'version'	=> '',		// ULONG,     4 bytes,  8 hex characters
				'type'		=> '',		// ULONG,     4 bytes,  8 hex characters
				'id'		=> '',		// ULONG,     4 bytes,  8 hex characters
				'server'	=> '',		// CHAR,     variable length
				'padding'	=> '',		// TCHAR[3],  4 bytes,  8 hex characters (upto 4 bytes)
			);
	
			$res['length'] = strlen($entryId);
			$offset = 0;

			// First determine padding, and remove if from the entryId
			$res['padding'] = $this->getPadding($entryId);
			$entryId = substr($entryId, 0, strlen($entryId) - strlen($res['padding']));

			$res['abFlags'] = substr($entryId, $offset, 8);
			$offset =+ 8;

			$res['guid'] = substr($entryId, $offset, 32);
			$offset += 32;

			$res['version'] = substr($entryId, $offset, 8);
			$offset += 8;

			$res['type'] = substr($entryId, $offset, 8);
			$offset += 8;

			$res['id'] = substr($entryId, $offset, 8);
			$offset += 8;

			$res['server'] = substr($entryId, $offset);

			$res['min_lenth'] = 64;
			$res['name'] = 'EID_V0';

			return $res;
		}

		// wrapped store entryid
		private function getWrappedSEID($storeEntryId)
		{
			$res = array();

			$res['name'] = 'WrappedSEID';
			$res['length'] = strlen($storeEntryId);

			// always make entryids in uppercase so comparison will be case insensitive
			$storeEntryId = strtoupper($storeEntryId);

			$offset = 0;

			$res['flags'] = substr($storeEntryId, $offset, 8);
			$offset += 8;

			$res['providerUID'] = substr($storeEntryId, $offset, 32);
			$offset += 32;

			$res['version'] = substr($storeEntryId, $offset, 2);
			$offset += 2;

			$res['type'] = substr($storeEntryId, $offset, 2);
			$offset += 2;

			// find length of dll name, find null character which indicates end of dll name after the current offset
			$termCharIndex = strpos(substr($storeEntryId, $offset), '00');
			$res['DLLFileName'] = substr($storeEntryId, $offset, $termCharIndex);
			$offset += $termCharIndex;

			$res['terminationChar'] = substr($storeEntryId, $offset, 2);
			$offset += 2;

			$res['unWrappedEntryId'] = substr($storeEntryId, $offset);

			// unwrapped entryid is actually an object entryid so decompose it
			$res['unWrappedEntryId'] = $this->createEntryIdObj($res['unWrappedEntryId']);

			return $res;
		}

		// Addressbook Entryid
		private function getABEIDVersion($entryid)
		{
			// always make entryids in uppercase so comparison will be case insensitive
			$entryId = strtoupper($entryid);

			$res = array(
				'abFlags'	=> '',		// BYTE[4],   4 bytes,  8 hex characters
				'guid'		=> '',		// GUID,     16 bytes, 32 hex characters
				'version'	=> '',		// ULONG,     4 bytes,  8 hex characters
				'type'		=> '',		// ULONG,     4 bytes,  8 hex characters
				'id'		=> '',		// ULONG,    16 bytes,  32 hex characters
				'extid'		=> '',		// CHAR,     variable length
				'padding'	=> '',		// TCHAR[3],  4 bytes,  8 hex characters (upto 4 bytes)
			);

			$res['length'] = strlen($entryId);
			$offset = 0;

			// First determine padding, and remove if from the entryId
			$res['padding'] = $this->getPadding($entryId);
			$entryId = substr($entryId, 0, strlen($entryId) - strlen($res['padding']));

			$res['abFlags'] = substr($entryId, $offset, 8);
			$offset =+ 8;

			$res['guid'] = substr($entryId, $offset, 32);
			$offset += 32;

			$res['version'] = substr($entryId, $offset, 8);
			$offset += 8;

			$res['type'] = substr($entryId, $offset, 8);
			$offset += 8;

			$res['id'] = substr($entryId, $offset, 8);
			$offset += 8;

			$res['extid'] = substr($entryId, $offset);

			$res['min_length'] = 88;
			$res['name'] = 'EID';

			return $res;
		}

		/**
		 * Creates an object that has split up all the components of an entryID.
		 * @param {String} entryid Entryid
		 * @return {Object} EntryID object
		 */
		private function createEntryIdObj($entryid)
		{
			// check if we are dealing with old or new object entryids
			$versionString = substr($entryid, 40, 8);

			if($versionString == '00000000') {
				// use EID_V0 struct
				$eidObj = $this->getEID_V0Version($entryid);
			} else {
				// use EID struct
				$eidObj = $this->getEIDVersion($entryid);
			}

			return $eidObj;
		}

		/**
		 * Compares two entryIds. It is possible to have two different entryIds that should match as they 
		 * represent the same object (in multiserver environments).
		 * @param {String} entryId1 EntryID
		 * @param {String} entryId2 EntryID
		 * @return {Boolean} Result of the comparison 
		 */
		public function compareEntryIds($entryId1, $entryId2)
		{
			if(!is_string($entryId1) || !is_string($entryId2)) {
				return false;
			}

			if($entryId1 === $entryId2) {
				// if normal comparison succeeds then we can directly say that entryids are same
				return true;
			}

			$eid1 = $this->createEntryIdObj($entryId1);
			$eid2 = $this->createEntryIdObj($entryId2);

			if($eid1['length'] != $eid2['length'])
				return false;

			if($eid1['abFlags'] != $eid2['abFlags'])
				return false;

			if($eid1['version'] != $eid2['version'])
				return false;

			if($eid1['type'] != $eid2['type'])
				return false;

			if($eid1['name'] == 'EID_V0') {
				if($eid1['length'] < $eid1['min_length'])
					return false;

				if($eid1['id'] != $eid2['id'])
					return false;
			} else {
				if($eid1['length'] < $eid1['min_length'])
					return false;

				if($eid1['uniqueId'] != $eid2['uniqueId'])
					return false;
			}

			return true;
		}

		/**
		 * Creates an object that has split up all the components of a store entryid.
		 * @param {String} storeEntryId unwrapped store entryid.
		 * @return {Object} store entryid object.
		 */
		private function createStoreEntryIdObj($storeEntryId)
		{
			return $this->getWrappedSEID($storeEntryId);
		}

		/**
		 * Compares two entryIds. It is possible to have two different entryIds that should match as they 
		 * represent the same object (in multiserver environments).
		 * @param {String} storeEntryId1 store entryid
		 * @param {String} storeEntryId2 store entryid
		 * @return {Boolean} Result of the comparison 
		 */
		public function compareStoreEntryIds($storeEntryId1, $storeEntryId2)
		{
			if(!is_string($storeEntryId1) || !is_string($storeEntryId2)) {
				return false;
			}

			if($storeEntryId1 === $storeEntryId2) {
				// if normal comparison succeeds then we can directly say that entryids are same
				return true;
			}

			$seid1 = $this->createStoreEntryIdObj($storeEntryId1);
			$seid2 = $this->createStoreEntryIdObj($storeEntryId2);

			// we are only interested in unwrapped entryid part
			$seid1 = $seid1['unWrappedEntryId'];
			$seid2 = $seid2['unWrappedEntryId'];

			if($seid1['length'] < $seid1['min_length'] || $seid2['length'] < $seid2['min_length'])
				return false;

			if($seid1['guid'] != $seid2['guid'])
				return false;

			if($seid1['version'] != $seid2['version'])
				return false;

			if($seid1['type'] != $seid2['type'])
				return false;

			if($seid1['name'] == 'EID_V0') {
				if($seid1['length'] < $seid1['min_length'])
					return false;

				if($seid1['id'] != $seid2['id'])
					return false;
			} else {
				if($seid1['length'] < $seid1['min_length'])
					return false;

				if($seid1['uniqueId'] != $seid2['uniqueId'])
					return false;
			}

			return true;
		}

		/**
		 * Creates an object that has split up all the components of an addressbook entryid.
		 * @param {String} abEntryId unwrapped addressbook entryid.
		 * @return {Object} addresbook entryid object.
		 */
		private function createABEntryIdObj($abEntryId)
		{
			return $this->getABEIDVersion($abEntryId);
		}

		/**
		 * Compares two entryIds. It is possible to have two different entryIds that should match as they 
		 * represent the same object (in multiserver environments).
		 * @param {String} entryId1 EntryID
		 * @param {String} entryId2 EntryID
		 * @return {Boolean} Result of the comparison 
		 */
		public function compareABEntryIds($entryId1, $entryId2)
		{
			if(!is_string($entryId1) || !is_string($entryId2)) {
				return false;
			}

			if($entryId1 === $entryId2) {
				// if normal comparison succeeds then we can directly say that entryids are same
				return true;
			}

			$eid1 = $this->createABEntryIdObj($entryId1);
			$eid2 = $this->createABEntryIdObj($entryId2);

			if($eid1['length'] != $eid2['length'])
				return false;

			if($eid1['abFlags'] != $eid2['abFlags'])
				return false;

			if($eid1['version'] != $eid2['version'])
				return false;

			if($eid1['type'] != $eid2['type'])
				return false;

			if($eid1['length'] < $eid1['min_length'])
				return false;

			if($eid1['extid'] != $eid2['extid'])
				return false;

			return true;
		}

		/**
		 * Checks if the passed folder entryid is a folder in the favorites folder, favorites folder
		 * contains 0x01 in the abFlags[3] flag.
		 * @param {String} entryId folder entryid
		 * @return {Boolean} true of folder is a favorite folder else false
		 */
		public function isFavoriteFolder($entryId)
		{
			$entryIdObj = $this->createEntryIdObj($entryId);

			return (substr($entryIdObj['abFlags'], 6, 8) == self::KOPANO_FAVORITE);
		}

		/**
		 * Checks if the given entryid is a oneoff entryid.
		 * @param {String} entryId The entryid
		 * @return {Boolean} true if the entryid is a oneoff
		 */
		public function isOneOffEntryId($entryId)
		{
			$entryIdObj = $this->createEntryIdObj($entryId);

			return ($entryIdObj['guid'] == self::MAPI_ONE_OFF_UID); 
		}

		/**
		 * Checks if the passed folder entryid is root favorites folder.
		 * @param {String} entryId folder entryid
		 * @return {Boolean} true of folder is a root favorite folder else false
		 */
		public function isFavoriteRootFolder($entryId)
		{
			$entryIdObj = $this->createEntryIdObj($entryId);

			return ($entryIdObj['uniqueId'] == self::STATIC_GUID_FAVORITE);
		}

		/**
		 * Checks if the passed folder entryid is root public folder.
		 * @param {String} entryId folder entryid
		 * @return {Boolean} true of folder is a root public folder else false
		 */
		public function isPublicRootFolder($entryId)
		{
			$entryIdObj = $this->createEntryIdObj($entryId);

			return ($entryIdObj['uniqueId'] == self::STATIC_GUID_PUBLICFOLDER);
		}

		/**
		 * Checks if the passed folder entryid is public subtree folder.
		 * @param {String} entryId folder entryid
		 * @return {Boolean} true of folder is a root public folder else false
		 */
		public function isPublicSubtreeFolder($entryId)
		{
			$entryIdObj = $this->createEntryIdObj($entryId);

			return ($entryIdObj['uniqueId'] == self::STATIC_GUID_FAVSUBTREE);
		}
	}
?>
