/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ZCABCONTAINER_H
#define ZCABCONTAINER_H

#include <kopano/memory.hpp>
#include <kopano/Util.h>
#include <mapispi.h>
#include <mapidefs.h>
#include "ZCABLogon.h"
#include "ZCABData.h"
#include "ZCMAPIProp.h"

/* should be derived from IMAPIProp, but since we don't do anything with those functions, let's skip the red tape. */
class ZCABContainer final :
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
	virtual HRESULT	QueryInterface(const IID &, void **) override;

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
	virtual HRESULT GetLastError(HRESULT result, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT SaveChanges(unsigned int) override;
	virtual HRESULT OpenProperty(unsigned int, const IID *, unsigned int, unsigned int, IUnknown **) override;
	virtual HRESULT SetProps(unsigned int, const SPropValue *, SPropProblemArray **) override;
	virtual HRESULT DeleteProps(const SPropTagArray *, SPropProblemArray **) override;
	virtual HRESULT CopyTo(unsigned int, const IID *, const SPropTagArray *, unsigned int, IMAPIProgress *, const IID *, void *, unsigned int, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *, unsigned int, IMAPIProgress *, const IID *, void *, unsigned int, SPropProblemArray **) override;
	virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, ULONG flags, ULONG *nvals, MAPINAMEID ***names) override;
	virtual HRESULT GetIDsFromNames(unsigned int, MAPINAMEID **, unsigned int, SPropTagArray **) override;

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
