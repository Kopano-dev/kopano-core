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

#ifndef IECEXPORTCHANGES_H
#define IECEXPORTCHANGES_H

#include <edkmdb.h>

namespace KC {

class ECLogger;

class IECExportChanges : public IExchangeExportChanges {
public:
	virtual HRESULT __stdcall ConfigSelective(ULONG ulPropTag, LPENTRYLIST lpEntries, LPENTRYLIST lpParents, ULONG ulFlags, LPUNKNOWN lpCollector, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize) = 0;
	virtual HRESULT __stdcall GetChangeCount(ULONG *lpcChanges) = 0;
	virtual HRESULT __stdcall SetMessageInterface(REFIID refiid) = 0;
	virtual HRESULT __stdcall SetLogger(ECLogger *lpLogger) = 0;
};

} /* namespace */

#endif
