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

#ifndef KC_KCORE_HPP
#define KC_KCORE_HPP 1

#include <cstdint>
#include <mapi.h>
#include <mapidefs.h>
#include <kopano/ECTags.h>

// We have 2 types of entryids: those of objects, and those of stores.
// Objects have a store-relative path, however they do have a GUID to make
// sure that we can differentiate 2 entryids from different stores
//
// The ulType fields makes sure that we can read whether it is a store EID
// or an object EID. This is used when opening a store's properties.

// This is our EntryID struct. The first abFlags[] array is required
// by MAPI, other fields in the struct can be anything you like

// Version is our internal version so we can differentiate them in later
// versions

// The ulId field is simply the ID of the record in the database hierarchy
// table. For stores, this is the ID of the msgstore record in the database
// (also the hierarchy table). Each record in on the server has a different
// ulId, even across different stores.
//
// We can differentiate two EntryIDs of 2 different servers with the same
// ulId because the guid is different. (This guid is different per server)
//
// When this is a store EID, the szServer field is also set, and the ulId
// points to the top-level object for the store. The other fields are the same.

/*
 * Dynamic-size structure view (instantiation forbidden) to interpret arbitrary
 * v1 EIDs. (v1 EIDs are used from ZCP 6 onwards.)
 *
 * The typical form that they are generated with is a 48-byte form with
 * szServer="\0\0\0\0" (EID_FIXED). Many places in KC somewhat arbitrarily only
 * accept that form.
 */
struct EID {
	BYTE abFlags[4]{};
	GUID guid{}; /* StoreGuid */
	uint32_t ulVersion = 1;
	uint16_t usType = 0;
	uint16_t usFlags = 0; /* Before Zarafa 7.1, ulFlags did not exist, and ulType was ULONG */
	GUID uniqueId{};
	char szServer[];

	EID(EID &&) = delete;
};

/* 48-byte fixed-size structure used for constructing standard (unwrapped) v1 EIDs */
struct EID_FIXED {
	BYTE abFlags[4]{};
	GUID guid{}; /* StoreGuid */
	uint32_t ulVersion = 1;
	uint16_t usType = 0;
	uint16_t usFlags = 0; /* Before Zarafa 7.1, ulFlags did not exist, and ulType was ULONG */
	GUID uniqueId{};
	char pad[4]{};

	EID_FIXED() = default;
	EID_FIXED(unsigned short type, const GUID &g, const GUID &id, unsigned short flags = 0) :
		guid(g), usType(type), usFlags(flags), uniqueId(id)
	{}
};

/*
 * Dynamic-size structure view (instantiation forbidden) to interpret arbitrary
 * v0 EIDs.
 *
 * The typical form that they are generated with is a 36-byte form with
 * szServer="\0\0\0\0". Many places in KC somewhat arbitrarily only accept that
 * form.
 */
struct EID_V0 {
	BYTE abFlags[4]{};
	GUID guid{}; /* StoreGuid */
	uint32_t ulVersion = 0;
	uint16_t usType = 0;
	uint16_t usFlags = 0; /* Before Zarafa 7.1, ulFlags did not exist, and ulType was ULONG */
	uint32_t ulId = 0;
	char szServer[];

	EID_V0(EID_V0 &&) = delete;
};

#define SIZEOF_EID_V0_FIXED (sizeof(EID_V0) + 4)

/* dynamic-size structure view (instantiation forbidden) to interpret arbitrary ABEIDs */
struct ABEID {
	BYTE abFlags[4]{};
	GUID guid{};
	uint32_t ulVersion = 0, ulType = 0, ulId = 0;
	char szExId[];

	ABEID(ABEID &&) = delete;
};

/* fixed-size structure used for well-known ABEID constants in code */
struct ABEID_FIXED {
	BYTE abFlags[4]{};
	GUID guid{};
	uint32_t ulVersion = 0, ulType = 0, ulId = 0;
	char pad[4]{};

	constexpr ABEID_FIXED() = default;
	constexpr ABEID_FIXED(unsigned int type, const GUID &g, unsigned int id) :
		guid(g), ulType(type), ulId(id)
	{}
};
typedef struct ABEID *PABEID;
#define CbNewABEID(p) ((sizeof(ABEID) + strlen(p) + 4) / 4 * 4)

