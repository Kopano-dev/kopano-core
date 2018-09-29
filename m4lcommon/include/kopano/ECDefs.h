/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECDEFS_H
#define ECDEFS_H

#include <string>
#include <kopano/platform.h>
#include <mapidefs.h>

namespace KC {

// Get permission type
#define ACCESS_TYPE_DENIED		1
#define ACCESS_TYPE_GRANT		2
#define ACCESS_TYPE_BOTH		3

#define	ecRightsNone			0x00000000L
#define ecRightsReadAny			0x00000001L
#define	ecRightsCreate			0x00000002L
#define	ecRightsEditOwned		0x00000008L
#define	ecRightsDeleteOwned		0x00000010L
#define	ecRightsEditAny			0x00000020L
#define	ecRightsDeleteAny		0x00000040L
#define	ecRightsCreateSubfolder	0x00000080L
#define	ecRightsFolderAccess	0x00000100L
//#define	ecrightsContact			0x00000200L
#define	ecRightsFolderVisible	0x00000400L

#define ecRightsTemplateNoRights	ecRightsFolderVisible
#define ecRightsTemplateReadOnly	ecRightsTemplateNoRights | ecRightsReadAny
#define ecRightsTemplateSecretary	ecRightsTemplateReadOnly | ecRightsCreate | ecRightsEditOwned | ecRightsDeleteOwned | ecRightsEditAny | ecRightsDeleteAny
#define ecRightsTemplateOwner		ecRightsTemplateSecretary | ecRightsCreateSubfolder | ecRightsFolderAccess

/* #define ecRightsTemplateReviewer	ecRightsTemplateReadOnly */
/* #define ecRightsTemplateAuthor		ecRightsTemplateReadOnly | ecRightsCreate | ecRightsEditOwned | ecRightsDeleteOwned */
/* #define ecRightsTemplateEditor		ecRightsTemplateReadOnly | ecRightsCreate | ecRightsEditAny | ecRightsDeleteAny */

#define ecRightsAll				0x000005FBL
#define ecRightsFullControl		0x000004FBL
#define ecRightsDefault			ecRightsNone | ecRightsFolderVisible
#define ecRightsDefaultPublic	ecRightsReadAny | ecRightsFolderVisible
#define	ecRightsAdmin			0x00001000L

#define	ecRightsAllMask			0x000015FBL

// Right change indication (state field in struct)
#define RIGHT_NORMAL				0x00
#define RIGHT_NEW					0x01
#define RIGHT_MODIFY				0x02
#define RIGHT_DELETED				0x04
#define RIGHT_AUTOUPDATE_DENIED		0x08

#define OBJECTCLASS(__type, __class) \
	( (((__type) << 16) | ((__class) & 0xffff)) )
#define OBJECTCLASS_CLASSTYPE(__class) \
	( ((__class) & 0xffff0000) )
#define OBJECTCLASS_TYPE(__class) \
	( (objecttype_t)(((__class) >> 16) & 0xffff) )
#define OBJECTCLASS_ISTYPE(__class) \
	( ((__class) & 0xffff) == 0 && ((__class) >> 16) != 0)
#define OBJECTCLASS_FIELD_COMPARE(__left, __right) \
	( !(__left) || !(__right) || (__left) == (__right) )
#define OBJECTCLASS_COMPARE(__left, __right) \
	( OBJECTCLASS_FIELD_COMPARE(OBJECTCLASS_TYPE(__left), OBJECTCLASS_TYPE(__right)) && \
	  OBJECTCLASS_FIELD_COMPARE((__left) & 0xffff, (__right) & 0xffff) )
#define OBJECTCLASS_COMPARE_SQL(__column, __objclass) \
	std::string(((__objclass) == 0) ? \
			"TRUE" : \
			((__objclass) & 0xffff) ? \
				__column " = " + stringify(__objclass) : \
				"(" __column " & 4294901760) = " + stringify((__objclass) & 0xffff0000))

enum objecttype_t {
	OBJECTTYPE_UNKNOWN		= 0,
	OBJECTTYPE_MAILUSER		= 1,
	OBJECTTYPE_DISTLIST		= 3,
	OBJECTTYPE_CONTAINER	= 4
};

enum objectclass_t {
	OBJECTCLASS_UNKNOWN		= OBJECTCLASS(OBJECTTYPE_UNKNOWN, 0),

	/* All User (active and nonactive) objectclasses */
	OBJECTCLASS_USER		= OBJECTCLASS(OBJECTTYPE_MAILUSER, 0),
	ACTIVE_USER				= OBJECTCLASS(OBJECTTYPE_MAILUSER, 1),
	NONACTIVE_USER			= OBJECTCLASS(OBJECTTYPE_MAILUSER, 2),
	NONACTIVE_ROOM			= OBJECTCLASS(OBJECTTYPE_MAILUSER, 3),
	NONACTIVE_EQUIPMENT		= OBJECTCLASS(OBJECTTYPE_MAILUSER, 4),
	NONACTIVE_CONTACT		= OBJECTCLASS(OBJECTTYPE_MAILUSER, 5),

	/* All distribution lists */
	OBJECTCLASS_DISTLIST	= OBJECTCLASS(OBJECTTYPE_DISTLIST, 0),
	DISTLIST_GROUP			= OBJECTCLASS(OBJECTTYPE_DISTLIST, 1),
	DISTLIST_SECURITY		= OBJECTCLASS(OBJECTTYPE_DISTLIST, 2),
	DISTLIST_DYNAMIC		= OBJECTCLASS(OBJECTTYPE_DISTLIST, 3),

