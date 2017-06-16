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
 */

/*
 * edkmdb.h – Defines additional properties and interfaces offered by the
 * server-side information store.
 */

#ifndef __M4L_EDKMDB_H_
#define __M4L_EDKMDB_H_

/*
 * No, this is not the edkmdb.h file,
 * but it contains the exchange interfaces used in kopano
 */

#define	OPENSTORE_USE_ADMIN_PRIVILEGE		((ULONG)1)
#define OPENSTORE_PUBLIC					((ULONG)2)
#define	OPENSTORE_HOME_LOGON				((ULONG)4)
#define OPENSTORE_TAKE_OWNERSHIP			((ULONG)8)
#define OPENSTORE_OVERRIDE_HOME_MDB			((ULONG)16)
#define OPENSTORE_TRANSPORT					((ULONG)32)
#define OPENSTORE_REMOTE_TRANSPORT			((ULONG)64)

#include <kopano/platform.h>
#include <initializer_list>
#include <edkguid.h>

class IExchangeManageStore : public virtual IUnknown {
public:
	virtual HRESULT __stdcall CreateStoreEntryID(LPTSTR lpszMsgStoreDN, LPTSTR lpszMailboxDN, ULONG	ulFlags, ULONG *lpcbEntryID,
												 LPENTRYID *lppEntryID) = 0;
	virtual HRESULT __stdcall EntryIDFromSourceKey(ULONG cFolderKeySize, BYTE *lpFolderSourceKey,	ULONG cMessageKeySize,
												   BYTE *lpMessageSourceKey, ULONG *lpcbEntryID, LPENTRYID *lppEntryID) = 0;
	virtual HRESULT __stdcall GetRights(ULONG cbUserEntryID, LPENTRYID lpUserEntryID, ULONG cbEntryID, LPENTRYID lpEntryID,
										ULONG *lpulRights) = 0;
	virtual HRESULT __stdcall GetMailboxTable(LPTSTR lpszServerName, LPMAPITABLE *lppTable, ULONG ulFlags) = 0;
	virtual HRESULT __stdcall GetPublicFolderTable(LPTSTR lpszServerName, LPMAPITABLE *lppTable, ULONG ulFlags) = 0;
};
IID_OF(IExchangeManageStore);

typedef IExchangeManageStore* LPEXCHANGEMANAGESTORE;

#define pidStoreNonTransMin					0x0E40
#define pidExchangeXmitReservedMin			0x3FE0
#define pidExchangeNonXmitReservedMin		0x65E0
#define pidProfileMin						0x6600
#define pidStoreMin							0x6618
#define pidFolderMin						0x6638
#define pidMessageReadOnlyMin				0x6640
#define pidMessageWriteableMin				0x6658
#define pidAttachReadOnlyMin				0x666C
#define pidSpecialMin						0x6670
#define pidAdminMin							0x6690
#define pidSecureProfileMin					PROP_ID_SECURE_MIN
#define pidRenMsgFldMin						0x1080
#define pidLocalStoreInternalMin			0x6500
#define pidLocalStoreInternalMax			0x65C0


#define PR_NON_IPM_SUBTREE_ENTRYID			PROP_TAG( PT_BINARY, pidStoreMin+0x08)
#define PR_IPM_FAVORITES_ENTRYID			PROP_TAG( PT_BINARY, pidStoreMin+0x18)
#define PR_IPM_PUBLIC_FOLDERS_ENTRYID		PROP_TAG( PT_BINARY, pidStoreMin+0x19)


/* missing PR_* defines for common/ECDebug */
#define PR_PROFILE_VERSION					PROP_TAG(PT_LONG, pidProfileMin+0x00)
#define PR_PROFILE_CONFIG_FLAGS				PROP_TAG(PT_LONG, pidProfileMin+0x01)
#define PR_PROFILE_HOME_SERVER				PROP_TAG(PT_STRING8, pidProfileMin+0x02)
#define PR_PROFILE_HOME_SERVER_DN			PROP_TAG(PT_STRING8, pidProfileMin+0x12)
#define PR_PROFILE_HOME_SERVER_ADDRS		PROP_TAG(PT_MV_STRING8, pidProfileMin+0x13)
#define PR_PROFILE_USER						PROP_TAG(PT_STRING8, pidProfileMin+0x03)
#define PR_PROFILE_CONNECT_FLAGS			PROP_TAG(PT_LONG, pidProfileMin+0x04)
#define PR_PROFILE_TRANSPORT_FLAGS			PROP_TAG(PT_LONG, pidProfileMin+0x05)
#define PR_PROFILE_UI_STATE					PROP_TAG(PT_LONG, pidProfileMin+0x06)
#define PR_PROFILE_UNRESOLVED_NAME			PROP_TAG(PT_STRING8, pidProfileMin+0x07)
#define PR_PROFILE_UNRESOLVED_SERVER		PROP_TAG(PT_STRING8, pidProfileMin+0x08)
#define PR_PROFILE_BINDING_ORDER			PROP_TAG(PT_STRING8, pidProfileMin+0x09)
#define PR_PROFILE_MAX_RESTRICT				PROP_TAG(PT_LONG, pidProfileMin+0x0D)
#define PR_PROFILE_AB_FILES_PATH			PROP_TAG(PT_STRING8, pidProfileMin+0xE)
#define PR_PROFILE_OFFLINE_STORE_PATH		PROP_TAG(PT_STRING8, pidProfileMin+0x10)
#define PR_PROFILE_OFFLINE_INFO				PROP_TAG(PT_BINARY, pidProfileMin+0x11)
#define PR_PROFILE_ADDR_INFO				PROP_TAG(PT_BINARY, pidSpecialMin+0x17)
#define PR_PROFILE_OPTIONS_DATA				PROP_TAG(PT_BINARY, pidSpecialMin+0x19)
#define PR_PROFILE_SECURE_MAILBOX			PROP_TAG(PT_BINARY, pidSecureProfileMin+0x00)
#define PR_DISABLE_WINSOCK					PROP_TAG(PT_LONG, pidProfileMin+0x18)
#define PR_PROFILE_AUTH_PACKAGE				PROP_TAG(PT_LONG, pidProfileMin+0x19)
#define PR_PROFILE_RECONNECT_INTERVAL		PROP_TAG(PT_LONG, pidProfileMin+0x1A)
#define PR_PROFILE_SERVER_VERSION			PROP_TAG(PT_LONG, pidProfileMin+0x1B)

#define PR_OST_ENCRYPTION					PROP_TAG(PT_LONG, 0x6702)

#define PR_PROFILE_OPEN_FLAGS				PROP_TAG(PT_LONG, pidProfileMin+0x09)
#define PR_PROFILE_TYPE						PROP_TAG(PT_LONG, pidProfileMin+0x0A)
#define PR_PROFILE_MAILBOX					PROP_TAG(PT_STRING8, pidProfileMin+0x0B)
#define PR_PROFILE_SERVER					PROP_TAG(PT_STRING8, pidProfileMin+0x0C)
#define PR_PROFILE_SERVER_DN				PROP_TAG(PT_STRING8, pidProfileMin+0x14)

#define PR_PROFILE_FAVFLD_DISPLAY_NAME		PROP_TAG(PT_STRING8, pidProfileMin+0x0F)
#define PR_PROFILE_FAVFLD_COMMENT			PROP_TAG(PT_STRING8, pidProfileMin+0x15)
#define PR_PROFILE_ALLPUB_DISPLAY_NAME		PROP_TAG(PT_STRING8, pidProfileMin+0x16)
#define PR_PROFILE_ALLPUB_COMMENT			PROP_TAG(PT_STRING8, pidProfileMin+0x17)

#define PR_PROFILE_MOAB						PROP_TAG(PT_STRING8, pidSpecialMin+0x0B)
#define PR_PROFILE_MOAB_GUID				PROP_TAG(PT_STRING8, pidSpecialMin+0x0C)
#define PR_PROFILE_MOAB_SEQ					PROP_TAG(PT_LONG, pidSpecialMin+0x0D)

#define PR_GET_PROPS_EXCLUDE_PROP_ID_LIST	PROP_TAG(PT_BINARY, pidSpecialMin+0x0E)


#define PR_USER_ENTRYID						PROP_TAG(PT_BINARY, pidStoreMin+0x01)
#define PR_USER_NAME						PROP_TAG( PT_TSTRING, pidStoreMin+0x02)
#define PR_USER_NAME_A						PROP_TAG( PT_STRING8, pidStoreMin+0x02)
#define PR_USER_NAME_W						PROP_TAG( PT_UNICODE, pidStoreMin+0x02)

