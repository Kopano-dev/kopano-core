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

#include <kopano/platform.h>

#include <vmime/message.hpp>
#include "SMIMEMessage.h"

SMIMEMessage::SMIMEMessage()
{
}

void SMIMEMessage::generateImpl(const vmime::generationContext &ctx,
    vmime::utility::outputStream &os, size_t curLinePos,
    size_t *newLinePos) const
{
    if(!m_body.empty()) {
        // Generate headers
        getHeader()->generate(ctx, os);

        // Concatenate S/MIME body without CRLF since it also contains some additional headers
        os << m_body;
        
    	if (newLinePos)
	    	*newLinePos = 0;
    } else {
        // Normal generation
        vmime::message::generate(ctx, os, curLinePos, newLinePos);
    }
}

void SMIMEMessage::setSMIMEBody(std::string &body)
{
    m_body = body;
}

