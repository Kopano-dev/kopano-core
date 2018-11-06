/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

//gsoap ns service name:	KCmd
//gsoap ns service style:	rpc
//gsoap ns service encoding:	encoded
//gsoap ns service location:	http://localhost:236/
//gsoap ns service namespace: urn:zarafa
//gsoap ns service method-action: KTRAPMARKER ""

#import "xop.h"
#import "xmlmime.h"

struct hiloLong {
	int hi;
	unsigned int lo;
};

// This is actually implemented in SOAP as a base64, not as an array of unsigned bytes
struct xsd__base64Binary {
	unsigned char *__ptr;
	int __size;
	xsd__base64Binary(); /* needed because present in a union */
};

struct xsd__Binary {
	_xop__Include xop__Include; // attachment
	@char *xmlmime__contentType; // and its contentType 
};

typedef struct xsd__base64Binary entryId;

struct mv_i2 {
	short int *__ptr;
	int __size;
	mv_i2(); /* union presence */
};

struct mv_long {
	unsigned int *__ptr;
	int __size;
	mv_long(); /* union presence */
};

struct mv_r4 {
	float *__ptr;
	int __size;
	mv_r4(); /* union presence */
};

struct mv_double {
	double *__ptr;
	int __size;
	mv_double(); /* union presence */
};

struct mv_string8 {
	char**__ptr;
	int __size;
	mv_string8(); /* union presence */
};

struct mv_hiloLong {
	struct hiloLong *__ptr;
	int __size;
	mv_hiloLong(); /* union presence */
};

struct mv_binary {
	struct xsd__base64Binary *__ptr;
	int __size;
	mv_binary(); /* union presence */
};

struct mv_i8 {
	LONG64 *__ptr;
	int __size;
	mv_i8(); /* union presence */
};

struct restrictTable;

union propValData {
    short int           i;          /* case PT_I2 */
    unsigned int		ul;			/* case PT_ULONG */
    float               flt;        /* case PT_R4 */
    double              dbl;        /* case PT_DOUBLE */
    bool				b;          /* case PT_BOOLEAN */
    char*               lpszA;      /* case PT_STRING8 */
	struct hiloLong *	hilo;
	struct xsd__base64Binary *	bin;
    LONG64				li;         /* case PT_I8 */
	struct mv_i2			mvi;		/* case PT_MV_I2 */
	struct mv_long			mvl;		/* case PT_MV_LONG */
	struct mv_r4			mvflt;		/* case PT_MV_R4 */
	struct mv_double		mvdbl;		/* case PT_MV_DOUBLE */
    struct mv_string8		mvszA;		/* case PT_MV_STRING8 */
	struct mv_hiloLong		mvhilo;
	struct mv_binary		mvbin;
	struct mv_i8			mvli;		/* case PT_MV_I8 */
	struct restrictTable	*res;
	struct actions			*actions;
	propValData();
};

struct propVal {
	unsigned int ulPropTag;
	int __union;
	union propValData Value;
};

struct propValArray {
	struct propVal *__ptr;
	int __size;
};

struct propTagArray {
	unsigned int *__ptr;
	int __size;
	propTagArray();
	propTagArray(unsigned int *, int = 0);
};

struct entryList {
	int __size;
	entryId *__ptr;
};

struct saveObject {
	int __size;					/* # children */
	struct saveObject *__ptr;	/* child objects */

	struct propTagArray delProps;
	struct propValArray modProps;
	bool bDelete;				/* delete this object completely */
	unsigned int ulClientId;		/* id for the client (PR_ROWID or PR_ATTACH_NUM, otherwise unused) */
	unsigned int ulServerId;		/* hierarchyid of the server (0 for new item) */
	unsigned int ulObjType;
	struct entryList *lpInstanceIds;	/* Single Instance Id (NULL for new item, or if Single Instancing is unknown) */
};

struct ns:loadObjectResponse {
	unsigned int er;
	struct saveObject sSaveObject;
};

struct ns:logonResponse {
	unsigned int	er;
	ULONG64 ulSessionId;
	char			*lpszVersion;
	unsigned int	ulCapabilities;
	struct xsd__base64Binary sLicenseResponse;
	struct xsd__base64Binary sServerGuid;
};

struct ns:ssoLogonResponse {
	unsigned int	er;
	ULONG64 ulSessionId;
	char			*lpszVersion;
	unsigned int	ulCapabilities;
	struct xsd__base64Binary *lpOutput;
	struct xsd__base64Binary sLicenseResponse;
	struct xsd__base64Binary sServerGuid;
};

struct ns:getStoreResponse {
	unsigned int				er;
	entryId						sStoreId;	// id of store
	entryId						sRootId;	// root folder id of store
	struct xsd__base64Binary	guid;		// guid of store
	char						*lpszServerPath;
};

struct ns:getStoreNameResponse {
	char			*lpszStoreName;
	unsigned int	er;
};

struct ns:getStoreTypeResponse {
	unsigned int	ulStoreType;
	unsigned int	er;
};


/* Warning, this is synced with MAPI's types! */
enum SortOrderType { EC_TABLE_SORT_ASCEND=0, EC_TABLE_SORT_DESCEND, EC_TABLE_SORT_COMBINE, EC_TABLE_SORT_CATEG_MAX = 4, EC_TABLE_SORT_CATEG_MIN = 8};

struct sortOrder {
	unsigned int ulPropTag;
	unsigned int ulOrder;
};

struct sortOrderArray {
	struct sortOrder *__ptr;
	int __size;
};

struct ns:readPropsResponse {
	unsigned int er;
	struct propTagArray aPropTag;
	struct propValArray aPropVal;
};

struct ns:loadPropResponse {
	unsigned int er;
	struct propVal *lpPropVal;
};

