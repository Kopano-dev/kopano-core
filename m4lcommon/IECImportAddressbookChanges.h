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

#ifndef IECIMPORTADDRESSBOOKCHANGES_H
#define IECIMPORTADDRESSBOOKCHANGES_H

#include <mapidefs.h>
#include <kopano/ECDefs.h>

class IECImportAddressbookChanges : public IUnknown {
public:
	virtual HRESULT __stdcall GetLastError(HRESULT hr, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT __stdcall Config(LPSTREAM lpState, ULONG ulFlags) = 0;
	virtual HRESULT __stdcall UpdateState(LPSTREAM lpState) = 0;

	virtual HRESULT __stdcall ImportABChange(ULONG type, ULONG cbObjId, LPENTRYID lpObjId) = 0;
	virtual HRESULT __stdcall ImportABDeletion(ULONG type, ULONG cbObjId, LPENTRYID lpObjId) = 0;
};

#endif
