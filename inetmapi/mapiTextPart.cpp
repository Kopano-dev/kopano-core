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

#include "mapiTextPart.h"
#include "vmime/exception.hpp"

#include "vmime/contentTypeField.hpp"
#include "vmime/contentDisposition.hpp"
#include "vmime/contentDispositionField.hpp"
#include "vmime/text.hpp"

#include "vmime/emptyContentHandler.hpp"
#include "vmime/stringContentHandler.hpp"

namespace vmime
{

	/** Create a new object and return a reference to it.
	 * vmime only goes as far as p4... 
	 * @return reference to the new object
	 */
	template <class T, class P0, class P1, class P2, class P3, class P4, class P5>
	static ref <T> create(const P0& p0, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5) { 
		return ref <T>::fromPtr(new T(p0, p1, p2, p3, p4, p5));
	}
                                            
                                            

mapiTextPart::mapiTextPart()
	: m_plainText(vmime::create <emptyContentHandler>()),
	  m_text(vmime::create <emptyContentHandler>()),
	  m_otherText(vmime::create <emptyContentHandler>())
{
	m_bHaveOtherCharset = false;
}

const mediaType mapiTextPart::getType() const
{
	// TODO: fixme?
	if (m_text->isEmpty())
		return (mediaType(mediaTypes::TEXT, mediaTypes::TEXT_PLAIN));
	else
		return (mediaType(mediaTypes::TEXT, mediaTypes::TEXT_HTML));
}

int mapiTextPart::getPartCount() const
{
	int count = 0;
	if (!m_plainText->isEmpty())
		++count;
	if (!m_text->isEmpty())
		++count;
	if (!m_otherText->isEmpty())
		++count;
	return count;
}

void mapiTextPart::generateIn(ref <bodyPart> /* message */, ref <bodyPart> parent) const
{
	// Plain text
	if (!m_plainText->isEmpty())
	{
		// -- Create a new part
		ref <bodyPart> part = vmime::create <bodyPart>();
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
	ref <bodyPart> htmlPart = vmime::create <bodyPart>();

	// -- Set contents
	htmlPart->getBody()->setContents(m_text,
									 mediaType(mediaTypes::TEXT, mediaTypes::TEXT_HTML), m_charset,
									 encoding::decide(m_text, m_charset, encoding::USAGE_TEXT));

	// Handle the case we have embedded objects
	if (!m_objects.empty())
	{
		// Create a "multipart/related" body part
		ref <bodyPart> relPart = vmime::create <bodyPart>();
		parent->getBody()->appendPart(relPart);

		relPart->getHeader()->ContentType()->
			setValue(mediaType(mediaTypes::MULTIPART, mediaTypes::MULTIPART_RELATED));

		// Add the HTML part into this part
		relPart->getBody()->appendPart(htmlPart);

		// Also add objects into this part
		for (auto obj : m_objects) {
			ref <bodyPart> objPart = vmime::create <bodyPart>();
			relPart->getBody()->appendPart(objPart);

			std::string id = obj->getId();
			std::string name = obj->getName();

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
				objPart->getHeader()->ContentDisposition().dynamicCast<contentDispositionField>()->setFilename(name);

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
		ref <bodyPart> otherPart = vmime::create <bodyPart>();

		parent->getBody()->appendPart(otherPart);

		// used by ical
		if (!m_otherMethod.empty())
		{
			vmime::ref<vmime::parameter> p = vmime::create<parameter>("method");
			p->parse(m_otherMethod);
			otherPart->getHeader()->ContentType().dynamicCast<contentTypeField>()->appendParameter(p);
		}

		// -- Set contents
		otherPart->getBody()->setContents(m_otherText, m_otherMediaType, m_bHaveOtherCharset ? m_otherCharset : m_charset, m_otherEncoding);
	}
}

void mapiTextPart::findEmbeddedParts(const bodyPart& part,
	std::vector <ref <const bodyPart> >& cidParts, std::vector <ref <const bodyPart> >& locParts)
{
	for (int i = 0 ; i < part.getBody()->getPartCount() ; ++i)
	{
		ref <const bodyPart> p = part.getBody()->getPartAt(i);

		// For a part to be an embedded object, it must have a
		// Content-Id field or a Content-Location field.
		try
		{
			p->getHeader()->findField(fields::CONTENT_ID);
			cidParts.push_back(p);
		}
		catch (exceptions::no_such_field)
		{
			// No "Content-id" field.
		}

		try
		{
			p->getHeader()->findField(fields::CONTENT_LOCATION);
			locParts.push_back(p);
		}
		catch (exceptions::no_such_field)
		{
			// No "Content-Location" field.
		}

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

	try
	{
		const ref <const headerField> ctf = part.getHeader()->ContentType();
		type = *ctf->getValue().dynamicCast <const mediaType>();
	}
	catch (exceptions::no_such_field)
	{
		// No "Content-type" field: assume "application/octet-stream".
	}

	m_objects.push_back(vmime::create <embeddedObject>
		(part.getBody()->getContents()->clone().dynamicCast <contentHandler>(),
		 part.getBody()->getEncoding(), id, type, string(), string()));
}

void mapiTextPart::parse(ref <const bodyPart> message, ref <const bodyPart> parent, ref <const bodyPart> textPart)
{
	// Search for possible embedded objects in the _whole_ message.
	std::vector <ref <const bodyPart> > cidParts;
	std::vector <ref <const bodyPart> > locParts;

	findEmbeddedParts(*message, cidParts, locParts);

	// Extract HTML text
	std::ostringstream oss;
	utility::outputStreamAdapter adapter(oss);

	textPart->getBody()->getContents()->extract(adapter);

	const string data = oss.str();

	m_text = textPart->getBody()->getContents()->clone();

	try
	{
		const ref <const contentTypeField> ctf =
			textPart->getHeader()->findField(fields::CONTENT_TYPE).dynamicCast <contentTypeField>();

		m_charset = ctf->getCharset();
	}
	catch (exceptions::no_such_field)
	{
		// No "Content-type" field.
	}
	catch (exceptions::no_such_parameter)
	{
		// No "charset" parameter.
	}

	// Extract embedded objects. The algorithm is quite simple: for each previously
	// found inline part, we check if its CID/Location is contained in the HTML text.
	for (const auto &part : cidParts) {
		const ref <const headerField> midField =
			part->getHeader()->findField(fields::CONTENT_ID);

		const messageId mid = *midField->getValue().dynamicCast <const messageId>();

		if (data.find("CID:" + mid.getId()) != string::npos ||
		    data.find("cid:" + mid.getId()) != string::npos)
			// This part is referenced in the HTML text.
			// Add it to the embedded object list.
			addEmbeddedObject(*part, mid.getId());
	}

	for (const auto &part : locParts) {
		const ref <const headerField> locField =
			part->getHeader()->findField(fields::CONTENT_LOCATION);

		const text loc = *locField->getValue().dynamicCast <const text>();
		const string locStr = loc.getWholeBuffer();

		if (data.find(locStr) != string::npos)
			// This part is referenced in the HTML text.
			// Add it to the embedded object list.
			addEmbeddedObject(*part, locStr);
	}

	// Extract plain text, if any.
	if (!findPlainTextPart(*message, *parent, *textPart))
	{
		m_plainText = vmime::create <emptyContentHandler>();
	}
}

bool mapiTextPart::findPlainTextPart(const bodyPart& part, const bodyPart& parent, const bodyPart& textPart)
{
	// We search for the nearest "multipart/alternative" part.
	try
	{
		const ref <const headerField> ctf =
			part.getHeader()->findField(fields::CONTENT_TYPE);

		const mediaType type = *ctf->getValue().dynamicCast <const mediaType>();

		if (type.getType() == mediaTypes::MULTIPART &&
		    type.getSubType() == mediaTypes::MULTIPART_ALTERNATIVE)
		{
			ref <const bodyPart> foundPart = NULL;

			for (int i = 0 ; i < part.getBody()->getPartCount() ; ++i)
			{
				const ref <const bodyPart> p = part.getBody()->getPartAt(i);

				if (p == &parent ||     // if "text/html" is in "multipart/related"
				    p == &textPart)     // if not...
				{
					foundPart = p;
				}
			}

			if (foundPart)
			{
				bool found = false;

				// Now, search for the alternative plain text part
				for (int i = 0 ; !found && i < part.getBody()->getPartCount() ; ++i)
				{
					const ref <const bodyPart> p = part.getBody()->getPartAt(i);

					try
					{
						const ref <const headerField> ctf =
							p->getHeader()->findField(fields::CONTENT_TYPE);

						const mediaType type = *ctf->getValue().dynamicCast <const mediaType>();

						if (type.getType() == mediaTypes::TEXT &&
						    type.getSubType() == mediaTypes::TEXT_PLAIN)
						{
							m_plainText = p->getBody()->getContents()->clone();
							found = true;
						}
					}
					catch (exceptions::no_such_field)
					{
						// No "Content-type" field.
					}
				}

				// If we don't have found the plain text part here, it means that
				// it does not exist (the MUA which built this message probably
				// did not include it...).
				return found;
			}
		}
	}
	catch (exceptions::no_such_field)
	{
		// No "Content-type" field.
	}

	bool found = false;

	for (int i = 0 ; !found && i < part.getBody()->getPartCount() ; ++i)
	{
		found = findPlainTextPart(*part.getBody()->getPartAt(i), parent, textPart);
	}

	return found;
}

void mapiTextPart::setCharset(const charset& ch)
{
	m_charset = ch;
}

void mapiTextPart::setPlainText(ref <contentHandler> plainText)
{
	m_plainText = plainText->clone();
}

void mapiTextPart::setOtherText(ref <contentHandler> otherText)
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

void mapiTextPart::setText(ref <contentHandler> text)
{
	m_text = text->clone();
}

const ref <const mapiTextPart::embeddedObject> mapiTextPart::findObject(const string& id_) const
{
	const string id = cleanId(id_);

	for (const auto &obj : m_objects)
		if (obj->getId() == id)
			return obj;
	throw exceptions::no_object_found();
}

bool mapiTextPart::hasObject(const string& id_) const
{
	const string id = cleanId(id_);

	for (const auto &obj : m_objects)
		if (obj->getId() == id)
			return true;
	return false;
}

const string mapiTextPart::addObject(ref <contentHandler> data,
	const encoding& enc, const mediaType& type)
{
	const messageId mid(messageId::generateId());
	const string id = mid.getId();

	return "CID:" + addObject(data, enc, type, id);
}

const string mapiTextPart::addObject(ref <contentHandler> data, const mediaType& type)
{
	return addObject(data, encoding::decide(data), type);
}

const string mapiTextPart::addObject(const string& data, const mediaType& type)
{
	ref <stringContentHandler> cts = vmime::create <stringContentHandler>(data);
	return addObject(cts, encoding::decide(cts), type);
}

const string mapiTextPart::addObject(ref <contentHandler> data, const encoding& enc, const mediaType& type, const string& id, const string& name, const string& loc)
{
	m_objects.push_back(vmime::create <embeddedObject>(data, enc, id, type, name, loc));
	return (id);
}

// static
const string mapiTextPart::cleanId(const string& id)
{
	if (id.length() >= 4 &&
	    (id[0] == 'c' || id[0] == 'C') &&
	    (id[1] == 'i' || id[1] == 'I') &&
	    (id[2] == 'd' || id[2] == 'D') &&
	    id[3] == ':')
	{
		return id.substr(4);
	}
	else
	{
		return id;
	}
}

//
// mapiTextPart::embeddedObject
//

mapiTextPart::embeddedObject::embeddedObject
	(ref <contentHandler> data, const encoding& enc,
	 const string& id, const mediaType& type, const string& name, const string& loc)
	: m_data(data->clone().dynamicCast <contentHandler>()),
	  m_encoding(enc), m_id(id), m_type(type), m_name(name), m_loc(loc)
{
}

} // vmime