#define PR_MAILBOX_OWNER_ENTRYID			PROP_TAG(PT_BINARY, pidStoreMin+0x03)
#define PR_MAILBOX_OWNER_NAME				PROP_TAG( PT_TSTRING, pidStoreMin+0x04)
#define PR_MAILBOX_OWNER_NAME_A				PROP_TAG( PT_STRING8, pidStoreMin+0x04)
#define PR_MAILBOX_OWNER_NAME_W				PROP_TAG( PT_UNICODE, pidStoreMin+0x04)
#define PR_OOF_STATE						PROP_TAG(PT_BOOLEAN, pidStoreMin+0x05)

#define PR_HIERARCHY_SERVER					PROP_TAG(PT_TSTRING, pidStoreMin+0x1B)

#define PR_SCHEDULE_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, pidStoreMin+0x06)

#define PR_IPM_DAF_ENTRYID					PROP_TAG(PT_BINARY, pidStoreMin+0x07)

#define PR_EFORMS_REGISTRY_ENTRYID				PROP_TAG(PT_BINARY, pidStoreMin+0x09)
#define PR_SPLUS_FREE_BUSY_ENTRYID				PROP_TAG(PT_BINARY, pidStoreMin+0x0A)
#define PR_OFFLINE_ADDRBOOK_ENTRYID				PROP_TAG(PT_BINARY, pidStoreMin+0x0B)
#define PR_NNTP_CONTROL_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, pidSpecialMin+0x1B)
#define PR_EFORMS_FOR_LOCALE_ENTRYID			PROP_TAG(PT_BINARY, pidStoreMin+0x0C)
#define PR_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID		PROP_TAG(PT_BINARY, pidStoreMin+0x0D)
#define PR_ADDRBOOK_FOR_LOCAL_SITE_ENTRYID		PROP_TAG(PT_BINARY, pidStoreMin+0x0E)
#define PR_NEWSGROUP_ROOT_FOLDER_ENTRYID		PROP_TAG(PT_BINARY, pidSpecialMin+0x1C)
#define PR_OFFLINE_MESSAGE_ENTRYID				PROP_TAG(PT_BINARY, pidStoreMin+0x0F)
#define PR_FAVORITES_DEFAULT_NAME				PROP_TAG(PT_STRING8, pidStoreMin+0x1D)
#define PR_SYS_CONFIG_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, pidStoreMin+0x1E)
#define PR_NNTP_ARTICLE_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, pidSpecialMin+0x1A)
#define PR_EVENTS_ROOT_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, pidSpecialMin+0xA)

#define PR_GW_MTSIN_ENTRYID					PROP_TAG(PT_BINARY, pidStoreMin+0x10)
#define PR_GW_MTSOUT_ENTRYID				PROP_TAG(PT_BINARY, pidStoreMin+0x11)
#define PR_TRANSFER_ENABLED					PROP_TAG(PT_BOOLEAN, pidStoreMin+0x12)

#define PR_TEST_LINE_SPEED					PROP_TAG(PT_BINARY, pidStoreMin+0x13)

#define PR_HIERARCHY_SYNCHRONIZER			PROP_TAG(PT_OBJECT, pidStoreMin+0x14)
#define PR_CONTENTS_SYNCHRONIZER			PROP_TAG(PT_OBJECT, pidStoreMin+0x15)
#define PR_COLLECTOR						PROP_TAG(PT_OBJECT, pidStoreMin+0x16)

#define PR_FAST_TRANSFER					PROP_TAG(PT_OBJECT, pidStoreMin+0x17)

#define PR_CHANGE_ADVISOR					PROP_TAG(PT_OBJECT, pidStoreMin+0x1C)

#define PR_CHANGE_NOTIFICATION_GUID			PROP_TAG(PT_CLSID, pidStoreMin+0x1F)

#define PR_STORE_OFFLINE					PROP_TAG(PT_BOOLEAN, pidStoreMin+0x1A)

#define PR_IN_TRANSIT						PROP_TAG(PT_BOOLEAN, pidStoreMin+0x00)

#define PR_REPLICATION_STYLE				PROP_TAG(PT_LONG, pidAdminMin+0x00)
#define PR_REPLICATION_SCHEDULE				PROP_TAG(PT_BINARY, pidAdminMin+0x01)
#define PR_REPLICATION_MESSAGE_PRIORITY 	PROP_TAG(PT_LONG, pidAdminMin+0x02)

#define PR_OVERALL_MSG_AGE_LIMIT			PROP_TAG(PT_LONG, pidAdminMin+0x03)
#define PR_REPLICATION_ALWAYS_INTERVAL		PROP_TAG(PT_LONG, pidAdminMin+0x04)
#define PR_REPLICATION_MSG_SIZE				PROP_TAG(PT_LONG, pidAdminMin+0x05)

#define PR_SOURCE_KEY						PROP_TAG(PT_BINARY, pidExchangeNonXmitReservedMin+0x00)
#define PR_PARENT_SOURCE_KEY				PROP_TAG(PT_BINARY, pidExchangeNonXmitReservedMin+0x01)
#define PR_CHANGE_KEY						PROP_TAG(PT_BINARY, pidExchangeNonXmitReservedMin+0x02)
#define PR_PREDECESSOR_CHANGE_LIST			PROP_TAG(PT_BINARY, pidExchangeNonXmitReservedMin+0x03)

#define PR_SOURCE_FID						PROP_TAG(PT_I8, pidStoreNonTransMin+0x1F)

#define PR_CATALOG							PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x1B)

#define PR_CI_SEARCH_ENABLED				PROP_TAG(PT_BOOLEAN, pidStoreNonTransMin+0x1C)
#define PR_CI_NOTIFICATION_ENABLED			PROP_TAG(PT_BOOLEAN, pidStoreNonTransMin+0x1D)
#define PR_MAX_CACHED_VIEWS					PROP_TAG(PT_LONG, pidStoreNonTransMin+0x28)
#define PR_MAX_INDICES						PROP_TAG(PT_LONG, pidStoreNonTransMin+0x1E)
#define PR_IMPLIED_RESTRICTIONS				PROP_TAG(PT_MV_BINARY, pidSpecialMin+0x0F)

#define PR_FOLDER_CHILD_COUNT				PROP_TAG(PT_LONG, pidFolderMin)
#define PR_RIGHTS							PROP_TAG(PT_LONG, pidFolderMin+0x01)
#define PR_ACL_TABLE						PROP_TAG(PT_OBJECT, pidExchangeXmitReservedMin)
#define PR_RULES_TABLE						PROP_TAG(PT_OBJECT, pidExchangeXmitReservedMin+0x1)
#define PR_HAS_RULES						PROP_TAG(PT_BOOLEAN, pidFolderMin+0x02)
#define PR_HAS_MODERATOR_RULES				PROP_TAG(PT_BOOLEAN, pidFolderMin+0x07 )

#define PR_ADDRESS_BOOK_ENTRYID				PROP_TAG(PT_BINARY, pidFolderMin+0x03)

#define PR_ACL_DATA							PROP_TAG(PT_BINARY, pidExchangeXmitReservedMin)
#define PR_RULES_DATA						PROP_TAG(PT_BINARY, pidExchangeXmitReservedMin+0x1)
#define PR_EXTENDED_ACL_DATA				PROP_TAG(PT_BINARY, pidExchangeXmitReservedMin+0x1E)
#define PR_FOLDER_DESIGN_FLAGS				PROP_TAG(PT_LONG, pidExchangeXmitReservedMin+0x2)
#define PR_DESIGN_IN_PROGRESS				PROP_TAG(PT_BOOLEAN, pidExchangeXmitReservedMin+0x4)
#define PR_SECURE_ORIGINATION				PROP_TAG(PT_BOOLEAN, pidExchangeXmitReservedMin+0x5)

#define PR_PUBLISH_IN_ADDRESS_BOOK			PROP_TAG(PT_BOOLEAN, pidExchangeXmitReservedMin+0x6)
#define PR_RESOLVE_METHOD					PROP_TAG(PT_LONG,	 pidExchangeXmitReservedMin+0x7)
#define PR_ADDRESS_BOOK_DISPLAY_NAME		PROP_TAG(PT_TSTRING, pidExchangeXmitReservedMin+0x8)

#define PR_EFORMS_LOCALE_ID					PROP_TAG(PT_LONG, pidExchangeXmitReservedMin+0x9)

#define PR_REPLICA_LIST						PROP_TAG(PT_BINARY, pidAdminMin+0x8)
#define PR_OVERALL_AGE_LIMIT				PROP_TAG(PT_LONG, pidAdminMin+0x9)

