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

	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID iid, void **lpvoid) _kc_override;
    
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _kc_override;
	virtual HRESULT __stdcall Config(LPSTREAM lpStream, ULONG ulFlags) _kc_override;
	virtual HRESULT __stdcall UpdateState(LPSTREAM lpStream) _kc_override;
	virtual HRESULT __stdcall ImportMessageChange(ULONG cValue, LPSPropValue lpPropArray, ULONG ulFlags, LPMESSAGE *lppMessage) _kc_override;
	virtual HRESULT __stdcall ImportMessageDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList) _kc_override;
	virtual HRESULT __stdcall ImportPerUserReadStateChange(ULONG cElements, LPREADSTATE lpReadState) _kc_override;
	virtual HRESULT __stdcall ImportMessageMove(ULONG cbSourceKeySrcFolder, BYTE *pbSourceKeySrcFolder, ULONG cbSourceKeySrcMessage, BYTE *pbSourceKeySrcMessage, ULONG cbPCLMessage, BYTE *pbPCLMessage, ULONG cbSourceKeyDestMessage, BYTE *pbSourceKeyDestMessage, ULONG cbChangeNumDestMessage, BYTE *pbChangeNumDestMessage) _kc_override;
};

#endif
