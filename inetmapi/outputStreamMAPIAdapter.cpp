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

#include <kopano/platform.h>

// Damn windows header defines max which break C++ header files
#undef max

#include "outputStreamMAPIAdapter.h"

outputStreamMAPIAdapter::outputStreamMAPIAdapter(IStream *lpStream)
{
	this->lpStream = lpStream;
	if(lpStream)
		lpStream->AddRef();
}

outputStreamMAPIAdapter::~outputStreamMAPIAdapter()
{
	if(lpStream)
		lpStream->Release();
}

void outputStreamMAPIAdapter::writeImpl(const vmime::byte_t *data, size_t count)
{
	lpStream->Write(data, count, NULL);
}

void outputStreamMAPIAdapter::flush()
{
    // just ignore the call, or call Commit() ?
}
