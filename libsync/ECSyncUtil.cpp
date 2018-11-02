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
