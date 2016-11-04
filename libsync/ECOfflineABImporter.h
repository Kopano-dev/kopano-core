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

class OfflineABImporter _kc_final : public IECImportAddressbookChanges {
public:
	OfflineABImporter(IECServiceAdmin *lpDstServiceAdmin, IECServiceAdmin *lpSrcServiceAdmin);
	~OfflineABImporter();
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID iid, void **iface) _kc_override;
	virtual HRESULT __stdcall GetLastError(HRESULT hr, ULONG flags, LPMAPIERROR *err) _kc_override;
	virtual HRESULT __stdcall Config(LPSTREAM state, ULONG flags) _kc_override;
	virtual HRESULT __stdcall UpdateState(LPSTREAM state) _kc_override;
	virtual HRESULT __stdcall ImportABChange(ULONG obj_type, ULONG objid_size, LPENTRYID objid) _kc_override;
	virtual HRESULT __stdcall ImportABDeletion(ULONG type, ULONG objid_size, LPENTRYID objid) _kc_override;

private:
	IECServiceAdmin *m_lpSrcServiceAdmin;
	IECServiceAdmin *m_lpDstServiceAdmin;
	ECLogger		*m_lpLogger;
};

#endif // ndef 
