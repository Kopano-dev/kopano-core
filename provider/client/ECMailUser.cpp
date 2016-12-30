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
 */
#include <kopano/platform.h>
#include <kopano/ECInterfaceDefs.h>
#include "resource.h"
#include <mapiutil.h>
#include "kcore.hpp"
#include <kopano/CommonUtil.h>
#include "ECMailUser.h"

#include "Mem.h"
#include <kopano/ECGuid.h>
#include <kopano/ECDebug.h>

ECMailUser::ECMailUser(void* lpProvider, BOOL fModify) : ECABProp(lpProvider, MAPI_MAILUSER, fModify, "IMailUser")
{
	// since we have no OpenProperty / abLoadProp, remove the 8k prop limit
	this->m_ulMaxPropSize = 0;
}

HRESULT ECMailUser::Create(void* lpProvider, BOOL fModify, ECMailUser** lppMailUser)
{

	HRESULT hr = hrSuccess;
	ECMailUser *lpMailUser = NULL;

	lpMailUser = new ECMailUser(lpProvider, fModify);

	hr = lpMailUser->QueryInterface(IID_ECMailUser, (void **)lppMailUser);

	if(hr != hrSuccess)
		delete lpMailUser;

	return hr;
}

HRESULT	ECMailUser::QueryInterface(REFIID refiid, void **lppInterface) 
{
	REGISTER_INTERFACE2(ECMailUser, this);
	REGISTER_INTERFACE2(ECABProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMailUser, &this->m_xMailUser);
	REGISTER_INTERFACE2(IMAPIProp, &this->m_xMailUser);
	REGISTER_INTERFACE2(IUnknown, &this->m_xMailUser);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMailUser::TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType)
{
	return MAPI_E_NOT_FOUND;
}

HRESULT ECMailUser::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	HRESULT			hr = MAPI_E_NOT_FOUND;

	if (lpiid == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if (ulFlags & MAPI_CREATE)
		// Don't support creating any sub-objects
		return MAPI_E_NO_ACCESS;

	switch(ulPropTag) {
	default:
		hr = ECABProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
		break;
	}
	return hr;
}

HRESULT ECMailUser::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems)
{
	return this->GetABStore()->m_lpMAPISup->DoCopyTo(&IID_IMailUser, &this->m_xMailUser, ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECMailUser::CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems)
{
	return this->GetABStore()->m_lpMAPISup->DoCopyProps(&IID_IMailUser, &this->m_xMailUser, lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

// IMailUser
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMailUser, MailUser, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMailUser, MailUser, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, GetLastError, (HRESULT, hError), (ULONG, ulFlags), (LPMAPIERROR *, lppMapiError))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, SaveChanges, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, GetProps, (const SPropTagArray *, lpPropTagArray), (ULONG, ulFlags), (ULONG *, lpcValues), (SPropValue **, lppPropArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, GetPropList, (ULONG, ulFlags), (LPSPropTagArray *, lppPropTagArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, OpenProperty, (ULONG, ulPropTag), (LPCIID, lpiid), (ULONG, ulInterfaceOptions), (ULONG, ulFlags), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, SetProps, (ULONG, cValues), (LPSPropValue, lpPropArray), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, DeleteProps, (LPSPropTagArray, lpPropTagArray), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, CopyTo, (ULONG, ciidExclude), (LPCIID, rgiidExclude), (LPSPropTagArray, lpExcludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (LPVOID, lpDestObj), (ULONG, ulFlags), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, CopyProps, (LPSPropTagArray, lpIncludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (LPVOID, lpDestObj), (ULONG, ulFlags), (LPSPropProblemArray *, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, GetNamesFromIDs, (LPSPropTagArray *, pptaga), (LPGUID, lpguid), (ULONG, ulFlags), (ULONG *, pcNames), (LPMAPINAMEID **, pppNames))
DEF_HRMETHOD1(TRACE_MAPI, ECMailUser, MailUser, GetIDsFromNames, (ULONG, cNames), (LPMAPINAMEID *, ppNames), (ULONG, ulFlags), (LPSPropTagArray *, pptaga))
