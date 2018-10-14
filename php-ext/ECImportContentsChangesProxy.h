/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECIMPORTCONTENTSCHANGESPROXY_H
#define ECIMPORTCONTENTSCHANGESPROXY_H

#include <edkmdb.h>

class ECImportContentsChangesProxy final :
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
	virtual ULONG AddRef() override;
	virtual ULONG Release() override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
    
	virtual HRESULT GetLastError(HRESULT result, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT Config(IStream *, unsigned int flags) override;
	virtual HRESULT UpdateState(IStream *) override;
	virtual HRESULT ImportMessageChange(unsigned int nvals, SPropValue *, unsigned int flags, IMessage **) override;
	virtual HRESULT ImportMessageDeletion(unsigned int flags, ENTRYLIST *source_entry) override;
	virtual HRESULT ImportPerUserReadStateChange(unsigned int nelem, READSTATE *) override;
	virtual HRESULT ImportMessageMove(unsigned int srcfld_size, BYTE *sk_srcfld, unsigned int msg_size, BYTE *sk_msg, unsigned int pclmsg_size, BYTE *pclmsg, unsigned int dstmsg_size, BYTE *sk_dstmsg, unsigned int cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage) override;
};

#endif
