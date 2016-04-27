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

#ifndef ECUSERMANAGEMENT_H
#define ECUSERMANAGEMENT_H

#include <kopano/zcdefs.h>
#include <list>
#include <map>
#include <ctime>
#include <pthread.h>

#include <kopano/kcodes.h>
#include <kopano/pcuser.hpp>
#include <kopano/ECConfig.h>
#include "ECSession.h"
#include <kopano/ECLogger.h>
#include <kopano/ECDefs.h>
#include "plugin.h"

class localobjectdetails_t _zcp_final : public objectdetails_t {
public:
    localobjectdetails_t() : objectdetails_t(), ulId(0) {};
	localobjectdetails_t(unsigned int id, objectclass_t objclass) : objectdetails_t(objclass), ulId(id) {};
	localobjectdetails_t(unsigned int id, const objectdetails_t &details) : objectdetails_t(details), ulId(id) {};

	bool operator==(const localobjectdetails_t &obj) const { return ulId == obj.ulId; };
	bool operator<(const localobjectdetails_t &obj) const { return ulId < obj.ulId; } ;

	unsigned int ulId;
};

class usercount_t _zcp_final {
public:
	enum ucIndex {
		ucActiveUser = 0,
		ucNonActiveUser,
		ucRoom,
		ucEquipment,
		ucContact,
		ucNonActiveTotal,			// Must be right before ucMAX
		ucMAX = ucNonActiveTotal	// Must be very last
	};

	usercount_t(): m_bValid(false) {
		memset(m_ulCounts, 0, sizeof(m_ulCounts));
	}
	
	usercount_t(unsigned int ulActiveUser, unsigned int ulNonActiveUser, unsigned int ulRoom, unsigned int ulEquipment, unsigned int ulContact): m_bValid(true) {
		m_ulCounts[ucActiveUser]	= ulActiveUser;
		m_ulCounts[ucNonActiveUser]	= ulNonActiveUser;
		m_ulCounts[ucRoom]			= ulRoom;
		m_ulCounts[ucEquipment]		= ulEquipment;
		m_ulCounts[ucContact]		= ulContact;
	}

	usercount_t(const usercount_t &other): m_bValid(other.m_bValid) {
		memcpy(m_ulCounts, other.m_ulCounts, sizeof(m_ulCounts));
	}

	void swap(usercount_t &other) {
		std::swap(m_bValid, other.m_bValid);
		for (unsigned i = 0; i < ucMAX; ++i)
			std::swap(m_ulCounts[i], other.m_ulCounts[i]);
	}

	void assign(unsigned int ulActiveUser, unsigned int ulNonActiveUser, unsigned int ulRoom, unsigned int ulEquipment, unsigned int ulContact) {
		usercount_t tmp(ulActiveUser, ulNonActiveUser, ulRoom, ulEquipment, ulContact);
		swap(tmp);
	}

	void assign(const usercount_t &other) {
		if (&other != this) {
			usercount_t tmp(other);
			swap(tmp);
		}
	}

	usercount_t& operator=(const usercount_t &other) {
		assign(other);
		return *this;
	}

	bool isValid() const {
		return m_bValid;
	}

	void set(ucIndex index, unsigned int ulValue) {
		if (index != ucNonActiveTotal) {
			ASSERT(index >= 0 && index < ucMAX);
			m_ulCounts[index] = ulValue;
			m_bValid = true;
		}
	}

	unsigned int operator[](ucIndex index) const {
		if (index == ucNonActiveTotal)
			return m_ulCounts[ucNonActiveUser] + m_ulCounts[ucRoom] + m_ulCounts[ucEquipment];	// Contacts don't count for non-active stores.

		ASSERT(index >= 0 && index < ucMAX);
		return m_ulCounts[index];
	}

private:
	bool			m_bValid;
	unsigned int	m_ulCounts[ucMAX];
};

// Use for ulFlags
#define USERMANAGEMENT_IDS_ONLY			0x1		// Return only local userID (the ulId field). 'details' is undefined in this case
#define USERMANAGEMENT_ADDRESSBOOK		0x2		// Return only objects which should be visible in the address book
#define USERMANAGEMENT_FORCE_SYNC		0x4		// Force sync with external database
#define USERMANAGEMENT_SHOW_HIDDEN		0x8		// Show hidden entries

// Use for ulLicenseStatus in CheckUserLicense()
#define USERMANAGEMENT_LIMIT_ACTIVE_USERS		0x1	/* Limit reached, but not yet exceeded */
#define USERMANAGEMENT_LIMIT_NONACTIVE_USERS	0x2 /* Limit reached, but not yet exceeded */
#define USERMANAGEMENT_EXCEED_ACTIVE_USERS		0x4 /* Limit exceeded */
#define USERMANAGEMENT_EXCEED_NONACTIVE_USERS	0x8 /* Limit exceeded */

