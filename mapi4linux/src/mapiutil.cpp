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
#include <memory>
#include <new>
#include <climits>
#include <cstdlib>
#include <cmath> // for pow() 
#include "m4l.mapidefs.h"
#include "m4l.mapix.h"
#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapidefs.h>
#include <kopano/ECTags.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include <kopano/tie.hpp>
#include <kopano/Util.h>

#include "ECMemStream.h"
#include <kopano/mapiguidext.h>

#include "rtf.h"

#include <kopano/charset/convstring.h>

using namespace KC;

SPropValue *PpropFindProp(SPropValue *lpPropArray, ULONG cValues,
    ULONG ulPropTag)
{
	return const_cast<SPropValue *>(PCpropFindProp(lpPropArray, cValues, ulPropTag));
}

const SPropValue *PCpropFindProp(const SPropValue *lpPropArray,
    ULONG cValues, ULONG ulPropTag)
{
	if (lpPropArray == NULL)
		return nullptr;
	for (ULONG i = 0; i < cValues; ++i)
		if ((lpPropArray[i].ulPropTag == ulPropTag) ||
		    (PROP_TYPE(ulPropTag) == PT_UNSPECIFIED &&
		    PROP_ID(lpPropArray[i].ulPropTag) == PROP_ID(ulPropTag)))
			return &lpPropArray[i];
	return nullptr;
}

HRESULT WrapStoreEntryID(ULONG ulFlags, const TCHAR *lpszDLLName,
    ULONG cbOrigEntry, const ENTRYID *lpOrigEntry, ULONG *lpcbWrappedEntry,
    ENTRYID **lppWrappedEntry)
{
	HRESULT hr = hrSuccess;
	ULONG cbDLLName = 0;
	ULONG cbPad = 0;
	std::string strDLLName = convstring(lpszDLLName, ulFlags);

	if (lpszDLLName == nullptr || lpOrigEntry == nullptr ||
	    lpcbWrappedEntry == nullptr || lppWrappedEntry == nullptr ||
	    cbOrigEntry <= 4 + sizeof(GUID))
		return MAPI_E_INVALID_PARAMETER;

	// The format of a wrapped entryid is:
	// - flags (4)
	// - static guid (16) (see m4l.common.h)
	// - 2 bytes unknown data
	// - then the dll name + termination char + padding to 32 bits
	// - then the entry id

	cbDLLName = strDLLName.size() + 1;
	cbPad = (4 - ((4+sizeof(GUID)+2+cbDLLName) & 0x03)) & 0x03;

	*lpcbWrappedEntry = 4+sizeof(GUID)+2+cbDLLName+cbPad+cbOrigEntry;
	hr = MAPIAllocateBuffer(*lpcbWrappedEntry, (void**)lppWrappedEntry);
	if (hr != hrSuccess)
		return hr;
	memset(*lppWrappedEntry, 0, *lpcbWrappedEntry);
	memcpy((*lppWrappedEntry)->ab, &muidStoreWrap, sizeof(GUID));

	strcpy(((char*)*lppWrappedEntry)+4+sizeof(GUID)+2, strDLLName.c_str());
	memcpy(((BYTE*)*lppWrappedEntry)+4+sizeof(GUID)+2+cbDLLName+cbPad, lpOrigEntry, cbOrigEntry);
	return hrSuccess;
}

void FreeProws(LPSRowSet lpRows)
{
	if(lpRows == NULL)
		return;
	for (unsigned int i = 0; i < lpRows->cRows; ++i)
		MAPIFreeBuffer(lpRows->aRow[i].lpProps);
	MAPIFreeBuffer(lpRows);
}

void FreePadrlist(LPADRLIST lpAdrlist)
{
	// it's the same in mapi4linux
	FreeProws((LPSRowSet) lpAdrlist);
}

// M4LMAPIAdviseSink is in mapidefs.cpp
HRESULT HrAllocAdviseSink(LPNOTIFCALLBACK lpFunction, void *lpContext,
    LPMAPIADVISESINK *lppSink)
{
	return alloc_wrap<M4LMAPIAdviseSink>(lpFunction, lpContext)
	       .as(IID_IMAPIAdviseSink, lppSink);
}

// rtf funcions

