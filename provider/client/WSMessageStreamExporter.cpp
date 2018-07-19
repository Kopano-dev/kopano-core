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
#include <kopano/mapi_ptr.h>
#include "WSMessageStreamExporter.h"
#include "WSSerializedMessage.h"
#include "WSTransport.h"
#include <kopano/charset/convert.h>
#include "WSUtil.h"
#include "soapKCmdProxy.h"

/**
 * Create a WSMessageStreamExporter instance.
 * @param[in]	ulOffset	The offset that should be used to index the streams. The server returns [0:B-A), while the client would
 *                          expect [A-B). This offset makes sure GetSerializedObject can be called with an index from the expected
 *                          range.
 * @param[in]	ulCount		The number message streams that should be handled. The actual amount of streams returned from the server
 *                          could be less if those messages didn't exist anymore on the server. This makes sure the client can still
 *                          request those streams and an appropriate error can be returned.
 * @param[in]	streams		The streams (or actually the information about the streams).
 * @param[in]	lpTransport	Pointer to the parent transport. Used to get the streams from the network. This transport MUST be used 
 *                          exclusively by this WSMessageStreamExporter only.
 * @param[out]	lppStreamExporter	The new instance.
 */
HRESULT WSMessageStreamExporter::Create(ULONG ulOffset, ULONG ulCount, const messageStreamArray &streams, WSTransport *lpTransport, WSMessageStreamExporter **lppStreamExporter)
{
	convert_context converter;
	WSMessageStreamExporterPtr ptrStreamExporter(new(std::nothrow) WSMessageStreamExporter);
	if (ptrStreamExporter == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	
	for (gsoap_size_t i = 0; i < streams.__size; ++i) {
		std::unique_ptr<StreamInfo> lpsi(new(std::nothrow) StreamInfo);
		if (lpsi == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		lpsi->id.assign(streams.__ptr[i].sStreamData.xop__Include.id);
		auto hr = MAPIAllocateBuffer(streams.__ptr[i].sPropVals.__size * sizeof(SPropValue), &~lpsi->ptrPropVals);
		if (hr != hrSuccess)
			return hr;
		for (gsoap_size_t j = 0; j < streams.__ptr[i].sPropVals.__size; ++j) {
			hr = CopySOAPPropValToMAPIPropVal(&lpsi->ptrPropVals[j], &streams.__ptr[i].sPropVals.__ptr[j], lpsi->ptrPropVals, &converter);
			if (hr != hrSuccess)
				return hr;
		}
		lpsi->cbPropVals = streams.__ptr[i].sPropVals.__size;

		ptrStreamExporter->m_mapStreamInfo[streams.__ptr[i].ulStep + ulOffset] = lpsi.release();
	}

	ptrStreamExporter->m_ulExpectedIndex = ulOffset;
	ptrStreamExporter->m_ulMaxIndex = ulOffset + ulCount;
	ptrStreamExporter->m_ptrTransport.reset(lpTransport);

	*lppStreamExporter = ptrStreamExporter.release();
	return hrSuccess;
}

/**
 * Check if any more streams are available from the exporter.
 */
bool WSMessageStreamExporter::IsDone() const
{
	assert(m_ulExpectedIndex <= m_ulMaxIndex);
	return m_ulExpectedIndex == m_ulMaxIndex;
}

/**
 * Request a serialized messages.
 * @param[in]	ulIndex		The index of the requested messages stream. The first time this must equal the ulOffset used during
 *                          construction. At each consecutive call this must be one higher than the previous call.
 * @param[out]	lppSerializedMessage	The requested stream.
 *
 * @retval	MAPI_E_INVALID_PARAMETER	ulIndex was not as expected or lppSerializedMessage is NULL
 * @retval	SYNC_E_OBJECT_DELETED		The message was deleted on the server.
 */
HRESULT WSMessageStreamExporter::GetSerializedMessage(ULONG ulIndex, WSSerializedMessage **lppSerializedMessage)
{
	if (ulIndex != m_ulExpectedIndex || lppSerializedMessage == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto iStreamInfo = m_mapStreamInfo.find(ulIndex);
	if (iStreamInfo == m_mapStreamInfo.cend()) {
		++m_ulExpectedIndex;
		return SYNC_E_OBJECT_DELETED;
	}
	KC::object_ptr<WSSerializedMessage> ptrMessage(new(std::nothrow) WSSerializedMessage(m_ptrTransport->m_lpCmd->soap, iStreamInfo->second->id, iStreamInfo->second->cbPropVals, iStreamInfo->second->ptrPropVals.get()));
	if (ptrMessage == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	AddChild(ptrMessage);

	++m_ulExpectedIndex;
	*lppSerializedMessage = ptrMessage.release();	
	return hrSuccess;
}

WSMessageStreamExporter::~WSMessageStreamExporter()
{
	if (m_ulMaxIndex != m_ulExpectedIndex &&
	    m_ptrTransport->m_lpCmd != nullptr)
		// We are halfway through a sync batch, so there is data waiting for us. Since we have our
		// own transport, we just drop the connection now, instead of letting the server output up to 254
		// messages that we'd just discard. Probably we will need to reconnect very soon after this call, to LogOff()
		// the transport's session, but that's better than receiving unwanted data.
		m_ptrTransport->m_lpCmd->soap->fshutdownsocket(m_ptrTransport->m_lpCmd->soap, m_ptrTransport->m_lpCmd->soap->socket, 0);

	for (const auto &i : m_mapStreamInfo)
		delete i.second;
}