#define USERMANAGEMENT_BLOCK_CREATE_ACTIVE_USER		( USERMANAGEMENT_LIMIT_ACTIVE_USERS | USERMANAGEMENT_EXCEED_ACTIVE_USERS )
#define USERMANAGEMENT_BLOCK_CREATE_NONACTIVE_USER	( USERMANAGEMENT_LIMIT_NONACTIVE_USERS | USERMANAGEMENT_EXCEED_NONACTIVE_USERS )
#define USERMANAGEMENT_USER_LICENSE_EXCEEDED		( USERMANAGEMENT_EXCEED_ACTIVE_USERS | USERMANAGEMENT_EXCEED_NONACTIVE_USERS )

class ECUserManagement {
public:
	ECUserManagement(BTSession *lpSession, ECPluginFactory *lpPluginFactory, ECConfig *lpConfig);
	virtual ~ECUserManagement();

	// Authenticate a user
	virtual ECRESULT	AuthUserAndSync(const char* szUsername, const char* szPassword, unsigned int* lpulUserId);

	// Get data for an object, with on-the-fly delete of the specified object id
	virtual ECRESULT	GetObjectDetails(unsigned int ulObjectId, objectdetails_t *lpDetails);
	// Get quota details for a user object
	virtual ECRESULT	GetQuotaDetailsAndSync(unsigned int ulObjectId, quotadetails_t *lpDetails, bool bGetUserDefault = false);
	// Set quota details for a user object
	virtual ECRESULT	SetQuotaDetailsAndSync(unsigned int ulObjectId, const quotadetails_t &sDetails);
	// Get (typed) objectlist for company, or list of all companies, with on-the-fly delete/create of users and groups
	virtual ECRESULT	GetCompanyObjectListAndSync(objectclass_t objclass, unsigned int ulCompanyId, std::list<localobjectdetails_t> **lppObjects, unsigned int ulFlags = 0);
	// Get subobjects in an object, with on-the-fly delete of the specified parent object
	virtual ECRESULT	GetSubObjectsOfObjectAndSync(userobject_relation_t relation, unsigned int ulParentId, std::list<localobjectdetails_t> **lppObjects, unsigned int ulFlags = 0);
	// Get parent to which an object belongs, with on-the-fly delete of the specified child object id
	virtual ECRESULT	GetParentObjectsOfObjectAndSync(userobject_relation_t relation, unsigned int ulChildId, std::list<localobjectdetails_t> **lppGroups, unsigned int ulFlags = 0);

	// Set data for a single user, with on-the-fly delete of the specified user id
	virtual ECRESULT	SetObjectDetailsAndSync(unsigned int ulObjectId, const objectdetails_t &sDetails, std::list<std::string> *lpRemoveProps );

	// Add a member to a group, with on-the-fly delete of the specified group id
	virtual ECRESULT	AddSubObjectToObjectAndSync(userobject_relation_t relation, unsigned int ulParentId, unsigned int ulChildId);
	virtual ECRESULT	DeleteSubObjectFromObjectAndSync(userobject_relation_t relation, unsigned int ulParentId, unsigned int ulChildId);

	// Resolve a user name to a user id, with on-the-fly create of the specified user
	virtual ECRESULT	ResolveObjectAndSync(objectclass_t objclass, const char* szName, unsigned int* lpulObjectId);

	// Get a local object ID for a part of a name
	virtual ECRESULT	SearchObjectAndSync(const char* szSearchString, unsigned int ulFlags, unsigned int* lpulId);

	// Create an object
	virtual ECRESULT	CreateObjectAndSync(const objectdetails_t &details, unsigned int* ulId);
	// Delete an object
	virtual ECRESULT	DeleteObjectAndSync(unsigned int ulObjectId);
	// Either modify or create an object with a specific object id and type (used for synchronize)
	virtual ECRESULT	CreateOrModifyObject(const objectid_t &sExternId, const objectdetails_t &details, unsigned int ulPreferredId, std::list<std::string> *lpRemoveProps);
	// Used in offline server to synchronise
	virtual ECRESULT	ModifyExternId(unsigned int ulObjectId, const objectid_t &sExternId);

	// Get MAPI property data for a group or user/group/company id, with on-the-fly delete of the specified user/group/company
	virtual ECRESULT	GetProps(struct soap *soap, unsigned int ulObjectId, struct propTagArray *lpPropTagArray, struct propValArray *lpPropValArray);
	virtual ECRESULT	GetContainerProps(struct soap *soap, unsigned int ulObjectId, struct propTagArray *lpPropTagArray, struct propValArray *lpPropValArray);
	// Do the same for a whole set of items
	virtual ECRESULT	QueryContentsRowData(struct soap *soap, ECObjectTableList *lpRowList, struct propTagArray *lpPropTagArray, struct rowSet **lppRowSet);
	virtual ECRESULT	QueryHierarchyRowData(struct soap *soap, ECObjectTableList *lpRowList, struct propTagArray *lpPropTagArray, struct rowSet **lppRowSet);

