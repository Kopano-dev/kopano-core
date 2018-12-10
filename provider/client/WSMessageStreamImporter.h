/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef WSMessageStreamImporter_INCLUDED
#define WSMessageStreamImporter_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "soapStub.h"
#include "ECFifoBuffer.h"
#include <kopano/ECThreadPool.h>
#include "WSTransport.h"

class ECMAPIFolder;
class WSMessageStreamImporter;

/**
 * This class represents the data sink into which the stream data can be written.
 * It is returned from WSMessageStreamImporter::StartTransfer.
 */
class WSMessageStreamSink final : public KC::ECUnknown {
public:
	static HRESULT Create(KC::ECFifoBuffer *, ULONG timeout, WSMessageStreamImporter *, WSMessageStreamSink **);
	HRESULT Write(const void *data, unsigned int size);

	protected:
	~WSMessageStreamSink();

private:
	WSMessageStreamSink(KC::ECFifoBuffer *, ULONG timeout, WSMessageStreamImporter *);

	KC::ECFifoBuffer *m_lpFifoBuffer;
	WSMessageStreamImporter *m_lpImporter;
	ALLOC_WRAP_FRIEND;
};

/**
 * This class is used to perform a message stream import to the server.
 * The actual import call to the server is deferred until StartTransfer is called. When that
 * happens, the actual transfer is done on a worker thread so the calling thread can start writing
 * data in the returned WSMessageStreamSink. Once the returned stream is deleted, GetAsyncResult can
 * be used to wait for the worker and obtain its return values.
 */
class WSMessageStreamImporter final :
    public KC::ECUnknown, private KC::ECWaitableTask {
public:
	static HRESULT Create(ULONG flags, ULONG sync_id, ULONG eid_size, const ENTRYID *eid, ULONG feid_size, const ENTRYID *folder_eid, bool newmsg, const SPropValue *conflict_items, WSTransport *, WSMessageStreamImporter **);
	HRESULT StartTransfer(WSMessageStreamSink **lppSink);
	HRESULT GetAsyncResult(HRESULT *lphrResult);

	protected:
	~WSMessageStreamImporter();

private:
	WSMessageStreamImporter(ULONG flags, ULONG sync_id, const entryId &eid, const entryId &feid, bool newmsg, const propVal &conflict_items, WSTransport *, ULONG bufsize, ULONG timeout);
	void run();
	static void  *StaticMTOMReadOpen(struct soap *soap, void *handle, const char *id, const char *type, const char *description);
	static size_t StaticMTOMRead(struct soap *soap, void *handle, char *buf, size_t len);
	static void   StaticMTOMReadClose(struct soap *soap, void *handle);
	void  *MTOMReadOpen(struct soap *soap, void *handle, const char *id, const char *type, const char *description);
	size_t MTOMRead(struct soap *soap, void *handle, char *buf, size_t len);
	void   MTOMReadClose(struct soap *soap, void *handle);

	unsigned int m_ulFlags, m_ulSyncId;
	entryId m_sEntryId, m_sFolderEntryId;
	bool m_bNewMessage;
	propVal m_sConflictItems;
	KC::object_ptr<WSTransport> m_ptrTransport;
	HRESULT m_hr = hrSuccess;
	KC::ECFifoBuffer m_fifoBuffer;
	KC::ECThreadPool m_threadPool;
	ULONG m_ulTimeout;
};

typedef KC::object_ptr<WSMessageStreamImporter> WSMessageStreamImporterPtr;

#endif // ndef WSMessageStreamImporter_INCLUDED