	/* All container objects */
	OBJECTCLASS_CONTAINER	= OBJECTCLASS(OBJECTTYPE_CONTAINER, 0),
	CONTAINER_COMPANY		= OBJECTCLASS(OBJECTTYPE_CONTAINER, 1),
	CONTAINER_ADDRESSLIST	= OBJECTCLASS(OBJECTTYPE_CONTAINER, 2)
};

enum userobject_relation_t {
	OBJECTRELATION_GROUP_MEMBER = 1,
	OBJECTRELATION_COMPANY_VIEW = 2,
	OBJECTRELATION_COMPANY_ADMIN = 3,
	OBJECTRELATION_QUOTA_USERRECIPIENT = 4,
	OBJECTRELATION_QUOTA_COMPANYRECIPIENT = 5,
	OBJECTRELATION_USER_SENDAS = 6,
	OBJECTRELATION_ADDRESSLIST_MEMBER = 7
};

// Warning, those values are the same as ECSecurity::eQuotaStatus
enum eQuotaStatus{ QUOTA_OK, QUOTA_WARN, QUOTA_SOFTLIMIT, QUOTA_HARDLIMIT};

enum userobject_admin_level_t {
	ADMIN_LEVEL_ADMIN = 1,		/* Administrator over user's own company. */
	ADMIN_LEVEL_SYSADMIN = 2		/* System administrator (same rights as SYSTEM). */
};

struct ECSVRNAMELIST {
	unsigned int	cServers;
	LPTSTR*			lpszaServer;
};

struct SPROPMAPENTRY {
	unsigned int	ulPropId;
	LPTSTR			lpszValue;
};
typedef struct SPROPMAPENTRY *LPSPROPMAPENTRY;

struct SPROPMAP {
	unsigned int		cEntries;
	LPSPROPMAPENTRY		lpEntries;
};
typedef struct SPROPMAP *LPSPROPMAP;

struct MVPROPMAPENTRY {
	unsigned int	ulPropId;
	int				cValues;
	LPTSTR*			lpszValues;
};
typedef struct MVPROPMAPENTRY *LPMVPROPMAPENTRY;

struct MVPROPMAP {
	unsigned int		cEntries;
	LPMVPROPMAPENTRY	lpEntries;
};
typedef struct MVPROPMAP *LPMVPROPMAP;

struct ECUSER {
	LPTSTR			lpszUsername;	// username@companyname
	LPTSTR			lpszPassword;
	LPTSTR			lpszMailAddress;
	LPTSTR			lpszFullName;
	LPTSTR			lpszServername;
	objectclass_t	ulObjClass;
	unsigned int	ulIsAdmin;		// See userobject_admin_level_t
	unsigned int	ulIsABHidden;	// Is user hidden from address book
	unsigned int	ulCapacity;		// Resource capacity
	SPROPMAP		sPropmap;		// Extra anonymous properties for addressbook
	MVPROPMAP		sMVPropmap;		// Extra anonymous MV properties for addressbook
	SBinary sUserId;
};

struct ECGROUP {
	LPTSTR			lpszGroupname; // groupname@companyname
	LPTSTR			lpszFullname;
	LPTSTR			lpszFullEmail;
	SBinary sGroupId;
	unsigned int	ulIsABHidden;	// Is group hidden from address book
	SPROPMAP		sPropmap;		// Extra anonymous properties for addressbook
	MVPROPMAP		sMVPropmap;		// Extra anonymous MV properties for addressbook
};

struct ECCOMPANY {
	SBinary sAdministrator; // userid of the administrator
	LPTSTR			lpszCompanyname;
	LPTSTR			lpszServername;
	SBinary sCompanyId;
	unsigned int	ulIsABHidden;	// Is company hidden from address book
	SPROPMAP		sPropmap;		// Extra anonymous properties for addressbook
	MVPROPMAP		sMVPropmap;		// Extra anonymous MV properties for addressbook
};

#define UPDATE_STATUS_UNKNOWN	0
#define UPDATE_STATUS_SUCCESS   1
#define UPDATE_STATUS_PENDING   2
#define UPDATE_STATUS_FAILED    3

struct ECPERMISSION {
	unsigned int	ulType;
	unsigned int	ulRights;
	unsigned int	ulState;
	SBinary sUserId;
};

struct ECQUOTA {
	bool			bUseDefaultQuota;
	bool			bIsUserDefaultQuota; // Default quota for users within company
	int64_t		llWarnSize;
	int64_t		llSoftSize;
	int64_t		llHardSize;
};

struct ECQUOTASTATUS {
	int64_t		llStoreSize;
	eQuotaStatus	quotaStatus;
};

struct ECSERVER {
	LPTSTR	lpszName;
	LPTSTR	lpszFilePath;
	LPTSTR	lpszHttpPath;
	LPTSTR	lpszSslPath;
	LPTSTR	lpszPreferedPath;
	ULONG	ulFlags;
};

struct ECSERVERLIST {
	unsigned int	cServers;
	ECSERVER *lpsaServer;
};

// Flags for ns__submitMessage
#define EC_SUBMIT_LOCAL			0x00000000
#define EC_SUBMIT_MASTER		0x00000001

#define EC_SUBMIT_DOSENTMAIL	0x00000002

// GetServerDetails
#define EC_SERVERDETAIL_NO_NAME			0x00000001
#define EC_SERVERDETAIL_FILEPATH		0x00000002
#define EC_SERVERDETAIL_HTTPPATH		0x00000004
#define EC_SERVERDETAIL_SSLPATH			0x00000008
#define EC_SERVERDETAIL_PREFEREDPATH	0x00000010

#define EC_SDFLAG_IS_PEER		0x00000001
#define EC_SDFLAG_HAS_PUBLIC	0x00000002

// CreateStore flag(s)
#define EC_OVERRIDE_HOMESERVER			0x00000001

} /* namespace */

#endif
