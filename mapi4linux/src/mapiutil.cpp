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
#include <cstdlib>
#include <cmath> // for pow() 

#include "m4l.mapiutil.h"
#include "m4l.mapidefs.h"
#include "m4l.mapix.h"
#include "m4l.debug.h"
#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapidefs.h>

#include <kopano/ECDebug.h>
#include <kopano/ECTags.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include <kopano/Util.h>

#include "ECMemStream.h"
#include <kopano/mapiguidext.h>

#include "rtf.h"

#include <kopano/charset/convstring.h>

using namespace KCHL;

ULONG __stdcall UlRelease(LPVOID lpUnknown)
{
	TRACE_MAPILIB(TRACE_ENTRY, "UlRelease", "");
	if(lpUnknown)
		return ((IUnknown *)lpUnknown)->Release();
	else
		return 0;
}

void __stdcall DeinitMapiUtil(void)
{
	TRACE_MAPILIB(TRACE_ENTRY, "DeInitMAPIUtil", "");
	TRACE_MAPILIB(TRACE_RETURN, "DeInitMAPIUtil", "");
}

SPropValue * __stdcall PpropFindProp(SPropValue *lpPropArray, ULONG cValues,
    ULONG ulPropTag)
{
	return const_cast<SPropValue *>(PCpropFindProp(lpPropArray, cValues, ulPropTag));
}

const SPropValue * __stdcall PCpropFindProp(const SPropValue *lpPropArray,
    ULONG cValues, ULONG ulPropTag)
{
	TRACE_MAPILIB1(TRACE_ENTRY, "PpropFindProp", "%08x", ulPropTag);
	const SPropValue *lpValue = NULL;

	if (lpPropArray == NULL)
		goto exit;

	for (ULONG i = 0; i<cValues; ++i) {
		if ((lpPropArray[i].ulPropTag == ulPropTag) ||
			(PROP_TYPE(ulPropTag) == PT_UNSPECIFIED && PROP_ID(lpPropArray[i].ulPropTag) == PROP_ID(ulPropTag))) {
			lpValue = &lpPropArray[i];
			break;
		}
	}

exit:
	TRACE_MAPILIB2(TRACE_RETURN, "PpropFindProp", "%s: %08x", (lpValue ? "SUCCESS" : "FAILED"), ulPropTag);
	return lpValue;
}

// Find a property with a given property Id in a property array. NOTE: doesn't care about prop type!
LPSPropValue __stdcall LpValFindProp(ULONG ulPropTag, ULONG cValues, LPSPropValue lpProps)
{
	TRACE_MAPILIB1(TRACE_ENTRY, "LpValFindProp", "%08x", ulPropTag);
	LPSPropValue lpValue = NULL;

	if (lpProps == NULL)
		goto exit;

	for (ULONG i = 0; i < cValues; ++i) {
		if(PROP_ID(lpProps[i].ulPropTag) == PROP_ID(ulPropTag)) {
			lpValue = &lpProps[i];
			break;
		}
	}

exit:
	TRACE_MAPILIB2(TRACE_RETURN, "LpValFindProp", "%s: %08x", (lpValue ? "SUCCESS" : "FAILED"), ulPropTag);
	return  lpValue;
}

SCODE __stdcall PropCopyMore( LPSPropValue lpSPropValueDest,  LPSPropValue lpSPropValueSrc,  ALLOCATEMORE * lpfAllocMore,  LPVOID lpvObject)
{
	HRESULT hr = hrSuccess;
	TRACE_MAPILIB1(TRACE_ENTRY, "PropCopyMore", "%s", PropNameFromPropArray(1, lpSPropValueSrc).c_str());
	hr = Util::HrCopyProperty(lpSPropValueDest, lpSPropValueSrc, lpvObject, lpfAllocMore);
	TRACE_MAPILIB2(TRACE_RETURN, "PropCopyMore", "%s: %s", GetMAPIErrorDescription(hr).c_str(), PropNameFromPropArray(1, lpSPropValueDest).c_str());
	return hr;
}

HRESULT __stdcall WrapStoreEntryID(ULONG ulFlags, LPTSTR lpszDLLName, ULONG cbOrigEntry,
						 LPENTRYID lpOrigEntry, ULONG *lpcbWrappedEntry, LPENTRYID *lppWrappedEntry) {
	TRACE_MAPILIB(TRACE_ENTRY, "WrapStoreEntryID", "");

	HRESULT hr = hrSuccess;
	ULONG cbDLLName = 0;
	ULONG cbPad = 0;
	std::string strDLLName = convstring(lpszDLLName, ulFlags);

	if (lpszDLLName == NULL || lpOrigEntry == NULL || lpcbWrappedEntry == NULL || lppWrappedEntry == NULL || cbOrigEntry <= (4+sizeof(GUID)) ) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

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
		goto exit;
		
	memset(*lppWrappedEntry, 0, *lpcbWrappedEntry);
	memcpy((*lppWrappedEntry)->ab, &muidStoreWrap, sizeof(GUID));

	strcpy(((char*)*lppWrappedEntry)+4+sizeof(GUID)+2, strDLLName.c_str());
	memcpy(((BYTE*)*lppWrappedEntry)+4+sizeof(GUID)+2+cbDLLName+cbPad, lpOrigEntry, cbOrigEntry);
	
exit:
	TRACE_MAPILIB1(TRACE_ENTRY, "WrapStoreEntryID", "0x%08x", hr);
	return hr;
}

void __stdcall FreeProws(LPSRowSet lpRows) {
	TRACE_MAPILIB(TRACE_ENTRY, "FreeProws", "");
	unsigned int i;
	
	if(lpRows == NULL)
		return;

	for (i = 0; i < lpRows->cRows; ++i)
		MAPIFreeBuffer(lpRows->aRow[i].lpProps);
	MAPIFreeBuffer(lpRows);
	TRACE_MAPILIB(TRACE_RETURN, "FreeProws", "");
}

