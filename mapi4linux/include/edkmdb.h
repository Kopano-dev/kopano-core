/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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

struct IExchangeManageStore : public virtual IUnknown {
public:
	virtual HRESULT CreateStoreEntryID(const TCHAR *store_dn, const TCHAR *mbox_dn, ULONG flags, ULONG *eid_size, ENTRYID **eid) = 0;
	virtual HRESULT EntryIDFromSourceKey(ULONG cFolderKeySize, BYTE *lpFolderSourceKey, ULONG cMessageKeySize, BYTE *lpMessageSourceKey, ULONG *lpcbEntryID, LPENTRYID *lppEntryID) = 0;
	virtual HRESULT GetRights(ULONG ueid_size, const ENTRYID *user_eid, ULONG eid_size, const ENTRYID *eid, ULONG *rights) = 0;
	virtual HRESULT GetMailboxTable(const TCHAR *server, IMAPITable **, ULONG flags) = 0;
	virtual HRESULT GetPublicFolderTable(const TCHAR *server, IMAPITable **, ULONG flags) = 0;
};
IID_OF(IExchangeManageStore)

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

#define PR_NON_IPM_SUBTREE_ENTRYID			PROP_TAG( PT_BINARY, 0x6620)
#define PR_IPM_FAVORITES_ENTRYID			PROP_TAG( PT_BINARY, 0x6630)
#define PR_IPM_PUBLIC_FOLDERS_ENTRYID		PROP_TAG( PT_BINARY, 0x6631)

/* missing PR_* defines for common/ECDebug */
#define PR_PROFILE_VERSION					PROP_TAG(PT_LONG, 0x6600)
#define PR_PROFILE_CONFIG_FLAGS				PROP_TAG(PT_LONG, 0x6601)
#define PR_PROFILE_HOME_SERVER				PROP_TAG(PT_STRING8, 0x6602)
#define PR_PROFILE_HOME_SERVER_DN			PROP_TAG(PT_STRING8, 0x6612)
#define PR_PROFILE_HOME_SERVER_ADDRS		PROP_TAG(PT_MV_STRING8, 0x6613)
#define PR_PROFILE_USER						PROP_TAG(PT_STRING8, 0x6603)
#define PR_PROFILE_CONNECT_FLAGS			PROP_TAG(PT_LONG, 0x6604)
#define PR_PROFILE_TRANSPORT_FLAGS			PROP_TAG(PT_LONG, 0x6605)
#define PR_PROFILE_UI_STATE					PROP_TAG(PT_LONG, 0x6606)
#define PR_PROFILE_UNRESOLVED_NAME			PROP_TAG(PT_STRING8, 0x6607)
#define PR_PROFILE_UNRESOLVED_SERVER		PROP_TAG(PT_STRING8, 0x6608)
#define PR_PROFILE_BINDING_ORDER			PROP_TAG(PT_STRING8, 0x6609)
#define PR_PROFILE_MAX_RESTRICT				PROP_TAG(PT_LONG, 0x660D)
#define PR_PROFILE_AB_FILES_PATH			PROP_TAG(PT_STRING8, 0x660E)
#define PR_PROFILE_OFFLINE_STORE_PATH		PROP_TAG(PT_STRING8, 0x6610)
#define PR_PROFILE_OFFLINE_INFO				PROP_TAG(PT_BINARY, 0x6611)
#define PR_PROFILE_ADDR_INFO				PROP_TAG(PT_BINARY, 0x6687)
#define PR_PROFILE_OPTIONS_DATA				PROP_TAG(PT_BINARY, 0x6689)
#define PR_PROFILE_SECURE_MAILBOX			PROP_TAG(PT_BINARY, 0x67F0)
#define PR_DISABLE_WINSOCK					PROP_TAG(PT_LONG, 0x6618)
#define PR_PROFILE_AUTH_PACKAGE				PROP_TAG(PT_LONG, 0x6619)
#define PR_PROFILE_RECONNECT_INTERVAL		PROP_TAG(PT_LONG, 0x661A)
#define PR_PROFILE_SERVER_VERSION			PROP_TAG(PT_LONG, 0x661B)

#define PR_OST_ENCRYPTION					PROP_TAG(PT_LONG, 0x6702)

#define PR_PROFILE_OPEN_FLAGS				PROP_TAG(PT_LONG, 0x6609)
#define PR_PROFILE_TYPE						PROP_TAG(PT_LONG, 0x660A)
#define PR_PROFILE_MAILBOX					PROP_TAG(PT_STRING8, 0x660B)
#define PR_PROFILE_SERVER					PROP_TAG(PT_STRING8, 0x660C)
#define PR_PROFILE_SERVER_DN				PROP_TAG(PT_STRING8, 0x6614)

#define PR_PROFILE_FAVFLD_DISPLAY_NAME		PROP_TAG(PT_STRING8, 0x660F)
#define PR_PROFILE_FAVFLD_COMMENT			PROP_TAG(PT_STRING8, 0x6615)
#define PR_PROFILE_ALLPUB_DISPLAY_NAME		PROP_TAG(PT_STRING8, 0x6616)
#define PR_PROFILE_ALLPUB_COMMENT			PROP_TAG(PT_STRING8, 0x6617)

#define PR_PROFILE_MOAB						PROP_TAG(PT_STRING8, 0x667B)
#define PR_PROFILE_MOAB_GUID				PROP_TAG(PT_STRING8, 0x667C)
#define PR_PROFILE_MOAB_SEQ					PROP_TAG(PT_LONG, 0x667D)

#define PR_GET_PROPS_EXCLUDE_PROP_ID_LIST	PROP_TAG(PT_BINARY, 0x667E)

#define PR_USER_ENTRYID						PROP_TAG(PT_BINARY, 0x6619)
#define PR_USER_NAME						PROP_TAG( PT_TSTRING, 0x661A)
#define PR_USER_NAME_A						PROP_TAG( PT_STRING8, 0x661A)
#define PR_USER_NAME_W						PROP_TAG( PT_UNICODE, 0x661A)