static inline ULONG ABEID_TYPE(const ABEID *p)
{
	return p != nullptr ? p->ulType : -1;
}

template<typename T> static inline ULONG ABEID_ID(const T *p)
{
	return p != nullptr ? reinterpret_cast<const ABEID *>(p)->ulId : 0;
}

/* 36 bytes */
struct SIEID {
	BYTE	abFlags[4];
	GUID	guid;
	ULONG	ulVersion;
	ULONG	ulType;
	ULONG	ulId;
	CHAR	szServerId[1];
	CHAR	szPadding[3];

	SIEID() {
		memset(this, 0, sizeof(SIEID));
	}
};
typedef struct SIEID *LPSIEID;

/* Bit definitions for abFlags[3] of ENTRYID */
#define	KOPANO_FAVORITE		0x01		// Entryid from the favorits folder

// Indexes of the identity property array
enum
{
    XPID_NAME,              // Array Indexes
    XPID_EID,
    XPID_SEARCH_KEY,
	XPID_STORE_EID,
	XPID_ADDRESS,
	XPID_ADDRTYPE,
    NUM_IDENTITY_PROPS      // Array size
};

#define TRANSPORT_ADDRESS_TYPE_SMTP KC_T("SMTP")
#define TRANSPORT_ADDRESS_TYPE_ZARAFA KC_T("ZARAFA")
#define TRANSPORT_ADDRESS_TYPE_FAX KC_T("FAX")

typedef EID * PEID;
#define CbNewEID(p) ((sizeof(EID) + strlen(p) + 4) / 4 * 4)

#define EID_TYPE_STORE		1
#define EID_TYPE_OBJECT		2

#ifndef STORE_HTML_OK
#define STORE_HTML_OK	0x00010000
#endif

//The message store supports properties containing ANSI (8-bit) characters
#ifndef STORE_ANSI_OK
#define STORE_ANSI_OK	0x00020000
#endif

//This flag is reserved and should not be used
#ifndef STORE_LOCALSTORE
#define STORE_LOCALSTORE	0x00080000
#endif

#ifndef STORE_UNICODE_OK
#define STORE_UNICODE_OK	0x00040000L
#endif

#ifndef STORE_PUSHER_OK
#define STORE_PUSHER_OK ((ULONG) 0x00800000)
#endif

// This is what we support for private store
#define EC_SUPPORTMASK_PRIVATE \
							STORE_ENTRYID_UNIQUE | \
							STORE_SEARCH_OK | \
							STORE_MODIFY_OK | \
							STORE_CREATE_OK | \
							STORE_ATTACH_OK | \
							STORE_OLE_OK | \
							STORE_NOTIFY_OK | \
							STORE_MV_PROPS_OK | \
							STORE_CATEGORIZE_OK | \
							STORE_RTF_OK | \
							STORE_RESTRICTION_OK | \
							STORE_SORT_OK | \
							STORE_HTML_OK | \
							STORE_UNICODE_OK | \
							STORE_SUBMIT_OK

// This is what we support for archive store
#define EC_SUPPORTMASK_ARCHIVE \
							STORE_ENTRYID_UNIQUE | \
							STORE_SEARCH_OK | \
							STORE_MODIFY_OK | \
							STORE_CREATE_OK | \
							STORE_ATTACH_OK | \
							STORE_OLE_OK | \
							STORE_NOTIFY_OK | \
							STORE_MV_PROPS_OK | \
							STORE_CATEGORIZE_OK | \
							STORE_RTF_OK | \
							STORE_RESTRICTION_OK | \
							STORE_SORT_OK | \
							STORE_HTML_OK | \
							STORE_UNICODE_OK

// This is what we support for delegate store
#define EC_SUPPORTMASK_DELEGATE \
							STORE_ENTRYID_UNIQUE | \
							STORE_SEARCH_OK | \
							STORE_MODIFY_OK | \
							STORE_CREATE_OK | \
							STORE_ATTACH_OK | \
							STORE_OLE_OK | \
							STORE_NOTIFY_OK | \
							STORE_MV_PROPS_OK | \
							STORE_CATEGORIZE_OK | \
							STORE_RTF_OK | \
							STORE_RESTRICTION_OK | \
							STORE_SORT_OK | \
							STORE_HTML_OK | \
							STORE_UNICODE_OK | \
							STORE_SUBMIT_OK

