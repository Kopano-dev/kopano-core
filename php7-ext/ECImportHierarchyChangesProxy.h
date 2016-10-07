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

#ifndef ECIMPORTHIERARCHYCHANGESPROXY_H
#define ECIMPORTHIERARCHYCHANGESPROXY_H

#include <edkmdb.h>

class ECImportHierarchyChangesProxy  : public IExchangeImportHierarchyChanges {
private:
    ULONG m_cRef;
	zval m_lpObj;
#ifdef ZTS
	TSRMLS_D;
#endif
public:
	ECImportHierarchyChangesProxy(const zval *v TSRMLS_DC);
    ~ECImportHierarchyChangesProxy();

    virtual ULONG 	__stdcall AddRef();
    virtual ULONG	__stdcall Release();
    virtual HRESULT __stdcall QueryInterface(REFIID iid, void **lpvoid);
    
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT __stdcall Config(LPSTREAM lpStream, ULONG ulFlags) ;
	virtual HRESULT __stdcall UpdateState(LPSTREAM lpStream);
	virtual HRESULT __stdcall ImportFolderChange(ULONG cValue, LPSPropValue lpPropArray);
	virtual HRESULT __stdcall ImportFolderDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList);
    
};

#endif
