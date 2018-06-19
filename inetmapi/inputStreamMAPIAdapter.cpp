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

#include "inputStreamMAPIAdapter.h"

namespace KC {

inputStreamMAPIAdapter::inputStreamMAPIAdapter(IStream *s) :
	lpStream(s)
{}

size_t inputStreamMAPIAdapter::read(unsigned char *data, size_t count)
{
	ULONG ulSize = 0;

	if (lpStream->Read(data, count, &ulSize) != hrSuccess)
		return 0;
	if (ulSize != count)
		ateof = true;
	return ulSize;
}

void inputStreamMAPIAdapter::reset()
{
	LARGE_INTEGER move;

	move.QuadPart = 0;

	lpStream->Seek(move, SEEK_SET, NULL);
	ateof = false;
}

size_t inputStreamMAPIAdapter::skip(size_t count)
{
	ULARGE_INTEGER ulSize;
	LARGE_INTEGER move;

	move.QuadPart = count;

	lpStream->Seek(move, SEEK_CUR, &ulSize);

	if (ulSize.QuadPart != count)
		ateof = true;
	return ulSize.QuadPart;
}

outputStreamMAPIAdapter::outputStreamMAPIAdapter(IStream *s) :
	lpStream(s)
{}

void outputStreamMAPIAdapter::writeImpl(const vmime::byte_t *data, size_t count)
{
	lpStream->Write(data, count, NULL);
}

void outputStreamMAPIAdapter::flush()
{
    // just ignore the call, or call Commit() ?
}

} /* namespace */
