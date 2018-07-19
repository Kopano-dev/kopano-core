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
#include <memory>
#include "mapiAttachment.h"
#include <vmime/contentDispositionField.hpp>
#include <vmime/contentTypeField.hpp>
#include <vmime/messageId.hpp>

namespace KC {

mapiAttachment::mapiAttachment(vmime::shared_ptr<const vmime::contentHandler> data,
    const vmime::encoding &enc, const vmime::mediaType &type,
    const std::string &contentid, const vmime::word &filename,
    const vmime::text &desc, const vmime::word &name) :
	defaultAttachment(data, enc, type, desc, name),
	m_filename(filename), m_contentid(contentid)
{}

void mapiAttachment::addCharset(vmime::charset ch) {
	m_hasCharset = true;
	m_charset = ch;
}

void mapiAttachment::generatePart(vmime::shared_ptr<vmime::bodyPart> part) const
{
	vmime::defaultAttachment::generatePart(part);
	vmime::dynamicCast<vmime::contentDispositionField>(part->getHeader()->ContentDisposition())->setFilename(m_filename);
	auto ctf = vmime::dynamicCast<vmime::contentTypeField>(part->getHeader()->ContentType());
	ctf->getParameter("name")->setValue(m_filename);
	if (m_hasCharset)
		ctf->setCharset(vmime::charset(m_charset));
	if (m_contentid != "")
		// Field is created when accessed through ContentId, so don't create if we have no
		// content-id to set
		part->getHeader()->ContentId()->setValue(vmime::messageId("<" + m_contentid + ">"));
}

} /* namespace */