#define PR_IS_NEWSGROUP_ANCHOR				PROP_TAG(PT_BOOLEAN, pidAdminMin+0x06)
#define PR_IS_NEWSGROUP						PROP_TAG(PT_BOOLEAN, pidAdminMin+0x07)
#define PR_NEWSGROUP_COMPONENT				PROP_TAG(PT_STRING8, pidAdminMin+0x15)
#define PR_INTERNET_NEWSGROUP_NAME			PROP_TAG(PT_STRING8, pidAdminMin+0x17)
#define PR_NEWSFEED_INFO					PROP_TAG(PT_BINARY,  pidAdminMin+0x16)

#define PR_PREVENT_MSG_CREATE				PROP_TAG(PT_BOOLEAN, pidExchangeNonXmitReservedMin+0x14)
#define PR_IMAP_INTERNAL_DATE				PROP_TAG(PT_SYSTIME, pidExchangeNonXmitReservedMin+0x15)
#define PR_INBOUND_NEWSFEED_DN				PROP_TAG(PT_STRING8, pidSpecialMin+0x1D)
#define PR_OUTBOUND_NEWSFEED_DN				PROP_TAG(PT_STRING8, pidSpecialMin+0x1E)
#define PR_INTERNET_CHARSET					PROP_TAG(PT_TSTRING, pidAdminMin+0xA)
#define PR_PUBLIC_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, pidFolderMin+0x04)
#define PR_HIERARCHY_CHANGE_NUM				PROP_TAG(PT_LONG, pidFolderMin+0x06)

#define PR_USER_SID							PROP_TAG(PT_BINARY, PROP_ID(ptagSearchState))
#define PR_CREATOR_TOKEN					PR_USER_SID

#define PR_HAS_NAMED_PROPERTIES				PROP_TAG(PT_BOOLEAN, pidMessageReadOnlyMin+0x0A)

#define PR_CREATOR_NAME						PROP_TAG(PT_TSTRING, pidExchangeXmitReservedMin+0x18)
#define PR_CREATOR_NAME_A					PROP_TAG(PT_STRING8, pidExchangeXmitReservedMin+0x18)
#define PR_CREATOR_NAME_W					PROP_TAG(PT_UNICODE, pidExchangeXmitReservedMin+0x18)
#define PR_CREATOR_ENTRYID					PROP_TAG(PT_BINARY, pidExchangeXmitReservedMin+0x19)
#define PR_LAST_MODIFIER_NAME				PROP_TAG(PT_TSTRING, pidExchangeXmitReservedMin+0x1A)
#define PR_LAST_MODIFIER_NAME_A				PROP_TAG(PT_STRING8, pidExchangeXmitReservedMin+0x1A)
#define PR_LAST_MODIFIER_NAME_W				PROP_TAG(PT_UNICODE, pidExchangeXmitReservedMin+0x1A)
#define PR_LAST_MODIFIER_ENTRYID			PROP_TAG(PT_BINARY, pidExchangeXmitReservedMin+0x1B)
#define PR_REPLY_RECIPIENT_SMTP_PROXIES		PROP_TAG(PT_TSTRING, pidExchangeXmitReservedMin+0x1C)
#define PR_REPLY_RECIPIENT_SMTP_PROXIES_A	PROP_TAG(PT_STRING8, pidExchangeXmitReservedMin+0x1C)
#define PR_REPLY_RECIPIENT_SMTP_PROXIES_W	PROP_TAG(PT_UNICODE, pidExchangeXmitReservedMin+0x1C)

#define PR_HAS_DAMS							PROP_TAG(PT_BOOLEAN, pidExchangeXmitReservedMin+0x0A)
#define PR_RULE_TRIGGER_HISTORY				PROP_TAG(PT_BINARY, pidExchangeXmitReservedMin+0x12)
#define PR_MOVE_TO_STORE_ENTRYID			PROP_TAG(PT_BINARY, pidExchangeXmitReservedMin+0x13)
#define PR_MOVE_TO_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, pidExchangeXmitReservedMin+0x14)

#define PR_REPLICA_SERVER					PROP_TAG(PT_TSTRING, pidMessageReadOnlyMin+0x04)
#define PR_REPLICA_VERSION					PROP_TAG(PT_I8, pidMessageReadOnlyMin+0x0B)

#define PR_CREATOR_SID						PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x18)
#define PR_LAST_MODIFIER_SID				PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x19)
#define PR_SENDER_SID						PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x0d)
#define PR_SENT_REPRESENTING_SID			PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x0e)
#define PR_ORIGINAL_SENDER_SID				PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x0f)
#define PR_ORIGINAL_SENT_REPRESENTING_SID	PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x10)
#define PR_READ_RECEIPT_SID					PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x11)
#define PR_REPORT_SID						PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x12)
#define PR_ORIGINATOR_SID					PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x13)
#define PR_REPORT_DESTINATION_SID			PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x14)
#define PR_ORIGINAL_AUTHOR_SID				PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x15)
#define PR_RECEIVED_BY_SID					PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x16)
#define PR_RCVD_REPRESENTING_SID			PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x17)

#define PR_TRUST_SENDER_NO					0x00000000L
#define PR_TRUST_SENDER_YES					0x00000001L
#define PR_TRUST_SENDER						PROP_TAG(PT_LONG,	pidStoreNonTransMin+0x39)

#define PR_CREATOR_SID_AS_XML						PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x2C)
#define PR_LAST_MODIFIER_SID_AS_XML					PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x2D)
#define PR_SENDER_SID_AS_XML						PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x2E)
#define PR_SENT_REPRESENTING_SID_AS_XML				PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x2F)
#define PR_ORIGINAL_SENDER_SID_AS_XML				PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x30)
#define PR_ORIGINAL_SENT_REPRESENTING_SID_AS_XML	PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x31)
#define PR_READ_RECEIPT_SID_AS_XML					PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x32)
#define PR_REPORT_SID_AS_XML						PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x33)
#define PR_ORIGINATOR_SID_AS_XML					PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x34)
#define PR_REPORT_DESTINATION_SID_AS_XML			PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x35)
#define PR_ORIGINAL_AUTHOR_SID_AS_XML				PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x36)
#define PR_RECEIVED_BY_SID_AS_XML					PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x37)
#define PR_RCVD_REPRESENTING_SID_AS_XML				PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x38)


#define PR_MERGE_MIDSET_DELETED			PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x3a)
#define PR_RESERVE_RANGE_OF_IDS			PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x3b)

#define PR_FID_VID						PROP_TAG(PT_BINARY, pidMessageReadOnlyMin+0x0C)
//#define PR_FID_MID						PR_FID_VID	 //NSK : temporary to allow transition

#define PR_ORIGIN_ID					PROP_TAG(PT_BINARY, pidMessageReadOnlyMin+0x0D)

#define PR_RANK							PROP_TAG(PT_LONG, pidAdminMin+0x82 )

#define PR_MSG_FOLD_TIME				PROP_TAG(PT_SYSTIME, pidMessageReadOnlyMin+0x14)
#define PR_ICS_CHANGE_KEY				PROP_TAG(PT_BINARY, pidMessageReadOnlyMin+0x15)

#define PR_DEFERRED_SEND_NUMBER			PROP_TAG(PT_LONG, pidExchangeXmitReservedMin+0xB)
#define PR_DEFERRED_SEND_UNITS			PROP_TAG(PT_LONG, pidExchangeXmitReservedMin+0xC)
#define PR_EXPIRY_NUMBER				PROP_TAG(PT_LONG, pidExchangeXmitReservedMin+0xD)
#define PR_EXPIRY_UNITS					PROP_TAG(PT_LONG, pidExchangeXmitReservedMin+0xE)

#define PR_DEFERRED_SEND_TIME			PROP_TAG(PT_SYSTIME, pidExchangeXmitReservedMin+0xF)
#define PR_GW_ADMIN_OPERATIONS			PROP_TAG(PT_LONG, pidMessageWriteableMin)

#define PR_P1_CONTENT					PROP_TAG(PT_BINARY, 0x1100)
#define PR_P1_CONTENT_TYPE				PROP_TAG(PT_BINARY, 0x1101)

#define PR_CLIENT_ACTIONS				PROP_TAG(PT_BINARY, pidMessageReadOnlyMin+0x5)
#define PR_DAM_ORIGINAL_ENTRYID			PROP_TAG(PT_BINARY, pidMessageReadOnlyMin+0x6)
#define PR_DAM_BACK_PATCHED				PROP_TAG(PT_BOOLEAN, pidMessageReadOnlyMin+0x7)

