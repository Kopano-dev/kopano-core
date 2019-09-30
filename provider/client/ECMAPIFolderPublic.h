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
	virtual HRESULT SetEntryId(unsigned int eid_size, const ENTRYID *eid) override;

public:
	// Override ECMAPIContainer
	virtual HRESULT GetContentsTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT GetHierarchyTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT SetProps(unsigned int nvals, const SPropValue *props, SPropProblemArray **) override;
	virtual HRESULT DeleteProps(const SPropTagArray *props, SPropProblemArray **) override;
	virtual HRESULT CreateMessage(const IID *intf, unsigned int flags, IMessage **) override;
	virtual HRESULT CopyMessages(ENTRYLIST *msglist, const IID *Intf, void *dst_fld, unsigned int ui_param, IMAPIProgress *, unsigned int flags) override;
	virtual HRESULT CopyFolder(unsigned int eid_size, const ENTRYID *eid, const IID *intf, void *dst_fld, const TCHAR *newname, ULONG_PTR ui_param, IMAPIProgress *, unsigned int flags) override;
	virtual HRESULT DeleteFolder(ULONG eid_size, const ENTRYID *, ULONG ui_param, IMAPIProgress *, ULONG flags) override;

	enumPublicEntryID	m_ePublicEntryID;

protected:
	virtual HRESULT SaveChanges(unsigned int flags) override;
	ALLOC_WRAP_FRIEND;
};

#endif //#ifndef ECMAPIFOLDERPUBLIC_H