	virtual ECRESULT	GetUserCount(unsigned int *lpulActive, unsigned int *lpulNonActive); // returns active users and non-active users (so you may get ulUsers=3, ulNonActives=5)
	virtual ECRESULT	GetUserCount(usercount_t *lpUserCount);
	virtual ECRESULT	GetCachedUserCount(usercount_t *lpUserCount);
	virtual ECRESULT	GetPublicStoreDetails(objectdetails_t *lpDetails);

	virtual ECRESULT	GetServerDetails(const std::string &strServer, serverdetails_t *lpDetails);
	virtual ECRESULT	GetServerList(serverlist_t *lpServerList);

	/* Check if the user license status */
	ECRESULT	CheckUserLicense(unsigned int *lpulLicenseStatus);

	// Returns true if ulId is an internal ID (so either SYSTEM or EVERYONE)
	bool		IsInternalObject(unsigned int ulId);

	// Create a v1 based AB SourceKey
	ECRESULT GetABSourceKeyV1(unsigned int ulUserId, SOURCEKEY *lpsSourceKey);

	// Get userinfo from cache
	ECRESULT	GetExternalId(unsigned int ulId, objectid_t *lpExternId, unsigned int *lpulCompanyId = NULL, std::string *lpSignature = NULL);
	ECRESULT	GetLocalId(const objectid_t &sExternId, unsigned int *lpulId, std::string *lpSignature = NULL);

	/* calls localid->externid and login->user/company conversions */
	virtual ECRESULT	UpdateUserDetailsFromClient(objectdetails_t *lpDetails);

	/* Create an ABEID in version 1 or version 0 */
	ECRESULT	CreateABEntryID(struct soap *soap, unsigned int ulVersion, unsigned int ulObjId, unsigned int ulType, objectid_t *sExternId, int *lpcbEID, PABEID *lppEid);

	/* Completely remove all users, groups, etc except for the passed object */
	ECRESULT	RemoveAllObjectsAndSync(unsigned int ulObjId);

	/* Resync all objects from the plugin. */
	ECRESULT	SyncAllObjects();

private:
	/* Convert a user loginname to username and companyname */
	virtual ECRESULT	ConvertLoginToUserAndCompany(objectdetails_t *lpDetails);
	/* Convert username and companyname to loginname */
	virtual ECRESULT	ConvertUserAndCompanyToLogin(objectdetails_t *lpDetails);
	/* convert extern id's to local id's */
	virtual ECRESULT	ConvertExternIDsToLocalIDs(objectdetails_t *lpDetails);
	/* convert local id's to extern id's */
	virtual ECRESULT	ConvertLocalIDsToExternIDs(objectdetails_t *lpDetails);
	/* calls externid->localid and user/company->login conversions */
	virtual ECRESULT	UpdateUserDetailsToClient(objectdetails_t *lpDetails);
	ECRESULT ComplementDefaultFeatures(objectdetails_t *lpDetails);
	ECRESULT RemoveDefaultFeatures(objectdetails_t *lpDetails);
	bool				MustHide(/*const*/ ECSecurity& security, unsigned int ulFlags, const objectdetails_t& details);

	// Get object details from list
	ECRESULT	GetLocalObjectListFromSignatures(const list<objectsignature_t> &lstSignatures,
												 const std::map<objectid_t, unsigned int> &mapExternToLocal,
												 unsigned int ulFlags,
												 list<localobjectdetails_t> *lpDetails);
	// Get local details
	ECRESULT	GetLocalObjectDetails(unsigned int ulId, objectdetails_t *lpDetails);

	// Get remote details
	ECRESULT	GetExternalObjectDetails(unsigned int ulId, objectdetails_t *lpDetails);

	// Get userid from usertable or create a new user/group if it doesn't exist yet
	ECRESULT	GetLocalObjectIdOrCreate(const objectsignature_t &signature, unsigned int *lpulId);
	ECRESULT	GetLocalObjectsIdsOrCreate(const list<objectsignature_t> &lstSignatures, map<objectid_t, unsigned int> *lpmapLocalObjIds);

	// Get a list of local object ID's in the database plus any internal objects (SYSTEM, EVERYONE)
	ECRESULT	GetLocalObjectIdList(objectclass_t objclass, unsigned int ulCompanyId, std::list<unsigned int> **lppObjects);