struct ns:createFolderResponse {
	unsigned int er;
	entryId	sEntryId;
};

struct ns:tableOpenResponse {
	unsigned int er;
	unsigned int ulTableId;
};

struct tableOpenRequest {
    entryId sEntryId;
    unsigned int ulTableType;
    unsigned int ulType;
    unsigned int ulFlags;
};

struct tableSortRequest {
    struct sortOrderArray sSortOrder;
    unsigned int ulCategories;
    unsigned int ulExpanded;
};

struct tableQueryRowsRequest {
    unsigned int ulCount;
    unsigned int ulFlags;
};

struct rowSet {
	struct propValArray *__ptr;
	int __size;
};

struct ns:tableQueryRowsResponse {
	unsigned int er;
	struct rowSet sRowSet;
};

struct ns:tableQueryColumnsResponse {
	unsigned int er;
	struct propTagArray sPropTagArray;
};

struct ns:tableGetRowCountResponse {
	unsigned int er;
	unsigned int ulCount;
	unsigned int ulRow;
};

struct ns:tableSeekRowResponse {
	unsigned int er;
	int lRowsSought; // may be negative
};

struct ns:tableBookmarkResponse {
	unsigned int er;
	unsigned int ulbkPosition;
};

struct ns:tableExpandRowResponse {
    unsigned int er;
    struct rowSet rowSet;
    unsigned int ulMoreRows;
};

struct ns:tableCollapseRowResponse {
    unsigned int er;
    unsigned int ulRows;
};

struct ns:tableGetCollapseStateResponse {
    struct xsd__base64Binary sCollapseState;
    unsigned int er;
};

struct ns:tableSetCollapseStateResponse {
    unsigned int ulBookmark;
    unsigned int er;
};

struct tableMultiRequest {
    unsigned int ulTableId;
	unsigned int ulFlags;
    struct tableOpenRequest *lpOpen; 			// Open
    struct propTagArray *lpSetColumns;			// SetColumns
    struct restrictTable *lpRestrict;			// Restrict
    struct tableSortRequest *lpSort;			// Sort
    struct tableQueryRowsRequest *lpQueryRows; 	// QueryRows
};

struct ns:tableMultiResponse {
    unsigned int er;
    unsigned int ulTableId;
    struct rowSet sRowSet; 						// QueryRows
};

struct categoryState {
    struct propValArray sProps;
    unsigned int fExpanded;
};

struct categoryStateArray {
    int __size;
    struct categoryState* __ptr;
};

struct collapseState {
    struct categoryStateArray sCategoryStates;
    struct propValArray sBookMarkProps;
};
   
struct notificationObject {
	entryId* pEntryId;
	unsigned int ulObjType;
	entryId* pParentId;
	entryId* pOldId;
	entryId* pOldParentId;
	struct propTagArray* pPropTagArray;
};

struct notificationTable{
	unsigned int ulTableEvent;
	unsigned int ulObjType;
	unsigned int hResult;
	struct propVal propIndex;
	struct propVal propPrior;
	struct propValArray* pRow;
};

struct notificationNewMail {
	entryId* pEntryId;
	entryId* pParentId;
	char* lpszMessageClass;
	unsigned int ulMessageFlags;
};

struct notificationICS {
	struct xsd__base64Binary *pSyncState;
	unsigned int ulChangeType;
};

struct notification {
	unsigned int ulConnection;
	unsigned int ulEventType;
	struct notificationObject *obj;
	struct notificationTable *tab;
	struct notificationNewMail *newmail;
	struct notificationICS *ics;
};

struct notificationArray {
	int __size;
	struct notification *__ptr;
};

struct ns:notifyResponse {
	struct notificationArray	*pNotificationArray;
	unsigned int er;
};

struct notifySyncState {
	unsigned int ulSyncId;
	unsigned int ulChangeId;
};

struct notifySubscribe {
	unsigned int ulConnection;
	struct xsd__base64Binary sKey;
	unsigned int ulEventMask;
	struct notifySyncState sSyncState;
};

struct notifySubscribeArray {
	int __size;
	struct notifySubscribe *__ptr;
};

#define TABLE_NOADVANCE 1

struct rights {
	unsigned int ulUserid;
	unsigned int ulType;
	unsigned int ulRights;
	unsigned int ulState;
	entryId		 sUserId;
};

struct rightsArray {
	int __size;
	struct rights *__ptr;
};

struct ns:rightsResponse {
	struct rightsArray	*pRightsArray;
	unsigned int er;
};

struct userobject {
	char* lpszName;
	unsigned int ulId;
	entryId		 sId;
	unsigned int ulType;
};

struct userobjectArray {
	int __size;
	struct userobject *__ptr;
};

struct ns:getOwnerResponse {
	unsigned int ulOwner;
	entryId sOwner;
	unsigned int er;
};

struct statObjectResponse {
	unsigned int ulSize;
	unsigned int ftCreated;
	unsigned int ftModified;
	unsigned int er;
};

struct namedProp {
	unsigned int *lpId;
	char *lpString;
	struct xsd__base64Binary *lpguid;
};

struct namedPropArray {
	int __size;
	struct namedProp * __ptr;
};

struct ns:getIDsFromNamesResponse {
	struct propTagArray lpsPropTags;
	unsigned int er;
};

struct ns:getNamesFromIDsResponse {
	struct namedPropArray lpsNames;
	unsigned int er;
};

struct restrictTable;

struct restrictAnd {
	int __size;
	struct restrictTable **__ptr;
};

struct restrictBitmask {
	unsigned int ulMask;
	unsigned int ulPropTag;
	unsigned int ulType;
};

struct restrictCompare {
	unsigned int ulPropTag1;
	unsigned int ulPropTag2;
	unsigned int ulType;
};

