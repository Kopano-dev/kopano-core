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

#ifndef ECMAPIPROP_H
#define ECMAPIPROP_H

#include <kopano/memory.hpp>
#include <kopano/zcdefs.h>
#include "kcore.hpp"
#include <kopano/IECInterfaces.hpp>
#include "ECGenericProp.h"

// For HrSetFlags
#define SET         1
#define UNSET       2

using namespace KC;
class ECMsgStore;

class ECMAPIProp : public ECGenericProp, public IECSecurity {
protected:
	ECMAPIProp(ECMsgStore *prov, ULONG obj_type, BOOL modify, const ECMAPIProp *root, const char *cls = nullptr);
	virtual ~ECMAPIProp() = default;

public:
	/**
	 * \brief Obtain a different interface to this object.
	 *
	 * See ECUnkown::QueryInterface.
	 */
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);

	// Callback for Commit() on streams
	static HRESULT	HrStreamCommit(IStream *lpStream, void *lpData);
	// Callback for ECMemStream delete local data
	static HRESULT	HrStreamCleanup(void *lpData);

	static HRESULT	DefaultMAPIGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT SetPropHandler(ULONG ulPropTag, void *lpProvider, const SPropValue *lpsPropValue, void *lpParam);

	// IMAPIProp override
	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk);
	virtual HRESULT SaveChanges(ULONG ulFlags);
	virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT CopyProps(const SPropTagArray *lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *, ULONG flags, ULONG *nvals, MAPINAMEID ***names) override;
	virtual HRESULT GetIDsFromNames(ULONG cNames, LPMAPINAMEID * ppNames, ULONG ulFlags, LPSPropTagArray * pptaga);

	virtual HRESULT HrSetSyncId(ULONG ulSyncId);
	virtual HRESULT SetParentID(ULONG eid, const ENTRYID *eid_size);
	virtual HRESULT SetICSObject(BOOL bICSObject);
	virtual BOOL IsICSObject();

protected:
	HRESULT HrLoadProps();
	virtual HRESULT HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject);

	HRESULT GetSerializedACLData(LPVOID lpBase, LPSPropValue lpsPropValue);
	HRESULT SetSerializedACLData(const SPropValue *lpsPropValue);
	HRESULT	UpdateACLs(ULONG cNewPerms, ECPERMISSION *lpNewPerms);

	// IECServiceAdmin and IECSecurity
	virtual HRESULT GetUserList(ULONG eid_size, const ENTRYID *comp_eid, ULONG flags, ULONG *nusers, ECUSER **);
	virtual HRESULT GetGroupList(ULONG eid_size, const ENTRYID *comp_eid, ULONG flags, ULONG *ngrps, ECGROUP **);
	virtual HRESULT GetCompanyList(ULONG ulFlags, ULONG *lpcCompanies, ECCOMPANY **lppsCompanies);
	// IECSecurity
	virtual HRESULT GetOwner(ULONG *lpcbOwner, LPENTRYID *lppOwner);
	virtual HRESULT GetPermissionRules(int ulType, ULONG* lpcPermissions, ECPERMISSION **lppECPermissions);
	virtual HRESULT SetPermissionRules(ULONG n, const ECPERMISSION *) override;

public:
	ECMsgStore *GetMsgStore() const { return static_cast<ECMsgStore *>(lpProvider); }

private:
	BOOL m_bICSObject = false; // coming from the ICS system
	ULONG m_ulSyncId = 0, m_cbParentID = 0;
	KC::memory_ptr<ENTRYID> m_lpParentID; /* Overrides the parentid from the server */

public:
	const ECMAPIProp *m_lpRoot; // Points to the 'root' object that was opened by OpenEntry; normally points to 'this' except for Attachments and Submessages
};
IID_OF(ECMAPIProp)

#endif // ECMAPIPROP_H
