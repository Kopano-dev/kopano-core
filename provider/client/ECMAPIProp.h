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

#include <kopano/zcdefs.h>
#include "kcore.hpp"
#include <kopano/IECSecurity.h>
#include "ECGenericProp.h"

// For HrSetFlags
#define SET         1
#define UNSET       2

class ECMsgStore;

class ECMAPIProp : public ECGenericProp {
protected:
	ECMAPIProp(void *lpProvider, ULONG ulObjType, BOOL fModify, ECMAPIProp *lpRoot, const char *szClassName = NULL);
	virtual ~ECMAPIProp();

public:
	/**
	 * \brief Obtain a different interface to this object.
	 *
	 * See ECUnkown::QueryInterface.
	 */
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

	static HRESULT TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType);

	// Callback for Commit() on streams
	static HRESULT	HrStreamCommit(IStream *lpStream, void *lpData);
	// Callback for ECMemStream delete local data
	static HRESULT	HrStreamCleanup(void *lpData);

	static HRESULT	DefaultMAPIGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT	SetPropHandler(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam);

	// IMAPIProp override
	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk);
	virtual HRESULT SaveChanges(ULONG ulFlags);
	virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT GetNamesFromIDs(LPSPropTagArray * pptaga, LPGUID lpguid, ULONG ulFlags, ULONG * pcNames, LPMAPINAMEID ** pppNames);
	virtual HRESULT GetIDsFromNames(ULONG cNames, LPMAPINAMEID * ppNames, ULONG ulFlags, LPSPropTagArray * pptaga);

	virtual HRESULT HrSetSyncId(ULONG ulSyncId);

	virtual HRESULT SetParentID(ULONG cbParentID, LPENTRYID lpParentID);

	virtual HRESULT SetICSObject(BOOL bICSObject);
	virtual BOOL IsICSObject();

protected:
	HRESULT HrLoadProps();
	virtual HRESULT HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject);

	HRESULT GetSerializedACLData(LPVOID lpBase, LPSPropValue lpsPropValue);
	HRESULT SetSerializedACLData(LPSPropValue lpsPropValue);
	HRESULT	UpdateACLs(ULONG cNewPerms, ECPERMISSION *lpNewPerms);

protected:
	// IECServiceAdmin and IECSecurity
	virtual HRESULT GetUserList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers);
	virtual HRESULT GetGroupList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups);
	virtual HRESULT GetCompanyList(ULONG ulFlags, ULONG *lpcCompanies, ECCOMPANY **lppsCompanies);
	// IECSecurity
	virtual HRESULT GetOwner(ULONG *lpcbOwner, LPENTRYID *lppOwner);
	virtual HRESULT GetPermissionRules(int ulType, ULONG* lpcPermissions, ECPERMISSION **lppECPermissions);
	virtual HRESULT SetPermissionRules(ULONG cPermissions, ECPERMISSION *lpECPermissions);

public:
	ECMsgStore*				GetMsgStore();


public:	
	class xMAPIProp _zcp_final : public IMAPIProp {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IMAPIProp.hpp>
	} m_xMAPIProp;

	class xECSecurity _zcp_final : public IECSecurity {
		#include <kopano/xclsfrag/IUnknown.hpp>
		// <kopano/xclsfrag/IECSecurity.hpp>
		virtual HRESULT GetOwner(ULONG *lpcbOwner, LPENTRYID *lppOwner) _zcp_override;
		virtual HRESULT GetPermissionRules(int ulType, ULONG *lpcPermissions, ECPERMISSION **lppECPermissions) _zcp_override;
		virtual HRESULT SetPermissionRules(ULONG cPermissions, ECPERMISSION *lpECPermissions) _zcp_override;
		virtual HRESULT GetUserList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers) _zcp_override;
		virtual HRESULT GetGroupList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups) _zcp_override;
		virtual HRESULT GetCompanyList(ULONG ulFlags, ULONG *lpcCompanies, ECCOMPANY **lppCompanies);
	} m_xECSecurity;

private:
	BOOL		m_bICSObject; // coming from the ICS system
	ULONG		m_ulSyncId;
	ULONG		m_cbParentID;
	LPENTRYID	m_lpParentID; // Overrides the parentid from the server

public:
	ECMAPIProp *m_lpRoot;		// Points to the 'root' object that was opened by OpenEntry; normally points to 'this' except for Attachments and Submessages
};

typedef struct STREAMDATA {
	ULONG ulPropTag;
	ECMAPIProp *lpProp;
} STREAMDATA;

#endif // ECMAPIPROP_H
