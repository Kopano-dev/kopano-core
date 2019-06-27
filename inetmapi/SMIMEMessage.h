/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef SMIMEMESSAGE_H
#define SMIMEMESSAGE_H

#include <string>
#include <vmime/message.hpp>
#include <vmime/utility/stream.hpp>
#include <vmime/generationContext.hpp>

namespace KC {

/**
 * We are adding a bit of functionality to vmime::message here for S/MIME support.
 *
 * MAPI provides us with the actual body of a signed S/MIME message that looks like
 *
 * -----------------------
 * Content-Type: xxxx
 *
 * data
 * data
 * data
 * ...
 * -----------------------
 *
 * This class works just like a vmime::message instance, except that when then 'SMIMEBody' is set, it will
 * use that body (including some headers!) to generate the RFC 2822 message. All other methods are inherited
 * directly from vmime::message.
 *
 * Note that any other body data set will be override by the SMIMEBody.
 */
class SMIMEMessage final : public vmime::message {
public:
	void generateImpl(const vmime::generationContext &, vmime::utility::outputStream &, size_t cur_line_pos = 0, size_t *newline_pos = nullptr) const override;
	void setSMIMEBody(const char *body) { m_body = body; }
private:
    std::string m_body;
};

} /* namespace */

#endif