#define PR_RULE_ERROR					PROP_TAG(PT_LONG, pidMessageReadOnlyMin+0x8)
#define PR_RULE_ACTION_TYPE				PROP_TAG(PT_LONG, pidMessageReadOnlyMin+0x9)
#define PR_RULE_ACTION_NUMBER			PROP_TAG(PT_LONG, pidMessageReadOnlyMin+0x10)
#define PR_RULE_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, pidMessageReadOnlyMin+0x11)

#define PR_INTERNET_CONTENT				PROP_TAG(PT_BINARY, pidMessageWriteableMin+0x1)
#define PR_INTERNET_CONTENT_HANDLE		PROP_TAG(PT_FILE_HANDLE, pidMessageWriteableMin+0x1)
#define PR_INTERNET_CONTENT_EA			PROP_TAG(PT_FILE_EA, pidMessageWriteableMin+0x1)

#define PR_DOTSTUFF_STATE				PROP_TAG(PT_LONG, pidUserNonTransmitMin+0x1)
#define PR_MIME_SIZE					PROP_TAG(PT_LONG, 0x6746)
#define PR_MIME_SIZE_EXTENDED			PROP_TAG(PT_I8, 0x6746)
#define PR_FILE_SIZE					PROP_TAG(PT_LONG, 0x6747)
#define PR_FILE_SIZE_EXTENDED			PROP_TAG(PT_I8, 0x6747)
#define PR_MSG_EDITOR_FORMAT			PROP_TAG(PT_LONG, 0x5909)

#define PR_CONVERSION_STATE				PROP_TAG(PT_LONG, PROP_ID(ptagAdminNickName))
#define PR_HTML							PROP_TAG(PT_BINARY, PROP_ID(PR_BODY_HTML))
#define PR_ACTIVE_USER_ENTRYID			PROP_TAG(PT_BINARY, pidMessageReadOnlyMin+0x12)
#define PR_CONFLICT_ENTRYID				PROP_TAG(PT_BINARY, pidExchangeXmitReservedMin+0x10)
#define PR_MESSAGE_LOCALE_ID			PROP_TAG(PT_LONG, pidExchangeXmitReservedMin+0x11)
#define PR_MESSAGE_CODEPAGE				PROP_TAG(PT_LONG, pidExchangeXmitReservedMin+0x1D)
#define PR_STORAGE_QUOTA_LIMIT			PROP_TAG(PT_LONG, pidExchangeXmitReservedMin+0x15)
#define PR_EXCESS_STORAGE_USED			PROP_TAG(PT_LONG, pidExchangeXmitReservedMin+0x16)
#define PR_SVR_GENERATING_QUOTA_MSG		PROP_TAG(PT_TSTRING, pidExchangeXmitReservedMin+0x17)
#define PR_DELEGATED_BY_RULE			PROP_TAG(PT_BOOLEAN, pidExchangeXmitReservedMin+0x3)
#define PR_X400_ENVELOPE_TYPE			PROP_TAG(PT_LONG, pidMessageReadOnlyMin+0x13)
#define PR_AUTO_RESPONSE_SUPPRESS		PROP_TAG(PT_LONG, pidExchangeXmitReservedMin-0x01)
#define PR_INTERNET_CPID				PROP_TAG(PT_LONG, pidExchangeXmitReservedMin-0x02)
#define PR_SYNCEVENT_FIRED				PROP_TAG(PT_BOOLEAN, pidMessageReadOnlyMin+0x0F)
#define PR_IN_CONFLICT					PROP_TAG(PT_BOOLEAN, pidAttachReadOnlyMin)

#define PR_DELETED_ON							PROP_TAG(PT_SYSTIME, pidSpecialMin+0x1F)
#define PR_DELETED_MSG_COUNT					PROP_TAG(PT_LONG, pidFolderMin+0x08)
#define PR_DELETED_ASSOC_MSG_COUNT				PROP_TAG(PT_LONG, pidFolderMin+0x0B)
#define PR_DELETED_FOLDER_COUNT					PROP_TAG(PT_LONG, pidFolderMin + 0x09)
#define PR_OLDEST_DELETED_ON					PROP_TAG(PT_SYSTIME, pidFolderMin + 0x0A)

#define PR_DELETED_MESSAGE_SIZE_EXTENDED		PROP_TAG(PT_I8, pidAdminMin+0xB)
#define PR_DELETED_NORMAL_MESSAGE_SIZE_EXTENDED PROP_TAG(PT_I8, pidAdminMin+0xC)
#define PR_DELETED_ASSOC_MESSAGE_SIZE_EXTENDED	PROP_TAG(PT_I8, pidAdminMin+0xD)

#define PR_RETENTION_AGE_LIMIT					PROP_TAG(PT_LONG, pidAdminMin+0x34)
#define PR_DISABLE_PERUSER_READ					PROP_TAG(PT_BOOLEAN, pidAdminMin+0x35)
#define PR_LAST_FULL_BACKUP						PROP_TAG(PT_SYSTIME, pidSpecialMin+0x15)

#define PR_URL_NAME						PROP_TAG(PT_TSTRING, pidAdminMin+0x77)
#define PR_URL_NAME_A					PROP_TAG(PT_STRING8, pidAdminMin+0x77)
#define PR_URL_NAME_W					PROP_TAG(PT_UNICODE, pidAdminMin+0x77)

#define PR_URL_COMP_NAME				PROP_TAG(PT_TSTRING, pidRenMsgFldMin+0x73)
#define PR_URL_COMP_NAME_A				PROP_TAG(PT_STRING8, pidRenMsgFldMin+0x73)
#define PR_URL_COMP_NAME_W				PROP_TAG(PT_UNICODE, pidRenMsgFldMin+0x73)

#define PR_PARENT_URL_NAME				PROP_TAG(PT_TSTRING, pidAdminMin+0x7D)
#define PR_PARENT_URL_NAME_A			PROP_TAG(PT_STRING8, pidAdminMin+0x7D)
#define PR_PARENT_URL_NAME_W			PROP_TAG(PT_UNICODE, pidAdminMin+0x7D)

#define PR_FLAT_URL_NAME				PROP_TAG(PT_TSTRING, pidAdminMin+0x7E)
#define PR_FLAT_URL_NAME_A				PROP_TAG(PT_STRING8, pidAdminMin+0x7E)
#define PR_FLAT_URL_NAME_W				PROP_TAG(PT_UNICODE, pidAdminMin+0x7E)

#define PR_SRC_URL_NAME					PROP_TAG(PT_TSTRING, pidAdminMin+0x7F)
#define PR_SRC_URL_NAME_A				PROP_TAG(PT_STRING8, pidAdminMin+0x7F)
#define PR_SRC_URL_NAME_W				PROP_TAG(PT_UNICODE, pidAdminMin+0x7F)

#define PR_SECURE_IN_SITE				PROP_TAG(PT_BOOLEAN, pidAdminMin+0xE)
#define PR_LOCAL_COMMIT_TIME			PROP_TAG(PT_SYSTIME, pidAdminMin+0x79)
#define PR_LOCAL_COMMIT_TIME_MAX		PROP_TAG(PT_SYSTIME, pidAdminMin+0x7a)

#define PR_DELETED_COUNT_TOTAL			PROP_TAG(PT_LONG, pidAdminMin+0x7b)

#define PR_AUTO_RESET					PROP_TAG(PT_MV_CLSID, pidAdminMin+0x7c)

#define PR_LONGTERM_ENTRYID_FROM_TABLE	PROP_TAG(PT_BINARY, pidSpecialMin)

#define PR_SUBFOLDER					PROP_TAG(PT_BOOLEAN, pidAdminMin+0x78)

/* ATTN: new property types */
#define PT_SRESTRICTION					((ULONG) 0x00FD)
#define PT_ACTIONS						((ULONG) 0x00FE)

