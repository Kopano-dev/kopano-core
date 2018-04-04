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
#include <kopano/Util.h>
#include <kopano/memory.hpp>

using namespace KC;

class ECExchangeImportHierarchyChanges _kc_final :
    public ECUnknown, public IExchangeImportHierarchyChanges {
protected:
	ECExchangeImportHierarchyChanges(ECMAPIFolder *lpFolder);
public:
	static	HRESULT Create(ECMAPIFolder *lpFolder, LPEXCHANGEIMPORTHIERARCHYCHANGES* lppExchangeImportHierarchyChanges);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Config(LPSTREAM lpStream, ULONG ulFlags);
	virtual HRESULT UpdateState(LPSTREAM lpStream);
	virtual HRESULT ImportFolderChange(ULONG cValue, LPSPropValue lpPropArray);
	virtual HRESULT ImportFolderDeletion(ULONG ulFlags, LPENTRYLIST lpSourceEntryList);

private:
	KC::object_ptr<ECMAPIFolder> m_lpFolder;
	IStream *m_lpStream = nullptr;
	unsigned int m_ulFlags = 0, m_ulSyncId = 0, m_ulChangeId = 0;
	ALLOC_WRAP_FRIEND;
};

#endif // ECEXCHANGEIMPORTCHIERARCHYCHANGES_H
