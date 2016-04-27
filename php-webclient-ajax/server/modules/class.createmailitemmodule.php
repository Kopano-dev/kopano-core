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
	 * Create Mail ItemModule
	 * Module which openes, creates, saves and deletes an item. It
	 * extends the Module class.
	 */
	class CreateMailItemModule extends ItemModule
	{
		/**
		 * @var Array properties of mail item that will be used to get data
		 */
		var $properties = null;

		/**
		 * Constructor
		 * @param int $id unique id.
		 * @param array $data list of all actions.
		 */
		function CreateMailItemModule($id, $data)
		{
			parent::ItemModule($id, $data);
		}

		/**
		 * Function which opens a draft. Ite opens the body in plain text.
		 * @param object $store MAPI Message Store Object
		 * @param string $entryid entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function open($store, $entryid, $action)
		{
			$result = false;

			if($store && $entryid) {
				$convertToPlainFormat = true;
				if($GLOBALS['settings']->get('createmail/mailformat',"html") == 'html' && FCKEDITOR_INSTALLED){
					$convertToPlainFormat = false;
				}

				$data = array();
				$data["attributes"] = array("type" => "item");

				$attachNum = $this->getAttachNum($action);
				$message = $GLOBALS["operations"]->openMessage($store, $entryid, $attachNum);

				// Decode smime signed messages on this message
 				parse_smime($store, $message);

				if(!$attachNum){
					// Get the standard properties
					$data["item"] = $GLOBALS["operations"]->getMessageProps($store, $message, $this->properties, $convertToPlainFormat);
				}else{
					// Get the sub-message properties
					$data["item"] = $GLOBALS["operations"]->getEmbeddedMessageProps($store, $message, $this->properties, $entryid, $attachNum);
				}

				array_push($this->responseData["action"], $data);
				$GLOBALS["bus"]->addData($this->responseData);

				$result = true;
			}

			return $result;
		}

		/**
		 * Function which saves and/or sends an item.
		 * @param object $store MAPI Message Store Object
		 * @param string $parententryid parent entryid of the message
		 * @param array $action the action data, sent by the client
		 * @return boolean true on success or false on failure
		 */
		function save($store, $parententryid, $action)
		{
			global $state;

			if(!$parententryid && isset($action["props"]["parent_entryid"])) {
				$parententryid = $action["props"]["parent_entryid"];
			}

			$result = false;

			if(!$store) {
				$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
			}

			// Get inline information.
			$add_inline_attachments = array();
			$delete_inline_attachments = array();
			if (isset($action["inline_attachments"]) && isset($action["inline_attachments"]["images"]) && is_array($action["inline_attachments"]["images"])) {
				// Inline images which are to be added.
				if(is_array($action["inline_attachments"]["images"]["add"])){
					if (!isset($action["inline_attachments"]["images"]["add"][0])){
						$add_inline_attachments = array($action["inline_attachments"]["images"]["add"]);
					}else{
						$add_inline_attachments = $action["inline_attachments"]["images"]["add"];
					}
				}

				// Inline images which are to be deleted.
				if(is_array($action["inline_attachments"]["images"]["delete"])){
					if (!isset($action["inline_attachments"]["images"]["delete"][0])){
						$delete_inline_attachments = array($action["inline_attachments"]["images"]["delete"]);
					}else{
						$delete_inline_attachments = $action["inline_attachments"]["images"]["delete"];
					}
				}
			}

			if($store) {
				if(isset($action["props"]) && isset($action["recipients"])){
					/**
					 * When the no recipients are added the transformation from
					 * XML to an array does not add the empty recipient subarray.
					 */
					if(!isset($action["recipients"]["recipient"])) {
						$action["recipients"]["recipient"] = Array();
					}
					$messageProps = array();

					$send = false;
					if(isset($action["send"])) {
						$send = $action["send"];
					}

					$copyAttachments = false;
					$copyAttachmentsStore = false;
					$copy_only_inline_attachments = false;
					$replyOrigMessageDetails = false;
					$send_as_onbehalf = true;
					if(isset($action["message_action"]) && isset($action["message_action"]["action_type"])) {
						/** we need to copy the original attachments when it is an forwarded message, or when editing a already sent message
						 *  OR
						 *  we need to copy ONLY original inline(HIDDEN) attachments when it is reply/replyall message
						 */
						$copyAttachments = hex2bin($action["message_action"]["entryid"]);
						$copyAttachmentsStore = hex2bin($action["message_action"]["storeid"]);
						// Open store (this => $GLOBALS['mapisession'])
						$copyAttachmentsStore = $GLOBALS['mapisession']->openMessageStore($copyAttachmentsStore);

						if ($action["message_action"]["action_type"] == "reply" || $action["message_action"]["action_type"] == "replyall") {
							//if user replies using a plain text format do not display/add the inline attachments to the item
							if(isset($action["props"]["use_html"]) && $action["props"]["use_html"] == "true"){
								$copy_only_inline_attachments = true;
								$replyOrigMessageDetails = array(
									"entryid"=>$action["message_action"]["entryid"],
									"storeid"=>$action["message_action"]["storeid"]
								);
							} else {
								$copyAttachments = false;
								$copyAttachmentsStore = false;
							}
						}
					}


					if($send) {
						$prop = Conversion::mapXML2MAPI($this->properties, $action["props"]);
						$session = $GLOBALS["mapisession"]->getSession();

						/**
						 * NOTE: PR_SENT_REPRESENTING_NAME might contain user info
						 * for which current user is sending mail on behalf of, so
						 * get delegator's store.
						 */
						if (!empty($prop[PR_SENT_REPRESENTING_NAME]) && !empty($prop[PR_SENT_REPRESENTING_EMAIL_ADDRESS])) {
							$addrbook = mapi_openaddressbook($session);
							// resolve users based on email address
							/**
							 * users are resolved only on PR_DISPLAY_NAME property
							 * so used PR_DISPLAY_NAME property and passed email address as its value
							 * we can't use PR_SMTP_ADDRESS or PR_EMAIL_ADDRESS properties for resolving users
							 *
							 * EMS_AB_ADDRESS_LOOKUP is used to specify strict matching
							 */
							$user = array( array( PR_DISPLAY_NAME => u2w($prop[PR_SENT_REPRESENTING_EMAIL_ADDRESS]) ) );
							$user = mapi_ab_resolvename($addrbook, $user, EMS_AB_ADDRESS_LOOKUP);
							if(mapi_last_hresult() == NOERROR) {
								$user2_entryid = mapi_msgstore_createentryid($store, $user[0][PR_EMAIL_ADDRESS]);
								$store = mapi_openmsgstore($session, $user2_entryid);
							}
						} else if(!$action["props"]["from"]) {
							// if from address is not specified and we are sending mail from
							// delegate's mailbox then don't send mail on behalf of
							$send_as_onbehalf = false;
						}

						if(!$store) {
							$store = $GLOBALS["mapisession"]->getDefaultMessageStore();
						}

						$parententryid = $this->getDefaultFolderEntryID($store, $action["props"]["message_class"]);
						$result = $GLOBALS["operations"]->submitMessage($store, Conversion::mapXML2MAPI($this->properties, $action["props"]), $action["recipients"]["recipient"], $action["dialog_attachments"], $messageProps, $copyAttachments, $copyAttachmentsStore, $add_inline_attachments, $delete_inline_attachments, $copy_only_inline_attachments, $replyOrigMessageDetails, $send_as_onbehalf);

						// If draft is send from the drafts folder, delete notification
						if($result) {
							if(isset($action["props"]["entryid"]) && $action["props"]["entryid"] != "") {
								$props = array();
								$props[PR_ENTRYID] = hex2bin($action["props"]["entryid"]);
								$props[PR_PARENT_ENTRYID] = $parententryid;

								$storeprops = mapi_getprops($store, array(PR_ENTRYID));
								$props[PR_STORE_ENTRYID] = $storeprops[PR_ENTRYID];

								$GLOBALS["bus"]->notify(bin2hex($parententryid), TABLE_DELETE, $props);
							}
						}
					} else {
						$parententryid = $this->getDefaultFolderEntryID($GLOBALS["mapisession"]->getDefaultMessageStore(), $action["props"]["message_class"]);
						if ($action["props"]["entryid"]==""){
							$action["props"]["entryid"] = $state->read("createmail".$action["dialog_attachments"]);
						}
						$result = $GLOBALS["operations"]->saveMessage($GLOBALS["mapisession"]->getDefaultMessageStore(), $parententryid, Conversion::mapXML2MAPI($this->properties, $action["props"]), $action["recipients"]["recipient"], $action["dialog_attachments"], $messageProps, $copyAttachments, $copyAttachmentsStore, array(), $add_inline_attachments, $delete_inline_attachments, $copy_only_inline_attachments);
						$state->write("createmail".$action["dialog_attachments"], bin2hex($messageProps[PR_ENTRYID]));

						// Update the client with the (new) entryid and parententryid to allow the draft message to be removed when submitting.
						// Retrieve entryid and parententryid of new mail.
						$props = array();
						$props = mapi_getprops($result, array(PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID));
						// Send data to client
						$data = array();
						$data["attributes"] = array("type" => "update");
						$data["item"] = array();
						$data["item"]["entryid"] = bin2hex($props[PR_ENTRYID]);
						$data["item"]["parententryid"] = bin2hex($props[PR_PARENT_ENTRYID]);
						$data["item"]["storeid"] = bin2hex($props[PR_STORE_ENTRYID]);

						//send info to update attachments
						/**
						 * We have to reopen the message because the PR_ATTACH_SIZE is calculated
						 * when the message has been saved. This information is then not updated in
						 * the MAPI Message Object that is still open. So you have to reopen it to
						 * get the SIZE data.
						 */
						$savedMsg = $GLOBALS['operations']->openMessage($store, $props[PR_ENTRYID]);
						$hasattachProp = mapi_getprops($savedMsg, array(PR_HASATTACH));
						if (isset($hasattachProp[PR_HASATTACH])&& $hasattachProp[PR_HASATTACH]){
							$attachmentTable = mapi_message_getattachmenttable($savedMsg);
							$attachments = mapi_table_queryallrows($attachmentTable, array(PR_ATTACH_NUM, PR_ATTACH_SIZE, PR_ATTACH_LONG_FILENAME, PR_ATTACH_FILENAME, PR_ATTACHMENT_HIDDEN, PR_DISPLAY_NAME, PR_ATTACH_METHOD, PR_ATTACH_CONTENT_ID, PR_ATTACH_MIME_TAG));
							$attachment_props["attachments"] = array();
							$attachment_props["attachments"]["attachment"] = array();

							foreach($attachments as $attachmentRow)
							{
								if(!isset($attachmentRow[PR_ATTACHMENT_HIDDEN]) || !$attachmentRow[PR_ATTACHMENT_HIDDEN]) {
									$attachment = array();
									$attachment["attach_num"] = $attachmentRow[PR_ATTACH_NUM];
									$attachment["attach_method"] = $attachmentRow[PR_ATTACH_METHOD];
									$attachment["size"] = $attachmentRow[PR_ATTACH_SIZE];
									$attachment["cid"] = $attachmentRow[PR_ATTACH_CONTENT_ID];
									$attachment["filetype"] = $attachmentRow[PR_ATTACH_MIME_TAG];

									if(isset($attachmentRow[PR_ATTACH_LONG_FILENAME]))
										$attachment["name"] = $attachmentRow[PR_ATTACH_LONG_FILENAME];
									else if(isset($attachmentRow[PR_ATTACH_FILENAME]))
										$attachment["name"] = $attachmentRow[PR_ATTACH_FILENAME];
									else if(isset($attachmentRow[PR_DISPLAY_NAME]))
										$attachment["name"] = $attachmentRow[PR_DISPLAY_NAME];
									else
										$attcahment["name"] = "untitled";

									if (isset($attachment["name"])){
										$attachment["name"] = windows1252_to_utf8($attachment["name"]);
									}

									if ($attachment["attach_method"] == ATTACH_EMBEDDED_MSG){
										// open attachment to get the message class
										$attach = mapi_message_openattach($savedMsg, $attachment["attach_num"]);
										$embMessage = mapi_attach_openobj($attach);
										$embProps = mapi_getprops($embMessage, array(PR_MESSAGE_CLASS));
										if (isset($embProps[PR_MESSAGE_CLASS]))
											$attachment["attach_message_class"] = $embProps[PR_MESSAGE_CLASS];
									}

									array_push($attachment_props["attachments"]["attachment"], $attachment);
								}
							}
							$data["item"]["attachments"] = $attachment_props["attachments"];
						}

						array_push($this->responseData["action"], $data);
						$GLOBALS["bus"]->addData($this->responseData);
					}

					// Reply/Reply All/Forward Actions (ICON_INDEX & LAST_VERB_EXECUTED)
					if(isset($action["message_action"]) && isset($action["message_action"]["action_type"]) && $action["message_action"]["action_type"] != "forwardasattachment") {
						$props = array();
						$props[$this->properties["entryid"]] = hex2bin($action["message_action"]["entryid"]);

						switch($action["message_action"]["action_type"])
						{
							case "reply":
								$props[$this->properties["icon_index"]] = 261;
								$props[$this->properties["last_verb_executed"]] = 102;
								break;
							case "replyall":
								$props[$this->properties["icon_index"]] = 261;
								$props[$this->properties["last_verb_executed"]] = 103;
								break;
							case "forward":
								$props[$this->properties["icon_index"]] = 262;
								$props[$this->properties["last_verb_executed"]] = 104;
								break;
						}

						$props[$this->properties["last_verb_execution_time"]] = mktime();

						// Use the storeid of that belongs to the message_action entryid.
						// This is in case the message is on another store.
						$storeOrigMsg = $GLOBALS["mapisession"]->openMessageStore(hex2bin($action["message_action"]["storeid"]));

						$message_action_props = array();
						$message_action_result = $GLOBALS["operations"]->saveMessage($storeOrigMsg, $parententryid, $props, false, array(), $message_action_props);

						if($message_action_result) {
							if(isset($message_action_props[PR_PARENT_ENTRYID])) {
								$GLOBALS["bus"]->notify(bin2hex($message_action_props[PR_PARENT_ENTRYID]), TABLE_SAVE, $message_action_props);
							}
						}
					}
				}
				if($result && !$send) {
					$GLOBALS["bus"]->notify(bin2hex($messageProps[PR_PARENT_ENTRYID]), TABLE_SAVE, $messageProps);
				}

				// Send success message to client, only when you are sending a mail, not when saving it
				if($send) {
					$this->sendFeedback(true);
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
			$this->properties = $GLOBALS["properties"]->getMailProperties($store);
		}
	}
?>