void __stdcall FreePadrlist(LPADRLIST lpAdrlist) {
	TRACE_MAPILIB(TRACE_ENTRY, "FreePadrlist", "");
	// it's the same in mapi4linux
	FreeProws((LPSRowSet) lpAdrlist);
	TRACE_MAPILIB(TRACE_RETURN, "FreePadrlist", "");
}

// M4LMAPIAdviseSink is in mapidefs.cpp
HRESULT __stdcall HrAllocAdviseSink(LPNOTIFCALLBACK lpFunction, void *lpContext, LPMAPIADVISESINK *lppSink)
{
	TRACE_MAPILIB(TRACE_ENTRY, "HrAllocAdviseSink", "");
	HRESULT hr = hrSuccess;
	IMAPIAdviseSink *lpSink = NULL;

	lpSink = new(std::nothrow) M4LMAPIAdviseSink(lpFunction, lpContext);
	if (!lpSink) {
		hr = MAPI_E_NOT_ENOUGH_MEMORY;
		goto exit;
	}

	lpSink->AddRef();

	*lppSink = lpSink;

exit:
	TRACE_MAPILIB1(TRACE_RETURN, "HrAllocAdviseSink", "0x%08x", hr);
	return hr;
}

// Linux always has multithreaded advise sinks
HRESULT __stdcall HrThisThreadAdviseSink(LPMAPIADVISESINK lpAdviseSink, LPMAPIADVISESINK *lppAdviseSink) {
	TRACE_MAPILIB(TRACE_ENTRY, "HrThisThreadAdviseSink", "");
	*lppAdviseSink = lpAdviseSink;
	lpAdviseSink->AddRef();
	TRACE_MAPILIB1(TRACE_RETURN, "HrThisThreadAdviseSink", "0x%08x", hrSuccess);
	return hrSuccess;
}

// rtf funcions

// This is called when a user calls Commit() on a wrapped (uncompressed) RTF Stream
static HRESULT RTFCommitFunc(IStream *lpUncompressedStream, void *lpData)
{
	HRESULT hr = hrSuccess;
	IStream *lpCompressedStream = (IStream *)lpData;
	STATSTG sStatStg;
	std::unique_ptr<char[]> lpUncompressed;
	char *lpReadPtr = NULL;
	ULONG ulRead = 0;
	ULONG ulWritten = 0;
	unsigned int ulCompressedSize;
	char *lpCompressed = NULL;
	ULARGE_INTEGER zero = {{0,0}};
	LARGE_INTEGER front = {{0,0}};

	hr = lpUncompressedStream->Stat(&sStatStg, STATFLAG_NONAME);

	if(hr != hrSuccess)
		goto exit;
	lpUncompressed.reset(new(std::nothrow) char[sStatStg.cbSize.LowPart]);
	if(lpUncompressed == NULL) {
		hr = MAPI_E_NOT_ENOUGH_MEMORY;
		goto exit;
	}

	lpReadPtr = lpUncompressed.get();
	while(1) {
		hr = lpUncompressedStream->Read(lpReadPtr, 1024, &ulRead);

		if(hr != hrSuccess || ulRead == 0)
			break;

		lpReadPtr += ulRead;
	}

	// We now have the complete uncompressed data in lpUncompressed
	if (rtf_compress(&lpCompressed, &ulCompressedSize, lpUncompressed.get(), sStatStg.cbSize.LowPart) != 0) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	// lpCompressed is the compressed RTF stream, write it to lpCompressedStream

	lpReadPtr = lpCompressed;

	lpCompressedStream->SetSize(zero);
	lpCompressedStream->Seek(front,SEEK_SET,NULL);

	while(ulCompressedSize) {
		hr = lpCompressedStream->Write(lpReadPtr, ulCompressedSize > 16384 ? 16384 : ulCompressedSize, &ulWritten);

		if(hr != hrSuccess)
			break;

		lpReadPtr += ulWritten;
		ulCompressedSize -= ulWritten;
	}

exit:
	free(lpCompressed);
	return hr;
}

