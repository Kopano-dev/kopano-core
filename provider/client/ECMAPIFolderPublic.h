/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECMAPIFOLDERPUBLIC_H
#define ECMAPIFOLDERPUBLIC_H

#include "ECMAPIFolder.h"
#include <kopano/ECMemTable.h>
#include <kopano/Util.h>
#include <kopano/zcdefs.h>
#include "ClientUtil.h"

class ECMAPIFolderPublic KC_FINAL_OPG : public ECMAPIFolder {
protected:
	ECMAPIFolderPublic(ECMsgStore *lpMsgStore, BOOL fModify, WSMAPIFolderOps *lpFolderOps, enumPublicEntryID ePublicEntryID);

public:
	static HRESULT Create(ECMsgStore *lpMsgStore, BOOL fModify, WSMAPIFolderOps *lpFolderOps, enumPublicEntryID ePublicEntryID, ECMAPIFolder **lppECMAPIFolder);
	static HRESULT GetPropHandler(unsigned int tag, void *prov, unsigned int flags, SPropValue *, ECGenericProp *lpParam, void *base);
	static HRESULT SetPropHandler(unsigned int tag, void *prov, const SPropValue *, ECGenericProp *);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT SetEntryId(ULONG eid_size, const ENTRYID *eid);

public:
	// Override ECMAPIContainer
	virtual HRESULT GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT SetProps(ULONG cValues, const SPropValue *lpPropArray, LPSPropProblemArray *lppProblems);
	virtual HRESULT DeleteProps(const SPropTagArray *lpPropTagArray, LPSPropProblemArray *lppProblems);
	virtual HRESULT CreateMessage(LPCIID lpInterface, ULONG ulFlags, LPMESSAGE *lppMessage);
	virtual HRESULT CopyMessages(LPENTRYLIST lpMsgList, LPCIID lpInterface, LPVOID lpDestFolder, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags);
	virtual HRESULT CopyFolder(ULONG eid_size, const ENTRYID *eid, const IID *intf, void *dst_fld, const TCHAR *newname, ULONG_PTR ui_param, IMAPIProgress *, ULONG flags);
	virtual HRESULT DeleteFolder(ULONG eid_size, const ENTRYID *, ULONG ui_param, IMAPIProgress *, ULONG flags) override;

	enumPublicEntryID	m_ePublicEntryID;

protected:
	virtual HRESULT SaveChanges(ULONG ulFlags);
	ALLOC_WRAP_FRIEND;
};

#endif //#ifndef ECMAPIFOLDERPUBLIC_H
