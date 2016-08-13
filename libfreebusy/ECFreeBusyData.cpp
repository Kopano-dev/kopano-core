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

#include <kopano/platform.h>
#include "ECFreeBusyData.h"

#include "ECEnumFBBlock.h"

ECFreeBusyData::ECFreeBusyData(void)
{
	m_rtmStart = 0;
	m_rtmEnd = 0;
}

HRESULT ECFreeBusyData::Init(LONG rtmStart, LONG rtmEnd, ECFBBlockList* lpfbBlockList)
{
	HRESULT hr = hrSuccess;

	if(lpfbBlockList == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	m_rtmStart = rtmStart;
	m_rtmEnd = rtmEnd;

	m_fbBlockList.Copy(lpfbBlockList);

	// Update the start time if missing.
	if (m_rtmStart == 0) {
		FBBlock_1 blk;
		if (m_fbBlockList.Next(&blk) == hrSuccess)
			m_rtmStart = blk.m_tmStart;
		m_fbBlockList.Reset();
	}

	// Update the end time if missing.
	if (m_rtmEnd == 0)
		m_fbBlockList.GetEndTime(&m_rtmEnd);

exit:
	return hr;
}

HRESULT ECFreeBusyData::Create(ECFreeBusyData **lppECFreeBusyData)
{
	HRESULT hr = hrSuccess;
	ECFreeBusyData *lpECFreeBusyData = NULL;

	lpECFreeBusyData = new ECFreeBusyData();

	hr = lpECFreeBusyData->QueryInterface(IID_ECFreeBusyData, (void **)lppECFreeBusyData);

	if(hr != hrSuccess)
		delete lpECFreeBusyData;

	return hr;
}

HRESULT ECFreeBusyData::QueryInterface(REFIID refiid, void** lppInterface)
{
	REGISTER_INTERFACE(IID_ECFreeBusyData, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IFreeBusyData, &this->m_xFreeBusyData);
	REGISTER_INTERFACE(IID_ECUnknown, &this->m_xFreeBusyData);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECFreeBusyData::EnumBlocks(IEnumFBBlock **ppenumfb, FILETIME ftmStart, FILETIME ftmEnd)
{
	HRESULT			hr = S_OK;
	LONG			rtmStart = 0;
	LONG			rtmEnd = 0;
	ECEnumFBBlock*	lpECEnumFBBlock = NULL;

	if(ppenumfb == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	FileTimeToRTime(&ftmStart, &rtmStart);
	FileTimeToRTime(&ftmEnd, &rtmEnd);

	hr = m_fbBlockList.Restrict(rtmStart, rtmEnd);
	if(hr != hrSuccess)
		goto exit;

	hr = ECEnumFBBlock::Create(&m_fbBlockList, &lpECEnumFBBlock);
	if(hr != hrSuccess)
		goto exit;

	hr = lpECEnumFBBlock->QueryInterface(IID_IEnumFBBlock, (void**)ppenumfb);
	if(hr != hrSuccess)
		goto exit;
	
exit:
	if(lpECEnumFBBlock)
		lpECEnumFBBlock->Release();

	return hr;
}

/**
 * Documentation of this function cannot be found. This is what I think it does.
 *
 * Find first free block inside the specified range <ulBegin,ulEnd]. Note that ulBegin is non-inclusive, so 
 * the earliest block that can be returned is starts at ulBegin + 1.
 *
 * I think that this function should normally look for the first position in the given range in which the specified
 * duration (ulMinutes) fits. However, in practice, it is only called to check if range <ulBegin, ulEnd] is free, since
 * ulEnd - ulBegin - 1 == ulMinutes. This means we can use a much simpler algorithm, and just check if the entire range
 * is free, and return that if it is.
 *
 * It is my theory that someone made this function, but later found out that it is not very useful since you always want to 
 * find a specific slot, not the first slot that fits, so now it is only used to check for availability.
 *
 * @param ulBegin Begin time as RTIME
 * @param ulMinutes Duration of the slot to find
 * @param ulNumber (Guess) Number of resources that should be free at this moment (always one in my tests)
 * @param bA (Unknown) always TRUE
 * @param ulEnd End time as RTIME
 * @param ulUnknown Unknown, always 0
 * @param ulMinutesPerDay Unknown, always set to 1440 ( = 24 * 60 )
 * @result 0 for OK, anything else is an error
 */
HRESULT ECFreeBusyData::FindFreeBlock(LONG ulBegin, LONG ulMinutes, LONG ulNumber, BOOL bA, LONG ulEnd, LONG ulUnknown, LONG ulMinutesPerDay, FBBlock_1 *lpBlock)
{
	HRESULT hr = hrSuccess;
	FBBlock_1 sBlock;
	BOOL bOverlap = false;

	if(ulBegin+1+ulMinutes > ulEnd) {
		// Requested slot can never fit between start and end
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	m_fbBlockList.Reset();

	// Loop through FB data to find if there is a block that overlaps the requested slot
	while(TRUE) {
		hr = m_fbBlockList.Next(&sBlock);
		if(hr != hrSuccess)
			break;

		if(sBlock.m_tmStart >= ulEnd)
			break;

		if(sBlock.m_tmEnd > ulBegin+1 && sBlock.m_tmStart < ulEnd) {
			bOverlap = true;
			break;
		}
	}

	if (!bOverlap) {
		hr = hrSuccess;
		lpBlock->m_fbstatus = fbFree;
		lpBlock->m_tmStart = ulBegin+1;
		lpBlock->m_tmEnd = lpBlock->m_tmStart + ulMinutes;
	} else {
		hr = MAPI_E_NOT_FOUND;
	}

exit:
	return hr;
}

HRESULT ECFreeBusyData::SetFBRange(LONG rtmStart, LONG rtmEnd)
{
	m_rtmStart = rtmStart;
	m_rtmEnd = rtmEnd;
	return S_OK;
}

HRESULT ECFreeBusyData::GetFBPublishRange(LONG *prtmStart, LONG *prtmEnd)
{
	HRESULT hr = S_OK;

	if(prtmStart == NULL || prtmEnd == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	*prtmStart = m_rtmStart;
	*prtmEnd = m_rtmEnd;

exit:
	return hr;
}

// Interfaces
//		IUnknown
//		IFreeBusyData
HRESULT __stdcall ECFreeBusyData::xFreeBusyData::QueryInterface(REFIID refiid , void** lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::QueryInterface", "");
	METHOD_PROLOGUE_(ECFreeBusyData, FreeBusyData);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusyData::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

ULONG __stdcall ECFreeBusyData::xFreeBusyData::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::AddRef", "");
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	return pThis->AddRef();
}

ULONG __stdcall ECFreeBusyData::xFreeBusyData::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::Release", "");
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	return pThis->Release();
}

HRESULT __stdcall ECFreeBusyData::xFreeBusyData::Reload(void* lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::Reload", "");
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	HRESULT hr = pThis->Reload(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusyData::Reload", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusyData::xFreeBusyData::EnumBlocks(IEnumFBBlock **ppenumfb, FILETIME ftmStart, FILETIME ftmEnd)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::EnumBlocks", "");
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	HRESULT hr = pThis->EnumBlocks(ppenumfb, ftmStart, ftmEnd);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusyData::EnumBlocks", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusyData::xFreeBusyData::Merge(void* lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::Merge", "");
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	HRESULT hr = pThis->Merge(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusyData::Placeholder2", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusyData::xFreeBusyData::GetDelegateInfo(void* lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::GetDelegateInfo", "");
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	HRESULT hr = pThis->GetDelegateInfo(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusyData::GetDelegateInfo", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusyData::xFreeBusyData::FindFreeBlock(LONG ulBegin, LONG ulMinutes, LONG ulNumber, BOOL bA, LONG ulEnd, LONG ulUnknown, LONG ulMinutesPerDay, FBBlock_1 *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::FindFreeBlock", "%x %x %x %s %x %x %x %x", ulBegin, ulMinutes, ulNumber, bA ? "true" : "false", ulEnd, ulUnknown, ulMinutesPerDay, lpData);
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	HRESULT hr = pThis->FindFreeBlock(ulBegin, ulMinutes, ulNumber, bA, ulEnd, ulUnknown, ulMinutesPerDay, lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusyData::FindFreeBlock", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusyData::xFreeBusyData::InterSect(void *lpData1, LONG ulA, void *lpData2)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::InterSect", "");
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	HRESULT hr = pThis->InterSect(lpData1, ulA, lpData2);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusyData::InterSect", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusyData::xFreeBusyData::SetFBRange(LONG rtmStart, LONG rtmEnd)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::SetFBRange", "rtmStart=%d, rtmEnd=%d", rtmStart, rtmEnd);
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	HRESULT hr = pThis->SetFBRange(rtmStart, rtmEnd);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusyData::SetFBRange", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusyData::xFreeBusyData::NextFBAppt(void *lpData1, ULONG ulA, void *lpData2, ULONG ulB, void *lpData3, void *lpData4)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::NextFBAppt", "");
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	HRESULT hr = pThis->NextFBAppt(lpData1, ulA, lpData2, ulB, lpData3, lpData4);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusyData::NextFBAppt", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusyData::xFreeBusyData::GetFBPublishRange(LONG *prtmStart, LONG *prtmEnd)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusyData::GetFBPublishRange", "");
	METHOD_PROLOGUE_(ECFreeBusyData , FreeBusyData);
	HRESULT hr = pThis->GetFBPublishRange(prtmStart, prtmEnd);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusyData::GetFBPublishRange", "%s, prtmStart=%d, prtmEnd=%d", GetMAPIErrorDescription(hr).c_str(), (prtmStart)?*prtmStart:0,(prtmEnd)?*prtmEnd : 0);
	return hr;
}