#define PR_ORIGINATOR_NAME				PROP_TAG( PT_TSTRING, pidMessageWriteableMin+0x3)
#define PR_ORIGINATOR_ADDR				PROP_TAG( PT_TSTRING, pidMessageWriteableMin+0x4)
#define PR_ORIGINATOR_ADDRTYPE			PROP_TAG( PT_TSTRING, pidMessageWriteableMin+0x5)
#define PR_ORIGINATOR_ENTRYID			PROP_TAG( PT_BINARY, pidMessageWriteableMin+0x6)
#define PR_ARRIVAL_TIME					PROP_TAG( PT_SYSTIME, pidMessageWriteableMin+0x7)
#define PR_TRACE_INFO					PROP_TAG( PT_BINARY, pidMessageWriteableMin+0x8)
#define PR_INTERNAL_TRACE_INFO			PROP_TAG( PT_BINARY, pidMessageWriteableMin+0x12)
#define PR_SUBJECT_TRACE_INFO			PROP_TAG( PT_BINARY, pidMessageWriteableMin+0x9)
#define PR_RECIPIENT_NUMBER				PROP_TAG( PT_LONG, pidMessageWriteableMin+0xA)
#define PR_MTS_SUBJECT_ID				PROP_TAG(PT_BINARY, pidMessageWriteableMin+0xB)
#define PR_REPORT_DESTINATION_NAME		PROP_TAG(PT_TSTRING, pidMessageWriteableMin+0xC)
#define PR_REPORT_DESTINATION_ENTRYID	PROP_TAG(PT_BINARY, pidMessageWriteableMin+0xD)
#define PR_CONTENT_SEARCH_KEY			PROP_TAG(PT_BINARY, pidMessageWriteableMin+0xE)
#define PR_FOREIGN_ID					PROP_TAG(PT_BINARY, pidMessageWriteableMin+0xF)
#define PR_FOREIGN_REPORT_ID			PROP_TAG(PT_BINARY, pidMessageWriteableMin+0x10)
#define PR_FOREIGN_SUBJECT_ID			PROP_TAG(PT_BINARY, pidMessageWriteableMin+0x11)
#define PR_PROMOTE_PROP_ID_LIST			PROP_TAG(PT_BINARY, pidMessageWriteableMin+0x13)
#define PR_MTS_ID						PR_MESSAGE_SUBMISSION_ID
#define PR_MTS_REPORT_ID				PR_MESSAGE_SUBMISSION_ID

#define PR_MEMBER_ID					PROP_TAG(PT_I8, pidSpecialMin+0x01)
#define PR_MEMBER_NAME					PROP_TAG(PT_TSTRING, pidSpecialMin+0x02)
#define PR_MEMBER_ENTRYID				PR_ENTRYID
#define PR_MEMBER_RIGHTS				PROP_TAG(PT_LONG, pidSpecialMin+0x03)

#define PR_RULE_ID						PROP_TAG(PT_I8, pidSpecialMin+0x04)
#define PR_RULE_IDS						PROP_TAG(PT_BINARY, pidSpecialMin+0x05)
#define PR_RULE_SEQUENCE				PROP_TAG(PT_LONG, pidSpecialMin+0x06)
#define PR_RULE_STATE					PROP_TAG(PT_LONG, pidSpecialMin+0x07)
#define PR_RULE_USER_FLAGS				PROP_TAG(PT_LONG, pidSpecialMin+0x08)
#define PR_RULE_CONDITION				PROP_TAG(PT_SRESTRICTION, pidSpecialMin+0x09)
#define PR_RULE_ACTIONS					PROP_TAG(PT_ACTIONS, pidSpecialMin+0x10)
#define PR_RULE_PROVIDER				PROP_TAG(PT_STRING8, pidSpecialMin+0x11)
#define PR_RULE_NAME					PROP_TAG(PT_TSTRING, pidSpecialMin+0x12)
#define PR_RULE_LEVEL					PROP_TAG(PT_LONG, pidSpecialMin+0x13)
#define PR_RULE_PROVIDER_DATA			PROP_TAG(PT_BINARY, pidSpecialMin+0x14)

#define PR_EXTENDED_RULE_ACTIONS		PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x59)
#define PR_EXTENDED_RULE_CONDITION		PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x5a)
#define PR_EXTENDED_RULE_SIZE_LIMIT		PROP_TAG(PT_LONG, pidStoreNonTransMin+0x5b)

#define PR_NT_USER_NAME					PROP_TAG(PT_TSTRING, pidAdminMin+0x10)

#define PR_LAST_LOGON_TIME				PROP_TAG(PT_SYSTIME, pidAdminMin+0x12 )
#define PR_LAST_LOGOFF_TIME				PROP_TAG(PT_SYSTIME, pidAdminMin+0x13 )
#define PR_STORAGE_LIMIT_INFORMATION	PROP_TAG(PT_LONG, pidAdminMin+0x14 )

#define PR_INTERNET_MDNS				PROP_TAG(PT_BOOLEAN, PROP_ID(PR_NEWSGROUP_COMPONENT))

#define PR_QUOTA_WARNING_THRESHOLD		PROP_TAG(PT_LONG, pidAdminMin+0x91)
#define PR_QUOTA_SEND_THRESHOLD			PROP_TAG(PT_LONG, pidAdminMin+0x92)
#define PR_QUOTA_RECEIVE_THRESHOLD		PROP_TAG(PT_LONG, pidAdminMin+0x93)


#define PR_FOLDER_FLAGS							PROP_TAG(PT_LONG, pidAdminMin+0x18)
#define PR_LAST_ACCESS_TIME						PROP_TAG(PT_SYSTIME, pidAdminMin+0x19)
#define PR_RESTRICTION_COUNT					PROP_TAG(PT_LONG, pidAdminMin+0x1A)
#define PR_CATEG_COUNT							PROP_TAG(PT_LONG, pidAdminMin+0x1B)
#define PR_CACHED_COLUMN_COUNT					PROP_TAG(PT_LONG, pidAdminMin+0x1C)
#define PR_NORMAL_MSG_W_ATTACH_COUNT			PROP_TAG(PT_LONG, pidAdminMin+0x1D)
#define PR_ASSOC_MSG_W_ATTACH_COUNT				PROP_TAG(PT_LONG, pidAdminMin+0x1E)
#define PR_RECIPIENT_ON_NORMAL_MSG_COUNT		PROP_TAG(PT_LONG, pidAdminMin+0x1F)
#define PR_RECIPIENT_ON_ASSOC_MSG_COUNT			PROP_TAG(PT_LONG, pidAdminMin+0x20)
#define PR_ATTACH_ON_NORMAL_MSG_COUNT			PROP_TAG(PT_LONG, pidAdminMin+0x21)
#define PR_ATTACH_ON_ASSOC_MSG_COUNT			PROP_TAG(PT_LONG, pidAdminMin+0x22)
#define PR_NORMAL_MESSAGE_SIZE					PROP_TAG(PT_LONG, pidAdminMin+0x23)
#define PR_NORMAL_MESSAGE_SIZE_EXTENDED			PROP_TAG(PT_I8, pidAdminMin+0x23)
#define PR_ASSOC_MESSAGE_SIZE					PROP_TAG(PT_LONG, pidAdminMin+0x24)
#define PR_ASSOC_MESSAGE_SIZE_EXTENDED			PROP_TAG(PT_I8, pidAdminMin+0x24)
#define PR_FOLDER_PATHNAME						PROP_TAG(PT_TSTRING, pidAdminMin+0x25)
#define PR_OWNER_COUNT							PROP_TAG(PT_LONG, pidAdminMin+0x26)
#define PR_CONTACT_COUNT						PROP_TAG(PT_LONG, pidAdminMin+0x27)

#define PR_PF_OVER_HARD_QUOTA_LIMIT				PROP_TAG(PT_LONG, pidAdminMin+0x91)
#define PR_PF_MSG_SIZE_LIMIT					PROP_TAG(PT_LONG, pidAdminMin+0x92)

#define PR_PF_DISALLOW_MDB_WIDE_EXPIRY			PROP_TAG(PT_BOOLEAN, pidAdminMin+0x93)

#define PR_LOCALE_ID					PROP_TAG(PT_LONG, pidAdminMin+0x11)
#define PR_CODE_PAGE_ID					PROP_TAG(PT_LONG, pidAdminMin+0x33)
#define PR_SORT_LOCALE_ID				PROP_TAG(PT_LONG, pidAdminMin+0x75)

#define PR_MESSAGE_SIZE_EXTENDED		PROP_TAG(PT_I8, PROP_ID(PR_MESSAGE_SIZE))

#define PR_AUTO_ADD_NEW_SUBS			PROP_TAG(PT_BOOLEAN, pidExchangeNonXmitReservedMin+0x5)
#define PR_NEW_SUBS_GET_AUTO_ADD		PROP_TAG(PT_BOOLEAN, pidExchangeNonXmitReservedMin+0x6)

#define PR_OFFLINE_FLAGS				PROP_TAG(PT_LONG, pidFolderMin+0x5)
#define PR_SYNCHRONIZE_FLAGS			PROP_TAG(PT_LONG, pidExchangeNonXmitReservedMin+0x4)

