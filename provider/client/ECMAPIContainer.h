/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
// ECMAPIContainer.h: interface for the ECMAPIContainer class.
#ifndef ECMAPICONTAINER
#define ECMAPICONTAINER

#include <mapidefs.h>
#include "WSTransport.h"
#include "ECMsgStore.h"
#include "ECMAPIProp.h"

class ECMAPIContainer : public ECMAPIProp, public virtual IMAPIContainer {
public:
	ECMAPIContainer(ECMsgStore *lpMsgStore, ULONG ulObjType, BOOL fModify, const char *szClassName);
	virtual ~ECMAPIContainer(void) = default;

	// IUnknown
	virtual HRESULT	QueryInterface(const IID &, void **) override;

	// IMAPIContainer
	virtual HRESULT GetContentsTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT GetHierarchyTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT SetSearchCriteria(const SRestriction *, const ENTRYLIST *container, ULONG flags) override;
	virtual HRESULT GetSearchCriteria(unsigned int flags, SRestriction **, ENTRYLIST **containers, unsigned int *state) override;

	// IMAPIProp
	virtual HRESULT CopyTo(unsigned int nexcl, const IID *iid_excl, const SPropTagArray *exclprop, unsigned int ui_param, IMAPIProgress *, const IID *intf, void *dst_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, unsigned int ui_param, IMAPIProgress *, const IID *intf, void *dst_obj, unsigned int flags, SPropProblemArray **) override;
};

#endif // #ifndef ECMAPICONTAINER
