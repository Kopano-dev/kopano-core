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

#ifndef ECXPPROVIDER_H
#define ECXPPROVIDER_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>

class ECXPProvider : public ECUnknown
{
protected:
	ECXPProvider();
	virtual ~ECXPProvider();

public:
	static  HRESULT Create(ECXPProvider **lppECXPProvider);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

    virtual HRESULT Shutdown(ULONG * lpulFlags);
	virtual HRESULT TransportLogon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG FAR * lpulFlags, LPMAPIERROR FAR * lppMAPIError, LPXPLOGON FAR * lppXPLogon);

	class xXPProvider _zcp_final : public IXPProvider {
		// IUnknown
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;

		//IXPProvider
		virtual HRESULT __stdcall Shutdown(ULONG * lpulFlags);
		virtual HRESULT __stdcall TransportLogon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG FAR * lpulFlags, LPMAPIERROR FAR * lppMAPIError, LPXPLOGON FAR * lppXPLogon);
	}m_xXPProvider;
	
	LPSPropValue	m_lpIdentityProps;

};

#endif // #ifndef ECXPPROVIDER_H