struct restrictContent {
	unsigned int ulFuzzyLevel;
	unsigned int ulPropTag;
	struct propVal *lpProp;
};

struct restrictExist {
	unsigned int ulPropTag;
};

struct restrictComment {
	struct restrictTable *lpResTable;
	struct propValArray sProps;
};

struct restrictNot {
	struct restrictTable *lpNot;
};

struct restrictOr {
	int __size;
	struct restrictTable **__ptr;
};

struct restrictProp {
	unsigned int ulType;
	unsigned int ulPropTag;
	struct propVal *lpProp;
};

struct restrictSize {
	unsigned int ulType;
	unsigned int ulPropTag;
	unsigned int cb;
};

struct restrictSub {
	unsigned int ulSubObject;
	struct restrictTable *lpSubObject;
};

struct restrictTable {
	unsigned int ulType;
	struct restrictAnd *lpAnd;
	struct restrictBitmask *lpBitmask;
	struct restrictCompare *lpCompare;
	struct restrictContent *lpContent;
	struct restrictExist *lpExist;
	struct restrictNot *lpNot;
	struct restrictOr *lpOr;
	struct restrictProp *lpProp;
	struct restrictSize *lpSize;
	struct restrictComment *lpComment;
	struct restrictSub *lpSub;
};

struct ns:tableGetSearchCriteriaResponse {
	struct restrictTable *lpRestrict;
	struct entryList *lpFolderIDs;
	unsigned int ulFlags;
	unsigned int er;
};

struct receiveFolder {
	entryId sEntryId;
	char* lpszAExplicitClass;
};

struct ns:receiveFolderResponse {
	struct receiveFolder sReceiveFolder;
	unsigned int er;
};

struct receiveFoldersArray {
	int __size;
	struct receiveFolder * __ptr;
};

struct ns:receiveFolderTableResponse {
	struct receiveFoldersArray sFolderArray;
	unsigned int er;
};

struct searchCriteria {
	struct restrictTable *lpRestrict;
	struct entryList *lpFolders;
	unsigned int ulFlags;
};

struct propmapPair {
	unsigned int ulPropId;
	char *lpszValue;
};

struct propmapPairArray {
	int __size;
	struct propmapPair *__ptr;
};

struct propmapMVPair {
	unsigned int ulPropId;
	struct mv_string8 sValues;
};

struct propmapMVPairArray {
	int __size;
	struct propmapMVPair *__ptr;
};

struct user {
	unsigned int ulUserId;
	char		*lpszUsername;
	char		*lpszPassword;
	char		*lpszMailAddress;
	char		*lpszFullName;
	char		*lpszServername;
	unsigned int 	ulIsNonActive; /* was used for 6.40 clients */
	unsigned int 	ulIsAdmin;
	unsigned int	ulIsABHidden;
	unsigned int	ulCapacity;
	unsigned int	ulObjClass;
	struct propmapPairArray *lpsPropmap;
	struct propmapMVPairArray *lpsMVPropmap;
	entryId		sUserId;
};

struct userArray {
	int __size;
	struct user *__ptr;
};

struct ns:userListResponse {
	struct userArray sUserArray;
	unsigned int er;
};

struct ns:getUserResponse {
	struct user  *lpsUser;
	unsigned int er;
};

struct ns:setUserResponse {
	unsigned int ulUserId;
	entryId		 sUserId;
	unsigned int er;
};

struct group {
	unsigned int ulGroupId;
	entryId		 sGroupId;
	char		*lpszGroupname;
	char		*lpszFullname;
	char		*lpszFullEmail;
	unsigned int	ulIsABHidden;
	struct propmapPairArray *lpsPropmap;
	struct propmapMVPairArray *lpsMVPropmap;
};

struct groupArray {
	int __size;
	struct group *__ptr;
};

struct ns:groupListResponse {
	struct groupArray sGroupArray;
	unsigned int er;
};

struct ns:getGroupResponse {
	struct group *lpsGroup;
	unsigned int er;
};

struct ns:setGroupResponse {
	unsigned int ulGroupId;
	entryId		 sGroupId;
	unsigned int er;
};

struct company {
	unsigned int ulCompanyId;
	unsigned int ulAdministrator;
	entryId sCompanyId;
	entryId sAdministrator;
	char *lpszCompanyname;
	char *lpszServername;
	unsigned int	ulIsABHidden;
	struct propmapPairArray *lpsPropmap;
	struct propmapMVPairArray *lpsMVPropmap;
};

struct companyArray {
	int __size;
	struct company *__ptr;
};

struct ns:companyListResponse {
	struct companyArray sCompanyArray;
	unsigned int er;
};

struct ns:getCompanyResponse {
	struct company *lpsCompany;
	unsigned int er;
};

struct ns:setCompanyResponse {
	unsigned int ulCompanyId;
	entryId sCompanyId;
	unsigned int er;
};

struct ns:resolveUserStoreResponse {
	unsigned int ulUserId;
	entryId	sUserId;
	entryId	sStoreId;
	struct xsd__base64Binary guid;
	unsigned int er;
	char *lpszServerPath;
};

struct querySubMessageResponse {
	entryId sEntryId;
	unsigned int er;
};

struct userProfileResponse {
	char *szProfileName;
	char *szProfileAddress;
	unsigned int er;
};

struct ns:resolveCompanyResponse {
	unsigned int ulCompanyId;
	entryId sCompanyId;
	unsigned int er;
};

struct ns:resolveGroupResponse {
	unsigned int ulGroupId;
	entryId sGroupId;
	unsigned int er;
};

struct ns:resolveUserResponse {
	unsigned int ulUserId;
	entryId sUserId;
	unsigned int er;
};

struct readChunkResponse {
	struct xsd__base64Binary data;
	unsigned int er;
};

struct flagArray {
	int __size;
	unsigned int *__ptr;
};