HRESULT __stdcall WrapCompressedRTFStream(LPSTREAM lpCompressedRTFStream, ULONG ulFlags, LPSTREAM * lppUncompressedStream) {
	// This functions doesn't really wrap the stream, but decodes the
	// compressed stream, and writes the uncompressed data to the
	// Uncompressed stream. This is usually not a problem, as the whole
	// stream is read out in one go anyway.
	//
	// Also, not much streaming is done on the input data, the function
	// therefore is quite memory-hungry.

	STATSTG sStatStg;
	HRESULT hr = hrSuccess;
	std::unique_ptr<char[]> lpCompressed, lpUncompressed;
	char *lpReadPtr = NULL;
	ULONG ulRead = 0;
	object_ptr<ECMemStream> lpUncompressedStream;
	ULONG ulUncompressedLen = 0;
	
	hr = lpCompressedRTFStream->Stat(&sStatStg, STATFLAG_NONAME);

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
        	if (rtf_decompress(lpUncompressed.get(), lpCompressed.get(), sStatStg.cbSize.LowPart) != 0)
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
HRESULT __stdcall RTFSync(LPMESSAGE lpMessage, ULONG ulFlags, BOOL * lpfMessageUpdated) {
	TRACE_MAPILIB(TRACE_ENTRY, "RTFSync", "");
	HRESULT hr = MAPI_E_NO_SUPPORT;
	TRACE_MAPILIB1(TRACE_RETURN, "RTFSync", "0x%08x", hr);
	return hr;
}

//--- php-ext used functions
HRESULT __stdcall HrQueryAllRows(LPMAPITABLE lpTable,
    const SPropTagArray *lpPropTags, LPSRestriction lpRestriction,
    const SSortOrderSet *lpSortOrderSet, LONG crowsMax, LPSRowSet *lppRows)
{
	TRACE_MAPILIB1(TRACE_ENTRY, "HrQueryAllRows", "%s", PropNameFromPropTagArray(lpPropTags).c_str());
	HRESULT hr = hrSuccess;

	hr = lpTable->SeekRow(BOOKMARK_BEGINNING, 0, NULL);
	if (hr != hrSuccess)
		goto exit;

	if (lpPropTags) {
		hr = lpTable->SetColumns(lpPropTags, TBL_BATCH);
		if (hr != hrSuccess)
			goto exit;
	}

	if (lpRestriction) {
		hr = lpTable->Restrict(lpRestriction, TBL_BATCH);
		if (hr != hrSuccess)
			goto exit;
	}

	if (lpSortOrderSet) {
		hr = lpTable->SortTable(lpSortOrderSet, TBL_BATCH);
		if (hr != hrSuccess)
			goto exit;
	}

	if (crowsMax == 0)
		crowsMax = 0x7FFFFFFF;

	hr = lpTable->QueryRows(crowsMax, 0, lppRows);

exit:
	TRACE_MAPILIB1(TRACE_RETURN, "HrQueryAllRows", "0x%08x", hr);
	return hr;
}

HRESULT __stdcall HrGetOneProp(IMAPIProp *lpProp, ULONG ulPropTag, LPSPropValue *lppPropVal) {
	TRACE_MAPILIB1(TRACE_ENTRY, "HrGetOneProp", "%08x", ulPropTag);
	HRESULT hr = hrSuccess;
	SizedSPropTagArray(1, sPropTag) = { 1, { ulPropTag } };
	ULONG cValues = 0;
	memory_ptr<SPropValue> lpPropVal;

	hr = lpProp->GetProps(sPropTag, 0, &cValues, &~lpPropVal);
	if(HR_FAILED(hr))
		goto exit;
		
	if(cValues != 1 || lpPropVal->ulPropTag != ulPropTag) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
	*lppPropVal = lpPropVal.release();
exit:
	TRACE_MAPILIB1(TRACE_RETURN, "HrGetOneProp", "0x%08x", hr);
	return hr;
}

HRESULT __stdcall HrSetOneProp(LPMAPIPROP lpMapiProp, const SPropValue *lpProp)
{
	TRACE_MAPILIB1(TRACE_ENTRY, "HrSetOneProp", "%s", PropNameFromPropArray(1, lpProp).c_str());
	HRESULT hr = hrSuccess;

	hr = lpMapiProp->SetProps(1, lpProp, NULL);
	// convert ProblemArray into HRESULT error?

	TRACE_MAPILIB1(TRACE_RETURN, "HrSetOneProp", "0x%08x", hr);
	return hr;
}

BOOL __stdcall FPropExists(LPMAPIPROP lpMapiProp, ULONG ulPropTag)
{
	TRACE_MAPILIB1(TRACE_ENTRY, "FPropExists", "%08x", ulPropTag);
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpPropVal = NULL;

	hr = HrGetOneProp(lpMapiProp, ulPropTag, &~lpPropVal);
	TRACE_MAPILIB1(TRACE_RETURN, "FPropExists", "0x%08x", hr);
	return (hr == hrSuccess);
}

/* Actually not part of MAPI */
HRESULT __stdcall CreateStreamOnHGlobal(void *hGlobal, BOOL fDeleteOnRelease, IStream **lppStream)
{
	HRESULT hr = hrSuccess;
	object_ptr<ECMemStream> lpStream;
	
	if (hGlobal != nullptr || fDeleteOnRelease != TRUE)
		return MAPI_E_INVALID_PARAMETER;
	hr = ECMemStream::Create(nullptr, 0, STGM_WRITE, nullptr, nullptr, nullptr, &~lpStream); // NULLs: no callbacks and custom data
	if(hr != hrSuccess) 
		return hr;
	return lpStream->QueryInterface(IID_IStream, reinterpret_cast<void **>(lppStream));
}

HRESULT __stdcall OpenStreamOnFile(LPALLOCATEBUFFER lpAllocateBuffer, LPFREEBUFFER lpFreeBuffer, ULONG ulFlags,
    LPTSTR lpszFileName, LPTSTR lpszPrefix, LPSTREAM *lppStream)
{
	TRACE_MAPILIB(TRACE_ENTRY, "OpenStreamOnFile", "");
	HRESULT hr = MAPI_E_NOT_FOUND;
	TRACE_MAPILIB1(TRACE_RETURN, "OpenStreamOnFile", "0x%08x", hr);
	return hr;
}

HRESULT __stdcall BuildDisplayTable(LPALLOCATEBUFFER lpAllocateBuffer, LPALLOCATEMORE lpAllocateMore,
	LPFREEBUFFER lpFreeBuffer, LPMALLOC lpMalloc,
	HINSTANCE hInstance, UINT cPages,
	LPDTPAGE lpPage, ULONG ulFlags,
	LPMAPITABLE * lppTable, LPTABLEDATA * lppTblData)
{
	TRACE_MAPILIB(TRACE_ENTRY, "BuildDisplayTable", "");
	HRESULT hr = MAPI_E_NO_SUPPORT;
	TRACE_MAPILIB1(TRACE_RETURN, "BuildDisplayTable", "0x%08x", hr);
	return hr;
}

#pragma pack(push, 1)
struct CONVERSATION_INDEX {
	char ulReserved1;
	char ftTime[5];
	GUID guid;
};
#pragma pack(pop)

HRESULT __stdcall ScCreateConversationIndex (ULONG cbParent,
	LPBYTE lpbParent,
	ULONG *lpcbConvIndex,
	LPBYTE *lppbConvIndex)
{
	HRESULT hr;
	TRACE_MAPILIB1(TRACE_ENTRY, "ScCreateConversationIndex", "%s", lpbParent ? bin2hex(cbParent, lpbParent).c_str() : "<null>");
	ULONG cbConvIndex = 0;
	BYTE *pbConvIndex = NULL;

	if(cbParent == 0) {
		FILETIME ft;
		if ((hr = MAPIAllocateBuffer(sizeof(CONVERSATION_INDEX), (void **)&pbConvIndex)) != hrSuccess)
			return hr;
		cbConvIndex = sizeof(CONVERSATION_INDEX);

		CONVERSATION_INDEX *ci = (CONVERSATION_INDEX*)pbConvIndex;
		ci->ulReserved1 = 1;
		UnixTimeToFileTime(time(NULL), &ft);
		memcpy(ci->ftTime, &ft, 5);
		CoCreateGuid(&ci->guid);
	} else {
		FILETIME now;
		FILETIME parent;
		FILETIME diff;

		if ((hr = MAPIAllocateBuffer(cbParent + 5, (void **)&pbConvIndex)) != hrSuccess)
			return hr;
		cbConvIndex = cbParent+5;
		memcpy(pbConvIndex, lpbParent, cbParent);

		memset(&parent, 0, sizeof(FILETIME));
		memcpy(&parent, ((CONVERSATION_INDEX*)lpbParent)->ftTime, 5);

		UnixTimeToFileTime(time(NULL), &now);

		diff = FtSubFt(now, parent);

		memcpy(pbConvIndex + sizeof(CONVERSATION_INDEX), &diff, 5);
	}

	*lppbConvIndex = pbConvIndex;
	*lpcbConvIndex = cbConvIndex;

	TRACE_MAPILIB1(TRACE_RETURN, "ScCreateConversationIndex", "%s", bin2hex(cbConvIndex, pbConvIndex).c_str());
	return hrSuccess;
}

SCODE __stdcall ScDupPropset( int cprop,  LPSPropValue rgprop,  LPALLOCATEBUFFER lpAllocateBuffer,  LPSPropValue *prgprop )
{
	TRACE_MAPILIB(TRACE_ENTRY, "ScDupPropset", "");

	HRESULT hr = hrSuccess;
	LPSPropValue lpDst = NULL;
	ULONG ulSize = 0;

	hr = ScCountProps(cprop, rgprop, &ulSize);
	if(hr != hrSuccess)
		goto exit;

	hr = lpAllocateBuffer(ulSize, (void **)&lpDst);
	if(hr != hrSuccess)
		goto exit;

	hr = ScCopyProps(cprop, rgprop, lpDst, NULL);
	if(hr != hrSuccess)
		goto exit;

	*prgprop = lpDst;

exit:
	TRACE_MAPILIB1(TRACE_RETURN, "ScDupPropset", "0x%08x", hr);
	return hr;
}

SCODE __stdcall ScRelocProps(int cprop, LPSPropValue rgprop, LPVOID pvBaseOld, LPVOID pvBaseNew, ULONG *pcb)
{
	TRACE_MAPILIB(TRACE_ENTRY, "ScRelocProps", "");
	TRACE_MAPILIB1(TRACE_RETURN, "ScRelocProps", "0x%08x", S_FALSE);
	return S_FALSE;
}
ULONG __stdcall CbOfEncoded(LPCSTR lpszEnc)
{
	TRACE_MAPILIB(TRACE_ENTRY, "CbOfEncoded", "");
	ULONG ulRet = 0;

	if (lpszEnc)
		ulRet = (((strlen(lpszEnc) | 3) >> 2) + 1) * 3;

	TRACE_MAPILIB(TRACE_RETURN, "CbOfEncoded", "");
	return ulRet;
}

ULONG __stdcall CchOfEncoding(LPCSTR lpszEnd)
{
	TRACE_MAPILIB(TRACE_ENTRY, "CchOfEncoding", "");
	TRACE_MAPILIB(TRACE_RETURN, "CchOfEncoding", "");
	return 0;
}

SCODE __stdcall ScCopyProps( int cprop,  LPSPropValue rgprop,  LPVOID pvDst,  ULONG *pcb )
{
	TRACE_MAPILIB1(TRACE_ENTRY, "ScCopyProps", "%s", PropNameFromPropArray(cprop, rgprop).c_str());
	BYTE *lpHeap = (BYTE *)pvDst + sizeof(SPropValue) * cprop;
	LPSPropValue lpProp = (LPSPropValue)pvDst;

	for (int i = 0 ; i < cprop; ++i) {
		lpProp[i] = rgprop[i];

		switch(PROP_TYPE(rgprop[i].ulPropTag)) {
		case PT_ERROR:
			lpProp[i].Value.err = rgprop[i].Value.err;
			break;
		case PT_NULL:
		case PT_OBJECT:
			lpProp[i].Value.x = rgprop[i].Value.x;
			break;
		case PT_BOOLEAN:
			lpProp[i].Value.b = rgprop[i].Value.b;
			break;
		case PT_SHORT:
			lpProp[i].Value.i = rgprop[i].Value.i;
			break;
		case PT_MV_SHORT:
			for (ULONG j = 0; j < rgprop[i].Value.MVi.cValues; ++j)
				lpProp[i].Value.MVi.lpi[j] = rgprop[i].Value.MVi.lpi[j];
			break;
		case PT_LONG:
			lpProp[i].Value.l = rgprop[i].Value.l;
			break;
		case PT_MV_LONG:
			for (ULONG j = 0; j < rgprop[i].Value.MVl.cValues; ++j)
				lpProp[i].Value.MVl.lpl[j] = rgprop[i].Value.MVl.lpl[j];
			break;
		case PT_LONGLONG:
			memcpy(&lpProp[i].Value.li, &rgprop[i].Value.li, sizeof(rgprop[i].Value.li));
			break;
		case PT_MV_LONGLONG:
			for (ULONG j = 0; j < rgprop[i].Value.MVli.cValues; ++j)
				memcpy(&lpProp[i].Value.MVli.lpli[j], &rgprop[i].Value.MVli.lpli[j], sizeof(rgprop[i].Value.MVli.lpli[j]));
			break;
		case PT_FLOAT:
			lpProp[i].Value.flt = rgprop[i].Value.flt;
			break;
		case PT_MV_FLOAT:
			for (ULONG j = 0; j < rgprop[i].Value.MVflt.cValues; ++j)
				lpProp[i].Value.MVflt.lpflt[j] = rgprop[i].Value.MVflt.lpflt[j];
			break;
		case PT_DOUBLE:
			lpProp[i].Value.dbl = rgprop[i].Value.dbl;
			break;
		case PT_MV_DOUBLE:
			for (ULONG j = 0; j < rgprop[i].Value.MVdbl.cValues; ++j)
				lpProp[i].Value.MVdbl.lpdbl[j] = rgprop[i].Value.MVdbl.lpdbl[j];
			break;
		case PT_CURRENCY:
			memcpy(&lpProp[i].Value.cur, &rgprop[i].Value.cur, sizeof(rgprop[i].Value.cur));
			break;
		case PT_MV_CURRENCY:
			for (ULONG j = 0; j < rgprop[i].Value.MVcur.cValues; ++j)
				memcpy(&lpProp[i].Value.MVcur.lpcur[j], &rgprop[i].Value.MVcur.lpcur[j], sizeof(rgprop[i].Value.MVcur.lpcur[j]));
			break;
		case PT_SYSTIME:
			memcpy(&lpProp[i].Value.ft, &rgprop[i].Value.ft, sizeof(rgprop[i].Value.ft));
			break;
		case PT_MV_SYSTIME:
			for (ULONG j = 0; j < rgprop[i].Value.MVft.cValues; ++j)
				memcpy(&lpProp[i].Value.MVft.lpft[j], &rgprop[i].Value.MVft.lpft[j], sizeof(rgprop[i].Value.MVft.lpft[j]));
			break;
		case PT_APPTIME:
			lpProp[i].Value.at = rgprop[i].Value.at;
			break;
		case PT_MV_APPTIME:
			for (ULONG j = 0; j < rgprop[i].Value.MVat.cValues; ++j)
				lpProp[i].Value.MVat.lpat[j] = rgprop[i].Value.MVat.lpat[j];
			break;

		case PT_CLSID:
			memcpy(lpHeap, rgprop[i].Value.lpguid, sizeof(GUID));
			lpProp[i].Value.lpguid = (LPGUID)lpHeap;
			lpHeap += sizeof(GUID);
			break;
		case PT_MV_CLSID:
			memcpy(lpHeap, rgprop[i].Value.MVguid.lpguid, sizeof(GUID) * rgprop[i].Value.MVguid.cValues);
			lpProp[i].Value.MVguid.lpguid = (LPGUID)lpHeap;
			lpHeap += sizeof(GUID) * rgprop[i].Value.MVguid.cValues;
			break;

#define COPY_STRING8(__heap, __target, __source)	\
{													\
	strcpy((char *)(__heap), (__source));			\
	(__target) = (char *)(__heap);					\
	(__heap) += strlen((__source)) + 1;				\
}
		case PT_STRING8:
			COPY_STRING8(lpHeap, lpProp[i].Value.lpszA, rgprop[i].Value.lpszA);
			break;
		case PT_MV_STRING8:
			for (ULONG j = 0; j < rgprop[i].Value.MVszA.cValues; ++j)
				COPY_STRING8(lpHeap, lpProp[i].Value.MVszA.lppszA[j], rgprop[i].Value.MVszA.lppszA[j]);
			break;

#define COPY_BINARY(__heap, __target, __source)		\
{													\
	memcpy((__heap), (__source).lpb, (__source).cb);\
	(__target).lpb = (__heap);						\
	(__target).cb = (__source).cb;					\
	(__heap) += (__source).cb;						\
}
		case PT_BINARY:
			COPY_BINARY(lpHeap, lpProp[i].Value.bin, rgprop[i].Value.bin);
			break;
		case PT_MV_BINARY:
			for (ULONG j = 0; j < rgprop[i].Value.MVbin.cValues; ++j)
				COPY_BINARY(lpHeap, lpProp[i].Value.MVbin.lpbin[j], rgprop[i].Value.MVbin.lpbin[j]);
			break;

#define COPY_UNICODE(__heap, __target, __source)				\
{																\
		(__target) = lstrcpyW((WCHAR *)(__heap), (__source));	\
		(__heap) += (lstrlenW((__source)) + 1) * sizeof(WCHAR);	\
}
		case PT_UNICODE:
			COPY_UNICODE(lpHeap, lpProp[i].Value.lpszW, rgprop[i].Value.lpszW);
			break;
		case PT_MV_UNICODE:
			for (ULONG j = 0; j < rgprop[i].Value.MVszW.cValues; ++j)
				COPY_UNICODE(lpHeap, lpProp[i].Value.MVszW.lppszW[j], rgprop[i].Value.MVszW.lppszW[j]);
			break;
		default:
			break;
		}
	}

	if(pcb)
		*pcb = lpHeap - (BYTE *)pvDst;

	TRACE_MAPILIB1(TRACE_RETURN, "ScCopyProps", "%s", PropNameFromPropArray(cprop, (LPSPropValue)pvDst).c_str());
	return S_OK;
}

SCODE __stdcall ScCountProps(int cValues, LPSPropValue lpPropArray, ULONG *lpcb)
{
	SCODE sc = S_OK;
	ULONG ulSize = 0;

	for (int i = 0; i < cValues; ++i) {
		ulSize += sizeof(SPropValue);

		switch(PROP_TYPE(lpPropArray[i].ulPropTag)) {
		case PROP_ID_NULL:
		case PROP_ID_INVALID:
			sc = MAPI_E_INVALID_PARAMETER;
			break;
		case PT_STRING8:
			ulSize += strlen(lpPropArray[i].Value.lpszA)+1;
			break;
		case PT_MV_STRING8:
			ulSize += sizeof(LPTSTR) * lpPropArray[i].Value.MVszA.cValues;
			for (unsigned int j = 0; j < lpPropArray[i].Value.MVszA.cValues; ++j)
				ulSize += strlen(lpPropArray[i].Value.MVszA.lppszA[j])+1;
			break;
		case PT_BINARY:
			ulSize += lpPropArray[i].Value.bin.cb;
			break;
		case PT_MV_BINARY:
			ulSize += sizeof(SBinary) * lpPropArray[i].Value.MVbin.cValues;
			for (unsigned int j = 0; j < lpPropArray[i].Value.MVbin.cValues; ++j)
				ulSize += lpPropArray[i].Value.MVbin.lpbin[j].cb;
			break;
		case PT_UNICODE:
			ulSize += (lstrlenW(lpPropArray[i].Value.lpszW) + 1) * sizeof(WCHAR);
			break;
		case PT_MV_UNICODE:
			ulSize += sizeof(LPWSTR) * lpPropArray[i].Value.MVszW.cValues;
			for (unsigned int j = 0; j < lpPropArray[i].Value.MVszW.cValues; ++j)
				ulSize += (lstrlenW(lpPropArray[i].Value.MVszW.lppszW[j]) + 1) * sizeof(WCHAR);
			break;
		case PT_CLSID:
			ulSize += sizeof(GUID);
			break;
		case PT_MV_CLSID:
			ulSize += (lpPropArray[i].Value.MVguid.cValues * sizeof(GUID));
			break;
		default:
			break;
		}
	}

	if (lpcb)
		*lpcb = ulSize;

	TRACE_MAPILIB1(TRACE_RETURN, "ScCountProps", "%d", ulSize);
	return sc;
}

SCODE __stdcall ScInitMapiUtil(ULONG ulFlags)
{
	TRACE_MAPILIB(TRACE_ENTRY, "ScInitMAPIUtil", "");
	TRACE_MAPILIB1(TRACE_RETURN, "ScInitMAPIUtil", "0x%08x", S_OK);
	return S_OK;
}

BOOL __stdcall FBinFromHex(LPTSTR sz, LPBYTE pb)
{
	TRACE_MAPILIB1(TRACE_ENTRY, "FBinFromHex", "%s", sz);
	ULONG len;
	memory_ptr<BYTE> lpBin;

	Util::hex2bin((char *)sz, strlen((char *)sz), &len, &~lpBin);
	memcpy(pb, lpBin, len);
	TRACE_MAPILIB1(TRACE_RETURN, "FBinFromHex", "%s", sz);
	return true;
}

void __stdcall HexFromBin(LPBYTE pb, int cb, LPTSTR sz)
{
	std::string hex = bin2hex(cb, pb);
	TRACE_MAPILIB1(TRACE_ENTRY, "HexFromBin", "%s", hex.c_str());

	strcpy((char *)sz, hex.c_str());

	TRACE_MAPILIB1(TRACE_RETURN, "HexFromBin", "%s", sz);
}

// @todo according to MSDN, this function also supports Unicode strings
// 		but I don't see how that's easy possible
LPTSTR __stdcall SzFindCh(LPCTSTR lpsz, USHORT ch)
{
	TRACE_MAPILIB(TRACE_ENTRY, "SzFindCh", "");
	LPTSTR lpszFind = (LPTSTR)strchr((char*)lpsz, ch);
	TRACE_MAPILIB(TRACE_RETURN, "SzFindCh", "");
	return lpszFind;
}

int __stdcall MNLS_CompareStringW(LCID Locale, DWORD dwCmpFlags, LPCWSTR lpString1, int cchCount1, LPCWSTR lpString2, int cchCount2)
{
	TRACE_MAPILIB4(TRACE_ENTRY, "MNLS_CompareStringW", "%d %S, %d %S", cchCount1, lpString1, cchCount2, lpString2);
	// FIXME: we're ignoring Locale, dwCmpFlags, cchCount1 and cchCount2
	int ulCmp = wcscmp((LPWSTR)lpString1, (LPWSTR)lpString2);
	TRACE_MAPILIB1(TRACE_RETURN, "MNLS_CompareStringW", "%d", ulCmp);
	return ulCmp;
}

int __stdcall MNLS_lstrlenW(LPCWSTR lpString)
{
	TRACE_MAPILIB1(TRACE_ENTRY, "MNLS_lstrlenW", "%S", lpString);
	int ulLen = lstrlenW(lpString);
	TRACE_MAPILIB2(TRACE_RETURN, "MNLS_lstrlenW", "%S: %d", lpString, ulLen);
	return ulLen;
}

int __stdcall MNLS_lstrlen(LPCSTR lpString)
{
	TRACE_MAPILIB1(TRACE_ENTRY, "MNLS_lstrlen", "%S", lpString);
	int ulLen = lstrlenW((LPCWSTR)lpString);
	TRACE_MAPILIB2(TRACE_RETURN, "MNLS_lstrlen", "%S: %d", lpString, ulLen);
	return ulLen;
}

int __stdcall MNLS_lstrcmpW(LPCWSTR lpString1, LPCWSTR lpString2)
{
	TRACE_MAPILIB2(TRACE_ENTRY, "lstrcmpW", "%S, %S", lpString1, lpString2);
	int ulCmp = lstrcmpW(lpString1, lpString2);
	TRACE_MAPILIB3(TRACE_RETURN, "lstrcmpW", "%S, %S: %d", lpString1, lpString2, ulCmp);
	return ulCmp;
}

LPWSTR __stdcall MNLS_lstrcpyW(LPWSTR lpString1, LPCWSTR lpString2)
{
	TRACE_MAPILIB1(TRACE_ENTRY, "MNLS_lstrcpyW", "%S", lpString2);
	LPWSTR str = lstrcpyW(lpString1, lpString2);
	TRACE_MAPILIB(TRACE_RETURN, "MNLS_lstrcpyW", "");
	return str;
}

FILETIME __stdcall FtAddFt( FILETIME Addend1,  FILETIME Addend2    )
{
	TRACE_MAPILIB4(TRACE_ENTRY, "FtAddFt", "(%u,%u) (%u,%u)", Addend1.dwHighDateTime, Addend1.dwLowDateTime, Addend2.dwHighDateTime, Addend2.dwLowDateTime);
	FILETIME ft;
	unsigned long long l = ((unsigned long long)Addend1.dwHighDateTime << 32) + Addend1.dwLowDateTime;
	l += ((unsigned long long)Addend2.dwHighDateTime << 32) + Addend2.dwLowDateTime;

	ft.dwHighDateTime = l >> 32;
	ft.dwLowDateTime = l & 0xffffffff;

	TRACE_MAPILIB2(TRACE_RETURN, "FtAddFt", "(%u,%u)", ft.dwHighDateTime, ft.dwLowDateTime);
	return ft;
}

FILETIME __stdcall FtSubFt( FILETIME Minuend,  FILETIME Subtrahend )
{
	TRACE_MAPILIB4(TRACE_ENTRY, "FtSubFt", "(%u,%u) (%u,%u)", Minuend.dwHighDateTime, Minuend.dwLowDateTime, Subtrahend.dwHighDateTime, Subtrahend.dwLowDateTime);
	FILETIME ft;
	unsigned long long l = ((unsigned long long)Minuend.dwHighDateTime << 32) + Minuend.dwLowDateTime;
	l -= ((unsigned long long)Subtrahend.dwHighDateTime << 32) + Subtrahend.dwLowDateTime;

	ft.dwHighDateTime = l >> 32;
	ft.dwLowDateTime = l & 0xffffffff;

	TRACE_MAPILIB2(TRACE_RETURN, "FtSubFt", "(%u,%u)", ft.dwHighDateTime, ft.dwLowDateTime);
	return ft;
}

FILETIME __stdcall FtDivFtBogus(FILETIME f, FILETIME f2, DWORD n)
{
	TRACE_MAPILIB5(TRACE_ENTRY, "FtDivFtBogus", "(%u, %u), (%u, %u), %u", f.dwHighDateTime, f.dwLowDateTime, f2.dwHighDateTime, f2.dwLowDateTime, n);
	// Obtained by experiment: this function does (f*f2) >> (n+64)
	// Since we don't have a good int64 * int64, we do our own addition_plus_bitshift
	// which discards the lowest 64 bits on the fly.
	unsigned long long shift = 0;
	unsigned long long ret = (unsigned long long)f.dwHighDateTime * f2.dwHighDateTime;
	ret += ((unsigned long long)f.dwLowDateTime * f2.dwHighDateTime) >> 32;
	ret += ((unsigned long long)f.dwHighDateTime * f2.dwLowDateTime) >> 32;

	// The remainder may give us a few more, use the top 32 bits of the remainder.
	shift += (((unsigned long long)f.dwLowDateTime * f2.dwHighDateTime) & 0xFFFFFFFF);
	shift += (((unsigned long long)f.dwHighDateTime * f2.dwLowDateTime) & 0xFFFFFFFF);
	shift += ((unsigned long long)f.dwLowDateTime * f2.dwLowDateTime) >> 32;

	ret += shift >> 32;

	ret >>= n;

	FILETIME ft;
	ft.dwHighDateTime = ret >> 32;
	ft.dwLowDateTime = ret & 0xFFFFFFFF;

	TRACE_MAPILIB2(TRACE_RETURN, "FtDivFtBogus", "(%u %u)", ft.dwHighDateTime, ft.dwLowDateTime);
	return ft;
}

FILETIME __stdcall FtMulDw(DWORD ftMultiplier, FILETIME ftMultiplicand)
{
	TRACE_MAPILIB3(TRACE_ENTRY, "FtMulDw", "%d x (%d, %d)", ftMultiplier, ftMultiplicand.dwHighDateTime, ftMultiplicand.dwLowDateTime);
	FILETIME ft;
	unsigned long long t = ((unsigned long long)ftMultiplicand.dwHighDateTime << 32) + (ftMultiplicand.dwLowDateTime & 0xffffffff);

	t *= ftMultiplier;

	ft.dwHighDateTime = t >> 32;
	ft.dwLowDateTime = t & 0xFFFFFFFF;

	TRACE_MAPILIB2(TRACE_RETURN, "FtMulDw", "%(%d, %d)", ft.dwHighDateTime, ft.dwLowDateTime);
	return ft;
}

LONG __stdcall MAPIInitIdle( LPVOID lpvReserved  )
{
	TRACE_MAPILIB(TRACE_ENTRY, "MAPIInitIdle", "");
	TRACE_MAPILIB(TRACE_RETURN, "MAPIInitIdle", "");
	return 0;
}

void __stdcall MAPIDeinitIdle(void)
{
	TRACE_MAPILIB(TRACE_ENTRY, "MAPIDeinitIdle", "");
	TRACE_MAPILIB(TRACE_RETURN, "MAPIDeinitIdle", "");
}

void __stdcall DeregisterIdleRoutine( FTG ftg  )
{
	TRACE_MAPILIB(TRACE_ENTRY, "DeregisterIdleRoutine", "");
	TRACE_MAPILIB(TRACE_RETURN, "DeregisterIdleRoutine", "");
}

void __stdcall EnableIdleRoutine( FTG ftg,  BOOL fEnable  )
{
	TRACE_MAPILIB(TRACE_ENTRY, "EnableIdleRoutine", "");
	TRACE_MAPILIB(TRACE_RETURN, "EnableIdleRoutine", "");
}

void __stdcall ChangeIdleRoutine(FTG ftg, PFNIDLE pfnIdle, LPVOID pvIdleParam, short priIdle, ULONG csecIdle, USHORT iroIdle, USHORT ircIdle)
{
	TRACE_MAPILIB(TRACE_ENTRY, "ChangeIdleRoutine", "");
	TRACE_MAPILIB(TRACE_RETURN, "ChangeIdleRoutine", "");
}

FTG __stdcall FtgRegisterIdleRoutine(PFNIDLE pfnIdle,  LPVOID pvIdleParam,  short priIdle,  ULONG csecIdle,  USHORT iroIdle)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FtgRegisterIdleRoutine", "");
	FTG f = NULL;
	TRACE_MAPILIB(TRACE_RETURN, "FtgRegisterIdleRoutine", "");
	return f;
}

