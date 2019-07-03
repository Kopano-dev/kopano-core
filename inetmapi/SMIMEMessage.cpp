/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <vmime/message.hpp>
#include "SMIMEMessage.h"

namespace KC {

void SMIMEMessage::generateImpl(const vmime::generationContext &ctx,
    vmime::utility::outputStream &os, size_t curLinePos,
    size_t *newLinePos) const
{
	if (m_body.empty()) {
		// Normal generation
		vmime::bodyPart::generateImpl(ctx, os, curLinePos, newLinePos);
		return;
	}
        // Generate headers
        getHeader()->generate(ctx, os);

        // Concatenate S/MIME body without CRLF since it also contains some additional headers
        os << m_body;
    	if (newLinePos)
	    	*newLinePos = 0;
}

} /* namespace */