	// Converts anonymous Object Detail to property. */
	ECRESULT	ConvertAnonymousObjectDetailToProp(struct soap *soap, objectdetails_t *lpDetails, unsigned int ulPropTag, struct propVal *lpPropVal);
	// Converts the data in user/group/company details fields into property value array for content tables and MAPI_MAILUSER and MAPI_DISTLIST objects
	ECRESULT	ConvertObjectDetailsToProps(struct soap *soap, unsigned int ulId, objectdetails_t *lpObjectDetails, struct propTagArray *lpPropTags, struct propValArray *lpPropVals);
	// Converts the data in company/addresslist details fields into property value array for hierarchy tables and MAPI_ABCONT objects
	ECRESULT	ConvertContainerObjectDetailsToProps(struct soap *soap, unsigned int ulId, objectdetails_t *lpObjectDetails, struct propTagArray *lpPropTags, struct propValArray *lpPropVals);
	// Create GlobalAddressBook properties
	ECRESULT	ConvertABContainerToProps(struct soap *soap, unsigned int ulId, struct propTagArray *lpPropTagArray, struct propValArray *lpPropValArray);

	ECRESULT	MoveOrCreateLocalObject(const objectsignature_t &signature, unsigned int *lpulObjectId, bool *lpbMoved);
	ECRESULT	CreateLocalObjectSimple(const objectsignature_t &signature, unsigned int ulPreferredId);
	ECRESULT	CreateLocalObject(const objectsignature_t &signature, unsigned int *lpulObjectId);
	ECRESULT	MoveOrDeleteLocalObject(unsigned int ulObjectId, objectclass_t objclass);
	ECRESULT	MoveLocalObject(unsigned int ulObjectId, objectclass_t objclass, unsigned int ulCompanyId, const std::string &strNewUserName);
	ECRESULT	DeleteLocalObject(unsigned int ulObjectId, objectclass_t objclass);
	ECRESULT	UpdateObjectclassOrDelete(const objectid_t &sExternId, unsigned int *lpulObjectId);

	ECRESULT	GetUserAndCompanyFromLoginName(const std::string &strLoginName, string *lpstrUserName, string *lpstrCompanyName);

	// Process the modification of a user-object
	ECRESULT	CheckObjectModified(unsigned int ulObjectId, const string &localsignature, const string &remotesignature);
	ECRESULT	ProcessModification(unsigned int ulId, const std::string &newsignature);

	ECRESULT	ResolveObject(objectclass_t objclass, const std::string &strName, const objectid_t &sCompany, objectid_t *lpsExternId);
	ECRESULT	CreateABEntryID(struct soap *soap, const objectid_t &sExternId, struct propVal *lpPropVal);
	ECRESULT	CreateABEntryID(struct soap *soap, unsigned int ulObjId, unsigned int ulType, struct propVal *lpPropVal);

	ECRESULT	GetSecurity(ECSecurity **lppSecurity);

protected:
	ECPluginFactory 	*m_lpPluginFactory;
	BTSession			*m_lpSession;
	ECConfig			*m_lpConfig;

private:
	pthread_mutex_t				m_hMutex;
	usercount_t 				m_userCount;
	time_t m_usercount_ts;
};

#define KOPANO_UID_EVERYONE 1
#define KOPANO_UID_SYSTEM 2

#define KOPANO_ACCOUNT_SYSTEM "SYSTEM"
#define KOPANO_FULLNAME_SYSTEM "SYSTEM"
#define KOPANO_ACCOUNT_EVERYONE "Everyone"
#define KOPANO_FULLNAME_EVERYONE "Everyone"

/*
* Fixed addressbook containers
* Only ID's 0, 1 and 2 are available for hardcoding
* IDs for the fixed addressbook containers. This is because
* those ID's are the only ones which will not conflict with
* entries in the users table.
*
* The account name of the containers are used for the path
* name of the container and is used in determine the exact
* order of the containers and the subcontainers in the Address
* Book. The fullname of the containers are used as display
* name to the user.
*/
#define KOPANO_UID_ADDRESS_BOOK			0
#define KOPANO_UID_GLOBAL_ADDRESS_BOOK		1
#define KOPANO_UID_GLOBAL_ADDRESS_LISTS		2

#define KOPANO_ACCOUNT_ADDRESS_BOOK		"Kopano Address Book"
#define KOPANO_FULLNAME_ADDRESS_BOOK		"Kopano Address Book"
#define KOPANO_ACCOUNT_GLOBAL_ADDRESS_BOOK	"Global Address Book"
#define KOPANO_FULLNAME_GLOBAL_ADDRESS_BOOK	"Global Address Book"
#define KOPANO_ACCOUNT_GLOBAL_ADDRESS_LISTS	"Global Address Lists"
#define KOPANO_FULLNAME_GLOBAL_ADDRESS_LISTS	"All Address Lists"

#endif