#define PR_MAILBOX_OWNER_ENTRYID			PROP_TAG(PT_BINARY, 0x661B)
#define PR_MAILBOX_OWNER_NAME				PROP_TAG( PT_TSTRING, 0x661C)
#define PR_MAILBOX_OWNER_NAME_A				PROP_TAG( PT_STRING8, 0x661C)
#define PR_MAILBOX_OWNER_NAME_W				PROP_TAG( PT_UNICODE, 0x661C)
#define PR_OOF_STATE						PROP_TAG(PT_BOOLEAN, 0x661D)

#define PR_HIERARCHY_SERVER					PROP_TAG(PT_TSTRING, 0x6633)

#define PR_SCHEDULE_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, 0x661E)

#define PR_IPM_DAF_ENTRYID					PROP_TAG(PT_BINARY, 0x661F)

#define PR_EFORMS_REGISTRY_ENTRYID				PROP_TAG(PT_BINARY, 0x6621)
#define PR_SPLUS_FREE_BUSY_ENTRYID				PROP_TAG(PT_BINARY, 0x6622)
#define PR_OFFLINE_ADDRBOOK_ENTRYID				PROP_TAG(PT_BINARY, 0x6623)
#define PR_NNTP_CONTROL_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, 0x668B)
#define PR_EFORMS_FOR_LOCALE_ENTRYID			PROP_TAG(PT_BINARY, 0x6624)
#define PR_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID		PROP_TAG(PT_BINARY, 0x6625)
#define PR_ADDRBOOK_FOR_LOCAL_SITE_ENTRYID		PROP_TAG(PT_BINARY, 0x6626)
#define PR_NEWSGROUP_ROOT_FOLDER_ENTRYID		PROP_TAG(PT_BINARY, 0x668C)
#define PR_OFFLINE_MESSAGE_ENTRYID				PROP_TAG(PT_BINARY, 0x6627)
#define PR_FAVORITES_DEFAULT_NAME				PROP_TAG(PT_STRING8, 0x6635)
#define PR_SYS_CONFIG_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, 0x6636)
#define PR_NNTP_ARTICLE_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, 0x668A)
#define PR_EVENTS_ROOT_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, 0x667A)

#define PR_GW_MTSIN_ENTRYID					PROP_TAG(PT_BINARY, 0x6628)
#define PR_GW_MTSOUT_ENTRYID				PROP_TAG(PT_BINARY, 0x6629)
#define PR_TRANSFER_ENABLED					PROP_TAG(PT_BOOLEAN, 0x662A)

#define PR_TEST_LINE_SPEED					PROP_TAG(PT_BINARY, 0x662B)

#define PR_HIERARCHY_SYNCHRONIZER			PROP_TAG(PT_OBJECT, 0x662C)
#define PR_CONTENTS_SYNCHRONIZER			PROP_TAG(PT_OBJECT, 0x662D)
#define PR_COLLECTOR						PROP_TAG(PT_OBJECT, 0x662E)

#define PR_FAST_TRANSFER					PROP_TAG(PT_OBJECT, 0x662F)

#define PR_CHANGE_ADVISOR					PROP_TAG(PT_OBJECT, 0x6634)

#define PR_CHANGE_NOTIFICATION_GUID			PROP_TAG(PT_CLSID, 0x6637)

#define PR_STORE_OFFLINE					PROP_TAG(PT_BOOLEAN, 0x6632)

#define PR_IN_TRANSIT						PROP_TAG(PT_BOOLEAN, 0x6618)

#define PR_REPLICATION_STYLE				PROP_TAG(PT_LONG, 0x6690)
#define PR_REPLICATION_SCHEDULE				PROP_TAG(PT_BINARY, 0x6691)
#define PR_REPLICATION_MESSAGE_PRIORITY 	PROP_TAG(PT_LONG, 0x6692)

#define PR_OVERALL_MSG_AGE_LIMIT			PROP_TAG(PT_LONG, 0x6693)
#define PR_REPLICATION_ALWAYS_INTERVAL		PROP_TAG(PT_LONG, 0x6694)
#define PR_REPLICATION_MSG_SIZE				PROP_TAG(PT_LONG, 0x6695)

#define PR_SOURCE_KEY						PROP_TAG(PT_BINARY, 0x65E0)
#define PR_PARENT_SOURCE_KEY				PROP_TAG(PT_BINARY, 0x65E1)
#define PR_CHANGE_KEY						PROP_TAG(PT_BINARY, 0x65E2)
#define PR_PREDECESSOR_CHANGE_LIST			PROP_TAG(PT_BINARY, 0x65E3)

#define PR_SOURCE_FID						PROP_TAG(PT_I8, 0x0E5F)

#define PR_CATALOG							PROP_TAG(PT_BINARY, 0x0E5B)

#define PR_CI_SEARCH_ENABLED				PROP_TAG(PT_BOOLEAN, 0x0E5C)
#define PR_CI_NOTIFICATION_ENABLED			PROP_TAG(PT_BOOLEAN, 0x0E5D)
#define PR_MAX_CACHED_VIEWS					PROP_TAG(PT_LONG, 0x0E68)
#define PR_MAX_INDICES						PROP_TAG(PT_LONG, 0x0E5E)
#define PR_IMPLIED_RESTRICTIONS				PROP_TAG(PT_MV_BINARY, 0x667F)

#define PR_FOLDER_CHILD_COUNT				PROP_TAG(PT_LONG, 0x6638)
#define PR_RIGHTS							PROP_TAG(PT_LONG, 0x6639)
#define PR_ACL_TABLE						PROP_TAG(PT_OBJECT, 0x3FE0)
#define PR_RULES_TABLE						PROP_TAG(PT_OBJECT, 0x3FE1)
#define PR_HAS_RULES						PROP_TAG(PT_BOOLEAN, 0x663A)
#define PR_HAS_MODERATOR_RULES				PROP_TAG(PT_BOOLEAN, 0x663F )

#define PR_ADDRESS_BOOK_ENTRYID				PROP_TAG(PT_BINARY, 0x663B)

