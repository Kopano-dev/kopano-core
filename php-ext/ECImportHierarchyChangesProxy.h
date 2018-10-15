/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECIMPORTHIERARCHYCHANGESPROXY_H
#define ECIMPORTHIERARCHYCHANGESPROXY_H

#include <edkmdb.h>

class ECImportHierarchyChangesProxy final :
    public IExchangeImportHierarchyChanges {
private:
    ULONG m_cRef;
    zval *m_lpObj;
#ifdef ZTS
	TSRMLS_D;
#endif
public:
    ECImportHierarchyChangesProxy(zval *objTarget TSRMLS_DC);
    ~ECImportHierarchyChangesProxy();
	virtual ULONG AddRef() override;
	virtual ULONG Release() override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
    
	virtual HRESULT GetLastError(HRESULT result, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT Config(IStream *, unsigned int flags) override;
	virtual HRESULT UpdateState(IStream *) override;
	virtual HRESULT ImportFolderChange(unsigned int nvals, SPropValue *) override;
	virtual HRESULT ImportFolderDeletion(unsigned int flags, ENTRYLIST *source_entry) override;
};

#endif
