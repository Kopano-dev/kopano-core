/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
#ifndef VMIME_MAPITEXTPART_HPP_INCLUDED
#define VMIME_MAPITEXTPART_HPP_INCLUDED

#include <memory>
#include <string>
#include <vmime/textPart.hpp>
#include <vmime/messageId.hpp>
#include <vmime/encoding.hpp>
#include <vmime/contentHandler.hpp>

namespace KC {

/** Text part of type 'text/html'.
  */
class mapiTextPart final : public vmime::textPart {
public:
	mapiTextPart();
	const vmime::mediaType getType() const override;
	const vmime::charset &getCharset() const override { return m_charset; }
	void setCharset(const vmime::charset &) override;

	/* plain text */
	vmime::shared_ptr<const vmime::contentHandler> getPlainText() const { return m_plainText; }
	void setPlainText(vmime::shared_ptr<vmime::contentHandler> plain_text);

	/* 'other' text */
	vmime::shared_ptr<const vmime::contentHandler> getOtherText() const { return m_otherText; }
	void setOtherText(vmime::shared_ptr<vmime::contentHandler> other_text);
	/* extra 'other' properties */
	void setOtherContentType(const vmime::mediaType &);
	void setOtherContentEncoding(const vmime::encoding &);
	void setOtherMethod(const std::string &);
	void setOtherCharset(const vmime::charset &);

	/* html + plain + 'other' text */
	const vmime::shared_ptr<const vmime::contentHandler> getText() const override { return m_text; }
	void setText(const vmime::shared_ptr<vmime::contentHandler> &) override;

	/** Embedded object (eg: image for &lt;IMG> tag).
	  */
	class embeddedObject final : public vmime::object {
	public:
		embeddedObject(vmime::shared_ptr<vmime::contentHandler> data, const vmime::encoding &enc, const std::string &id, const vmime::mediaType &, const std::string &name, const std::string &loc);

		/** Return data stored in this embedded object.
		  *
		  * @return stored data
		  */
		vmime::shared_ptr<const vmime::contentHandler> getData() const { return m_data; }

		/** Return the encoding used for data in this
		  * embedded object.
		  *
		  * @return data encoding
		  */
		const vmime::encoding &getEncoding(void) const { return m_encoding; }

		/** Return the identifier of this embedded object.
		  *
		  * @return object identifier
		  */
		const std::string &getId(void) const { return m_id; }

		/** Return the location (URL) of this embedded object.
		  *
		  * @return object location url
		  */
		const std::string &getLocation(void) const { return m_loc; }

		/** Return the content type of data stored in
		  * this embedded object.
		  *
		  * @return data type
		  */
		const vmime::mediaType &getType() const { return m_type; }

		/** Return the object name of this embedded object, if any
		 *
		 * @return object name
		 */
		const std::string &getName(void) const { return m_name; }

	private:
		vmime::shared_ptr<vmime::contentHandler> m_data;
		vmime::encoding m_encoding;
		std::string m_id;
		vmime::mediaType m_type;
		std::string m_name;
		std::string m_loc;
	};

	/** Test the existence of an embedded object given its identifier.
	  *
	  * @param id object identifier
	  * @return true if an object with this identifier exists,
	  * false otherwise
	  */
	bool hasObject(const std::string &id) const;

	/** Return the embedded object with the specified identifier.
	  *
	  * @throw exceptions::no_object_found() if no object has been found
	  * @param id object identifier
	  * @return embedded object with the specified identifier
	  */
	vmime::shared_ptr<const embeddedObject> findObject(const std::string &id) const;

	/** Return the number of embedded objects.
	  *
	  * @return number of embedded objects
	  */
	int getObjectCount(void) const { return m_objects.size(); }

	/** Return the embedded object at the specified position.
	  *
	  * @param pos position of the embedded object
	  * @return embedded object at position 'pos'
	  */
	vmime::shared_ptr<const embeddedObject> getObjectAt(int pos) const { return m_objects[pos]; }

	/** Embed an object and returns a string which identifies it.
	  * The returned identifier is suitable for use in the 'src' attribute
	  * of an <img> tag.
	  *
	  * \deprecated Use the addObject() methods which take a 'contentHandler'
	  * parameter type instead.
	  *
	  * @param data object data
	  * @param type data type
	  * @return an unique object identifier used to identify the new
	  * object among all other embedded objects
	  */
	const std::string addObject(const std::string &data, const vmime::mediaType &);

	/** Embed an object and returns a string which identifies it.
	  * The returned identifier is suitable for use in the 'src' attribute
	  * of an <img> tag.
	  *
	  * @param data object data
	  * @param type data type
	  * @return an unique object identifier used to identify the new
	  * object among all other embedded objects
	  */
	const std::string addObject(vmime::shared_ptr<vmime::contentHandler> data, const vmime::mediaType &);

	/** Embed an object and returns a string which identifies it.
	  * The returned identifier is suitable for use in the 'src' attribute
	  * of an <img> tag.
	  *
	  * @param data object data
	  * @param enc data encoding
	  * @param type data type
	  * @return an unique object identifier used to identify the new
	  * object among all other embedded objects
	  */
	const std::string addObject(vmime::shared_ptr<vmime::contentHandler> data, const vmime::encoding &, const vmime::mediaType &);

	/** Embed an object and returns a string which identifies it.
	 *
	 * @param data object data
	 * @param enc data encoding
	 * @param type data type
	 * @param id unique object identifier
	 * @param loc optional location url of object
	 * @param name filename of attachment
	 * @return an unique object identifier used to identify the new
	 * object among all other embedded objects
	 */
	const std::string addObject(vmime::shared_ptr<vmime::contentHandler> data, const vmime::encoding &, const vmime::mediaType &, const std::string &id, const std::string &name = {}, const std::string &loc = {});

	size_t getPartCount() const override;
	void generateIn(const vmime::shared_ptr<vmime::bodyPart> &message, const vmime::shared_ptr<vmime::bodyPart> &parent) const override;
	void parse(const vmime::shared_ptr<const vmime::bodyPart> &message, const vmime::shared_ptr<const vmime::bodyPart> &parent, const vmime::shared_ptr<const vmime::bodyPart> &text_part) override;

private:
	vmime::shared_ptr<vmime::contentHandler> m_plainText;
	vmime::shared_ptr<vmime::contentHandler> m_text; /* htmlText */
	vmime::charset m_charset;
	vmime::shared_ptr<vmime::contentHandler> m_otherText;
	vmime::mediaType m_otherMediaType;
	vmime::encoding m_otherEncoding;
	std::string m_otherMethod; /* ical special */
	vmime::charset m_otherCharset;
	bool m_bHaveOtherCharset = false;
	std::vector<vmime::shared_ptr<embeddedObject> > m_objects;

	void findEmbeddedParts(const vmime::bodyPart &, std::vector<vmime::shared_ptr<const vmime::bodyPart>> &cid_parts, std::vector<vmime::shared_ptr<const vmime::bodyPart>> &loc_parts);
	void addEmbeddedObject(const vmime::bodyPart &, const std::string &id);
	bool findPlainTextPart(const vmime::bodyPart &, const vmime::bodyPart &parent, const vmime::bodyPart &text_prat);
	static const std::string cleanId(const std::string &id);
};

} /* namespace */

#endif // VMIME_HTMLTEXTPART_HPP_INCLUDED