const WORD kwBaseOffset = 0xAC00;  // Hangul char range (AC00-D7AF)
LPWSTR __stdcall EncodeID(ULONG cbEID, LPENTRYID rgbID, LPWSTR *lpWString)
{
	TRACE_MAPILIB(TRACE_ENTRY, "EncodeID", "");
	ULONG   i = 0;
	LPWSTR  pwzDst = NULL;
	LPBYTE  pbSrc = NULL;
	LPWSTR  pwzIDEncoded = NULL;

	// rgbID is the item Entry ID or the attachment ID
	// cbID is the size in bytes of rgbID

	// Allocate memory for pwzIDEncoded
	pwzIDEncoded = new WCHAR[cbEID+1];
	if (!pwzIDEncoded)
		goto exit;

	for (i = 0, pbSrc = (LPBYTE)rgbID, pwzDst = pwzIDEncoded;
	     i < cbEID; ++i, ++pbSrc, ++pwzDst)
		*pwzDst = (WCHAR) (*pbSrc + kwBaseOffset);

	// Ensure NULL terminated
	*pwzDst = L'\0';

exit:
	// pwzIDEncoded now contains the entry ID encoded.
	TRACE_MAPILIB1(TRACE_RETURN, "EncodeID", "%s", (pwzIDEncoded ? "SUCCESS" : "FAILED"));
	return pwzIDEncoded;
}

