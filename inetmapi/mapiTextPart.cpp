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

// based on htmlTextPart, but with additions
// we cannot use a class derived from htmlTextPart, since that class has alot of privates

//
// VMime library (http://www.vmime.org)
// Copyright (C) 2002-2009 Vincent Richard <vincent@vincent-richard.net>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 3 of
// the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Linking this library statically or dynamically with other modules is making
// a combined work based on this library.  Thus, the terms and conditions of
// the GNU General Public License cover the whole combination.
//

#include <memory>
#include "mapiTextPart.h"
#include <vmime/exception.hpp>
#include <vmime/contentTypeField.hpp>
#include <vmime/contentDisposition.hpp>
#include <vmime/contentDispositionField.hpp>
#include <vmime/text.hpp>
#include <vmime/emptyContentHandler.hpp>
#include <vmime/stringContentHandler.hpp>
#include <vmime/utility/outputStreamAdapter.hpp>

namespace vmime {

mapiTextPart::mapiTextPart()
	: m_plainText(vmime::make_shared<emptyContentHandler>()),
	  m_text(vmime::make_shared<emptyContentHandler>()),
	  m_otherText(vmime::make_shared<emptyContentHandler>())
{}

const mediaType mapiTextPart::getType() const
{
	// TODO: fixme?
	if (m_text->isEmpty())
		return (mediaType(mediaTypes::TEXT, mediaTypes::TEXT_PLAIN));
	else
		return (mediaType(mediaTypes::TEXT, mediaTypes::TEXT_HTML));
}

size_t mapiTextPart::getPartCount() const
{
	return !m_plainText->isEmpty() + !m_text->isEmpty() +
	       !m_otherText->isEmpty();
}

void mapiTextPart::generateIn(vmime::shared_ptr<bodyPart> /* message */,
    vmime::shared_ptr<bodyPart> parent) const
{
	// Plain text
	if (!m_plainText->isEmpty())
	{
		// -- Create a new part
		auto part = vmime::make_shared<bodyPart>();
		parent->getBody()->appendPart(part);

		// -- Set contents
		part->getBody()->setContents(m_plainText,
			mediaType(mediaTypes::TEXT, mediaTypes::TEXT_PLAIN), m_charset,
			encoding::decide(m_plainText, m_charset, encoding::USAGE_TEXT));
	}

	// HTML text
	// -- Create a new part
	if (!m_text->isEmpty())
	{
		auto htmlPart = vmime::make_shared<bodyPart>();

	// -- Set contents
	htmlPart->getBody()->setContents(m_text,
		mediaType(mediaTypes::TEXT, mediaTypes::TEXT_HTML), m_charset,
		encoding::decide(m_text, m_charset, encoding::USAGE_TEXT));

	// Handle the case we have embedded objects
	if (!m_objects.empty())
	{
		// Create a "multipart/related" body part
		auto relPart = vmime::make_shared<bodyPart>();
		parent->getBody()->appendPart(relPart);

		relPart->getHeader()->ContentType()->
			setValue(mediaType(mediaTypes::MULTIPART, mediaTypes::MULTIPART_RELATED));

		// Add the HTML part into this part
		relPart->getBody()->appendPart(htmlPart);

		// Also add objects into this part
		for (auto obj : m_objects) {
			auto objPart = vmime::make_shared<bodyPart>();
			relPart->getBody()->appendPart(objPart);

			std::string id = obj->getId(), name = obj->getName();
			if (id.substr(0, 4) == "CID:")
				id = id.substr(4);

			// throw an error when id and location are empty?

			objPart->getHeader()->ContentType()->setValue(obj->getType());
			if (!id.empty())
				objPart->getHeader()->ContentId()->setValue(messageId("<" + id + ">"));
			objPart->getHeader()->ContentDisposition()->setValue(contentDisposition(contentDispositionTypes::INLINE));
			objPart->getHeader()->ContentTransferEncoding()->setValue(obj->getEncoding());
			if (!obj->getLocation().empty())
				objPart->getHeader()->ContentLocation()->setValue(obj->getLocation());
			//encoding(encodingTypes::BASE64);

			if (!name.empty())
				vmime::dynamicCast<vmime::contentDispositionField>(objPart->getHeader()->ContentDisposition())->setFilename(vmime::word(name));

			objPart->getBody()->setContents(obj->getData()->clone());
		}
	}
	else
	{
		// Add the HTML part into the parent part
		parent->getBody()->appendPart(htmlPart);
	}
	} // if (html)

	// Other text
	if (!m_otherText->isEmpty())
	{
		// -- Create a new part
		auto otherPart = vmime::make_shared<bodyPart>();
		parent->getBody()->appendPart(otherPart);

		// used by ical
		if (!m_otherMethod.empty())
		{
			auto p = vmime::make_shared<vmime::parameter>("method");
			p->component::parse(m_otherMethod);
			vmime::dynamicCast<contentTypeField>(otherPart->getHeader()->ContentType())->appendParameter(p);
		}

		// -- Set contents
		otherPart->getBody()->setContents(m_otherText, m_otherMediaType, m_bHaveOtherCharset ? m_otherCharset : m_charset, m_otherEncoding);
	}
}

void mapiTextPart::findEmbeddedParts(const bodyPart& part,
	std::vector<vmime::shared_ptr<const bodyPart> > &cidParts,
	std::vector<vmime::shared_ptr<const bodyPart> > &locParts)
{
	for (size_t i = 0 ; i < part.getBody()->getPartCount() ; ++i)
	{
		auto p = part.getBody()->getPartAt(i);

		// For a part to be an embedded object, it must have a
		// Content-Id field or a Content-Location field.
		if (p->getHeader()->hasField(fields::CONTENT_ID))
			cidParts.emplace_back(p);
		if (p->getHeader()->hasField(fields::CONTENT_LOCATION))
			locParts.emplace_back(p);
		findEmbeddedParts(*p, cidParts, locParts);
	}
}

void mapiTextPart::addEmbeddedObject(const bodyPart& part, const string& id)
{
	// The object may already exists. This can happen if an object is
	// identified by both a Content-Id and a Content-Location. In this
	// case, there will be two embedded objects with two different IDs
	// but referencing the same content.

	mediaType type;

	if (part.getHeader()->hasField(fields::CONTENT_TYPE)) {
		auto ctf = part.getHeader()->ContentType();
		type = *vmime::dynamicCast<const vmime::mediaType>(ctf->getValue());
	}
	// Else assume "application/octet-stream".
	m_objects.emplace_back(vmime::make_shared<embeddedObject>(
		vmime::dynamicCast<vmime::contentHandler>(part.getBody()->getContents()->clone()),
		part.getBody()->getEncoding(), id, type, string(), string()));
}

void mapiTextPart::parse(vmime::shared_ptr<const vmime::bodyPart> message,
    vmime::shared_ptr<const vmime::bodyPart> parent,
    vmime::shared_ptr<const vmime::bodyPart> text_part)
{
	// Search for possible embedded objects in the _whole_ message.
	std::vector<vmime::shared_ptr<const vmime::bodyPart>> cidParts, locParts;
	findEmbeddedParts(*message, cidParts, locParts);

	// Extract HTML text
	std::ostringstream oss;
	utility::outputStreamAdapter adapter(oss);
	text_part->getBody()->getContents()->extract(adapter);
	const string data = oss.str();
	m_text = text_part->getBody()->getContents()->clone();
	if (text_part->getHeader()->hasField(fields::CONTENT_TYPE)) {
		auto ctf = vmime::dynamicCast<vmime::contentTypeField>(text_part->getHeader()->findField(fields::CONTENT_TYPE));
		m_charset = ctf->getCharset();
	}

	// Extract embedded objects. The algorithm is quite simple: for each previously
	// found inline part, we check if its CID/Location is contained in the HTML text.
	for (const auto &part : cidParts) {
		auto midField = part->getHeader()->findField(fields::CONTENT_ID);
		auto mid = *vmime::dynamicCast<const vmime::messageId>(midField->getValue());

		if (data.find("CID:" + mid.getId()) != string::npos ||
		    data.find("cid:" + mid.getId()) != string::npos)
			// This part is referenced in the HTML text.
			// Add it to the embedded object list.
			addEmbeddedObject(*part, mid.getId());
	}

	for (const auto &part : locParts) {
		auto locField = part->getHeader()->findField(fields::CONTENT_LOCATION);
		auto loc = *vmime::dynamicCast<const vmime::text>(locField->getValue());
		const string locStr = loc.getWholeBuffer();

		if (data.find(locStr) != string::npos)
			// This part is referenced in the HTML text.
			// Add it to the embedded object list.
			addEmbeddedObject(*part, locStr);
	}

	// Extract plain text, if any.
	if (!findPlainTextPart(*message, *parent, *text_part))
		m_plainText = vmime::make_shared<vmime::emptyContentHandler>();
}

bool mapiTextPart::findPlainTextPart(const bodyPart &part,
    const bodyPart &parent, const bodyPart &text_part)
{
	// We search for the nearest "multipart/alternative" part.
	if (part.getHeader()->hasField(fields::CONTENT_TYPE)) {
		auto ctf = part.getHeader()->findField(fields::CONTENT_TYPE);
		auto type = *vmime::dynamicCast<const vmime::mediaType>(ctf->getValue());

		if (type.getType() == mediaTypes::MULTIPART &&
		    type.getSubType() == mediaTypes::MULTIPART_ALTERNATIVE)
		{
			vmime::shared_ptr<const vmime::bodyPart> foundPart;

			for (size_t i = 0; i < part.getBody()->getPartCount(); ++i) {
				auto p = part.getBody()->getPartAt(i);
				if (p.get() == &parent ||     // if "text/html" is in "multipart/related"
				    p.get() == &text_part) /* if not... */
					foundPart = p;
			}

			if (foundPart)
			{
				bool found = false;

				// Now, search for the alternative plain text part
				for (size_t i = 0; !found && i < part.getBody()->getPartCount(); ++i) {
					auto p = part.getBody()->getPartAt(i);
					if (!p->getHeader()->hasField(fields::CONTENT_TYPE))
						continue;
					ctf = p->getHeader()->findField(fields::CONTENT_TYPE);
					type = *vmime::dynamicCast<const vmime::mediaType>(ctf->getValue());
					if (type.getType() == mediaTypes::TEXT &&
					    type.getSubType() == mediaTypes::TEXT_PLAIN)
					{
						m_plainText = p->getBody()->getContents()->clone();
						found = true;
					}
				}

				// If we don't have found the plain text part here, it means that
				// it does not exist (the MUA which built this message probably
				// did not include it...).
				return found;
			}
		}
	}

	bool found = false;

	for (size_t i = 0; !found && i < part.getBody()->getPartCount(); ++i)
		found = findPlainTextPart(*part.getBody()->getPartAt(i), parent, text_part);
	return found;
}

void mapiTextPart::setCharset(const charset& ch)
{
	m_charset = ch;
}

void mapiTextPart::setPlainText(vmime::shared_ptr<vmime::contentHandler> plainText)
{
	m_plainText = plainText->clone();
}

void mapiTextPart::setOtherText(vmime::shared_ptr<vmime::contentHandler> otherText)
{
	m_otherText = otherText->clone();
}

void mapiTextPart::setOtherContentType(const mediaType& type)
{
       m_otherMediaType = type;
}

void mapiTextPart::setOtherContentEncoding(const encoding& enc)
{
       m_otherEncoding = enc;
}

void mapiTextPart::setOtherMethod(const string& method)
{
       m_otherMethod = method;
}

void mapiTextPart::setOtherCharset(const charset& ch)
{
       m_otherCharset = ch;
       m_bHaveOtherCharset = true;
}

void mapiTextPart::setText(vmime::shared_ptr<vmime::contentHandler> text)
{
	m_text = text->clone();
}

vmime::shared_ptr<const mapiTextPart::embeddedObject>
mapiTextPart::findObject(const std::string &id_) const
{
	const string id = cleanId(id_);

	for (const auto &obj : m_objects)
		if (obj->getId() == id)
			return obj;
	return vmime::null;
}

bool mapiTextPart::hasObject(const string& id_) const
{
	const string id = cleanId(id_);

	for (const auto &obj : m_objects)
		if (obj->getId() == id)
			return true;
	return false;
}

const std::string
mapiTextPart::addObject(vmime::shared_ptr<vmime::contentHandler> data,
    const vmime::encoding &enc, const vmime::mediaType &type)
{
	const messageId mid(messageId::generateId());
	const string id = mid.getId();

	return "CID:" + addObject(data, enc, type, id);
}

const string mapiTextPart::addObject(vmime::shared_ptr<vmime::contentHandler> data,
    const vmime::mediaType &type)
{
	return addObject(data, encoding::decide(data), type);
}

const string mapiTextPart::addObject(const string& data, const mediaType& type)
{
	auto cts = vmime::make_shared<vmime::stringContentHandler>(data);
	return addObject(cts, encoding::decide(cts), type);
}

const string mapiTextPart::addObject(vmime::shared_ptr<vmime::contentHandler> data,
    const vmime::encoding &enc, const vmime::mediaType &type,
    const std::string &id, const std::string &name, const std::string &loc)
{
	m_objects.emplace_back(vmime::make_shared<embeddedObject>(data, enc, id, type, name, loc));
	return (id);
}

// static
const string mapiTextPart::cleanId(const string& id)
{
	auto s = reinterpret_cast<const unsigned char *>(id.c_str());
	if (id.length() >= 4 && tolower(s[0]) == 'c' &&
	    tolower(s[1]) == 'i' && tolower(s[2]) == 'd' && s[3] == ':')
		return id.substr(4);
	return id;
}

//
// mapiTextPart::embeddedObject
//

mapiTextPart::embeddedObject::embeddedObject(vmime::shared_ptr<vmime::contentHandler> data,
    const vmime::encoding &enc, const std::string &id,
    const vmime::mediaType &type, const std::string &name,
    const std::string &loc) :
	m_data(vmime::dynamicCast<vmime::contentHandler>(data->clone())),
	m_encoding(enc), m_id(id), m_type(type), m_name(name), m_loc(loc)
{
}

} // vmime
