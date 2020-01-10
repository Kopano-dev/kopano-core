/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ZCMAPIPROP_H
#define ZCMAPIPROP_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <mapidefs.h>
#include <kopano/charset/convert.h>
#include <map>

class ZCMAPIProp _no_final : public KC::ECUnknown, public IMailUser {
protected:
	ZCMAPIProp(ULONG ulObjType, const char *szClassName = NULL);
	virtual ~ZCMAPIProp();

	HRESULT ConvertMailUser(LPSPropTagArray lpNames, ULONG cValues, LPSPropValue lpProps, ULONG ulIndex);
	HRESULT ConvertDistList(ULONG cValues, LPSPropValue lpProps);
	HRESULT ConvertProps(IMAPIProp *contact, ULONG eid_size, const ENTRYID *eid, ULONG index);

	/* getprops helper */
	HRESULT CopyOneProp(KC::convert_context &, ULONG flags, const std::map<short, SPropValue>::const_iterator &, SPropValue *prop, SPropValue *base);

public:
	static HRESULT Create(IMAPIProp *lpContact, ULONG eid_size, const ENTRYID *eid, ZCMAPIProp **);
	virtual HRESULT QueryInterface(const IID &, void **) override;

	// From IMAPIProp
	virtual HRESULT GetLastError(HRESULT, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT SaveChanges(unsigned int flags) override;
	virtual HRESULT GetProps(const SPropTagArray *props, unsigned int flags, unsigned int *nvals, SPropValue **) override;
	virtual HRESULT GetPropList(unsigned int flags, SPropTagArray **) override;
	virtual HRESULT OpenProperty(unsigned int tag, const IID *intf, unsigned int intf_opts, unsigned int flags, IUnknown **) override;
	virtual HRESULT SetProps(unsigned int nvals, const SPropValue *, SPropProblemArray **) override;
	virtual HRESULT DeleteProps(const SPropTagArray *, SPropProblemArray **) override;
	virtual HRESULT CopyTo(unsigned int nexcl, const IID *iidexcl, const SPropTagArray *exclprop, unsigned int ui_param, IMAPIProgress *, const IID *intf, void *dst_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, unsigned int ui_param, IMAPIProgress *, const IID *intf, void *dst_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, ULONG flags, ULONG *nvals, MAPINAMEID ***names) override;
	virtual HRESULT GetIDsFromNames(unsigned int nnames, MAPINAMEID **names, unsigned int flags, SPropTagArray **) override;

private:
	SPropValue *m_base = nullptr;
	wchar_t empty[1]{};
	std::map<short, SPropValue> m_mapProperties;
	ULONG m_ulObject;
};

#endif
