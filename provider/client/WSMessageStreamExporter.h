/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include <kopano/ECUnknown.h>
#include "soapStub.h"
#include <string>
#include <map>

class WSTransport;
class WSSerializedMessage;

/**
 * This object encapsulates a set of exported streams. It allows the user to request each individual stream. The
 * streams must be requested in the correct sequence.
 */
class WSMessageStreamExporter KC_FINAL_OPG : public KC::ECUnknown {
public:
	static HRESULT Create(ULONG ulOffset, ULONG ulCount, const messageStreamArray &streams, WSTransport *lpTransport, WSMessageStreamExporter **lppStreamExporter);
	bool IsDone() const;
	HRESULT GetSerializedMessage(ULONG ulIndex, WSSerializedMessage **lppSerializedMessage);

	protected:
	~WSMessageStreamExporter();

private:
	WSMessageStreamExporter(void) = default;
	// Inhibit copying
	WSMessageStreamExporter(const WSMessageStreamExporter &) = delete;
	WSMessageStreamExporter &operator=(const WSMessageStreamExporter &) = delete;

	struct StreamInfo {
		std::string	id;
		unsigned long	cbPropVals;
		KC::memory_ptr<SPropValue> ptrPropVals;
	};
	typedef std::map<ULONG, StreamInfo*>	StreamInfoMap;

	ULONG m_ulExpectedIndex = 0, m_ulMaxIndex = 0;
	KC::object_ptr<WSTransport> m_ptrTransport;
	StreamInfoMap	m_mapStreamInfo;
};

typedef KC::object_ptr<WSMessageStreamExporter> WSMessageStreamExporterPtr;
