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

#ifndef ECMAILUSER
#define ECMAILUSER

#include <kopano/zcdefs.h>
#include <kopano/Util.h>
#include <mapidefs.h>
#include "ECABContainer.h"
#include "ECABProp.h"

class ECABLogon;

class ECDistList _kc_final : public ECABContainer, public IDistList {
	public:
	static HRESULT Create(ECABLogon *prov, BOOL modify, ECDistList **);
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);

	// Override IMAPIProp
	virtual HRESULT OpenProperty(ULONG proptag, const IID *intf, ULONG iface_opts, ULONG flags, IUnknown **) override;
	virtual HRESULT CopyTo(ULONG nexcl, const IID *excl, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;

	// override IUnknown
	virtual HRESULT	QueryInterface(REFIID refiid, void **) _kc_override;

	protected:
	ECDistList(ECABLogon *prov, BOOL modify);
	ALLOC_WRAP_FRIEND;
};

class ECMailUser _kc_final : public ECABProp, public IMailUser {
private:
	ECMailUser(ECABLogon *prov, BOOL modify);

public:
	static HRESULT Create(ECABLogon *prov, BOOL modify, ECMailUser **);
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);
	static HRESULT DefaultGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT OpenProperty(ULONG proptag, const IID *intf, ULONG iface_opts, ULONG flags, IUnknown **) override;
	virtual HRESULT CopyTo(ULONG nexcl, const IID *excl, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;
	ALLOC_WRAP_FRIEND;
};

#endif // #ifndef ECMAILUSER
