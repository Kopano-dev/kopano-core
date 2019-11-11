/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECUSERMANAGEMENT_H
#define ECUSERMANAGEMENT_H

#include <kopano/zcdefs.h>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <kopano/kcodes.h>
#include <kopano/pcuser.hpp>
#include <kopano/ECConfig.h>
#include "ECSession.h"
#include <kopano/ECLogger.h>
#include <kopano/ECDefs.h>
#include "plugin.h"

struct restrictTable;
struct soap;

namespace KC {

class localobjectdetails_t final : public objectdetails_t {
public:
	localobjectdetails_t(void) = default;
	localobjectdetails_t(unsigned int id, objectclass_t objclass) : objectdetails_t(objclass), ulId(id) {};
	localobjectdetails_t(unsigned int id, const objectdetails_t &details) : objectdetails_t(details), ulId(id) {};
	bool operator==(const localobjectdetails_t &obj) const noexcept { return ulId == obj.ulId; };
	bool operator<(const localobjectdetails_t &obj) const noexcept { return ulId < obj.ulId; } ;

	unsigned int ulId = 0;
};

// Use for ulFlags
#define USERMANAGEMENT_IDS_ONLY			0x1		// Return only local userID (the ulId field). 'details' is undefined in this case
#define USERMANAGEMENT_ADDRESSBOOK		0x2		// Return only objects which should be visible in the address book
#define USERMANAGEMENT_FORCE_SYNC		0x4		// Force sync with external database
#define USERMANAGEMENT_SHOW_HIDDEN		0x8		// Show hidden entries

class KC_EXPORT ECUserManagement final {
public:
	KC_HIDDEN ECUserManagement(BTSession *, ECPluginFactory *, std::shared_ptr<ECConfig>);
	KC_HIDDEN virtual ~ECUserManagement() = default;

	// Authenticate a user
	KC_HIDDEN virtual ECRESULT AuthUserAndSync(const char *user, const char *pass, unsigned int *user_id);

	/* Get data for an object, with on-the-fly deletion of the specified object id. */
	virtual ECRESULT GetObjectDetails(unsigned int obj_id, objectdetails_t *ret);
	// Get quota details for a user object
	KC_HIDDEN virtual ECRESULT GetQuotaDetailsAndSync(unsigned int obj_id, quotadetails_t *ret, bool get_user_default = false);
	// Set quota details for a user object
	KC_HIDDEN virtual ECRESULT SetQuotaDetailsAndSync(unsigned int obj_id, const quotadetails_t &);
	/* Get (typed) objectlist for company, or list of all companies, with on-the-fly deletion/creation of users and groups. */
	KC_HIDDEN virtual ECRESULT GetCompanyObjectListAndSync(objectclass_t, unsigned int company_id, const restrictTable *, std::list<localobjectdetails_t> &objs, unsigned int flags = 0);
	/* Get subobjects in an object, with on-the-fly deletion of the specified parent object. */
	KC_HIDDEN virtual ECRESULT GetSubObjectsOfObjectAndSync(userobject_relation_t, unsigned int parent_id, std::list<localobjectdetails_t> &objs, unsigned int flags = 0);
	/* Get parent for an object, with on-the-fly deletion of the specified child object id. */
	KC_HIDDEN virtual ECRESULT GetParentObjectsOfObjectAndSync(userobject_relation_t, unsigned int child_id, std::list<localobjectdetails_t> &groups, unsigned int flags = 0);
	/* Set data for a single user, with on-the-fly deletion of the specified user id. */
	KC_HIDDEN virtual ECRESULT SetObjectDetailsAndSync(unsigned int obj_id, const objectdetails_t &, std::list<std::string> *remove_props);
	/* Add a member to a group, with on-the-fly deletion of the specified group id. */
	KC_HIDDEN virtual ECRESULT AddSubObjectToObjectAndSync(userobject_relation_t, unsigned int parent_id, unsigned int child_id);
	KC_HIDDEN virtual ECRESULT DeleteSubObjectFromObjectAndSync(userobject_relation_t, unsigned int parent_id, unsigned int child_id);
	/* Resolve a user name to a user id, with on-the-fly creation of the specified user. */
	KC_HIDDEN virtual ECRESULT ResolveObjectAndSync(objectclass_t, const char *name, unsigned int *obj_id, bool try_resolve = false);

	// Get a local object ID for a part of a name
	virtual ECRESULT SearchObjectAndSync(const char *search_string, unsigned int flags, unsigned int *id);

