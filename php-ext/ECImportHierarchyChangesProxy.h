/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECIMPORTHIERARCHYCHANGESPROXY_H
#define ECIMPORTHIERARCHYCHANGESPROXY_H

#include <kopano/zcdefs.h>
#include <edkmdb.h>

class ECImportHierarchyChangesProxy _kc_final :
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
	virtual ULONG AddRef(void) _kc_override;
	virtual ULONG Release(void) _kc_override;
	virtual HRESULT QueryInterface(REFIID iid, void **lpvoid) _kc_override;
    
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _kc_override;
	virtual HRESULT Config(LPSTREAM lpStream, ULONG ulFlags) _kc_override;
	virtual HRESULT UpdateState(LPSTREAM lpStream) _kc_override;
	virtual HRESULT ImportFolderChange(ULONG cValue, LPSPropValue lpPropArray) _kc_override;
	virtual HRESULT ImportFolderDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) _kc_override;
};

#endif