#define PR_ACL_DATA							PROP_TAG(PT_BINARY, 0x3FE0)
#define PR_RULES_DATA						PROP_TAG(PT_BINARY, 0x3FE1)
#define PR_EXTENDED_ACL_DATA				PROP_TAG(PT_BINARY, 0x3FFE)
#define PR_FOLDER_DESIGN_FLAGS				PROP_TAG(PT_LONG, 0x3FE2)
#define PR_DESIGN_IN_PROGRESS				PROP_TAG(PT_BOOLEAN, 0x3FE4)
#define PR_SECURE_ORIGINATION				PROP_TAG(PT_BOOLEAN, 0x3FE5)

#define PR_PUBLISH_IN_ADDRESS_BOOK			PROP_TAG(PT_BOOLEAN, 0x3FE6)
#define PR_RESOLVE_METHOD					PROP_TAG(PT_LONG, 0x3FE7)
#define PR_ADDRESS_BOOK_DISPLAY_NAME		PROP_TAG(PT_TSTRING, 0x3FE8)

#define PR_EFORMS_LOCALE_ID					PROP_TAG(PT_LONG, 0x3FE9)

#define PR_REPLICA_LIST						PROP_TAG(PT_BINARY, 0x6698)
#define PR_OVERALL_AGE_LIMIT				PROP_TAG(PT_LONG, 0x6699)

#define PR_IS_NEWSGROUP_ANCHOR				PROP_TAG(PT_BOOLEAN, 0x6696)
#define PR_IS_NEWSGROUP						PROP_TAG(PT_BOOLEAN, 0x6697)
#define PR_NEWSGROUP_COMPONENT				PROP_TAG(PT_STRING8, 0x66A5)
#define PR_INTERNET_NEWSGROUP_NAME			PROP_TAG(PT_STRING8, 0x66A7)
#define PR_NEWSFEED_INFO					PROP_TAG(PT_BINARY, 0x66A6)

#define PR_PREVENT_MSG_CREATE				PROP_TAG(PT_BOOLEAN, 0x65F4)
#define PR_IMAP_INTERNAL_DATE				PROP_TAG(PT_SYSTIME, 0x65F5)
#define PR_INBOUND_NEWSFEED_DN				PROP_TAG(PT_STRING8, 0x668D)
#define PR_OUTBOUND_NEWSFEED_DN				PROP_TAG(PT_STRING8, 0x668E)
#define PR_INTERNET_CHARSET					PROP_TAG(PT_TSTRING, 0x669A)
#define PR_PUBLIC_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, 0x663C)
#define PR_HIERARCHY_CHANGE_NUM				PROP_TAG(PT_LONG, 0x663E)

#define PR_USER_SID CHANGE_PROP_TYPE(ptagSearchState, PT_BINARY)
#define PR_CREATOR_TOKEN					PR_USER_SID

#define PR_HAS_NAMED_PROPERTIES				PROP_TAG(PT_BOOLEAN, 0x664A)

#define PR_CREATOR_NAME						PROP_TAG(PT_TSTRING, 0x3FF8)
#define PR_CREATOR_NAME_A					PROP_TAG(PT_STRING8, 0x3FF8)
#define PR_CREATOR_NAME_W					PROP_TAG(PT_UNICODE, 0x3FF8)
#define PR_CREATOR_ENTRYID					PROP_TAG(PT_BINARY, 0x3FF9)
#define PR_LAST_MODIFIER_NAME				PROP_TAG(PT_TSTRING, 0x3FFA)
#define PR_LAST_MODIFIER_NAME_A				PROP_TAG(PT_STRING8, 0x3FFA)
#define PR_LAST_MODIFIER_NAME_W				PROP_TAG(PT_UNICODE, 0x3FFA)
#define PR_LAST_MODIFIER_ENTRYID			PROP_TAG(PT_BINARY, 0x3FFB)
#define PR_REPLY_RECIPIENT_SMTP_PROXIES		PROP_TAG(PT_TSTRING, 0x3FFC)
#define PR_REPLY_RECIPIENT_SMTP_PROXIES_A	PROP_TAG(PT_STRING8, 0x3FFC)
#define PR_REPLY_RECIPIENT_SMTP_PROXIES_W	PROP_TAG(PT_UNICODE, 0x3FFC)

#define PR_HAS_DAMS							PROP_TAG(PT_BOOLEAN, 0x3FEA)
#define PR_RULE_TRIGGER_HISTORY				PROP_TAG(PT_BINARY, 0x3FF2)
#define PR_MOVE_TO_STORE_ENTRYID			PROP_TAG(PT_BINARY, 0x3FF3)
#define PR_MOVE_TO_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, 0x3FF4)

#define PR_REPLICA_SERVER					PROP_TAG(PT_TSTRING, 0x6644)
#define PR_REPLICA_VERSION					PROP_TAG(PT_I8, 0x664B)

#define PR_CREATOR_SID						PROP_TAG(PT_BINARY, 0x0E58)
#define PR_LAST_MODIFIER_SID				PROP_TAG(PT_BINARY, 0x0E59)
#define PR_SENDER_SID						PROP_TAG(PT_BINARY, 0x0E4D)
#define PR_SENT_REPRESENTING_SID			PROP_TAG(PT_BINARY, 0x0E4E)
#define PR_ORIGINAL_SENDER_SID				PROP_TAG(PT_BINARY, 0x0E4F)
#define PR_ORIGINAL_SENT_REPRESENTING_SID	PROP_TAG(PT_BINARY, 0x0E50)
#define PR_READ_RECEIPT_SID					PROP_TAG(PT_BINARY, 0x0E51)
#define PR_REPORT_SID						PROP_TAG(PT_BINARY, 0x0E52)
#define PR_ORIGINATOR_SID					PROP_TAG(PT_BINARY, 0x0E53)
#define PR_REPORT_DESTINATION_SID			PROP_TAG(PT_BINARY, 0x0E54)
#define PR_ORIGINAL_AUTHOR_SID				PROP_TAG(PT_BINARY, 0x0E55)
#define PR_RECEIVED_BY_SID					PROP_TAG(PT_BINARY, 0x0E56)
#define PR_RCVD_REPRESENTING_SID			PROP_TAG(PT_BINARY, 0x0E57)

