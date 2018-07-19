/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECABPROVIDER
#define ECABPROVIDER

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>

using namespace KC;

class ECABProvider _kc_final : public ECUnknown, public IABProvider {
protected:
	ECABProvider(ULONG ulFlags, const char *szClassName);
	virtual ~ECABProvider(void) = default;
public:
	static  HRESULT Create(ECABProvider **lppECABProvider);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Shutdown(ULONG *flags) override;
	virtual HRESULT Logon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profile, ULONG flags, ULONG *sec_size, BYTE **sec, MAPIERROR **, IABLogon **) override;

	ULONG m_ulFlags;
	ALLOC_WRAP_FRIEND;
};

class ECABProviderSwitch _kc_final : public ECUnknown, public IABProvider {
protected:
	ECABProviderSwitch(void);

public:
	static  HRESULT Create(ECABProviderSwitch **lppECABProvider);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Shutdown(ULONG *flags) override;
	virtual HRESULT Logon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profile, ULONG flags, ULONG *sec_size, BYTE **sec, MAPIERROR **, IABLogon **) override;
	ALLOC_WRAP_FRIEND;
};

#endif // #ifndef ECABPROVIDER
