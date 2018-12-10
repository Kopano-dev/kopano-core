/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECABPROVIDER
#define ECABPROVIDER

#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <kopano/zcdefs.h>
#include <mapispi.h>

class ZCABProvider KC_FINAL_OPG : public KC::ECUnknown, public IABProvider {
protected:
	ZCABProvider(ULONG ulFlags, const char *szClassName);

public:
	static  HRESULT Create(ZCABProvider **lppZCABProvider);
	virtual HRESULT QueryInterface(const IID &, void **) override;
    virtual HRESULT Shutdown(ULONG * lpulFlags);
	virtual HRESULT Logon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam, const TCHAR *lpszProfileName, ULONG ulFlags, ULONG *lpulcbSecurity, LPBYTE *lppbSecurity, LPMAPIERROR *lppMAPIError, LPABLOGON *lppABLogon);

private:
	ALLOC_WRAP_FRIEND;
};

#endif
