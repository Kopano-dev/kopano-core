/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include "resource.h"
#include <mapiutil.h>
#include "kcore.hpp"
#include <kopano/CommonUtil.h>
#include "ECMailUser.h"
#include "ECMAPITable.h"
#include "Mem.h"
#include <kopano/ECGuid.h>

ECDistList::ECDistList(ECABLogon *prov, BOOL modify) :
	ECABContainer(prov, MAPI_DISTLIST, modify, "IDistList")
{
	// since we have no OpenProperty / abLoadProp, remove the 8k prop limit
	m_ulMaxPropSize = 0;
}

HRESULT ECDistList::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECDistList, this);
	REGISTER_INTERFACE2(ECABContainer, this);
	REGISTER_INTERFACE2(ECABProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IDistList, this);
	REGISTER_INTERFACE2(IABContainer, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECDistList::Create(ECABLogon *lpProvider, BOOL fModify,
    ECDistList **lppDistList)
{
	return KC::alloc_wrap<ECDistList>(lpProvider, fModify).put(lppDistList);
}

HRESULT ECDistList::TableRowGetProp(void *provider, const struct propVal *src,
    SPropValue *dst, void **base, ULONG type)
{
	return MAPI_E_NOT_FOUND;
}

HRESULT ECDistList::OpenProperty(ULONG ulPropTag, LPCIID lpiid,
    ULONG ulInterfaceOptions, ULONG ulFlags, IUnknown **lppUnk)
{
	if (lpiid == NULL)
		return MAPI_E_INVALID_PARAMETER;
	return ECABProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions,
	       ulFlags, lppUnk);
}

HRESULT ECDistList::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return GetABStore()->m_lpMAPISup->DoCopyTo(&IID_IDistList,
	       static_cast<IDistList *>(this), ciidExclude, rgiidExclude,
	       lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj,
	       ulFlags, lppProblems);
}

HRESULT ECDistList::CopyProps(const SPropTagArray *lpIncludeProps,
    ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return GetABStore()->m_lpMAPISup->DoCopyProps(&IID_IDistList,
	       static_cast<IDistList *>(this), lpIncludeProps, ulUIParam,
	       lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

ECMailUser::ECMailUser(ECABLogon *prov, BOOL modify) :
	ECABProp(prov, MAPI_MAILUSER, modify, "IMailUser")
{
	// since we have no OpenProperty / abLoadProp, remove the 8k prop limit
	m_ulMaxPropSize = 0;
}

HRESULT ECMailUser::Create(ECABLogon *lpProvider, BOOL fModify,
    ECMailUser **lppMailUser)
{
	return KC::alloc_wrap<ECMailUser>(lpProvider, fModify).put(lppMailUser);
}

HRESULT ECMailUser::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMailUser, this);
	REGISTER_INTERFACE2(ECABProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMailUser, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMailUser::TableRowGetProp(void *lpProvider, const struct propVal *src,
    SPropValue *dst, void **base, ULONG type)
{
	return MAPI_E_NOT_FOUND;
}

HRESULT ECMailUser::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	if (lpiid == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (ulFlags & MAPI_CREATE)
		// Don't support creating any sub-objects
		return MAPI_E_NO_ACCESS;
	return ECABProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
}

HRESULT ECMailUser::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return GetABStore()->m_lpMAPISup->DoCopyTo(&IID_IMailUser,
	       static_cast<IMailUser *>(this), ciidExclude, rgiidExclude,
	       lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj,
	       ulFlags, lppProblems);
}

HRESULT ECMailUser::CopyProps(const SPropTagArray *lpIncludeProps,
    ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return GetABStore()->m_lpMAPISup->DoCopyProps(&IID_IMailUser,
	       static_cast<IMailUser *>(this), lpIncludeProps, ulUIParam,
	       lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}
