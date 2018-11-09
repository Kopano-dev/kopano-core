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

#include "inputStreamMAPIAdapter.h"

inputStreamMAPIAdapter::inputStreamMAPIAdapter(IStream *lpStream)
{
	this->lpStream = lpStream;
	if (lpStream)
		lpStream->AddRef();
	this->ateof = false;
}

inputStreamMAPIAdapter::~inputStreamMAPIAdapter()
{
	if (lpStream)
		lpStream->Release();
}

size_t inputStreamMAPIAdapter::read(unsigned char *data, size_t count)
{
	ULONG ulSize = 0;

	lpStream->Read(data, count, &ulSize);
	if (ulSize != count)
		this->ateof = true;

	return ulSize;
}

void inputStreamMAPIAdapter::reset()
{
	LARGE_INTEGER move;

	move.QuadPart = 0;

	lpStream->Seek(move, SEEK_SET, NULL);

	this->ateof = false;
}

size_t inputStreamMAPIAdapter::skip(size_t count)
{
	ULARGE_INTEGER ulSize;
	LARGE_INTEGER move;

	move.QuadPart = count;

	lpStream->Seek(move, SEEK_CUR, &ulSize);

	if (ulSize.QuadPart != count)
		this->ateof = true;

	return ulSize.QuadPart;
}