#define PR_TRUST_SENDER_NO					0x00000000L
#define PR_TRUST_SENDER_YES					0x00000001L
#define PR_TRUST_SENDER						PROP_TAG(PT_LONG, 0x0E79)

#define PR_CREATOR_SID_AS_XML						PROP_TAG(PT_TSTRING, 0x0E6C)
#define PR_LAST_MODIFIER_SID_AS_XML					PROP_TAG(PT_TSTRING, 0x0E6D)
#define PR_SENDER_SID_AS_XML						PROP_TAG(PT_TSTRING, 0x0E6E)
#define PR_SENT_REPRESENTING_SID_AS_XML				PROP_TAG(PT_TSTRING, 0x0E6F)
#define PR_ORIGINAL_SENDER_SID_AS_XML				PROP_TAG(PT_TSTRING, 0x0E70)
#define PR_ORIGINAL_SENT_REPRESENTING_SID_AS_XML	PROP_TAG(PT_TSTRING, 0x0E71)
#define PR_READ_RECEIPT_SID_AS_XML					PROP_TAG(PT_TSTRING, 0x0E72)
#define PR_REPORT_SID_AS_XML						PROP_TAG(PT_TSTRING, 0x0E73)
#define PR_ORIGINATOR_SID_AS_XML					PROP_TAG(PT_TSTRING, 0x0E74)
#define PR_REPORT_DESTINATION_SID_AS_XML			PROP_TAG(PT_TSTRING, 0x0E75)
#define PR_ORIGINAL_AUTHOR_SID_AS_XML				PROP_TAG(PT_TSTRING, 0x0E76)
#define PR_RECEIVED_BY_SID_AS_XML					PROP_TAG(PT_TSTRING, 0x0E77)
#define PR_RCVD_REPRESENTING_SID_AS_XML				PROP_TAG(PT_TSTRING, 0x0E78)

#define PR_MERGE_MIDSET_DELETED			PROP_TAG(PT_BINARY, 0x0E7A)
#define PR_RESERVE_RANGE_OF_IDS			PROP_TAG(PT_BINARY, 0x0E7B)

#define PR_FID_VID						PROP_TAG(PT_BINARY, 0x664C)
//#define PR_FID_MID						PR_FID_VID	 //NSK : temporary to allow transition

#define PR_ORIGIN_ID					PROP_TAG(PT_BINARY, 0x664D)

#define PR_RANK							PROP_TAG(PT_LONG, 0x6712 )

#define PR_MSG_FOLD_TIME				PROP_TAG(PT_SYSTIME, 0x6654)
#define PR_ICS_CHANGE_KEY				PROP_TAG(PT_BINARY, 0x6655)

#define PR_DEFERRED_SEND_NUMBER			PROP_TAG(PT_LONG, 0x3FEB)
#define PR_DEFERRED_SEND_UNITS			PROP_TAG(PT_LONG, 0x3FEC)
#define PR_EXPIRY_NUMBER				PROP_TAG(PT_LONG, 0x3FED)
#define PR_EXPIRY_UNITS					PROP_TAG(PT_LONG, 0x3FEE)

#define PR_DEFERRED_SEND_TIME			PROP_TAG(PT_SYSTIME, 0x3FEF)
#define PR_GW_ADMIN_OPERATIONS			PROP_TAG(PT_LONG, 0x6658)

#define PR_P1_CONTENT					PROP_TAG(PT_BINARY, 0x1100)
#define PR_P1_CONTENT_TYPE				PROP_TAG(PT_BINARY, 0x1101)

#define PR_CLIENT_ACTIONS				PROP_TAG(PT_BINARY, 0x6645)
#define PR_DAM_ORIGINAL_ENTRYID			PROP_TAG(PT_BINARY, 0x6646)
#define PR_DAM_BACK_PATCHED				PROP_TAG(PT_BOOLEAN, 0x6647)

#define PR_RULE_ERROR					PROP_TAG(PT_LONG, 0x6648)
#define PR_RULE_ACTION_TYPE				PROP_TAG(PT_LONG, 0x6649)
#define PR_RULE_ACTION_NUMBER			PROP_TAG(PT_LONG, 0x6650)
#define PR_RULE_FOLDER_ENTRYID			PROP_TAG(PT_BINARY, 0x6651)

#define PR_INTERNET_CONTENT				PROP_TAG(PT_BINARY, 0x6659)
#define PR_INTERNET_CONTENT_HANDLE		PROP_TAG(PT_FILE_HANDLE, 0x6659)
#define PR_INTERNET_CONTENT_EA			PROP_TAG(PT_FILE_EA, 0x6659)

#define PR_DOTSTUFF_STATE				PROP_TAG(PT_LONG, pidUserNonTransmitMin+0x1)
#define PR_MIME_SIZE					PROP_TAG(PT_LONG, 0x6746)
#define PR_MIME_SIZE_EXTENDED			PROP_TAG(PT_I8, 0x6746)
#define PR_FILE_SIZE					PROP_TAG(PT_LONG, 0x6747)
#define PR_FILE_SIZE_EXTENDED			PROP_TAG(PT_I8, 0x6747)
#define PR_MSG_EDITOR_FORMAT			PROP_TAG(PT_LONG, 0x5909)

