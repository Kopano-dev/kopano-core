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

#ifndef ECFIFOSTREAM_H
#define ECFIFOSTREAM_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>

class ECFifoBuffer;

/**
 * This is a FIFO stream that exposes two separate IStream interfaces, one
 * for reading, and one for writing. The semantics are much like the pipe()
 * system call; A reader will block while the writer has not yet sent enough
 * data, but once the writer has released it's interface, the reader will receive
 * an EOF when no more data is available. Conversely, when a reader has closed
 * its connection, the writer will receive a write error when attempting to write
 *
 * Each object supports normal reference counting using AddRef/Release.
 *
 * Although we expose the full IStream interface, the reader and writer each only
 * support the IStream::Read() or IStream::Write() calls. All other calls will
 * return MAPI_E_NO_SUPPORT.
 */

HRESULT CreateFifoStreamPair(IStream **lppReader, IStream **lppWriter);

/**
 * This class exists only to apply refcounting to ECFifoBuffer
 */
class ECFifoStream : public ECUnknown {
public:
    static HRESULT Create(ECFifoStream **lppFifoStream);
    
    ECFifoBuffer *m_lpFifoBuffer;

private:
    ECFifoStream();
    ~ECFifoStream();
};

// Class defs for reader and writer classes
class ECFifoStreamReader _zcp_final : public ECUnknown {
public:
    static HRESULT Create(ECFifoStream *lpBuffer, ECFifoStreamReader **lppReader);
    
    HRESULT QueryInterface(REFIID refiid, LPVOID *lppInterface) _zcp_override;
    HRESULT Read(void *pv, ULONG cb, ULONG *pcbRead);
    
private:
    ECFifoStreamReader(ECFifoStream *lpBuffer);
    ~ECFifoStreamReader();
    
    class xStream _zcp_final : public IStream {
    public:
        virtual ULONG   __stdcall AddRef(void) _zcp_override;
        virtual ULONG   __stdcall Release(void) _zcp_override;
        virtual HRESULT __stdcall QueryInterface(REFIID refiid, LPVOID *lppInterface) _zcp_override;

        virtual HRESULT __stdcall Read(void *pv, ULONG cb, ULONG *pcbRead) _zcp_override;
        virtual HRESULT __stdcall Write(const void *pv, ULONG cb, ULONG *pcbWritten) _zcp_override;
        virtual HRESULT __stdcall Seek(LARGE_INTEGER dlibmove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) _zcp_override;
        virtual HRESULT __stdcall SetSize(ULARGE_INTEGER libNewSize) _zcp_override;
        virtual HRESULT __stdcall CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) _zcp_override;
        virtual HRESULT __stdcall Commit(DWORD grfCommitFlags) _zcp_override;
        virtual HRESULT __stdcall Revert() _zcp_override;
        virtual HRESULT __stdcall LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) _zcp_override;
        virtual HRESULT __stdcall UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) _zcp_override;
        virtual HRESULT __stdcall Stat(STATSTG *pstatstg, DWORD grfStatFlag) _zcp_override;
        virtual HRESULT __stdcall Clone(IStream **ppstm) _zcp_override;
    } m_xStream;
    
    ECFifoStream *m_lpFifoBuffer;
};

class ECFifoStreamWriter : public ECUnknown {
public:
    static HRESULT Create(ECFifoStream *lpBuffer, ECFifoStreamWriter **lppWriter);
    
    HRESULT QueryInterface(REFIID refiid, LPVOID *lppInterface) _zcp_override;
    HRESULT Write(const void *pv, ULONG cb, ULONG *pcbWritten);
    HRESULT Commit(DWORD grfCommitFlags);
    
private:
    ECFifoStreamWriter(ECFifoStream *lpBuffer);
    ~ECFifoStreamWriter();
    
    class xStream _zcp_final : public IStream {
    public:
        virtual ULONG   __stdcall AddRef(void) _zcp_override;
        virtual ULONG   __stdcall Release(void) _zcp_override;
        virtual HRESULT __stdcall QueryInterface(REFIID refiid, LPVOID *lppInterface) _zcp_override;

        virtual HRESULT __stdcall Read(void *pv, ULONG cb, ULONG *pcbRead) _zcp_override;
        virtual HRESULT __stdcall Write(const void *pv, ULONG cb, ULONG *pcbWritten) _zcp_override;
        virtual HRESULT __stdcall Seek(LARGE_INTEGER dlibmove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) _zcp_override;
        virtual HRESULT __stdcall SetSize(ULARGE_INTEGER libNewSize) _zcp_override;
        virtual HRESULT __stdcall CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) _zcp_override;
        virtual HRESULT __stdcall Commit(DWORD grfCommitFlags) _zcp_override;
        virtual HRESULT __stdcall Revert() _zcp_override;
        virtual HRESULT __stdcall LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) _zcp_override;
        virtual HRESULT __stdcall UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) _zcp_override;
        virtual HRESULT __stdcall Stat(STATSTG *pstatstg, DWORD grfStatFlag) _zcp_override;
        virtual HRESULT __stdcall Clone(IStream **ppstm) _zcp_override;
    } m_xStream;
    
    ECFifoStream *m_lpFifoBuffer;
};

#endif
