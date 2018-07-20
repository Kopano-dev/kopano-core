/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECIMPORTCONTENTSCHANGESPROXY_H
#define ECIMPORTCONTENTSCHANGESPROXY_H

#include <kopano/zcdefs.h>
#include <edkmdb.h>

class ECImportContentsChangesProxy _kc_final :
    public IExchangeImportContentsChanges {
private:
    ULONG m_cRef;
    zval *m_lpObj;
#ifdef ZTS
	TSRMLS_D;
#endif
public:
    ECImportContentsChangesProxy(zval *objTarget TSRMLS_DC);
    ~ECImportContentsChangesProxy();
	virtual ULONG AddRef(void) _kc_override;
	virtual ULONG Release(void) _kc_override;
	virtual HRESULT QueryInterface(REFIID iid, void **lpvoid) _kc_override;
    
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _kc_override;
	virtual HRESULT Config(LPSTREAM lpStream, ULONG ulFlags) _kc_override;
	virtual HRESULT UpdateState(LPSTREAM lpStream) _kc_override;
	virtual HRESULT ImportMessageChange(ULONG cValue, LPSPropValue lpPropArray, ULONG ulFlags, LPMESSAGE *lppMessage) _kc_override;
	virtual HRESULT ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) _kc_override;
	virtual HRESULT ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState) _kc_override;
	virtual HRESULT ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE *pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE *pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE *pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE *pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage) _kc_override;
};

#endif