void __stdcall FDecodeID(LPCSTR lpwEncoded, LPENTRYID *lpDecoded, ULONG *cbEncoded)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FDecodeID", "");
	TRACE_MAPILIB(TRACE_RETURN, "FDecodeID", "");
	// ?
}

BOOL __stdcall FBadRglpszA(const TCHAR *, ULONG cStrings)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadRglpszA", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadRglpszA", "");
	return FALSE;
}

BOOL __stdcall FBadRglpszW(const wchar_t *, ULONG cStrings)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadRglpszW", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadRglpszW", "");
	return FALSE;
}

BOOL __stdcall FBadRowSet(const SRowSet *)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadRowSet", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadRowSet", "");
	return FALSE;
}

BOOL __stdcall FBadRglpNameID(LPMAPINAMEID *lppNameId, ULONG cNames)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadRglpNameID", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadRglpNameID", "");
	return FALSE;
}

ULONG __stdcall FBadPropTag(ULONG ulPropTag)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadPropTag", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadPropTag", "");
	return FALSE;
}

ULONG __stdcall FBadRow(const SRow *)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadRow", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadRow", "");
	return FALSE;
}

ULONG __stdcall FBadProp(const SPropValue *)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadProp", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadProp", "");
	return FALSE;
}

ULONG __stdcall FBadColumnSet(const SPropTagArray *lpptaCols)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadColumnSet", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadColumnSet", "");
	return FALSE;
}

