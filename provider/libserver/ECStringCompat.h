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
	/**
	 * Convert the input data to true UTF8. If ulClientCaps contains
	 * KOPANO_CAP_UNICODE, the input data is expected to be in UTF8,
	 * so no conversion is needed. If convert is set to true, the input
	 * data is expected to be in WTF1252, and the data is converted
	 * accordingly.
	 *
	 * @param[in]	szIn	The input data.
	 *
	 * @return		The input data encoded in UTF8.
	 */
	char *to_UTF8(soap *lpsoap, const char *szIn) const;
};

enum EncodingFixDirection { In, Out };

ECRESULT FixPropEncoding(struct soap *soap, const ECStringCompat &stringCompat, enum EncodingFixDirection type, struct propVal *lpProp, bool bNoTagUpdate = false);

// inlines
inline char *ECStringCompat::to_UTF8(soap *lpsoap, const char *szIn) const
{
	return const_cast<char *>(szIn);
}

} /* namespace */

#endif // ndef ECSTRINGCOMPAT_H