	// Create an object
	KC_HIDDEN virtual ECRESULT CreateObjectAndSync(const objectdetails_t &, unsigned int *id);
	// Delete an object
	KC_HIDDEN virtual ECRESULT DeleteObjectAndSync(unsigned int obj_id);
	/* Get MAPI property data for a group or user/group/company id, with on-the-fly deletion of the specified user/group/company. */
	KC_HIDDEN virtual ECRESULT GetProps(struct soap *, unsigned int obj_id, struct propTagArray *, struct propValArray *);
	KC_HIDDEN virtual ECRESULT GetContainerProps(struct soap *, unsigned int obj_id, struct propTagArray *, struct propValArray *);
	// Do the same for a whole set of items
	KC_HIDDEN virtual ECRESULT QueryContentsRowData(struct soap *, const ECObjectTableList *, const struct propTagArray *, struct rowSet **);
	KC_HIDDEN virtual ECRESULT QueryHierarchyRowData(struct soap *, const ECObjectTableList *, const struct propTagArray *, struct rowSet **);
	KC_HIDDEN virtual ECRESULT GetPublicStoreDetails(objectdetails_t *) const;
	virtual ECRESULT GetServerDetails(const std::string &server, serverdetails_t *);
	KC_HIDDEN virtual ECRESULT GetServerList(serverlist_t *);

	// Returns true if ulId is an internal ID (so either SYSTEM or EVERYONE)
	bool IsInternalObject(unsigned int id) const;

	// Create a v1 based AB SourceKey
	KC_HIDDEN ECRESULT GetABSourceKeyV1(unsigned int user_id, SOURCEKEY *);

	// Get userinfo from cache
	KC_HIDDEN ECRESULT GetExternalId(unsigned int di, objectid_t *extern_id, unsigned int *company_id = nullptr, std::string *signature = nullptr) const;
	KC_HIDDEN ECRESULT GetLocalId(const objectid_t &extern_id, unsigned int *id, std::string *signature = nullptr) const;

	/* calls localid->externid and login->user/company conversions */
	KC_HIDDEN virtual ECRESULT UpdateUserDetailsFromClient(objectdetails_t *);

	/* Create an ABEID in version 1 or version 0 */
	KC_HIDDEN ECRESULT CreateABEntryID(struct soap *, unsigned int vers, unsigned int obj_id, unsigned int type, objectid_t *extern_id, gsoap_size_t *eid_size, ABEID **eid) const;

	/* Resync all objects from the plugin. */
	KC_HIDDEN ECRESULT SyncAllObjects();

private:
	/* Convert a user loginname to username and companyname */
	KC_HIDDEN virtual ECRESULT ConvertLoginToUserAndCompany(objectdetails_t *);
	/* Convert username and companyname to loginname */
	KC_HIDDEN virtual ECRESULT ConvertUserAndCompanyToLogin(objectdetails_t *);
	/* convert extern IDs to local IDs */
	KC_HIDDEN virtual ECRESULT ConvertExternIDsToLocalIDs(objectdetails_t *);
	/* convert local IDs to extern IDs */
	KC_HIDDEN virtual ECRESULT ConvertLocalIDsToExternIDs(objectdetails_t *) const;
	/* calls externid->localid and user/company->login conversions */
	KC_HIDDEN virtual ECRESULT UpdateUserDetailsToClient(objectdetails_t *);
	KC_HIDDEN ECRESULT ComplementDefaultFeatures(objectdetails_t *) const;
	KC_HIDDEN ECRESULT RemoveDefaultFeatures(objectdetails_t *) const;
	KC_HIDDEN bool MustHide(/*const*/ ECSecurity &, unsigned int flags, const objectdetails_t &) const;

	// Get object details from list
	KC_HIDDEN ECRESULT GetLocalObjectListFromSignatures(const signatures_t &, const std::map<objectid_t, unsigned int> &extern_to_local, unsigned int flags, std::list<localobjectdetails_t> &) const;
	// Get local details
	KC_HIDDEN ECRESULT GetLocalObjectDetails(unsigned int id, objectdetails_t *) const;

	// Get remote details
	KC_HIDDEN ECRESULT GetExternalObjectDetails(unsigned int id, objectdetails_t *);

	// Get userid from usertable or create a new user/group if it doesn't exist yet
	KC_HIDDEN ECRESULT GetLocalObjectIdOrCreate(const objectsignature_t &signature, unsigned int *id);
	KC_HIDDEN ECRESULT GetLocalObjectsIdsOrCreate(const signatures_t &, std::map<objectid_t, unsigned int> *local_objids);

