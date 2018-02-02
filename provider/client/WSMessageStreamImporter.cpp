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
#include <new>
#include "SOAPUtils.h"
#include "WSMessageStreamImporter.h"
#include "WSUtil.h"
#include "ECSyncSettings.h"
#include "soapKCmdProxy.h"

/**
 * Create a new WSMessageStreamSink instance
 * @param[in]	lpFifoBuffer	The fifobuffer to write the data into.
 * @param[in]	ulTimeout		The timeout in ms to use when writing to the
 * 								fifobuffer.
 * @param[out]	lppSink			The newly created object
 */
HRESULT WSMessageStreamSink::Create(ECFifoBuffer *lpFifoBuffer, ULONG ulTimeout, WSMessageStreamImporter *lpImporter, WSMessageStreamSink **lppSink)
{
	return alloc_wrap<WSMessageStreamSink>(lpFifoBuffer, ulTimeout,
	       lpImporter).put(lppSink);
}

/**
 * Write data into the underlaying fifo buffer.
 * @param[in]	lpData	Pointer to the data
 * @param[in]	cbData	The amount of data in bytes.
 */
HRESULT WSMessageStreamSink::Write(LPVOID lpData, ULONG cbData)
{
	HRESULT hr = hrSuccess;
	HRESULT hrAsync = hrSuccess;

	hr = kcerr_to_mapierr(m_lpFifoBuffer->Write(lpData, cbData, 0, NULL));
	if(hr != hrSuccess) {
		// Write failed, close the write-side of the FIFO
		m_lpFifoBuffer->Close(ECFifoBuffer::cfWrite);

		// Failure writing to the fifo. This means there must have been some error
		// on the other side of the FIFO. Since that is the root cause of the write failure,
		// return that instead of the error from the FIFO buffer (most probably a network
		// error, but others also possible, eg logon failure, session lost, etc)
		m_lpImporter->GetAsyncResult(&hrAsync);

		// Make sure that we only use the async error if there really was an error
		if(hrAsync != hrSuccess)
			hr = hrAsync;
	}

	return hr;
}

/**
 * @param[in]	lpFifoBuffer	The fifobuffer to write the data into.
 */
WSMessageStreamSink::WSMessageStreamSink(ECFifoBuffer *lpFifoBuffer, ULONG ulTimeout, WSMessageStreamImporter *lpImporter)
: m_lpFifoBuffer(lpFifoBuffer)
, m_lpImporter(lpImporter)
{ }

/**
 * Closes the underlaying fifo buffer, causing the reader to stop reading.
 */
WSMessageStreamSink::~WSMessageStreamSink()
{
	m_lpFifoBuffer->Close(ECFifoBuffer::cfWrite);
}

