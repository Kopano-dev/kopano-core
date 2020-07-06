/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ZCABCONTAINER_H
#define ZCABCONTAINER_H

#include <memory>
#include <vector>
#include <kopano/memory.hpp>
#include <kopano/Util.h>
#include <kopano/zcdefs.h>
#include <mapispi.h>
#include <mapidefs.h>
#include "ZCABLogon.h"
#include "ZCABData.h"
#include "ZCMAPIProp.h"

/* should be derived from IMAPIProp, but since we don't do anything with those functions, let's skip the red tape. */
class ZCABContainer KC_FINAL_OPG :
    public KC::ECUnknown, public IABContainer, public IDistList {
protected:
	ZCABContainer(std::shared_ptr<std::vector<zcabFolderEntry>> folders, IMAPIFolder *contacts, IMAPISupport *, void *provider, const char *class_name);

private:
	HRESULT MakeWrappedEntryID(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulObjType, ULONG ulOffset, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);

public:
	static HRESULT Create(std::shared_ptr<std::vector<zcabFolderEntry>> folders, IMAPIFolder *contacts, IMAPISupport *, void *provider, ZCABContainer **);
	static HRESULT Create(IMessage *contact, ULONG eid_size, const ENTRYID *eid, IMAPISupport *, ZCABContainer **);

	HRESULT GetFolderContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	HRESULT GetDistListContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable);

	// IUnknown
	virtual HRESULT	QueryInterface(const IID &, void **) override;

	// IABContainer
	virtual HRESULT CreateEntry(unsigned int eid_size, const ENTRYID *eid, unsigned int flags, IMAPIProp **) override;
	virtual HRESULT CopyEntries(const ENTRYLIST *, ULONG ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT DeleteEntries(const ENTRYLIST *, ULONG flags) override;
	virtual HRESULT ResolveNames(const SPropTagArray *props, unsigned int flags, ADRLIST *, FlagList *) override;

	// From IMAPIContainer
	virtual HRESULT GetContentsTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT GetHierarchyTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT SetSearchCriteria(const SRestriction *, const ENTRYLIST *container, ULONG flags) override;
	virtual HRESULT GetSearchCriteria(unsigned int flags, SRestriction **, ENTRYLIST **containers, unsigned int *state) override;

	// very limited IMAPIProp, passed to ZCMAPIProp for m_lpDistList.
	virtual HRESULT GetProps(const SPropTagArray *, unsigned int flags, unsigned int *nvals, SPropValue **) override;
	virtual HRESULT GetPropList(unsigned int flags, SPropTagArray **) override;
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
	std::shared_ptr<std::vector<zcabFolderEntry>> m_lpFolders;
	KC::object_ptr<IMAPIFolder> m_lpContactFolder;
	KC::object_ptr<IMAPISupport> m_lpMAPISup;
	void *m_lpProvider;

	/* distlist version of this container */
	KC::object_ptr<IMAPIProp> m_lpDistList;
	ALLOC_WRAP_FRIEND;
};

#endif