// This is what we support for public store
#define EC_SUPPORTMASK_PUBLIC \
							STORE_ENTRYID_UNIQUE | \
							STORE_SEARCH_OK | \
							STORE_MODIFY_OK | \
							STORE_CREATE_OK | \
							STORE_ATTACH_OK | \
							STORE_OLE_OK | \
							STORE_NOTIFY_OK | \
							STORE_MV_PROPS_OK | \
							STORE_CATEGORIZE_OK | \
							STORE_RTF_OK | \
							STORE_RESTRICTION_OK | \
							STORE_SORT_OK | \
							STORE_HTML_OK | \
							STORE_UNICODE_OK | \
							STORE_PUBLIC_FOLDERS


// This is the DLL name given to WrapStoreEntryID so MAPI can
// figure out which DLL to open for a given EntryID

// Note that the '32' is added by MAPI
#define WCLIENT_DLL_NAME "zarafa6client.dll"

// Default freebusy publish months
#define ECFREEBUSY_DEFAULT_PUBLISH_MONTHS		6

#define TABLE_CAP_STRING	255
#define TABLE_CAP_BINARY	511

//
// Capabilities bitmask, sent with ns__logon()
//
// test SOAP flag
#ifdef WITH_GZIP
#define KOPANO_CAP_COMPRESSION			0x0001
#else
// no compression in soap
#define KOPANO_CAP_COMPRESSION			0x0000
#endif
// Client has PR_MAILBOX_OWNER_* properties
#define KOPANO_CAP_MAILBOX_OWNER		0x0002
// Server sends Mod. time and Create time in readProps() call
//#define KOPANO_CAP_TIMES_IN_READPROPS	0x0004 //not needed since saveObject is introduced
#define KOPANO_CAP_CRYPT				0x0008
// 64 bit session IDs
#define KOPANO_CAP_LARGE_SESSIONID		0x0010
// Includes license server
#define KOPANO_CAP_LICENSE_SERVER		0x0020
// Client/Server understands ABEID (longer than 36 bytes)
#define KOPANO_CAP_MULTI_SERVER				0x0040
// Server supports enhanced ICS operations
#define KOPANO_CAP_ENHANCED_ICS				0x0100
// Support 'entryid' field in loadProp
#define KOPANO_CAP_LOADPROP_ENTRYID		0x0080
// Client supports unicode
#define KOPANO_CAP_UNICODE				0x0200
// Server side message locking
#define KOPANO_CAP_MSGLOCK				0x0400
// ExportMessageChangeAsStream supports ulPropTag parameter
#define KOPANO_CAP_EXPORT_PROPTAG		0x0800
// Support impersonation
#define KOPANO_CAP_IMPERSONATION        0x1000
// Client stores the max ab changeid obtained from the server
#define KOPANO_CAP_MAX_ABCHANGEID		0x2000
// Client can read and write binary anonymous ab properties
#define KOPANO_CAP_EXTENDED_ANON		0x4000
/*
 * Client knows how to handle (or at least, error-handle) 32-bit IDs
 * returned from the getIDsForNames RPC.
 */
#define KOPANO_CAP_GIFN32 0x8000

// Do *not* use this from a client. This is just what the latest server supports.
#define KOPANO_LATEST_CAPABILITIES (KOPANO_CAP_CRYPT | KOPANO_CAP_LICENSE_SERVER | KOPANO_CAP_LOADPROP_ENTRYID | KOPANO_CAP_EXPORT_PROPTAG | KOPANO_CAP_IMPERSONATION | KOPANO_CAP_GIFN32)

//
// Logon flags, sent with ns__logon()
//
// Don't allow uid based authentication (Unix socket only)
#define KOPANO_LOGON_NO_UID_AUTH		0x0001
// Don't register session after authentication
#define KOPANO_LOGON_NO_REGISTER_SESSION	0x0002

// MTOM IDs
#define MTOM_ID_EXPORTMESSAGES			"idExportMessages"

#endif /* KC_KCORE_H */
