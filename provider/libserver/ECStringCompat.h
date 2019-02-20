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
#include <mapidefs.h>

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

	/**
	 * Convert the data from UTF8 to either UTF8 ot WTF1252. If culClientCaps
	 * contains KOPANO_CAP_UNICODE, the output data will not be converted and
	 * will be in UTF8. Otherwise the data will be encoded in WTF1252.
	 *
	 *
	 * @param[in]	szIn	The input data in UTF8.
	 *
	 * @return		The input data encoded in UTF8 or WTF1252 depending on the
	 *				current convert setting.
	 */
	char *from_UTF8(soap *lpsoap, const char *szIn) const;

	/**
	 * Convert and copy the data from UTF8 to either UTF8 or WTF1252. If
	 * ulClientCaps contains KOPANO_CAP_UNICODE, the output data will not be
	 * converted and will be in UTF8. Otherwise the data will be encoded in
	 * WTF1252.
	 *
	 * This function is intended to be used when a copy should be made
	 * regardless of the convert setting. So when the value is going to be
	 * used as a result and gsoap is going to use it, this function should
	 * be used unless the original string is static.
	 *
	 * @param[in]	szIn	The input data in UTF8.
	 *
	 * @return		The input data encoded in UTF8 or WTF1252 depending on the
	 *				current convert setting.
	 */
	char *from_UTF8_cpy(soap *lpsoap, const char *szIn)const ;

	/**
	 * Returns the prop type needed for strings that are returned to the client.
	 *
	 * @retval	PT_STRING8 when the client does not support unicode.
	 * @retval	PT_UNICODE when the client does support unicoce.
	 */
	ULONG string_prop_type() const;
};

enum EncodingFixDirection { In, Out };

ECRESULT FixPropEncoding(struct soap *soap, const ECStringCompat &stringCompat, enum EncodingFixDirection type, struct propVal *lpProp, bool bNoTagUpdate = false);

// inlines
inline char *ECStringCompat::to_UTF8(soap *lpsoap, const char *szIn) const
{
	return const_cast<char *>(szIn);
}

inline char *ECStringCompat::from_UTF8(soap *lpsoap, const char *szIn) const
{
	return const_cast<char *>(szIn);
}

inline char *ECStringCompat::from_UTF8_cpy(soap *lpsoap, const char *szIn) const
{
	return s_strcpy(lpsoap, szIn);
}

inline ULONG ECStringCompat::string_prop_type() const
{
	return PT_UNICODE;
}

} /* namespace */

#endif // ndef ECSTRINGCOMPAT_H
