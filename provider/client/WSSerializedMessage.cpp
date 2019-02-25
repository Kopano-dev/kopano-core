/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/Util.h>
#include "WSSerializedMessage.h"

/**
 * @param[in]	lpSoap		The gSoap object from which the MTOM attachments must be obtained.
 * @param[in]	strStreamId	The expected stream id. Used to validate the MTOM attachment obtained from gSoap.
 * @param[in]	cbProps		The amount of properties returned from the original soap call.
 * @param[in]	lpProps		The properties returned from the original soap call. Only the pointer is stored, no
 *                          copy is made since our parent object will stay alive for our complete lifetime.
 */
WSSerializedMessage::WSSerializedMessage(soap *lpSoap,
    const std::string &strStreamId, ULONG cbProps, SPropValue *lpProps)
: m_lpSoap(lpSoap)
, m_strStreamId(strStreamId)
, m_cbProps(cbProps)
, m_lpProps(lpProps)
{
}

/**
 * Get a copy of the properties stored with this object.
 * @param[out]	lpcbProps	The amount of properties returned.
 * @param[out]	lppProps	A copy of the properties. The caller is responsible for deleting them.
 */
HRESULT WSSerializedMessage::GetProps(ULONG *lpcbProps, LPSPropValue *lppProps)
{
	if (lpcbProps == NULL || lppProps == NULL)
		return MAPI_E_INVALID_PARAMETER;
	return KC::Util::HrCopyPropertyArray(m_lpProps, m_cbProps, lppProps, lpcbProps);
}

/**
 * Copy the message stream to an IStream instance.
 * @param[in]	lpDestStream	The stream to write the data to.
 * @retval	MAPI_E_INVALID_PARAMETER	lpDestStream is NULL.
 */
HRESULT WSSerializedMessage::CopyData(LPSTREAM lpDestStream)
{
	if (lpDestStream == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = DoCopyData(lpDestStream);
	if (hr != hrSuccess)
		return hr;
	return lpDestStream->Commit(0);
}

/**
 * Read the data from the gSoap MTOM attachment, but discard it immediately.
 */
HRESULT WSSerializedMessage::DiscardData()
{
	// The data must be read from the network.
	return DoCopyData(NULL);
}

/**
 * Copy the message stream to an IStream instance.
 * @param[in]	lpDestStream	The stream to write the data to. If lpDestStream is
 *                              NULL, the data will be discarded.
 */
HRESULT WSSerializedMessage::DoCopyData(LPSTREAM lpDestStream)
{
	if (m_bUsed)
		return MAPI_E_UNCONFIGURED;
	m_bUsed = true;
	m_hr = hrSuccess;
	m_ptrDestStream.reset(lpDestStream);
	m_lpSoap->fmimewriteopen = StaticMTOMWriteOpen;
	m_lpSoap->fmimewrite = StaticMTOMWrite;
	m_lpSoap->fmimewriteclose = StaticMTOMWriteClose;
	soap_recv_mime_attachment(m_lpSoap, this);
	if (m_lpSoap->error != 0)
		return MAPI_E_NETWORK_ERROR;
	return m_hr;
}

void* WSSerializedMessage::StaticMTOMWriteOpen(struct soap *soap, void *handle, const char *id, const char *type, const char *description, enum soap_mime_encoding encoding)
{
	return static_cast<WSSerializedMessage *>(handle)->MTOMWriteOpen(soap, handle, id, type, description, encoding);
}

int WSSerializedMessage::StaticMTOMWrite(struct soap *soap, void *handle, const char *buf, size_t len)
{
	return static_cast<WSSerializedMessage *>(handle)->MTOMWrite(soap, handle, buf, len);
}

void WSSerializedMessage::StaticMTOMWriteClose(struct soap *soap, void *handle)
{
	static_cast<WSSerializedMessage *>(handle)->MTOMWriteClose(soap, handle);
}

void* WSSerializedMessage::MTOMWriteOpen(struct soap *soap, void *handle, const char *id, const char* /*type*/, const char* /*description*/, enum soap_mime_encoding encoding)
{
	if (encoding != SOAP_MIME_BINARY || id == NULL ||
	    m_strStreamId.compare(id) != 0) {
		soap->error = SOAP_ERR;
		m_hr = MAPI_E_INVALID_TYPE;
		m_ptrDestStream.reset();
	}
	return handle;
}

int WSSerializedMessage::MTOMWrite(struct soap *soap, void* /*handle*/, const char *buf, size_t len)
{
	ULONG cbWritten = 0;

	if (!m_ptrDestStream)
		return SOAP_OK;
	auto hr = m_ptrDestStream->Write(buf, static_cast<unsigned int>(len), &cbWritten);
	if (hr == hrSuccess)
		return SOAP_OK;
	soap->error = SOAP_ERR;
	m_hr = hr;
	m_ptrDestStream.reset();
	// @todo: Should we check if everything was written?
	return SOAP_OK;
}

void WSSerializedMessage::MTOMWriteClose(struct soap *soap, void *handle)
{
	m_ptrDestStream.reset();
}
