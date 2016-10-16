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

#ifndef ECSTRINGCOMPAT_H
#define ECSTRINGCOMPAT_H

#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include "kcore.hpp"
#include "SOAPUtils.h"
#include <mapidefs.h>

struct soap;
class convert_context;
struct user;
struct group;
struct company;

/**
 * This class is responsible of converting string encodings from
 * UTF8 to WTF1252 and vice versa. WTF1252 is a string with characters
 * from the windows-1252 codepage encoded as UTF8. So the difference
 * with UTF8 is that is a string with true unicode code points.
 */
class ECStringCompat _kc_final {
public:
	static char *WTF1252_to_WINDOWS1252(soap *lpsoap, const char *szWTF1252, convert_context *lpConverter = NULL);
	static char *WTF1252_to_UTF8(struct soap *lpsoap, const char *szWTF1252, convert_context *lpConverter = NULL);
	static char *UTF8_to_WTF1252(struct soap *lpsoap, const char *szUTF8, convert_context *lpConverter = NULL);

	ECStringCompat(bool fUnicode = false);
	ECStringCompat(const ECStringCompat &) = delete;
	~ECStringCompat();
	void operator=(const ECStringCompat &) = delete;
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

private:
	convert_context *m_lpConverter;
	bool m_fUnicode;
};



enum EncodingFixDirection { In, Out };

ECRESULT FixPropEncoding(struct soap *soap, const ECStringCompat &stringCompat, enum EncodingFixDirection type, struct propVal *lpProp, bool bNoTagUpdate = false);
ECRESULT FixRestrictionEncoding(struct soap *soap, const ECStringCompat &stringCompat, enum EncodingFixDirection type, struct restrictTable *lpRestrict);
ECRESULT FixRowSetEncoding(struct soap *soap, const ECStringCompat &stringCompat, enum EncodingFixDirection type, struct rowSet *lpRowSet);
ECRESULT FixUserEncoding(struct soap *soap, const ECStringCompat &stringCompat, enum EncodingFixDirection type, struct user *lpUser);
ECRESULT FixGroupEncoding(struct soap *soap, const ECStringCompat &stringCompat, enum EncodingFixDirection type, struct group *lpGroup);
ECRESULT FixCompanyEncoding(struct soap *soap, const ECStringCompat &stringCompat, enum EncodingFixDirection type, struct company *lpCompany);
ECRESULT FixNotificationsEncoding(struct soap *soap, const ECStringCompat &stringCompat, struct notificationArray *notifications);



// inlines
inline char *ECStringCompat::to_UTF8(soap *lpsoap, const char *szIn) const
{
	return m_fUnicode ? (char*)szIn : WTF1252_to_UTF8(lpsoap, szIn, m_lpConverter);
}

inline char *ECStringCompat::from_UTF8(soap *lpsoap, const char *szIn) const
{
	return m_fUnicode ? (char*)szIn : UTF8_to_WTF1252(lpsoap, szIn, m_lpConverter);
}

inline char *ECStringCompat::from_UTF8_cpy(soap *lpsoap, const char *szIn) const
{
	return m_fUnicode ? s_strcpy(lpsoap, szIn) : UTF8_to_WTF1252(lpsoap, szIn);
}

inline ULONG ECStringCompat::string_prop_type() const
{
	return m_fUnicode ? PT_UNICODE : PT_STRING8;
}

#endif // ndef ECSTRINGCOMPAT_H