ULONG __stdcall FBadSortOrderSet(const SSortOrderSet *)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadSortOrderSet", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadSortOrderSet", "");
	return FALSE;
}

BOOL __stdcall FBadEntryList(const SBinaryArray *lpEntryList)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadEntryList", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadEntryList", "");
	return FALSE;
}

ULONG __stdcall FBadRestriction(const SRestriction *)
{
	TRACE_MAPILIB(TRACE_ENTRY, "FBadRestriction", "");
	TRACE_MAPILIB(TRACE_RETURN, "FBadRestriction", "");
	return FALSE;
}

HRESULT GetConnectionProperties(LPSPropValue lpServer, LPSPropValue lpUsername, ULONG *lpcValues, LPSPropValue *lppProps)
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpProps;
	char *szUsername;
	std::string strServerPath;
	ULONG cProps = 0;

	if (lpServer == nullptr || lpUsername == nullptr)
		return MAPI_E_UNCONFIGURED;
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * 5, &~lpProps);
	if (hr != hrSuccess)
		return hr;
	if (m4l_lpConfig->GetSetting("server_address")[0])
		strServerPath = (std::string)"https://" + m4l_lpConfig->GetSetting("server_address") + ":" + m4l_lpConfig->GetSetting("ssl_port") + "/";
	else
		strServerPath = (std::string)"https://" + lpServer->Value.lpszA + ":" + m4l_lpConfig->GetSetting("ssl_port") + "/";
	szUsername = lpUsername->Value.lpszA;

	if(strrchr(szUsername, '='))
		szUsername = strrchr(szUsername, '=')+1;

	lpProps[cProps].ulPropTag = PR_EC_PATH;
	if ((hr = MAPIAllocateMore(strServerPath.size() + 1, lpProps, (void**)&lpProps[cProps].Value.lpszA)) != hrSuccess)
		return hr;
	memcpy(lpProps[cProps++].Value.lpszA, strServerPath.c_str(),strServerPath.size() + 1);

	lpProps[cProps].ulPropTag = PR_EC_USERNAME_A;
	if ((hr = MAPIAllocateMore(strlen(szUsername) + 1, lpProps, (void**)&lpProps[cProps].Value.lpszA)) != hrSuccess)
		return hr;
	memcpy(lpProps[cProps++].Value.lpszA, szUsername, strlen(szUsername) + 1);

	lpProps[cProps].ulPropTag = PR_EC_USERPASSWORD_A;
	if ((hr = MAPIAllocateMore(1, lpProps, (void**)&lpProps[cProps].Value.lpszA)) != hrSuccess)
		return hr;
	memcpy(lpProps[cProps++].Value.lpszA, "", 1);

	lpProps[cProps].ulPropTag = PR_EC_SSLKEY_FILE;
	if ((hr = MAPIAllocateMore(strlen(m4l_lpConfig->GetSetting("ssl_key_file")) + 1, lpProps, (void**)&lpProps[cProps].Value.lpszA)) != hrSuccess)
		return hr;
	memcpy(lpProps[cProps++].Value.lpszA, m4l_lpConfig->GetSetting("ssl_key_file"), strlen(m4l_lpConfig->GetSetting("ssl_key_file")) + 1);

	lpProps[cProps].ulPropTag = PR_EC_SSLKEY_PASS;
	if ((hr = MAPIAllocateMore(strlen(m4l_lpConfig->GetSetting("ssl_key_pass")) + 1, lpProps, (void**)&lpProps[cProps].Value.lpszA)) != hrSuccess)
		return hr;
	memcpy(lpProps[cProps++].Value.lpszA, m4l_lpConfig->GetSetting("ssl_key_pass"), strlen(m4l_lpConfig->GetSetting("ssl_key_pass")) + 1);

	*lpcValues = cProps;
	*lppProps = lpProps.release();
	return hrSuccess;
}