struct ns:abResolveNamesResponse {
	struct rowSet sRowSet;
	struct flagArray aFlags;
	unsigned int er;
};

struct action {
	unsigned int acttype;
	unsigned int flavor;
	unsigned int flags;
	int __union;
	union _act {
		struct _moveCopy {
			struct xsd__base64Binary store;
			struct xsd__base64Binary folder;
		} moveCopy;
		struct _reply {
			struct xsd__base64Binary message;
			struct xsd__base64Binary guid;
		} reply;
		struct _defer {
			struct xsd__base64Binary bin;
		} defer;
		unsigned int bouncecode;
		struct rowSet *adrlist;
		struct propVal *prop;
		_act();
	} act;
};

struct actions {
	struct action *__ptr;
	int __size;
};

struct quota {
	bool bUseDefaultQuota;
	bool bIsUserDefaultQuota;
	LONG64 llWarnSize;
	LONG64 llSoftSize;
	LONG64 llHardSize;
};

struct ns:quotaResponse {
	struct quota sQuota;
	unsigned int er;
};

struct ns:quotaStatus {
	LONG64 llStoreSize;
	unsigned int ulQuotaStatus;
	unsigned int er;
};

struct ns:messageStatus {
	unsigned int ulMessageStatus;
	unsigned int er;
};

struct icsChange {
	unsigned int ulChangeId;
    struct xsd__base64Binary sSourceKey;
    struct xsd__base64Binary sParentSourceKey;
    unsigned int ulChangeType;
	unsigned int ulFlags;
};

struct icsChangesArray {
	int __size;
	struct icsChange *__ptr;
};

struct ns:icsChangeResponse {
	struct icsChangesArray sChangesArray;
	unsigned int ulMaxChangeId;
	unsigned int er;
};

struct ns:setSyncStatusResponse {
	unsigned int ulSyncId;
	unsigned int er;
};

struct ns:getEntryIDFromSourceKeyResponse {
	entryId	sEntryId;
	unsigned int er;
};

struct ns:getLicenseAuthResponse {
    struct xsd__base64Binary sAuthResponse;
    unsigned int er;
};
struct ns:resolvePseudoUrlResponse {
	const char *lpszServerPath;
	bool bIsPeer;
	unsigned int er;
};

struct server {
	char *lpszName;
	char *lpszFilePath;
	char *lpszHttpPath;
	char *lpszSslPath;
	char *lpszPreferedPath;
	unsigned int ulFlags;
};

struct serverList {
	int __size;
	struct server *__ptr;
};

struct ns:getServerDetailsResponse {
	struct serverList sServerList;
	unsigned int er;
};

struct ns:getServerBehaviorResponse {
	unsigned int ulBehavior;
	unsigned int er;
};

struct sourceKeyPair {
	struct xsd__base64Binary sParentKey;
	struct xsd__base64Binary sObjectKey;
};

struct sourceKeyPairArray {
	int __size;
	struct sourceKeyPair *__ptr;
};

struct messageStream {
	unsigned int ulStep;
	struct propValArray sPropVals;
	struct xsd__Binary sStreamData;
};

struct messageStreamArray {
	int __size;
	struct messageStream *__ptr;
};

struct ns:exportMessageChangesAsStreamResponse {
	struct messageStreamArray sMsgStreams;
	unsigned int er;
};

struct ns:getChangeInfoResponse {
	struct propVal sPropPCL;
	struct propVal sPropCK;
	unsigned int er;
};

struct syncState {
	unsigned int ulSyncId;
	unsigned int ulChangeId;
};

struct syncStateArray {
	int __size;
	struct syncState *__ptr;
};

struct ns:getSyncStatesReponse {
	struct syncStateArray sSyncStates;
	unsigned int er;
	//getSyncstatesReponse */
};

struct ns:purgeDeferredUpdatesResponse {
    unsigned int ulDeferredRemaining;
    unsigned int er;
};

struct ns:userClientUpdateStatusResponse {
	unsigned int ulTrackId;
	time_t tUpdatetime;
	char *lpszCurrentversion;
	char *lpszLatestversion;
	char *lpszComputername;
	unsigned int ulStatus;
	unsigned int er;
};

struct ns:resetFolderCountResponse {
	unsigned int ulUpdates;
	unsigned int er;
};

//TableType flags for function ns__tableOpen
#define TABLETYPE_MS				1	// MessageStore tables
#define TABLETYPE_AB				2	// Addressbook tables
#define TABLETYPE_SPOOLER			3	// Spooler tables
#define TABLETYPE_MULTISTORE		4	// Multistore tables
#define TABLETYPE_STATS_SYSTEM		5	// System stats
#define TABLETYPE_STATS_SESSIONS	6	// Session stats
#define TABLETYPE_STATS_USERS		7	// User stats
#define TABLETYPE_STATS_COMPANY		8	// Company stats (hosted only)
#define TABLETYPE_USERSTORES		9	// UserStore tables
#define TABLETYPE_MAILBOX			10	// Mailbox Table
#define TABLETYPE_STATS_SERVERS		11	// Servers table

// Flags for struct tableMultiRequest
#define TABLE_MULTI_CLEAR_RESTRICTION	0x1	// Clear table restriction

#define fnevKopanoIcsChange			(fnevExtended | 0x00000001)

int ns__logon(const char *szUsername, const char *szPassword, const char *szImpersonateUser, const char *szVersion, unsigned int ulCapabilities, unsigned int ulFlags, struct xsd__base64Binary sLicenseReq, ULONG64 ullSessionGroup, const char *szClientApp, const char *szClientAppVersion, const char *szClientAppMisc, struct ns:logonResponse *lpsLogonResponse);
int ns__ssoLogon(ULONG64 ulSessionId, const char *szUsername, const char *szImpersonateUser, struct xsd__base64Binary *lpInput, const char *clientVersion, unsigned int clientCaps, struct xsd__base64Binary sLicenseReq, ULONG64 ullSessionGroup, const char *szClientApp, const char *szClientAppVersion, const char *szClientAppMisc, struct ns:ssoLogonResponse *lpsResponse);

