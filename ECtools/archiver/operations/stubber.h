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

#ifndef stubber_INCLUDED
#define stubber_INCLUDED

#include <kopano/zcdefs.h>
#include "operations.h"

namespace KC { namespace operations {

/**
 * Performs the stub part of the archive oepration.
 */
class Stubber _kc_final : public ArchiveOperationBase {
public:
	Stubber(ECArchiverLogger *lpLogger, ULONG ulptStubbed, int ulAge, bool bProcessUnread);
	HRESULT ProcessEntry(LPMAPIFOLDER lpFolder, ULONG cProps, const LPSPropValue lpProps);
	HRESULT ProcessEntry(LPMESSAGE lpMessage);
	
private:
	ULONG m_ulptStubbed;
};

}} /* namespace */

#endif // ndef stubber_INCLUDED
