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

#ifndef OUTPUT_STREAM_MAPI_ADAPTER_H
#define OUTPUT_STREAM_MAPI_ADAPTER_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <sys/types.h>
#include <vmime/utility/outputStream.hpp>

class outputStreamMAPIAdapter : public vmime::utility::outputStream {
public:
	outputStreamMAPIAdapter(IStream *lpStream);
	virtual ~outputStreamMAPIAdapter();
	virtual void writeImpl(const unsigned char *, const size_t);
	virtual void flush(void) _kc_override;

private:
	IStream *lpStream;
};

#endif
