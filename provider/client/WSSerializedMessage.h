/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/memory.hpp>
#include "soapStub.h"
#include <string>

/**
 * This object represents one exported message stream. It is responsible for requesting the MTOM attachments from soap.
 */
class WSSerializedMessage KC_FINAL_OPG : public KC::ECUnknown {
public:
	WSSerializedMessage(soap *, const std::string &stream_id, ULONG nprops, SPropValue *props);
	HRESULT GetProps(ULONG *lpcbProps, LPSPropValue *lppProps);
	HRESULT CopyData(IStream *dst);
	HRESULT DiscardData();

private:
	HRESULT DoCopyData(IStream *dst);
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