#define PR_MESSAGE_SITE_NAME			PROP_TAG(PT_TSTRING, pidExchangeNonXmitReservedMin+0x7)
#define PR_MESSAGE_SITE_NAME_A			PROP_TAG(PT_STRING8, pidExchangeNonXmitReservedMin+0x7)
#define PR_MESSAGE_SITE_NAME_W			PROP_TAG(PT_UNICODE, pidExchangeNonXmitReservedMin+0x7)

#define PR_MESSAGE_PROCESSED			PROP_TAG(PT_BOOLEAN, pidExchangeNonXmitReservedMin+0x8)

#define PR_MSG_BODY_ID					PROP_TAG(PT_LONG, pidExchangeXmitReservedMin-0x03)

#define PR_BILATERAL_INFO				PROP_TAG(PT_BINARY, pidExchangeXmitReservedMin-0x04)
#define PR_DL_REPORT_FLAGS				PROP_TAG(PT_LONG, pidExchangeXmitReservedMin-0x05)

#define PR_ABSTRACT						PROP_TAG(PT_TSTRING, pidExchangeXmitReservedMin-0x06)
#define PR_ABSTRACT_A					PROP_TAG(PT_STRING8, pidExchangeXmitReservedMin-0x06)
#define PR_ABSTRACT_W					PROP_TAG(PT_UNICODE, pidExchangeXmitReservedMin-0x06)

#define PR_PREVIEW						PROP_TAG(PT_TSTRING, pidExchangeXmitReservedMin-0x07)
#define PR_PREVIEW_A					PROP_TAG(PT_STRING8, pidExchangeXmitReservedMin-0x07)
#define PR_PREVIEW_W					PROP_TAG(PT_UNICODE, pidExchangeXmitReservedMin-0x07)

#define PR_PREVIEW_UNREAD				PROP_TAG(PT_TSTRING, pidExchangeXmitReservedMin-0x08)
#define PR_PREVIEW_UNREAD_A				PROP_TAG(PT_STRING8, pidExchangeXmitReservedMin-0x08)
#define PR_PREVIEW_UNREAD_W				PROP_TAG(PT_UNICODE, pidExchangeXmitReservedMin-0x08)

#define PR_DISABLE_FULL_FIDELITY		PROP_TAG(PT_BOOLEAN, pidRenMsgFldMin+0x72)

#define PR_ATTR_HIDDEN					PROP_TAG(PT_BOOLEAN, pidRenMsgFldMin+0x74)
#define PR_ATTR_SYSTEM					PROP_TAG(PT_BOOLEAN, pidRenMsgFldMin+0x75)
#define PR_ATTR_READONLY				PROP_TAG(PT_BOOLEAN, pidRenMsgFldMin+0x76)

#define PR_READ							PROP_TAG(PT_BOOLEAN, pidStoreNonTransMin+0x29)

#define PR_ADMIN_SECURITY_DESCRIPTOR	PROP_TAG(PT_BINARY, 0x3d21)
#define PR_WIN32_SECURITY_DESCRIPTOR	PROP_TAG(PT_BINARY, 0x3d22)
#define PR_NON_WIN32_ACL				PROP_TAG(PT_BOOLEAN, 0x3d23)

#define PR_ITEM_LEVEL_ACL				PROP_TAG(PT_BOOLEAN, 0x3d24)

#define PR_DAV_TRANSFER_SECURITY_DESCRIPTOR		PROP_TAG(PT_BINARY, 0x0E84)

#define PR_NT_SECURITY_DESCRIPTOR_AS_XML			PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x2A)
#define PR_NT_SECURITY_DESCRIPTOR_AS_XML_A			PROP_TAG(PT_STRING8, pidStoreNonTransMin+0x2A)
#define PR_NT_SECURITY_DESCRIPTOR_AS_XML_W			PROP_TAG(PT_UNICODE, pidStoreNonTransMin+0x2A)
#define PR_ADMIN_SECURITY_DESCRIPTOR_AS_XML			PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x2B)
#define PR_ADMIN_SECURITY_DESCRIPTOR_AS_XML_A		PROP_TAG(PT_STRING8, pidStoreNonTransMin+0x2B)
#define PR_ADMIN_SECURITY_DESCRIPTOR_AS_XML_W		PROP_TAG(PT_UNICODE, pidStoreNonTransMin+0x2B)

#define PR_OWA_URL						PROP_TAG (PT_STRING8, pidRenMsgFldMin+0x71)

#define PR_SYNCEVENT_SUPPRESS_GUID		PROP_TAG( PT_BINARY,	0x3880 )

#define PR_LOCK_BRANCH_ID				PROP_TAG( PT_I8,		0x3800 )
#define PR_LOCK_RESOURCE_FID			PROP_TAG( PT_I8,		0x3801 )
#define PR_LOCK_RESOURCE_DID			PROP_TAG( PT_I8,		0x3802 )
#define PR_LOCK_RESOURCE_VID			PROP_TAG( PT_I8,		0x3803 )
#define PR_LOCK_ENLISTMENT_CONTEXT		PROP_TAG( PT_BINARY,	0x3804 )
#define PR_LOCK_TYPE					PROP_TAG( PT_SHORT,		0x3805 )
#define PR_LOCK_SCOPE					PROP_TAG( PT_SHORT,		0x3806 )
#define PR_LOCK_TRANSIENT_ID			PROP_TAG( PT_BINARY,	0x3807 )
#define PR_LOCK_DEPTH					PROP_TAG( PT_LONG,		0x3808 )
#define PR_LOCK_TIMEOUT					PROP_TAG( PT_LONG,		0x3809 )
#define PR_LOCK_EXPIRY_TIME				PROP_TAG( PT_SYSTIME,	0x380a )
#define PR_LOCK_GLID					PROP_TAG( PT_BINARY,	0x380b )
#define PR_LOCK_NULL_URL_W				PROP_TAG( PT_UNICODE,	0x380c )

#define PR_ANTIVIRUS_VENDOR				PROP_TAG(PT_STRING8,	pidStoreNonTransMin+0x45)
#define PR_ANTIVIRUS_VERSION			PROP_TAG(PT_LONG,		pidStoreNonTransMin+0x46)

#define PR_ANTIVIRUS_SCAN_STATUS		PROP_TAG(PT_LONG,		pidStoreNonTransMin+0x47)

#define PR_ANTIVIRUS_SCAN_INFO			PROP_TAG(PT_STRING8,	pidStoreNonTransMin+0x48)

#define PR_ADDR_TO						PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x57)
#define PR_ADDR_TO_A					PROP_TAG(PT_STRING8, pidStoreNonTransMin+0x57)
#define PR_ADDR_TO_W					PROP_TAG(PT_UNICODE, pidStoreNonTransMin+0x57)

#define PR_ADDR_CC						PROP_TAG(PT_TSTRING, pidStoreNonTransMin+0x58)
#define PR_ADDR_CC_A					PROP_TAG(PT_STRING8, pidStoreNonTransMin+0x58)
#define PR_ADDR_CC_W					PROP_TAG(PT_UNICODE, pidStoreNonTransMin+0x58)

#define pbGlobalProfileSectionGuid	"\x13\xDB\xB0\xC8\xAA\x05\x10\x1A\x9B\xB0\x00\xAA\x00\x2F\xC4\x5A"


/*
 *	IExchangeModifyTable
 *
 *	Used for get/set rules (and access control) on folders.
 *
 */

/* ulRowFlags */
#define ROWLIST_REPLACE		((ULONG)1)
#define ROW_ADD				((ULONG)1)
#define ROW_MODIFY			((ULONG)2)
#define ROW_REMOVE			((ULONG)4)
#define ROW_EMPTY			(ROW_ADD|ROW_REMOVE)

struct ROWENTRY {
	ULONG			ulRowFlags;
	ULONG			cValues;
	LPSPropValue	rgPropVals;
};
typedef struct ROWENTRY *LPROWENTRY;

struct ROWLIST {
	ROWLIST(void) = delete;
	template<typename _T> ROWLIST(std::initializer_list<_T>) = delete;
	ULONG			cEntries;
	ROWENTRY		aEntries[MAPI_DIM];
};
typedef struct ROWLIST *LPROWLIST;

#define CbNewROWLIST(_centries) \
    (offsetof(ROWLIST,aEntries) + (_centries)*sizeof(ROWENTRY))

