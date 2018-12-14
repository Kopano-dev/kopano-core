/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include <kopano/timeutil.hpp>
#include "ECFreeBusyData.h"
#include "ECEnumFBBlock.h"

namespace KC {

ECFreeBusyData::ECFreeBusyData(LONG rtmStart, LONG rtmEnd,
    const ECFBBlockList &lpfbBlockList) :
	m_fbBlockList(lpfbBlockList), m_rtmStart(rtmStart), m_rtmEnd(rtmEnd)
{
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
}

HRESULT ECFreeBusyData::Create(LONG start, LONG end,
    const ECFBBlockList &bl, ECFreeBusyData **out)
{
	return alloc_wrap<ECFreeBusyData>(start, end, bl).put(out);
}

HRESULT ECFreeBusyData::QueryInterface(REFIID refiid, void** lppInterface)
{
	REGISTER_INTERFACE2(ECFreeBusyData, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IFreeBusyData, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECFreeBusyData::EnumBlocks(IEnumFBBlock **ppenumfb,
    const FILETIME &ftmStart, const FILETIME &ftmEnd)
{
	object_ptr<ECEnumFBBlock> lpECEnumFBBlock;
	if(ppenumfb == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = m_fbBlockList.Restrict(FileTimeToRTime(ftmStart), FileTimeToRTime(ftmEnd));
	if(hr != hrSuccess)
		return hr;
	hr = ECEnumFBBlock::Create(&m_fbBlockList, &~lpECEnumFBBlock);
	if(hr != hrSuccess)
		return hr;
	return lpECEnumFBBlock->QueryInterface(IID_IEnumFBBlock, (void**)ppenumfb);
}

HRESULT ECFreeBusyData::SetFBRange(LONG rtmStart, LONG rtmEnd)
{
	m_rtmStart = rtmStart;
	m_rtmEnd = rtmEnd;
	return S_OK;
}

HRESULT ECFreeBusyData::GetFBPublishRange(LONG *prtmStart, LONG *prtmEnd)
{
	if(prtmStart == NULL || prtmEnd == NULL)
		return MAPI_E_INVALID_PARAMETER;
	*prtmStart = m_rtmStart;
	*prtmEnd = m_rtmEnd;
	return S_OK;
}

} /* namespace */
