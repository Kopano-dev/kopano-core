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

// -*- Mode: C++; -*-
#ifndef MAPIATTACHMENT_H
#define MAPIATTACHMENT_H

#include <kopano/zcdefs.h>
#include <vmime/defaultAttachment.hpp>
#include <string>

namespace KC {

class mapiAttachment _kc_final : public vmime::defaultAttachment {
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
