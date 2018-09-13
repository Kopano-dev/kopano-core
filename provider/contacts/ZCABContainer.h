/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ZCABCONTAINER_H
#define ZCABCONTAINER_H

#include <kopano/memory.hpp>
#include <kopano/zcdefs.h>
#include <kopano/Util.h>
#include <mapispi.h>
#include <mapidefs.h>
#include "ZCABLogon.h"
#include "ZCABData.h"
#include "ZCMAPIProp.h"

/* should be derived from IMAPIProp, but since we don't do anything with those functions, let's skip the red tape. */
class ZCABContainer _kc_final :
    public KC::ECUnknown, public IABContainer, public IDistList {
protected:
	ZCABContainer(const std::vector<zcabFolderEntry> *folders, IMAPIFolder *contacts, IMAPISupport *, void *provider, const char *class_name);

private:
	HRESULT MakeWrappedEntryID(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulObjType, ULONG ulOffset, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);

public:
	static HRESULT Create(const std::vector<zcabFolderEntry> *folders, IMAPIFolder *contacts, IMAPISupport *, void *provider, ZCABContainer **);
	static HRESULT Create(IMessage *contact, ULONG eid_size, const ENTRYID *eid, IMAPISupport *, ZCABContainer **);

	HRESULT GetFolderContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	HRESULT GetDistListContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable);

	// IUnknown
	virtual HRESULT	QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

	// IABContainer
	virtual HRESULT CreateEntry(ULONG eid_size, const ENTRYID *eid, ULONG flags, IMAPIProp **);
	virtual HRESULT CopyEntries(const ENTRYLIST *, ULONG ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT DeleteEntries(const ENTRYLIST *, ULONG flags) override;
	virtual HRESULT ResolveNames(const SPropTagArray *lpPropTagArray, ULONG ulFlags, LPADRLIST lpAdrList, LPFlagList lpFlagList);

	// From IMAPIContainer
	virtual HRESULT GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT SetSearchCriteria(const SRestriction *, const ENTRYLIST *container, ULONG flags) override;
	virtual HRESULT GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState);

	// very limited IMAPIProp, passed to ZCMAPIProp for m_lpDistList.
	virtual HRESULT GetProps(const SPropTagArray *lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray);
	virtual HRESULT GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray);
	virtual HRESULT GetLastError(HRESULT, ULONG, MAPIERROR **) _kc_override;
	virtual HRESULT SaveChanges(ULONG) _kc_override;
	virtual HRESULT OpenProperty(ULONG, const IID *, ULONG, ULONG, IUnknown **) _kc_override;
	virtual HRESULT SetProps(ULONG, const SPropValue *, SPropProblemArray **) _kc_override;
	virtual HRESULT DeleteProps(const SPropTagArray *, SPropProblemArray **) _kc_override;
	virtual HRESULT CopyTo(ULONG, const IID *, const SPropTagArray *, ULONG, IMAPIProgress *, const IID *, void *, ULONG, SPropProblemArray **) _kc_override;
	virtual HRESULT CopyProps(const SPropTagArray *, ULONG, IMAPIProgress *, const IID *, void *, ULONG, SPropProblemArray **) _kc_override;
	virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, ULONG flags, ULONG *nvals, MAPINAMEID ***names) override;
	virtual HRESULT GetIDsFromNames(ULONG, MAPINAMEID **, ULONG, SPropTagArray **) _kc_override;

private:
	/* reference to ZCABLogon .. ZCABLogon needs to live because of this, so AddChild */
	const std::vector<zcabFolderEntry> *m_lpFolders;
	KC::object_ptr<IMAPIFolder> m_lpContactFolder;
	KC::object_ptr<IMAPISupport> m_lpMAPISup;
	void *m_lpProvider;

	/* distlist version of this container */
	KC::object_ptr<IMAPIProp> m_lpDistList;
	ALLOC_WRAP_FRIEND;
};

#endif