class IExchangeModifyTable : public virtual IUnknown {
public:
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT __stdcall GetTable(ULONG ulFlags, LPMAPITABLE *lppTable) = 0;
	virtual HRESULT __stdcall ModifyTable(ULONG ulFlags, LPROWLIST lpMods) = 0;
};
IID_OF(IExchangeModifyTable);

typedef IExchangeModifyTable* LPEXCHANGEMODIFYTABLE;

/* --- snip acl stuff --- */

/*
 * Rules specifics
 */

// Property types
#define PT_SRESTRICTION				((ULONG) 0x00FD)
#define PT_ACTIONS					((ULONG) 0x00FE)

/*
 * PT_FILE_HANDLE: real data is in file specified by handle.
 *					prop.Value.l has file handle
 * PT_FILE_EA: real data is in file specified by extended attribute
 *					prop.Value.bin has binary EA data
 * PT_VIRTUAL: real data is computed on the fly.
 *					prop.Value.bin has raw binary virtual property blob that has
 *					information to do conversion. This is internal to the store and
 *					is not supported for outside calls.
 */

#define PT_FILE_HANDLE					((ULONG) 0x0103)
#define PT_FILE_EA						((ULONG) 0x0104)
#define PT_VIRTUAL						((ULONG) 0x0105)

#define FVirtualProp(ptag)			(PROP_TYPE(ptag) == PT_VIRTUAL)
#define FFileHandleProp(ptag)		(PROP_TYPE(ptag) == PT_FILE_HANDLE || PROP_TYPE(ptag) == PT_FILE_EA)

//Properties in rule table
#define PR_RULE_ID						PROP_TAG(PT_I8, pidSpecialMin+0x04)
#define PR_RULE_IDS						PROP_TAG(PT_BINARY, pidSpecialMin+0x05)
#define PR_RULE_SEQUENCE				PROP_TAG(PT_LONG, pidSpecialMin+0x06)
#define PR_RULE_STATE					PROP_TAG(PT_LONG, pidSpecialMin+0x07)
#define PR_RULE_USER_FLAGS				PROP_TAG(PT_LONG, pidSpecialMin+0x08)
#define PR_RULE_CONDITION				PROP_TAG(PT_SRESTRICTION, pidSpecialMin+0x09)
#define PR_RULE_ACTIONS					PROP_TAG(PT_ACTIONS, pidSpecialMin+0x10)
#define PR_RULE_PROVIDER				PROP_TAG(PT_STRING8, pidSpecialMin+0x11)
#define PR_RULE_NAME					PROP_TAG(PT_TSTRING, pidSpecialMin+0x12)
#define PR_RULE_LEVEL					PROP_TAG(PT_LONG, pidSpecialMin+0x13)
#define PR_RULE_PROVIDER_DATA			PROP_TAG(PT_BINARY, pidSpecialMin+0x14)

#define PR_EXTENDED_RULE_ACTIONS		PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x59)
#define PR_EXTENDED_RULE_CONDITION		PROP_TAG(PT_BINARY, pidStoreNonTransMin+0x5a)
#define PR_EXTENDED_RULE_SIZE_LIMIT		PROP_TAG(PT_LONG, pidStoreNonTransMin+0x5b)

// what?
// moved to ptag.h (scottno) - still needed for 2.27 upgrader
// #define	PR_RULE_VERSION				PROP_TAG( PT_I2, pidSpecialMin+0x1D)

//PR_STATE property values
#define ST_DISABLED						0x0000
#define ST_ENABLED						0x0001
#define ST_ERROR						0x0002
#define ST_ONLY_WHEN_OOF				0x0004
#define ST_KEEP_OOF_HIST				0x0008
#define ST_EXIT_LEVEL					0x0010
#define ST_SKIP_IF_SCL_IS_SAFE			0x0020
#define ST_RULE_PARSE_ERROR				0x0040
#define ST_CLEAR_OOF_HIST			0x80000000

//Empty restriction
#define NULL_RESTRICTION	0xff

// special RELOP for Member of DL
#define RELOP_MEMBER_OF_DL	100

//Action types
enum ACTTYPE {
	OP_MOVE = 1,
	OP_COPY,
	OP_REPLY,
	OP_OOF_REPLY,
	OP_DEFER_ACTION,
	OP_BOUNCE,
	OP_FORWARD,
	OP_DELEGATE,
	OP_TAG,
	OP_DELETE,
	OP_MARK_AS_READ
};

// provider name for moderator rules
#define szProviderModeratorRule		"MSFT:MR"
// #define wszProviderModeratorRule	L"MSFT:MR"

// action flavors

// for OP_REPLY
#define DO_NOT_SEND_TO_ORIGINATOR		1
#define STOCK_REPLY_TEMPLATE			2

// for OP_FORWARD
#define FWD_PRESERVE_SENDER				1
#define FWD_DO_NOT_MUNGE_MSG			2
#define FWD_AS_ATTACHMENT				4

//scBounceCode values
#define BOUNCE_MESSAGE_SIZE_TOO_LARGE	(SCODE) MAPI_DIAG_LENGTH_CONSTRAINT_VIOLATD
#define BOUNCE_FORMS_MISMATCH			(SCODE) MAPI_DIAG_RENDITION_UNSUPPORTED
#define BOUNCE_ACCESS_DENIED			(SCODE) MAPI_DIAG_MAIL_REFUSED

//Message class prefix for Reply and OOF Reply templates
#define szReplyTemplateMsgClassPrefix	"IPM.Note.Rules.ReplyTemplate."
#define szOofTemplateMsgClassPrefix		"IPM.Note.Rules.OofTemplate."

//Action structure
struct ACTION {
	ACTTYPE		acttype;

	// to indicate which flavor of the action.
	ULONG		ulActionFlavor;

	// Action restriction
	// currently unused and must be set to NULL
	LPSRestriction	lpRes;

	// currently unused and must be set to NULL.
	LPSPropTagArray lpPropTagArray;

	// User defined flags
	ULONG		ulFlags;

	// padding to align the union on 8 byte boundary
	ULONG		dwAlignPad;

	union {
		// used for OP_MOVE and OP_COPY actions
		struct {
			ULONG		cbStoreEntryId;
			LPENTRYID	lpStoreEntryId;
			ULONG		cbFldEntryId;
			LPENTRYID	lpFldEntryId;
		} actMoveCopy;

		// used for OP_REPLY and OP_OOF_REPLY actions
		struct {
			ULONG		cbEntryId;
			LPENTRYID	lpEntryId;
			GUID		guidReplyTemplate;
		} actReply;

		// used for OP_DEFER_ACTION action
		struct {
			ULONG		cbData;
			BYTE		*pbData;
		} actDeferAction;

		// Error code to set for OP_BOUNCE action
		SCODE			scBounceCode;

		// list of address for OP_FORWARD and OP_DELEGATE action
		LPADRLIST		lpadrlist;

		// prop value for OP_TAG action
		SPropValue		propTag;
	};
};
typedef struct ACTION *LPACTION;

// Rules version
#define EDK_RULES_VERSION		1

//Array of actions
struct ACTIONS {
	ULONG		ulVersion;		// use the #define above
	UINT		cActions;
	LPACTION	lpAction;
};

#ifdef __cplusplus
extern "C" {
#endif
HRESULT HrSerializeSRestriction(IMAPIProp * pprop, LPSRestriction prest, BYTE ** ppbRest, ULONG * pcbRest);
HRESULT HrDeserializeSRestriction(IMAPIProp * pprop, BYTE * pbRest, ULONG cbRest, LPSRestriction * pprest);
HRESULT HrSerializeActions(IMAPIProp * pprop, ACTIONS * pActions, BYTE ** ppbActions, ULONG * pcbActions);
HRESULT HrDeserializeActions(IMAPIProp * pprop, BYTE * pbActions, ULONG cbActions, ACTIONS ** ppActions);
#ifdef __cplusplus
} // extern "C"
#endif

// message class definitions for Deferred Action and Deffered Error messages
#define szDamMsgClass		"IPC.Microsoft Exchange 4.0.Deferred Action"
#define szDemMsgClass		"IPC.Microsoft Exchange 4.0.Deferred Error"
#define szExRuleMsgClass	"IPM.ExtendedRule.Message"
//#define wszExRuleMsgClass	L"IPM.ExtendedRule.Message"

/*
 *	Rule error codes
 *	Values for PR_RULE_ERROR
 */
