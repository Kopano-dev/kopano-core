/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef WSMessageStreamExporter_INCLUDED
#define WSMessageStreamExporter_INCLUDED

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
class WSMessageStreamExporter final : public KC::ECUnknown {
public:
	static HRESULT Create(ULONG ulOffset, ULONG ulCount, const messageStreamArray &streams, WSTransport *lpTransport, WSMessageStreamExporter **lppStreamExporter);
	bool IsDone() const;
	HRESULT GetSerializedMessage(ULONG ulIndex, WSSerializedMessage **lppSerializedMessage);

private:
	WSMessageStreamExporter(void) = default;
	~WSMessageStreamExporter();
	// Inhibit copying
	WSMessageStreamExporter(const WSMessageStreamExporter &) = delete;
	WSMessageStreamExporter &operator=(const WSMessageStreamExporter &) = delete;

	struct StreamInfo {
		std::string	id;
		unsigned long	cbPropVals;
		KC::SPropArrayPtr ptrPropVals;
	};
	typedef std::map<ULONG, StreamInfo*>	StreamInfoMap;

	ULONG m_ulExpectedIndex = 0, m_ulMaxIndex = 0;
	KC::object_ptr<WSTransport> m_ptrTransport;
	StreamInfoMap	m_mapStreamInfo;
};

typedef KC::object_ptr<WSMessageStreamExporter> WSMessageStreamExporterPtr;

#endif // ndef ECMessageStreamExporter_INCLUDED
