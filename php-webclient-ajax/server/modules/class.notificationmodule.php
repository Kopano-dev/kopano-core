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
	* Notification Module
	* 
	* Until we have real MAPI Notification support we just check
	* here the PR_CONTENT_COUNT and PR_CONTENT_UNREAD from the inbox
	*
	*/
	class NotificationModule extends Module
	{
		var $inboxProps;
		var $quotaProps;
		var $lastCheck;
		var $firstTime;
		var $hasData;


		/**
		* Constructor
		* @param int $id unique id.
		* @param array $data list of all actions.
		*/
		function NotificationModule($id, $data)
		{
			$this->firstTime = true;

			parent::Module($id, $data, array());

			// until we have real notification support, we only check the inbox here
			$inbox = mapi_msgstore_getreceivefolder($GLOBALS["mapisession"]->getDefaultMessageStore());
			$this->inboxProps = mapi_getprops($inbox, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_CONTENT_COUNT, PR_CONTENT_UNREAD));
			$this->quotaProps = array(PR_QUOTA_WARNING_THRESHOLD=>0,PR_QUOTA_SEND_THRESHOLD=>0,PR_QUOTA_RECEIVE_THRESHOLD=>0,PR_MESSAGE_SIZE_EXTENDED=>0);
			
			$register = true;
			foreach($data as $action){
				if(isset($action["attributes"]) && isset($action["attributes"]["type"]) && $action["attributes"]["type"]=="getcounters") {
					// this instance will only be used to get the current counters, so we don't need to register
					$register = false;
				}
			}
			
			if ($register){
				// register for internal notifications
				$GLOBALS["bus"]->register($this, bin2hex($this->inboxProps[PR_ENTRYID]), array(OBJECT_SAVE, OBJECT_DELETE, TABLE_SAVE, TABLE_DELETE), false);
				// register for request start/end
				$GLOBALS["bus"]->register($this, REQUEST_ENTRYID, array(REQUEST_START,REQUEST_END));
			}
		}
		
		function execute()
		{
			$result = false;
			foreach($this->data as $action)
			{
				if(isset($action["attributes"]) && isset($action["attributes"]["type"])) {
					switch($action["attributes"]["type"])
					{
						case "getcounters":
							$inbox = mapi_msgstore_getreceivefolder($GLOBALS["mapisession"]->getDefaultMessageStore());
							$props = mapi_getprops($inbox, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_CONTENT_COUNT, PR_CONTENT_UNREAD));

							$data = array();
							$data["attributes"] = array("type" => "update");
							$data["count"] = $props[PR_CONTENT_COUNT];
							$data["unread"] = $props[PR_CONTENT_UNREAD];

							$quotaDetails = $GLOBALS["operations"]->getQuotaDetails($store);
							$data["store_size"] = $quotaDetails["store_size"];
							$data["quota_warning"] = $quotaDetails["quota_warning"];
							$data["quota_soft"] = $quotaDetails["quota_soft"];
							$data["quota_hard"] = $quotaDetails["quota_hard"];
							
							array_push($this->responseData["action"], $data);
							$GLOBALS["bus"]->addData($this->responseData);
							$result = true;
							break;
					}
				}
			}
			
			return $result;
		}

		function getNewMail($inbox)
		{
			$result = false;
			
			$inboxprops = mapi_getprops($inbox, array(PR_CONTENT_UNREAD));
			
			if(isset($this->lastUnread)) {
			    if($this->lastUnread < $inboxprops[PR_CONTENT_UNREAD])
			        $result = true;
			}
			
			$this->lastUnread = $inboxprops[PR_CONTENT_UNREAD];
			return $result;
		}

		/**
		 * If an event elsewhere has occurred, it enters in this methode. This method
		 * executes one ore more actions, depends on the event.
		 * @param int $event Event.
		 * @param string $entryid Entryid.
		 * @param array $data array of data.
		 */
		function update($event, $entryid, $data)
		{
			$inbox = mapi_msgstore_getreceivefolder($GLOBALS["mapisession"]->getDefaultMessageStore());

			switch($event){
				case REQUEST_START:
					$this->reset();
					$this->hasData = false;

					$newmail = $this->getNewMail($inbox);
					if ($newmail!=false){
						$inboxProps = mapi_getprops($inbox, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_CONTENT_COUNT, PR_CONTENT_UNREAD));

						$data = array();
						$data["attributes"] = array();
						$data["attributes"]["type"] = "newmail";
						$data["attributes"]["content_count"] = $inboxProps[PR_CONTENT_COUNT];
						$data["attributes"]["content_unread"] = $inboxProps[PR_CONTENT_UNREAD];

						$data["item"] = $newmail;
						
						// Add entryid of folder to XML data in order to refreah the list.
						$data["parent_entryid"] = bin2hex($inboxProps[PR_ENTRYID]);
						
						array_push($this->responseData["action"], $data);

						$GLOBALS["bus"]->notify(bin2hex($inboxProps[PR_ENTRYID]), TABLE_SAVE, $inboxProps);
						$this->hasData = true;
					}
					break;
				case REQUEST_END:
					$data = array();
					$data["attributes"] = array("type" => "update");

                    // Update unread counter now, so that any changes that we have done ourselved (for example
                    // moving an unread e-mail into the inbox) will not be detected by the newMail detection in
                    // REQUEST_START. We're not interested in the return value though.
					$this->getNewMail($inbox);

					$isChanged = false;
					$inboxProps = mapi_getprops($inbox, array(PR_ENTRYID, PR_STORE_ENTRYID, PR_CONTENT_COUNT, PR_CONTENT_UNREAD));

					// only when counters are changed we will send the counters, but also ofcourse the first time we are called
					if ($this->firstTime || $inboxProps[PR_CONTENT_COUNT]!=$this->inboxProps[PR_CONTENT_COUNT] || $inboxProps[PR_CONTENT_UNREAD]!=$this->inboxProps[PR_CONTENT_UNREAD]){
						$data["count"] = $inboxProps[PR_CONTENT_COUNT];
						$data["unread"] = $inboxProps[PR_CONTENT_UNREAD];
						$this->inboxProps = $inboxProps;
						$isChanged = true;
					}

					$quotaProps = mapi_getprops($GLOBALS["mapisession"]->getDefaultMessageStore(),array(PR_QUOTA_WARNING_THRESHOLD,PR_QUOTA_SEND_THRESHOLD,PR_QUOTA_RECEIVE_THRESHOLD,PR_MESSAGE_SIZE_EXTENDED));
					if ($this->firstTime || $quotaProps[PR_MESSAGE_SIZE_EXTENDED]!=$this->quotaProps[PR_MESSAGE_SIZE_EXTENDED]){
						$data["store_size"] = round($quotaProps[PR_MESSAGE_SIZE_EXTENDED]/1024);
						$data["quota_warning"] = $quotaProps[PR_QUOTA_WARNING_THRESHOLD];
						$data["quota_soft"] = $quotaProps[PR_QUOTA_SEND_THRESHOLD];
						$data["quota_hard"] = $quotaProps[PR_QUOTA_RECEIVE_THRESHOLD];
						$this->quotaProps = $quotaProps;
						$isChanged = true;
					}
					
					if ($isChanged){
						array_push($this->responseData["action"], $data);
						$this->hasData = true;
					}

					if ($this->hasData){
						$GLOBALS["bus"]->addData($this->responseData);
						$this->hasData = false;
					}
					$this->firstTime = false;
					break;	

				default:
					// if we get here, we have an internal notification
					// todo: check if we changed something to the inbox
			}
		}
	}
?>
