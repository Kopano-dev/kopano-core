/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef INPUT_STREAM_MAPI_ADAPTER_H
#define INPUT_STREAM_MAPI_ADAPTER_H

#include <mapidefs.h>
#include <sys/types.h>
#include <kopano/memory.hpp>
#include <vmime/utility/inputStream.hpp>
#include <vmime/utility/outputStream.hpp>

namespace KC {

class inputStreamMAPIAdapter final : public vmime::utility::inputStream {
public:
	inputStreamMAPIAdapter(IStream *lpStream);
	virtual size_t read(vmime::byte_t *, size_t) override;
	virtual size_t skip(size_t) override;
	virtual void reset() override;
	virtual bool eof() const override { return ateof; }

private:
	bool ateof = false;
	object_ptr<IStream> lpStream;
};

class outputStreamMAPIAdapter final : public vmime::utility::outputStream {
	public:
	outputStreamMAPIAdapter(IStream *);
	virtual void writeImpl(const unsigned char *, const size_t) override;
	virtual void flush() override;

	private:
	object_ptr<IStream> lpStream;
};

} /* namespace */

#endif
