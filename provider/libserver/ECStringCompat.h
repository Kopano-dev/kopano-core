/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSTRINGCOMPAT_H
#define ECSTRINGCOMPAT_H

#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include "kcore.hpp"
#include "SOAPUtils.h"

struct soap;

namespace KC {

/**
 * This class is responsible of converting string encodings from
 * UTF8 to WTF1252 and vice versa. WTF1252 is a string with characters
 * from the windows-1252 codepage encoded as UTF8. So the difference
 * with UTF8 is that is a string with true unicode code points.
 */
class ECStringCompat final {
public:
	~ECStringCompat() {}
};

enum EncodingFixDirection { In, Out };

extern ECRESULT FixPropEncoding(struct soap *, enum EncodingFixDirection, struct propVal *, bool no_tag_update = false);

} /* namespace */

#endif // ndef ECSTRINGCOMPAT_H