int ns__getStore(ULONG64 ulSessionId, entryId* lpsEntryId, struct ns:getStoreResponse *lpsResponse);
int ns__getStoreName(ULONG64 ulSessionId, entryId sEntryId, struct ns:getStoreNameResponse* lpsResponse);
int ns__getStoreType(ULONG64 ulSessionId, entryId sEntryId, struct ns:getStoreTypeResponse* lpsResponse);
int ns__getPublicStore(ULONG64 ulSessionId, unsigned int ulFlags, struct ns:getStoreResponse *lpsResponse);
int ns__logoff(ULONG64 ulSessionId, unsigned int *result);

int ns__getRights(ULONG64 ulSessionId, entryId sEntryId, int ulType, struct ns:rightsResponse *lpsRightResponse);
int ns__setRights(ULONG64 ulSessionId, entryId sEntryId, struct rightsArray *lpsrightsArray, unsigned int *result);

/* loads a big prop from an object */
int ns__loadProp(ULONG64 ulSessionId, entryId sEntryId, unsigned int ulObjId, unsigned int ulPropTag, struct ns:loadPropResponse *lpsResponse);
int ns__saveObject(ULONG64 ulSessionId, entryId sParentEntryId, entryId sEntryId, struct saveObject *lpsSaveObj, unsigned int ulFlags, unsigned int ulSyncId, struct ns:loadObjectResponse *lpsLoadObjectResponse);
int ns__loadObject(ULONG64 ulSessionId, entryId sEntryId, struct notifySubscribe *lpsNotSubscribe, unsigned int ulFlags, struct ns:loadObjectResponse *lpsLoadObjectResponse);

int ns__createFolder(ULONG64 ulSessionId, entryId sParentId, entryId *lpsNewEntryId, unsigned int ulType, const char *szName, const char *szComment, bool fOpenIfExists, unsigned int ulSyncId, struct xsd__base64Binary sOrigSourceKey, struct ns:createFolderResponse *lpsCreateFolderResponse);
int ns__deleteObjects(ULONG64 ulSessionId, unsigned int ulFlags, struct entryList *aMessages, unsigned int ulSyncId, unsigned int *result);
int ns__copyObjects(ULONG64 ulSessionId, struct entryList *aMessages, entryId sDestFolderId, unsigned int ulFlags, unsigned int ulSyncId, unsigned int *result);
int ns__emptyFolder(ULONG64 ulSessionId, entryId sEntryId,  unsigned int ulFlags, unsigned int ulSyncId, unsigned int *result);
int ns__deleteFolder(ULONG64 ulSessionId, entryId sEntryId, unsigned int ulFlags, unsigned int ulSyncId, unsigned int *result);
int ns__copyFolder(ULONG64 ulSessionId, entryId sEntryId, entryId sDestFolderId, const char *lpszNewFolderName, unsigned int ulFlags, unsigned int ulSyncId, unsigned int *result);
int ns__setReadFlags(ULONG64 ulSessionId, unsigned int ulFlags, entryId* lpsEntryId, struct entryList *lpMessages, unsigned int ulSyncId, unsigned int *result);
int ns__setReceiveFolder(ULONG64 ulSessionId, entryId sStoreId, entryId *lpsEntryId, const char *lpszMessageClass, unsigned int *result);
int ns__getReceiveFolder(ULONG64 ulSessionId, entryId sStoreId, const char *lpszMessageClass, struct ns:receiveFolderResponse *lpsReceiveFolder);
int ns__getReceiveFolderTable(ULONG64 ulSessionId, entryId sStoreId, struct ns:receiveFolderTableResponse *lpsReceiveFolderTable);

int ns__getMessageStatus(ULONG64 ulSessionId, entryId sEntryId, unsigned int ulFlags, struct ns:messageStatus* lpsStatus);
int ns__setMessageStatus(ULONG64 ulSessionId, entryId sEntryId, unsigned int ulNewStatus, unsigned int ulNewStatusMask, unsigned int ulSyncId, struct ns:messageStatus* lpsOldStatus);

int ns__getIDsFromNames(ULONG64 ulSessionId, struct namedPropArray *lpsNamedProps, unsigned int ulFlags, struct ns:getIDsFromNamesResponse *lpsResponse);
int ns__getNamesFromIDs(ULONG64 ulSessionId, struct propTagArray *lpsPropTags, struct ns:getNamesFromIDsResponse *lpsResponse);

int ns__notify(ULONG64 ulSessionId, struct notification sNotification, unsigned int *er);
int ns__notifySubscribe(ULONG64 ulSessionId, struct notifySubscribe *notifySubscribe, unsigned int *result);
int ns__notifySubscribeMulti(ULONG64 ulSessionId, struct notifySubscribeArray *notifySubscribeArray, unsigned int *result);
int ns__notifyUnSubscribe(ULONG64 ulSessionId, unsigned int ulConnection, unsigned int *result);
int ns__notifyUnSubscribeMulti(ULONG64 ulSessionId, struct mv_long *ulConnectionArray, unsigned int *result);
int ns__notifyGetItems(ULONG64 ulSessionId, struct ns:notifyResponse *notifications);