#define PR_CONVERSION_STATE CHANGE_PROP_TYPE(ptagAdminNickName, PT_LONG)
#define PR_HTML CHANGE_PROP_TYPE(PR_BODY_HTML, PT_BINARY)
#define PR_ACTIVE_USER_ENTRYID			PROP_TAG(PT_BINARY, 0x6652)
#define PR_CONFLICT_ENTRYID				PROP_TAG(PT_BINARY, 0x3FF0)
#define PR_MESSAGE_LOCALE_ID			PROP_TAG(PT_LONG, 0x3FF1)
#define PR_MESSAGE_CODEPAGE				PROP_TAG(PT_LONG, 0x3FFD)
#define PR_STORAGE_QUOTA_LIMIT			PROP_TAG(PT_LONG, 0x3FF5)
#define PR_EXCESS_STORAGE_USED			PROP_TAG(PT_LONG, 0x3FF6)
#define PR_SVR_GENERATING_QUOTA_MSG		PROP_TAG(PT_TSTRING, 0x3FF7)
#define PR_DELEGATED_BY_RULE			PROP_TAG(PT_BOOLEAN, 0x3FE3)
#define PR_X400_ENVELOPE_TYPE			PROP_TAG(PT_LONG, 0x6653)
#define PR_AUTO_RESPONSE_SUPPRESS		PROP_TAG(PT_LONG, 0x3FDF)
#define PR_INTERNET_CPID				PROP_TAG(PT_LONG, 0x3FDE)
#define PR_SYNCEVENT_FIRED				PROP_TAG(PT_BOOLEAN, 0x664F)
#define PR_IN_CONFLICT					PROP_TAG(PT_BOOLEAN, 0x666C)

#define PR_DELETED_ON							PROP_TAG(PT_SYSTIME, 0x668F)
#define PR_DELETED_MSG_COUNT					PROP_TAG(PT_LONG, 0x6640)
#define PR_DELETED_ASSOC_MSG_COUNT				PROP_TAG(PT_LONG, 0x6643)
#define PR_DELETED_FOLDER_COUNT					PROP_TAG(PT_LONG, 0x6641)
#define PR_OLDEST_DELETED_ON					PROP_TAG(PT_SYSTIME, 0x6642)

#define PR_DELETED_MESSAGE_SIZE_EXTENDED		PROP_TAG(PT_I8, 0x669B)
#define PR_DELETED_NORMAL_MESSAGE_SIZE_EXTENDED PROP_TAG(PT_I8, 0x669C)
#define PR_DELETED_ASSOC_MESSAGE_SIZE_EXTENDED	PROP_TAG(PT_I8, 0x669D)

#define PR_RETENTION_AGE_LIMIT					PROP_TAG(PT_LONG, 0x66C4)
#define PR_DISABLE_PERUSER_READ					PROP_TAG(PT_BOOLEAN, 0x66C5)
#define PR_LAST_FULL_BACKUP						PROP_TAG(PT_SYSTIME, 0x6685)

#define PR_URL_NAME						PROP_TAG(PT_TSTRING, 0x6707)
#define PR_URL_NAME_A					PROP_TAG(PT_STRING8, 0x6707)
#define PR_URL_NAME_W					PROP_TAG(PT_UNICODE, 0x6707)

#define PR_URL_COMP_NAME				PROP_TAG(PT_TSTRING, 0x10F3)
#define PR_URL_COMP_NAME_A				PROP_TAG(PT_STRING8, 0x10F3)
#define PR_URL_COMP_NAME_W				PROP_TAG(PT_UNICODE, 0x10F3)

#define PR_PARENT_URL_NAME				PROP_TAG(PT_TSTRING, 0x670D)
#define PR_PARENT_URL_NAME_A			PROP_TAG(PT_STRING8, 0x670D)
#define PR_PARENT_URL_NAME_W			PROP_TAG(PT_UNICODE, 0x670D)

#define PR_FLAT_URL_NAME				PROP_TAG(PT_TSTRING, 0x670E)
#define PR_FLAT_URL_NAME_A				PROP_TAG(PT_STRING8, 0x670E)
#define PR_FLAT_URL_NAME_W				PROP_TAG(PT_UNICODE, 0x670E)

#define PR_SRC_URL_NAME					PROP_TAG(PT_TSTRING, 0x670F)
#define PR_SRC_URL_NAME_A				PROP_TAG(PT_STRING8, 0x670F)
#define PR_SRC_URL_NAME_W				PROP_TAG(PT_UNICODE, 0x670F)

#define PR_SECURE_IN_SITE				PROP_TAG(PT_BOOLEAN, 0x669E)
#define PR_LOCAL_COMMIT_TIME			PROP_TAG(PT_SYSTIME, 0x6709)
#define PR_LOCAL_COMMIT_TIME_MAX		PROP_TAG(PT_SYSTIME, 0x670A)

#define PR_DELETED_COUNT_TOTAL			PROP_TAG(PT_LONG, 0x670B)

#define PR_AUTO_RESET					PROP_TAG(PT_MV_CLSID, 0x670C)

#define PR_LONGTERM_ENTRYID_FROM_TABLE	PROP_TAG(PT_BINARY, 0x6670)

#define PR_SUBFOLDER					PROP_TAG(PT_BOOLEAN, 0x6708)

/* ATTN: new property types */
#define PT_SRESTRICTION					((ULONG) 0x00FD)
#define PT_ACTIONS						((ULONG) 0x00FE)

#define PR_ORIGINATOR_NAME				PROP_TAG( PT_TSTRING, 0x665B)
#define PR_ORIGINATOR_ADDR				PROP_TAG( PT_TSTRING, 0x665C)
#define PR_ORIGINATOR_ADDRTYPE			PROP_TAG( PT_TSTRING, 0x665D)
#define PR_ORIGINATOR_ENTRYID			PROP_TAG( PT_BINARY, 0x665E)
#define PR_ARRIVAL_TIME					PROP_TAG( PT_SYSTIME, 0x665F)
#define PR_TRACE_INFO					PROP_TAG( PT_BINARY, 0x6660)
#define PR_INTERNAL_TRACE_INFO			PROP_TAG( PT_BINARY, 0x666A)
#define PR_SUBJECT_TRACE_INFO			PROP_TAG( PT_BINARY, 0x6661)
#define PR_RECIPIENT_NUMBER				PROP_TAG( PT_LONG, 0x6662)
#define PR_MTS_SUBJECT_ID				PROP_TAG(PT_BINARY, 0x6663)
#define PR_REPORT_DESTINATION_NAME		PROP_TAG(PT_TSTRING, 0x6664)
#define PR_REPORT_DESTINATION_ENTRYID	PROP_TAG(PT_BINARY, 0x6665)
#define PR_CONTENT_SEARCH_KEY			PROP_TAG(PT_BINARY, 0x6666)
#define PR_FOREIGN_ID					PROP_TAG(PT_BINARY, 0x6667)
#define PR_FOREIGN_REPORT_ID			PROP_TAG(PT_BINARY, 0x6668)
#define PR_FOREIGN_SUBJECT_ID			PROP_TAG(PT_BINARY, 0x6669)
#define PR_PROMOTE_PROP_ID_LIST			PROP_TAG(PT_BINARY, 0x666B)
#define PR_MTS_ID						PR_MESSAGE_SUBMISSION_ID
#define PR_MTS_REPORT_ID				PR_MESSAGE_SUBMISSION_ID

