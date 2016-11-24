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

#ifndef IECSPOOLER_H
#define IECSPOOLER_H

#include <mapidefs.h>

namespace KC {

// This is our special spooler interface

class IECSpooler : public IUnknown {
public:
	// Gets an IMAPITable containing all the outgoing messages on the server
	virtual HRESULT __stdcall GetMasterOutgoingTable(ULONG ulFlags, IMAPITable **lppTable) = 0;

	// Removes a message from the master outgoing table
	virtual HRESULT __stdcall DeleteFromMasterOutgoingTable(ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG ulFlags) = 0;
};

} /* namespace */

#endif
