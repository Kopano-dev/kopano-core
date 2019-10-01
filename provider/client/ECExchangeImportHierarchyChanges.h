/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECEXCHANGEIMPORTCHIERARCHYCHANGES_H
#define ECEXCHANGEIMPORTCHIERARCHYCHANGES_H

#include <mapidefs.h>
#include "ECMAPIFolder.h"
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include <kopano/zcdefs.h>

class ECExchangeImportHierarchyChanges KC_FINAL_OPG :
    public KC::ECUnknown, public IExchangeImportHierarchyChanges {
protected:
	ECExchangeImportHierarchyChanges(ECMAPIFolder *lpFolder);
public:
	static	HRESULT Create(ECMAPIFolder *lpFolder, LPEXCHANGEIMPORTHIERARCHYCHANGES* lppExchangeImportHierarchyChanges);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT GetLastError(HRESULT result, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT Config(IStream *, unsigned int flags) override;
	virtual HRESULT UpdateState(IStream *) override;
	virtual HRESULT ImportFolderChange(unsigned int nvals, SPropValue *) override;
	virtual HRESULT ImportFolderDeletion(unsigned int flags, ENTRYLIST *source_entry) override;

private:
	KC::object_ptr<ECMAPIFolder> m_lpFolder;
	IStream *m_lpStream = nullptr;
	unsigned int m_ulFlags = 0, m_ulSyncId = 0, m_ulChangeId = 0;
	ALLOC_WRAP_FRIEND;
};

#endif // ECEXCHANGEIMPORTCHIERARCHYCHANGES_H