	// Converts anonymous Object Detail to property. */
	KC_HIDDEN ECRESULT ConvertAnonymousObjectDetailToProp(struct soap *, const objectdetails_t *, unsigned int tag, struct propVal *) const;
	// Converts the data in user/group/company details fields into property value array for content tables and MAPI_MAILUSER and MAPI_DISTLIST objects
	KC_HIDDEN ECRESULT cvt_user_to_props(struct soap *, unsigned int id, unsigned int mapitype, unsigned int proptag, const objectdetails_t *, struct propVal *out);
	KC_HIDDEN ECRESULT cvt_distlist_to_props(struct soap *, unsigned int id, unsigned int mapitype, unsigned int proptag, const objectdetails_t *, struct propVal *out);
	KC_HIDDEN ECRESULT cvt_adrlist_to_props(struct soap *, unsigned int id, unsigned int mapitype, unsigned int proptag, const objectdetails_t *, struct propVal *out) const;
	KC_HIDDEN ECRESULT cvt_company_to_props(struct soap *, unsigned int id, unsigned int mapitype, unsigned int proptag, const objectdetails_t *, struct propVal *out) const;
	KC_HIDDEN ECRESULT ConvertObjectDetailsToProps(struct soap *, unsigned int id, const objectdetails_t *, const struct propTagArray *proptags, struct propValArray *propvals);
	// Converts the data in company/addresslist details fields into property value array for hierarchy tables and MAPI_ABCONT objects
	KC_HIDDEN ECRESULT ConvertContainerObjectDetailsToProps(struct soap *, unsigned int id, const objectdetails_t *, const struct propTagArray *proptags, struct propValArray *propvals) const;
	// Create GlobalAddressBook properties
	KC_HIDDEN ECRESULT ConvertABContainerToProps(struct soap *, unsigned int id, const struct propTagArray *, struct propValArray *) const;

	KC_HIDDEN ECRESULT MoveOrCreateLocalObject(const objectsignature_t &signature, unsigned int *obj_id, bool *moved);
	KC_HIDDEN ECRESULT CreateLocalObjectSimple(const objectsignature_t &signature, unsigned int pref_id);
	KC_HIDDEN ECRESULT CreateLocalObject(const objectsignature_t &signature, unsigned int *obj_id);
	KC_HIDDEN ECRESULT MoveOrDeleteLocalObject(unsigned int obj_id, objectclass_t);
	KC_HIDDEN ECRESULT MoveLocalObject(unsigned int obj_id, objectclass_t, unsigned int company_id, const std::string &newusername);
	KC_HIDDEN ECRESULT DeleteLocalObject(unsigned int obj_id, objectclass_t);
	KC_HIDDEN ECRESULT UpdateObjectclassOrDelete(const objectid_t &extern_id, unsigned int *obj_id);
	KC_HIDDEN ECRESULT GetUserAndCompanyFromLoginName(const std::string &login, std::string *user, std::string *company) const;

	// Process the modification of a user-object
	KC_HIDDEN ECRESULT CheckObjectModified(unsigned int obj_id, const std::string &localsignature, const std::string &remotesignature);
	KC_HIDDEN ECRESULT ProcessModification(unsigned int id, const std::string &newsignature);

	KC_HIDDEN ECRESULT ResolveObject(objectclass_t, const std::string &name, const objectid_t &company, objectid_t *extern_id) const;
	KC_HIDDEN ECRESULT CreateABEntryID(struct soap *, const objectid_t &extern_id, struct propVal *) const;
	KC_HIDDEN ECRESULT CreateABEntryID(struct soap *, unsigned int obj_id, unsigned int type, struct propVal *) const;
	KC_HIDDEN ECRESULT GetSecurity(ECSecurity **) const;

protected:
	ECPluginFactory 	*m_lpPluginFactory;
	BTSession			*m_lpSession;
	std::shared_ptr<ECConfig> m_lpConfig;
};

#define KOPANO_UID_EVERYONE 1
#define KOPANO_UID_SYSTEM 2

#define KOPANO_ACCOUNT_SYSTEM "SYSTEM"
#define KOPANO_FULLNAME_SYSTEM "SYSTEM"
#define KOPANO_ACCOUNT_EVERYONE "Everyone"
#define KOPANO_FULLNAME_EVERYONE "Everyone"

/*
* Fixed addressbook containers
* Only IDs 0, 1 and 2 are available for hardcoding
* IDs for the fixed addressbook containers. This is because
* those IDs are the only ones which will not conflict with
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

} /* namespace */

#endif
