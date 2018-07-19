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
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

	// From IMAPIProp
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR * lppMAPIError);
	virtual HRESULT SaveChanges(ULONG ulFlags);
	virtual HRESULT GetProps(const SPropTagArray *lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray);
	virtual HRESULT GetPropList(ULONG ulFlags, LPSPropTagArray * lppPropTagArray);
	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN * lppUnk);
	virtual HRESULT SetProps(ULONG cValues, const SPropValue *lpPropArray, LPSPropProblemArray *lppProblems);
	virtual HRESULT DeleteProps(const SPropTagArray *lpPropTagArray, LPSPropProblemArray *lppProblems);
	virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT CopyProps(const SPropTagArray *lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems);
	virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, ULONG flags, ULONG *nvals, MAPINAMEID ***names) override;
	virtual HRESULT GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID * lppPropNames, ULONG ulFlags, LPSPropTagArray * lppPropTags);

private:
	SPropValue *m_base = nullptr;
	WCHAR empty[1] = {0};
	std::map<short, SPropValue> m_mapProperties;
	ULONG m_ulObject;
};

#endif
