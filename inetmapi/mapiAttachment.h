/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
// -*- Mode: C++; -*-
#ifndef MAPIATTACHMENT_H
#define MAPIATTACHMENT_H

#include <vmime/defaultAttachment.hpp>
#include <string>

namespace KC {

class mapiAttachment final : public vmime::defaultAttachment {
public:
	mapiAttachment(vmime::shared_ptr<const vmime::contentHandler> data,
				   const vmime::encoding& enc,
				   const vmime::mediaType& type,
				   const std::string& contentid,
				   const vmime::word &filename,
				   const vmime::text& desc = vmime::NULL_TEXT,
				   const vmime::word& name = vmime::NULL_WORD);
	void addCharset(vmime::charset ch);

private:
	vmime::word m_filename;
	std::string m_contentid;
	bool m_hasCharset = false;
	vmime::charset m_charset;

	void generatePart(const vmime::shared_ptr<vmime::bodyPart> &) const override;
};

} /* namespace */

#endif
