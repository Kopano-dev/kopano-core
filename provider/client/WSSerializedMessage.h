/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef WSSerializedMessage_INCLUDED
#define WSSerializedMessage_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include "soapStub.h"
#include <string>

/**
 * This object represents one exported message stream. It is responsible for requesting the MTOM attachments from soap.
 */
class WSSerializedMessage _kc_final : public KC::ECUnknown {
public:
	WSSerializedMessage(soap *, const std::string &stream_id, ULONG nprops, SPropValue *props);
	HRESULT GetProps(ULONG *lpcbProps, LPSPropValue *lppProps);
	HRESULT CopyData(LPSTREAM lpDestStream);
	HRESULT DiscardData();

private:
	HRESULT DoCopyData(LPSTREAM lpDestStream);

	static void*	StaticMTOMWriteOpen(struct soap *soap, void *handle, const char *id, const char *type, const char *description, enum soap_mime_encoding encoding);
	static int		StaticMTOMWrite(struct soap *soap, void *handle, const char *buf, size_t len);
	static void		StaticMTOMWriteClose(struct soap *soap, void *handle);

	void*	MTOMWriteOpen(struct soap *soap, void *handle, const char *id, const char *type, const char *description, enum soap_mime_encoding encoding);
	int		MTOMWrite(struct soap *soap, void *handle, const char *buf, size_t len);
	void	MTOMWriteClose(struct soap *soap, void *handle);

	soap				*m_lpSoap;
	const std::string	m_strStreamId;
	ULONG				m_cbProps;
	LPSPropValue		m_lpProps;	//	Points to data from parent object.

	bool m_bUsed = false;
	KC::object_ptr<IStream> m_ptrDestStream;
	HRESULT m_hr = hrSuccess;
};

#endif // ndef WSSerializedMessage_INCLUDED
