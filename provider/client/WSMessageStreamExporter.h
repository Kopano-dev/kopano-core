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

#ifndef WSMessageStreamExporter_INCLUDED
#define WSMessageStreamExporter_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include <kopano/ECUnknown.h>
#include "soapStub.h"
#include <string>
#include <map>

using namespace KC;
class WSTransport;
class WSSerializedMessage;

/**
 * This object encapsulates a set of exported streams. It allows the user to request each individual stream. The
 * streams must be requested in the correct sequence.
 */
class WSMessageStreamExporter _kc_final : public ECUnknown {
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
		SPropArrayPtr	ptrPropVals;
	};
	typedef std::map<ULONG, StreamInfo*>	StreamInfoMap;

	ULONG m_ulExpectedIndex = 0, m_ulMaxIndex = 0;
	KC::object_ptr<WSTransport> m_ptrTransport;
	StreamInfoMap	m_mapStreamInfo;
};

typedef KC::object_ptr<WSMessageStreamExporter> WSMessageStreamExporterPtr;

#endif // ndef ECMessageStreamExporter_INCLUDED