int ns__tableOpen(ULONG64 ulSessionId, entryId sEntryId, unsigned int ulTableType, unsigned int ulType, unsigned int ulFlags, struct ns:tableOpenResponse *lpsTableOpenResponse);
int ns__tableClose(ULONG64 ulSessionId, unsigned int ulTableId, unsigned int *result);
int ns__tableSetColumns(ULONG64 ulSessionId, unsigned int ulTableId, struct propTagArray *aPropTag, unsigned int *result);
int ns__tableQueryColumns(ULONG64 ulSessionId, unsigned int ulTableId, unsigned int ulFlags, struct ns:tableQueryColumnsResponse *lpsTableQueryColumnsResponse);
int ns__tableSort(ULONG64 ulSessionId, unsigned int ulTableId, struct sortOrderArray *aSortOrder, unsigned int ulCategories, unsigned int ulExpanded, unsigned int *result);
int ns__tableRestrict(ULONG64 ulSessionId, unsigned int ulTableId, struct restrictTable *lpRestrict, unsigned int *result);
int ns__tableGetRowCount(ULONG64 ulSessionId, unsigned int ulTableId, struct ns:tableGetRowCountResponse *lpsTableGetRowCountResponse);
int ns__tableQueryRows(ULONG64 ulSessionId, unsigned int ulTableId, unsigned int ulRowCount, unsigned int ulFlags, struct ns:tableQueryRowsResponse *lpsQueryRowsResponse);
int ns__tableFindRow(ULONG64 ulSessionId, unsigned int ulTableId, unsigned int ulBookmark, unsigned int ulFlags, struct restrictTable *lpsRestrict, unsigned int *result);
int ns__tableSeekRow(ULONG64 ulSessionId, unsigned int ulTableId, unsigned int ulBookmark, int lRowCount, struct ns:tableSeekRowResponse *lpsResponse);
int ns__tableCreateBookmark(ULONG64 ulSessionId, unsigned int ulTableId, struct ns:tableBookmarkResponse *lpsResponse);
int ns__tableFreeBookmark(ULONG64 ulSessionId, unsigned int ulTableId, unsigned int ulbkPosition, unsigned int *result);
int ns__tableSetSearchCriteria(ULONG64 ulSessionId, entryId sEntryId, struct restrictTable *lpRestrict, struct entryList *lpFolders, unsigned int ulFlags, unsigned int *result);
int ns__tableGetSearchCriteria(ULONG64 ulSessionId, entryId sEntryId, struct ns:tableGetSearchCriteriaResponse *lpsResponse);
int ns__tableSetMultiStoreEntryIDs(ULONG64 ulSessionId, unsigned int ulTableId, struct entryList *aMessages, unsigned int *result);
int ns__tableExpandRow(ULONG64 ulSessionId, unsigned int ulTableId, struct xsd__base64Binary sInstanceKey, unsigned int ulRowCount, unsigned int ulFlags, struct ns:tableExpandRowResponse *lpsTableExpandRowResponse);
int ns__tableCollapseRow(ULONG64 ulSessionId, unsigned int ulTableId, struct xsd__base64Binary sInstanceKey, unsigned int ulFlags, struct ns:tableCollapseRowResponse *lpsTableCollapseRowResponse);
int ns__tableGetCollapseState(ULONG64 ulSessionId, unsigned int ulTableId, struct xsd__base64Binary sBookmark, struct ns:tableGetCollapseStateResponse *lpsResponse);
int ns__tableSetCollapseState(ULONG64 ulSessionId, unsigned int ulTableId, struct xsd__base64Binary sCollapseState, struct ns:tableSetCollapseStateResponse *lpsResponse);
int ns__tableMulti(ULONG64 ulSessionId, struct tableMultiRequest sRequest, struct ns:tableMultiResponse *lpsResponse);

int ns__submitMessage(ULONG64 ulSessionId, entryId sEntryId, unsigned int ulFlags, unsigned int *result);
int ns__finishedMessage(ULONG64 ulSessionId, entryId sEntryId, unsigned int ulFlags, unsigned int *result);
int ns__abortSubmit(ULONG64 ulSessionId, entryId sEntryId, unsigned int *result);

// Get user ID / store for username (username == NULL for current user)
int ns__resolveStore(ULONG64 ulSessionId, struct xsd__base64Binary sStoreGuid, struct ns:resolveUserStoreResponse *lpsResponse);
int ns__resolveUserStore(ULONG64 ulSessionId, const char *szUserName, unsigned int ulStoreTypeMask, unsigned int ulFlags, struct ns:resolveUserStoreResponse *lpsResponse);

// Actual user creation/deletion in the external user source
int ns__createUser(ULONG64 ulSessionId, struct user *lpsUser, struct ns:setUserResponse *lpsUserSetResponse);
int ns__deleteUser(ULONG64 ulSessionId, unsigned int ulUserId, entryId sUserId, unsigned int *result);
int ns__removeAllObjects(ULONG64 ulSessionId, entryId sExceptUserId, unsigned int *result);

// Get user fullname/name/emailaddress/etc for specific user id (userid = 0 for current user)
int ns__getUser(ULONG64 ulSessionId, unsigned int ulUserId, entryId sUserId, struct ns:getUserResponse *lpsUserGetResponse);
int ns__setUser(ULONG64 ulSessionId, struct user *lpsUser, unsigned int *result);
int ns__getUserList(ULONG64 ulSessionId, unsigned int ulCompanyId, entryId sCompanyId, struct ns:userListResponse *lpsUserList);
int ns__getSendAsList(ULONG64 ulSessionId, unsigned int ulUserId, entryId sUserId, struct ns:userListResponse *lpsUserList);
int ns__addSendAsUser(ULONG64 ulSessionId, unsigned int ulUserId, entryId sUserId, unsigned int ulSenderId, entryId sSenderId, unsigned int *result);
int ns__delSendAsUser(ULONG64 ulSessionId, unsigned int ulUserId, entryId sUserId, unsigned int ulSenderId, entryId sSenderId, unsigned int *result);
int ns__getUserClientUpdateStatus(ULONG64 ulSessionId, entryId sUserId, struct ns:userClientUpdateStatusResponse *lpsResponse);