#define PR_MEMBER_ID					PROP_TAG(PT_I8, 0x6671)
#define PR_MEMBER_NAME					PROP_TAG(PT_TSTRING, 0x6672)
#define PR_MEMBER_ENTRYID				PR_ENTRYID
#define PR_MEMBER_RIGHTS				PROP_TAG(PT_LONG, 0x6673)

#define PR_RULE_ID						PROP_TAG(PT_I8, 0x6674)
#define PR_RULE_IDS						PROP_TAG(PT_BINARY, 0x6675)
#define PR_RULE_SEQUENCE				PROP_TAG(PT_LONG, 0x6676)
#define PR_RULE_STATE					PROP_TAG(PT_LONG, 0x6677)
#define PR_RULE_USER_FLAGS				PROP_TAG(PT_LONG, 0x6678)
#define PR_RULE_CONDITION				PROP_TAG(PT_SRESTRICTION, 0x6679)
#define PR_RULE_ACTIONS					PROP_TAG(PT_ACTIONS, 0x6680)
#define PR_RULE_PROVIDER				PROP_TAG(PT_STRING8, 0x6681)
#define PR_RULE_NAME					PROP_TAG(PT_TSTRING, 0x6682)
#define PR_RULE_LEVEL					PROP_TAG(PT_LONG, 0x6683)
#define PR_RULE_PROVIDER_DATA			PROP_TAG(PT_BINARY, 0x6684)

#define PR_EXTENDED_RULE_ACTIONS		PROP_TAG(PT_BINARY, 0x0E99)
#define PR_EXTENDED_RULE_CONDITION		PROP_TAG(PT_BINARY, 0x0E9A)
#define PR_EXTENDED_RULE_SIZE_LIMIT		PROP_TAG(PT_LONG, 0x0E9B)

#define PR_NT_USER_NAME					PROP_TAG(PT_TSTRING, 0x66A0)

#define PR_LAST_LOGON_TIME				PROP_TAG(PT_SYSTIME, 0x66A2 )
#define PR_LAST_LOGOFF_TIME				PROP_TAG(PT_SYSTIME, 0x66A3 )
#define PR_STORAGE_LIMIT_INFORMATION	PROP_TAG(PT_LONG, 0x66A4 )

#define PR_INTERNET_MDNS CHANGE_PROP_TYPE(PR_NEWSGROUP_COMPONENT, PT_BOOLEAN)

#define PR_QUOTA_WARNING_THRESHOLD		PROP_TAG(PT_LONG, 0x6721)
#define PR_QUOTA_SEND_THRESHOLD			PROP_TAG(PT_LONG, 0x6722)
#define PR_QUOTA_RECEIVE_THRESHOLD		PROP_TAG(PT_LONG, 0x6723)

#define PR_FOLDER_FLAGS							PROP_TAG(PT_LONG, 0x66A8)
#define PR_LAST_ACCESS_TIME						PROP_TAG(PT_SYSTIME, 0x66A9)
#define PR_RESTRICTION_COUNT					PROP_TAG(PT_LONG, 0x66AA)
#define PR_CATEG_COUNT							PROP_TAG(PT_LONG, 0x66AB)
#define PR_CACHED_COLUMN_COUNT					PROP_TAG(PT_LONG, 0x66AC)
#define PR_NORMAL_MSG_W_ATTACH_COUNT			PROP_TAG(PT_LONG, 0x66AD)
#define PR_ASSOC_MSG_W_ATTACH_COUNT				PROP_TAG(PT_LONG, 0x66AE)
#define PR_RECIPIENT_ON_NORMAL_MSG_COUNT		PROP_TAG(PT_LONG, 0x66AF)
#define PR_RECIPIENT_ON_ASSOC_MSG_COUNT			PROP_TAG(PT_LONG, 0x66B0)
#define PR_ATTACH_ON_NORMAL_MSG_COUNT			PROP_TAG(PT_LONG, 0x66B1)
#define PR_ATTACH_ON_ASSOC_MSG_COUNT			PROP_TAG(PT_LONG, 0x66B2)
#define PR_NORMAL_MESSAGE_SIZE					PROP_TAG(PT_LONG, 0x66B3)
#define PR_NORMAL_MESSAGE_SIZE_EXTENDED			PROP_TAG(PT_I8, 0x66B3)
#define PR_ASSOC_MESSAGE_SIZE					PROP_TAG(PT_LONG, 0x66B4)
#define PR_ASSOC_MESSAGE_SIZE_EXTENDED			PROP_TAG(PT_I8, 0x66B4)
#define PR_FOLDER_PATHNAME						PROP_TAG(PT_TSTRING, 0x66B5)
#define PR_OWNER_COUNT							PROP_TAG(PT_LONG, 0x66B6)
#define PR_CONTACT_COUNT						PROP_TAG(PT_LONG, 0x66B7)

#define PR_PF_OVER_HARD_QUOTA_LIMIT				PROP_TAG(PT_LONG, 0x6721)
#define PR_PF_MSG_SIZE_LIMIT					PROP_TAG(PT_LONG, 0x6722)

#define PR_PF_DISALLOW_MDB_WIDE_EXPIRY			PROP_TAG(PT_BOOLEAN, 0x6723)

#define PR_LOCALE_ID					PROP_TAG(PT_LONG, 0x66A1)
#define PR_CODE_PAGE_ID					PROP_TAG(PT_LONG, 0x66C3)
#define PR_SORT_LOCALE_ID				PROP_TAG(PT_LONG, 0x6705)

