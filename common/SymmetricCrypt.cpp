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
#include <kopano/base64.h>

#include <string>
#include <kopano/charset/convert.h>
#include <cassert>
#include "SymmetricCrypt.h"

/**
 * Check if the provided password is crypted.
 * 
 * Crypted passwords have the format "{N}:<crypted password>, with N being the encryption algorithm. 
 * Currently only algorithm number 1 and 2 are supported:
 * 1: base64-of-XOR-A5 of windows-1252 encoded data
 * 2: base64-of-XOR-A5 of UTF-8 encoded data
 * 
 * @param[in]	strCrypted
 * 					The string to test.
 * 
 * @return	boolean
 * @retval	true	The provided string was encrypted.
 * @retval	false 	The provided string was not encrypted.
 */
#ifdef _DEBUG
#define new DEBUG_NEW
#endif

bool SymmetricIsCrypted(const char *c)
{
	return strncmp(c, "{1}:", 4) == 0 || strncmp(c, "{2}:", 4) == 0;
}

/**
 * Check if the provided password is crypted.
 * 
 * Crypted passwords have the format "{N}:<crypted password>, with N being the encryption algorithm. 
 * Currently only algorithm number 1 and 2 are supported:
 * 1: base64-of-XOR-A5 of windows-1252 encoded data
 * 2: base64-of-XOR-A5 of UTF-8 encoded data
 * 
 * @param[in]	strCrypted
 * 					The wide character string to test.
 * 
 * @return	boolean
 * @retval	true	The provided string was encrypted.
 * @retval	false 	The provided string was not encrypted.
 */
bool SymmetricIsCrypted(const wchar_t *c)
{
	return wcsncmp(c, L"{1}:", 4) == 0 || wcsncmp(c, L"{2}:", 4) == 0;
}

/**
 * Decrypt the crypt data.
 * 
 * Depending on the N value, the password is decrypted using algorithm 1 or 2.
 * 
 * @param[in]	ulAlg
 * 					The number selecting the algorithm. (1 or 2)
 * @param[in]	strXORed
 * 					The binary data to decrypt.
 * 
 * @return	The decrypted password encoded in UTF-8.
 */
static std::string SymmetricDecryptBlob(unsigned int ulAlg, const std::string &strXORed)
{
	std::string strRaw = strXORed;
	size_t z = strRaw.size();
	
	assert(ulAlg == 1 || ulAlg == 2);

	for (unsigned int i = 0; i < z; ++i)
		strRaw[i] ^= 0xA5;
	
	// Check the encoding algorithm. If it equals 1, the raw data is windows-1252.
	// Otherwise, it must be 2, which means it is already UTF-8.
	if (ulAlg == 1)
		strRaw = convert_to<std::string>("UTF-8", strRaw, rawsize(strRaw), "WINDOWS-1252");
	
	return strRaw;
}

/**
 * Decrypt an encrypted password.
 * 
 * Depending on the N value, the password is decrypted using algorithm 1 or 2.
 * 
 * @param[in]	strCrypted
 * 					The UTF-8 encoded encrypted password to decrypt.
 * 
 * @return	THe decrypted password encoded in UTF-8.
 */
std::string SymmetricDecrypt(const char *strCrypted)
{
	if (!SymmetricIsCrypted(strCrypted))
		return "";
	// Length has been guaranteed to be >=4.
	return SymmetricDecryptBlob(strCrypted[1] - '0',
		base64_decode(convert_to<std::string>(strCrypted + 4)));
}

/**
 * Decrypt an encrypted password.
 * 
 * Depending on the N value, the password is decrypted using algorithm 1 or 2.
 * 
 * @param[in]	strCrypted
 * 					The wide character encrypted password to decrypt.
 * 
 * @return	THe decrypted password encoded in UTF-8.
 */
std::string SymmetricDecrypt(const wchar_t *wstrCrypted)
{
	if (!SymmetricIsCrypted(wstrCrypted))
		return "";
	// Length has been guaranteed to be >=4.
	return SymmetricDecryptBlob(wstrCrypted[1] - '0',
		base64_decode(convert_to<std::string>(wstrCrypted + 4)));
}