// Start softdelete purge
int ns__purgeSoftDelete(ULONG64 ulSessionId, unsigned int ulDays, unsigned int *result);
// Do deferred purge
int ns__purgeDeferredUpdates(ULONG64 ulSessionId, struct ns:purgeDeferredUpdatesResponse *lpsResponse);
// Clear the cache
int ns__purgeCache(ULONG64 ulSessionId, unsigned int ulFlags, unsigned int *result);

// Create store for a user
int ns__createStore(ULONG64 ulSessionId, unsigned int ulStoreType, unsigned int ulUserId, entryId sUserId, entryId sStoreId, entryId sRootId, unsigned int ulFlags, unsigned int *result);
// Mark store deleted for softdelete to purge from database
int ns__removeStore(ULONG64 ulSessionId, struct xsd__base64Binary sStoreGuid, unsigned int ulSyncId, unsigned int *result);
// Hook a store to a specified user (overrides previous hooked store)
int ns__hookStore(ULONG64 ulSessionId, unsigned int ulStoreType, entryId sUserId, struct xsd__base64Binary sStoreGuid, unsigned int ulSyncId, unsigned int *result);
// Unhook a store from a specific user
int ns__unhookStore(ULONG64 ulSessionId, unsigned int ulStoreType, entryId sUserId, unsigned int ulSyncId, unsigned int *result);

int ns__getOwner(ULONG64 ulSessionId, entryId sEntryId, struct ns:getOwnerResponse *lpsResponse);
int ns__resolveUsername(ULONG64 ulSessionId, const char *lpszUsername, struct ns:resolveUserResponse *lpsResponse);

int ns__createGroup(ULONG64 ulSessionId, struct group *lpsGroup, struct ns:setGroupResponse *lpsSetGroupResponse);
int ns__setGroup(ULONG64 ulSessionId, struct group *lpsGroup, unsigned int *result);
int ns__getGroup(ULONG64 ulSessionId, unsigned int ulGroupId, entryId sGroupId, struct ns:getGroupResponse *lpsReponse);
int ns__getGroupList(ULONG64 ulSessionId,  unsigned int ulCompanyId, entryId sCompanyId, struct ns:groupListResponse *lpsGroupList);
int ns__groupDelete(ULONG64 ulSessionId, unsigned int ulGroupId, entryId sGroupId, unsigned int *result);
int ns__resolveGroupname(ULONG64 ulSessionId, const char *lpszGroupname, struct ns:resolveGroupResponse *lpsResponse);

int ns__deleteGroupUser(ULONG64 ulSessionId, unsigned int ulGroupId, entryId sGroupId, unsigned int ulUserId, entryId sUserId, unsigned int *result);
int ns__addGroupUser(ULONG64 ulSessionId, unsigned int ulGroupId, entryId sGroupId, unsigned int ulUserId, entryId sUserId, unsigned int *result);
int ns__getUserListOfGroup(ULONG64 ulSessionId, unsigned int ulGroupId, entryId sGroupId, struct ns:userListResponse *lpsUserList);
int ns__getGroupListOfUser(ULONG64 ulSessionId, unsigned int ulUserId, entryId sUserId, struct ns:groupListResponse *lpsGroupList);

int ns__createCompany(ULONG64 ulSessionId, struct company *lpsCompany, struct ns:setCompanyResponse *lpsResponse);
int ns__deleteCompany(ULONG64 ulSessionId, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result);
int ns__setCompany(ULONG64 ulSessionId, struct company *lpsCompany, unsigned int *result);
int ns__getCompany(ULONG64 ulSessionId, unsigned int ulCompanyId, entryId sCompanyId, struct ns:getCompanyResponse *lpsResponse);
int ns__resolveCompanyname(ULONG64 ulSessionId, const char *lpszCompanyname, struct ns:resolveCompanyResponse *lpsResponse);
int ns__getCompanyList(ULONG64 ulSessionId, struct ns:companyListResponse *lpsCompanyList);

int ns__addCompanyToRemoteViewList(ULONG64 ecSessionId, unsigned int ulSetCompanyId, entryId sSetCompanyId, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result);
int ns__delCompanyFromRemoteViewList(ULONG64 ecSessionId, unsigned int ulSetCompanyId, entryId sSetCompanyId, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result);
int ns__getRemoteViewList(ULONG64 ecSessionId, unsigned int ulCompanyId, entryId sCompanyId, struct ns:companyListResponse *lpsCompanyList);
int ns__addUserToRemoteAdminList(ULONG64 ecSessionId, unsigned int ulUserId, entryId sUserId, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result);
int ns__delUserFromRemoteAdminList(ULONG64 ecSessionId, unsigned int ulUserId, entryId sUserId, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result);
int ns__getRemoteAdminList(ULONG64 ecSessionId, unsigned int ulCompanyId, entryId sCompanyId, struct ns:userListResponse *lpsUserList);

int ns__checkExistObject(ULONG64 ulSessionId, entryId sEntryId, unsigned int ulFlags, unsigned int *result);

int ns__readABProps(ULONG64 ulSessionId, entryId sEntryId, struct ns:readPropsResponse *readPropsResponse);

int ns__abResolveNames(ULONG64 ulSessionId, struct propTagArray* lpaPropTag, struct rowSet* lpsRowSet, struct flagArray* lpaFlags, unsigned int ulFlags, struct ns:abResolveNamesResponse* lpsABResolveNames);

int ns__syncUsers(ULONG64 ulSessionId, unsigned int ulCompanyId, entryId sCompanyId, unsigned int *result);

int ns__setLockState(ULONG64 ulSessionId, entryId sEntryId, bool bLocked, unsigned int *result); 

