/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <kopano/timeutil.hpp>
#include "ECFreeBusyUpdate.h"
#include "freebusytags.h"
#include "freebusyutil.h"

namespace KC {

ECFreeBusyUpdate::ECFreeBusyUpdate(IMessage *lpMessage) :
	m_lpMessage(lpMessage)
{}

HRESULT ECFreeBusyUpdate::Create(IMessage* lpMessage, ECFreeBusyUpdate **lppECFreeBusyUpdate)
{
	return alloc_wrap<ECFreeBusyUpdate>(lpMessage).put(lppECFreeBusyUpdate);
}

HRESULT ECFreeBusyUpdate::QueryInterface(REFIID refiid, void** lppInterface)
{
	REGISTER_INTERFACE2(ECFreeBusyUpdate, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IFreeBusyUpdate, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECFreeBusyUpdate::PublishFreeBusy(const FBBlock_1 *lpBlocks, ULONG nBlocks)
{
	if(nBlocks > 0 && lpBlocks == NULL)
		return MAPI_E_INVALID_PARAMETER;
	for (ULONG i = 0; i < nBlocks; ++i)
		m_fbBlockList.Add(lpBlocks[i]);
	return S_OK;
}

HRESULT ECFreeBusyUpdate::ResetPublishedFreeBusy()
{
	m_fbBlockList.Clear();
	return S_OK;
}

HRESULT ECFreeBusyUpdate::SaveChanges(const FILETIME &ftStart,
    const FILETIME &ftEnd)
{
	unsigned int cValues = 0, cProps = 0, ulMonths;
	memory_ptr<SPropValue> lpPropArray, lpPropFBDataArray;
	FILETIME		ft;	
	struct tm tmStart, tmEnd;
	static constexpr const SizedSPropTagArray(8, sPropsFBDelete) = {
		8,
		{
			PR_FREEBUSY_ALL_EVENTS,
			PR_FREEBUSY_ALL_MONTHS,
			PR_FREEBUSY_BUSY_EVENTS,
			PR_FREEBUSY_BUSY_MONTHS,
			PR_FREEBUSY_OOF_EVENTS,
			PR_FREEBUSY_OOF_MONTHS,
			PR_FREEBUSY_TENTATIVE_EVENTS,
			PR_FREEBUSY_TENTATIVE_MONTHS
		}
	};
	auto rtmStart = FileTimeToRTime(ftStart);
	auto rtmEnd   = FileTimeToRTime(ftEnd);
	auto clean    = make_scope_success([&]() { m_fbBlockList.Reset(); });

	if(m_lpMessage == NULL)
		return MAPI_E_INVALID_OBJECT;
	if((ULONG)rtmStart > (ULONG)rtmEnd)
		return MAPI_E_BAD_VALUE;

	GetSystemTimeAsFileTime(&ft);

	// Restrict on start and end date
	m_fbBlockList.Restrict(rtmStart, rtmEnd);

	//Calculate months
	gmtime_safe(RTimeToUnixTime(rtmStart), &tmStart);
	gmtime_safe(RTimeToUnixTime(rtmEnd), &tmEnd);
	ulMonths = DiffYearMonthToMonth(&tmStart, &tmEnd);
	if(ulMonths == 0)
		++ulMonths;

	cValues = 9;
	cProps = 0;
	auto hr = MAPIAllocateBuffer(sizeof(SPropValue) * cValues, &~lpPropArray);
	if (hr != hrSuccess)
		return hr;

	lpPropArray[cProps].ulPropTag = PR_FREEBUSY_LAST_MODIFIED;
	lpPropArray[cProps++].Value.ft = ft;
	lpPropArray[cProps].ulPropTag = PR_FREEBUSY_START_RANGE;
	lpPropArray[cProps++].Value.l = rtmStart;
	lpPropArray[cProps].ulPropTag = PR_FREEBUSY_END_RANGE;
	lpPropArray[cProps++].Value.l = rtmEnd;
	lpPropArray[cProps].ulPropTag = PR_FREEBUSY_NUM_MONTHS;
	lpPropArray[cProps++].Value.l = ulMonths;	
	hr = m_lpMessage->SetProps(cProps, lpPropArray, NULL);
	if(hr != hrSuccess)
		return hr;

	// Delete all free/busy data properties	
	hr = m_lpMessage->DeleteProps(sPropsFBDelete, NULL);
  	if(hr != hrSuccess)
		return hr;
	if (CreateFBProp(fbKopanoAllBusy, ulMonths, PR_FREEBUSY_ALL_MONTHS,
	    PR_FREEBUSY_ALL_EVENTS, &m_fbBlockList, &~lpPropFBDataArray) == hrSuccess) {
		hr = m_lpMessage->SetProps(2, lpPropFBDataArray, NULL);
		if(hr != hrSuccess)
			return hr;
	}
	if (CreateFBProp(fbBusy, ulMonths, PR_FREEBUSY_BUSY_MONTHS,
	    PR_FREEBUSY_BUSY_EVENTS, &m_fbBlockList, &~lpPropFBDataArray) == hrSuccess) {
		hr = m_lpMessage->SetProps(2, lpPropFBDataArray, NULL);
		if(hr != hrSuccess)
			return hr;
	}
	if (CreateFBProp(fbTentative, ulMonths, PR_FREEBUSY_TENTATIVE_MONTHS,
	    PR_FREEBUSY_TENTATIVE_EVENTS, &m_fbBlockList, &~lpPropFBDataArray) == hrSuccess) {
		hr = m_lpMessage->SetProps(2, lpPropFBDataArray, NULL);
		if(hr != hrSuccess)
			return hr;
	}
	if (CreateFBProp(fbOutOfOffice, ulMonths, PR_FREEBUSY_OOF_MONTHS,
	    PR_FREEBUSY_OOF_EVENTS, &m_fbBlockList, &~lpPropFBDataArray) == hrSuccess) {
		hr = m_lpMessage->SetProps(2, lpPropFBDataArray, NULL);
		if(hr != hrSuccess)
			return hr;
	}
	return m_lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
}

} /* namespace */
