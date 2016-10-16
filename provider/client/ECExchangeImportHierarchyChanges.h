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

#ifndef ECEXCHANGEIMPORTCHIERARCHYCHANGES_H
#define ECEXCHANGEIMPORTCHIERARCHYCHANGES_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include "ECMAPIFolder.h"
#include <kopano/ECUnknown.h>

class ECExchangeImportHierarchyChanges _kc_final : public ECUnknown {
protected:
	ECExchangeImportHierarchyChanges(ECMAPIFolder *lpFolder);
	virtual ~ECExchangeImportHierarchyChanges();

public:
	static	HRESULT Create(ECMAPIFolder *lpFolder, LPEXCHANGEIMPORTHIERARCHYCHANGES* lppExchangeImportHierarchyChanges);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Config(LPSTREAM lpStream, ULONG ulFlags);
	virtual HRESULT UpdateState(LPSTREAM lpStream);
	virtual HRESULT ImportFolderChange(ULONG cValue, LPSPropValue lpPropArray);
	virtual HRESULT ImportFolderDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList);

	class xExchangeImportHierarchyChanges _zcp_final : public IExchangeImportHierarchyChanges{
		// IUnknown
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;

		// IExchangeImportContentsChanges
		virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
		virtual HRESULT __stdcall Config(LPSTREAM lpStream, ULONG ulFlags);
		virtual HRESULT __stdcall UpdateState(LPSTREAM lpStream);
		virtual HRESULT __stdcall ImportFolderChange(ULONG cValue, LPSPropValue lpPropArray);
		virtual HRESULT __stdcall ImportFolderDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList);
	} m_xExchangeImportHierarchyChanges;

private:
	ECMAPIFolder *m_lpFolder = nullptr;
	IStream *m_lpStream = nullptr;
	ULONG m_ulFlags = 0;
	ULONG m_ulSyncId = 0;
	ULONG m_ulChangeId = 0;
};

#endif // ECEXCHANGEIMPORTCHIERARCHYCHANGES_H