#define PR_MESSAGE_SIZE_EXTENDED CHANGE_PROP_TYPE(PR_MESSAGE_SIZE, PT_I8)

#define PR_AUTO_ADD_NEW_SUBS			PROP_TAG(PT_BOOLEAN, 0x65E5)
#define PR_NEW_SUBS_GET_AUTO_ADD		PROP_TAG(PT_BOOLEAN, 0x65E6)

#define PR_OFFLINE_FLAGS				PROP_TAG(PT_LONG, 0x663D)
#define PR_SYNCHRONIZE_FLAGS			PROP_TAG(PT_LONG, 0x65E4)

#define PR_MESSAGE_SITE_NAME			PROP_TAG(PT_TSTRING, 0x65E7)
#define PR_MESSAGE_SITE_NAME_A			PROP_TAG(PT_STRING8, 0x65E7)
#define PR_MESSAGE_SITE_NAME_W			PROP_TAG(PT_UNICODE, 0x65E7)

#define PR_MESSAGE_PROCESSED			PROP_TAG(PT_BOOLEAN, 0x65E8)

#define PR_MSG_BODY_ID					PROP_TAG(PT_LONG, 0x3FDD)

#define PR_BILATERAL_INFO				PROP_TAG(PT_BINARY, 0x3FDC)
#define PR_DL_REPORT_FLAGS				PROP_TAG(PT_LONG, 0x3FDB)

#define PR_ABSTRACT						PROP_TAG(PT_TSTRING, 0x3FDA)
#define PR_ABSTRACT_A					PROP_TAG(PT_STRING8, 0x3FDA)
#define PR_ABSTRACT_W					PROP_TAG(PT_UNICODE, 0x3FDA)

#define PR_PREVIEW						PROP_TAG(PT_TSTRING, 0x3FD9)
#define PR_PREVIEW_A					PROP_TAG(PT_STRING8, 0x3FD9)
#define PR_PREVIEW_W					PROP_TAG(PT_UNICODE, 0x3FD9)

#define PR_PREVIEW_UNREAD				PROP_TAG(PT_TSTRING, 0x3FD8)
#define PR_PREVIEW_UNREAD_A				PROP_TAG(PT_STRING8, 0x3FD8)
#define PR_PREVIEW_UNREAD_W				PROP_TAG(PT_UNICODE, 0x3FD8)

#define PR_DISABLE_FULL_FIDELITY		PROP_TAG(PT_BOOLEAN, 0x10F2)

#define PR_ATTR_HIDDEN					PROP_TAG(PT_BOOLEAN, 0x10F4)
#define PR_ATTR_SYSTEM					PROP_TAG(PT_BOOLEAN, 0x10F5)
#define PR_ATTR_READONLY				PROP_TAG(PT_BOOLEAN, 0x10F6)

#define PR_READ							PROP_TAG(PT_BOOLEAN, 0x0E69)

#define PR_ADMIN_SECURITY_DESCRIPTOR	PROP_TAG(PT_BINARY, 0x3d21)
#define PR_WIN32_SECURITY_DESCRIPTOR	PROP_TAG(PT_BINARY, 0x3d22)
#define PR_NON_WIN32_ACL				PROP_TAG(PT_BOOLEAN, 0x3d23)

#define PR_ITEM_LEVEL_ACL				PROP_TAG(PT_BOOLEAN, 0x3d24)

#define PR_DAV_TRANSFER_SECURITY_DESCRIPTOR		PROP_TAG(PT_BINARY, 0x0E84)

#define PR_NT_SECURITY_DESCRIPTOR_AS_XML			PROP_TAG(PT_TSTRING, 0x0E6A)
#define PR_NT_SECURITY_DESCRIPTOR_AS_XML_A			PROP_TAG(PT_STRING8, 0x0E6A)
#define PR_NT_SECURITY_DESCRIPTOR_AS_XML_W			PROP_TAG(PT_UNICODE, 0x0E6A)
#define PR_ADMIN_SECURITY_DESCRIPTOR_AS_XML			PROP_TAG(PT_TSTRING, 0x0E6B)
#define PR_ADMIN_SECURITY_DESCRIPTOR_AS_XML_A		PROP_TAG(PT_STRING8, 0x0E6B)
#define PR_ADMIN_SECURITY_DESCRIPTOR_AS_XML_W		PROP_TAG(PT_UNICODE, 0x0E6B)

#define PR_OWA_URL						PROP_TAG (PT_STRING8, 0x10F1)

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

#define PR_ANTIVIRUS_VENDOR				PROP_TAG(PT_STRING8, 0x0E85)
#define PR_ANTIVIRUS_VERSION			PROP_TAG(PT_LONG, 0x0E86)

#define PR_ANTIVIRUS_SCAN_STATUS		PROP_TAG(PT_LONG, 0x0E87)

#define PR_ANTIVIRUS_SCAN_INFO			PROP_TAG(PT_STRING8, 0x0E88)

#define PR_ADDR_TO						PROP_TAG(PT_TSTRING, 0x0E97)
#define PR_ADDR_TO_A					PROP_TAG(PT_STRING8, 0x0E97)
#define PR_ADDR_TO_W					PROP_TAG(PT_UNICODE, 0x0E97)

#define PR_ADDR_CC						PROP_TAG(PT_TSTRING, 0x0E98)
#define PR_ADDR_CC_A					PROP_TAG(PT_STRING8, 0x0E98)
#define PR_ADDR_CC_W					PROP_TAG(PT_UNICODE, 0x0E98)

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
	ULONG ulRowFlags, cValues;
	LPSPropValue	rgPropVals;
};
typedef struct ROWENTRY *LPROWENTRY;

struct ROWLIST {
	ROWLIST(void) = delete;
	template<typename T> ROWLIST(std::initializer_list<T>) = delete;
	ULONG			cEntries;
	ROWENTRY		aEntries[MAPI_DIM];
};
typedef struct ROWLIST *LPROWLIST;

