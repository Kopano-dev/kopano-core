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

#include "ECFifoStream.h"
#include "ECFifoBuffer.h"
#include <kopano/ECInterfaceDefs.h>
#include <kopano/ECGuid.h>
#include <kopano/Trace.h>

#include <mapidefs.h>

/* This file contains almost 100% boilerplate code. It does nothing, just forwards calls
 * to other objects */

/**
 * Create an IStream pair as a FIFO
 *
 * Works much like the pipe() system call.
 */
HRESULT CreateFifoStreamPair(IStream **lppReader, IStream **lppWriter) {
    HRESULT hr = hrSuccess;
    ECFifoStream *lpFifoStream = NULL;
    ECFifoStreamReader *lpFifoStreamReader = NULL;
    ECFifoStreamWriter *lpFifoStreamWriter = NULL;
    
    hr = ECFifoStream::Create(&lpFifoStream);
    if(hr != hrSuccess)
        goto exit;
        
    hr = ECFifoStreamReader::Create(lpFifoStream, &lpFifoStreamReader);
    if(hr != hrSuccess)
        goto exit;
        
    hr = ECFifoStreamWriter::Create(lpFifoStream, &lpFifoStreamWriter);
    if(hr != hrSuccess)
        goto exit;

    hr = lpFifoStreamReader->QueryInterface(IID_IStream, (void **)lppReader);
    if(hr != hrSuccess)
        goto exit;
    
    hr = lpFifoStreamWriter->QueryInterface(IID_IStream, (void **)lppWriter);
    if(hr != hrSuccess)
        goto exit;
        
exit:
    if (lpFifoStream)
        lpFifoStream->Release();
        
    if (lpFifoStreamReader)
        lpFifoStreamReader->Release();
        
    if (lpFifoStreamWriter)
        lpFifoStreamWriter->Release();
    
    return hr;
}

HRESULT ECFifoStream::Create(ECFifoStream **lppFifoStream)
{
    ECFifoStream *lpStream = new ECFifoStream();
    lpStream->AddRef();
    *lppFifoStream = lpStream;
    
    return hrSuccess;
}

ECFifoStream::ECFifoStream()
{
    m_lpFifoBuffer = new ECFifoBuffer();
}

ECFifoStream::~ECFifoStream()
{
    delete m_lpFifoBuffer;
}
    
HRESULT ECFifoStreamReader::Create(ECFifoStream *lpBuffer, ECFifoStreamReader **lppReader)
{
    ECFifoStreamReader *lpReader = new ECFifoStreamReader(lpBuffer);
    lpReader->AddRef();
    *lppReader = lpReader;
    
    return hrSuccess;
}

HRESULT ECFifoStreamReader::QueryInterface(REFIID refiid, LPVOID *lppInterface)
{
    REGISTER_INTERFACE(IID_ECUnknown, this);
    REGISTER_INTERFACE(IID_IStream, &this->m_xStream);
    
    return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECFifoStreamReader::Read(void *pv, ULONG cb, ULONG *pcbRead)
{
	size_t read;
	HRESULT hr = m_lpFifoBuffer->m_lpFifoBuffer->Read(pv, cb, 0, &read);
	if (hr != hrSuccess)
		return hr;
	*pcbRead = read;
	return hrSuccess;
}
    
ECFifoStreamReader::ECFifoStreamReader(ECFifoStream *lpBuffer)
{
    m_lpFifoBuffer = lpBuffer;
    m_lpFifoBuffer->AddRef();
}

ECFifoStreamReader::~ECFifoStreamReader()
{
    m_lpFifoBuffer->m_lpFifoBuffer->Close(ECFifoBuffer::cfRead); // Tell the fifo that it will not be used anymore
    m_lpFifoBuffer->Release();
}
    
ULONG ECFifoStreamReader::xStream::AddRef()
{
    METHOD_PROLOGUE_(ECFifoStreamReader, Stream);
    TRACE_MAPI(TRACE_ENTRY, "IStream::AddRef", NULL);
    return pThis->AddRef();
}

ULONG ECFifoStreamReader::xStream::Release()
{
    METHOD_PROLOGUE_(ECFifoStreamReader, Stream);
    TRACE_MAPI(TRACE_ENTRY, "IStream::Release", NULL);
    return pThis->Release();
}

DEF_HRMETHOD			(TRACE_MAPI, ECFifoStreamReader, Stream, QueryInterface, (REFIID, refiid), (LPVOID *, lppInterface))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamReader, Stream, Write, (const void *, pv), (ULONG, cb), (ULONG *, pcbWritten))
DEF_HRMETHOD			(TRACE_MAPI, ECFifoStreamReader, Stream, Read, (void *, pv), (ULONG, cb), (ULONG *, pcbRead))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamReader, Stream, Seek, (LARGE_INTEGER, dlibmove), (DWORD, dwOrigin), (ULARGE_INTEGER *, plibNewPosition))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamReader, Stream, SetSize, (ULARGE_INTEGER, libNewSize))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamReader, Stream, CopyTo, (IStream *, pstm), (ULARGE_INTEGER, cb), (ULARGE_INTEGER *, pcbRead), (ULARGE_INTEGER *, pcbWritten))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamReader, Stream, Commit, (DWORD, grfCommitFlags))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamReader, Stream, Revert, (void))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamReader, Stream, LockRegion, (ULARGE_INTEGER, libOffset), (ULARGE_INTEGER, cb), (DWORD, dwLockType))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamReader, Stream, UnlockRegion, (ULARGE_INTEGER, libOffset), (ULARGE_INTEGER, cb), (DWORD, dwLockType))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamReader, Stream, Stat, (STATSTG *, pstatstg), (DWORD, grfStatFlag))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamReader, Stream, Clone, (IStream **, ppstm))

