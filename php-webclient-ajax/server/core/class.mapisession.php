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
     * MAPI session handling
	 *
	 * This class handles MAPI authentication and stores
	 * 
	 * @package core
	 */
	class MAPISession
	{
		/**
		 * @var resource This holds the MAPI Session
		 */
		var $session;

		/**
		 * @var resource This can hold the addressbook resource
		 */
		var $ab;

		/**
		 * @var array List with all the currently opened stores
		 */
		var $stores;
		var $store_entryids;

		/**
		 * @var string The entryid (binary) of the default store
		 */
		var $defaultstore;

		/**
		 * @var array Information about the current session (username/email/password/etc)
		 */
		var $session_info;
		
		/**
		 * @var array Mapping username -> entryid for other stores
		 */
		 
		var $userstores;
		 
		 
		/**
		 * @var int Makes sure retrieveUserData is called only once
		 */
		var $userDataRetrieved;
		
		/**
		 * Default constructor
		 */		 		
		function MAPISession()
		{
			$this->session_info = array("auth"=>false);
			$this->stores = array();
			$this->defaultstore = 0;
			$this->session = false;
			$this->ab = false;
			$this->userstores = array();
			$this->userDataRetrieved = false;
		} 
	
		/**
		 * Logon to Zarafa's MAPI system via php MAPI extension
		 *
		 * Logs on to Zarafa with the specified username and password. If the server is not specified,
		 * it will logon to the local server.
		 *
		 * @param string $username the username of the user
		 * @param string $password the password of the user 
		 * @param string $server the server address
		 * @param string $sslcert_file the optional ssl certificate file
		 * @param string $sslcert_pass the optional ssl certificate password
		 * @result int 0 on no error, otherwise a MAPI error code
		 */		 		
		function logon($username = NULL, $password = NULL, $server = DEFAULT_SERVER, $sslcert_file = NULL, $sslcert_pass = NULL)
		{
			$result = NOERROR;

			if(is_string($username) && is_string($password)) {
				if(function_exists("openssl_decrypt")) {
					// In PHP 5.3.3 the iv parameter was added
					if(version_compare(phpversion(), "5.3.3", "<")) {
						$password = openssl_decrypt($password,"des-ede3-cbc",PASSWORD_KEY,0);
					} else { 
						$password = openssl_decrypt($password,"des-ede3-cbc",PASSWORD_KEY,0,PASSWORD_IV);
					}
				} else if (function_exists("mcrypt_decrypt")) {
					$password = trim(mcrypt_decrypt(MCRYPT_TRIPLEDES, PASSWORD_KEY, base64_decode($password), MCRYPT_MODE_CBC, PASSWORD_IV));
				}
				// logon
				$this->session = mapi_logon_zarafa($username, $password, $server, $sslcert_file, $sslcert_pass);
				$result = mapi_last_hresult();

				if(function_exists("openssl_encrypt")) {
					// In PHP 5.3.3 the iv parameter was added
					if(version_compare(phpversion(), "5.3.3", "<")) {
						$password = openssl_encrypt($password,"des-ede3-cbc",PASSWORD_KEY,0);
					} else {
						$password = openssl_encrypt($password,"des-ede3-cbc",PASSWORD_KEY,0,PASSWORD_IV);
					}
				} else if (function_exists("mcrypt_encrypt")) {
					$password = base64_encode(mcrypt_encrypt(MCRYPT_TRIPLEDES, PASSWORD_KEY, $password, MCRYPT_MODE_CBC, PASSWORD_IV));
				}

				if ($result == NOERROR && $this->session !== false){
					$this->session_info["username"] = $username;
					$this->session_info["password"] = $password;
					$this->session_info["server"] = $server;
					
					// we are authenticated
					$this->session_info["auth"] = true;
				}
			}

			return $result;
		}

		/**
		* Get logged-in user information
		*
		* This function populates the 'session_info' property of this class with the following information:
		* - userentryid: the MAPI entryid of the current user
		* - fullname: the fullname of the current user
		* - emailaddress: the email address of the current user
		*
		* The function only populates the information once, subsequent calls will return without error and without
		* doing anything.
		*
		* @return array Array of information about the currently logged-on user
		* @access private
		*/
		function retrieveUserData()
		{
			if($this->userDataRetrieved)
				return;
				
			$result = NOERROR;
			
			// get user entryid
			$store_props = mapi_getprops($this->getDefaultMessageStore(), array(PR_USER_ENTRYID));
			$result = mapi_last_hresult();
						
			if ($result == NOERROR){
				// open the user entry
				$user = mapi_ab_openentry($this->getAddressbook(), $store_props[PR_USER_ENTRYID]);
				$result = mapi_last_hresult();
			}
			
			if ($result == NOERROR){
				// receive userdata
				$user_props = mapi_getprops($user, array(PR_DISPLAY_NAME, PR_SMTP_ADDRESS));
				$result = mapi_last_hresult();
			}

			if ($result == NOERROR && is_array($user_props) && isset($user_props[PR_DISPLAY_NAME]) && isset($user_props[PR_SMTP_ADDRESS])){
				$this->session_info["userentryid"] = $store_props[PR_USER_ENTRYID];
				$this->session_info["fullname"] = $user_props[PR_DISPLAY_NAME];
				$this->session_info["emailaddress"] = $user_props[PR_SMTP_ADDRESS];
			}
			
			$this->userDataRetrieved = true;

			return $result;
		}

		/**
		 * Get MAPI session object
		 *
		 * @return mapisession Current MAPI session
		 */
		function getSession()
		{
			return $this->session;
		}

		/**
		 * Get MAPI addressbook object
		 *
		 * @return mapiaddressbook An addressbook object to be used with mapi_ab_*
		 */
		function getAddressbook()
		{
			$result = NOERROR;
			if ($this->ab === false){
				$this->ab = mapi_openaddressbook($this->session);
				$result = mapi_last_hresult();
			}
			
			if ($result == NOERROR && $this->ab !== false){
				$result = $this->ab;
			}
			return $result;
		}


		/**
		 * Get logon status
		 *
		 * @return boolean true on logged on, false on not logged on
		 */		 		
		function isLoggedOn()
		{
			return array_key_exists("auth",$this->session_info)?$this->session_info["auth"]:false;
		}
		
		/**
		 * Get current user entryid
		 * @return string Current user's entryid
		 */
		function getUserEntryID()
		{
			$result = $this->retrieveUserData(); 
			
			return array_key_exists("userentryid",$this->session_info)?$this->session_info["userentryid"]:false;
		}

		/**
		 * Get current username
		 * @return string Current user's username (equal to username passed in logon() )
		 */
		function getUserName()
		{
			$result = $this->retrieveUserData(); 
			
			return array_key_exists("username",$this->session_info)?$this->session_info["username"]:false;
		}

		/**
		 * Get current user's full name
		 * @return string User's full name
		 */
		function getFullName()
		{
			$result = $this->retrieveUserData(); 
			
			return array_key_exists("fullname",$this->session_info)?$this->session_info["fullname"]:false;
		}

		/**
		 * Get current user's email address
		 * @return string User's email address
		 */
		function getEmail()
		{
			$result = $this->retrieveUserData(); 
			
			return array_key_exists("emailaddress",$this->session_info)?$this->session_info["emailaddress"]:false;
		}


		/**
		 * Get the current user's default message store
		 *
		 * The store is opened only once, subsequent calls will return the previous store object
		 * @return mapistore User's default message store object
		 */
		function getDefaultMessageStore()
		{
			// Return cached default store if we have one
			if(isset($this->defaultstore) && isset($this->stores[$this->defaultstore])) {
				return $this->stores[$this->defaultstore];
			}
			
			// Find the default store
			$storestables = mapi_getmsgstorestable($this->session);
			$result = mapi_last_hresult();
			$entryid = false;
			
			if ($result == NOERROR){
				$rows = mapi_table_queryallrows($storestables, array(PR_ENTRYID, PR_DEFAULT_STORE, PR_MDB_PROVIDER));
				$result = mapi_last_hresult();
				
				foreach($rows as $row) {
					if(isset($row[PR_DEFAULT_STORE]) && $row[PR_DEFAULT_STORE] == true) {
						$entryid = $row[PR_ENTRYID];
					}
				}
			}

			if($entryid)
				return $this->openMessageStore($entryid);
			else
				return false;
		}
	
		/**
		 * Get all message stores currently open in the session
		 *
		 * @return array Associative array with entryid -> mapistore of all open stores (private, public, delegate)
		 */
		function getAllMessageStores()
		{
			
			// Loop through all stores
			$storestables = mapi_getmsgstorestable($this->session);
			$result = mapi_last_hresult();
			$entryid = false;
			$this->getArchivedStores($this->getUserEntryID());
			
			if ($result == NOERROR){
				$rows = mapi_table_queryallrows($storestables, array(PR_ENTRYID));
				$result = mapi_last_hresult();
	
				if($result == NOERROR) {				
					foreach($rows as $row) {
						$this->openMessageStore($row[PR_ENTRYID]);
					}
				}
			}
			
			// The cache now contains all the stores in our profile. Next, add the stores
			// for other users.

			$otherusers = $this->retrieveOtherUsersFromSettings();
			if(is_array($otherusers)) {
				foreach($otherusers as $username=>$folder) {
				    if(is_array($folder) && count($folder) > 0) {
                        $user_entryid = mapi_msgstore_createentryid($this->getDefaultMessageStore(), $username);
                        $hresult = mapi_last_hresult();
                    
                        if($hresult == NOERROR) {
                            $this->userstores[$username] = $user_entryid;
                            $this->openMessageStore($user_entryid);
							// Check if an entire store will be loaded, if so load the archive store as well
							if(isset($folder['all']) && $folder['all']['type'] == 'all'){
								$this->getArchivedStores($this->resolveStrictUserName($username));
							}
                        }
                    }
				}
			}	
			// Just return all the stores in our cache
			return $this->stores;
		}

		/**
		 * Open the message store with entryid $entryid
		 *
		 * @return mapistore The opened store on success, false otherwise
		 */
		function openMessageStore($entryid)
		{
			// Check the cache before opening
			if(isset($this->stores[$entryid])) 
				return $this->stores[$entryid];
							
			$store = mapi_openmsgstore($this->session, $entryid);
			$hresult = mapi_last_hresult();
			
			// Cache the store for later use
			if($hresult == NOERROR) 
				$this->stores[$entryid] = $store;
			
			return $store;
		}

		/**
		 * Searches for the PR_EC_ARCHIVE_SERVERS property of the user of the passed entryid in the 
		 * Addressbook. It will get all his archive store objects and add those to the $this->stores
		 * list. It will return an array with the list of archive stores where the key is the 
		 * entryid of the store and the value the store resource.
		 * @param String $userEntryid Binary entryid of the user
		 * @return MAPIStore[] List of store resources with the key being the entryid of the store
		 */
		function getArchivedStores($userEntryid)
		{
			$ab = $this->getAddressbook();
			$abitem = mapi_ab_openentry($ab, $userEntryid);
			$userData = mapi_getprops($abitem, Array(PR_ACCOUNT, PR_EC_ARCHIVE_SERVERS));

			// Get the store of the user, need this for the call to mapi_msgstore_getarchiveentryid()
			$userStoreEntryid = mapi_msgstore_createentryid($this->getDefaultMessageStore(), $userData[PR_ACCOUNT]);
			$userStore = mapi_openmsgstore($GLOBALS['mapisession']->getSession(), $userStoreEntryid);
			$storeResult = mapi_last_hresult();

			$archiveStores = Array();
			if($storeResult === NOERROR && isset($userData[PR_EC_ARCHIVE_SERVERS]) && count($userData[PR_EC_ARCHIVE_SERVERS]) > 0){
				for($i=0;$i<count($userData[PR_EC_ARCHIVE_SERVERS]);$i++){
					$archiveStoreEntryid = mapi_msgstore_getarchiveentryid($userStore, $userData[PR_ACCOUNT], $userData[PR_EC_ARCHIVE_SERVERS][$i]);
					// Check if the store exists. It can be that the store archiving has been enabled, but no 
					// archived store has been created an none can be found in the PR_EC_ARCHIVE_SERVERS property.
					if(mapi_last_hresult() === NOERROR){
						$archiveStores[$archiveStoreEntryid] = mapi_openmsgstore($GLOBALS['mapisession']->getSession(), $archiveStoreEntryid);
						// Add the archive store to the list
						$this->stores[$archiveStoreEntryid] = $archiveStores[$archiveStoreEntryid];
					}
				}
			}
			return $archiveStores;
		}

		/**
		 * Resolve a username to its entryid
		 *
		 * @return string Entryid of the user on success, false otherwise
		 */
		function resolveUserName($username)
		{
			$request = array(array(PR_DISPLAY_NAME => $username));
			$ret = mapi_ab_resolvename($this->getAddressbook(), $request);
			$result = mapi_last_hresult();
			if ($result == NOERROR){
				$result = $ret[0][PR_ENTRYID];
			}
			return $result;
		}

		/**
		 * Resolve the username strictly by opening that user's store and returning the 
		 * PR_MAILBOX_OWNER_ENTRYID. This can be used for resolving an username without the risk of 
		 * ambiguity since mapi_ab_resolve() does not strictly resolve on the username.
		 * It is a hackish solution, but it is the only one that works.
		 * @param String $username The username
		 * @return Binary|Integer Entryid of the user on success otherwise the hresult error code
		 */
		function resolveStrictUserName($username)
		{
			$storeEntryid = mapi_msgstore_createentryid($this->getDefaultMessageStore(), $username);
			$store = mapi_openmsgstore($this->getSession(), $storeEntryid);
			$storeProps = mapi_getprops($store, Array(PR_MAILBOX_OWNER_ENTRYID));
			$result = mapi_last_hresult();
			if ($result == NOERROR){
				$result = $storeProps[PR_MAILBOX_OWNER_ENTRYID];
			}
			return $result;
		}

		/**
		 * Get other users from settings
		 *
		 * @return array Array of usernames of delegate stores
		 */
		function retrieveOtherUsersFromSettings()
		{
			$result = false;
			$other_users = $GLOBALS["settings"]->get("otherstores",null);

			if (is_array($other_users)){
			    $result = Array();
                // Due to a previous bug you were able to open folders from both user_a and USER_A
                // so we have to filter that here. We do that by making everything lower-case
                foreach($other_users as $username=>$folders) {
                    $username = strtolower($username);
                    if(!isset($result[$username]))
                        $result[$username] = Array();

					if(!isset($folders) || count($folders) <= 0)
						continue;	// zero folders (why? maybe the store was closed by user that's why it is not in settings)

					foreach($folders as $type => $folder) {
						if(is_array($folder)) {
							$result[$username][$folder["type"]] = Array();
                        	$result[$username][$folder["type"]]["type"] = $folder["type"];
                        	$result[$username][$folder["type"]]["show_subfolders"] = $folder["show_subfolders"];
						} else {
							// older settings
							$result[$username][$folder] = Array();
							$result[$username][$folder]["type"] = $type;
							$result[$username][$folder]["show_subfolders"] = "false";
						}
                    }
                }

				// Delete first so that we do a full replace instead of update
                $GLOBALS["settings"]->delete("otherstores");
                $GLOBALS["settings"]->set("otherstores", $result);
			}
			return $result;
		}

		/**
		 * Add the store of another user to the list of other user stores
		 *
		 * @param string $username The username whose store should be added to the list of other users' stores
		 * @return mapistore The store of the user or false on error;
		 */
		function addUserStore($username)
		{
			$user_entryid = mapi_msgstore_createentryid($this->getDefaultMessageStore(), u2w($username));
			
			if($user_entryid) {
				$this->userstores[$username] = $user_entryid;
			
				return $this->openMessageStore($user_entryid);
			} else
				return false;
		}
		
		/**
		 * Remove the store of another user from the list of other user stores
		 *
		 * @param string $username The username whose store should be deleted from the list of other users' stores
		 */
		function removeUserStore($username)
		{
			// Remove the reference to the store if we had one
			if (isset($this->userstores[$username])){
				unset($this->stores[$this->userstores[$username]]);
			}
		}	
		
		/**
		 * Get the username of the owner of the specified store
		 *
		 * The store must have been previously added via addUserStores.
		 *
		 * @param string $entryid EntryID of the store
		 * @return string Username of the specified store or false if it is not found
		 */
		function getUserNameOfStore($entryid)
		{
			foreach($this->userstores as $username => $storeentryid) {
				if($storeentryid == $entryid)
					return $username;
			}
			
			return false;
		}
	} 	
?>