// This is called when a user calls Commit() on a wrapped (uncompressed) RTF Stream
static HRESULT RTFCommitFunc(IStream *lpUncompressedStream, void *lpData)
{
	auto lpCompressedStream = static_cast<IStream *>(lpData);
	STATSTG sStatStg;
	ULONG ulRead = 0;
	ULONG ulWritten = 0;
	unsigned int ulCompressedSize;
	std::unique_ptr<char, cstdlib_deleter> lpCompressed;
	ULARGE_INTEGER zero = {{0,0}};
	LARGE_INTEGER front = {{0,0}};

	auto hr = lpUncompressedStream->Stat(&sStatStg, STATFLAG_NONAME);
	if(hr != hrSuccess)
		return hr;
	std::unique_ptr<char[]> lpUncompressed(new(std::nothrow) char[sStatStg.cbSize.LowPart]);
	if (lpUncompressed == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	auto lpReadPtr = lpUncompressed.get();
	while(1) {
		hr = lpUncompressedStream->Read(lpReadPtr, 1024, &ulRead);

		if(hr != hrSuccess || ulRead == 0)
			break;

		lpReadPtr += ulRead;
	}

	// We now have the complete uncompressed data in lpUncompressed
	if (rtf_compress(&unique_tie(lpCompressed), &ulCompressedSize, lpUncompressed.get(), sStatStg.cbSize.LowPart) != 0)
		return MAPI_E_CALL_FAILED;
	// lpCompressed is the compressed RTF stream, write it to lpCompressedStream
	lpReadPtr = lpCompressed.get();
	lpCompressedStream->SetSize(zero);
	lpCompressedStream->Seek(front,SEEK_SET,NULL);

	while(ulCompressedSize) {
		hr = lpCompressedStream->Write(lpReadPtr, ulCompressedSize > 16384 ? 16384 : ulCompressedSize, &ulWritten);

		if(hr != hrSuccess)
			return hr;
		lpReadPtr += ulWritten;
		ulCompressedSize -= ulWritten;
	}
	return hrSuccess;
}

HRESULT WrapCompressedRTFStream(LPSTREAM lpCompressedRTFStream, ULONG ulFlags,
    LPSTREAM *lppUncompressedStream)
{
	// This functions doesn't really wrap the stream, but decodes the
	// compressed stream, and writes the uncompressed data to the
	// Uncompressed stream. This is usually not a problem, as the whole
	// stream is read out in one go anyway.
	//
	// Also, not much streaming is done on the input data, the function
	// therefore is quite memory-hungry.

	STATSTG sStatStg;
	std::unique_ptr<char[]> lpCompressed, lpUncompressed;
	char *lpReadPtr = NULL;
	ULONG ulRead = 0;
	object_ptr<ECMemStream> lpUncompressedStream;
	ULONG ulUncompressedLen = 0;
	
	auto hr = lpCompressedRTFStream->Stat(&sStatStg, STATFLAG_NONAME);
	if(hr != hrSuccess)
		return hr;

	if(sStatStg.cbSize.LowPart > 0) {
		lpCompressed.reset(new(std::nothrow) char[sStatStg.cbSize.LowPart]);
		if (lpCompressed == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;

        	// Read in the whole compressed data buffer
        	lpReadPtr = lpCompressed.get();
        	while(1) {
        		hr = lpCompressedRTFStream->Read(lpReadPtr, 1024, &ulRead);

        		if(hr != hrSuccess)
				return hr;
        		if(ulRead == 0)
        			break;	
        	
        		lpReadPtr += ulRead;		
        	}
        	ulUncompressedLen = rtf_get_uncompressed_length(lpCompressed.get(), sStatStg.cbSize.LowPart);
        	lpUncompressed.reset(new(std::nothrow) char[ulUncompressedLen]);
		if (lpUncompressed == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		memset(lpUncompressed.get(), 0, ulUncompressedLen);
		ulUncompressedLen = rtf_decompress(lpUncompressed.get(), lpCompressed.get(), sStatStg.cbSize.LowPart);
		if (ulUncompressedLen == UINT_MAX)
			return MAPI_E_INVALID_PARAMETER;
        	// We now have the uncompressed data, create a stream and write the uncompressed data into it
	}
	
	hr = ECMemStream::Create(lpUncompressed.get(), ulUncompressedLen,
	     STGM_WRITE | STGM_TRANSACTED, RTFCommitFunc,
	     nullptr /* no cleanup */,
	     lpCompressedRTFStream, &~lpUncompressedStream);
	if(hr != hrSuccess)
		return hr;
	return lpUncompressedStream->QueryInterface(IID_IStream,
	       reinterpret_cast<void **>(lppUncompressedStream));
}

// RTFSync is not much use even in windows, so we don't implement it
HRESULT RTFSync(LPMESSAGE lpMessage, ULONG ulFlags, BOOL *lpfMessageUpdated)
{
	return MAPI_E_NO_SUPPORT;
}

//--- php-ext used functions
HRESULT HrQueryAllRows(LPMAPITABLE lpTable, const SPropTagArray *lpPropTags,
    LPSRestriction lpRestriction, const SSortOrderSet *lpSortOrderSet,
    LONG crowsMax, LPSRowSet *lppRows)
{
	auto hr = lpTable->SeekRow(BOOKMARK_BEGINNING, 0, NULL);
	if (hr != hrSuccess)
		return hr;
	if (lpPropTags) {
		hr = lpTable->SetColumns(lpPropTags, TBL_BATCH);
		if (hr != hrSuccess)
			return hr;
	}

	if (lpRestriction) {
		hr = lpTable->Restrict(lpRestriction, TBL_BATCH);
		if (hr != hrSuccess)
			return hr;
	}

	if (lpSortOrderSet) {
		hr = lpTable->SortTable(lpSortOrderSet, TBL_BATCH);
		if (hr != hrSuccess)
			return hr;
	}

	if (crowsMax == 0)
		crowsMax = 0x7FFFFFFF;
	return lpTable->QueryRows(crowsMax, 0, lppRows);
}

HRESULT HrGetOneProp(IMAPIProp *lpProp, ULONG ulPropTag,
    LPSPropValue *lppPropVal)
{
	SizedSPropTagArray(1, sPropTag) = { 1, { ulPropTag } };
	ULONG cValues = 0;
	memory_ptr<SPropValue> lpPropVal;

	auto hr = lpProp->GetProps(sPropTag, 0, &cValues, &~lpPropVal);
	if(HR_FAILED(hr))
		return hr;
	if (cValues != 1 || lpPropVal->ulPropTag != ulPropTag)
		/*
		 * This proptag check should filter out the case of
		 * hr==MAPI_W_ERRORS_RETURNED (lpPropVal->ulPropTag==PT_ERROR).
		 */
		return MAPI_E_NOT_FOUND;
	*lppPropVal = lpPropVal.release();
	return hrSuccess;
}

HRESULT HrSetOneProp(LPMAPIPROP lpMapiProp, const SPropValue *lpProp)
{
	return lpMapiProp->SetProps(1, lpProp, nullptr);
	// convert ProblemArray into HRESULT error?
}

BOOL FPropExists(LPMAPIPROP lpMapiProp, ULONG ulPropTag)
{
	memory_ptr<SPropValue> lpPropVal;
	return HrGetOneProp(lpMapiProp, ulPropTag, &~lpPropVal) == hrSuccess;
}

/* Actually not part of MAPI */
HRESULT CreateStreamOnHGlobal(void *hGlobal, BOOL fDeleteOnRelease,
    IStream **lppStream)
{
	object_ptr<ECMemStream> lpStream;
	
	if (hGlobal != nullptr || fDeleteOnRelease != TRUE)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = ECMemStream::Create(nullptr, 0, STGM_WRITE, nullptr, nullptr, nullptr, &~lpStream); // NULLs: no callbacks and custom data
	if(hr != hrSuccess) 
		return hr;
	return lpStream->QueryInterface(IID_IStream, reinterpret_cast<void **>(lppStream));
}

#pragma pack(push, 1)
struct CONVERSATION_INDEX { /* 22 bytes */
	char ulReserved1;
	char ftTime[5];
	GUID guid;
};
#pragma pack(pop)

HRESULT ScCreateConversationIndex(ULONG cbParent, LPBYTE lpbParent,
    ULONG *lpcbConvIndex, LPBYTE *lppbConvIndex)
{
	ULONG cbConvIndex = 0;
	BYTE *pbConvIndex = NULL;

	if(cbParent == 0) {
		auto hr = MAPIAllocateBuffer(sizeof(CONVERSATION_INDEX), reinterpret_cast<void **>(&pbConvIndex));
		if (hr != hrSuccess)
			return hr;
		cbConvIndex = sizeof(CONVERSATION_INDEX);
		auto ci = reinterpret_cast<CONVERSATION_INDEX *>(pbConvIndex);
		ci->ulReserved1 = 1;
		auto ft = UnixTimeToFileTime(time(nullptr));
		uint32_t tmp = cpu_to_le32(ft.dwLowDateTime);
		memcpy(ci->ftTime, &tmp, sizeof(tmp));
		ci->ftTime[4] = ft.dwHighDateTime;
		CoCreateGuid(&ci->guid);
	} else {
		FILETIME parent;
		auto hr = MAPIAllocateBuffer(cbParent + 5, reinterpret_cast<void **>(&pbConvIndex));
		if (hr != hrSuccess)
			return hr;
		cbConvIndex = cbParent+5;
		memcpy(pbConvIndex, lpbParent, cbParent);

		auto ci = reinterpret_cast<const CONVERSATION_INDEX *>(lpbParent);
		memcpy(&parent.dwLowDateTime, &ci->ftTime, sizeof(DWORD));
		parent.dwLowDateTime = le32_to_cpu(parent.dwLowDateTime);
		parent.dwHighDateTime = lpbParent[4];
		auto now = UnixTimeToFileTime(time(nullptr));
		auto diff = FtSubFt(now, parent);
		diff.dwLowDateTime = cpu_to_le32(diff.dwLowDateTime);
		diff.dwHighDateTime = cpu_to_le32(diff.dwHighDateTime);
		memcpy(pbConvIndex + sizeof(CONVERSATION_INDEX), &diff.dwLowDateTime, 4);
		pbConvIndex[sizeof(CONVERSATION_INDEX)+4] = diff.dwHighDateTime;
	}

	*lppbConvIndex = pbConvIndex;
	*lpcbConvIndex = cbConvIndex;
	return hrSuccess;
}

FILETIME FtSubFt(const FILETIME &Minuend, const FILETIME &Subtrahend)
{
	FILETIME ft;
	unsigned long long l = ((unsigned long long)Minuend.dwHighDateTime << 32) + Minuend.dwLowDateTime;
	l -= ((unsigned long long)Subtrahend.dwHighDateTime << 32) + Subtrahend.dwLowDateTime;

	ft.dwHighDateTime = l >> 32;
	ft.dwLowDateTime = l & 0xffffffff;
	return ft;
}