#define RULE_ERR_UNKNOWN			1			//general catchall error
#define RULE_ERR_LOAD				2			//unable to load folder rules
#define RULE_ERR_DELIVERY			3			//unable to deliver message temporarily
#define RULE_ERR_PARSING			4			//error while parsing
#define RULE_ERR_CREATE_DAE			5			//error creating DAE message
#define RULE_ERR_NO_FOLDER			6			//folder to move/copy doesn't exist
#define RULE_ERR_NO_RIGHTS			7			//no rights to move/copy into folder
#define RULE_ERR_CREATE_DAM			8			//error creating DAM
#define RULE_ERR_NO_SENDAS			9			//can not send as another user
#define RULE_ERR_NO_TEMPLATE		10			//reply template is missing
#define RULE_ERR_EXECUTION			11			//error in rule execution
#define RULE_ERR_QUOTA_EXCEEDED		12			//mailbox quota size exceeded
#define RULE_ERR_TOO_MANY_RECIPS	13			//number of recips exceded upper limit

#define RULE_ERR_FIRST		RULE_ERR_UNKNOWN
#define RULE_ERR_LAST		RULE_ERR_TOO_MANY_RECIPS

/*
 * "IExchangeRuleAction" Interface Declaration
 *
 * Used for get actions from a Deferred Action Message.
 */

class IExchangeRuleAction : public virtual IUnknown {
public:
	virtual HRESULT __stdcall ActionCount(ULONG *lpcActions) = 0;
	virtual HRESULT __stdcall GetAction(ULONG ulActionNumber, LARGE_INTEGER *lpruleid, LPACTION *lppAction) = 0;
};

typedef IExchangeRuleAction* LPEXCHANGERULEACTION;


//Outlook 2007, Blocked Attachments
class IAttachmentSecurity : public IUnknown {
public:
	virtual HRESULT __stdcall IsAttachmentBlocked(LPCWSTR pwszFileName, BOOL *pfBlocked) = 0;
};

struct READSTATE {
	ULONG		cbSourceKey;
	BYTE	*	pbSourceKey;
	ULONG		ulFlags;
};
typedef struct READSTATE *LPREADSTATE;

/*      Special flag bit for DeleteFolder */
#define DELETE_HARD_DELETE                              ((ULONG) 0x00000010)


/*------------------------------------------------------------------------
 *
 *	Errors returned by Exchange Incremental Change Synchronization Interface
 *
 *-----------------------------------------------------------------------*/

#define MAKE_SYNC_E(err)	(MAKE_SCODE(SEVERITY_ERROR, FACILITY_ITF, err ))
#define MAKE_SYNC_W(warn)	(MAKE_SCODE(SEVERITY_SUCCESS, FACILITY_ITF, warn))

#define SYNC_E_UNKNOWN_FLAGS			MAPI_E_UNKNOWN_FLAGS
#define SYNC_E_INVALID_PARAMETER		E_INVALIDARG
#define SYNC_E_ERROR					E_FAIL
#define SYNC_E_OBJECT_DELETED			MAKE_SYNC_E(0x800)
#define SYNC_E_IGNORE					MAKE_SYNC_E(0x801)
#define SYNC_E_CONFLICT					MAKE_SYNC_E(0x802)
#define SYNC_E_NO_PARENT				MAKE_SYNC_E(0x803)
#define SYNC_E_INCEST					MAKE_SYNC_E(0x804)
#define SYNC_E_UNSYNCHRONIZED			MAKE_SYNC_E(0x805)

#define SYNC_W_PROGRESS					MAKE_SYNC_W(0x820)
#define SYNC_W_CLIENT_CHANGE_NEWER		MAKE_SYNC_W(0x821)

/*------------------------------------------------------------------------
 *
 *	Flags used by Exchange Incremental Change Synchronization Interface
 *
 *-----------------------------------------------------------------------*/

#define SYNC_UNICODE				0x01
#define SYNC_NO_DELETIONS			0x02
#define SYNC_NO_SOFT_DELETIONS		0x04
#define SYNC_READ_STATE				0x08
#define SYNC_ASSOCIATED				0x10
#define SYNC_NORMAL					0x20
#define SYNC_NO_CONFLICTS			0x40
#define SYNC_ONLY_SPECIFIED_PROPS	0x80
#define SYNC_NO_FOREIGN_KEYS		0x100
#define SYNC_LIMITED_IMESSAGE		0x200
#define SYNC_CATCHUP				0x400
#define SYNC_NEW_MESSAGE			0x800	// only applicable to ImportMessageChange()
#define SYNC_MSG_SELECTIVE			0x1000	// Used internally.	 Will reject if used by clients.
#define SYNC_BEST_BODY				0x2000
#define SYNC_IGNORE_SPECIFIED_ON_ASSOCIATED 0x4000
#define SYNC_PROGRESS_MODE			0x8000	// AirMapi progress mode
#define SYNC_FXRECOVERMODE			0x10000
#define SYNC_DEFER_CONFIG			0x20000
#define SYNC_FORCE_UNICODE			0x40000	// Forces server to return Unicode properties
#define SYNC_NO_DB_CHANGES			0x80000

/*------------------------------------------------------------------------
 *
 *	Flags used by ImportMessageDeletion and ImportFolderDeletion methods
 *
 *-----------------------------------------------------------------------*/

#define SYNC_SOFT_DELETE			0x01
#define SYNC_EXPIRY					0x02

/*------------------------------------------------------------------------
 *
 *	Flags used by ImportPerUserReadStateChange method
 *
 *-----------------------------------------------------------------------*/

#define SYNC_READ					0x01

class IExchangeExportChanges : public virtual IUnknown {
public:
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT __stdcall Config(LPSTREAM lpStream, ULONG ulFlags, LPUNKNOWN lpCollector, LPSRestriction lpRestriction, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize) = 0;
	virtual HRESULT __stdcall Synchronize(ULONG *pulSteps, ULONG *pulProgress) = 0;
	virtual HRESULT __stdcall UpdateState(LPSTREAM lpStream) = 0;
};

typedef IExchangeExportChanges* LPEXCHANGEEXPORTCHANGES;

class IExchangeImportContentsChanges : public virtual IUnknown {
public:
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT __stdcall Config(LPSTREAM lpStream, ULONG ulFlags) = 0;
	virtual HRESULT __stdcall UpdateState(LPSTREAM lpStream) = 0;
	virtual HRESULT __stdcall ImportMessageChange(ULONG cValue, LPSPropValue lpPropArray, ULONG ulFlags, LPMESSAGE * lppMessage) = 0;
	virtual HRESULT __stdcall ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) = 0;
	virtual HRESULT __stdcall ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState) = 0;
	virtual HRESULT __stdcall ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE *pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE *pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE *pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE *pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage) = 0;
};

typedef IExchangeImportContentsChanges* LPEXCHANGEIMPORTCONTENTSCHANGES;

class IExchangeImportHierarchyChanges : public virtual IUnknown {
public:
    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT __stdcall Config(LPSTREAM lpStream, ULONG ulFlags) = 0;
	virtual HRESULT __stdcall UpdateState(LPSTREAM lpStream) = 0;
	virtual HRESULT __stdcall ImportFolderChange(ULONG cValue, LPSPropValue lpPropArray) = 0;
	virtual HRESULT __stdcall ImportFolderDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) = 0;
};

typedef IExchangeImportHierarchyChanges* LPEXCHANGEIMPORTHIERARCHYCHANGES;

class IProxyStoreObject : public virtual IUnknown {
public:
	virtual HRESULT __stdcall UnwrapNoRef(LPVOID *ppvObject) = 0;
};

typedef IProxyStoreObject* LPPROXYSTOREOBJECT;

#define FS_NONE					0x00 //indicates that the folder does not support sharing.
#define FS_SUPPORTS_SHARING		0x01 //indicates that the folder supports sharing.

// Outlook 2007
// Provides information about a folder's support for sharing.
class IFolderSupport : public virtual IUnknown {
public:
	virtual HRESULT __stdcall GetSupportMask(DWORD *pdwSupportMask) = 0;
};

typedef IFolderSupport* LPIFOLDERSUPPORT;

class IExchangeFavorites : public IUnknown {
public:
    virtual HRESULT __stdcall GetLastError(HRESULT hr, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
    virtual HRESULT __stdcall AddFavorites(LPENTRYLIST lpEntryList) = 0;
    virtual HRESULT __stdcall DelFavorites(LPENTRYLIST lpEntryList) = 0;
};

/* New from Outlook 2010 MAPI Extension */
class IMAPIGetSession : public virtual IUnknown {
public:
    //    virtual ~IMAPIGetSession() = 0;

	virtual HRESULT __stdcall GetMAPISession(LPUNKNOWN *lppSession) = 0;
};

typedef IMAPIGetSession* LPMAPIGETSESSION;


#endif