HRESULT WSMessageStreamImporter::Create(ULONG ulFlags, ULONG ulSyncId, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG cbFolderEntryID, LPENTRYID lpFolderEntryID, bool bNewMessage, LPSPropValue lpConflictItems, WSTransport *lpTransport, WSMessageStreamImporter **lppStreamImporter)
{
	HRESULT hr = hrSuccess;
	entryId sEntryId, sFolderEntryId;
	struct propVal sConflictItems;
	WSMessageStreamImporterPtr ptrStreamImporter;
	ECSyncSettings* lpSyncSettings = NULL;

	if (lppStreamImporter == NULL || 
		lpEntryID == NULL || cbEntryID == 0 || 
		lpFolderEntryID == NULL || cbFolderEntryID == 0 || 
		(bNewMessage == true && lpConflictItems != NULL) ||
		lpTransport == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = CopyMAPIEntryIdToSOAPEntryId(cbEntryID, lpEntryID, &sEntryId, false);
	if (hr != hrSuccess)
		goto exit;

	hr = CopyMAPIEntryIdToSOAPEntryId(cbFolderEntryID, lpFolderEntryID, &sFolderEntryId, false);
	if (hr != hrSuccess)
		goto exit;

	if (lpConflictItems) {
		hr = CopyMAPIPropValToSOAPPropVal(&sConflictItems, lpConflictItems);
		if (hr != hrSuccess)
			goto exit;
	}

	lpSyncSettings = &ECSyncSettings::instance;
	ptrStreamImporter.reset(new(std::nothrow) WSMessageStreamImporter(ulFlags, ulSyncId, sEntryId, sFolderEntryId, bNewMessage, sConflictItems, lpTransport, lpSyncSettings->StreamBufferSize(), lpSyncSettings->StreamTimeout()));
	if (ptrStreamImporter == nullptr) {
		hr = MAPI_E_NOT_ENOUGH_MEMORY;
		goto exit;
	}
	// The following are now owned by the stream importer
	sEntryId.__ptr = NULL;
	sFolderEntryId.__ptr = NULL;
	sConflictItems.Value.bin = NULL;
	*lppStreamImporter = ptrStreamImporter.release();

exit:
	s_free(nullptr, sEntryId.__ptr);
	s_free(nullptr, sFolderEntryId.__ptr);
	if (sConflictItems.Value.bin)
		s_free(nullptr, sConflictItems.Value.bin->__ptr);
	s_free(nullptr, sConflictItems.Value.bin);
	return hr;
}

HRESULT WSMessageStreamImporter::StartTransfer(WSMessageStreamSink **lppSink)
{
	HRESULT hr;
	KC::object_ptr<WSMessageStreamSink> ptrSink;
	
	if (!m_threadPool.dispatch(this))
		return MAPI_E_CALL_FAILED;

	hr = WSMessageStreamSink::Create(&m_fifoBuffer, m_ulTimeout, this, &~ptrSink);
	if (hr != hrSuccess) {
		m_fifoBuffer.Close(ECFifoBuffer::cfWrite);
		return hr;
	}

	AddChild(ptrSink);
	*lppSink = ptrSink.release();
	return hrSuccess;
}

HRESULT WSMessageStreamImporter::GetAsyncResult(HRESULT *lphrResult)
{
	if (lphrResult == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (!wait(m_ulTimeout))
		return MAPI_E_TIMEOUT;
	*lphrResult = m_hr;
	return hrSuccess;
}

WSMessageStreamImporter::WSMessageStreamImporter(ULONG ulFlags, ULONG ulSyncId, const entryId &sEntryId, const entryId &sFolderEntryId, bool bNewMessage, const propVal &sConflictItems, WSTransport *lpTransport, ULONG ulBufferSize, ULONG ulTimeout)
: m_ulFlags(ulFlags)
, m_ulSyncId(ulSyncId)
, m_sEntryId(sEntryId)
, m_sFolderEntryId(sFolderEntryId)
, m_bNewMessage(bNewMessage)
, m_sConflictItems(sConflictItems)
, m_ptrTransport(lpTransport, true)
, m_fifoBuffer(ulBufferSize)
, m_threadPool(1)
, m_ulTimeout(ulTimeout)
{ 
}

WSMessageStreamImporter::~WSMessageStreamImporter()
{ 
	s_free(nullptr, m_sEntryId.__ptr);
	s_free(nullptr, m_sFolderEntryId.__ptr);
	if (m_sConflictItems.Value.bin)
		s_free(nullptr, m_sConflictItems.Value.bin->__ptr);
	s_free(nullptr, m_sConflictItems.Value.bin);
}

void WSMessageStreamImporter::run()
{
	unsigned int ulResult = 0;
	struct xsd__Binary sStreamData;
	struct soap *lpSoap = m_ptrTransport->m_lpCmd->soap;
	propVal *lpsConflictItems = NULL;

	if (m_sConflictItems.ulPropTag != 0)
		lpsConflictItems = &m_sConflictItems;

	sStreamData.xop__Include.__ptr = (unsigned char*)this;
	sStreamData.xop__Include.type = const_cast<char *>("application/binary");

	m_ptrTransport->LockSoap();

	soap_set_omode(lpSoap, SOAP_ENC_MTOM | SOAP_IO_CHUNK);	
    lpSoap->mode &= ~SOAP_XML_TREE;
    lpSoap->omode &= ~SOAP_XML_TREE;
	lpSoap->fmimereadopen = &StaticMTOMReadOpen;
	lpSoap->fmimeread = &StaticMTOMRead;
	lpSoap->fmimereadclose = &StaticMTOMReadClose;

	m_hr = hrSuccess;
	if (m_ptrTransport->m_lpCmd->importMessageFromStream(m_ptrTransport->m_ecSessionId,
	    m_ulFlags, m_ulSyncId, m_sFolderEntryId, m_sEntryId, m_bNewMessage,
	    lpsConflictItems, sStreamData, &ulResult) != SOAP_OK)
		m_hr = MAPI_E_NETWORK_ERROR;
	else if (m_hr == hrSuccess) // Could be set from callback
		m_hr = kcerr_to_mapierr(ulResult, MAPI_E_NOT_FOUND);

	m_ptrTransport->UnLockSoap();
}

void* WSMessageStreamImporter::StaticMTOMReadOpen(struct soap *soap, void *handle, const char *id, const char *type, const char *description)
{
	return static_cast<WSMessageStreamImporter *>(handle)->MTOMReadOpen(soap, handle, id, type, description);
}

size_t WSMessageStreamImporter::StaticMTOMRead(struct soap *soap, void *handle, char *buf, size_t len)
{
	return static_cast<WSMessageStreamImporter *>(handle)->MTOMRead(soap, handle, buf, len);
}

void WSMessageStreamImporter::StaticMTOMReadClose(struct soap *soap, void *handle)
{
	static_cast<WSMessageStreamImporter *>(handle)->MTOMReadClose(soap, handle);
}

void* WSMessageStreamImporter::MTOMReadOpen(struct soap* /*soap*/, void *handle, const char* /*id*/, const char* /*type*/, const char* /*description*/)
{
	return handle;
}

size_t WSMessageStreamImporter::MTOMRead(struct soap* soap, void* /*handle*/, char *buf, size_t len)
{
	ECRESULT er = erSuccess;
	ECFifoBuffer::size_type cbRead = 0;

	er = m_fifoBuffer.Read(buf, len, 0, &cbRead);
	if (er != erSuccess) {
		m_hr = kcerr_to_mapierr(er);
		return 0;
	}

	return cbRead;
}

void WSMessageStreamImporter::MTOMReadClose(struct soap* /*soap*/, void* /*handle*/)
{
	m_fifoBuffer.Close(ECFifoBuffer::cfRead);
}
