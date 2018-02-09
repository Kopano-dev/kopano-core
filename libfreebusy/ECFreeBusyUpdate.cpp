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
#include <new>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
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
	HRESULT			hr = hrSuccess;
	ULONG			cValues = 0;
	ULONG			cProps = 0;
	ULONG			ulMonths;
	memory_ptr<SPropValue> lpPropArray, lpPropFBDataArray;
	FILETIME		ft;	
	struct tm		tmStart;
	struct tm		tmEnd;
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

	if(m_lpMessage == NULL)
	{
		hr = MAPI_E_INVALID_OBJECT;
		goto exit;
	}

	if((ULONG)rtmStart > (ULONG)rtmEnd)
	{
		hr = MAPI_E_BAD_VALUE;
		goto exit;
	}

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
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * cValues, &~lpPropArray);
	if (hr != hrSuccess)
		goto exit;

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
		goto exit;

	// Delete all free/busy data properties	
	hr = m_lpMessage->DeleteProps(sPropsFBDelete, NULL);
  	if(hr != hrSuccess)
		goto exit;
	if (CreateFBProp(fbKopanoAllBusy, ulMonths, PR_FREEBUSY_ALL_MONTHS,
	    PR_FREEBUSY_ALL_EVENTS, &m_fbBlockList, &~lpPropFBDataArray) == hrSuccess) {
		hr = m_lpMessage->SetProps(2, lpPropFBDataArray, NULL);
		if(hr != hrSuccess)
			goto exit;
	}
	if (CreateFBProp(fbBusy, ulMonths, PR_FREEBUSY_BUSY_MONTHS,
	    PR_FREEBUSY_BUSY_EVENTS, &m_fbBlockList, &~lpPropFBDataArray) == hrSuccess) {
		hr = m_lpMessage->SetProps(2, lpPropFBDataArray, NULL);
		if(hr != hrSuccess)
			goto exit;
	}
	if (CreateFBProp(fbTentative, ulMonths, PR_FREEBUSY_TENTATIVE_MONTHS,
	    PR_FREEBUSY_TENTATIVE_EVENTS, &m_fbBlockList, &~lpPropFBDataArray) == hrSuccess) {
		hr = m_lpMessage->SetProps(2, lpPropFBDataArray, NULL);
		if(hr != hrSuccess)
			goto exit;
	}
	if (CreateFBProp(fbOutOfOffice, ulMonths, PR_FREEBUSY_OOF_MONTHS,
	    PR_FREEBUSY_OOF_EVENTS, &m_fbBlockList, &~lpPropFBDataArray) == hrSuccess) {
		hr = m_lpMessage->SetProps(2, lpPropFBDataArray, NULL);
		if(hr != hrSuccess)
			goto exit;
	}

	hr = m_lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if(hr != hrSuccess)
		goto exit;

exit:
	m_fbBlockList.Reset();
	return hr;
}

} /* namespace */
