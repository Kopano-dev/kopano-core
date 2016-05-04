/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef ECIMPORTCONTENTSCHANGESPROXY_H
#define ECIMPORTCONTENTSCHANGESPROXY_H

#include <edkmdb.h>

class ECImportContentsChangesProxy  : public IExchangeImportContentsChanges {
private:
    ULONG m_cRef;
    zval *m_lpObj;
#ifdef ZTS
	TSRMLS_D;
#endif
public:
    ECImportContentsChangesProxy(zval *objTarget TSRMLS_DC);
    ~ECImportContentsChangesProxy();

    virtual ULONG 	__stdcall AddRef();
    virtual ULONG	__stdcall Release();
    virtual HRESULT __stdcall QueryInterface(REFIID iid, void **lpvoid);
    
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT __stdcall Config(LPSTREAM lpStream, ULONG ulFlags) ;
	virtual HRESULT __stdcall UpdateState(LPSTREAM lpStream);
	virtual HRESULT __stdcall ImportMessageChange(ULONG cValue, LPSPropValue lpPropArray, ULONG ulFlags, LPMESSAGE * lppMessage);
	virtual HRESULT __stdcall ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList);
	virtual HRESULT __stdcall ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState);
	virtual HRESULT __stdcall ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE FAR * pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE FAR * pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE FAR * pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE FAR * pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE FAR * pbChangeNumDestMessage);
    
};

#endif
