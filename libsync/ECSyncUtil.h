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

#ifndef ECSYNCUTIL_H
#define ECSYNCUTIL_H

#include <string>
#include <set>
#include <kopano/zcdefs.h>
#include <mapidefs.h>

namespace KC {

typedef std::set<std::pair<unsigned int, std::string> > PROCESSEDCHANGESSET;
extern HRESULT HrDecodeSyncStateStream(LPSTREAM, ULONG *sync_id, ULONG *change_id, PROCESSEDCHANGESSET * = nullptr);
HRESULT ResetStream(LPSTREAM lpStream);
HRESULT CreateNullStatusStream(LPSTREAM *lppStream);

} /* namespace */

#endif // ndef ECSYNCUTIL_H
