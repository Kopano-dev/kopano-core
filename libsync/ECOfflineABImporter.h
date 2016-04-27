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

#ifndef ECOFFLINEABIMPORTER_INCLUDED
#define ECOFFLINEABIMPORTER_INCLUDED

#include <kopano/zcdefs.h>
#include <IECImportAddressbookChanges.h>
#include <kopano/IECServiceAdmin.h>

class ECLogger;

class OfflineABImporter _zcp_final : public IECImportAddressbookChanges {
public:
	OfflineABImporter(IECServiceAdmin *lpDstServiceAdmin, IECServiceAdmin *lpSrcServiceAdmin);
	~OfflineABImporter();
	
	virtual ULONG __stdcall AddRef(void) _zcp_override;
	virtual ULONG __stdcall Release(void) _zcp_override;
	virtual HRESULT __stdcall QueryInterface(REFIID iid, void **lpvoid) _zcp_override;

	virtual HRESULT __stdcall GetLastError(HRESULT hr, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _zcp_override;
	virtual HRESULT __stdcall Config(LPSTREAM lpState, ULONG ulFlags) _zcp_override;
	virtual HRESULT __stdcall UpdateState(LPSTREAM lpState) _zcp_override;
			
	virtual HRESULT __stdcall ImportABChange(ULONG ulObjType, ULONG cbObjId, LPENTRYID lpObjId) _zcp_override;
	virtual HRESULT __stdcall ImportABDeletion(ULONG ulType, ULONG cbObjId, LPENTRYID lpObjId) _zcp_override;

private:
	IECServiceAdmin *m_lpSrcServiceAdmin;
	IECServiceAdmin *m_lpDstServiceAdmin;
	ECLogger		*m_lpLogger;
};

#endif // ndef 