int ns__resetFolderCount(ULONG64 ulSessionId, entryId sEntryId, struct ns:resetFolderCountResponse *lpsResponse);

// Quota
int ns__GetQuota(ULONG64 ulSessionId, unsigned int ulUserid, entryId sUserId, bool bGetUserDefault, struct ns:quotaResponse* lpsQuota);
int ns__SetQuota(ULONG64 ulSessionId, unsigned int ulUserid, entryId sUserId, struct quota* lpsQuota, unsigned int *result);
int ns__AddQuotaRecipient(ULONG64 ulSessionId, unsigned int ulCompanyid, entryId sCompanyId, unsigned int ulRecipientId, entryId sRecipientId, unsigned int ulType, unsigned int *result);
int ns__DeleteQuotaRecipient(ULONG64 ulSessionId, unsigned int ulCompanyid, entryId sCompanyId, unsigned int ulRecipientId, entryId sRecipientId, unsigned int ulType, unsigned int *result);
int ns__GetQuotaRecipients(ULONG64 ulSessionId, unsigned int ulUserid, entryId sUserId, struct ns:userListResponse *lpsResponse);
int ns__GetQuotaStatus(ULONG64 ulSessionId, unsigned int ulUserid, entryId sUserId, struct ns:quotaStatus* lpsQuotaStatus);

// Incremental Change Synchronization
int ns__getChanges(ULONG64 ulSessionId, struct xsd__base64Binary sSourceKeyFolder, unsigned int ulSyncId, unsigned int ulChangeId, unsigned int ulChangeType, unsigned int ulFlags, struct restrictTable *lpsRestrict, struct ns:icsChangeResponse* lpsChanges);
int ns__setSyncStatus(ULONG64 ulSessionId, struct xsd__base64Binary sSourceKeyFolder, unsigned int ulSyncId, unsigned int ulChangeId, unsigned int ulChangeType, unsigned int ulFlags, struct ns:setSyncStatusResponse *lpsResponse);

int ns__getEntryIDFromSourceKey(ULONG64 ulSessionId, entryId sStoreId, struct xsd__base64Binary folderSourceKey, struct xsd__base64Binary messageSourceKey, struct ns:getEntryIDFromSourceKeyResponse *lpsResponse);
int ns__getSyncStates(ULONG64 ulSessionId, struct mv_long ulaSyncId, struct ns:getSyncStatesReponse *lpsResponse);

// Licensing
int ns__getLicenseAuth(ULONG64 ulSessionId, struct xsd__base64Binary sAuthData, struct ns:getLicenseAuthResponse *lpsResponse);

// Multi Server
int ns__resolvePseudoUrl(ULONG64 ulSessionId, const char *lpszPseudoUrl, struct ns:resolvePseudoUrlResponse *lpsResponse);
int ns__getServerDetails(ULONG64 ulSessionId, struct mv_string8 szaSvrNameList, unsigned int ulFlags, struct ns:getServerDetailsResponse* lpsResponse);

// Server Behavior, legacy calls for 6.30 clients, unused and may be removed in the future
int ns__getServerBehavior(ULONG64 ulSessionId, struct ns:getServerBehaviorResponse* lpsResponse);
int ns__setServerBehavior(ULONG64 ulSessionId, unsigned int ulBehavior, unsigned int *result);

// Streaming
int ns__exportMessageChangesAsStream(ULONG64 ulSessionId, unsigned int ulFlags, struct propTagArray sPropTags, struct sourceKeyPairArray, unsigned int ulPropTag, struct ns:exportMessageChangesAsStreamResponse *lpsResponse);
int ns__importMessageFromStream(ULONG64 ulSessionId, unsigned int ulFlags, unsigned int ulSyncId, entryId sParentEntryId, entryId sEntryId, bool bIsNew, struct propVal *lpsConflictItems, struct xsd__Binary sStreamData, unsigned int *result);
int ns__getChangeInfo(ULONG64 ulSessionId, entryId sEntryId, struct ns:getChangeInfoResponse *lpsResponse);

// Debug
struct testPerformArgs {
    int __size;
    char *__ptr[];
};

struct ns:testGetResponse {
    char *szValue;    
    unsigned int er;
};

int ns__testPerform(ULONG64 ulSessionId, const char *cmd, struct testPerformArgs sPerform, unsigned int *result);
int ns__testSet(ULONG64 ulSessionId, const char *name, const char *value, unsigned int *result);
int ns__testGet(ULONG64 ulSessionId, const char *name, struct ns:testGetResponse *lpsResponse);

struct attachment {
	char	*lpszAttachmentName;
	struct xsd__Binary sData;
};

struct attachmentArray {
	int __size;
	struct attachment *__ptr;
};

struct ns:clientUpdateResponse {
	unsigned int ulLogLevel;
	char *lpszServerPath;
	struct xsd__base64Binary sLicenseResponse;
	struct xsd__Binary sStreamData;
	unsigned int er;
};

struct clientUpdateInfoRequest {
	unsigned int ulTrackId;
	char *szUsername;
	char *szClientIPList;
	char *szClientVersion;
	char *szWindowsVersion;
	char *szComputerName;

	struct xsd__base64Binary sLicenseReq;
};

struct clientUpdateStatusRequest {
	unsigned int ulTrackId;
	unsigned int ulLastErrorCode;
	unsigned int ulLastErrorAction;
	struct attachmentArray sFiles;
};

struct ns:clientUpdateStatusResponse {
	unsigned int er;
};

int ns__getClientUpdate(struct clientUpdateInfoRequest sClientUpdateInfo, struct ns:clientUpdateResponse* lpsResponse);
int ns__setClientUpdateStatus(struct clientUpdateStatusRequest sClientUpdateStatus, struct ns:clientUpdateStatusResponse* lpsResponse);

