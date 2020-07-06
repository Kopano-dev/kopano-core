/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <sys/types.h>
#include <vmime/dateTime.hpp>
#include <vmime/utility/inputStream.hpp>
#include <vmime/utility/outputStream.hpp>
#include <mapidefs.h>
#include <kopano/memory.hpp>

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

extern FILETIME vmimeDatetimeToFiletime(const vmime::datetime &dt);
extern vmime::datetime FiletimeTovmimeDatetime(const FILETIME &ft);
const char *ext_to_mime_type(const char *ext, const char *def = "application/octet-stream");
const char *mime_type_to_ext(const char *mime_type, const char *def = "txt");

} /* namespace */
