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

#ifndef ECABPROVIDERSWITCH_H
#define ECABPROVIDERSWITCH_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>

class ECABProviderSwitch _kc_final : public ECUnknown {
protected:
	ECABProviderSwitch(void);

public:
	static  HRESULT Create(ECABProviderSwitch **lppECABProvider);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

    virtual HRESULT Shutdown(ULONG * lpulFlags);
	virtual HRESULT Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG ulFlags, ULONG * lpulcbSecurity, LPBYTE * lppbSecurity, LPMAPIERROR * lppMAPIError, LPABLOGON * lppABLogon);

	class xABProvider _zcp_final : public IABProvider {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IABProvider.hpp>
	} m_xABProvider;
};

#endif // #ifndef ECABPROVIDERSWITCH_H
