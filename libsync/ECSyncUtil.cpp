/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#include <kopano/platform.h>
#include <iterator>
#include <memory>
#include "ECSyncUtil.h"
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include <mapix.h>

namespace KC {

HRESULT HrDecodeSyncStateStream(LPSTREAM lpStream, ULONG *lpulSyncId, ULONG *lpulChangeId, PROCESSEDCHANGESSET *lpSetProcessChanged)
{
	STATSTG		stat;
	ULONG		ulSyncId = 0;
	ULONG		ulChangeId = 0;
	ULONG		ulChangeCount = 0;
	ULONG		ulProcessedChangeId = 0;
	ULONG		ulSourceKeySize = 0;
	LARGE_INTEGER		liPos = {{0, 0}};
	PROCESSEDCHANGESSET setProcessedChanged;

	auto hr = lpStream->Stat(&stat, STATFLAG_NONAME);
	if(hr != hrSuccess)
		return hr;
	
	if (stat.cbSize.HighPart == 0 && stat.cbSize.LowPart == 0) {
		ulSyncId = 0;
		ulChangeId = 0;
	} else {
		if (stat.cbSize.HighPart != 0 || stat.cbSize.LowPart < 8)
			return MAPI_E_INVALID_PARAMETER;
		hr = lpStream->Seek(liPos, STREAM_SEEK_SET, NULL);
		if (hr != hrSuccess)
			return hr;
		hr = lpStream->Read(&ulSyncId, 4, NULL);
		if (hr != hrSuccess)
			return hr;
		hr = lpStream->Read(&ulChangeId, 4, NULL);
		if (hr != hrSuccess)
			return hr;
			
		// Following the sync ID and the change ID is the list of changes that were already processed for
		// this sync ID / change ID combination. This allows us partial processing of items retrieved from 
		// the server.
		if (lpSetProcessChanged != NULL && lpStream->Read(&ulChangeCount, 4, NULL) == hrSuccess) {
			// The stream contains a list of already processed items, read them
			
			for (ULONG i = 0; i < ulChangeCount; ++i) {
				std::unique_ptr<char[]> lpData;

				hr = lpStream->Read(&ulProcessedChangeId, 4, NULL);
				if (hr != hrSuccess)
					/* Not the amount of expected bytes are there */
					return hr;
				hr = lpStream->Read(&ulSourceKeySize, 4, NULL);
				if (hr != hrSuccess)
					return hr;
				if (ulSourceKeySize > 1024)
					// Stupidly large source key, the stream must be bad.
					return MAPI_E_INVALID_PARAMETER;
				lpData.reset(new char[ulSourceKeySize]);
				hr = lpStream->Read(lpData.get(), ulSourceKeySize, NULL);
				if(hr != hrSuccess)
					return hr;
				setProcessedChanged.emplace(ulProcessedChangeId, std::string(lpData.get(), ulSourceKeySize));
			}
		}
	}

	if (lpulSyncId)
		*lpulSyncId = ulSyncId;

	if (lpulChangeId)
		*lpulChangeId = ulChangeId;

	if (lpSetProcessChanged)
		lpSetProcessChanged->insert(gcc5_make_move_iterator(setProcessedChanged.begin()), gcc5_make_move_iterator(setProcessedChanged.end()));
	return hrSuccess;
}

HRESULT ResetStream(LPSTREAM lpStream)
{
	LARGE_INTEGER liPos = {{0, 0}};
	ULARGE_INTEGER uliSize = {{8, 0}};
	HRESULT hr = lpStream->Seek(liPos, STREAM_SEEK_SET, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = lpStream->SetSize(uliSize);
	if (hr != hrSuccess)
		return hr;
	hr = lpStream->Write("\0\0\0\0\0\0\0\0", 8, NULL);
	if (hr != hrSuccess)
		return hr;
	return lpStream->Seek(liPos, STREAM_SEEK_SET, NULL);
}

HRESULT CreateNullStatusStream(LPSTREAM *lppStream)
{
	StreamPtr ptrStream;

	HRESULT hr = CreateStreamOnHGlobal(GlobalAlloc(GPTR, 8), true, &~ptrStream);
	if (hr != hrSuccess)
		return hr;
	hr = ResetStream(ptrStream);
	if (hr != hrSuccess)
		return hr;
	return ptrStream->QueryInterface(IID_IStream,
	       reinterpret_cast<LPVOID *>(lppStream));
}

} /* namespace */