#define CbNewROWLIST(centries) \
	(offsetof(ROWLIST, aEntries) + (centries) * sizeof(ROWENTRY))

struct IExchangeModifyTable : public virtual IUnknown {
public:
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT GetTable(ULONG ulFlags, LPMAPITABLE *lppTable) = 0;
	virtual HRESULT ModifyTable(ULONG ulFlags, LPROWLIST lpMods) = 0;
};
IID_OF(IExchangeModifyTable)

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
#define PR_RULE_ID						PROP_TAG(PT_I8, 0x6674)
#define PR_RULE_IDS						PROP_TAG(PT_BINARY, 0x6675)
#define PR_RULE_SEQUENCE				PROP_TAG(PT_LONG, 0x6676)
#define PR_RULE_STATE					PROP_TAG(PT_LONG, 0x6677)
#define PR_RULE_USER_FLAGS				PROP_TAG(PT_LONG, 0x6678)
#define PR_RULE_CONDITION				PROP_TAG(PT_SRESTRICTION, 0x6679)
#define PR_RULE_ACTIONS					PROP_TAG(PT_ACTIONS, 0x6680)
#define PR_RULE_PROVIDER				PROP_TAG(PT_STRING8, 0x6681)
#define PR_RULE_NAME					PROP_TAG(PT_TSTRING, 0x6682)
#define PR_RULE_LEVEL					PROP_TAG(PT_LONG, 0x6683)
#define PR_RULE_PROVIDER_DATA			PROP_TAG(PT_BINARY, 0x6684)

#define PR_EXTENDED_RULE_ACTIONS		PROP_TAG(PT_BINARY, 0x0E99)
#define PR_EXTENDED_RULE_CONDITION		PROP_TAG(PT_BINARY, 0x0E9A)
#define PR_EXTENDED_RULE_SIZE_LIMIT		PROP_TAG(PT_LONG, 0x0E9B)

// what?
// moved to ptag.h (scottno) - still needed for 2.27 upgrader
// #define	PR_RULE_VERSION				PROP_TAG( PT_I2, 0x668D)

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
#define RULE_ERR_TOO_MANY_RECIPS	13			//number of recips exceeded upper limit

#define RULE_ERR_FIRST		RULE_ERR_UNKNOWN
#define RULE_ERR_LAST		RULE_ERR_TOO_MANY_RECIPS

/*
 * "IExchangeRuleAction" Interface Declaration
 *
 * Used for get actions from a Deferred Action Message.
 */
struct IExchangeRuleAction : public virtual IUnknown {
public:
	virtual HRESULT ActionCount(ULONG *lpcActions) = 0;
	virtual HRESULT GetAction(ULONG ulActionNumber, LARGE_INTEGER *lpruleid, LPACTION *lppAction) = 0;
};

typedef IExchangeRuleAction* LPEXCHANGERULEACTION;

//Outlook 2007, Blocked Attachments
struct IAttachmentSecurity : public virtual IUnknown {
public:
	virtual HRESULT IsAttachmentBlocked(LPCWSTR pwszFileName, BOOL *pfBlocked) = 0;
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

struct IExchangeExportChanges : public virtual IUnknown {
public:
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT Config(LPSTREAM lpStream, ULONG ulFlags, LPUNKNOWN lpCollector, LPSRestriction lpRestriction, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize) = 0;
	virtual HRESULT Synchronize(ULONG *pulSteps, ULONG *pulProgress) = 0;
	virtual HRESULT UpdateState(LPSTREAM lpStream) = 0;
};

typedef IExchangeExportChanges* LPEXCHANGEEXPORTCHANGES;

struct IExchangeImportContentsChanges : public virtual IUnknown {
public:
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT Config(LPSTREAM lpStream, ULONG ulFlags) = 0;
	virtual HRESULT UpdateState(LPSTREAM lpStream) = 0;
	virtual HRESULT ImportMessageChange(ULONG cValue, LPSPropValue lpPropArray, ULONG ulFlags, LPMESSAGE * lppMessage) = 0;
	virtual HRESULT ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) = 0;
	virtual HRESULT ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState) = 0;
	virtual HRESULT ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE *pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE *pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE *pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE *pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage) = 0;
};

typedef IExchangeImportContentsChanges* LPEXCHANGEIMPORTCONTENTSCHANGES;

struct IExchangeImportHierarchyChanges : public virtual IUnknown {
public:
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT Config(LPSTREAM lpStream, ULONG ulFlags) = 0;
	virtual HRESULT UpdateState(LPSTREAM lpStream) = 0;
	virtual HRESULT ImportFolderChange(ULONG cValue, LPSPropValue lpPropArray) = 0;
	virtual HRESULT ImportFolderDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) = 0;
};

typedef IExchangeImportHierarchyChanges* LPEXCHANGEIMPORTHIERARCHYCHANGES;

struct IProxyStoreObject : public virtual IUnknown {
public:
	virtual HRESULT UnwrapNoRef(LPVOID *ppvObject) = 0;
};

typedef IProxyStoreObject* LPPROXYSTOREOBJECT;

#define FS_NONE					0x00 //indicates that the folder does not support sharing.
#define FS_SUPPORTS_SHARING		0x01 //indicates that the folder supports sharing.

// Outlook 2007
// Provides information about a folder's support for sharing.
struct IFolderSupport : public virtual IUnknown {
public:
	virtual HRESULT GetSupportMask(DWORD *pdwSupportMask) = 0;
};

typedef IFolderSupport* LPIFOLDERSUPPORT;

struct IExchangeFavorites : public virtual IUnknown {
public:
	virtual HRESULT GetLastError(HRESULT hr, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT AddFavorites(LPENTRYLIST lpEntryList) = 0;
	virtual HRESULT DelFavorites(LPENTRYLIST lpEntryList) = 0;
};

/* New from Outlook 2010 MAPI Extension */
struct IMAPIGetSession : public virtual IUnknown {
public:
	virtual HRESULT GetMAPISession(LPUNKNOWN *lppSession) = 0;
};

typedef IMAPIGetSession* LPMAPIGETSESSION;

#endif
