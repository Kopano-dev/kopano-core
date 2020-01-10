/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
	lpStream->Seek(move, STREAM_SEEK_SET, nullptr);
	ateof = false;
}

size_t inputStreamMAPIAdapter::skip(size_t count)
{
	ULARGE_INTEGER ulSize;
	LARGE_INTEGER move;

	move.QuadPart = count;
	lpStream->Seek(move, STREAM_SEEK_CUR, &ulSize);
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