HRESULT ECFifoStreamWriter::Create(ECFifoStream *lpBuffer, ECFifoStreamWriter **lppWriter)
{
    ECFifoStreamWriter *lpWriter = new ECFifoStreamWriter(lpBuffer);
    lpWriter->AddRef();
    *lppWriter = lpWriter;
    return hrSuccess;
}

HRESULT ECFifoStreamWriter::QueryInterface(REFIID refiid, LPVOID *lppInterface)
{
    REGISTER_INTERFACE(IID_ECUnknown, this);
    REGISTER_INTERFACE(IID_IStream, &this->m_xStream);

    return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECFifoStreamWriter::Write(const void *pv, ULONG cb, ULONG *pcbWritten)
{
	size_t written;
	HRESULT hr = m_lpFifoBuffer->m_lpFifoBuffer->Write(pv, cb, 0, &written);
	if (hr != hrSuccess)
		return hr;
	*pcbWritten = written;
	return hrSuccess;
}

/**
 * Close the write-end and wait for the reader to finish
 */
HRESULT ECFifoStreamWriter::Commit(DWORD ulFlags)
{
	HRESULT hr = m_lpFifoBuffer->m_lpFifoBuffer->Close(ECFifoBuffer::cfWrite);
	if (hr != hrSuccess)
		return hr;
	return m_lpFifoBuffer->m_lpFifoBuffer->Flush();
}
    
ECFifoStreamWriter::ECFifoStreamWriter(ECFifoStream *lpBuffer)
{
    m_lpFifoBuffer = lpBuffer;
    m_lpFifoBuffer->AddRef();
}

ECFifoStreamWriter::~ECFifoStreamWriter()
{
    m_lpFifoBuffer->m_lpFifoBuffer->Close(ECFifoBuffer::cfWrite);	// Tell the fifo it will not receive any more data
    m_lpFifoBuffer->Release();
}

ULONG ECFifoStreamWriter::xStream::AddRef()
{
    METHOD_PROLOGUE_(ECFifoStreamWriter, Stream);
    TRACE_MAPI(TRACE_ENTRY, "IStream::AddRef", NULL);
    return pThis->AddRef();
}

ULONG ECFifoStreamWriter::xStream::Release()
{
    METHOD_PROLOGUE_(ECFifoStreamWriter, Stream);
    TRACE_MAPI(TRACE_ENTRY, "IStream::Release", NULL);
    return pThis->Release();
}
    
DEF_HRMETHOD			(TRACE_MAPI, ECFifoStreamWriter, Stream, QueryInterface, (REFIID, refiid), (LPVOID *, lppInterface))
DEF_HRMETHOD			(TRACE_MAPI, ECFifoStreamWriter, Stream, Write, (const void *, pv), (ULONG, cb), (ULONG *, pcbWritten))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamWriter, Stream, Read, (void *, pv), (ULONG, cb), (ULONG *, pcbRead))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamWriter, Stream, Seek, (LARGE_INTEGER, dlibmove), (DWORD, dwOrigin), (ULARGE_INTEGER *, plibNewPosition))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamWriter, Stream, SetSize, (ULARGE_INTEGER, libNewSize))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamWriter, Stream, CopyTo, (IStream *, pstm), (ULARGE_INTEGER, cb), (ULARGE_INTEGER *, pcbRead), (ULARGE_INTEGER *, pcbWritten))
DEF_HRMETHOD			(TRACE_MAPI, ECFifoStreamWriter, Stream, Commit, (DWORD, grfCommitFlags))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamWriter, Stream, Revert, (void))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamWriter, Stream, LockRegion, (ULARGE_INTEGER, libOffset), (ULARGE_INTEGER, cb), (DWORD, dwLockType))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamWriter, Stream, UnlockRegion, (ULARGE_INTEGER, libOffset), (ULARGE_INTEGER, cb), (DWORD, dwLockType))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamWriter, Stream, Stat, (STATSTG *, pstatstg), (DWORD, grfStatFlag))
DEF_HRMETHOD_NOSUPPORT	(TRACE_MAPI, ECFifoStreamWriter, Stream, Clone, (IStream **, ppstm))

